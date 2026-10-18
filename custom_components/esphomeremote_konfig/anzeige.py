"""Dock-Anzeige (2026-10-17): Daten fuer die "Smart Display"-Seite der Remote, solange sie im Dock steht.

Die Remote holt sie per HTTP (wie die Belegung, auch durch den Bluetooth-Tunnel):
  GET /api/esphomeremote_konfig/anzeige/<geraet>?k=<schluessel>
  -> {"w": {"i": mdi-Name, "t": Temperatur, "z": Text, "h": Hoch, "l": Tief},
      "r": {"t": Raumtemperatur, "f": Luftfeuchte},
      "k": [[Tag, Zeit, Titel], ...]}
Was angezeigt wird, steht in data["anzeige"] (Konfigurator, Bereich Dockingstation):
  wetter, raum_t, raum_f, kalender: [...], tage, anzahl
Termine und Vorhersage werden zwischengespeichert (5 bzw. 15 min) - die Remote fragt jede Minute.
"""
from __future__ import annotations

import copy
import hmac
import json
import logging
import time
from datetime import datetime, timedelta

from aiohttp import web
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant
from homeassistant.util import dt as dt_util

_LOGGER = logging.getLogger(__name__)
STANDARD = {
    "wetter": "weather.forecast_home",   # HA-Standard; alles andere waehlt man im Konfigurator (Dockingstation)
    "raum_t": "",
    "raum_f": "",
    "kalender": [],
    "tage": 7,
    "anzahl": 3,
}
WETTER = {
    "sunny": ("weather-sunny", "Sonnig"), "clear-night": ("weather-night", "Klar"),
    "partlycloudy": ("weather-partly-cloudy", "Teils bewölkt"), "cloudy": ("weather-cloudy", "Bewölkt"),
    "fog": ("weather-fog", "Nebel"), "hail": ("weather-hail", "Hagel"), "lightning": ("weather-lightning", "Gewitter"),
    "lightning-rainy": ("weather-lightning-rainy", "Gewitter"), "pouring": ("weather-pouring", "Starkregen"),
    "rainy": ("weather-rainy", "Regen"), "snowy": ("weather-snowy", "Schnee"), "snowy-rainy": ("weather-snowy-rainy", "Schneeregen"),
    "windy": ("weather-windy", "Windig"), "windy-variant": ("weather-windy-variant", "Windig"),
    "exceptional": ("weather-sunny-alert", "Unwetter"),
}
TAGE = ["Mo", "Di", "Mi", "Do", "Fr", "Sa", "So"]


def _zahl(hass: HomeAssistant, eid: str | None) -> float | None:
    st = hass.states.get(eid or "")
    try:
        return float(st.state) if st else None
    except ValueError:
        return None


def _rund(v: float | None, n: int = 0):
    if v is None:
        return None
    return round(v, n) if n else int(round(v))


class Anzeige:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.cache: dict[str, tuple[float, object]] = {}

    @property
    def cfg(self) -> dict:
        c = self.kd.data.setdefault("anzeige", {})
        for k, v in STANDARD.items():
            if k not in c:
                c[k] = copy.deepcopy(v)
        return c

    async def _gemerkt(self, name: str, sek: float, f):
        t, v = self.cache.get(name, (0.0, None))
        if time.monotonic() - t < sek and v is not None:
            return v
        try:
            v = await f()
        except Exception as err:  # noqa: BLE001 - Anzeige soll nie an einem Dienst scheitern
            _LOGGER.debug("Anzeige %s: %s", name, err)
            v = v if v is not None else {}
        self.cache[name] = (time.monotonic(), v)
        return v

    async def _vorhersage(self) -> dict:
        eid = self.cfg["wetter"]
        r = await self.hass.services.async_call("weather", "get_forecasts", {"entity_id": eid, "type": "daily"},
                                                blocking=True, return_response=True)
        f = ((r or {}).get(eid) or {}).get("forecast") or []
        return f[0] if f else {}

    async def _termine(self) -> list:
        c = self.cfg
        kal = [k for k in c["kalender"] if self.hass.states.get(k)]
        if not kal:
            return []
        jetzt = dt_util.now()
        r = await self.hass.services.async_call(
            "calendar", "get_events",
            {"entity_id": kal, "start_date_time": jetzt.isoformat(), "end_date_time": (jetzt + timedelta(days=int(c["tage"]))).isoformat()},
            blocking=True, return_response=True)
        ev = []
        for eid, d in (r or {}).items():
            for e in (d or {}).get("events") or []:
                ev.append(e)
        out = []
        heute = jetzt.date()

        def beginn(e):
            s = str(e.get("start"))
            return (datetime.fromisoformat(s) if "T" in s else dt_util.start_of_local_day(datetime.fromisoformat(s).date()))

        for e in sorted(ev, key=lambda e: dt_util.as_local(beginn(e)) if beginn(e).tzinfo else beginn(e).replace(tzinfo=jetzt.tzinfo)):
            s = str(e.get("start"))
            ganz = "T" not in s
            b = beginn(e)
            b = dt_util.as_local(b) if b.tzinfo else b.replace(tzinfo=jetzt.tzinfo)
            tag = b.date()
            name = "Heute" if tag == heute else "Morgen" if tag == heute + timedelta(days=1) else f"{TAGE[tag.weekday()]} {tag.day}.{tag.month}."
            out.append([name, "ganztägig" if ganz else b.strftime("%H:%M"), str(e.get("summary") or "")[:40]])
            if len(out) >= int(c["anzahl"]):
                break
        return out

    async def daten(self) -> dict:
        c = self.cfg
        out: dict = {}
        st = self.hass.states.get(c["wetter"] or "")
        if st and st.state not in ("unavailable", "unknown"):
            zustand = st.state
            sonne = self.hass.states.get("sun.sun")
            nacht = sonne is not None and sonne.state == "below_horizon"
            ico, text = WETTER.get(zustand, ("weather-cloudy", zustand))
            if nacht and zustand == "partlycloudy":
                ico = "weather-night-partly-cloudy"
            if nacht and zustand == "sunny":
                ico, text = "weather-night", "Klar"
            f = await self._gemerkt("vorhersage", 900, self._vorhersage)
            out["w"] = {"i": ico, "z": text, "t": _rund(st.attributes.get("temperature")),
                        "h": _rund(f.get("temperature")), "l": _rund(f.get("templow"))}
        rt, rf = _zahl(self.hass, c["raum_t"]), _zahl(self.hass, c["raum_f"])
        if rt is not None or rf is not None:
            out["r"] = {"t": _rund(rt, 1), "f": _rund(rf)}
        out["k"] = await self._gemerkt("termine", 300, self._termine)
        return out

    def stand(self) -> dict:
        def liste(domain, dc=None):
            return sorted(({"id": s.entity_id, "name": s.attributes.get("friendly_name") or s.entity_id}
                           for s in self.hass.states.async_all(domain)
                           if s.state != "unavailable" and (dc is None or s.attributes.get("device_class") == dc)),
                          key=lambda x: x["name"].lower())
        return {"cfg": self.cfg, "wetter": liste("weather"), "temp": liste("sensor", "temperature"),
                "feuchte": liste("sensor", "humidity"), "kalender": liste("calendar")}

    async def setzen(self, neu: dict) -> None:
        c = self.cfg
        for k in ("wetter", "raum_t", "raum_f"):
            if k in neu:
                c[k] = str(neu[k] or "")
        if "kalender" in neu:
            c["kalender"] = [str(x) for x in neu["kalender"]][:12]
        if "tage" in neu:
            c["tage"] = max(1, min(31, int(neu["tage"])))
        if "anzahl" in neu:
            c["anzahl"] = max(0, min(4, int(neu["anzahl"])))
        self.cache.clear()
        await self.kd.store.async_save(self.kd.data)


class AnzeigeView(HomeAssistantView):
    url = "/api/esphomeremote_konfig/anzeige/{dev}"
    name = "api:esphomeremote_konfig:anzeige"
    requires_auth = False

    def __init__(self, kd, anz: Anzeige) -> None:
        self.kd = kd
        self.anz = anz

    async def get(self, request: web.Request, dev: str) -> web.Response:
        if not hmac.compare_digest(request.query.get("k", ""), self.kd.key):
            return web.Response(status=403)
        if dev not in self.kd.devices:
            return web.Response(status=404)
        return web.json_response(await self.anz.daten(), headers={"Cache-Control": "no-store"},
                                 dumps=lambda o: json.dumps(o, ensure_ascii=False, separators=(",", ":")))
