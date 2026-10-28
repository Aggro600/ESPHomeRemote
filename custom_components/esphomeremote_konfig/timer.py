"""Timer abgelaufen (2026-10-26): Popup auf den Remotes, wenn ein gewaehlter Timer ablaeuft.

Gewaehlt werden die Timer je Remote unter Verknuepfungen ("timer", auch mehrere). Laeuft einer davon ab, setzt die
Integration den Sensor

    sensor.esphomeremote_timer_abgelaufen   Zustand = Name des Timers
        status: abgelaufen | aus   (Popup-Ausloeser, Attribut - der Name allein wiederholt sich)
        entity_id, dauer ("0:30:00"), gestartet_um, abgelaufen_um ("19:42")

"aus" kommt, sobald irgendein gewaehlter Timer wieder startet oder nach 30 min. Das Popup "popup_timer" legt die
Integration einmalig an (__init__.py _timer_popup); es ist ein normales Popup und frei gestaltbar.
Aktionen fuer die Knoepfe: esphomeremote_konfig.timer_verlaengern (minuten) und .timer_neu_starten - beide fuer
den zuletzt abgelaufenen Timer.
"""

from __future__ import annotations

from datetime import timedelta
import logging

import voluptuous as vol

from homeassistant.core import Event, HomeAssistant, ServiceCall, callback
from homeassistant.helpers.event import async_call_later
from homeassistant.util import dt as dt_util

from .spiegel import als_liste

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"
SENSOR = "sensor.esphomeremote_timer_abgelaufen"
AUS_NACH = 1800


def _dauer_text(d: str) -> str:
    """'0:30:00' -> '30 min', '1:05:00' -> '1 h 5 min', '0:00:45' -> '45 s'."""
    try:
        h, m, s = (int(float(x)) for x in str(d).split(":"))
    except ValueError:
        return str(d or "")
    if h:
        return f"{h} h {m} min" if m else f"{h} h"
    if m:
        return f"{m} min" if not s else f"{m} min {s} s"
    return f"{s} s"


class TimerWache:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self.letzter: str = ""        # zuletzt abgelaufener Timer
        self.dauer: str = ""          # seine Dauer ("0:30:00")
        self._aus = None

    def gewaehlt(self) -> set[str]:
        out = set()
        for dev in self.kd.devices:
            try:
                out.update(als_liste((self.kd.entry(dev)["config"].get("ents") or {}).get("timer")))
            except (KeyError, AttributeError, TypeError):
                continue
        return out

    @callback
    def starten(self) -> None:
        if self.hass.states.get(SENSOR) is None:
            self.hass.states.async_set(SENSOR, "–", {"status": "aus", "friendly_name": "Timer abgelaufen", "icon": "mdi:timer-alert-outline"})
        self.hass.bus.async_listen("timer.finished", self._abgelaufen)
        self.hass.bus.async_listen("timer.started", self._gestartet)
        self.hass.bus.async_listen("timer.restarted", self._gestartet)

    @callback
    def _abgelaufen(self, ev: Event) -> None:
        e = ev.data.get("entity_id") or ""
        if e not in self.gewaehlt():
            return
        st = self.hass.states.get(e)
        name = (st.attributes.get("friendly_name") if st else None) or e
        dauer = str(st.attributes.get("duration") or "") if st else ""
        jetzt = dt_util.now()
        start = ""
        try:
            h, m, s = (int(float(x)) for x in dauer.split(":"))
            start = (jetzt - timedelta(hours=h, minutes=m, seconds=s)).strftime("%H:%M")
        except ValueError:
            pass
        self.letzter, self.dauer = e, dauer
        self.hass.states.async_set(SENSOR, name, {
            "status": "abgelaufen", "entity_id": e, "dauer": _dauer_text(dauer), "gestartet_um": start,
            "abgelaufen_um": jetzt.strftime("%H:%M Uhr"), "friendly_name": "Timer abgelaufen", "icon": "mdi:timer-alert-outline"})
        _LOGGER.info("Timer %s abgelaufen - Popup an die Remotes", e)
        if self._aus:
            self._aus()
        self._aus = async_call_later(self.hass, AUS_NACH, self._ausschalten)

    @callback
    def _gestartet(self, ev: Event) -> None:
        if ev.data.get("entity_id") in self.gewaehlt():
            self._ausschalten(None)

    @callback
    def _ausschalten(self, _now) -> None:
        st = self.hass.states.get(SENSOR)
        if st is not None and st.attributes.get("status") == "abgelaufen":
            self.hass.states.async_set(SENSOR, st.state, {**st.attributes, "status": "aus"})
        self._aus = None

    async def verlaengern(self, minuten: int) -> None:
        if self.letzter:
            await self.hass.services.async_call("timer", "start", {"entity_id": self.letzter,
                                                                   "duration": f"00:{max(1, min(600, minuten)):02d}:00"
                                                                   if minuten < 60 else f"{minuten // 60:02d}:{minuten % 60:02d}:00"})

    async def neu_starten(self) -> None:
        if self.letzter:
            daten = {"entity_id": self.letzter, **({"duration": self.dauer} if self.dauer else {})}
            await self.hass.services.async_call("timer", "start", daten)


def einrichten(hass: HomeAssistant, kd) -> TimerWache:
    w = TimerWache(hass, kd)
    hass.data[DOMAIN + "_timer"] = w
    w.starten()

    async def verlaengern(call: ServiceCall) -> None:
        await w.verlaengern(int(call.data.get("minuten", 5)))

    async def neu(call: ServiceCall) -> None:
        await w.neu_starten()

    hass.services.async_register(DOMAIN, "timer_verlaengern", verlaengern,
                                 schema=vol.Schema({vol.Optional("minuten", default=5): vol.Coerce(int)}))
    hass.services.async_register(DOMAIN, "timer_neu_starten", neu)
    return w
