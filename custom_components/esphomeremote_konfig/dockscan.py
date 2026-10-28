"""Suchtempo der Docks (Scan-Stufen des Bluetooth-Proxys, 2026-10-22).

Das Dock sucht als Proxy nach den Remotes (HA-Link ueber Bluetooth). Schnelles Suchen kostet Funkzeit, die auf dem
ESP32-C3 WLAN, ESP-NOW und die Bluetooth-Geraete am Dock (dock_hid) brauchen - der Controller-Watchdog schlug genau in
der Scan-Planung zu. Darum je nach Lage (Konfigurator Dockingstation > Suchtempo, kd.data["dockscan"]):
  - alle mitgezaehlten Remotes verbunden        -> langsam (es kommt keiner mehr)
  - eine fehlt seit hoechstens fehlt_min Minuten -> schnell (sie kommt wohl gleich wieder)
  - fehlt sie laenger                           -> normal
  - Weckruf: fragt eine Remote nach dem Aufwachen ihren Stand ab (dock_direkt typ 4), sucht das Dock weck_s Sekunden
    schnell - das entscheidet das Dock selbst, ohne HA (dock_hid::weckruf)
Modus "schnell"/"normal"/"langsam" = immer diese Stufe ("schnell" mit 150/50 ms = Verhalten vor 2026-10-22).
"Verbunden" = Bluetooth-Link der Bruecke (ble_link.py) steht.
Gesendet wird jede Minute und bei jeder Aenderung (ESPHome-Aktion esphome.<dock>_scan_stufe); bleibt HA 3 min still,
nimmt das Dock wieder seinen Standard aus der YAML.
"""

from __future__ import annotations

import asyncio
import copy
import logging
import time
from datetime import timedelta
from typing import Any

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event, async_track_time_interval

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"

MODI = ("adaptiv", "schnell", "normal", "langsam")
# 2026-10-25 (Funkkonzept "Dock, WLAN als Ausweg"): die Remotes werben nicht mehr per Bluetooth fuer HA - schnelles
# Suchen nach ihnen bringt nichts mehr und nimmt ESP-NOW/WLAN/Geraeten am Dock Funkzeit. Standard jetzt "normal"
# (wie ein ESPHome-Proxy ab Werk, fuer die anderen Bluetooth-Geraete in HA) und kein Weckruf.
STANDARD = {
    "modus": "normal",
    "schnell": {"i": 150, "f": 50},     # ein Drittel der Zeit (bisheriger fester Wert)
    "normal": {"i": 320, "f": 30},      # wie ein ESPHome-Proxy ab Werk (~9 %)
    "langsam": {"i": 1000, "f": 30},    # 3 %
    "fehlt_min": 10,                    # so lange schnell suchen, nachdem eine Remote weg ist
    "weck_s": 0,                        # schnell nach einem Weckruf (0 = aus; bis 2026-10-25: 30)
    "fe_verbunden": 30,                 # hoechstens so lange am Stueck hinhoeren, solange ein Geraet am Dock haengt
    "ohne": {},                         # {dock: [remote, ...]} - zaehlen fuer dieses Dock nicht
}


def validate_dockscan(d: Any) -> dict:
    if not isinstance(d, dict):
        raise vol.Invalid("Suchtempo: Objekt erwartet")
    out = copy.deepcopy(STANDARD)
    if d.get("modus") in MODI:
        out["modus"] = d["modus"]
    for k in ("schnell", "normal", "langsam"):
        s = d.get(k) or {}
        try:
            i = int(s.get("i", out[k]["i"]))
            f = int(s.get("f", out[k]["f"]))
        except (TypeError, ValueError) as err:
            raise vol.Invalid(f"Suchtempo {k}: Zahl erwartet") from err
        if not 20 <= i <= 10000:
            raise vol.Invalid(f"Suchtempo {k}: Abstand 20 bis 10000 ms")
        if not 10 <= f <= i:
            raise vol.Invalid(f"Suchtempo {k}: Hinhören 10 ms bis höchstens Abstand")
        out[k] = {"i": i, "f": f}
    try:
        out["fehlt_min"] = max(0, min(240, int(d.get("fehlt_min", out["fehlt_min"]))))
        out["weck_s"] = max(0, min(600, int(d.get("weck_s", out["weck_s"]))))
        out["fe_verbunden"] = max(10, min(50, int(d.get("fe_verbunden", out["fe_verbunden"]))))
    except (TypeError, ValueError) as err:
        raise vol.Invalid("Suchtempo: Zeiten als Zahl") from err
    ohne = {}
    for dock, rs in (d.get("ohne") or {}).items():
        if isinstance(rs, list) and rs:
            ohne[str(dock)[:64]] = [str(r)[:64] for r in rs][:16]
    out["ohne"] = ohne
    return out


class DockScan:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self.weg_seit: dict[str, float] = {}     # Remote -> seit wann ohne Link (Start von HA zaehlt als "gerade weg")
        self.gesendet: dict[str, tuple] = {}
        self.stand: dict[str, dict] = {}         # Dock -> {stufe, grund, remotes: [{dev, name, da, weg_s}]}
        self._abo = None

    @property
    def cfg(self) -> dict:
        if "dockscan" not in self.kd.data:
            return copy.deepcopy(STANDARD)
        return self.kd.data["dockscan"]

    async def speichern(self, d: dict) -> None:
        self.kd.data["dockscan"] = validate_dockscan(d)
        await self.kd.store.async_save(self.kd.data)
        await self.pruefen(immer=True)

    def _links(self) -> dict[str, bool]:
        """Remote (remote-wz-<slug>) -> Bluetooth-Link verbunden."""
        b = self.hass.data.get(DOMAIN + "_bruecke")
        links = b.mod.async_links(self.hass) if b is not None and b.mod else {}
        out = {}
        for r in (b.stand if b is not None else []):
            link = links.get(f"Remote {r['name']}")
            client = getattr(link, "client", None) if link is not None else None
            out[f"remote-wz-{r['slug']}"] = bool(client is not None and client.is_connected)
        return out

    def _namen(self) -> dict[str, str]:
        b = self.hass.data.get(DOMAIN + "_bruecke")
        return {f"remote-wz-{r['slug']}": r["name"] for r in (b.stand if b is not None else [])}

    def berechnen(self, dock: str, links: dict[str, bool], jetzt: float) -> dict:
        c = self.cfg
        namen = self._namen()
        ohne = set(c["ohne"].get(dock, []))
        zaehlen = [d for d in links if d not in ohne]
        rem = [{"dev": d, "name": namen.get(d, d), "da": links[d], "zaehlt": d not in ohne,
                "weg_s": 0 if links[d] else int(jetzt - self.weg_seit.get(d, jetzt))} for d in links]
        if c["modus"] != "adaptiv":
            return {"stufe": c["modus"], "grund": "fest eingestellt", "remotes": rem, "zaehlen": zaehlen}
        if not zaehlen:
            return {"stufe": "normal", "grund": "keine Remote zählt", "remotes": rem, "zaehlen": zaehlen}
        fehlen = [d for d in zaehlen if not links[d]]
        if not fehlen:
            return {"stufe": "langsam", "grund": f"alle {len(zaehlen)} Remotes da", "remotes": rem, "zaehlen": zaehlen}
        frisch = [d for d in fehlen if jetzt - self.weg_seit.get(d, jetzt) < c["fehlt_min"] * 60]
        if frisch:
            d = min(frisch, key=lambda x: jetzt - self.weg_seit.get(x, jetzt))
            return {"stufe": "schnell", "grund": f"{namen.get(d, d)} fehlt", "remotes": rem, "zaehlen": zaehlen,
                    "bis": self.weg_seit.get(d, jetzt) + c["fehlt_min"] * 60}
        d = fehlen[0]
        return {"stufe": "normal", "grund": f"{namen.get(d, d)} fehlt länger", "remotes": rem, "zaehlen": zaehlen}

    async def pruefen(self, _now=None, immer: bool = False) -> None:
        jetzt = time.time()
        links = self._links()
        if not links:   # Bruecke kurz nach dem HA-Start noch nicht abgeglichen - nichts Falsches schicken
            return
        for d, da in links.items():
            if da:
                self.weg_seit.pop(d, None)
            else:
                self.weg_seit.setdefault(d, jetzt)
        c = self.cfg
        for dock in self.kd.dock_macs():
            s = self.berechnen(dock, links, jetzt)
            self.stand[dock] = s
            st = c[s["stufe"]]
            daten = {"intervall": st["i"], "fenster": st["f"], "grund": f"{s['stufe']}: {s['grund']}"[:60],
                     "weck_s": c["weck_s"], "weck_intervall": c["schnell"]["i"], "weck_fenster": c["schnell"]["f"],
                     "remotes": ",".join(s["zaehlen"]), "fenster_verbunden": c.get("fe_verbunden", 30)}
            sig = tuple(sorted(daten.items()))
            # jede Minute wiederholen (sonst faellt das Dock nach 3 min auf seinen Standard), sonst nur bei Aenderung
            if not immer and _now is None and self.gesendet.get(dock) == sig:
                continue
            if not self.hass.services.has_service("esphome", f"{dock}_scan_stufe"):
                continue
            if self.gesendet.get(dock, (None,)) != sig:
                _LOGGER.info("Suchtempo %s: %s (%s von %s ms)", dock, daten["grund"], st["f"], st["i"])
            self.gesendet[dock] = sig
            try:
                await self.hass.services.async_call("esphome", f"{dock}_scan_stufe", daten, blocking=False)
            except Exception as err:  # noqa: BLE001 - Dock offline
                _LOGGER.debug("%s_scan_stufe: %s", dock, err)

    @callback
    def starten(self) -> None:
        # Links haben keine Ereignisse: alle 10 s nachsehen (sendet nur bei Aenderung), jede Minute sicher senden
        async def kurz(_now) -> None:
            await self.pruefen(immer=False)

        async_track_time_interval(self.hass, kurz, timedelta(seconds=10))
        async_track_time_interval(self.hass, self.pruefen, timedelta(minutes=1))
        docks = [f"binary_sensor.{d}_status" for d in self.kd.dock_macs()]
        if docks:
            @callback
            def dock_da(ev: Event) -> None:
                neu = ev.data.get("new_state")
                if neu is not None and neu.state == "on":
                    async def spaeter() -> None:
                        await asyncio.sleep(3)   # API-Aktionen erst kurz nach dem Verbinden da
                        await self.pruefen(immer=True)
                    self.hass.async_create_task(spaeter())
            async_track_state_change_event(self.hass, docks, dock_da)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/dockscan_get"})
@callback
def ws_dockscan_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    ds: DockScan = hass.data[DOMAIN + "_dockscan"]
    connection.send_result(msg["id"], {"cfg": ds.cfg, "standard": STANDARD, "stand": ds.stand})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/dockscan_save", vol.Required("cfg"): dict})
@websocket_api.async_response
async def ws_dockscan_save(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    ds: DockScan = hass.data[DOMAIN + "_dockscan"]
    try:
        await ds.speichern(msg["cfg"])
    except vol.Invalid as err:
        connection.send_error(msg["id"], "invalid", str(err))
        return
    connection.send_result(msg["id"], {"cfg": ds.cfg, "stand": ds.stand})


def einrichten(hass: HomeAssistant, kd) -> DockScan:
    ds = DockScan(hass, kd)
    hass.data[DOMAIN + "_dockscan"] = ds
    ds.starten()
    websocket_api.async_register_command(hass, ws_dockscan_get)
    websocket_api.async_register_command(hass, ws_dockscan_save)
    return ds
