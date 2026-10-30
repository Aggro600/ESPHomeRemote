"""Zusammengefasster "Jetzt laeuft"-Player (2026-10-19, vorher custom_components/esphomeremote_now_playing).

Fasst Fernseher, Streamer, Multiroom (Music Assistant) und Chromecast zu EINER media_player-Entitaet zusammen, die
die Fernbedienungen fuer Wiedergabe-Karte, Media-Seite und Musik-Anzeige nutzen (Konfigurator > Verknuepfungen).
Angelegt und eingestellt im Konfigurator (Seite "Wiedergabe", kd.data["jetzt"]); jede Quelle ist optional.
Rangfolge siehe _recompute.
"""

from __future__ import annotations

import re

import voluptuous as vol

from homeassistant.components.media_player import MediaPlayerEntity, MediaPlayerEntityFeature, MediaPlayerState
from homeassistant.core import Event, EventStateChangedData, callback
from homeassistant.helpers.event import async_track_state_change_event
from homeassistant.util import dt as dt_util

# Pakete, die keine "App" sind: Tuner/HDMI-Anzeige und Startbildschirme
TV_KEINE_APP = ("com.sony.dtv.tvx", "com.google.android.tvlauncher", "com.google.android.apps.tv.launcherx",
                "com.sony.dtv.sonysystemservice", "com.android.tv.settings")
APP_NAMEN = {
    "com.google.android.youtube.tv": "YouTube", "com.google.android.youtube.tvmusic": "YouTube Music",
    "com.netflix.ninja": "Netflix", "com.amazon.amazonvideo.livingroom": "Prime Video",
    "com.disney.disneyplus": "Disney+", "de.rtli.tvnow": "RTL+", "org.smarttube.stable": "SmartTube",
    "com.spotify.tv.android": "Spotify", "com.plexapp.android": "Plex", "de.sky.bw": "Sky",
    "com.apple.atve.androidtv.appletv": "Apple TV", "com.wbd.stream": "HBO Max", "org.xbmc.kodi": "Kodi",
    "de.zdf.mediathek": "ZDF", "com.zdf.android.mediathek": "ZDF", "de.swr.avp.ard.tv": "ARD Mediathek",
    "de.prosiebensat1digital.seventv": "Joyn", "org.jellyfin.androidtv": "Jellyfin",
}
EMPTY_STATES = ("", "unknown", "unavailable")
DOMAIN = "esphomeremote_konfig"
QUELLEN = ("tv", "tv_app", "tv_cast", "cast", "multiroom")


def validate_jetzt(liste) -> list[dict]:
    if not isinstance(liste, list):
        raise vol.Invalid("Wiedergabe: Liste erwartet")
    out, ids = [], set()
    for j in liste[:8]:
        jid = str(j.get("id") or "").strip()
        if not re.fullmatch(r"[a-z0-9_]{1,24}", jid) or jid in ids:
            raise vol.Invalid(f"Wiedergabe: Kennung '{jid}' ungültig oder doppelt")
        ids.add(jid)
        st = j.get("streamer")
        st = "" if st in (None, False, True) else str(st)[:24]   # Kennung eines Streamers (streamer.py), "" = keiner
        o = {"id": jid, "name": str(j.get("name") or "Jetzt läuft")[:40], "streamer": st}
        for q in QUELLEN:
            e = str(j.get(q) or "").strip()
            if e and not re.fullmatch(r"media_player\.[a-z0-9_]+", e):
                raise vol.Invalid(f"Wiedergabe {o['name']}: '{e}' ist kein Media Player")
            o[q] = e
        out.append(o)
    return out


def _absolute_url(url: str | None, basis: str) -> str | None:
    if not url:
        return None
    if url.startswith("http://") or url.startswith("https://"):
        return url
    if not url.startswith("/"):
        url = "/" + url
    return basis.rstrip("/") + url


class JetztPlayer(MediaPlayerEntity):
    """Fuehrt Streamer/Multiroom/Cast/TV zu einer Entitaet zusammen."""

    _attr_should_poll = False
    # Befehle (2026-10-30): an die gerade laufende Quelle weitergereicht (_ziel) - vorher keine, die Widgets der
    # Remote liefen ins Leere ("does not support action media_play_pause")
    _attr_supported_features = (MediaPlayerEntityFeature.PLAY | MediaPlayerEntityFeature.PAUSE | MediaPlayerEntityFeature.STOP
                                | MediaPlayerEntityFeature.NEXT_TRACK | MediaPlayerEntityFeature.PREVIOUS_TRACK
                                | MediaPlayerEntityFeature.VOLUME_STEP | MediaPlayerEntityFeature.VOLUME_MUTE)
    _attr_icon = "mdi:cast-connected"
    _attr_media_image_remotely_accessible = True

    def __init__(self, kd, j: dict) -> None:
        self._kd = kd
        self._attr_name = j["name"]
        self._attr_unique_id = f"jetzt_{j['id']}"
        self._cast, self._ma, self._tv = j["cast"], j["multiroom"], j["tv"]
        self._tv_app, self._tv_cast = j["tv_app"], j["tv_cast"]
        self._streamer_id = j["streamer"]
        self._streamer = None
        self._ha_url = ""
        self._attr_state = MediaPlayerState.OFF
        self._tempo = 1.0
        self._quelle = "aus"

    @property
    def extra_state_attributes(self) -> dict[str, float | str]:
        # Wiedergabetempo, mit dem die Fernbedienungen die Position selbst weiterzaehlen.
        # quelle: streamer/multiroom/cast/tv/aus - die Fernbedienung schickt Play/Pause/Weiter
        # bei "multiroom" an den Multiroom-Player statt per Bluetooth an den Streamer.
        return {"tempo": self._tempo, "quelle": self._quelle}

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        # Die Remote laedt Bilder direkt, braucht also eine im LAN erreichbare Adresse
        self._ha_url = "http://" + (self._kd.basis() or "homeassistant.local:8123")

        @callback
        def _on_change(_event: Event[EventStateChangedData]) -> None:
            self._recompute()
            self.async_write_ha_state()

        quellen = [e for e in (self._cast, self._ma, self._tv, self._tv_app, self._tv_cast) if e]
        if quellen:
            self.async_on_remove(async_track_state_change_event(self.hass, quellen, _on_change))
        alle = self.hass.data.get(DOMAIN + "_streamer")
        st = alle.alle.get(self._streamer_id) if (alle and self._streamer_id) else None
        self._streamer = st
        if st is not None:
            @callback
            def _streamer_neu() -> None:
                self._recompute()
                self.async_write_ha_state()
            self.async_on_remove(st.abonnieren(_streamer_neu))
        self._recompute()

    def _ziel(self, lautstaerke: bool = False) -> str | None:
        """Wohin ein Befehl geht: die laufende Quelle (Streamer ueber seine ADB-Entitaet, Multiroom, Chromecast, TV).
        Lautstaerke: am Fernseher, wenn einer eingetragen ist (dort haengt der Ton), sonst wie die Wiedergabe."""
        streamer_adb = self._streamer.cfg.get("adb") if self._streamer is not None else ""
        q = self._quelle
        ziel = {"streamer": streamer_adb, "multiroom": self._ma, "cast": self._cast or self._tv_cast,
                "tv": self._tv_app or self._tv}.get(q) or ""
        if lautstaerke and q != "multiroom" and self._tv:
            return self._tv
        return ziel or self._tv or None

    async def _weiter(self, dienst: str, lautstaerke: bool = False, **daten) -> None:
        ziel = self._ziel(lautstaerke)
        if not ziel:
            return
        await self.hass.services.async_call("media_player", dienst, {"entity_id": ziel, **daten}, blocking=False)

    async def async_media_play_pause(self) -> None:
        await self._weiter("media_play_pause")

    async def async_media_play(self) -> None:
        await self._weiter("media_play")

    async def async_media_pause(self) -> None:
        await self._weiter("media_pause")

    async def async_media_stop(self) -> None:
        await self._weiter("media_stop")

    async def async_media_next_track(self) -> None:
        await self._weiter("media_next_track")

    async def async_media_previous_track(self) -> None:
        await self._weiter("media_previous_track")

    async def async_volume_up(self) -> None:
        await self._weiter("volume_up", True)

    async def async_volume_down(self) -> None:
        await self._weiter("volume_down", True)

    async def async_mute_volume(self, mute: bool) -> None:
        await self._weiter("volume_mute", True, is_volume_muted=mute)

    def _st(self, k: str) -> str | None:
        """Daten des Streamers (streamer.py) als Text wie frueher die Sensoren; None = kein Streamer/keine Daten."""
        if self._streamer is None or not self._streamer.daten:
            return None
        v = self._streamer.daten.get(k)
        return None if v is None else str(v)

    def _state_of(self, entity_id: str) -> str | None:
        st = self.hass.states.get(entity_id) if entity_id else None
        return st.state if st is not None else None

    def _attr_of(self, entity_id: str, attr: str) -> str | None:
        st = self.hass.states.get(entity_id) if entity_id else None
        if st is None:
            return None
        return st.attributes.get(attr)

    def _position_from_entity(self, entity_id: str, playing: bool) -> None:
        """Position/Dauer aus den Standard-Attributen einer anderen Entitaet uebernehmen.

        media_position gilt zum Zeitpunkt media_position_updated_at; laeuft die
        Wiedergabe, wird bis jetzt hochgerechnet. Die Fernbedienung zaehlt danach selbst
        weiter, wir muessen also nur bei jeder Aenderung einen aktuellen Stand liefern.
        """
        pos = self._attr_of(entity_id, "media_position")
        if pos is None:
            return
        try:
            pos = float(pos)
        except (TypeError, ValueError):
            return
        stamp = self._attr_of(entity_id, "media_position_updated_at")
        if playing and stamp is not None:
            try:
                pos += max((dt_util.utcnow() - stamp).total_seconds(), 0.0)
            except TypeError:
                pass
        self._attr_media_position = int(pos)
        self._attr_media_position_updated_at = dt_util.utcnow()
        dur = self._attr_of(entity_id, "media_duration")
        if dur:
            try:
                self._attr_media_duration = int(float(dur))
            except (TypeError, ValueError):
                pass

    def _clear(self) -> None:
        # Leer heisst "" bzw. -1/0, NICHT None: Home Assistant laesst Attribute mit None weg, und ESPHome
        # bekommt dann kein Update - die Fernbedienung behielte den alten Wert (Interpret von SmartTube bei
        # Prime Video, alte Position bei einer App ohne Position). Stand 2026-09-22.
        self._attr_media_title = ""
        self._attr_media_artist = ""
        self._attr_app_name = ""
        self._attr_media_image_url = None
        self._attr_media_position = -1
        self._attr_media_position_updated_at = None
        self._attr_media_duration = 0
        self._tempo = 1.0

    def _fill_streamer(self, playback: str | None) -> None:
        """Streamer: JEDE App mit Medien-Sitzung (SmartTube, Netflix, Disney+ ...)."""
        title = self._st("titel") or ""
        artist = self._st("interpret") or ""
        app = self._st("app") or ""
        cover = self._st("cover") or ""
        # app_name ist immer die echte App (Nutzerwunsch 2026-10-14). Liefert die App keinen Titel
        # (Titel = App-Name, z. B. Netflix), steht der Name des Streamers (Konfigurator) als Titel.
        quelle = self._streamer.cfg["name"] if self._streamer is not None else "Streamer"
        self._attr_media_title = quelle if (not title or title == app) else title
        self._attr_media_artist = artist
        self._attr_app_name = app or quelle
        self._attr_media_image_url = _absolute_url(cover, self._ha_url) if cover not in EMPTY_STATES else None
        pos = self._st("position")
        try:
            if pos not in EMPTY_STATES and int(float(pos)) >= 0:
                self._attr_media_position = int(float(pos))
                self._attr_media_position_updated_at = dt_util.utcnow()
        except (TypeError, ValueError):
            pass
        try:
            self._tempo = float(self._st("tempo") or 1.0)
        except (TypeError, ValueError):
            self._tempo = 1.0
        dauer = self._st("dauer")
        try:
            if dauer not in EMPTY_STATES and int(float(dauer)) > 0:
                self._attr_media_duration = int(float(dauer))
        except (TypeError, ValueError):
            pass
        self._attr_state = (
            MediaPlayerState.PLAYING if playback == "playing" else MediaPlayerState.PAUSED
        )

    def _fill_cast(self, cast_state: str) -> None:
        self._attr_media_title = self._attr_of(self._cast, "media_title")
        self._attr_media_artist = self._attr_of(
            self._cast, "media_artist"
        ) or self._attr_of(self._cast, "app_name")
        self._attr_app_name = self._attr_of(self._cast, "app_name") or "Chromecast"
        self._attr_media_image_url = _absolute_url(self._attr_of(self._cast, "entity_picture"), self._ha_url)
        self._attr_state = (
            MediaPlayerState.PLAYING if cast_state == "playing" else MediaPlayerState.PAUSED
        )
        self._position_from_entity(self._cast, cast_state == "playing")

    def _fill_ma(self, ma_state: str) -> None:
        """Music Assistant (Multiroom-Gruppe): Titel, Interpret, Cover, Position."""
        self._attr_media_title = self._attr_of(self._ma, "media_title") or ""
        self._attr_media_artist = self._attr_of(self._ma, "media_artist") or ""
        self._attr_app_name = "Multiroom"
        self._attr_media_image_url = _absolute_url(self._attr_of(self._ma, "entity_picture"), self._ha_url)
        self._attr_state = (
            MediaPlayerState.PLAYING if ma_state == "playing" else MediaPlayerState.PAUSED
        )
        self._position_from_entity(self._ma, ma_state == "playing")

    def _tv_app_id(self) -> str:
        """Paket der App, die gerade auf dem Fernseher selbst laeuft ("" = Tuner/HDMI/Startbildschirm)."""
        if self._state_of(self._tv_app) in (None, *EMPTY_STATES, "off", "standby"):
            return ""
        app = str(self._attr_of(self._tv_app, "app_id") or "")
        return "" if app in TV_KEINE_APP else app

    def _fill_tv(self, tv_state: str, extern: bool) -> None:
        """Fernseher selbst: App auf dem TV, Tuner-Sender bzw. gewaehlter HDMI-Eingang."""
        source = self._attr_of(self._tv, "source")
        app = "" if extern else self._tv_app_id()
        if app:
            name = APP_NAMEN.get(app) or app.split(".")[-1].capitalize()
            self._attr_app_name = name
            cast_st = self._state_of(self._tv_cast)
            if cast_st in ("playing", "paused") and self._attr_of(self._tv_cast, "media_title"):
                # Die App meldet eine Medien-Sitzung am eingebauten Chromecast (YouTube, Netflix ...)
                self._attr_media_title = self._attr_of(self._tv_cast, "media_title") or ""
                self._attr_media_artist = self._attr_of(self._tv_cast, "media_artist") or ""
                self._attr_media_image_url = _absolute_url(self._attr_of(self._tv_cast, "entity_picture"), self._ha_url)
                self._attr_state = MediaPlayerState.PLAYING if cast_st == "playing" else MediaPlayerState.PAUSED
                self._position_from_entity(self._tv_cast, cast_st == "playing")
            else:
                # Ohne Titel steht wie beim Streamer die Quelle als Titel
                self._attr_media_title = "Fernseher"
                self._attr_state = MediaPlayerState.IDLE
            return
        self._attr_media_title = (
            self._attr_of(self._tv, "media_title") or source or "Fernseher"
        )
        channel = self._attr_of(self._tv, "media_channel")
        self._attr_media_artist = channel or ("Fernseher" if source is None else source)
        self._attr_app_name = "Fernseher"
        self._attr_media_image_url = _absolute_url(self._attr_of(self._tv, "entity_picture"), self._ha_url)
        # Externer Eingang (HDMI): wir wissen nicht, was dort laeuft -> nur "an".
        if extern:
            self._attr_state = MediaPlayerState.IDLE
        else:
            self._attr_state = (
                MediaPlayerState.PAUSED if tv_state == "paused" else MediaPlayerState.PLAYING
            )
            self._position_from_entity(self._tv, tv_state == "playing")

    def _recompute(self) -> None:
        """Waehlt die Quelle (Stand 2026-10-15):

        0. App auf dem Fernseher selbst (YouTube, Netflix ... am TV, nicht am Streamer)
        1. Streamer: irgendeine App SPIELT (nicht nur SmartTube/Netflix)
        1b. Multiroom (Music Assistant): spielt (2026-10-12)
        2. Chromecast: spielt
        3. Fernseher: an und NICHT auf einem HDMI-Eingang (Tuner, TV-App) - dort laeuft
           dann etwas anderes als am Streamer; Sender/App-Name kommt aus dem Fernseher
        4. Streamer: pausiert
        4b. Multiroom: pausiert
        5. Chromecast: pausiert
        6. Fernseher: an (HDMI-Eingang, Inhalt unbekannt)
        7. sonst: aus
        """
        self._clear()
        streamer_title = self._st("titel") or ""
        streamer = self._st("status")
        streamer_aktiv = streamer_title not in EMPTY_STATES and streamer in ("playing", "paused")
        cast_state = self._state_of(self._cast)
        tv_state = self._state_of(self._tv)
        tv_an = tv_state in ("on", "playing", "paused")
        content_id = self._attr_of(self._tv, "media_content_id") or ""
        tv_extern = str(content_id).startswith("extInput:")

        ma_state = self._state_of(self._ma)

        if tv_an and not tv_extern and self._tv_app_id():
            # App auf dem Fernseher selbst: der Streamer hinter HDMI ist dann nicht zu sehen
            self._fill_tv(tv_state, False)
            self._quelle = "tv"
        elif streamer_aktiv and streamer == "playing":
            self._fill_streamer(streamer)
            self._quelle = "streamer"
        elif ma_state == "playing":
            self._fill_ma(ma_state)
            self._quelle = "multiroom"
        elif cast_state == "playing":
            self._fill_cast(cast_state)
            self._quelle = "cast"
        elif tv_an and not tv_extern:
            self._fill_tv(tv_state, False)
            self._quelle = "tv"
        elif streamer_aktiv:
            self._fill_streamer(streamer)
            self._quelle = "streamer"
        elif ma_state == "paused":
            self._fill_ma(ma_state)
            self._quelle = "multiroom"
        elif cast_state == "paused":
            self._fill_cast(cast_state)
            self._quelle = "cast"
        elif tv_an:
            self._fill_tv(tv_state, True)
            self._quelle = "tv"
        else:
            self._attr_state = MediaPlayerState.OFF
            self._quelle = "aus"


# ---------------------------------------------------------------------------------------------- Websocket
from homeassistant.components import websocket_api  # noqa: E402



@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/jetzt_get"})
@callback
def ws_jetzt_get(hass, connection, msg) -> None:
    v = hass.data[DOMAIN + "_dock"]
    liste = v.kd.data.get("jetzt") or []
    ents = {j["id"]: getattr(v.ents.get(f"jetzt_{j['id']}"), "entity_id", None) for j in liste}
    connection.send_result(msg["id"], {"jetzt": liste, "entitaeten": ents,
                                       "streamer": [{"id": x["id"], "name": x["name"]} for x in v.kd.data.get("streamer") or []]})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/jetzt_save", vol.Required("jetzt"): list})
@websocket_api.async_response
async def ws_jetzt_save(hass, connection, msg) -> None:
    v = hass.data[DOMAIN + "_dock"]
    try:
        v.kd.data["jetzt"] = validate_jetzt(msg["jetzt"])
    except vol.Invalid as e:
        connection.send_error(msg["id"], "ungueltig", str(e))
        return
    await v.kd.store.async_save(v.kd.data)
    await v.sync()
    ws_jetzt_get(hass, connection, msg)
