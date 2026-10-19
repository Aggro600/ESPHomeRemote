"""Streamer-Funktion (2026-10-19, vorher Template-Paket packages/esphomeremote_smarttube.yaml mit festen Entitaeten).

Auf einem Android-TV-Streamer laeuft die App "ESPHomeRemote Streamer" (esphomeremote_streamer_app). Sie schickt bei
jeder Aenderung den Text ihrer Abfrage (Medien-Sitzungen, Vordergrund-App, Tastatur, Laufzeit) an einen Webhook.
Die Integration wertet ihn aus und liefert:
  - die Daten fuer den "Jetzt laeuft"-Player (jetzt.py: App, Titel, Interpret, Cover, Position, Dauer, Tempo)
  - Sensoren je Streamer (Tastatur offen? fuer Popups, Wiedergabestatus, App, Titel, am Fernseher sichtbar)
  - Knoepfe "App beenden" und "Neu starten" (ueber die ADB-Entitaet)
Eingestellt im Konfigurator (Seite "Wiedergabe", kd.data["streamer"]): Name, ADB-Entitaet (androidtv), Webhook.
Die Ziel-Adresse traegt der Knopf "Am Streamer eintragen" per ADB ein (settings put global esphomeremote_webhook).
"""

from __future__ import annotations

import logging
import re
import secrets
from typing import Any, Callable

import voluptuous as vol

from homeassistant.components import webhook, websocket_api
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.util import dt as dt_util

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"

NAMEN = {
    "com.netflix.ninja": "Netflix", "org.smarttube.stable": "SmartTube", "com.google.android.youtube.tv": "YouTube",
    "com.google.android.apps.youtube.music": "YouTube Music", "com.disney.disneyplus": "Disney+", "de.rtli.tvnow": "RTL+",
    "org.xbmc.kodi": "Kodi", "com.amazon.amazonvideo.livingroom": "Prime Video", "com.spotify.tv.android": "Spotify",
    "com.plexapp.android": "Plex", "de.sky.bw": "Sky", "com.apple.atve.androidtv.appletv": "Apple TV",
    "com.google.android.apps.tv.launcherx": "Google TV", "com.wbd.stream": "HBO Max", "de.zdf.mediathek": "ZDF",
    "com.zdf.android.mediathek": "ZDF", "com.google.android.youtube.tvmusic": "YouTube Music",
    "de.prosiebensat1digital.seventv": "Joyn", "de.swr.avp.ard.tv": "ARD Mediathek", "org.videolan.vlc": "VLC",
    "com.google.android.videos": "Google Play Filme", "io.homeassistant.companion.androidtv": "HA Dashboard",
    "com.lonelycatgames.Xplore": "X-plore", "com.android.gallery3d": "Galerie", "com.google.android.play.games": "Play Games",
    "com.android.vending": "Play Store", "com.rma.speedtesttv": "Speedtest", "flar2.homebutton": "Button Mapper",
}
LAUNCHER = ("com.google.android.apps.tv.launcherx", "com.google.android.tvlauncher", "com.spocky.projengmenu",
            "com.android.systemui", "com.android.tv.settings", "com.google.android.tvsettings")
SMARTTUBE = "org.smarttube.stable"
RE_SITZUNG = re.compile(r"package=([^ \n]+)[ \n]+state=PlaybackState [{]state=[A-Z_]+[(]([0-9]+)[)], position=([0-9]+), "
                        r"buffered position=[0-9]+, speed=([-0-9.]+), updated=([0-9]+)[^\n]*[\n][ ]+metadata: ([^\n]*)")
RE_TOP = re.compile(r"topResumedActivity=ActivityRecord[{][0-9a-f]+ u[0-9]+ ([A-Za-z0-9_.]+)/")
LEER, NETFLIX = "/local/esphomeremote_cover_leer.png", "/local/esphomeremote_netflix_n.png"


def validate_streamer(liste) -> list[dict]:
    if not isinstance(liste, list):
        raise vol.Invalid("Streamer: Liste erwartet")
    out, ids = [], set()
    for st in liste[:4]:
        sid = str(st.get("id") or "").strip()
        if not re.fullmatch(r"[a-z0-9_]{1,24}", sid) or sid in ids:
            raise vol.Invalid(f"Streamer: Kennung '{sid}' ungültig oder doppelt")
        ids.add(sid)
        adb = str(st.get("adb") or "").strip()
        if adb and not re.fullmatch(r"media_player\.[a-z0-9_]+", adb):
            raise vol.Invalid(f"Streamer: '{adb}' ist kein Media Player (Android-TV-Integration)")
        wh = str(st.get("webhook") or "").strip() or f"esphomeremote_streamer_{secrets.token_hex(12)}"
        if not re.fullmatch(r"[A-Za-z0-9_-]{8,80}", wh):
            raise vol.Invalid("Streamer: Webhook-Kennung ungültig")
        out.append({"id": sid, "name": str(st.get("name") or "Streamer")[:32], "adb": adb, "webhook": wh})
    return out


def auswerten(r: str, alt: dict, basis: str) -> dict:
    """Text der Streamer-App -> Daten (wie frueher die Template-Sensoren)."""
    ss = [m for m in RE_SITZUNG.findall(r) if m[0] not in ("com.google.android.bluetooth", "com.android.bluetooth")]
    tm = RE_TOP.findall(r)
    top = tm[0] if tm else ""
    playing = [x for x in ss if x[1] == "3"]
    paused = [x for x in ss if x[1] == "2" and x[0] == top]
    sel = playing[0] if playing else (paused[0] if paused else None)
    pkg = sel[0] if sel else ""
    name = lambda p: NAMEN.get(p) or p.split(".")[-1].capitalize()   # noqa: E731
    fg = name(top) if (top and top not in LAUNCHER and not sel) else ""
    app = name(pkg) if sel else fg
    ist_st = pkg == SMARTTUBE
    feld = lambda k: ((re.findall(k + r"=([^\n]*)", r) or [""])[0]).strip()   # noqa: E731
    # Titel: SmartTube genau aus dem Helfer, andere Apps aus der Beschreibung der Sitzung, sonst der App-Name
    if not sel:
        titel = fg
    else:
        d = (re.findall(r"description=(.*)", sel[5]) or [""])[0]
        d = re.sub(r"(, null)+$", "", d)
        st_titel = feld("TITEL")
        if ist_st and st_titel:
            titel = st_titel
        elif d and d != "null":
            titel = app if d.lower().replace(" ", "") == app.lower().replace(" ", "") else d
        else:
            titel = app
    interpret = feld("ARTIST") if (sel and ist_st) else ""
    status = ("paused" if fg else "") if not sel else ("playing" if sel[1] == "3" else "paused")
    leer, netflix = f"http://{basis}{LEER}", f"http://{basis}{NETFLIX}"
    art = ""
    for a_pkg, a_url in re.findall(r"ART:([^=\n]+)=([^\n]*)", r):
        if a_pkg == pkg:
            art = a_url.strip()
    if not sel:
        cover = leer if fg else ""
    elif pkg == "com.netflix.ninja":
        cover = netflix
    else:
        cover = feld("COVER") if (ist_st and feld("COVER")) else (art or leer)
    # Position (s) hochgerechnet mit der Laufzeit des Streamers; der alte Wert bleibt, solange er hoechstens 2 s
    # daneben liegt (sonst ginge jede neue Zahl an alle Fernbedienungen, obwohl sie selbst weiterzaehlen)
    pos, tempo = -1, 1.0
    if sel:
        up = float((re.findall(r"---UP[ \n]+([0-9.]+)", r) or [0])[0]) * 1000
        laeuft = sel[1] == "3"
        jetzt = float(sel[2]) + ((up - float(sel[4])) * float(sel[3]) if (laeuft and up > 0) else 0)
        neu = max(int(jetzt / 1000), 0)
        a_pos, a_zeit, a_lief = alt.get("position", -1), alt.get("pos_zeit"), alt.get("laeuft")
        erwartet = a_pos + ((dt_util.utcnow() - a_zeit).total_seconds() * float(sel[3]) if (a_zeit and laeuft) else 0)
        pos = a_pos if (a_pos >= 0 and a_lief == laeuft and abs(neu - erwartet) <= 2) else neu
        tempo = float(playing[0][3]) if playing else 1.0
    dm = re.findall("DUR:" + re.escape(pkg) + "=([0-9]+)", r) if sel else []
    dauer = int(int(dm[0]) / 1000) if dm else 0
    if "mInputShown=true" not in r:
        tastatur = "aus"
    else:
        t = int((re.findall(r"editorInfo: +inputType=([0-9]+)", r) or ["1"])[-1])
        tastatur = "zahlen" if (t % 16) in (2, 3, 4) else "text"
    out = {"titel": titel, "interpret": interpret, "status": status, "cover": cover, "app": app, "paket": pkg,
           "position": pos, "tempo": tempo, "dauer": dauer, "tastatur": tastatur, "laeuft": bool(playing),
           "zuletzt": dt_util.utcnow()}
    out["pos_zeit"] = dt_util.utcnow() if pos != alt.get("position") or not alt.get("pos_zeit") else alt["pos_zeit"]
    return out


class Streamer:
    """Ein Streamer: empfaengt den Webhook, haelt die ausgewerteten Daten, meldet Aenderungen an Abonnenten."""

    def __init__(self, hass: HomeAssistant, kd, cfg: dict) -> None:
        self.hass, self.kd, self.cfg = hass, kd, cfg
        self.daten: dict[str, Any] = {}
        self.cec: str | None = None   # zeigt der Fernseher gerade den Streamer? "true"/"false" (HDMI-CEC per ADB)
        self._abos: list[Callable[[], None]] = []
        self._weg: list[Callable[[], None]] = []

    @callback
    def abonnieren(self, fn: Callable[[], None]) -> Callable[[], None]:
        self._abos.append(fn)
        return lambda: self._abos.remove(fn) if fn in self._abos else None

    @callback
    def _melden(self) -> None:
        for fn in list(self._abos):
            fn()

    @callback
    def starten(self) -> None:
        webhook.async_register(self.hass, DOMAIN, f"Streamer {self.cfg['name']}", self.cfg["webhook"], self._webhook,
                               local_only=True, allowed_methods=["POST"])
        self._weg.append(lambda: webhook.async_unregister(self.hass, self.cfg["webhook"]))
        if self.cfg["adb"]:
            from datetime import timedelta
            self._weg.append(async_track_time_interval(self.hass, self._cec_fragen, timedelta(seconds=30)))
            self.hass.async_create_task(self._cec_fragen(None))

    @callback
    def stoppen(self) -> None:
        for w in self._weg:
            w()
        self._weg.clear()

    async def _webhook(self, hass, webhook_id, request):
        try:
            text = str((await request.json()).get("text") or "")
        except Exception:  # noqa: BLE001
            return None
        self.daten = auswerten(text, self.daten, self.kd.basis() or "homeassistant.local:8123")
        self._melden()
        return None

    # ---- ADB (androidtv-Integration): HDMI-CEC, App beenden, Neustart
    async def adb(self, befehl: str) -> str:
        if not self.cfg["adb"]:
            raise vol.Invalid("Kein ADB-Gerät gewählt")
        await self.hass.services.async_call("androidtv", "adb_command", {"entity_id": self.cfg["adb"], "command": befehl}, blocking=True)
        st = self.hass.states.get(self.cfg["adb"])
        return str(st.attributes.get("adb_response") or "") if st else ""

    async def _cec_fragen(self, _now) -> None:
        st = self.hass.states.get(self.cfg["adb"])
        if st is None or st.state in ("off", "standby", "unavailable", "unknown"):
            return
        try:
            r = await self.adb("dumpsys hdmi_control 2>/dev/null | grep -m1 'isActiveSource()'")
        except Exception as err:  # noqa: BLE001
            _LOGGER.debug("Streamer %s CEC: %s", self.cfg["name"], err)
            return
        m = re.findall(r"isActiveSource\(\): (true|false)", r)
        if m and m[0] != self.cec:
            self.cec = m[0]
            self._melden()

    async def app_beenden(self) -> None:
        st = self.hass.states.get(self.cfg["adb"]) if self.cfg["adb"] else None
        app = str(st.attributes.get("app_id") or "").strip() if st else ""
        if app and "launcher" not in app.lower() and app != "com.android.systemui":
            await self.adb(f"am force-stop {app}")

    async def eintragen(self) -> str:
        """Ziel-Adresse des Webhooks auf dem Streamer setzen (die App liest sie aus den globalen Einstellungen)."""
        url = f"http://{self.kd.basis()}/api/webhook/{self.cfg['webhook']}"
        await self.adb(f"settings put global esphomeremote_webhook {url}")
        return url


class Streamers:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self.alle: dict[str, Streamer] = {}

    @property
    def liste(self) -> list[dict]:
        return self.kd.data.get("streamer") or []

    @callback
    def sync(self) -> None:
        soll = {s["id"]: s for s in self.liste}
        for sid in list(self.alle):
            if sid not in soll:
                self.alle.pop(sid).stoppen()
            elif self.alle[sid].cfg != soll[sid]:   # Einstellungen geaendert: dasselbe Objekt (Abonnenten bleiben)
                st = self.alle[sid]
                st.stoppen()
                st.cfg = soll[sid]
                st.starten()
        for sid, cfg in soll.items():
            if sid not in self.alle:
                self.alle[sid] = Streamer(self.hass, self.kd, cfg)
                self.alle[sid].starten()


def einrichten(hass: HomeAssistant, kd) -> Streamers:
    s = Streamers(hass, kd)
    hass.data[DOMAIN + "_streamer"] = s
    s.sync()
    for fn in (ws_streamer_get, ws_streamer_save, ws_streamer_eintragen):
        websocket_api.async_register_command(hass, fn)
    return s


def _antwort(hass) -> dict:
    s: Streamers = hass.data[DOMAIN + "_streamer"]
    return {"streamer": s.liste, "basis": s.kd.basis(),
            "zuletzt": {sid: (st.daten.get("zuletzt").isoformat() if st.daten.get("zuletzt") else None) for sid, st in s.alle.items()},
            "app": {sid: st.daten.get("app") or "" for sid, st in s.alle.items()}}


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/streamer_get"})
@callback
def ws_streamer_get(hass, connection, msg) -> None:
    connection.send_result(msg["id"], _antwort(hass))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/streamer_save", vol.Required("streamer"): list})
@websocket_api.async_response
async def ws_streamer_save(hass, connection, msg) -> None:
    s: Streamers = hass.data[DOMAIN + "_streamer"]
    try:
        s.kd.data["streamer"] = validate_streamer(msg["streamer"])
    except (vol.Invalid, ValueError, TypeError, AttributeError) as e:
        connection.send_error(msg["id"], "ungueltig", str(e))
        return
    # "Jetzt laeuft"-Player ohne den geloeschten Streamer
    ids = {x["id"] for x in s.liste}
    for j in s.kd.data.get("jetzt") or []:
        if j.get("streamer") and j["streamer"] not in ids:
            j["streamer"] = ""
    await s.kd.store.async_save(s.kd.data)
    s.sync()
    if (v := hass.data.get(DOMAIN + "_dock")) is not None:
        await v.sync()
    connection.send_result(msg["id"], _antwort(hass))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/streamer_eintragen", vol.Required("streamer"): str})
@websocket_api.async_response
async def ws_streamer_eintragen(hass, connection, msg) -> None:
    st = hass.data[DOMAIN + "_streamer"].alle.get(msg["streamer"])
    if st is None:
        connection.send_error(msg["id"], "ungueltig", "Unbekannter Streamer")
        return
    try:
        url = await st.eintragen()
    except Exception as e:  # noqa: BLE001
        connection.send_error(msg["id"], "adb", str(e))
        return
    connection.send_result(msg["id"], {"url": url})


# ---------------------------------------------------------------------------------------------- Entitaeten
from homeassistant.components.button import ButtonEntity  # noqa: E402
from homeassistant.components.sensor import SensorEntity  # noqa: E402

# key: (Name, Symbol, Wert aus dem Streamer)
SENSOREN = {
    "tastatur": ("Tastatur", "mdi:keyboard-outline", lambda s: s.daten.get("tastatur") or "aus"),
    "status": ("Wiedergabe", "mdi:play-pause", lambda s: s.daten.get("status") or "aus"),
    "app": ("App", "mdi:application-outline", lambda s: s.daten.get("app") or ""),
    "titel": ("Titel", "mdi:format-title", lambda s: s.daten.get("titel") or ""),
    "am_tv": ("Am Fernseher sichtbar", "mdi:television-shimmer", lambda s: s.cec or "unbekannt"),
}


class StreamerSensor(SensorEntity):
    """Wert eines Streamers, z. B. ob seine Bildschirmtastatur offen ist (Ausloeser fuer das Tastatur-Popup)."""

    _attr_should_poll = False

    def __init__(self, cfg: dict, key: str) -> None:
        self._sid, self._key = cfg["id"], key
        name, self._attr_icon, self._wert = SENSOREN[key]
        self._attr_name = f"{cfg['name']} {name}"
        self._attr_unique_id = f"streamer_{cfg['id']}_{key}"
        self.entity_id = f"sensor.esphomeremote_streamer_{cfg['id']}_{key}"

    def _st(self) -> Streamer | None:
        return self.hass.data[DOMAIN + "_streamer"].alle.get(self._sid)

    @property
    def native_value(self) -> str:
        st = self._st()
        return self._wert(st) if st else "unbekannt"

    async def async_added_to_hass(self) -> None:
        if (st := self._st()) is not None:
            self.async_on_remove(st.abonnieren(self.async_write_ha_state))


class StreamerKnopf(ButtonEntity):
    """App beenden (force-stop der laufenden App) bzw. Streamer neu starten - ueber die ADB-Entitaet."""

    def __init__(self, cfg: dict, key: str) -> None:
        self._sid, self._key = cfg["id"], key
        self._attr_name = f"{cfg['name']} " + ("App beenden" if key == "app_beenden" else "neu starten")
        self._attr_icon = "mdi:close-box-outline" if key == "app_beenden" else "mdi:restart"
        self._attr_unique_id = f"streamer_{cfg['id']}_{key}"
        self.entity_id = f"button.esphomeremote_streamer_{cfg['id']}_{key}"

    async def async_press(self) -> None:
        st = self.hass.data[DOMAIN + "_streamer"].alle.get(self._sid)
        if st is None:
            return
        if self._key == "app_beenden":
            await st.app_beenden()
        else:
            await st.adb("reboot")
