"""Sprache von der Remote per HTTP (2026-11-06): Assist-Sprachassistent und Durchsage ueber denselben Weg.

Die Remote nimmt auf, solange die Taste gehalten wird, und schickt das Audio schon waehrend der Aufnahme als
HTTP-POST (chunked) an /api/esphomeremote/sprache/<geraet>?k=<schluessel>&zweck=assist|durchsage. Der
HTTP-Vermittler der Remote (components/ha_tunnel) waehlt den Weg selbst: WLAN, sonst Bluetooth-Link oder das
Dock (dock_direkt http_relay) - so braucht der Sprachassistent kein WLAN mehr.

Audio: Rahmen zu 132 Byte = 0x51, i16 Startwert, u8 Index, 128 Byte IMA-ADPCM (256 Samples, 16 kHz, unteres
Nibble zuerst) - dasselbe Format wie ueber den Bluetooth-Link (ble_link._ima_decode).

Antwort (text/plain, gestreamt): Zeilen "<typ>|<text>" wie beim Link (0x53): 1 erkannt, 2 Antwort, 3 TTS-URL,
4 Fehler, 5 fertig. Leere Zeilen sind Lebenszeichen - das Dock-Relais bricht nach 3 s Stille ab.

Durchsage: Aufnahme -> WAV (optional Gong davor, lauter gemacht) -> media_player.play_media auf dem Ziel aus dem
Konfigurator (kd.data["durchsage"]); Lautstaerke dafuer optional setzen und danach zuruecknehmen.
"""
from __future__ import annotations

import asyncio
import io
import logging
import math
import secrets
import struct
import time
import wave

import voluptuous as vol
from aiohttp import web

from homeassistant.components import websocket_api
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import Context, HomeAssistant

from .ble_link import _ima_decode

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
KEY = "durchsage"
RAHMEN = 132
RATE = 16000
STANDARD = {"ziel": "", "lautstaerke": 0, "gong": True, "an": True}
_AUDIO: dict[str, tuple[float, bytes]] = {}   # Kennung -> (Zeit, WAV) - fuer den Abruf durch den Lautsprecher


def daten(kd) -> dict:
    d = {**STANDARD, **(kd.data.get(KEY) or {})}
    return d


def validieren(d: dict) -> dict:
    ziel = str(d.get("ziel") or "").strip()
    if ziel and not ziel.startswith("media_player."):
        raise vol.Invalid("ziel: media_player.…")
    try:
        lautstaerke = max(0, min(100, int(d.get("lautstaerke") or 0)))
    except (TypeError, ValueError):
        lautstaerke = 0
    return {"an": bool(d.get("an", True)), "ziel": ziel, "lautstaerke": lautstaerke, "gong": bool(d.get("gong", True))}


# ---------------------------------------------------------------------------------------------- Audio
def _gong() -> bytes:
    """Zweiklang (E5 -> C5) mit Ausklingen, 16 kHz PCM16."""
    out = bytearray()
    for f, dauer in ((659.25, 0.42), (523.25, 0.65)):
        n = int(RATE * dauer)
        for i in range(n):
            t = i / RATE
            hk = math.exp(-3.2 * t) * min(1.0, i / 160)
            v = hk * (0.6 * math.sin(2 * math.pi * f * t) + 0.25 * math.sin(4 * math.pi * f * t) + 0.1 * math.sin(6 * math.pi * f * t))
            out += struct.pack("<h", int(max(-1, min(1, v)) * 9000))
    out += b"\x00\x00" * int(RATE * 0.75)  # Ausklang + 0,5 s Pause vor der Stimme (2026-11-06)
    return bytes(out)


def _biquad(art: str, f: float, q: float) -> tuple:
    w = 2 * math.pi * f / RATE
    c, a = math.cos(w), math.sin(w) / (2 * q)
    b = ((1 - c) / 2, 1 - c, (1 - c) / 2) if art == "lp" else ((1 + c) / 2, -(1 + c), (1 + c) / 2)
    a0 = 1 + a
    return (b[0] / a0, b[1] / a0, b[2] / a0, 2 * c / a0, -(1 - a) / a0)


def _kerbe(f: float, breite: float = 40.0) -> tuple:
    """Notch-Filter mit fester Bandbreite (Hz)."""
    w = 2 * math.pi * f / RATE
    c, a = math.cos(w), math.sin(w) / (2 * (f / breite))
    a0 = 1 + a
    return (1 / a0, -2 * c / a0, 1 / a0, 2 * c / a0, -(1 - a) / a0)


# Stoerungen im Mikrofon der Remote (Messung 2026-11-06 an einer echten Durchsage):
#  - Pfeifton bei genau 1, 2, 3 ... 7 kHz (1-ms-Takt der Elektronik): schmale Kerben bei 1-5 kHz,
#  - darueber Tiefpass 4,5 kHz Butterworth 8. Ordnung (6 kHz -47 dB, 7 kHz -98 dB),
#  - Hochpass 100 Hz gegen Brummen und Gleichanteil.
_FILTER = ([_biquad("hp", 100, 0.7071)] + [_kerbe(k * 1000.0) for k in range(1, 6)]
           + [_biquad("lp", 4500, 1 / (2 * math.sin(math.pi * (2 * k + 1) / 16))) for k in range(4)])


def _einschwingen_weg(pcm: bytes) -> bytes:
    """Anfang und Ende saeubern. Das Mikrofon steht nach dem Einschalten ~0,25 s am Anschlag und schwingt danach bis
    ~0,8 s mit einem grossen Gleichanteil ein (Knall, verdirbt die Lautstaerke-Anhebung). Nur das Stueck am Anschlag
    wegschneiden, den Gleichanteil danach herausrechnen (2026-11-06: vorher wurde bis zum Ende des Einschwingens
    geschnitten - wer gleich losredete, verlor den Anfang)."""
    n = len(pcm) // 2
    x = struct.unpack(f"<{n}h", pcm[: n * 2])
    start = 0
    for i in range(min(n, RATE)):   # letzte Probe am Anschlag in der ersten Sekunde
        if abs(x[i]) >= 32700:
            start = i + RATE // 100
    start = min(start, n)
    # Ende: Loslassen der Taste + Mikrofon aus knacken (letzte ~60 ms bis zum Anschlag) - 150 ms abschneiden,
    # bei einem Ausreisser in der letzten halben Sekunde schon dort
    ende = max(start, n - RATE * 15 // 100)
    for j in range(max(start, n - RATE // 2), ende):
        if abs(x[j]) > 30000:
            ende = max(start, j - RATE // 50)
            break
    y = list(x[start:ende])
    # Gleichanteil nachfuehren (Hochpass 1. Ordnung ~8 Hz, Anfangswert = erster Mittelwert - kein Sprung am Schnitt);
    # den Rest erledigt der Hochpass 100 Hz in _filtern
    if y:
        a = math.exp(-2 * math.pi * 8 / RATE)
        m = sum(y[:160]) / len(y[:160])
        for i, v in enumerate(y):
            m = a * m + (1 - a) * v
            y[i] = max(-32768, min(32767, int(v - m)))
    ein = min(len(y) // 2, RATE // 50)  # 20 ms ein- und ausblenden
    for i in range(ein):
        y[i] = int(y[i] * i / ein)
        y[-1 - i] = int(y[-1 - i] * i / ein)
    return struct.pack(f"<{len(y)}h", *y)


def _filtern(pcm: bytes) -> list[float]:
    n = len(pcm) // 2
    werte: list[float] = list(struct.unpack(f"<{n}h", pcm[: n * 2]))
    for b0, b1, b2, a1, a2 in _FILTER:
        x1 = x2 = y1 = y2 = 0.0
        out: list[float] = []
        ap = out.append
        for x in werte:
            y = b0 * x + b1 * x1 + b2 * x2 + a1 * y1 + a2 * y2
            x2, x1, y2, y1 = x1, x, y1, y
            ap(y)
        werte = out
    return werte


def _lauter(pcm: bytes) -> bytes:
    """Filtern, dann Spitze auf etwa -3 dB anheben (hoechstens 6-fach) - das Mikrofon ist leise, Lautsprecher stehen weit weg."""
    if len(pcm) < 2:
        return pcm
    werte = _filtern(_einschwingen_weg(pcm))
    if not werte:   # nach dem Saeubern nichts uebrig (sehr kurze Aufnahme)
        return b""
    spitze = max(1.0, max(abs(v) for v in werte))
    faktor = max(1.0, min(6.0, 23000 / spitze))
    return struct.pack(f"<{len(werte)}h", *(max(-32768, min(32767, int(v * faktor))) for v in werte))


def _wav(pcm: bytes) -> bytes:
    b = io.BytesIO()
    with wave.open(b, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm)
    return b.getvalue()


class AudioView(HomeAssistantView):
    """Die WAV der Durchsage fuer den Lautsprecher (Cast holt sie selbst ab, ohne HA-Login - zufaellige Kennung)."""

    url = "/api/esphomeremote/durchsage/{kennung}"
    name = "api:esphomeremote:durchsage"
    requires_auth = False

    async def get(self, request: web.Request, kennung: str) -> web.Response:
        a = _AUDIO.get(kennung.removesuffix(".wav"))
        if a is None:
            return web.Response(status=404)
        return web.Response(body=a[1], content_type="audio/wav", headers={"Cache-Control": "no-store"})


# ---------------------------------------------------------------------------------------------- Aufnahme empfangen
class SpracheView(HomeAssistantView):
    url = "/api/esphomeremote/sprache/{dev}"
    name = "api:esphomeremote:sprache"
    requires_auth = False

    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd

    async def post(self, request: web.Request, dev: str) -> web.StreamResponse:
        from . import schluessel_ok

        if not schluessel_ok(request, self.kd.key):
            return web.Response(status=403)
        zweck = request.query.get("zweck", "assist")
        resp = web.StreamResponse(headers={"Content-Type": "text/plain; charset=utf-8", "Cache-Control": "no-store"})
        resp.enable_chunked_encoding()
        await resp.prepare(request)
        sperre = asyncio.Lock()
        t0 = time.monotonic()

        async def melden(typ: int, text: str = "") -> None:
            async with sperre:
                try:
                    await resp.write(f"{typ}|{text.replace(chr(10), ' ').replace(chr(13), ' ')[:400]}\n".encode())
                except Exception:  # noqa: BLE001 - Remote schon weg
                    pass

        async def lebenszeichen() -> None:
            while True:
                await asyncio.sleep(1)
                async with sperre:
                    try:
                        await resp.write(b"\n")
                    except Exception:  # noqa: BLE001
                        return

        q: asyncio.Queue = asyncio.Queue()

        async def lesen() -> None:
            puffer, rahmen = bytearray(), 0
            try:
                while True:
                    teil = await request.content.read(4096)
                    if not teil:
                        break
                    puffer += teil
                    while len(puffer) >= RAHMEN:
                        if puffer[0] != 0x51:   # aus dem Takt: bis zum naechsten Rahmenkopf weiter
                            del puffer[0]
                            continue
                        q.put_nowait(_ima_decode(bytes(puffer[1:RAHMEN])))
                        del puffer[:RAHMEN]
                        rahmen += 1
            except Exception as err:  # noqa: BLE001 - Verbindung abgerissen: mit dem Empfangenen weiter
                _LOGGER.info("%s: Sprache: Empfang abgebrochen (%r)", dev, err)
            q.put_nowait(None)
            _LOGGER.info("%s: Sprache (%s): %u Rahmen (%.1f s) in %.1f s", dev, zweck, rahmen, rahmen * 256 / RATE,
                         time.monotonic() - t0)

        leser = self.hass.async_create_task(lesen())
        puls = self.hass.async_create_task(lebenszeichen())
        try:
            if zweck == "durchsage":
                await durchsage(self.hass, self.kd, dev, q, melden)
            else:
                await assist(self.hass, dev, q, melden)
        except Exception as err:  # noqa: BLE001
            _LOGGER.warning("%s: Sprache (%s): %r", dev, zweck, err)
            await melden(4, str(err))
        finally:
            puls.cancel()
            await leser
            await melden(5)
        try:
            await resp.write_eof()
        except Exception:  # noqa: BLE001
            pass
        return resp


async def assist(hass: HomeAssistant, dev: str, q: asyncio.Queue, melden) -> None:
    """Assist-Pipeline wie ueber den Bluetooth-Link (ble_link._voice_run), Pipeline aus der Auswahl der Remote."""
    from homeassistant.components import assist_pipeline, stt
    from homeassistant.components.media_player import async_process_play_media_url

    t0 = time.monotonic()
    pid = None
    sel = hass.states.get(f"select.{dev.replace('-', '_')}_assistent")
    if sel is not None and sel.state not in ("preferred", "unknown", "unavailable"):
        for p in assist_pipeline.async_get_pipelines(hass):
            if p.name == sel.state:
                pid = p.id
    pipeline = assist_pipeline.async_get_pipeline(hass, pid)
    sprache = pipeline.stt_language or pipeline.language

    async def strom():
        while (chunk := await q.get()) is not None:
            yield chunk

    def ereignis(ev) -> None:
        typ, d = ev.type, ev.data or {}
        E = assist_pipeline.PipelineEventType
        if typ == E.STT_END:
            text = d.get("stt_output", {}).get("text", "")
            _LOGGER.info("%s: Sprache erkannt: %s (nach %.1f s)", dev, text, time.monotonic() - t0)
            hass.async_create_task(melden(1, text))
        elif typ == E.INTENT_END:
            sp = (((d.get("intent_output") or {}).get("response") or {}).get("speech") or {}).get("plain") or {}
            hass.async_create_task(melden(2, sp.get("speech", "")))
        elif typ == E.TTS_END:
            url = (d.get("tts_output") or {}).get("url")
            if url:
                hass.async_create_task(melden(3, async_process_play_media_url(hass, url)))
        elif typ == E.ERROR:
            hass.async_create_task(melden(4, str(d.get("message", d.get("code", "")))))

    await assist_pipeline.async_pipeline_from_audio_stream(
        hass, context=Context(), event_callback=ereignis,
        stt_metadata=stt.SpeechMetadata(
            language=sprache, format=stt.AudioFormats.WAV, codec=stt.AudioCodecs.PCM,
            bit_rate=stt.AudioBitRates.BITRATE_16, sample_rate=stt.AudioSampleRates.SAMPLERATE_16000,
            channel=stt.AudioChannels.CHANNEL_MONO),
        stt_stream=strom(), pipeline_id=pid,
    )
    # Ereignisse aus dem Rueckruf noch rausgehen lassen, bevor "fertig" kommt
    await asyncio.sleep(0.05)


async def durchsage(hass: HomeAssistant, kd, dev: str, q: asyncio.Queue, melden) -> None:
    from homeassistant.helpers.network import NoURLAvailableError, get_url

    d = daten(kd)
    pcm = bytearray()
    while (chunk := await q.get()) is not None:
        pcm += chunk
    sek = len(pcm) / 2 / RATE
    if not d["an"]:
        await melden(4, "Durchsage ist im Konfigurator ausgeschaltet")
        return
    if sek < 0.4:
        await melden(4, "Zu kurz – Taste halten und sprechen")
        return
    ziel = d["ziel"] or ""
    if not ziel or hass.states.get(ziel) is None:
        await melden(4, "Kein Lautsprecher eingestellt (Konfigurator › Funktionen › Durchsage)")
        return
    await melden(1, f"Durchsage ({sek:.1f} s)".replace(".", ","))
    stimme = await hass.async_add_executor_job(_lauter, bytes(pcm))
    if len(stimme) < RATE * 2 * 3 // 10:
        await melden(4, "Zu kurz – Taste halten und sprechen")
        return
    audio = (_gong() if d["gong"] else b"") + stimme
    kennung = secrets.token_urlsafe(18)
    jetzt = time.time()
    for k in [k for k, (t, _) in _AUDIO.items() if jetzt - t > 600]:
        _AUDIO.pop(k, None)
    _AUDIO[kennung] = (jetzt, _wav(audio))
    try:
        basis = get_url(hass, allow_external=False, prefer_external=False)
    except NoURLAvailableError:
        basis = f"http://{kd.basis()}"
    url = f"{basis}/api/esphomeremote/durchsage/{kennung}.wav"
    st = hass.states.get(ziel)
    vorher = st.attributes.get("volume_level") if st else None
    if d["lautstaerke"]:
        await hass.services.async_call("media_player", "volume_set", {"entity_id": ziel, "volume_level": d["lautstaerke"] / 100},
                                       blocking=True)
    await hass.services.async_call("media_player", "play_media", {"entity_id": ziel, "media_content_id": url,
                                                                  "media_content_type": "music"}, blocking=True)
    name = (st.attributes.get("friendly_name") if st else "") or ziel
    await melden(2, f"Läuft auf {name}")
    _LOGGER.info("%s: Durchsage %.1f s auf %s", dev, sek, ziel)
    if d["lautstaerke"] and isinstance(vorher, (int, float)):
        dauer = len(audio) / 2 / RATE + 4

        async def zurueck() -> None:
            await asyncio.sleep(dauer)
            await hass.services.async_call("media_player", "volume_set", {"entity_id": ziel, "volume_level": vorher})

        hass.async_create_task(zurueck())


# ---------------------------------------------------------------------------------------------- Konfigurator
@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/durchsage_get"})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_get(hass, connection, msg) -> None:
    kd = hass.data[DOMAIN + "_kd"]
    d = daten(kd)
    if not d["ziel"]:   # Vorschlag: eine Cast-Gruppe mit "Wohnung" im Namen, sonst die erste Cast-Gruppe
        cand = [s for s in hass.states.async_all("media_player") if "group" in str(s.attributes.get("friendly_name", "")).lower()
                or "wohnung" in s.entity_id]
        d["vorschlag"] = cand[0].entity_id if cand else ""
    connection.send_result(msg["id"], d)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/durchsage_save", vol.Required("daten"): dict})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_save(hass, connection, msg) -> None:
    kd = hass.data[DOMAIN + "_kd"]
    try:
        d = validieren(msg["daten"])
    except vol.Invalid as err:
        connection.send_error(msg["id"], "ungueltig", str(err))
        return
    kd.data[KEY] = d
    await kd.store.async_save(kd.data)
    connection.send_result(msg["id"], d)


def einrichten(hass: HomeAssistant, kd) -> None:
    hass.data[DOMAIN + "_kd"] = kd
    hass.http.register_view(SpracheView(hass, kd))
    hass.http.register_view(AudioView())
    for fn in (ws_get, ws_save):
        websocket_api.async_register_command(hass, fn)
