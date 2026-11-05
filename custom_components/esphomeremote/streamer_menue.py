"""Popup-Menue auf dem Fernseher fuer jeden Streamer (2026-11-05).

Nutzer: "Das, was wir fuer den Magenta gemacht haben, inklusive dem Menue, das moechte ich auch im Konfigurator ...
mach das Menue so wie das von der Remote. Nutze bei Bedarf den gesamten TV-Screen oder nur eine einstellbare Groesse.
Das sind nicht nur Magenta-Sachen, sondern fuer alle Streamer."

- Je Streamer: Menue im Format der Remote (Seiten mit Kacheln, {start, pages}), Menue-Taste (Android-Tastencode, von der
  Streamer-App abgefangen), Groesse (Prozent des Bildschirms, 100 = ganzer Bildschirm) und Lage.
- Die Streamer-App (ab 1.17, WebMenue.java) zeigt das Menue als WebView ueber dem laufenden Bild. Die Seite liefert HA
  (frontend/streamer_menu.html): Kacheln, Symbole, Live-Zustaende. Pfeile/OK/Zurueck reicht die App an die Seite weiter.
- Aktionen fuehrt HA aus (nur Kacheln aus dem gespeicherten Menue - die Seite schickt Seite + Index, keine Dienste).
- Adresse: <basis>/api/esphomeremote/stmenue/<geheim>/<streamer>/<art> - das Geheimnis bleibt gespeichert (der Stick
  merkt sich die Adresse), ohne HA-Anmeldung wie das Kamera-Popup.
"""
from __future__ import annotations

import asyncio
import logging
import re
import secrets
from pathlib import Path

import voluptuous as vol
from aiohttp import web

from homeassistant.components import websocket_api
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant, callback

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
KEY = "streamer_menue"
PFAD = f"/api/{DOMAIN}/stmenue"
SEITE = Path(__file__).parent / "frontend" / "streamer_menu.html"
POSITIONEN = ("mitte", "links", "rechts", "oben", "unten", "oben_links", "oben_rechts", "unten_links", "unten_rechts")
# Werte der Aktion (englisch) -> intern
LAGE_ID = {"center": "mitte", "left": "links", "right": "rechts", "top": "oben", "bottom": "unten", "top-left": "oben_links",
           "top-right": "oben_rechts", "bottom-left": "unten_links", "bottom-right": "unten_rechts"}
MAX_SEITEN = 60

STANDARD = {"an": False, "taste": None, "groesse": 70, "position": "mitte", "dauer": 45, "menu": {"start": "main", "pages": [
    {"id": "main", "title": "Menü", "items": []}]}}


def _geheim(kd) -> str:
    g = kd.data.get(KEY + "_geheim")
    if not g:
        g = kd.data[KEY + "_geheim"] = secrets.token_urlsafe(24)
    return g


def validieren(d: dict) -> dict:
    if not isinstance(d, dict):
        raise vol.Invalid("Menü: Objekt erwartet")
    taste = d.get("taste")
    if taste in ("", None):
        taste = None
    else:
        taste = int(taste)
        if not 1 <= taste <= 400:
            raise vol.Invalid("Menü-Taste ungültig")
    groesse = max(30, min(100, int(d.get("groesse") or 70)))
    pos = d.get("position") if d.get("position") in POSITIONEN else "mitte"
    menu = d.get("menu") or {}
    pages = menu.get("pages") if isinstance(menu.get("pages"), list) else []
    if len(pages) > MAX_SEITEN:
        raise vol.Invalid(f"Menü: höchstens {MAX_SEITEN} Seiten")
    sauber = []
    for p in pages:
        if not isinstance(p, dict) or not re.fullmatch(r"[A-Za-z0-9_\-]{1,40}", str(p.get("id") or "")):
            raise vol.Invalid("Menü: Seite ohne gültige Kennung")
        items = p.get("items") if isinstance(p.get("items"), list) else []
        sauber.append({**p, "items": [i for i in items if isinstance(i, dict)][:80]})
    start = str(menu.get("start") or (sauber[0]["id"] if sauber else "main"))
    try:
        dauer = max(0, min(3600, int(d.get("dauer", 45))))
    except (TypeError, ValueError):
        dauer = 45
    return {"an": bool(d.get("an")), "taste": taste, "groesse": groesse, "position": pos, "dauer": dauer,
            "menu": {"start": start, "pages": sauber or STANDARD["menu"]["pages"]}}


def daten(kd, sid: str) -> dict:
    return (kd.data.get(KEY) or {}).get(sid) or validieren(dict(STANDARD))


def url(kd, sid: str) -> str:
    b = kd.basis()
    return f"http://{b}{PFAD}/{_geheim(kd)}/{sid}/seite" if b else ""


async def anwenden(hass: HomeAssistant, sid: str) -> None:
    """Einstellungen an den Streamer (ADB): Menue-Taste, Adresse, Groesse/Lage; Leserecht fuer die Fernseher-Tasten."""
    s = hass.data[DOMAIN + "_streamer"]
    st = s.alle.get(sid)
    if st is None or not st.cfg.get("adb"):
        return
    d = daten(s.kd, sid)
    u = url(s.kd, sid) if d["an"] else ""
    taste = d["taste"] if d["an"] and d["taste"] else ""
    befehl = (f"settings put global esphomeremote_menue_url '{u}'; settings put global esphomeremote_menue_taste '{taste}'; "
              f"settings put global esphomeremote_menue_groesse {d['groesse']}; "
              f"settings put global esphomeremote_menue_position {d['position']}; "
              "pm grant de.esphomeremote.streamer android.permission.READ_LOGS 2>/dev/null")
    await st.adb(befehl)


# ----------------------------------------------------------------- Aktionen


def _tv_fuer(hass: HomeAssistant, sid: str) -> str:
    """Fernseher (Kamera auf dem TV) dieses Streamers: Kennung oder Name fuer esphomeremote.camera_tv."""
    f = hass.data.get(DOMAIN + "_funktionen")
    if f is None:
        return ""
    for tid, k in f._tvs():  # noqa: SLF001
        if k.get("streamer") == sid:
            return tid or str(k.get("name") or "")
    return ""


def _kamera(it: dict) -> str:
    e = str(it.get("entity") or it.get("src") or "")
    return e if e.startswith("camera.") else (f"camera.{e}" if e else "")


async def _dienst(hass: HomeAssistant, svc: str, daten_: dict) -> None:
    if "." not in svc:
        return
    dom, name = svc.split(".", 1)
    await hass.services.async_call(dom, name, daten_, blocking=False)


async def ausfuehren(hass: HomeAssistant, sid: str, it: dict, art: str) -> None:
    t = it.get("t")
    e = str(it.get("entity") or "")
    dom = e.split(".", 1)[0] if e else ""
    # Eigene Schritte gehen vor (wie auf der Remote: Schalter/Anzeige mit Aktion, z. B. Skript statt toggle)
    if t != "action" and art not in ("plus", "minus") and any(x.get("t") == "ha" for x in it.get("steps") or []):
        t = "action"
    if t == "action":
        for s in it.get("steps") or []:
            if s.get("t") == "ha":
                d = dict(s.get("data") or {})
                if s.get("entity"):
                    d.setdefault("entity_id", s["entity"])
                await _dienst(hass, str(s.get("svc") or ""), d)
            elif s.get("t") == "wait":
                await asyncio.sleep(min(10.0, (s.get("ms") or 0) / 1000))
    elif t == "camera":
        await _dienst(hass, f"{DOMAIN}.camera_tv", {"tv": _tv_fuer(hass, sid), "camera": _kamera(it)})
    elif t in ("toggle", "light", "cover") and e:
        if t == "light" and art in ("plus", "minus"):
            await _dienst(hass, "light.turn_on", {"entity_id": e, "brightness_step_pct": 10 if art == "plus" else -10})
        elif t == "cover" and art in ("plus", "minus"):
            await _dienst(hass, f"cover.{'open' if art == 'plus' else 'close'}_cover", {"entity_id": e})
        else:
            await _dienst(hass, "homeassistant.toggle", {"entity_id": e})
    elif t == "mtaste" and e:
        fn = it.get("fn") or "play_pause"
        z = {"play_pause": "media_play_pause", "next": "media_next_track", "prev": "media_previous_track",
             "vol_up": "volume_up", "vol_down": "volume_down", "stop": "media_stop", "power": "toggle"}
        if fn == "mute":
            st = hass.states.get(e)
            await _dienst(hass, "media_player.volume_mute",
                          {"entity_id": e, "is_volume_muted": not (st and st.attributes.get("is_volume_muted"))})
        elif fn in z:
            await _dienst(hass, f"media_player.{z[fn]}", {"entity_id": e})
    elif t == "vpreset" and e:
        await _dienst(hass, "media_player.volume_set", {"entity_id": e, "volume_level": max(0, min(100, it.get("wert") or 0)) / 100})
    elif t == "number" and e:
        if dom == "media_player":
            await _dienst(hass, f"media_player.volume_{'up' if art != 'minus' else 'down'}", {"entity_id": e})
        elif dom == "input_number":
            await _dienst(hass, f"input_number.{'increment' if art != 'minus' else 'decrement'}", {"entity_id": e})
        elif dom == "number":
            await _dienst(hass, "number.set_value", {"entity_id": e, "value": _schritt(hass, e, art)})
    elif t in ("select", "dropdown") and e and dom in ("input_select", "select"):
        await _dienst(hass, f"{dom}.select_{'previous' if art == 'minus' else 'next'}", {"entity_id": e, "cycle": True})


def _schritt(hass: HomeAssistant, e: str, art: str) -> float:
    st = hass.states.get(e)
    try:
        v, step = float(st.state), float(st.attributes.get("step") or 1)
        lo, hi = float(st.attributes.get("min", v)), float(st.attributes.get("max", v))
    except (AttributeError, TypeError, ValueError):
        return 0
    return max(lo, min(hi, v + (step if art != "minus" else -step)))


# ----------------------------------------------------------------- Seite und Daten


def _entitaeten(menu: dict) -> set[str]:
    out = set()
    for p in menu.get("pages") or []:
        for it in p.get("items") or []:
            for k in ("entity", "we"):
                if isinstance(it.get(k), str) and "." in it[k]:
                    out.add(it[k])
    return out


ATTR = ("friendly_name", "brightness", "current_position", "options", "unit_of_measurement", "media_title",
        "media_artist", "volume_level", "is_volume_muted", "min", "max", "step", "temperature", "icon")


class StreamerMenueView(HomeAssistantView):
    url = PFAD + "/{geheim}/{sid}/{art}"
    name = "api:esphomeremote:stmenue"
    requires_auth = False

    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd

    def _ok(self, geheim: str, sid: str) -> bool:
        return secrets.compare_digest(geheim, _geheim(self.kd)) and sid in (self.kd.data.get(KEY) or {})

    async def get(self, request: web.Request, geheim: str, sid: str, art: str) -> web.Response:
        if not self._ok(geheim, sid):
            return web.Response(status=404)
        if art == "seite":
            text = (await self.hass.async_add_executor_job(SEITE.read_text, "utf-8")).replace(
                "__LANG__", re.sub(r"[^A-Za-z-]", "", self.hass.config.language or "en") or "en")
            return web.Response(text=text, content_type="text/html", headers={"Cache-Control": "no-store"})
        if art == "daten":
            d = daten(self.kd, sid)
            zust = {}
            for e in _entitaeten(d["menu"]):
                st = self.hass.states.get(e)
                if st is not None:
                    zust[e] = {"s": st.state, "a": {k: st.attributes[k] for k in ATTR if k in st.attributes}}
            return web.json_response({"menu": d["menu"], "zustaende": zust, "dauer": d.get("dauer", 45)})
        return web.Response(status=404)

    async def post(self, request: web.Request, geheim: str, sid: str, art: str) -> web.Response:
        if not self._ok(geheim, sid) or art != "aktion":
            return web.Response(status=404)
        try:
            q = await request.json()
            seite, idx = str(q.get("seite")), int(q.get("index"))
            wie = str(q.get("art") or "tap")
        except (ValueError, TypeError, AttributeError):
            return web.Response(status=400)
        menu = daten(self.kd, sid)["menu"]
        p = next((x for x in menu["pages"] if x.get("id") == seite), None)
        if p is None or not 0 <= idx < len(p["items"]):
            return web.Response(status=404)
        try:
            await ausfuehren(self.hass, sid, p["items"][idx], wie)
        except Exception as err:  # noqa: BLE001 - eine Kachel darf die Seite nicht abschiessen
            _LOGGER.warning("Streamer-Menü %s: %s", sid, err)
            return web.json_response({"ok": False, "fehler": str(err)})
        return web.json_response({"ok": True})


# ----------------------------------------------------------------- Konfigurator


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/streamer_menue_get", vol.Required("streamer"): str})
@websocket_api.require_admin
@callback
def ws_get(hass, connection, msg) -> None:
    s = hass.data[DOMAIN + "_streamer"]
    connection.send_result(msg["id"], {"daten": daten(s.kd, msg["streamer"]), "url": url(s.kd, msg["streamer"])})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/streamer_menue_save", vol.Required("streamer"): str,
                                  vol.Required("daten"): dict})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_save(hass, connection, msg) -> None:
    s = hass.data[DOMAIN + "_streamer"]
    sid = msg["streamer"]
    if sid not in s.alle:
        connection.send_error(msg["id"], "unbekannt", "Streamer unbekannt")
        return
    try:
        d = validieren(msg["daten"])
    except (vol.Invalid, ValueError, TypeError) as e:
        connection.send_error(msg["id"], "ungueltig", str(e))
        return
    s.kd.data.setdefault(KEY, {})[sid] = d
    _geheim(s.kd)
    await s.kd.store.async_save(s.kd.data)
    angewendet = True
    try:
        await anwenden(hass, sid)
    except Exception as err:  # noqa: BLE001 - Streamer aus: gilt beim naechsten Speichern/Einrichten
        _LOGGER.warning("Streamer-Menü %s nicht angewendet: %s", sid, err)
        angewendet = False
    connection.send_result(msg["id"], {"daten": d, "url": url(s.kd, sid), "angewendet": angewendet})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/streamer_lernen", vol.Required("streamer"): str,
                                  vol.Required("an"): bool})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_lernen(hass, connection, msg) -> None:
    """Lernmodus der Streamer-App: jede Taste wird als esphomeremote_streamer_key (press "learn") gemeldet."""
    s = hass.data[DOMAIN + "_streamer"]
    st = s.alle.get(msg["streamer"])
    if st is None or not st.cfg.get("adb"):
        connection.send_error(msg["id"], "unbekannt", "Streamer ohne ADB-Gerät")
        return
    try:
        await st.adb(f"settings put global esphomeremote_tasten_lernen {1 if msg['an'] else 0}")
    except Exception as err:  # noqa: BLE001
        connection.send_error(msg["id"], "adb", str(err))
        return
    connection.send_result(msg["id"], {"ok": True})


def einrichten(hass: HomeAssistant, kd) -> None:
    hass.http.register_view(StreamerMenueView(hass, kd))
    for fn in (ws_get, ws_save, ws_lernen):
        websocket_api.async_register_command(hass, fn)

    async def dienst_menue(call) -> None:
        """Menue auf dem Fernseher oeffnen/schliessen (z. B. von einer Remote-Taste). Felder englisch:
        command (toggle/open/close), page, size, position - alte deutsche Namen ueber aliases.py."""
        from .streamer import _streamer_finden
        st = _streamer_finden(hass.data[DOMAIN + "_streamer"], call.data.get("streamer"))
        if st is None or not st.cfg.get("adb"):
            raise vol.Invalid(f"Unknown streamer '{call.data.get('streamer')}' or no ADB device")
        d = daten(kd, st.cfg["id"])
        if not d["an"]:
            raise vol.Invalid("The TV menu is not enabled for this streamer (Configurator > Streamer)")
        b = {"open": "auf", "close": "zu", "toggle": "umschalten"}.get(str(call.data.get("command") or "toggle").lower())
        if b is None:
            raise vol.Invalid("command: toggle, open or close")
        # App ab 1.18: direkt per Rundruf (keine Menue-Taste noetig); ab 1.19 Groesse/Lage/Seite nur fuer diesmal
        extra = ""
        if b != "zu":
            if (g := call.data.get("size")) not in (None, ""):
                extra += f" --ei groesse {max(30, min(100, int(g)))}"
            if (p := LAGE_ID.get(str(call.data.get("position") or ""))):
                extra += f" --es position {p}"
            if re.fullmatch(r"[A-Za-z0-9_\-]{1,40}", str(call.data.get("page") or "")):
                extra += f" --es seite {call.data['page']}"
                if b == "umschalten":
                    b = "auf"   # bestimmte Seite: immer zeigen
        await st.adb(f"am broadcast -a de.esphomeremote.streamer.MENU -p de.esphomeremote.streamer --es web {b}{extra}")

    from .aliases import schema_with_aliases
    hass.services.async_register(DOMAIN, "streamer_menu", dienst_menue, schema=schema_with_aliases("streamer_menu"))
    # Name bis 2026-11-05 (nicht in services.yaml)
    hass.services.async_register(DOMAIN, "streamer_menue", dienst_menue, schema=schema_with_aliases("streamer_menu"))
