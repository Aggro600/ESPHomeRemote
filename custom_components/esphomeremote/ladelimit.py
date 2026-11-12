"""Ladelimit (2026-11-12, experimentell): Remote im Dock nur bis x % laden.

Ein Lithium-Akku, der dauerhaft bei 100 % am Lader haengt, altert schneller. Steht die Remote die meiste Zeit im Dock,
reichen 80 %. Die Remote selbst kann das Laden nicht stoppen (Lade-IC fest verdrahtet) - aber Docks mit geschalteten
Ladekontakten (Nachruestung GPIO2, switch.<dock>_ladekontakte) koennen die Pogo-Pins abschalten.

Ablauf (alle 60 s und bei jedem Wert ueber das Dock):
  - Remote meldet ueber Dock D: Akku a %, Ladekabel an  -> a >= Limit: Ladekontakte von D aus (gemerkt: wegen dieser Remote)
  - gemerkt aus und (a <= Limit - 5  ODER  seit 2 h aus  ODER  Remote meldet sich nicht mehr ueber D) -> wieder an,
    danach mindestens 10 min laden, bevor erneut gesperrt wird (sonst ginge es im naechsten Durchlauf gleich wieder aus)
  - HA-Start: alle gemerkten Sperren weg - Kontakte, die aus sind, gehen an (sonst bliebe ein Dock nach einem Neustart tot)
Grenzen: Ohne Strom an den Kontakten sieht die Remote "Kabel weg" - das Dock-Display kann dann ausgehen, bis die
Kontakte wieder an sind. Darum standardmaessig aus und im Konfigurator als experimentell gekennzeichnet.
Einstellung je Remote: kd.data["ladelimit"][remote] = Prozent (0 = aus). WS ladelimit_get / ladelimit_set.
"""
from __future__ import annotations

import logging
import time
from datetime import timedelta

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.event import async_track_time_interval

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
HYSTERESE = 5
MAX_AUS_S = 2 * 3600
NACHLADEN_S = 10 * 60   # nach dem Wiedereinschalten mindestens so lange laden, bevor erneut gesperrt wird
STILL_S = 15 * 60
TAKT = timedelta(seconds=60)
FREI = ("unavailable", "unknown", None, "")


class Ladelimit:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.aus: dict[str, dict] = {}   # dock-slug -> {"remote": ..., "seit": ts}
        self.frei_bis: dict[str, float] = {}   # dock-slug -> Zeitpunkt, ab dem wieder gesperrt werden darf

    @property
    def limits(self) -> dict[str, int]:
        return {k: int(v) for k, v in (self.kd.data.get("ladelimit") or {}).items() if int(v) > 0}

    def _schalter(self, dock: str) -> str | None:
        eid = f"switch.{dock.replace('-', '_')}_ladekontakte"
        return eid if self.hass.states.get(eid) is not None else None

    def _stand(self, dev: str) -> tuple[float | None, bool, str, float]:
        """(Akku %, Kabel an, Dock-Slug, Alter der Meldung in s) aus den Werten ueber das Dock."""
        ds = self.hass.data.get(DOMAIN + "_dockstatus")
        st = (ds.stand.get(dev) if ds else None) or {}
        w = st.get("werte") or {}
        try:
            akku = float(w.get("a")) if w.get("a") not in FREI else None
        except (TypeError, ValueError):
            akku = None
        kabel = str(w.get("l")) in ("1", "true", "True", "on")
        return akku, kabel, str(st.get("dock") or ""), time.time() - float(st.get("zeit") or 0)

    async def _schalten(self, eid: str, an: bool) -> None:
        await self.hass.services.async_call("switch", "turn_on" if an else "turn_off", {"entity_id": eid}, blocking=True)

    async def pruefen(self, *_) -> None:
        limits = self.limits
        # 1. Sperren aufheben, wo es Zeit ist
        for dock, info in list(self.aus.items()):
            dev = info["remote"]
            akku, kabel, bei, alter = self._stand(dev)
            limit = limits.get(dev, 0)
            grund = None
            if not limit:
                grund = "Limit aus"
            elif akku is not None and akku <= limit - HYSTERESE:
                grund = f"Akku {akku:.0f} %"
            elif time.time() - info["seit"] > MAX_AUS_S:
                grund = "2 h um"
            elif bei != dock or alter > STILL_S:
                grund = "Remote nicht mehr an diesem Dock"
            if grund:
                eid = self._schalter(dock)
                if eid:
                    await self._schalten(eid, True)
                _LOGGER.info("Ladelimit: Ladekontakte %s wieder an (%s, %s)", dock, dev, grund)
                self.aus.pop(dock, None)
                self.frei_bis[dock] = time.time() + NACHLADEN_S
        # 2. Neue Sperren
        for dev, limit in limits.items():
            akku, kabel, dock, alter = self._stand(dev)
            if akku is None or not kabel or not dock or alter > STILL_S or dock in self.aus:
                continue
            if time.time() < self.frei_bis.get(dock, 0):
                continue
            if akku >= limit:
                eid = self._schalter(dock)
                if not eid:
                    continue
                await self._schalten(eid, False)
                self.aus[dock] = {"remote": dev, "seit": time.time()}
                _LOGGER.info("Ladelimit: %s hat %.0f %% (Limit %d) - Ladekontakte %s aus", dev, akku, limit, dock)

    async def start(self) -> None:
        # Nach einem Neustart keine Sperre vergessen: Kontakte bekannter Docks, die aus sind, wieder an
        if self.limits:
            for st in self.hass.states.async_all("switch"):
                if st.entity_id.endswith("_ladekontakte") and st.state == "off":
                    await self._schalten(st.entity_id, True)
                    _LOGGER.info("Ladelimit: %s nach dem Start wieder an", st.entity_id)
        async_track_time_interval(self.hass, self.pruefen, TAKT)

    def stand(self) -> dict:
        return {"limits": self.limits, "aus": {d: {"remote": i["remote"], "seit": int(i["seit"])} for d, i in self.aus.items()},
                "docks_mit_schalter": sorted(st.entity_id for st in self.hass.states.async_all("switch") if st.entity_id.endswith("_ladekontakte"))}


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/ladelimit_get"})
@websocket_api.require_admin
@callback
def ws_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    connection.send_result(msg["id"], hass.data[DOMAIN + "_ladelimit"].stand())


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/ladelimit_set", vol.Required("device"): str,
                                  vol.Required("prozent"): vol.All(int, vol.Any(0, vol.Range(min=50, max=95)))})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    ll: Ladelimit = hass.data[DOMAIN + "_ladelimit"]
    if msg["device"] not in ll.kd.devices:
        connection.send_error(msg["id"], "unknown_device", f"Unbekannte Remote {msg['device']}")
        return
    ll.kd.data.setdefault("ladelimit", {})[msg["device"]] = msg["prozent"]
    if not msg["prozent"]:
        ll.kd.data["ladelimit"].pop(msg["device"], None)
    await ll.kd.store.async_save(ll.kd.data)
    await ll.pruefen()
    connection.send_result(msg["id"], ll.stand())


def einrichten(hass: HomeAssistant, kd) -> Ladelimit:
    ll = Ladelimit(hass, kd)
    hass.data[DOMAIN + "_ladelimit"] = ll
    for fn in (ws_get, ws_set):
        websocket_api.async_register_command(hass, fn)
    hass.async_create_task(ll.start())
    return ll
