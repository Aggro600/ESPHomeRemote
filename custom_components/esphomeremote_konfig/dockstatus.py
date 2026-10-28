"""Werte der Remote ueber das Dock (2026-10-23, Umbau "Dock als Zentrale").

Hat die Remote keine eigene Verbindung zu HA (WLAN aus, kein Bluetooth-Link), funkt sie ihre wichtigsten Werte per
ESP-NOW ans Dock; das Dock ruft damit diese Aktion auf (dock_direkt typ 3 -> call_homeassistant_service):

    esphomeremote_konfig.remote_status  {geraet: "remote-wz-wohnzimmer", a: Akku %, v: Spannung, l: Ladekabel 0/1,
                                         b: Bluetooth 0/1, k: Aktivitaet, h: Tasten-Konfiguration, t: Tiefschlaf-Status,
                                         n: letzter Neustart, d: Dock (Name)}

Die Werte landen in den normalen ESPHome-Entitaeten der Remote (Attribut quelle: dock) - Dashboards, Automationen
und der Konfigurator sehen sie wie gewohnt. Geschrieben wird nur, solange die Entitaet "unavailable"/"unknown" ist
oder schon vom Dock kam: steht die eigene Verbindung, gelten deren Werte. Der letzte Stand je Remote liegt ausserdem in
DockStatus.stand (Konfigurator: ws dockstatus_get).
"""

from __future__ import annotations

import logging
import time
from collections import deque

import voluptuous as vol

from homeassistant.core import HomeAssistant, ServiceCall, callback
from homeassistant.components import websocket_api

_LOGGER = logging.getLogger(__name__)
# Protokoll der Remotes ueber das Dock (2026-10-24): eigener Logger, damit es sich in configuration.yaml (logger:)
# getrennt ein- und ausschalten laesst
_LOG_REMOTE = logging.getLogger(__package__ + ".remote")
LOG_ZEILEN = 500
DOMAIN = "esphomeremote_konfig"

# Schluessel -> (Domaene, Entitaets-Endung, Umwandlung)
def _zahl(x: str):
    return round(float(x), 2)


def _an(x: str) -> str:
    return "on" if str(x).strip() in ("1", "true", "on") else "off"


WERTE = {
    "a": ("sensor", "akkustand", _zahl),
    "v": ("sensor", "akkuspannung", _zahl),
    "l": ("binary_sensor", "ladekabel", _an),
    "b": ("binary_sensor", "bluetooth_verbunden", _an),
    "k": ("sensor", "aktivitat", _zahl),
    "h": ("sensor", "tasten_konfiguration", str),
    "t": ("sensor", "tiefschlaf_status", str),
    "n": ("sensor", "letzter_neustart", str),
}
FREI = ("unavailable", "unknown")


class DockStatus:
    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self.stand: dict[str, dict] = {}   # geraet -> {werte, zeit, dock}
        self.log: dict[str, deque] = {}    # geraet -> [(zeit, stufe, text)]

    @callback
    def log_melden(self, daten: dict) -> int:
        """Protokoll-Zeilen der Remote ueber das Dock (esphomeremote_konfig.remote_log {geraet, z, d}): je Zeile
        "<Alter in 0,1 s> <E|W|I> <tag>: <text>" (esphomeremote-logpuffer.h fuer_dock). Landet im HA-Protokoll unter
        custom_components.esphomeremote_konfig.remote und in DockStatus.log (ws remote_log_get)."""
        geraet = str(daten.get("geraet") or "").strip().lower()
        if not geraet.startswith("remote-"):
            return 0
        jetzt = time.time()
        buch = self.log.setdefault(geraet, deque(maxlen=LOG_ZEILEN))
        n = 0
        for zeile in str(daten.get("z") or "").splitlines():
            alter, _, text = zeile.partition(" ")
            if not text:
                continue
            try:
                zeit = jetzt - int(alter) / 10
            except ValueError:
                zeit = jetzt
            stufe = text[:1] if text[:1] in "EWI" else "I"
            buch.append((round(zeit, 1), stufe, text[2:] if text[1:2] == " " else text))
            lv = logging.ERROR if stufe == "E" else logging.WARNING if stufe == "W" else logging.INFO
            _LOG_REMOTE.log(lv, "%s (vor %.0f s, ueber %s): %s", geraet, jetzt - zeit, daten.get("d") or "Dock", buch[-1][2])
            n += 1
        return n

    @callback
    def melden(self, daten: dict) -> int:
        geraet = str(daten.get("geraet") or "").strip().lower()
        if not geraet.startswith("remote-"):
            return 0
        slug = geraet.replace("-", "_")
        werte = {k: str(v) for k, v in daten.items() if k in WERTE}
        self.stand[geraet] = {"werte": werte, "zeit": time.time(), "dock": str(daten.get("d") or "")}
        n = 0
        for k, roh in werte.items():
            dom, endung, umw = WERTE[k]
            eid = f"{dom}.{slug}_{endung}"
            st = self.hass.states.get(eid)
            if st is None or (st.state not in FREI and st.attributes.get("quelle") != "dock"):
                continue   # eigene Verbindung steht - deren Wert gilt
            try:
                wert = umw(roh)
            except (TypeError, ValueError):
                continue
            attr = {**st.attributes, "quelle": "dock"}
            if daten.get("d"):
                attr["dock"] = str(daten["d"])
            self.hass.states.async_set(eid, wert, attr)
            n += 1
        _LOGGER.debug("Status %s ueber Dock %s: %d Werte gesetzt", geraet, daten.get("d"), n)
        return n


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/remote_log_get",
                                  vol.Optional("device"): str})
@callback
def ws_remote_log_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    ds: DockStatus = hass.data[DOMAIN + "_dockstatus"]
    log = {d: list(z) for d, z in ds.log.items() if not msg.get("device") or d == msg["device"]}
    connection.send_result(msg["id"], {"log": log})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/dockstatus_get"})
@callback
def ws_dockstatus_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    ds: DockStatus = hass.data[DOMAIN + "_dockstatus"]
    connection.send_result(msg["id"], {"stand": ds.stand})


def einrichten(hass: HomeAssistant) -> DockStatus:
    ds = DockStatus(hass)
    hass.data[DOMAIN + "_dockstatus"] = ds

    async def remote_status(call: ServiceCall) -> None:
        ds.melden(dict(call.data))

    async def einst_melden(call: ServiceCall) -> None:
        """Einstellungen ueber das Dock (2026-10-23): volle Werte (wie Ereignis esphome.remote_einstellungen) oder nur
        der Konfigurations-Stand - passt er, gelten die mit der Konfiguration geschickten Einstellungen als uebernommen."""
        import json
        from . import fnv1a
        kd = hass.data.get(DOMAIN)
        dev = str(call.data.get("geraet") or "")
        if kd is None or dev not in kd.devices:
            return
        h = str(call.data.get("hash") or "")
        try:
            werte = json.loads(call.data.get("werte") or "null")
        except ValueError:
            werte = None
        if isinstance(werte, dict) and werte:
            # Nur geaenderte Werte (2026-10-25, "teil"): in den zuletzt bekannten Stand einsortieren
            if str(call.data.get("teil") or "") == "1":
                werte = {**(kd.entry(dev).get("einst_ist") or {}), **werte}
            kd.einst_gemeldet(dev, werte, h)
            return
        e = kd.entry(dev)
        if h and h == fnv1a(kd.payload(dev)):
            e["einst_ist_rev"] = max([r for _, r in (e.get("einst") or {}).values()] + [0])
            kd.store.async_delay_save(lambda: kd.data, 5)
            hass.bus.async_fire(DOMAIN + "_einst", {"device": dev})
            _LOGGER.debug("Einstellungen von %s ueber Dock: Stand %s uebernommen", dev, h)

    hass.services.async_register(DOMAIN, "einst_melden", einst_melden,
                                 schema=vol.Schema({vol.Required("geraet"): str}, extra=vol.ALLOW_EXTRA))
    async def remote_log(call: ServiceCall) -> None:
        ds.log_melden(dict(call.data))

    hass.services.async_register(DOMAIN, "remote_log", remote_log,
                                 schema=vol.Schema({vol.Required("geraet"): str}, extra=vol.ALLOW_EXTRA))
    async def wlan_kommt(call: ServiceCall) -> None:
        """Remote schaltet gleich ihr WLAN ein (Sprache an HA, kein Dock ...): die Bruecke verbindet HA, sobald sie da ist."""
        dev = str(call.data["geraet"])
        b = hass.data.get(DOMAIN + "_bruecke")
        links = b.mod.async_links(hass) if b is not None and b.mod else {}
        for r in (b.stand if b is not None else []):
            if f"remote-wz-{r['slug']}" == dev and (link := links.get(f"Remote {r['name']}")) is not None:
                hass.async_create_task(link.wlan_kommt())
                return
        _LOGGER.debug("wlan_kommt: keine Bruecke fuer %s", dev)

    hass.services.async_register(DOMAIN, "wlan_kommt", wlan_kommt,
                                 schema=vol.Schema({vol.Required("geraet"): str}, extra=vol.ALLOW_EXTRA))
    hass.services.async_register(DOMAIN, "remote_status", remote_status,
                                 schema=vol.Schema({vol.Required("geraet"): str}, extra=vol.ALLOW_EXTRA))
    websocket_api.async_register_command(hass, ws_dockstatus_get)
    websocket_api.async_register_command(hass, ws_remote_log_get)
    return ds
