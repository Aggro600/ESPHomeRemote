"""Komplett-Sicherung (2026-11-12): alles in einer Datei, auch das, was die normale Sicherung nicht hat.

Die normale Sicherung (ws_sicherung) enthaelt Belegungen, Einstellungen und gemeinsame Bereiche - nicht aber:
  - die ESPHome-Geraetedateien (remote-*.yaml, dock-*.yaml), die der Konfigurator angelegt hat
  - die ESPHome-secrets.yaml (WLAN, Schluessel) - nur auf Wunsch, die Datei enthaelt Passwoerter
  - die Liste der im Konfigurator angelegten Remotes ("extra"), ohne die sie nach einem Umzug fehlen
Hier kommt beides zusammen: sicherung_komplett liefert die Zusatzteile, einspielen_dateien schreibt sie zurueck - nur
Dateien, die noch nicht da sind (nie ueberschreiben), fehlende Secrets-Zeilen anhaengen, fehlende Remotes anlegen.
Der Rest laeuft ueber die bekannten Wege (einspielen, sicherung_global) im Konfigurator.
"""
from __future__ import annotations

import logging
import re

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant

from . import firmware as _fw

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
REPO = "github.com/Aggro600/ESPHomeRemote"
_DATEI = re.compile(r"^(remote|dock)-[a-z0-9-]+(-halink)?\.yaml$")
_SECRET_ZEILE = re.compile(r"^([A-Za-z0-9_]+)\s*:\s*(.*?)\s*$")


def _geraetedateien() -> dict[str, str]:
    """Geraetedateien, die die Firmware von GitHub holen (also vom Konfigurator angelegt oder umgestellt sind)."""
    out = {}
    for f in sorted(_fw.ESPHOME_DIR.glob("*.yaml")):
        if not _DATEI.match(f.name):
            continue
        try:
            t = f.read_text(encoding="utf-8")
        except OSError:
            continue
        if REPO in t:
            out[f.name] = t
    return out


def _secrets_lesen() -> str:
    try:
        return (_fw.ESPHOME_DIR / "secrets.yaml").read_text(encoding="utf-8")
    except OSError:
        return ""


def _dateien_schreiben(dateien: dict[str, str]) -> tuple[list[str], list[str]]:
    """Nur anlegen, was fehlt. Gibt (angelegt, uebersprungen) zurueck."""
    neu, da = [], []
    _fw.ESPHOME_DIR.mkdir(parents=True, exist_ok=True)
    for name, inhalt in dateien.items():
        if not _DATEI.match(name) or not isinstance(inhalt, str) or REPO not in inhalt:
            continue
        ziel = _fw.ESPHOME_DIR / name
        if ziel.exists():
            da.append(name)
            continue
        ziel.write_text(inhalt, encoding="utf-8")
        neu.append(name)
    return neu, da


def _secrets_ergaenzen(text: str) -> list[str]:
    """Fehlende Zeilen aus der gesicherten secrets.yaml anhaengen - vorhandene Werte bleiben, wie sie sind."""
    pfad = _fw.ESPHOME_DIR / "secrets.yaml"
    alt = _secrets_lesen()
    vorhanden = {m.group(1) for z in alt.splitlines() if (m := _SECRET_ZEILE.match(z))}
    neu = [z for z in text.splitlines() if (m := _SECRET_ZEILE.match(z)) and m.group(1) not in vorhanden and not z.lstrip().startswith("#")]
    if not neu:
        return []
    pfad.parent.mkdir(parents=True, exist_ok=True)
    with pfad.open("a", encoding="utf-8") as f:
        f.write(("" if not alt else "\n" if alt.endswith("\n") else "\n\n") + "# ESPHomeRemote (aus Komplett-Sicherung eingespielt)\n"
                + "\n".join(neu) + "\n")
    return [z.split(":", 1)[0] for z in neu]


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/sicherung_komplett",
                                  vol.Optional("secrets", default=False): bool})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_sicherung_komplett(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    kd = hass.data[DOMAIN]
    dateien = await hass.async_add_executor_job(_geraetedateien)
    out = {"dateien": dateien, "extra": list(kd.data.get("extra") or []), "kanal": _fw.zweig(),
           "esphome_ordner": str(_fw.ESPHOME_DIR)}
    if msg["secrets"]:
        out["secrets"] = await hass.async_add_executor_job(_secrets_lesen)
    connection.send_result(msg["id"], out)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/einspielen_dateien",
                                  vol.Optional("dateien", default={}): {str: str},
                                  vol.Optional("secrets", default=""): str,
                                  vol.Optional("extra", default=[]): [str]})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_einspielen_dateien(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    kd = hass.data[DOMAIN]
    neu, da = await hass.async_add_executor_job(_dateien_schreiben, msg["dateien"])
    secrets = await hass.async_add_executor_job(_secrets_ergaenzen, msg["secrets"]) if msg["secrets"] else []
    remotes = []
    for dev in msg["extra"]:
        if re.fullmatch(r"remote-[a-z0-9-]+", dev) and dev not in kd.devices:
            await kd.geraet_dazu(dev)
            remotes.append(dev)
    _LOGGER.info("Komplett-Sicherung eingespielt: %d Dateien neu (%d schon da), %d Secrets-Zeilen, %d Remotes angelegt",
                 len(neu), len(da), len(secrets), len(remotes))
    connection.send_result(msg["id"], {"dateien_neu": neu, "dateien_da": da, "secrets": secrets, "remotes": remotes})


def registrieren(hass: HomeAssistant) -> None:
    for fn in (ws_sicherung_komplett, ws_einspielen_dateien):
        websocket_api.async_register_command(hass, fn)
