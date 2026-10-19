"""Docks einrichten (2026-10-16): neue Dock-Hardware aus dem Konfigurator anlegen, flashen und in HA einbinden.

Jedes Dock ist eine kleine ESPHome-Datei /config/esphome/dock-<name>.yaml (Name + Anzeigename), die die gemeinsame
Firmware templates/dock-basis.yaml einbindet. Bauen/Flashen: firmware.py (Tab "Firmware", auch im Dock-Tab je Dock).

Ablauf im Panel (Firmware & Geraete -> Docks):
  1. "Neues Dock" -> Name -> Datei anlegen
  2. Installieren: per WLAN (OTA, Dock hat schon ESPHome), per USB am HA-Rechner (serieller Port des Add-ons)
     oder Firmware herunterladen und im Browser mit ESPHome Web flashen (erstes Flashen, Dock am eigenen PC)
  3. In HA einbinden (ESPHome-Integration mit dem API-Schluessel) - danach erscheint der IR-Sender von selbst
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import time

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant

from .firmware import DOCK_BASIS as BASIS, ESPHOME_DIR, Dash, dock_dateien, test_dateien

DOMAIN = "esphomeremote_konfig"
_NAME_RE = re.compile(r"^dock-[a-z0-9]+(?:-[a-z0-9]+)*$")


def _slug(friendly: str) -> str:
    s = friendly.strip().lower()
    for a, b in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("ß", "ss")):
        s = s.replace(a, b)
    s = re.sub(r"[^a-z0-9]+", "-", s).strip("-")
    if not s.startswith("dock"):
        s = "dock-" + s
    s = re.sub(r"^dock(?!-)", "dock-", s)
    return s[:31].rstrip("-")


def _anlegen(friendly: str) -> dict:
    friendly = " ".join(friendly.split())
    if not friendly or len(friendly) > 22:
        raise ValueError("Name: 1 bis 22 Zeichen (er wird auch Name des Notfall-WLANs)")
    if not re.fullmatch(r"[\w äöüÄÖÜß.\-]+", friendly):
        raise ValueError("Name: nur Buchstaben, Ziffern, Leerzeichen, Punkt und Bindestrich")
    name = _slug(friendly)
    if not _NAME_RE.match(name) or name == "dock":
        raise ValueError("Aus dem Namen laesst sich kein Geraetename bilden")
    pfad = os.path.join(ESPHOME_DIR, name + ".yaml")
    if os.path.exists(pfad):
        raise ValueError(f"{name}.yaml gibt es schon")
    if not os.path.exists(os.path.join(ESPHOME_DIR, BASIS)):
        raise ValueError(f"{BASIS} fehlt")
    text = (f"# {friendly} - angelegt im Konfigurator ({time.strftime('%Y-%m-%d')}). "
            f"Firmware: {BASIS} (gemeinsam fuer alle Docks).\n"
            f"substitutions:\n  name: {name}\n  friendly_name: {friendly}\n\n"
            f"packages:\n  basis: !include {BASIS}\n")
    with open(pfad, "w", encoding="utf-8") as f:
        f.write(text)
    return {"datei": name + ".yaml", "name": name, "friendly": friendly}


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/docks_anlegen", vol.Required("name"): str})
@websocket_api.async_response
async def ws_anlegen(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    try:
        r = await hass.async_add_executor_job(_anlegen, msg["name"])
    except (ValueError, OSError) as err:
        connection.send_error(msg["id"], "anlegen", str(err))
        return
    connection.send_result(msg["id"], r)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/docks_einbinden", vol.Required("datei"): str})
@websocket_api.async_response
async def ws_einbinden(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Dock in die ESPHome-Integration aufnehmen (Adresse aus dem Device Builder, Schluessel aus der Konfiguration)."""
    datei = msg["datei"]
    # Docks und Platinen-Test (mikrofon-kontakttest*.yaml, 2026-10-17)
    alle = await hass.async_add_executor_job(dock_dateien) + await hass.async_add_executor_job(test_dateien)
    f = next((x for x in alle if x["datei"] == datei), None)
    if f is None:
        connection.send_error(msg["id"], "datei", f"{datei} ist kein Dock und kein Platinen-Test")
        return
    if any(e.data.get("device_name") == f["name"] for e in hass.config_entries.async_entries("esphome")):
        connection.send_result(msg["id"], {"ok": True, "text": "Ist schon in Home Assistant."})
        return
    try:
        async with Dash(hass) as d:
            g = next((x for x in (await d.befehl("devices/list")).get("configured", []) if x.get("configuration") == datei), {})
            schluessel = (await d.befehl("devices/get_encryption_key", {"configuration": datei}) or {}).get("key")
    except Exception as err:  # noqa: BLE001
        connection.send_error(msg["id"], "einbinden", str(err))
        return
    rt = g.get("runtime_state") or {}
    host = g.get("ip") or (rt.get("ip_addresses") or [None])[0] or f"{f['name']}.local"
    fm = hass.config_entries.flow
    try:
        r = await fm.async_init("esphome", context={"source": "user"})
        r = await fm.async_configure(r["flow_id"], {"host": host, "port": 6053})
        if r.get("type") == "form" and r.get("step_id") == "encryption_key":
            r = await fm.async_configure(r["flow_id"], {"noise_psk": schluessel or ""})
    except Exception as err:  # noqa: BLE001
        connection.send_error(msg["id"], "einbinden", f"{host}: {err}")
        return
    typ = str(r.get("type"))
    if typ.endswith("create_entry"):
        connection.send_result(msg["id"], {"ok": True, "text": f"Eingebunden ({host})."})
    elif typ.endswith("abort"):
        grund = r.get("reason")
        connection.send_result(msg["id"], {"ok": grund == "already_configured",
                                           "text": "Ist schon in Home Assistant." if grund == "already_configured"
                                           else f"Abgebrochen: {grund}"})
    else:
        if r.get("flow_id"):
            fm.async_abort(r["flow_id"])
        fehler = ", ".join(f"{k}: {v}" for k, v in (r.get("errors") or {}).items())
        connection.send_result(msg["id"], {"ok": False, "text": f"{host} nicht erreichbar ({fehler or r.get('step_id')}). "
                                           + ("Ist das Dock geflasht und im WLAN?" if f.get("art") == "dock" else "Ist die Platine geflasht und im WLAN?")})


def _wegraeumen(datei: str) -> str:
    """Dock-Datei nicht loeschen, sondern nach esphome/archive/geloescht/ verschieben (wiederherstellbar)."""
    ziel = Path(ESPHOME_DIR) / "archive" / "geloescht"
    ziel.mkdir(parents=True, exist_ok=True)
    neu = ziel / f"{Path(datei).stem}-{time.strftime('%Y%m%d-%H%M%S')}.yaml"
    shutil.move(os.path.join(ESPHOME_DIR, datei), neu)
    return str(neu.relative_to(ESPHOME_DIR))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/docks_entfernen", vol.Required("datei"): str})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_entfernen(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Dock entfernen (2026-10-18): ESPHome-Eintrag in HA, Build-Ordner im Add-on, Datei ins Archiv."""
    from .einrichten import bau_aufraeumen

    datei = msg["datei"]
    f = next((x for x in await hass.async_add_executor_job(dock_dateien) if x["datei"] == datei), None)
    if f is None:
        connection.send_error(msg["id"], "datei", f"{datei} ist kein Dock")
        return
    weg = []
    for e in hass.config_entries.async_entries("esphome"):
        if e.data.get("device_name") == f["name"] or str(e.data.get("host", "")) in (f["name"], f"{f['name']}.local"):
            await hass.config_entries.async_remove(e.entry_id)
            weg.append("HA-Eintrag")
    await bau_aufraeumen(hass, [datei])
    try:
        archiv = await hass.async_add_executor_job(_wegraeumen, datei)
    except OSError as err:
        connection.send_error(msg["id"], "datei", str(err))
        return
    weg.append(f"Datei (liegt jetzt in esphome/{archiv})")
    connection.send_result(msg["id"], {"entfernt": weg})


def registrieren(hass: HomeAssistant) -> None:
    for fn in (ws_anlegen, ws_einbinden, ws_entfernen):
        websocket_api.async_register_command(hass, fn)
