"""Neue Fernbedienung einrichten (2026-10-04) + Dienst "an alle Remotes".

Ablauf im Panel ("Neue Fernbedienung"):
  1. anlegen:   esphome/tools/remotes.py --neu <Name> <IP> erzeugt remote-wz-<kennung>.yaml aus
                templates/remote.yaml und das IP-Secret; die Remote kommt in die Geraeteliste der Integration.
  2. bauen:     Kompilieren ueber den ESPHome Device Builder (Add-on), Fortschritt per Abfrage. Gebaut und geflasht
                wird die -halink-Datei (wie im Bereich Firmware). Seit 2026-10-18 ueber die Auftrags-API des Builders
                (firmware/compile + get_job) mit Neuverbindung und zweitem Versuch - vorher riss die Verbindung unter
                Volllast ab, und der Assistent blieb ohne Ende stehen.
  3. flashen:   a) USB am Computer: Browser (ESP Web Tools) holt Manifest + factory.bin hier ab
                   (kurzlebiges Token, weil der Flasher keinen HA-Login mitschickt).
                b) USB am HA-Server: Upload ueber das Add-on an einen seriellen Port.
  4. einbinden: wartet, bis die Remote im WLAN ist, und legt den ESPHome-Eintrag in HA an
                (Schluessel aus der YAML).
"""

from __future__ import annotations

import asyncio
from collections import deque
import logging
import re
import secrets
import socket
import time

import voluptuous as vol
from aiohttp import web

from homeassistant.components import websocket_api
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .firmware import ESPHOME_DIR, Dash, _mit_versuch, _port, dash as _befehl   # eine Verbindung zum Device Builder fuer alles

_LOGGER = logging.getLogger(__name__)

MAX_ZEILEN = 400


def _yaml_info(dev: str) -> dict:
    """IP (aus secrets.yaml) und API-Schluessel der Remote aus ihrer YAML."""
    y = (ESPHOME_DIR / f"{dev}.yaml").read_text(encoding="utf-8")
    key = re.search(r'^\s+key:\s*"([^"]+)"', y, re.M)
    sec = re.search(r"static_ip:\s*!secret\s+(\S+)", y)
    ip = None
    if sec:
        m = re.search(rf"^{re.escape(sec.group(1))}:\s*(\S+)", (ESPHOME_DIR / "secrets.yaml").read_text(encoding="utf-8"), re.M)
        ip = m.group(1) if m else None
    return {"ip": ip, "key": key.group(1) if key else None}


def flash_datei(dev: str) -> str:
    """Die Datei, die gebaut und geflasht wird: <dev>-halink.yaml (Bluetooth-Schicht), sonst <dev>.yaml."""
    return f"{dev}-halink.yaml" if (ESPHOME_DIR / f"{dev}-halink.yaml").exists() else f"{dev}.yaml"


def _offen(ip: str, port: int, t: float = 1.5) -> bool:
    try:
        with socket.create_connection((ip, port), timeout=t):
            return True
    except OSError:
        return False


class Einrichtung:
    """Laufende Auftraege (bauen/flashen/einbinden) je Remote, fuers Panel abfragbar."""

    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.jobs: dict[str, dict] = {}
        self.tokens: dict[str, tuple[str, float]] = {}

    def job(self, dev: str) -> dict:
        return self.jobs.setdefault(dev, {"phase": "", "running": False, "exit": None, "log": deque(maxlen=MAX_ZEILEN),
                                          "msg": "", "fehler": "", "schritt": "", "progress": None})

    def stand(self, dev: str) -> dict:
        """Fuers Panel (ohne deque, nur die letzten Zeilen)."""
        j = self.job(dev)
        zeilen = [z for z in j["log"] if not re.match(r"^\[\d+/\d+\]", z)]
        return {"phase": j["phase"], "running": j["running"], "exit": j["exit"], "msg": j["msg"], "schritt": j["schritt"],
                "progress": j["progress"], "lines": zeilen[-60:]}

    def _zeile(self, dev: str, text: str) -> None:
        j = self.job(dev)
        for l in text.replace("\r", "\n").split("\n"):
            l = re.sub(r"(\x1b|\\033)\[[0-9;]*m", "", l).rstrip()
            if l:
                j["log"].append(l[:300])

    async def _bereit(self, dev: str) -> None:
        """Frisch angelegt: warten, bis der Device Builder beide Dateien eingelesen hat. Er erzeugt dabei die Quellen
        im gemeinsamen Build-Ordner (<dev>.yaml und -halink.yaml haben denselben Geraetenamen) - ein Bau, der
        gleichzeitig laeuft, verliert Dateien ("No such file or directory", 2026-10-18 nachgestellt)."""
        dateien = {f"{dev}.yaml", flash_datei(dev)}
        ende = time.monotonic() + 120
        gemeldet = False
        while time.monotonic() < ende:
            try:
                liste = (await _befehl(self.hass, "devices/list")).get("configured", [])
            except Exception:  # noqa: BLE001
                liste = []
            fertig = {x.get("configuration") for x in liste if x.get("expected_config_hash")}
            if dateien <= fertig:
                if gemeldet:
                    await asyncio.sleep(3)   # Nachlauf des Einlesens
                return
            if not gemeldet:
                self._zeile(dev, "Warte, bis das ESPHome-Add-on die neuen Dateien eingelesen hat ...")
                gemeldet = True
            await asyncio.sleep(3)
        self._zeile(dev, "ESPHome-Add-on hat die Dateien nach 2 min noch nicht gemeldet - baue trotzdem")

    async def ausfuehren(self, dev: str, art: str, port: str | None = None, teil: bool = False) -> int:
        """compile oder upload (port = serieller Port am HA-Server bzw. IP fuer OTA) ueber den Device Builder.
        teil=True: Schritt innerhalb eines groesseren Auftrags (Status bleibt "laeuft")."""
        j = self.job(dev)
        if teil:
            j["teil"] = art
        else:
            j.update(phase=art, running=True, exit=None, msg="", fehler="", schritt="", progress=None)
            j["log"].clear()
        datei = flash_datei(dev)
        name = dev.replace("remote-wz-", "Remote ").title()
        try:
            if art == "compile":
                await self._bereit(dev)
                ok = await _mit_versuch(self.hass, j, name, "bauen", "firmware/compile", {"configuration": datei})
            else:
                usb = bool(port) and port.startswith("/dev/")
                ok = await _mit_versuch(self.hass, j, name, "bauen und aufspielen", "firmware/install",
                                        {"configuration": datei, "port": port or "OTA", "force_local": False, "bootloader": usb})
            j["exit"] = 0 if ok else 1
            if not ok:
                j["msg"] = f"Fehler: {j['fehler'] or 'siehe Protokoll'}"
        except Exception as err:  # noqa: BLE001
            j["msg"] = f"Fehler: {err}"
            j["exit"] = 1
        finally:
            j["progress"] = None
            if not teil:
                j["running"] = False
        return j["exit"]

    async def _ip_vom_router(self, dev: str) -> str | None:
        """IP der Remote im WLAN, so wie der Router sie vergeben hat.

        mDNS geht nicht: die Remote-Firmware hat es aus (remote-halink.yaml, mdns: disabled) - deshalb fand auch der
        Device Builder sie nie (2026-10-18). Stattdessen fragen wir den Router: 1. Geraete-Tracker in HA (FRITZ!Box
        u. a.) mit dem Hostnamen, den die Remote per DHCP meldet; 2. Namensaufloesung (der Router loest
        DHCP-Hostnamen auf); 3. der Device Builder. Jede Adresse muss die ESPHome-API (6053) offen haben.
        """
        kandidaten: list[str] = []
        for st in self.hass.states.async_all("device_tracker"):
            name = str(st.attributes.get("host_name") or "").lower()
            if name in (dev, f"{dev}.local") and st.attributes.get("ip"):
                kandidaten.append(str(st.attributes["ip"]))
        for host in (dev, f"{dev}.fritz.box", f"{dev}.local"):
            try:
                for *_, adr in await self.hass.async_add_executor_job(socket.getaddrinfo, host, 6053, socket.AF_INET):
                    kandidaten.append(adr[0])
            except OSError:
                pass
        try:
            d = await _befehl(self.hass, "devices/list")
            for x in d.get("configured", []):
                if x.get("configuration") in (f"{dev}.yaml", flash_datei(dev)) and x.get("ip"):
                    kandidaten.append(x["ip"])
        except Exception:  # noqa: BLE001
            pass
        for ip in dict.fromkeys(kandidaten):
            if await self.hass.async_add_executor_job(_offen, ip, 6053):
                return ip
        return None

    async def einbinden(self, dev: str) -> None:
        """Warten, bis die Remote erreichbar ist, dann ESPHome-Eintrag mit Schluessel anlegen."""
        j = self.job(dev)
        j.update(phase="einbinden", running=True, exit=None, msg="", fehler="", schritt="", progress=None)
        j["log"].clear()
        try:
            info = await self.hass.async_add_executor_job(_yaml_info, dev)
            ip = info["ip"]
            dhcp = not ip
            if dhcp:
                # Erster Start mit IP vom Router: warten, bis ESPHome die Remote im Netz findet
                self._zeile(dev, f"Warte auf {dev} im WLAN (IP vom Router) ...")
                ende = time.monotonic() + 600
                while not (ip := await self._ip_vom_router(dev)):
                    if time.monotonic() > ende:
                        raise RuntimeError("Remote nach 10 min nicht im WLAN gefunden - eingeschaltet? WLAN-Daten richtig?")
                    await asyncio.sleep(5)
                self._zeile(dev, f"Gefunden: {ip}")
            for e in self.hass.config_entries.async_entries("esphome"):
                if e.data.get("host") not in (ip, f"{dev}.local"):
                    continue
                if e.data.get("device_name") in (None, dev):
                    j["msg"] = "Schon in Home Assistant eingebunden."
                    j["exit"] = 0
                    return
                # Gleiche IP, anderer Name: die Platine war vorher ein anderes Geraet (gleiche MAC -> gleiche IP
                # vom Router, 2026-10-18 "Mikrofon Kontakttest" -> Gast). Der alte Eintrag wuerde den neuen blockieren.
                self._zeile(dev, f"Entferne alten Eintrag „{e.title}“ (vorher auf dieser Platine) ...")
                await self.hass.config_entries.async_remove(e.entry_id)
            self._zeile(dev, f"Warte auf {dev} ({ip}) im WLAN ...")
            ende = time.monotonic() + 600
            while not await self.hass.async_add_executor_job(_offen, ip, 6053):
                if time.monotonic() > ende:
                    raise RuntimeError("Remote nach 10 min nicht im WLAN - eingeschaltet? WLAN-Daten richtig?")
                await asyncio.sleep(3)
            self._zeile(dev, "Erreichbar - lege den ESPHome-Eintrag an ...")
            flow = self.hass.config_entries.flow
            r = await flow.async_init("esphome", context={"source": "user"}, data={"host": ip, "port": 6053})
            for _ in range(5):
                if r.get("type") != "form":
                    break
                step = r.get("step_id")
                self._zeile(dev, f"Schritt: {step}")
                if step == "encryption_key":
                    r = await flow.async_configure(r["flow_id"], {"noise_psk": info["key"]})
                elif step == "user":
                    r = await flow.async_configure(r["flow_id"], {"host": ip, "port": 6053})
                else:
                    r = await flow.async_configure(r["flow_id"], {})
            t = str(r.get("type"))
            if "create_entry" in t:
                ok, text = True, "In Home Assistant eingebunden."
            elif "abort" in t:
                ok, text = r.get("reason") in ("already_configured", "already_in_progress"), f"HA meldet: {r.get('reason')}"
            else:
                ok, text = False, f"Unerwartet: {t} {r.get('step_id') or ''}"
            if ok and dhcp:
                # Die vom Router bekommene IP festschreiben und per WLAN aufspielen (schnellerer Start)
                self._zeile(dev, f"{text} Schreibe {ip} als feste IP fest ...")
                p = await asyncio.create_subprocess_exec(
                    "python3", str(ESPHOME_DIR / "tools" / "remotes.py"), "--fest", dev.replace("remote-wz-", ""), ip,
                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
                out = (await p.communicate())[0].decode(errors="replace").strip()
                if p.returncode != 0:
                    raise RuntimeError(f"Feste IP: {out}")
                self._zeile(dev, "Baue die Firmware mit fester IP und spiele sie per WLAN auf (Remote bitte wach halten) ...")
                if await self.ausfuehren(dev, "upload", ip, teil=True) != 0:
                    raise RuntimeError(f"Update per WLAN fehlgeschlagen ({j['fehler'] or 'siehe Protokoll'}) - Remote wach? "
                                       "Danach 'Nochmal versuchen'.")
                j["exit"] = None
                text = f"Eingebunden, feste IP {ip}. Tipp: im Router für die Remote reservieren."
            j["msg"] = text
            j["exit"] = 0 if ok else 1
            if ok and (b := self.hass.data.get("esphomeremote_konfig_bruecke")) is not None:
                await b.abgleichen()   # Bluetooth-Bruecke fuer die neue Remote, HA-Eintrag auf die Bruecke
        except Exception as err:  # noqa: BLE001
            j["msg"] = f"Fehler: {err}"
            j["exit"] = 1
        finally:
            j["running"] = False

    def token(self, dev: str) -> str:
        t = secrets.token_urlsafe(18)
        jetzt = time.monotonic()
        self.tokens = {k: v for k, v in self.tokens.items() if v[1] > jetzt}
        self.tokens[t] = (dev, jetzt + 1800)
        return t

    def token_dev(self, t: str) -> str | None:
        v = self.tokens.get(t)
        return v[0] if v and v[1] > time.monotonic() else None


class ManifestView(HomeAssistantView):
    """Manifest fuer ESP Web Tools (Flashen per USB im Browser)."""

    url = "/api/esphomeremote_konfig/manifest/{token}"
    name = "api:esphomeremote_konfig:manifest"
    requires_auth = False

    def __init__(self, ein: Einrichtung) -> None:
        self.ein = ein

    async def get(self, request: web.Request, token: str) -> web.Response:
        dev = self.ein.token_dev(token)
        if dev is None:
            return web.Response(status=403)
        m = {"name": dev.replace("remote-wz-", "Remote ").title(), "version": "1", "new_install_prompt_erase": True,
             "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": f"/api/esphomeremote_konfig/fw/{token}", "offset": 0}]}]}
        return web.json_response(m, headers={"Cache-Control": "no-store"})


class FirmwareView(HomeAssistantView):
    """factory.bin aus dem Device Builder durchreichen."""

    url = "/api/esphomeremote_konfig/fw/{token}"
    name = "api:esphomeremote_konfig:fw"
    requires_auth = False

    def __init__(self, ein: Einrichtung) -> None:
        self.ein = ein

    async def get(self, request: web.Request, token: str) -> web.StreamResponse:
        dev = self.ein.token_dev(token)
        if dev is None:
            return web.Response(status=403)
        hass = self.ein.hass
        r = await _befehl(hass, "firmware/download_token", {"configuration": flash_datei(dev), "file": "firmware.factory.bin"})
        p = await _port(hass)
        s = async_get_clientsession(hass)
        async with s.get(f"http://127.0.0.1:{p}/api/firmware/download", params={"token": r["token"]}) as fr:
            if fr.status != 200:
                return web.Response(status=502, text=f"ESPHome: {fr.status}")
            data = await fr.read()
        return web.Response(body=data, content_type="application/octet-stream", headers={"Cache-Control": "no-store"})


async def bau_aufraeumen(hass: HomeAssistant, dateien: list[str]) -> None:
    """Build-Ordner im ESPHome-Add-on loeschen (je Remote einige hundert MB), solange die Dateien noch da sind."""
    try:
        async with Dash(hass) as d:
            for datei in dateien:
                if (ESPHOME_DIR / datei).exists():
                    await d.befehl("firmware/clean", {"configuration": datei})
            await asyncio.sleep(2)
    except Exception as err:  # noqa: BLE001 - Aufraeumen ist nur nett, kein Muss
        _LOGGER.debug("Build-Ordner aufraeumen: %s", err)


def registrieren(hass: HomeAssistant, kd) -> Einrichtung:
    ein = Einrichtung(hass, kd)
    hass.http.register_view(ManifestView(ein))
    hass.http.register_view(FirmwareView(ein))

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/neu",
                                      vol.Required("name"): str, vol.Optional("ip", default=""): str,
                                      vol.Optional("vorlage"): str})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_neu(hass, connection, msg):
        if not (ESPHOME_DIR / "tools" / "remotes.py").exists() or not (ESPHOME_DIR / "templates" / "remote.yaml").exists():
            connection.send_error(msg["id"], "neu", "Der Assistent braucht die Entwickler-Einrichtung (esphome/tools/remotes.py). "
                                  "Sonst eine weitere Remote wie die erste anlegen (README) und in packages/esphomeremote_konfig.yaml eintragen.")
            return
        p = await asyncio.create_subprocess_exec(
            "python3", str(ESPHOME_DIR / "tools" / "remotes.py"), "--neu", msg["name"], msg["ip"].strip() or "dhcp",
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
        out = (await p.communicate())[0].decode(errors="replace").strip()
        if p.returncode != 0:
            connection.send_error(msg["id"], "neu", out.replace("FEHLER: ", "") or "Anlegen fehlgeschlagen")
            return
        dev = out.splitlines()[-1].strip()
        await kd.geraet_dazu(dev, msg.get("vorlage"))
        connection.send_result(msg["id"], {"device": dev})

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/job",
                                      vol.Required("device"): str, vol.Optional("start"): str,
                                      vol.Optional("port"): str})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_job(hass, connection, msg):
        dev = msg["device"]
        if dev not in kd.devices:
            connection.send_error(msg["id"], "unknown_device", dev)
            return
        st = msg.get("start")
        j = ein.job(dev)
        if st and not j["running"]:
            if st == "compile":
                hass.async_create_background_task(ein.ausfuehren(dev, "compile"), f"esphomeremote_bauen_{dev}")
            elif st == "usb":
                erlaubt = [p.get("port") for p in await _befehl(hass, "config/serial_ports")
                           if p.get("vid") == 0x303A or "jtag" in str(p.get("desc", "")).lower()]
                if msg.get("port") not in erlaubt:
                    connection.send_error(msg["id"], "port", "Kein ESP32 an diesem Port - aus Sicherheit nicht geflasht.")
                    return
                hass.async_create_background_task(ein.ausfuehren(dev, "upload", msg.get("port")), f"esphomeremote_usb_{dev}")
            elif st == "einbinden":
                hass.async_create_background_task(ein.einbinden(dev), f"esphomeremote_einbinden_{dev}")
            await asyncio.sleep(0.2)
        connection.send_result(msg["id"], ein.stand(dev))

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/flash_info",
                                      vol.Required("device"): str})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_flash_info(hass, connection, msg):
        dev = msg["device"]
        ports = []
        try:
            ports = await _befehl(hass, "config/serial_ports")
        except Exception as err:  # noqa: BLE001
            _LOGGER.warning("Serielle Ports nicht lesbar: %s", err)
        # NUR Espressif (ESP32-S3 meldet sich als "USB JTAG/serial debug unit", VID 0x303A) - sonst stuende
        # z. B. der Zigbee-Stick zur Auswahl und wuerde ueberschrieben.
        ports = [p for p in ports or [] if p.get("vid") == 0x303A or "jtag" in str(p.get("desc", "")).lower()]
        connection.send_result(msg["id"], {"manifest": f"/api/esphomeremote_konfig/manifest/{ein.token(dev)}",
                                           "ports": ports})

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/entfernen",
                                      vol.Required("device"): str})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_entfernen(hass, connection, msg):
        dev = msg["device"]
        if not kd.entfernbar(dev):
            connection.send_error(msg["id"], "geschuetzt", "Diese Remote kann hier nicht entfernt werden.")
            return
        weg = []
        # ESPHome-Eintrag in HA (Name bzw. Host passt zur Remote)
        for e in hass.config_entries.async_entries("esphome"):
            host = str(e.data.get("host", ""))
            if e.data.get("device_name") == dev or host in (dev, f"{dev}.local") or e.title.lower().replace(" ", "-") == dev:
                await hass.config_entries.async_remove(e.entry_id)
                weg.append("HA-Eintrag")
        await bau_aufraeumen(hass, [f"{dev}.yaml", f"{dev}-halink.yaml"])
        p = await asyncio.create_subprocess_exec(
            "python3", str(ESPHOME_DIR / "tools" / "remotes.py"), "--entfernen", dev.replace("remote-wz-", ""),
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
        await p.communicate()
        weg.append("YAML/Secret" if p.returncode == 0 else "YAML (Fehler)")
        await kd.geraet_weg(dev)
        ein.jobs.pop(dev, None)
        if (b := hass.data.get("esphomeremote_konfig_bruecke")) is not None:
            await b.abgleichen()
        connection.send_result(msg["id"], {"entfernt": weg})

    for cmd in (ws_neu, ws_job, ws_flash_info, ws_entfernen):
        websocket_api.async_register_command(hass, cmd)

    # ---- Dienst: einen ESPHome-Dienst auf allen verbundenen Remotes aufrufen ----
    async def an_remotes(call: ServiceCall) -> None:
        dienst = call.data["dienst"]
        daten = call.data.get("daten") or {}
        await an_alle(hass, kd.devices, dienst, daten)

    hass.services.async_register("esphomeremote_konfig", "an_remotes", an_remotes,
                                 schema=vol.Schema({vol.Required("dienst"): str, vol.Optional("daten"): dict}))
    return ein


async def an_alle(hass: HomeAssistant, devices: list[str], dienst: str, daten: dict) -> None:
    """ESPHome-Dienst <remote>_<dienst> bei allen verbundenen Remotes aufrufen, gleichzeitig.
    Verbunden = die ESPHome-Integration hat den Dienst (WLAN oder Bluetooth-Tunnel); eine schlafende Remote meldet
    einen Fehler bzw. haengt hoechstens 8 s und haelt die anderen nicht auf (2026-10-19: vorher nur mit dem Sensor
    ..._wlan_verbunden, den es nicht gibt - im WLAN kam nichts an, nur ueber den Tunnel)."""
    async def eins(slug: str) -> None:
        try:
            await asyncio.wait_for(hass.services.async_call("esphome", f"{slug}_{dienst}", daten, blocking=True), 8)
        except Exception as err:  # noqa: BLE001
            _LOGGER.debug("%s_%s: %s", slug, dienst, err)

    slugs = [d.replace("-", "_") for d in devices]
    await asyncio.gather(*(eins(s) for s in slugs if hass.services.has_service("esphome", f"{s}_{dienst}")))
