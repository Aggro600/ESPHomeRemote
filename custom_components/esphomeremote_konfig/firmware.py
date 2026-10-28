"""Firmware-Tab (2026-10-16): Remotes und Docks zentral aktualisieren.

Quelle der Firmware:
  Remotes: Vorlagen templates/remote.yaml + templates/remote-halink.yaml, tools/remotes.py erzeugt daraus fuer jede
           Remote (tools/remotes.json) remote-wz-<x>.yaml und remote-wz-<x>-halink.yaml. Geflasht wird die -halink-Datei.
  Docks:   dock-<x>.yaml bindet templates/dock-basis.yaml ein.
Ob ein Update ansteht, sagt der ESPHome Device Builder (Add-on): Konfig-Hash der Datei gegen den Hash auf dem Geraet
(has_pending_changes). Gebaut und geflasht wird ueber dessen WebSocket-API (ws://127.0.0.1:<ingress_port>/ws).

Wege:
  "OTA"      per WLAN. Remote mit WLAN aus: vorher das Wartungsfenster per HA-Taste oeffnen (geht ueber Bluetooth).
  "/dev/tty" USB am HA-Rechner (nur Espressif-Ports, nie z. B. den Zigbee-Stick).
  ""         nur bauen - fuer USB im Browser (Web Serial, Panel) oder Download (factory.bin).

Update von der Remote aus (2026-10-17): Wartung & Tests > "Firmware aktualisieren" schickt esphome.remote_update ->
derselbe Ablauf wie "OTA". Im Konfigurator laesst sich ein Update vormerken (Remote gerade offline): es startet beim
naechsten esphome.remote_verbunden. Den Stand bekommt die Remote ueber ihre API-Aktion fw_status(meldung, stufe).
"""

from __future__ import annotations

import asyncio
from collections import deque
import json
import logging
import os
from pathlib import Path
import re
import time
import uuid
from typing import Any

import aiohttp
import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.storage import Store

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"
ADDON = os.environ.get("ESPHOME_ADDON", "5c53de3b_esphome")
ESPHOME_DIR = Path("/config/esphome")
DOCK_BASIS = "templates/dock-basis.yaml"
ESPRESSIF_VID = 0x303A
_DASH_ENDE = {"completed", "failed", "cancelled", "canceled", "error"}
# Gesperrt wird nur noch von Hand im Firmware-Bereich (die Vorgabe "Remote 1 gesperrt" vom 13.10. galt der alten,
# defekten Platine - die erste Remote neue Remote darf geflasht werden)

_jobs: dict[str, dict] = {}


# ---------------------------------------------------------------------------------------------- Add-on
async def _port(hass: HomeAssistant) -> int:
    p = hass.data.get(DOMAIN + "_dashport")
    if p:
        return p
    tok = os.environ.get("SUPERVISOR_TOKEN")
    if not tok:
        raise RuntimeError("Kein Supervisor - ESPHome-Add-on nicht erreichbar")
    s = async_get_clientsession(hass)
    async with s.get(f"http://supervisor/addons/{ADDON}/info", headers={"Authorization": "Bearer " + tok}) as r:
        d = (await r.json())["data"]
    if d.get("state") != "started":
        raise RuntimeError("ESPHome-Add-on laeuft nicht")
    hass.data[DOMAIN + "_dashport"] = d["ingress_port"]
    return d["ingress_port"]


class Dash:
    """Eine Verbindung zur WebSocket-API des Device Builders."""

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self.ws: aiohttp.ClientWebSocketResponse | None = None
        self.n = 0

    async def __aenter__(self) -> Dash:
        s = async_get_clientsession(self.hass)
        self.ws = await s.ws_connect(f"http://127.0.0.1:{await _port(self.hass)}/ws", heartbeat=30,
                                     max_msg_size=16 * 1024 * 1024)
        while True:   # Begruessung {"server_version": ...}
            m = await asyncio.wait_for(self.ws.receive_json(), 10)
            if "server_version" in m:
                return self

    async def __aexit__(self, *a) -> None:
        if self.ws is not None:
            await self.ws.close()

    async def senden(self, cmd: str, args: dict | None = None) -> str:
        self.n += 1
        mid = str(self.n)
        m: dict[str, Any] = {"command": cmd, "message_id": mid}
        if args:
            m["args"] = args
        await self.ws.send_json(m)
        return mid

    async def befehl(self, cmd: str, args: dict | None = None, timeout: float = 30) -> Any:
        mid = await self.senden(cmd, args)
        ende = time.monotonic() + timeout
        while True:
            m = await asyncio.wait_for(self.ws.receive_json(), max(0.1, ende - time.monotonic()))
            if m.get("message_id") != mid or "event" in m:
                continue
            if "error_code" in m:
                raise RuntimeError(f"{cmd}: {m.get('details') or m['error_code']}")
            return m.get("result")


async def dash(hass: HomeAssistant, cmd: str, args: dict | None = None, timeout: float = 30) -> Any:
    async with Dash(hass) as d:
        return await d.befehl(cmd, args, timeout)


async def esp_ports(hass: HomeAssistant) -> list[dict]:
    """Serielle Ports des Add-ons - nur Espressif-USB (C3/S3 mit eingebautem USB)."""
    return [p for p in (await dash(hass, "config/serial_ports") or []) if p.get("vid") == ESPRESSIF_VID]


# ---------------------------------------------------------------------------------------------- Geraete
def _lesen(pfad: Path) -> str:
    try:
        return pfad.read_text(encoding="utf-8")
    except OSError:
        return ""


def dock_dateien() -> list[dict]:
    out = []
    for f in sorted(ESPHOME_DIR.glob("*.yaml")):
        t = _lesen(f)   # jeder Dateiname (2026-10-26: von Hand angelegte Docks hiessen nicht immer dock-...)
        if "dock-basis.yaml" not in t and DOCK_PAKET not in t and DOCK_PAKET_ALT not in t:   # eigene Docks und GitHub
            continue
        name = re.search(r"^\s+name:\s*(\S+)", t, re.M)
        fn = re.search(r"^\s+friendly_name:\s*(.+)$", t, re.M)
        out.append({"art": "dock", "datei": f.name, "name": name.group(1) if name else f.stem,
                    "friendly": fn.group(1).strip().strip("\"'") if fn else f.stem,
                    # angelegt vor 2026.10.26.3: bindet die lokale Vorlage ein, die es nur beim Entwickler gibt
                    "alt": ("!include templates/dock-basis.yaml" in t and not (ESPHOME_DIR / DOCK_BASIS).exists())
                    or (DOCK_PAKET_ALT in t and DOCK_PAKET not in t)})   # 2026-10-27: alter Dateiname im Repo
    return out


def remote_dateien() -> list[dict]:
    out = []
    for f in sorted(ESPHOME_DIR.glob("remote-wz-*.yaml")):
        if re.search(r"-(halink|sd)\.yaml$", f.name):
            continue
        slug = f.stem.removeprefix("remote-wz-")
        flash = ESPHOME_DIR / f"remote-wz-{slug}-halink.yaml"
        out.append({"art": "remote", "datei": flash.name if flash.exists() else f.name, "name": f.stem,
                    "slug": slug, "friendly": "Remote " + slug.capitalize()})
    # Remotes anderer Nutzer (2026-10-26): Datei mit dem Paket esphomeremote-firmware.yaml aus dem Repo
    # (esphomeremote.yaml; bis 2026-10-27 hiessen sie open-remote-firmware.yaml / open-remote.yaml)
    for f in sorted(ESPHOME_DIR.glob("*.yaml")):
        if f.name.startswith(("remote-wz-", "secrets")):
            continue
        t = _lesen(f)
        if REMOTE_PAKET not in t and REMOTE_PAKET_ALT not in t:
            continue
        sub = lambda k, d: (re.search(rf"^\s+{k}:\s*\"?([^\"#\n]+?)\"?\s*(#.*)?$", t, re.M) or [None, d])[1]
        name = sub("device_name", f.stem)
        out.append({"art": "remote", "datei": f.name, "name": name, "slug": name.replace("-", "_"),
                    "ha": sub("ha_name", name.replace("-", "_")), "friendly": sub("friendly_name", name),
                    # aeltere Datei: Firmware von @master, ohne durchgereichte Passwoerter (!secret im Paket) oder alter Name
                    "alt": "konfig_key:" not in t or "@master" in t or REMOTE_PAKET not in t})
    return out


def test_dateien() -> list[dict]:
    """Hardware-Test (2026-10-22, vorher Platinen-Test nur Mikrofon): hardware-test*.yaml - prueft eine Remote-Platine
    Bauteil fuer Bauteil. Ergebnisse kommen aus den Entitaeten des Geraets (liste: "test")."""
    out = []
    for f in sorted(ESPHOME_DIR.glob("hardware-test*.yaml")):
        t = _lesen(f)
        name = re.search(r"^\s+name:\s*(\S+)", t, re.M)
        fn = re.search(r"^\s+friendly_name:\s*(.+)$", t, re.M)
        out.append({"art": "test", "datei": f.name, "name": name.group(1) if name else f.stem,
                    "friendly": fn.group(1).strip().strip("\"'") if fn else f.stem})
    return out


def alle_dateien() -> list[dict]:
    return remote_dateien() + dock_dateien() + test_dateien()


def _test_stand(hass: HomeAssistant, entry) -> dict:
    """Ergebnisse des Hardware-Tests aus den Entitaeten des Geraets. Der Konfigurator liest die Zustaende live aus
    hass.states (entity) und ordnet sie nach Namen; knoepfe = Name -> button-Entitaet (OK/Fehler, IR senden ...)."""
    from homeassistant.helpers import entity_registry as er

    werte, knopf, knoepfe = [], None, {}
    for e in er.async_entries_for_config_entry(er.async_get(hass), entry.entry_id):
        st = hass.states.get(e.entity_id)
        name = e.original_name or e.entity_id
        if e.domain == "button":
            knoepfe[name] = e.entity_id
            if "wiederholen" in e.entity_id:
                knopf = e.entity_id
        elif e.domain == "sensor" and st is not None:
            werte.append({"name": name, "wert": st.state, "diag": bool(e.entity_category), "entity": e.entity_id})
    werte.sort(key=lambda x: (x["diag"], not x["name"].startswith("Pad"), x["name"]))
    return {"werte": werte, "knopf": knopf, "knoepfe": knoepfe}


def entwickler() -> bool:
    """Entwicklermodus (2026-10-18): Testwerkzeuge remote-dev-tests.yaml mitbauen? tools/entwickler.json, fehlt = an."""
    try:
        return bool(json.loads((ESPHOME_DIR / "tools" / "entwickler.json").read_text(encoding="utf-8")).get("an", True))
    except (OSError, ValueError):
        return True


async def remotes_erzeugen() -> str:
    """tools/remotes.py: remote-wz-<x>.yaml und -halink.yaml aus templates/remote*.yaml neu schreiben."""
    skript = ESPHOME_DIR / "tools" / "remotes.py"
    if not skript.exists():
        return ""
    p = await asyncio.create_subprocess_exec("python3", str(skript), stdout=asyncio.subprocess.PIPE,
                                             stderr=asyncio.subprocess.STDOUT)
    out, _ = await p.communicate()
    return out.decode(errors="replace").strip()


def _offen(host: str, port: int, timeout: float = 1.5) -> bool:
    import socket

    try:
        with socket.create_connection((host, port), timeout):
            return True
    except OSError:
        return False


def _mac_an(ip: str) -> str | None:
    """MAC-Adresse hinter einer IP aus dem ARP-Speicher (nach einem Verbindungsversuch gefuellt)."""
    try:
        with open("/proc/net/arp", encoding="ascii") as f:
            for z in f.read().splitlines()[1:]:
                t = z.split()
                if len(t) >= 4 and t[0] == ip and t[3] != "00:00:00:00:00:00":
                    return t[3].lower()
    except OSError:
        pass
    return None


def _erwartete_mac(hass: HomeAssistant, name: str) -> str | None:
    """WLAN-MAC des Geraets laut ESPHome-Integration (unique_id des Eintrags)."""
    for e in hass.config_entries.async_entries("esphome"):
        if e.data.get("device_name") == name and e.unique_id and re.fullmatch(r"([0-9a-f]{2}:){5}[0-9a-f]{2}", e.unique_id):
            return e.unique_id
    return None


# ---------------------------------------------------------------------------------------------- Jobs
def _job(titel: str, dateien: list[str], port: str) -> dict:
    jid = uuid.uuid4().hex[:12]
    j = {"job_id": jid, "titel": titel, "dateien": dateien, "port": port, "status": "laeuft", "schritt": "",
         "progress": None, "log": deque(maxlen=500), "fehler": "", "fertig": False, "t0": time.time(),
         "aktuell": None, "ergebnisse": {}}
    _jobs[jid] = j
    return j


def _log(j: dict, z: str) -> None:
    z = re.sub(r"\\033\[[0-9;]*[A-Za-z]|\x1b\[[0-9;]*[A-Za-z]", "", str(z or "")).rstrip()
    if z:
        j["log"].append(z[:300])


async def _dash_job(hass: HomeAssistant, j: dict, cmd: str, args: dict) -> bool:
    """Einen Auftrag im Device Builder starten und bis zum Ende mitlesen. True = erfolgreich.
    firmware/install legt zwei Auftraege an: bauen (Antwort) und hochladen (depends_on = Bau-Auftrag) - beide verfolgen."""
    async with Dash(hass) as d:
        r = await d.befehl(cmd, args)
    if not await _folgen(hass, j, r["job_id"], r.get("status")):
        return False
    if cmd != "firmware/install":
        return True
    async with Dash(hass) as d:
        for _ in range(10):
            jobs = await d.befehl("firmware/get_jobs", {})
            up = next((x for x in jobs or [] if x.get("depends_on") == r["job_id"]), None)
            if up:
                break
            await asyncio.sleep(1)
    if up is None:
        j["fehler"] = "Hochladen wurde nicht gestartet"
        return False
    j["schritt"] = j["schritt"].replace("bauen und installieren", "hochladen")
    j["progress"] = None
    return await _folgen(hass, j, up["job_id"], up.get("status"))


async def _folgen(hass: HomeAssistant, j: dict, djid: str, status: str | None) -> bool:
    """Auftrag bis zum Ende verfolgen. Reisst die Verbindung zum Device Builder ab (Volllast beim Kompilieren,
    Add-on-Neustart), laeuft der Auftrag dort weiter - dann neu verbinden und weiterlesen statt aufzugeben (2026-10-18:
    der Einrichtungs-Assistent blieb sonst ohne Ende und ohne Knopf stehen)."""
    abbrueche, mitlesen = 0, True
    while status not in _DASH_ENDE:
        try:
            status = await _folgen_verbindung(hass, j, djid, status, mitlesen)
        except asyncio.CancelledError:
            raise
        except Exception as err:  # noqa: BLE001 - Verbindung weg: der Auftrag laeuft im Add-on weiter
            abbrueche += 1
            if abbrueche > 30:
                j["fehler"] = f"Verbindung zum ESPHome-Add-on verloren ({err})"
                return False
            _log(j, f"Verbindung zum ESPHome-Add-on unterbrochen - lese weiter ({abbrueche}) ...")
            mitlesen = False   # die Ausgabe nicht doppelt holen, nur noch den Stand abfragen
            await asyncio.sleep(5)
    return status == "completed"


async def _folgen_verbindung(hass: HomeAssistant, j: dict, djid: str, status: str | None, mitlesen: bool) -> str | None:
    async with Dash(hass) as d:
        folge = await d.senden("firmware/follow_job", {"job_id": djid}) if mitlesen else None
        abfrage, naechste = None, 0.0
        while status not in _DASH_ENDE:
            if abfrage is None and time.monotonic() >= naechste:
                abfrage = await d.senden("firmware/get_job", {"job_id": djid})
                naechste = time.monotonic() + 2
            try:
                m = await asyncio.wait_for(d.ws.receive_json(), 2)
            except asyncio.TimeoutError:
                continue
            mid = m.get("message_id")
            if folge and mid == folge and m.get("event") == "output":
                data = m.get("data")
                for z in (data if isinstance(data, list) else [data]):
                    _log(j, z)
            elif mid == abfrage and ("result" in m or "error_code" in m):
                abfrage = None
                g = m.get("result") or {}
                status = g.get("status") or status
                if isinstance(g.get("progress"), (int, float)):
                    j["progress"] = g["progress"]
                if status in _DASH_ENDE and status != "completed":
                    j["fehler"] = g.get("failure_reason") or g.get("error") or f"Exit {g.get('exit_code')}"
                    if not mitlesen:   # Ende der Ausgabe nachtragen (fuer die Fehlersuche und den 2. Versuch)
                        for z in (g.get("output") or [])[-40:]:
                            _log(j, z)
        return status


async def _mit_versuch(hass: HomeAssistant, j: dict, name: str, text: str, cmd: str, args: dict) -> bool:
    """Auftrag im Device Builder, bei bekanntem Bau-Aussetzer (erste Uebersetzung nach einer Aenderung) ein 2. Versuch."""
    for versuch in (1, 2):
        j["schritt"] = f"{name}: {text}" + (" (2. Versuch)" if versuch == 2 else "")
        if await _dash_job(hass, j, cmd, args):
            return True
        rest = "\n".join(list(j["log"])[-40:])
        if versuch == 1 and re.search(r"No such file or directory|Exit 2\b|exit code 2|MDNSComponent", rest + j["fehler"]):
            _log(j, "Bau-Aussetzer - zweiter Versuch")
            if "MDNSComponent" in rest and args.get("configuration"):
                # Veralteter Build-Stand nach einer YAML-Aenderung (mDNS aus): nur Aufraeumen hilft (2026-10-18)
                try:
                    await dash(hass, "firmware/clean", {"configuration": args["configuration"]})
                    _log(j, "Build-Ordner aufgeraeumt")
                except Exception as err:  # noqa: BLE001
                    _log(j, f"Aufraeumen fehlgeschlagen: {err}")
            j["fehler"] = ""
            continue
        return False
    return False


async def _geraet(hass: HomeAssistant, j: dict, g: dict, port: str, info: dict) -> bool:
    """Ein Geraet bauen/flashen (mit Wartungsfenster und einem zweiten Versuch bei Bau-Aussetzern)."""
    datei = g["datei"]
    j["aktuell"] = datei
    j["progress"] = None
    j["fehler"] = ""
    name = g.get("friendly") or datei
    if g["art"] == "remote":
        j["schritt"] = f"{name}: Remote-Dateien aus der Vorlage erzeugen"
        _log(j, await remotes_erzeugen())
    ziel = port
    if g["art"] == "remote" and port == "OTA":
        # Erst bauen (dauert Minuten), dann das 3-min-WLAN-Fenster oeffnen - sonst ist es vor dem Hochladen zu
        if not await _mit_versuch(hass, j, name, "bauen", "firmware/compile", {"configuration": datei}):
            j["ergebnisse"][datei] = j["fehler"] or "Fehler"
            return False
    if port == "OTA":
        ip = info.get("ip") or ((info.get("runtime_state") or {}).get("ip_addresses") or [None])[0]
        # Remotes: feste IP aus secrets.yaml (das Dashboard merkt sich sonst eine alte Adresse, 2026-10-17: .60 statt .160)
        b = hass.data.get("esphomeremote_konfig_bruecke")
        fest = (b.info().get(g.get("name") or "") or {}).get("ip") if b and g["art"] == "remote" else None
        ip = fest or ip
        ziel = ip or "OTA"
        if g["art"] == "remote" and ip and not await hass.async_add_executor_job(_offen, ip, 3232):
            knopf = f"button.remote_wz_{g['slug']}_wartungsfenster_jetzt"
            st = hass.states.get(knopf)
            ds = hass.data.get("esphomeremote_konfig_dockstand")
            dev = g.get("name") or ""
            gedrueckt = False
            if st is not None and st.state != "unavailable":
                j["schritt"] = f"{name}: Wartungsfenster oeffnen (WLAN 3 min an)"
                _log(j, j["schritt"])
                try:
                    await hass.services.async_call("button", "press", {"entity_id": knopf}, blocking=True)
                    gedrueckt = True
                except Exception as err:  # noqa: BLE001
                    # Nach einem HA-Neustart steht der Knopf auf "verfuegbar", obwohl die Remote nie verbunden war
                    # (2026-10-25) - dann ueber das Dock
                    _log(j, f"{name}: Knopf nicht erreichbar ({err}) - versuche es ueber das Dock")
            if gedrueckt:
                frist, ueber_dock = 120, False
            elif ds is not None and dev:
                # Ohne HA-Verbindung (Funkkonzept "Dock, WLAN als Ausweg", 2026-10-25): ueber den Dock-Stand anfordern -
                # die Remote schaltet ihr WLAN an, sobald sie das Dock das naechste Mal fragt (beim Benutzen sofort)
                ds.wartung_anfordern(dev)
                j["schritt"] = f"{name}: Wartungsfenster ueber das Dock angefordert - Remote kurz benutzen (Taste druecken)"
                _log(j, j["schritt"])
                frist, ueber_dock = 900, True
            else:
                j["fehler"] = f"{name} ist nicht erreichbar - eine Taste druecken und nochmal"
                return False
            ende = time.monotonic() + frist
            try:
                while not await hass.async_add_executor_job(_offen, ip, 3232):
                    if time.monotonic() > ende:
                        j["fehler"] = f"{name}: WLAN kam nicht ({frist // 60} min)"
                        return False
                    await asyncio.sleep(3)
            finally:
                if ueber_dock:
                    ds.wartung_fertig(dev)
            _log(j, f"{name}: WLAN da ({ip})")
        # Antwortet unter der IP wirklich dieses Geraet? (2026-10-16: ein neues Dock bekam per DHCP die erste Remote feste
        # IP - ohne Pruefung waere die Remote-Firmware an das Dock gegangen)
        soll = _erwartete_mac(hass, g.get("name") or "")
        if ip and soll:
            await hass.async_add_executor_job(_offen, ip, 3232)
            ist = await hass.async_add_executor_job(_mac_an, ip)
            if ist and ist != soll:
                j["fehler"] = (f"{name}: unter {ip} antwortet ein anderes Geraet ({ist.upper()}, erwartet {soll.upper()}) - "
                               "IP-Konflikt, nicht geflasht")
                return False
    if port:
        ok = await _mit_versuch(hass, j, name, "bauen und installieren", "firmware/install",
                                {"configuration": datei, "port": ziel, "force_local": False, "bootloader": False})
    else:
        ok = await _mit_versuch(hass, j, name, "bauen", "firmware/compile", {"configuration": datei})
    j["ergebnisse"][datei] = "ok" if ok else (j["fehler"] or "Fehler")
    if ok and port:   # aufgespielte Version merken - Remotes ueber das Dock melden sie HA selten (keine API-Verbindung)
        from homeassistant.loader import async_get_integration
        v = str((await async_get_integration(hass, DOMAIN)).version or "")
        st, d = await _speicher(hass)
        d.setdefault("versionen", {})[datei] = v
        await st.async_save(d)
    return ok


async def _ablauf(hass: HomeAssistant, j: dict, geraete: list[dict], port: str) -> None:
    try:
        infos = {}
        try:
            for x in (await dash(hass, "devices/list")).get("configured", []):
                infos[x.get("configuration")] = x
        except Exception as err:  # noqa: BLE001
            _log(j, f"Geraeteliste: {err}")
        fehler = []
        for g in geraete:
            if not await _geraet(hass, j, g, port, infos.get(g["datei"]) or {}):
                fehler.append(f"{g.get('friendly') or g['datei']}: {j['fehler'] or 'Fehler'}")
        j["status"] = "fehler" if fehler else "fertig"
        j["fehler"] = "; ".join(fehler)
        j["schritt"] = "Fertig" if not fehler else "Fehler"
    except Exception as err:  # noqa: BLE001
        _LOGGER.exception("Firmware-Auftrag")
        j["status"], j["fehler"] = "fehler", str(err)
    j["fertig"] = True
    j["aktuell"] = None


# ---------------------------------------------------------------------------------------------- Websocket
async def _speicher(hass: HomeAssistant) -> tuple[Store, dict]:
    st = hass.data.get(DOMAIN + "_fwstore")
    if st is None:
        st = hass.data[DOMAIN + "_fwstore"] = Store(hass, 1, DOMAIN + "_firmware")
    d = await st.async_load() or {}
    d.setdefault("gesperrt", [])
    d.setdefault("vorgemerkt", [])
    return st, d


async def _gesperrt(hass: HomeAssistant) -> tuple[Store, list[str]]:
    st, d = await _speicher(hass)
    return st, d["gesperrt"]


async def liste(hass: HomeAssistant) -> dict:
    dateien = await hass.async_add_executor_job(alle_dateien)
    infos, ports, fehler = {}, [], ""
    try:
        async with Dash(hass) as d:
            for x in (await d.befehl("devices/list")).get("configured", []):
                infos[x.get("configuration")] = x
            ports = [p for p in (await d.befehl("config/serial_ports") or []) if p.get("vid") == ESPRESSIF_VID]
    except Exception as err:  # noqa: BLE001
        fehler = f"ESPHome-Add-on: {err}"
    _, sp = await _speicher(hass)
    gesperrt, vorgemerkt = sp["gesperrt"], sp["vorgemerkt"]
    eintraege = {e.data.get("device_name"): e for e in hass.config_entries.async_entries("esphome")}
    # Versionen (2026-10-26): Projektversion der Firmware (HA-Geraet "sw_version" = "<version> (ESPHome x)") gegen die
    # Version der Integration (manifest.json) - die bringt jedes Release mit
    from homeassistant.helpers import device_registry as dr
    from homeassistant.loader import async_get_integration
    soll = str((await async_get_integration(hass, DOMAIN)).version or "")
    reg = dr.async_get(hass)
    gemerkt = sp.get("versionen") or {}
    def fw_version(e, datei: str) -> str:
        # Projektversion legt HA als hw_version ab (manufacturer/model aus project.name); aufgespielt ueber den
        # Konfigurator: gemerkte Version (neuer Stand, falls HA das Geraet seitdem nicht direkt gesehen hat)
        v = ""
        if e is not None:
            for d in dr.async_entries_for_config_entry(reg, e.entry_id):
                if re.fullmatch(r"\d{4}\.\d+\.\d+(\.\d+)?", d.hw_version or ""):
                    v = d.hw_version
        g = gemerkt.get(datei, "")
        return max(v, g, key=lambda x: [int(t) for t in x.split(".")] if x else [])
    out = []
    for f in dateien:
        g = infos.get(f["datei"]) or {}
        rt = g.get("runtime_state") or {}
        dev_name = f["name"]
        e = eintraege.get(dev_name)
        x = {**f, "ip": g.get("ip") or (rt.get("ip_addresses") or [None])[0], "online": rt.get("state") == "online",
             "version": rt.get("deployed_version") or "", "geflasht": bool(rt.get("deployed_config_hash")),
             "update": bool(g.get("has_pending_changes")) and bool(rt.get("deployed_config_hash")),
             "hash_ist": rt.get("deployed_config_hash") or "", "hash_soll": g.get("expected_config_hash") or "",
             "in_ha": e is not None, "gesperrt": f["datei"] in gesperrt, "vorgemerkt": f["datei"] in vorgemerkt,
             "fw": fw_version(e, f["datei"])}
        x["veraltet"] = bool(soll) and x["geflasht"] and f["art"] in ("remote", "dock") and x["fw"] != soll
        if f["art"] == "test":
            x["test"] = _test_stand(hass, e) if e else None
        elif f["art"] == "dock":
            slug = dev_name.replace("-", "_")
            sender = f"infrared.{slug}_ir_sender"
            x["sender"] = sender if hass.states.get(sender) else None
            x["bt_mac"] = e.data.get("bluetooth_mac_address") if e else None
        else:
            pre = f.get("ha") or f"remote_wz_{f['slug']}"
            ha = hass.states.get(f"binary_sensor.{pre}_ha_verbunden")
            wlan = hass.states.get(f"binary_sensor.{pre}_wlan_aktiv")
            x["ha_da"] = bool(ha and ha.state == "on")
            b = hass.data.get("esphomeremote_konfig_bruecke")
            x["bruecke"] = b.info().get(dev_name) if b else None
            x["wlan"] = bool(wlan and wlan.state == "on")
            # "online" (per WLAN erreichbar) sagt die Remote selbst, wenn sie mit HA verbunden ist: der Ping-Stand des
            # Add-ons hing nach "Nur Bluetooth" minutenlang auf "online" (2026-10-18), obwohl die IP nicht antwortete.
            if wlan and wlan.state in ("on", "off"):
                x["online"] = x["wlan"]
        out.append(x)
    laufend = [{k: v for k, v in j.items() if k in ("job_id", "titel", "dateien", "port")}
               for j in _jobs.values() if not j["fertig"]]
    return {"geraete": out, "ports": ports, "fehler": fehler, "jobs": laufend, "version": soll,
            "lokal": await hass.async_add_executor_job(lambda: (ESPHOME_DIR / "templates" / "remote.yaml").exists()),
            "entwickler": await hass.async_add_executor_job(entwickler)}


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_liste"})
@websocket_api.async_response
async def ws_liste(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    connection.send_result(msg["id"], await liste(hass))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_installieren",
                                  vol.Required("dateien"): [str], vol.Required("port"): str})
@websocket_api.async_response
async def ws_installieren(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    port = msg["port"]
    bekannt = {g["datei"]: g for g in await hass.async_add_executor_job(alle_dateien)}
    geraete = [bekannt[d] for d in msg["dateien"] if d in bekannt]
    if not geraete or len(geraete) != len(msg["dateien"]):
        connection.send_error(msg["id"], "datei", "Unbekannte Firmware-Datei")
        return
    if port not in ("", "OTA"):
        try:
            ok = any(p.get("port") == port for p in await esp_ports(hass))
        except Exception:  # noqa: BLE001
            ok = False
        if not ok or len(geraete) != 1:
            connection.send_error(msg["id"], "port", f"{port} ist kein ESP32-USB-Port")
            return
    _, gesperrt = await _gesperrt(hass)
    if port and any(g["datei"] in gesperrt for g in geraete):
        connection.send_error(msg["id"], "gesperrt", "Gesperrt (z. B. Platine defekt) - erst entsperren")
        return
    if any(not j["fertig"] for j in _jobs.values()):
        connection.send_error(msg["id"], "laeuft", "Es laeuft schon ein Firmware-Auftrag")
        return
    j = _starten(hass, geraete, port)
    connection.send_result(msg["id"], {"job_id": j["job_id"]})


def _starten(hass: HomeAssistant, geraete: list[dict], port: str) -> dict:
    titel = (geraete[0].get("friendly") or geraete[0]["datei"]) if len(geraete) == 1 else f"{len(geraete)} Geraete"
    j = _job(titel, [g["datei"] for g in geraete], port)
    hass.async_create_background_task(_ablauf(hass, j, geraete, port), f"{DOMAIN} firmware {j['job_id']}")
    # Remotes per WLAN: Stand laufend auf der Remote zeigen (Wartung & Tests)
    if port == "OTA":
        for g in geraete:
            if g["art"] == "remote":
                hass.async_create_background_task(_melden(hass, j, g), f"{DOMAIN} firmware status {g['name']}")
    return j


# ---------------------------------------------------------------------------------------------- Update von der Remote
_letzte_info: dict[str, float] = {}       # Geraet -> Zeit der letzten "Update da?"-Meldung (hoechstens alle 10 min)
_letzter_versuch: dict[str, float] = {}   # Geraet -> letzter Start eines vorgemerkten Updates


async def _an_remote(hass: HomeAssistant, g: dict, text: str, stufe: int) -> None:
    """Stand an die Remote (API-Aktion fw_status; aeltere Firmware ohne die Aktion: nichts)."""
    dienst = f"{g['name'].replace('-', '_')}_fw_status"
    if not hass.services.has_service("esphome", dienst):
        return
    try:
        await asyncio.wait_for(hass.services.async_call("esphome", dienst, {"meldung": text[:300], "stufe": stufe},
                                                        blocking=True), 10)
    except Exception:  # noqa: BLE001 - Remote gerade weg (z. B. Neustart nach dem Hochladen)
        pass


def _klartext(schritt: str, name: str) -> str:
    t = schritt.removeprefix(f"{name}: ")
    return {"bauen": "Home Assistant baut die Firmware. Das dauert einige Minuten.",
            "bauen (2. Versuch)": "Home Assistant baut die Firmware (2. Versuch).",
            "bauen und installieren": "Firmware ist gebaut und wird gleich übertragen.",
            "bauen und installieren (2. Versuch)": "Firmware wird gleich übertragen (2. Versuch).",
            "Remote-Dateien aus der Vorlage erzeugen": "Update wird vorbereitet.",
            "Wartungsfenster oeffnen (WLAN 3 min an)": "WLAN wird für die Übertragung eingeschaltet.",
            "hochladen": "Firmware wird übertragen. Die Remote startet danach neu."}.get(
        t, t[:1].upper() + t[1:] + "." if t else "Update läuft.")


async def _melden(hass: HomeAssistant, j: dict, g: dict) -> None:
    """Waehrend des Auftrags: Stand an die Remote, bei Aenderung sofort, sonst jede Minute (haelt sie wach)."""
    name = g.get("friendly") or g["datei"]
    alt, zuletzt = None, 0.0
    while not j["fertig"]:
        if j.get("aktuell") == g["datei"]:
            text = _klartext(j.get("schritt") or "", name)
            if isinstance(j.get("progress"), (int, float)) and "übertragen" in text:
                text = f"Firmware wird übertragen: {int(j['progress'])} %. Die Remote startet danach neu."
            if text != alt or time.monotonic() - zuletzt > 60:
                alt, zuletzt = text, time.monotonic()
                await _an_remote(hass, g, text, 1)
        await asyncio.sleep(3)
    erg = j["ergebnisse"].get(g["datei"])
    if erg == "ok":
        await _vorgemerkt_entfernen(hass, g["datei"])
        await _an_remote(hass, g, "Fertig. Die Remote startet mit der neuen Firmware neu.", 2)
    elif erg is not None:
        await _an_remote(hass, g, f"Update fehlgeschlagen: {erg}", 3)


async def remote_update(hass: HomeAssistant, geraet: str, quelle: str) -> str | None:
    """Update einer Remote per WLAN starten. Rueckgabe: Fehlertext oder None."""
    g = next((x for x in await hass.async_add_executor_job(remote_dateien) if x["name"] == geraet), None)
    if g is None:
        return "unbekannte Remote"
    _, sp = await _speicher(hass)
    if g["datei"] in sp["gesperrt"]:
        await _an_remote(hass, g, "Im Konfigurator gesperrt (Firmware > Sperren) - erst dort entsperren.", 3)
        return "gesperrt"
    if any(not x["fertig"] for x in _jobs.values()):
        laeuft = next(x for x in _jobs.values() if not x["fertig"])
        if g["datei"] in laeuft["dateien"]:
            return None   # laeuft schon fuer diese Remote
        await _an_remote(hass, g, "Gerade läuft ein anderes Update. Bitte gleich nochmal versuchen.", 3)
        return "laeuft"
    _LOGGER.info("Firmware-Update fuer %s (%s)", geraet, quelle)
    _starten(hass, [g], "OTA")
    return None


async def _vorgemerkt_entfernen(hass: HomeAssistant, datei: str) -> None:
    st, sp = await _speicher(hass)
    if datei in sp["vorgemerkt"]:
        sp["vorgemerkt"].remove(datei)
        await st.async_save(sp)


async def _ereignis_update(hass: HomeAssistant, event) -> None:
    geraet = event.data.get("geraet") or ""
    fehler = await remote_update(hass, geraet, "Remote")
    if fehler:
        _LOGGER.info("Firmware-Update fuer %s nicht gestartet: %s", geraet, fehler)


async def _ereignis_verbunden(hass: HomeAssistant, event) -> None:
    """Remote ist (wieder) da: vorgemerktes Update starten, sonst hoechstens alle 10 min sagen, ob eins ansteht."""
    geraet = event.data.get("geraet") or ""
    g = next((x for x in await hass.async_add_executor_job(remote_dateien) if x["name"] == geraet), None)
    if g is None:
        return
    _, sp = await _speicher(hass)
    if g["datei"] in sp["vorgemerkt"] and not any(not x["fertig"] for x in _jobs.values()):
        # Vormerkung bleibt bis zum Erfolg (entfernt in _melden); nach einem Fehlschlag fruehestens nach 30 min neu
        if time.monotonic() - _letzter_versuch.get(geraet, -1e9) > 1800:
            _letzter_versuch[geraet] = time.monotonic()
            await asyncio.sleep(5)   # erst ankommen lassen (Einstellungen, Helligkeit ...)
            await remote_update(hass, geraet, "vorgemerkt")
            return
    if any(not x["fertig"] and g["datei"] in x["dateien"] for x in _jobs.values()):
        return
    if time.monotonic() - _letzte_info.get(geraet, -1e9) < 600:
        return
    _letzte_info[geraet] = time.monotonic()
    try:
        infos = {x.get("configuration"): x for x in (await dash(hass, "devices/list")).get("configured", [])}
    except Exception:  # noqa: BLE001
        return
    x = infos.get(g["datei"]) or {}
    da = bool(x.get("has_pending_changes")) and bool((x.get("runtime_state") or {}).get("deployed_config_hash"))
    await _an_remote(hass, g, "Ein Update ist verfügbar." if da else "Die Firmware ist aktuell.", 0)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_test_wiederholen",
                                  vol.Required("datei"): str})
@websocket_api.async_response
async def ws_test_wiederholen(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    f = next((x for x in await hass.async_add_executor_job(test_dateien) if x["datei"] == msg["datei"]), None)
    e = next((e for e in hass.config_entries.async_entries("esphome") if f and e.data.get("device_name") == f["name"]), None)
    knopf = _test_stand(hass, e)["knopf"] if e else None
    if knopf is None:
        connection.send_error(msg["id"], "test", "Platine ist nicht in Home Assistant eingebunden")
        return
    await hass.services.async_call("button", "press", {"entity_id": knopf}, blocking=True)
    connection.send_result(msg["id"], {"ok": True})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_vormerken",
                                  vol.Required("datei"): str, vol.Required("an"): bool})
@websocket_api.async_response
async def ws_vormerken(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    if msg["datei"] not in {g["datei"] for g in await hass.async_add_executor_job(remote_dateien)}:
        connection.send_error(msg["id"], "datei", "Nur Remotes lassen sich vormerken")
        return
    st, sp = await _speicher(hass)
    s = set(sp["vorgemerkt"])
    (s.add if msg["an"] else s.discard)(msg["datei"])
    sp["vorgemerkt"] = sorted(s)
    await st.async_save(sp)
    # Vormerken (2026-10-26): gleich bauen - das Aufspielen dauert dann nur noch das Hochladen, sobald die Remote
    # benutzt wird (Dock meldet sie wach) oder sich mit HA verbindet
    job = None
    if msg["an"] and not any(not j["fertig"] for j in _jobs.values()):
        g = next(x for x in await hass.async_add_executor_job(remote_dateien) if x["datei"] == msg["datei"])
        job = _starten(hass, [g], "")["job_id"]
    connection.send_result(msg["id"], {"vorgemerkt": sp["vorgemerkt"], "job_id": job})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_job", vol.Required("job_id"): str})
@callback
def ws_job(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    j = _jobs.get(msg["job_id"])
    if j is None:
        connection.send_error(msg["id"], "job", "Unbekannter Auftrag")
        return
    connection.send_result(msg["id"], {k: (list(v)[-80:] if k == "log" else v) for k, v in j.items()})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_sperren",
                                  vol.Required("datei"): str, vol.Required("gesperrt"): bool})
@websocket_api.async_response
async def ws_sperren(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    st, sp = await _speicher(hass)
    s = set(sp["gesperrt"])
    (s.add if msg["gesperrt"] else s.discard)(msg["datei"])
    sp["gesperrt"] = sorted(s)
    await st.async_save(sp)
    connection.send_result(msg["id"], {"gesperrt": sorted(s)})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_entwickler", vol.Required("an"): bool})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_entwickler(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Entwicklermodus an/aus: tools/remotes.py --entwickler schreibt die Einstellung und erzeugt die Remote-Dateien neu."""
    skript = ESPHOME_DIR / "tools" / "remotes.py"
    p = await asyncio.create_subprocess_exec("python3", str(skript), "--entwickler", "an" if msg["an"] else "aus",
                                             stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
    out = (await p.communicate())[0].decode(errors="replace").strip()
    if p.returncode != 0:
        connection.send_error(msg["id"], "entwickler", out or "fehlgeschlagen")
        return
    connection.send_result(msg["id"], {"entwickler": msg["an"], "text": out})


# Paket-Dateien im Repo (2026-10-27 umbenannt; die alten Namen liefert das Repo vorerst weiter mit)
REMOTE_PAKET, REMOTE_PAKET_ALT = "esphomeremote-firmware.yaml", "open-remote-firmware.yaml"
DOCK_PAKET, DOCK_PAKET_ALT = "esphomeremote-dock-firmware.yaml", "open-dock-firmware.yaml"
GITHUB_PAKET = ("packages:\n  {k}:\n    url: https://github.com/Aggro600/ESPHomeRemote\n    ref: stabil\n"
                "    files: [{f}]\n    refresh: 0s\n")


def umstellen(datei: str) -> str:
    """Alte Dock-/Remote-Datei auf die Firmware von GitHub umschreiben (2026-10-26). Die alte bleibt als <datei>.alt."""
    pfad = ESPHOME_DIR / datei
    t = pfad.read_text(encoding="utf-8")
    sub = lambda k, d: (re.search(rf"^\s+{k}:\s*\"?([^\"#\n]+?)\"?\s*(#.*)?$", t, re.M) or [None, d])[1].strip()
    datum = time.strftime("%Y-%m-%d")
    if "dock-basis.yaml" in t or DOCK_PAKET in t or DOCK_PAKET_ALT in t:
        name, fr = sub("name", pfad.stem), sub("friendly_name", pfad.stem)
        neu = (f"# {fr} - umgestellt im Konfigurator ({datum}): Firmware von GitHub ({DOCK_PAKET}, Zweig stabil).\n"
               f"substitutions:\n  name: {name}\n  friendly_name: {fr}\n"
               "  wifi_ssid:         !secret wifi_ssid\n  wifi_password:     !secret wifi_password\n"
               "  ota_pw:            !secret ota_pw\n  direkt_schluessel: !secret direkt_schluessel\n\n"
               + GITHUB_PAKET.format(k="basis", f=DOCK_PAKET))
    elif REMOTE_PAKET in t or REMOTE_PAKET_ALT in t:
        dev = sub("device_name", pfad.stem)
        fr, ha = sub("friendly_name", dev), sub("ha_name", dev.replace("-", "_"))
        neu = (f"# {fr} - umgestellt im Konfigurator ({datum}): Firmware von GitHub (Zweig stabil).\n"
               f"substitutions:\n  device_name:   \"{dev}\"\n  friendly_name: \"{fr}\"\n  ha_name:       \"{ha}\"\n"
               "  wifi_ssid:         !secret wifi_ssid\n  wifi_password:     !secret wifi_password\n  ota_pw:            !secret ota_pw\n"
               "  ap_password:       !secret ap_password\n  direkt_schluessel: !secret direkt_schluessel\n"
               "  konfig_key:        !secret esphomeremote_konfig_key\n\n"
               + GITHUB_PAKET.format(k="firmware", f=REMOTE_PAKET)
               + "\nwifi:\n  ssid: !secret wifi_ssid\n  password: !secret wifi_password\n")
    else:
        raise ValueError("Keine Dock- oder Remote-Datei aus dem Repo")
    (ESPHOME_DIR / (datei + ".alt")).write_text(t, encoding="utf-8")
    pfad.write_text(neu, encoding="utf-8")
    return neu


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/fw_umstellen", vol.Required("datei"): str})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_umstellen(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    bekannt = {g["datei"] for g in await hass.async_add_executor_job(alle_dateien)}
    if msg["datei"] not in bekannt:
        connection.send_error(msg["id"], "datei", "Unbekannte Datei")
        return
    try:
        await hass.async_add_executor_job(umstellen, msg["datei"])
    except (OSError, ValueError) as err:
        connection.send_error(msg["id"], "umstellen", str(err))
        return
    connection.send_result(msg["id"], {"ok": True, "sicherung": msg["datei"] + ".alt"})


class FirmwareView(HomeAssistantView):
    """Fertige Firmware (factory.bin, ab Adresse 0) - fuer USB im Browser (Web Serial) oder zum Herunterladen."""

    url = "/api/esphomeremote_konfig/firmware/{datei}"
    name = "api:esphomeremote_konfig:firmware"
    requires_auth = True

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass

    async def get(self, request, datei: str):
        from aiohttp import web

        if not any(f["datei"] == datei for f in await self.hass.async_add_executor_job(alle_dateien)):
            return web.Response(status=404, text="unbekannte Firmware")
        try:
            r = await dash(self.hass, "firmware/download_token", {"configuration": datei, "file": "firmware.factory.bin"})
            s = async_get_clientsession(self.hass)
            async with s.get(f"http://127.0.0.1:{await _port(self.hass)}/api/firmware/download",
                             params={"token": r["token"]}) as resp:
                if resp.status != 200:
                    return web.Response(status=resp.status, text="Firmware nicht verfuegbar - erst bauen")
                daten = await resp.read()
        except Exception as err:  # noqa: BLE001
            return web.Response(status=502, text=str(err))
        name = datei.removesuffix(".yaml") + ".factory.bin"
        return web.Response(body=daten, content_type="application/octet-stream",
                            headers={"Content-Disposition": f'attachment; filename="{name}"'})


def registrieren(hass: HomeAssistant) -> None:
    for fn in (ws_liste, ws_installieren, ws_job, ws_sperren, ws_vormerken, ws_test_wiederholen, ws_entwickler, ws_umstellen):
        websocket_api.async_register_command(hass, fn)
    hass.http.register_view(FirmwareView(hass))

    async def update(event) -> None:
        await _ereignis_update(hass, event)

    async def verbunden(event) -> None:
        await _ereignis_verbunden(hass, event)

    hass.bus.async_listen("esphome.remote_update", update)
    hass.bus.async_listen("esphome.remote_verbunden", verbunden)

    # Vorgemerkt + Remote wird gerade benutzt (meldet sich ueber das Dock): jetzt aufspielen (2026-10-26). Vorher kam
    # das Update nur mit esphome.remote_verbunden - Remotes, die nur ueber das Dock reden, verbinden sich fast nie.
    from datetime import timedelta
    from homeassistant.helpers.event import async_track_time_interval

    async def wach_pruefen(_now) -> None:
        if any(not j["fertig"] for j in _jobs.values()):
            return
        _, sp = await _speicher(hass)
        if not sp["vorgemerkt"]:
            return
        ds = hass.data.get(DOMAIN + "_dockstatus")
        for g in await hass.async_add_executor_job(remote_dateien):
            if g["datei"] not in sp["vorgemerkt"]:
                continue
            z = (ds.stand.get(g["name"]) or {}).get("zeit", 0) if ds else 0
            if time.time() - z > 45 or time.monotonic() - _letzter_versuch.get(g["name"], -1e9) < 600:
                continue
            _letzter_versuch[g["name"]] = time.monotonic()
            _LOGGER.info("Vorgemerktes Update fuer %s: Remote ist wach", g["name"])
            await remote_update(hass, g["name"], "vorgemerkt")
            return

    async_track_time_interval(hass, wach_pruefen, timedelta(seconds=30))
