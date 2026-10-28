"""Bluetooth-Bruecke der Fernbedienungen (frueher eigene Integration esphome_ble_link, seit 2026-10-27 Teil von
ESPHomeRemote; s. esphome/esphomeremote-docs/esphomeremote-ha-link-ble.md).

Bietet je Remote einen lokalen Port (127.0.0.1:<port>) an. HAs ESPHome-Integration zeigt auf diesen Port.
Fuer jede neue API-Sitzung waehlt die Bruecke den Weg:
  - WLAN-Adresse der Remote erreichbar -> 1:1 per TCP weiterleiten
  - sonst -> durch den Bluetooth-Link (HAs Bluetooth-Proxys) in die Remote (components/ha_tunnel)
Meldet die Remote ueber den Link "WLAN weg", wird eine WLAN-Sitzung sofort beendet; die ESPHome-Integration
verbindet neu und landet dann im Tunnel.

Die Links setzt bruecke.py aus der Remote-Liste (Name, Port, WLAN-Adresse, OTA-Port).
"""
from __future__ import annotations

import asyncio
import logging
import socket
import struct
import time

from bleak.backends.device import BLEDevice
import bleak
from bleak_retry_connector import establish_connection

from homeassistant.components import bluetooth
from homeassistant.const import CONF_NAME, CONF_PORT, EVENT_HOMEASSISTANT_STOP
from homeassistant.config_entries import ConfigEntryState
from homeassistant.core import HomeAssistant, callback

_LOGGER = logging.getLogger(__name__)
# Verbindungs- und "Remote X meldet"-Zeilen immer im HA-Log zeigen, auch nach einem Neustart (2026-10-20).
if _LOGGER.level == logging.NOTSET:
    _LOGGER.setLevel(logging.INFO)
DOMAIN = "esphomeremote_konfig_ble"   # Schluessel in hass.data
CONF_REMOTES = "remotes"
CONF_WIFI_HOST = "wifi_host"
CONF_PROXY = "proxy"
CONF_OTA_PORT = "ota_port"

RX = "7e1a0002-5c3d-4f2a-9e61-2d8b4c0a1f00"   # HA -> Remote
TX = "7e1a0003-5c3d-4f2a-9e61-2d8b4c0a1f00"   # Remote -> HA
COMPANY = 0xFFFF
WIEDERHOLEN_S = 1.0   # ohne Quittungs-Fortschritt so lange warten, dann ein Stueck als Probe neu senden
WIN_UP = 1024   # wie WIN_HTTP (2026-10-20): mit 4 KB verwarf der Proxy Pakete - Sitzungsabbrueche
WIN_DOWN = 4096
# HTTP (Bilder) hoechstens 1 KB unterwegs (2026-10-20): mit 4 KB verwarf der Proxy (3 Verbindungen) Pakete ("congested",
# ohne Bestaetigung stillschweigend) - von 13 KB kamen bei Remote 2 10-11 KB an, dann 20 s Warten auf den Rest.
WIN_HTTP = 1024
WIN_OTA = 8192


_IMA_STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767]
_IMA_INDEX = [-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]


def _ima_decode(frame: bytes) -> bytes:
    """Ein Audio-Rahmen der Remote: i16 Startwert, u8 Index, dann Nibbles (unteres zuerst) -> PCM16 LE."""
    pred, index = struct.unpack_from("<hB", frame)
    out = bytearray()
    for b in frame[3:]:
        for code in (b & 0x0F, b >> 4):
            step = _IMA_STEP[index]
            delta = step >> 3
            if code & 4:
                delta += step
            if code & 2:
                delta += step >> 1
            if code & 1:
                delta += step >> 2
            pred = max(-32768, min(32767, pred - delta if code & 8 else pred + delta))
            index = max(0, min(88, index + _IMA_INDEX[code]))
            out += struct.pack("<h", pred)
    return bytes(out)


def _kennung(name: str) -> bytes:
    h = 2166136261
    for ch in name.encode():
        h ^= ch
        h = (h * 16777619) & 0xFFFFFFFF
    return b"HL" + struct.pack("<I", h)


_ALLE: list = []   # alle Links (Adress-Sperre in _on_adv)


class Link:
    """Eine Remote: Bluetooth-Link halten, lokalen Port anbieten, Sitzungen weiterleiten."""

    def __init__(self, hass: HomeAssistant, name: str, port: int, wifi_host: str | None, proxy: str | None,
                 ota_port: int | None = None) -> None:
        self.proxy = proxy.upper() if proxy else None
        self.hass = hass
        self.name = name
        self.port = port
        self.wifi_host = wifi_host
        self.kennung = _kennung(name)
        self.address: str | None = None   # aktuelle Werbeadresse (wechselt mit dem HID-Platz)
        self.client: bleak.BleakClient | None = None
        self.mtu = 23
        self.ctl: asyncio.Queue = asyncio.Queue()
        self.wifi_up: bool | None = None
        self.wifi_since = 0.0   # seit wann der gemeldete WLAN-Zustand gilt
        self.session_writer: asyncio.StreamWriter | None = None
        self.session_since = 0.0
        self.http_writer: asyncio.StreamWriter | None = None
        self.http_sent = self.http_acked = 0
        self.http_room = asyncio.Event()
        self.ha_http = ("127.0.0.1", 8123)   # HAs eigener Webserver (Bilder unter /local, Konfiguration)
        self.voice_q: asyncio.Queue | None = None
        self.ota_port = ota_port
        self.ota_writer: asyncio.StreamWriter | None = None
        self.ota_open, self.ota_closed, self.ota_room = asyncio.Event(), asyncio.Event(), asyncio.Event()
        self.ota_sent = self.ota_acked = 0
        # Pipeline-Auswahl der Remote ("Remote Remote 2" -> select.remote_wz_wohnzimmer_assistent)
        self.assistent_select = f"select.remote_wz_{name.split()[-1].lower()}_assistent"
        self.wifi_session: tuple[asyncio.StreamWriter, asyncio.StreamWriter] | None = None
        self.server: asyncio.base_events.Server | None = None
        self._stop = False
        self._tasks: list[asyncio.Task] = []
        self._reset_flow()
        self.stats = {"tunnel": 0, "wifi": 0, "verbindungen": 0}
        self._lebenszeichen = 0.0
        self._ohne_cache = False
        _ALLE.append(self)
        self._werbung = asyncio.Event()   # Werbung der Remote gesehen -> _keep_link sofort weiter

    # ---------- Flusssteuerung (s. components/ha_tunnel/ha_tunnel.h) ----------
    def _reset_flow(self) -> None:
        # Wiederholung (2026-10-23): Remote mit Datenrahmen 0x03 (Position) - unquittierte Bytes aufheben und nach
        # WIEDERHOLEN_S ohne Quittungs-Fortschritt ab der letzten Quittung neu schicken (Proxy verwirft bei Last)
        self.mit_position = False
        self.up_puffer = bytearray()   # Bytes ab up_acked
        self.up_fortschritt = self.up_quittung = self.up_luecke = time.monotonic()
        self.up_total = self.up_acked = 0
        self.down_total = self.down_acked = 0
        self.up_room = asyncio.Event()
        self.up_room.set()

    async def _write(self, frame: bytes) -> None:
        if self.client is None or not self.client.is_connected:
            raise ConnectionError("Link nicht verbunden")
        # Ohne Bestaetigung. Mit Bestaetigung getestet (2026-10-19/20): der Proxy bricht mit error=133 ab und trennt.
        # Verlorene Bytes (Bild 14 KB, an der Remote 10,6 KB) waren Rahmen > 512 Byte: das RX-Merkmal der Remote fasst
        # 512, laengere verwirft sie ohne Bestaetigung stillschweigend - deshalb _teil().
        await self.client.write_gatt_char(RX, frame, response=False)

    def _teil(self) -> int:
        # Nutzdaten je Rahmen: MTU - 3, aber das RX-Merkmal der Remote fasst hoechstens 512 Byte (1 Byte Typ)
        return min(max(20, self.mtu - 3), 512) - 1

    def _ack(self) -> None:
        self.down_acked = self.down_total
        self.hass.async_create_task(self._safe_write(b"\x30" + struct.pack("<I", self.down_total)))

    async def _safe_write(self, frame: bytes) -> None:
        try:
            await self._write(frame)
        except Exception:  # noqa: BLE001
            pass

    async def _send_data(self, payload: bytes) -> None:
        chunk = self._teil()
        schritt = chunk - 4 if self.mit_position else chunk
        for i in range(0, len(payload), schritt):
            part = payload[i : i + schritt]
            while self.up_total - self.up_acked + len(part) > WIN_UP:
                self.up_room.clear()
                await asyncio.wait_for(self.up_room.wait(), 10)
            if self.mit_position:
                if self.up_total == self.up_acked:
                    self.up_fortschritt = self.up_quittung = time.monotonic()
                off = self.up_total
                self.up_puffer += part
                self.up_total += len(part)
                await self._write(b"\x03" + struct.pack("<I", off) + part)
            else:
                self.up_total += len(part)
                await self._write(b"\x00" + part)

    @callback
    def _notify(self, _char, data: bytearray) -> None:
        data = bytes(data)
        if not data:
            return
        t = data[0]
        if t == 0x00:
            self.down_total += len(data) - 1
            if self.down_total - self.down_acked >= 1024:
                self._ack()
            if self.session_writer is not None:
                self.session_writer.write(data[1:])
        elif t == 0x31:
            neu = struct.unpack("<I", data[1:5])[0]
            if neu > self.up_acked:
                del self.up_puffer[: neu - self.up_acked]
                self.up_fortschritt = self.up_quittung = time.monotonic()
            elif (self.mit_position and neu == self.up_acked and self.up_total > self.up_acked
                  and time.monotonic() - self.up_luecke > 0.3):
                # Gleiche Quittung noch einmal = die Remote meldet eine Luecke (Rahmen dahinter verworfen): sofort ab
                # hier alles neu schicken, nicht erst nach WIEDERHOLEN_S
                self.up_luecke = self.up_fortschritt = time.monotonic()
                self.stats["luecke"] = self.stats.get("luecke", 0) + 1
                self.hass.async_create_task(self._wiederholen())
            self.up_acked = max(self.up_acked, neu) if self.mit_position else neu
            if self.up_total - self.up_acked < WIN_UP:
                self.up_room.set()
        elif t == 0x21:
            up = bool(data[1])
            if up != self.wifi_up:
                _LOGGER.info("%s: WLAN %s", self.name, "da" if up else "weg")
            self.wifi_up = up
            self.wifi_since = time.monotonic()
            if not up and self.wifi_session is not None:
                _LOGGER.info("%s: WLAN weg - WLAN-Sitzung beendet, naechste geht durch den Tunnel", self.name)
                for w in self.wifi_session:   # beide Seiten, sonst haengt das Lesen vom toten WLAN-Socket
                    w.close()
        elif t == 0x43:   # Remote will HTTP zu HA (Bilder, Konfiguration)
            self.hass.async_create_task(self._http_open())
        elif t == 0x40:
            if self.http_writer is not None:
                self.http_writer.write(data[1:])
        elif t == 0x42:
            if self.http_writer is not None:
                self.http_writer.close()
                self.http_writer = None
        elif t == 0x44:
            self.http_acked = struct.unpack("<I", data[1:5])[0]
            if self.http_sent - self.http_acked < WIN_HTTP:
                self.http_room.set()
        elif t == 0x50:   # Sprache: Aufnahme beginnt
            _LOGGER.info("%s: Sprache: Start", self.name)
            self._voice_begin()
        elif t == 0x51:
            if self.voice_q is not None:
                self.voice_q.put_nowait(_ima_decode(data[1:]))
        elif t == 0x52:
            if self.voice_q is not None:
                self.voice_q.put_nowait(None)
        elif t == 0x61:   # OTA: Remote hat ihren OTA-Server erreicht
            self.ota_open.set()
        elif t == 0x62:
            if self.ota_writer is not None:
                self.ota_writer.write(data[1:])
        elif t == 0x63:
            self.ota_closed.set()
            if self.ota_writer is not None:
                self.ota_writer.close()
        elif t == 0x64:
            self.ota_acked = struct.unpack("<I", data[1:5])[0]
            self.ota_room.set()
        elif t == 0x20:
            _LOGGER.info("%s meldet: %s", self.name, data[1:].decode(errors="replace"))
        else:
            self.ctl.put_nowait(data)

    # ---------- Sprache: ADPCM vom Link -> Assist-Pipeline (s. components/ha_tunnel, 0x50-0x53) ----------
    def _voice_begin(self) -> None:
        if self.voice_q is not None:
            self.voice_q.put_nowait(None)   # alte Abfrage beenden
        self.voice_q = asyncio.Queue()
        self.hass.async_create_task(self._voice_run(self.voice_q))

    async def _voice_send(self, typ: int, text: str = "") -> None:
        room = max(20, self.mtu - 3) - 2
        raw = text.encode()[:room].decode(errors="ignore").encode()
        await self._safe_write(bytes([0x53, typ]) + raw)

    async def _voice_run(self, q: asyncio.Queue) -> None:
        from homeassistant.components import assist_pipeline, stt
        from homeassistant.components.media_player import async_process_play_media_url
        from homeassistant.core import Context

        t0 = time.monotonic()
        # Pipeline wie an der Remote eingestellt (Auswahl "Assistent")
        pid = None
        sel = self.hass.states.get(self.assistent_select)
        if sel is not None and sel.state not in ("preferred", "unknown", "unavailable"):
            for p in assist_pipeline.async_get_pipelines(self.hass):
                if p.name == sel.state:
                    pid = p.id
        pipeline = assist_pipeline.async_get_pipeline(self.hass, pid)
        sprache = pipeline.stt_language or pipeline.language

        async def strom():
            n = 0
            while (chunk := await q.get()) is not None:
                n += len(chunk)
                yield chunk
            _LOGGER.info("%s: Sprache %.1f s Audio empfangen", self.name, n / 32000)

        def ereignis(ev) -> None:
            typ, d = ev.type, ev.data or {}
            E = assist_pipeline.PipelineEventType
            if typ == E.STT_END:
                text = d.get("stt_output", {}).get("text", "")
                _LOGGER.info("%s: Sprache erkannt: %s (nach %.1f s)", self.name, text, time.monotonic() - t0)
                self.hass.async_create_task(self._voice_send(1, text))
            elif typ == E.INTENT_END:
                sp = (((d.get("intent_output") or {}).get("response") or {}).get("speech") or {}).get("plain") or {}
                _LOGGER.info("%s: Sprache Antwort: %s (nach %.1f s)", self.name, sp.get("speech", "")[:120],
                             time.monotonic() - t0)
                self.hass.async_create_task(self._voice_send(2, sp.get("speech", "")))
            elif typ == E.TTS_END:
                url = (d.get("tts_output") or {}).get("url")
                if url:
                    self.hass.async_create_task(self._voice_send(3, async_process_play_media_url(self.hass, url)))
            elif typ == E.ERROR:
                _LOGGER.info("%s: Sprache Fehler %s", self.name, d)
                self.hass.async_create_task(self._voice_send(4, str(d.get("message", d.get("code", "")))))

        try:
            await assist_pipeline.async_pipeline_from_audio_stream(
                self.hass, context=Context(), event_callback=ereignis,
                stt_metadata=stt.SpeechMetadata(
                    language=sprache, format=stt.AudioFormats.WAV, codec=stt.AudioCodecs.PCM,
                    bit_rate=stt.AudioBitRates.BITRATE_16, sample_rate=stt.AudioSampleRates.SAMPLERATE_16000,
                    channel=stt.AudioChannels.CHANNEL_MONO),
                stt_stream=strom(), pipeline_id=pid,
            )
        except Exception as err:  # noqa: BLE001
            _LOGGER.warning("%s: Sprache: Pipeline-Fehler %r", self.name, err)
            await self._voice_send(4, str(err))
        finally:
            if self.voice_q is q:
                self.voice_q = None
            await self._voice_send(5)
            _LOGGER.info("%s: Sprache fertig nach %.1f s", self.name, time.monotonic() - t0)

    # ---------- Update durch den Tunnel: lokaler Port (ota_port) -> Remote-OTA-Server ----------
    async def _ota_handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        if self.client is None or not self.client.is_connected or self.ota_writer is not None:
            _LOGGER.warning("%s: Update durch den Tunnel nicht moeglich (kein Link oder laeuft schon)", self.name)
            writer.close()
            return
        t0, n = time.monotonic(), 0
        self.ota_open, self.ota_closed = asyncio.Event(), asyncio.Event()
        self.ota_sent = self.ota_acked = 0
        self.ota_room = asyncio.Event()
        self.ota_writer = writer
        try:
            await self._write(b"\x60")
            await asyncio.wait_for(self.ota_open.wait(), 10)
            _LOGGER.info("%s: Update durch den Tunnel beginnt", self.name)
            chunk = self._teil()
            while not self.ota_closed.is_set() and (data := await reader.read(4096)):
                for i in range(0, len(data), chunk):
                    part = data[i : i + chunk]
                    while self.ota_sent - self.ota_acked + len(part) > WIN_OTA:
                        self.ota_room.clear()
                        await asyncio.wait_for(self.ota_room.wait(), 15)
                    self.ota_sent += len(part)
                    await self._write(b"\x62" + part)
                n += len(data)
            if n:   # nur nach einer echten Uebertragung auf das Ende warten (ein leerer Verbindungstest belegte den Kanal 30 s)
                await asyncio.wait_for(self.ota_closed.wait(), 30)   # Remote schliesst nach dem Neustart-Befehl
        except Exception as err:  # noqa: BLE001
            _LOGGER.warning("%s: Update durch den Tunnel abgebrochen: %r", self.name, err)
        finally:
            await self._safe_write(b"\x63")
            self.ota_writer = None
            writer.close()
            dt = time.monotonic() - t0
            _LOGGER.info("%s: Update durch den Tunnel: %d Bytes in %.0f s (%.1f KB/s)", self.name, n, dt,
                         n / max(dt, 0.1) / 1024)

    # ---------- HTTP-Kanal: Remote -> HA-Webserver (s. components/ha_tunnel, http_port) ----------
    async def _http_open(self) -> None:
        # Sitzungsnummer (2026-10-07): eine neue Anfrage ersetzt die alte. Deren "zu" (0x42) darf dann NICHT mehr
        # zur Remote - kam es nach dem 0x43 der neuen Anfrage an, hielt die Remote es fuer die Absage der neuen
        # ("HA hat nicht geantwortet"), die neue Verbindung blieb halb offen bis zur naechsten Anfrage - endlos.
        self.http_gen = getattr(self, "http_gen", 0) + 1
        gen = self.http_gen
        if self.http_writer is not None:
            self.http_writer.close()
        try:
            reader, writer = await asyncio.wait_for(asyncio.open_connection(*self.ha_http), 5)
        except Exception as err:  # noqa: BLE001
            _LOGGER.info("%s: HTTP zu HA nicht moeglich: %r", self.name, err)
            if self.http_gen == gen:
                await self._safe_write(b"\x42")
            return
        if self.http_gen != gen:   # waehrend des Verbindens schon von einer neueren Anfrage ersetzt
            writer.close()
            return
        self.http_writer = writer
        self.http_sent = self.http_acked = 0
        self.http_room = asyncio.Event()
        self.http_room.set()
        await self._safe_write(b"\x41")
        t0, n = time.monotonic(), 0
        chunk = self._teil()
        try:
            while data := await reader.read(4096):
                for i in range(0, len(data), chunk):
                    part = data[i : i + chunk]
                    while self.http_sent - self.http_acked + len(part) > WIN_HTTP:
                        self.http_room.clear()
                        await asyncio.wait_for(self.http_room.wait(), 10)
                    self.http_sent += len(part)
                    await self._write(b"\x40" + part)
                n += len(data)
        except Exception as err:  # noqa: BLE001
            _LOGGER.debug("%s: HTTP abgebrochen: %r", self.name, err)
        finally:
            if self.http_gen == gen:   # nur die aktuelle Sitzung meldet "zu"
                await self._safe_write(b"\x42")
            if self.http_writer is writer:
                self.http_writer = None
            writer.close()
            _LOGGER.info("%s: HTTP ueber Bluetooth: %d Bytes in %.1f s", self.name, n, time.monotonic() - t0)

    async def _expect(self, typ: int, timeout: float) -> bytes:
        end = time.monotonic() + timeout
        while True:
            d = await asyncio.wait_for(self.ctl.get(), max(0.01, end - time.monotonic()))
            if d[0] == typ:
                return d

    # ---------- Bluetooth ----------
    @callback
    def _on_adv(self, info: bluetooth.BluetoothServiceInfoBleak, _change) -> None:
        if info.manufacturer_data.get(COMPANY, b"")[:6] == self.kennung:
            # Adresse einer anderen Remote nicht uebernehmen (2026-10-23: "Remote Remote 1 gefunden unter" Remote 2' Adresse -
            # vermutlich vermischte Werbedaten eines Proxys; danach verbanden beide Links mit Remote 2, HA meldete
            # "Unexpected device" und die Datenstroeme liefen durcheinander)
            if any(l is not self and l.address == info.address for l in _ALLE):
                _LOGGER.debug("%s: Adresse %s gehoert schon einer anderen Remote - ignoriert", self.name, info.address)
                return
            if self.address != info.address:
                _LOGGER.info("%s gefunden unter %s (RSSI %s)", self.name, info.address, info.rssi)
            self.address = info.address
            self._werbung.set()

    def _on_disconnect(self, _client) -> None:
        _LOGGER.info("%s: Bluetooth-Link getrennt", self.name)
        self.wifi_up = None
        if self.session_writer is not None:
            self.session_writer.close()

    async def _ack_loop(self) -> None:
        """Teilquittungen alle 50 ms (sonst wartet die Remote bei kleinen Antworten aufs Fenster).
        Ausserdem: steht das WLAN wieder 5 s stabil, die Tunnel-Sitzung beenden - die ESPHome-Integration
        verbindet sofort neu und landet dann auf dem schnelleren WLAN (~20 ms statt ~300 ms pro Anfrage)."""
        while not self._stop:
            await asyncio.sleep(0.05)
            if self.down_total != self.down_acked and self.client is not None and self.client.is_connected:
                self._ack()
            if (self.mit_position and self.session_writer is not None and self.up_total > self.up_acked
                    and time.monotonic() - self.up_fortschritt > WIEDERHOLEN_S):
                self.up_fortschritt = time.monotonic()
                self.stats["wiederholt"] = self.stats.get("wiederholt", 0) + 1
                # Nur ein Stueck als Probe: war es nur eine spaete Quittung, kommt sie jetzt; fehlte etwas, meldet die
                # Remote die Luecke (gleiche Quittung) und alles ab dort geht sofort neu (s. 0x31)
                self.hass.async_create_task(self._wiederholen(nur_eins=True))
            if (self.mit_position and self.session_writer is not None and self.up_total > self.up_acked
                    and time.monotonic() - self.up_quittung > 6):
                _LOGGER.info("%s: 6 s keine Quittung trotz Wiederholung - Sitzung neu", self.name)
                self.stats["festgefahren"] = self.stats.get("festgefahren", 0) + 1
                self.session_writer.close()
                self.session_writer = None
            if (self.session_writer is not None and self.wifi_host and self.wifi_up
                    and time.monotonic() - self.wifi_since > 5 and time.monotonic() - self.session_since > 5):
                _LOGGER.info("%s: WLAN wieder da - Tunnel-Sitzung beendet, naechste geht ueber WLAN", self.name)
                self.session_writer.close()
                self.session_writer = None

    async def _keep_link(self) -> None:
        """Link dauerhaft halten."""
        while not self._stop:
            if self.client is not None and self.client.is_connected:
                await asyncio.sleep(2)
                # Lebenszeichen alle 20 s (2026-10-17): ohne trennt die Remote den Link nach 70 s - sonst blieb sie
                # an einer Proxy-Verbindung haengen, die HA laengst verloren hatte, und warb nicht mehr.
                if time.monotonic() - self._lebenszeichen > 20:
                    self._lebenszeichen = time.monotonic()
                    await self._safe_write(b"\x32" + struct.pack("<I", WIN_DOWN))
                continue
            # HA-eigenes Geraeteobjekt (mit Proxy-Details) - und zwar das des eingetragenen Proxys: die Remote nimmt
            # den Link nur von dessen Adresse an (sonst haelt sie die Verbindung fuer einen fremden HID-Host).
            device = None
            if self.address:
                for d in bluetooth.async_scanner_devices_by_address(self.hass, self.address, True):
                    if self.proxy is None or d.scanner.source.upper() == self.proxy:
                        device = d.ble_device
                        break
                if device is None:
                    _LOGGER.debug("%s: Proxy %s sieht %s nicht (Scanner: %s)", self.name, self.proxy, self.address,
                                  [d.scanner.source for d in bluetooth.async_scanner_devices_by_address(
                                      self.hass, self.address, True)])
            if not device:
                # Auf die naechste Werbung warten statt fest 2 s (2026-10-19): die Remote ist nach dem Aufwachen sofort dran
                self._werbung.clear()
                try:
                    await asyncio.wait_for(self._werbung.wait(), 2)
                except asyncio.TimeoutError:
                    pass
                continue
            t0 = time.monotonic()
            try:
                self.client = await establish_connection(
                    # zur Laufzeit nachschlagen: HA ersetzt bleak.BleakClient durch seinen Proxy-faehigen Wrapper
                    bleak.BleakClient, device, self.name,
                    disconnected_callback=self._on_disconnect, max_attempts=2,
                    use_services_cache=not self._ohne_cache,
                )
                self._ohne_cache = False
                self.mtu = self.client.mtu_size
                await self.client.start_notify(TX, self._notify)
                await self._write(b"\x32" + struct.pack("<I", WIN_DOWN))
                self.stats["verbindungen"] += 1
                _LOGGER.info("%s: Bluetooth-Link steht nach %.1f s (MTU %s)", self.name, time.monotonic() - t0, self.mtu)
                self.hass.async_create_task(self._api_anstossen())
            except Exception as err:  # noqa: BLE001
                _LOGGER.debug("%s: Verbinden fehlgeschlagen: %r", self.name, err, exc_info=True)
                # GATT-Fehler (2026-10-17): HA/Proxy merken sich die GATT-Tabelle der Remote. Aendert die Firmware sie
                # (z. B. Maus-Merkmale fuer einen Platz), zeigen die gemerkten Handles ins Leere ("Write not permitted")
                # und der Link kommt nie wieder hoch. Dann Cache leeren und trennen - der naechste Versuch liest neu.
                # Scheitert schon das Verbinden an der GATT-Tabelle (kein client), naechster Versuch ohne Cache
                # (2026-10-23: nach einem Flash hingen alle drei Remotes minutenlang an "GATT operation error")
                if "GATT" in str(err) or "Error 128" in str(err):
                    self._ohne_cache = True
                if "GATT" in str(err) and self.client is not None:
                    _LOGGER.info("%s: GATT-Tabelle veraltet - Cache wird geleert", self.name)
                    for ziel in (self.client, getattr(self.client, "_backend", None)):
                        fn = getattr(ziel, "clear_cache", None)
                        if fn is not None:
                            try:
                                await fn()
                                break
                            except Exception:  # noqa: BLE001
                                pass
                if self.client is not None:
                    try:
                        await self.client.disconnect()
                    except Exception:  # noqa: BLE001
                        pass
                self.client = None
                await asyncio.sleep(5)

    async def _api_anstossen(self) -> None:
        """HA sofort verbinden lassen, sobald der Link steht (2026-10-19). Solange die Remote schlief, versucht die
        ESPHome-Integration es nur alle 15 s - nach dem Aufwachen wartete die Remote also bis zu 15 s auf HA, obwohl der
        Link laengst stand. Laedt den ESPHome-Eintrag neu, wenn nach 1 s noch keine Sitzung laeuft (verbindet sofort)."""
        await asyncio.sleep(1.0)
        if self.session_writer is not None or self.wifi_session is not None or self.client is None or not self.client.is_connected:
            return
        for e in self.hass.config_entries.async_entries("esphome"):
            if e.data.get("host") != "127.0.0.1" or e.data.get("port") != self.port:
                continue
            rd = getattr(e, "runtime_data", None)
            if rd is not None and getattr(rd, "available", False):
                return
            if e.state is not ConfigEntryState.LOADED:
                return
            _LOGGER.info("%s: Link steht, HA noch nicht verbunden - sofort neu verbinden", self.name)
            self.stats["angestossen"] = self.stats.get("angestossen", 0) + 1
            await self.hass.config_entries.async_reload(e.entry_id)
            return

    async def wlan_kommt(self, sekunden: float = 20.0) -> None:
        """Die Remote schaltet gleich ihr WLAN ein (2026-10-25, Meldung ueber das Dock): sobald sie dort erreichbar ist,
        HA sofort verbinden - sonst erst beim naechsten Versuch der ESPHome-Integration (alle 15 s), und die Sprache an
        HA lief in ihre Frist."""
        if not self.wifi_host or getattr(self, "_wlan_wartet", False):
            return
        self._wlan_wartet = True
        t0 = time.monotonic()
        try:
            while time.monotonic() - t0 < sekunden:
                if self.wifi_session is not None:
                    return
                try:
                    _r, w = await asyncio.wait_for(asyncio.open_connection(self.wifi_host, 6053), 0.5)
                except Exception:  # noqa: BLE001
                    await asyncio.sleep(0.3)
                    continue
                w.close()
                self.wifi_up, self.wifi_since = True, time.monotonic()
                _LOGGER.info("%s: WLAN da nach %.1f s - HA verbindet", self.name, time.monotonic() - t0)
                await self._esphome_verbinden()
                return
            _LOGGER.info("%s: WLAN angekuendigt, aber in %.0f s nicht erreichbar", self.name, sekunden)
        finally:
            self._wlan_wartet = False

    async def _esphome_verbinden(self) -> None:
        """ESPHome-Eintrag dieser Bruecke neu laden, wenn HA gerade nicht verbunden ist (verbindet dann sofort)."""
        for e in self.hass.config_entries.async_entries("esphome"):
            if e.data.get("host") != "127.0.0.1" or e.data.get("port") != self.port:
                continue
            rd = getattr(e, "runtime_data", None)
            if rd is not None and getattr(rd, "available", False):
                return
            if e.state is not ConfigEntryState.LOADED:
                return
            self.stats["angestossen"] = self.stats.get("angestossen", 0) + 1
            await self.hass.config_entries.async_reload(e.entry_id)
            return

    # ---------- lokaler Port ----------
    async def _handle(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        # 1. WLAN, wenn die Remote dort erreichbar ist (und nicht gerade "WLAN weg" gemeldet hat)
        if self.wifi_host and self.wifi_up is not False:
            try:
                r2, w2 = await asyncio.wait_for(asyncio.open_connection(self.wifi_host, 6053), 0.8)
            except Exception:  # noqa: BLE001
                r2 = w2 = None
            if w2 is not None:
                # Tote WLAN-Sitzung schnell erkennen (2026-10-19): schaltet die Remote ihr WLAN ab, ohne dass "WLAN weg"
                # ankommt, hing HA sonst minutenlang an der Verbindung (Befehle kamen nicht an). Keepalive: nach 6 s
                # Stille alle 3 s pruefen, nach 3 Fehlversuchen ist sie zu - HA verbindet dann ueber den Tunnel.
                sock = w2.get_extra_info("socket")
                if sock is not None:
                    try:
                        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
                        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, 6)
                        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, 3)
                        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, 3)
                    except (OSError, AttributeError):
                        pass
                self.stats["wifi"] += 1
                _LOGGER.info("%s: API-Sitzung ueber WLAN", self.name)
                self.wifi_session = (writer, w2)
                await self._pipe(reader, writer, r2, w2)
                self.wifi_session = None
                return
        # 2. Tunnel
        if self.client is None or not self.client.is_connected or self.session_writer is not None:
            writer.close()
            return
        self.stats["tunnel"] += 1
        self._reset_flow()
        while not self.ctl.empty():
            self.ctl.get_nowait()
        self.session_writer = writer
        self.session_since = time.monotonic()
        try:
            await self._write(b"\x01")
            auf = await self._expect(0x01, 10)
            self.mit_position = len(auf) > 1 and bool(auf[1] & 1)
            _LOGGER.info("%s: API-Sitzung ueber Bluetooth", self.name)
            self.hass.async_create_task(self._sitzung_wachhund(writer))
            while True:
                data = await reader.read(4096)
                if not data:
                    break
                await self._send_data(data)
        except Exception as err:  # noqa: BLE001
            _LOGGER.info("%s: Tunnel-Sitzung beendet: %r", self.name, err)
        finally:
            self.session_writer = None
            await self._safe_write(b"\x02")
            writer.close()

    async def _wiederholen(self, nur_eins: bool = False) -> None:
        """Unquittierte Bytes ab der letzten Quittung neu schicken (die Remote ueberspringt Doppeltes)."""
        schritt = self._teil() - 4
        basis, daten = self.up_acked, bytes(self.up_puffer[:schritt if nur_eins else WIN_UP])
        _LOGGER.debug("%s: wiederhole %d Bytes ab %d", self.name, len(daten), basis)
        for i in range(0, len(daten), schritt):
            await self._safe_write(b"\x03" + struct.pack("<I", basis + i) + daten[i : i + schritt])

    async def _sitzung_wachhund(self, writer: asyncio.StreamWriter) -> None:
        """Erste Tunnel-Sitzung nach "WLAN weg" bekam keine Antwort (2026-10-23): HA wartete jedes Mal 60 s auf den
        Handshake, die zweite Sitzung lief sofort. Antwortet die Remote 8 s lang mit keinem Byte, obwohl HA schon
        gesendet hat, Sitzung beenden - HA verbindet gleich neu."""
        await asyncio.sleep(8)
        if self.session_writer is writer and self.down_total == 0 and self.up_total > 0:
            self.stats["wachhund"] = self.stats.get("wachhund", 0) + 1
            _LOGGER.info("%s: Tunnel-Sitzung ohne Antwort - neu verbinden", self.name)
            writer.close()

    @staticmethod
    async def _pipe(r1, w1, r2, w2) -> None:
        async def copy(r, w):
            try:
                while data := await r.read(4096):
                    w.write(data)
                    await w.drain()
            except Exception:  # noqa: BLE001
                pass
            finally:
                w.close()
        await asyncio.gather(copy(r1, w2), copy(r2, w1))

    async def start(self) -> None:
        self._unsub = bluetooth.async_register_callback(
            self.hass, self._on_adv,
            bluetooth.BluetoothCallbackMatcher(manufacturer_id=COMPANY, connectable=True),
            bluetooth.BluetoothScanningMode.PASSIVE,
        )
        for info in bluetooth.async_discovered_service_info(self.hass, True):
            self._on_adv(info, None)
        self.server = await asyncio.start_server(self._handle, "127.0.0.1", self.port)
        if self.ota_port:   # Updates: esphome upload --device 127.0.0.1 landet hier (ESPHome-Add-on = Host-Netz)
            self.ota_server = await asyncio.start_server(self._ota_handle, "127.0.0.1", self.ota_port)
        self._tasks.append(self.hass.async_create_background_task(self._keep_link(), f"{DOMAIN}_{self.port}"))
        self._tasks.append(self.hass.async_create_background_task(self._ack_loop(), f"{DOMAIN}_{self.port}_ack"))
        _LOGGER.info("%s: Bruecke auf 127.0.0.1:%s", self.name, self.port)

    async def stop(self) -> None:
        self._stop = True
        if self in _ALLE:
            _ALLE.remove(self)
        self._unsub()
        for srv in (self.server, getattr(self, "ota_server", None)):
            if srv:
                srv.close()
                try:
                    await asyncio.wait_for(srv.wait_closed(), 2)
                except (asyncio.TimeoutError, OSError):
                    pass
        for t in self._tasks:
            t.cancel()
        if self.client is not None:
            await self.client.disconnect()


def _register(hass: HomeAssistant) -> dict:
    r = hass.data.get(DOMAIN)
    if r is None:
        r = hass.data[DOMAIN] = {"links": {}, "quellen": {}, "lock": asyncio.Lock()}

        async def _stop(_event) -> None:
            for link in list(r["links"].values()):
                await link.stop()

        hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, _stop)
    return r


async def async_links_setzen(hass: HomeAssistant, quelle: str, remotes: list[dict]) -> None:
    """Links einer Quelle setzen (2026-10-17): "yaml" (configuration.yaml) oder "konfig" (Fernbedienungs-Konfigurator,
    aus Remote-Liste und festen IPs). Gleicher Name: der Konfigurator gewinnt. Nur die WLAN-Adresse geaendert ->
    ohne Neustart des Links uebernehmen; Port/OTA/Proxy geaendert -> Link neu starten."""
    r = _register(hass)
    async with r["lock"]:
        r["quellen"][quelle] = [dict(rc) for rc in remotes]
        soll: dict[str, dict] = {}
        for q in ("yaml", "konfig"):
            for rc in r["quellen"].get(q, []):
                soll[rc[CONF_NAME]] = rc
        for name, link in list(r["links"].items()):
            rc = soll.get(name)
            if rc is not None and (link.port, link.ota_port, link.proxy) == (
                    rc[CONF_PORT], rc.get(CONF_OTA_PORT), (rc.get(CONF_PROXY) or "").upper() or None):
                if link.wifi_host != rc.get(CONF_WIFI_HOST):
                    _LOGGER.info("%s: WLAN-Adresse %s -> %s", name, link.wifi_host, rc.get(CONF_WIFI_HOST))
                    link.wifi_host = rc.get(CONF_WIFI_HOST)
                continue
            _LOGGER.info("%s: Bruecke beendet (127.0.0.1:%s)", name, link.port)
            await link.stop()
            del r["links"][name]
        for name, rc in soll.items():
            if name in r["links"]:
                continue
            link = Link(hass, name, rc[CONF_PORT], rc.get(CONF_WIFI_HOST), rc.get(CONF_PROXY), rc.get(CONF_OTA_PORT))
            try:
                await link.start()
            except OSError as err:
                _LOGGER.error("%s: Port %s nicht verfuegbar: %s", name, rc[CONF_PORT], err)
                await link.stop()
                continue
            r["links"][name] = link


def async_links(hass: HomeAssistant) -> dict:
    """Laufende Links {Name: Link} (fuer den Konfigurator)."""
    return (hass.data.get(DOMAIN) or {}).get("links", {})
