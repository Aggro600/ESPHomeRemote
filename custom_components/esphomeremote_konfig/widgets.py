"""Widgets der Menueseiten (2026-10-27): Daten, die die Remote nicht selbst aus einem Zustand ablesen kann.

- Wetter: aktuelle Werte stehen am weather-Objekt, die Vorhersage nicht mehr (HA holt sie nur per Dienst
  weather.get_forecasts). WetterCache holt sie fuer die Entitaeten, die eine Remote abfragt, hoechstens alle 30 min.
- Timer: Restzeit in Sekunden (laeuft: bis finishes_at, pausiert: Attribut remaining).
- Bilder: GET /api/esphomeremote_konfig/bild?k=<key>&e=<entity>&w=&h=[&m=voll|ganz] - Kamera-Schnappschuss bzw.
  Cover eines media_player, auf w x h verkleinert (JPEG fuer online_image der Remote).
"""
from __future__ import annotations

import asyncio
import io
import logging
import time

from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.util import dt as dt_util

_LOGGER = logging.getLogger(__name__)

WOCHENTAG = ("Mo", "Di", "Mi", "Do", "Fr", "Sa", "So")


def _zahl(v, nk: int = 0):
    return round(float(v), nk) if isinstance(v, (int, float)) else None


class WetterCache:
    """Tagesvorhersage je weather-Entitaet: [(tag, zustand, max, min), ...], Abruf im Hintergrund."""

    ALTER_S = 1800

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self._daten: dict[str, tuple[float, list]] = {}
        self._laeuft: set[str] = set()

    def vorhersage(self, e: str) -> list:
        d = self._daten.get(e)
        if (d is None or time.monotonic() - d[0] > self.ALTER_S) and e not in self._laeuft:
            self._laeuft.add(e)
            self.hass.async_create_task(self._holen(e))
        return d[1] if d else []

    async def _holen(self, e: str) -> None:
        try:
            r = await self.hass.services.async_call("weather", "get_forecasts", {"entity_id": e, "type": "daily"},
                                                    blocking=True, return_response=True)
            tage = []
            for f in ((r or {}).get(e) or {}).get("forecast", [])[:4]:
                t = dt_util.parse_datetime(str(f.get("datetime", "")))
                tag = WOCHENTAG[dt_util.as_local(t).weekday()] if t else ""
                tage.append([tag, str(f.get("condition") or ""), _zahl(f.get("temperature")), _zahl(f.get("templow"))])
            self._daten[e] = (time.monotonic(), tage)
        except Exception as err:  # noqa: BLE001 - kein Tagesvorhersage-Dienst: dann eben ohne
            _LOGGER.debug("Wettervorhersage %s: %s", e, err)
            self._daten[e] = (time.monotonic(), [])
        finally:
            self._laeuft.discard(e)


def wetter_eintrag(cache: WetterCache, st) -> list:
    """Zusatzfelder fuer weather.*: Temperatur, Einheit, Feuchte, Wind, Windeinheit, Vorhersage [[tag, zustand, max, min]]"""
    a = st.attributes
    return [_zahl(a.get("temperature"), 1), str(a.get("temperature_unit") or "°C"), _zahl(a.get("humidity")),
            _zahl(a.get("wind_speed")), str(a.get("wind_speed_unit") or "km/h"), cache.vorhersage(st.entity_id)]


def _sek(text) -> int | None:
    """'0:05:00' / '1:02:03' -> Sekunden"""
    try:
        teile = [int(float(x)) for x in str(text).split(":")]
    except (TypeError, ValueError):
        return None
    s = 0
    for t in teile:
        s = s * 60 + t
    return s


def timer_eintrag(st) -> list:
    """Zusatzfelder fuer timer.*: Restzeit (s), Dauer (s), Name"""
    a = st.attributes
    dauer = _sek(a.get("duration")) or 0
    rest = _sek(a.get("remaining"))
    if st.state == "active" and a.get("finishes_at"):
        ende = dt_util.parse_datetime(str(a.get("finishes_at")))
        if ende:
            rest = max(0, round((ende - dt_util.utcnow()).total_seconds()))
    if rest is None:
        rest = dauer if st.state == "idle" else 0
    return [rest, dauer, str(a.get("friendly_name") or st.entity_id)[:40]]


def _verkleinern(daten: bytes, w: int, h: int, voll: bool) -> bytes:
    from PIL import Image
    src = Image.open(io.BytesIO(daten)).convert("RGB")
    r = src.width / src.height
    if voll:   # randlos, beschnitten
        nw, nh = (round(h * r), h) if r > w / h else (w, round(w / r))
        l, t = (nw - w) // 2, (nh - h) // 2
        bild = src.resize((nw, nh), Image.LANCZOS).crop((l, t, l + w, t + h))
    else:      # ganz, schwarze Balken
        nw, nh = (w, round(w / r)) if r > w / h else (round(h * r), h)
        bild = Image.new("RGB", (w, h), (0, 0, 0))
        bild.paste(src.resize((max(1, nw), max(1, nh)), Image.LANCZOS), ((w - nw) // 2, (h - nh) // 2))
    b = io.BytesIO()
    bild.save(b, "JPEG", quality=80)
    return b.getvalue()


class BildView(HomeAssistantView):
    """Bild fuer ein Widget (Kamera, Cover), passend verkleinert"""

    url = "/api/esphomeremote_konfig/bild"
    name = "api:esphomeremote_konfig:bild"
    requires_auth = False

    def __init__(self, kd, schluessel_ok) -> None:
        self.kd = kd
        self._ok = schluessel_ok
        self._cache: dict[tuple, tuple[float, bytes]] = {}
        self._sperre = asyncio.Lock()

    async def get(self, request: web.Request) -> web.Response:
        if not self._ok(request, self.kd.key):
            return web.Response(status=403)
        hass = request.app["hass"]
        e = request.query.get("e", "")
        try:
            w = max(16, min(240, int(request.query.get("w", "216"))))
            h = max(16, min(320, int(request.query.get("h", "112"))))
        except ValueError:
            return web.Response(status=400)
        voll = request.query.get("m", "voll") != "ganz"
        st = hass.states.get(e)
        if st is None or not e.startswith(("camera.", "media_player.")):
            return web.Response(status=404)
        quelle = st.attributes.get("entity_picture") if e.startswith("media_player.") else None
        schl = (e, w, h, voll, quelle)
        async with self._sperre:
            c = self._cache.get(schl)
            # Kamera: 5 s, Cover: solange dasselbe Bild
            if c and (quelle or time.monotonic() - c[0] < 5):
                return web.Response(body=c[1], content_type="image/jpeg", headers={"Cache-Control": "no-store"})
            try:
                if e.startswith("camera."):
                    from homeassistant.components.camera import async_get_image
                    roh = (await async_get_image(hass, e, timeout=10)).content
                else:
                    if not quelle:
                        return web.Response(status=404)
                    holen = quelle if quelle.startswith(("http://", "https://")) else \
                        "http://127.0.0.1:%d%s" % (hass.http.server_port or 8123, quelle if quelle.startswith("/") else "/" + quelle)
                    async with async_get_clientsession(hass, verify_ssl=False).get(holen, timeout=15) as r:
                        r.raise_for_status()
                        roh = await r.read()
                jpg = await hass.async_add_executor_job(_verkleinern, roh, w, h, voll)
            except Exception as err:  # noqa: BLE001
                _LOGGER.debug("Widget-Bild %s: %s", e, err)
                return web.Response(status=502)
            if len(self._cache) > 20:
                self._cache.clear()
            self._cache[schl] = (time.monotonic(), jpg)
        return web.Response(body=jpg, content_type="image/jpeg", headers={"Cache-Control": "no-store"})
