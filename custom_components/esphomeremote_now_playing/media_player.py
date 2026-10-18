"""ESPHomeRemote: universeller "Jetzt laeuft"-Player.

Fasst vier Quellen zu EINER media_player-Entitaet zusammen (Rangfolge bei _recompute):
  - Streamer: jede App mit Medien-Sitzung (SmartTube, Netflix, Disney+ ...), aus den Sensoren des Pakets
    esphomeremote_smarttube.yaml (Streamer-App per Webhook) - mit Position und Dauer.
  - Multiroom (Music Assistant, Option "multiroom").
  - Chromecast-Integration (Option "cast"): echte Cast-Sitzung.
  - Fernseher selbst (Option "tv"): Sender bzw. TV-App, oder der HDMI-Eingang.
Die Fernbedienung zeigt diese eine Entitaet; andere Dashboards koennen sie genauso nutzen.

    media_player:
      - platform: esphomeremote_now_playing
        name: "Wohnzimmer Jetzt läuft"
        cast: media_player.tv_wohnzimmer     # leer = nicht benutzt
        multiroom: media_player.multiroom_audio
        tv: media_player.tv_wz
        tv_app: media_player.tv_wohnzimmer_androidsony   # Android-TV-Remote des Fernsehers: laufende App
        tv_cast: media_player.tv_wohnzimmer_sony_chromecast   # Chromecast des Fernsehers: Titel/Cover der App
        ha_url: http://homeassistant.local:8123   # Praefix fuer Bild-URLs (leer = interne HA-Adresse)

Eigene Komponente statt template: - die Template-Integration kennt media_player nicht als Plattform.
"""

from __future__ import annotations

import voluptuous as vol

from homeassistant.components.media_player import (
    PLATFORM_SCHEMA as MEDIA_PLAYER_PLATFORM_SCHEMA,
    MediaPlayerEntity,
    MediaPlayerEntityFeature,
    MediaPlayerState,
)
from homeassistant.const import CONF_NAME, CONF_UNIQUE_ID
from homeassistant.core import Event, EventStateChangedData, HomeAssistant, callback
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.event import async_track_state_change_event
from homeassistant.helpers.network import NoURLAvailableError, get_url
from homeassistant.helpers.typing import ConfigType, DiscoveryInfoType
from homeassistant.util import dt as dt_util

SMARTTUBE_TITLE = "sensor.esphomeremote_smarttube_titel"
SMARTTUBE_ARTIST = "sensor.esphomeremote_smarttube_interpret"
SMARTTUBE_STATE = "sensor.esphomeremote_smarttube_wiedergabestatus"
# Position der aktiven ADB-Quelle (SmartTube ODER Netflix) in Sekunden, -1 = keine
# (packages/esphomeremote_smarttube.yaml, Stand 2026-09-21).
MEDIEN_POSITION = "sensor.esphomeremote_medien_position"
MEDIEN_APP = "sensor.esphomeremote_medien_app"
MEDIEN_DAUER = "sensor.esphomeremote_medien_dauer"
MEDIEN_COVER = "sensor.esphomeremote_medien_cover"
# Standardwerte der drei Quellen (in der YAML ueberschreibbar: cast, multiroom, tv, ha_url)
CAST_ENTITY = "media_player.tv_wohnzimmer"
# Multiroom-Musik ueber Music Assistant (packages/multiroom_audio.yaml, Stand 2026-10-12).
MA_ENTITY = "media_player.multiroom_audio"
TV_ENTITY = "media_player.tv_wz"
# Apps auf dem Fernseher selbst (2026-10-15): die Bravia-Entitaet kennt nur Tuner/HDMI, die App
# kommt von der Android-TV-Remote-Entitaet (app_id), Titel/Cover vom eingebauten Chromecast.
TV_APP_ENTITY = "media_player.tv_wohnzimmer_androidsony"
TV_CAST_ENTITY = "media_player.tv_wohnzimmer_sony_chromecast"
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
SMARTTUBE_COVER = "/local/esphomeremote_smarttube_cover.jpg"
CONF_CAST, CONF_MA, CONF_TV, CONF_HA_URL = "cast", "multiroom", "tv", "ha_url"
CONF_TV_APP, CONF_TV_CAST = "tv_app", "tv_cast"

EMPTY_STATES = ("", "unknown", "unavailable")

PLATFORM_SCHEMA = MEDIA_PLAYER_PLATFORM_SCHEMA.extend(
    {
        vol.Optional(CONF_NAME, default="Wohnzimmer Jetzt läuft"): cv.string,
        vol.Optional(
            CONF_UNIQUE_ID, default="esphomeremote_now_playing"
        ): cv.string,
        # Quellen (leer = nicht benutzt) und die HA-Adresse fuer absolute Bild-URLs (die Remote laedt sie direkt)
        vol.Optional(CONF_CAST, default=CAST_ENTITY): cv.string,
        vol.Optional(CONF_MA, default=MA_ENTITY): cv.string,
        vol.Optional(CONF_TV, default=TV_ENTITY): cv.string,
        vol.Optional(CONF_TV_APP, default=TV_APP_ENTITY): cv.string,
        vol.Optional(CONF_TV_CAST, default=TV_CAST_ENTITY): cv.string,
        vol.Optional(CONF_HA_URL, default=""): cv.string,
    }
)


async def async_setup_platform(
    hass: HomeAssistant,
    config: ConfigType,
    async_add_entities: AddEntitiesCallback,
    discovery_info: DiscoveryInfoType | None = None,
) -> None:
    if not config[CONF_HA_URL]:
        # Die Remote laedt Bilder direkt, braucht also eine im LAN erreichbare Adresse
        try:
            config = {**config, CONF_HA_URL: get_url(hass, allow_external=False)}
        except NoURLAvailableError:
            config = {**config, CONF_HA_URL: "http://homeassistant.local:8123"}
    async_add_entities([NowPlayingMediaPlayer(config)])


def _absolute_url(url: str | None, basis: str) -> str | None:
    if not url:
        return None
    if url.startswith("http://") or url.startswith("https://"):
        return url
    if not url.startswith("/"):
        url = "/" + url
    return basis.rstrip("/") + url


class NowPlayingMediaPlayer(MediaPlayerEntity):
    """Fuehrt SmartTube/Cast/TV zu einer Entitaet zusammen."""

    _attr_should_poll = False
    _attr_supported_features = MediaPlayerEntityFeature(0)
    _attr_icon = "mdi:cast-connected"
    _attr_media_image_remotely_accessible = True

    def __init__(self, config: ConfigType) -> None:
        self._attr_name = config[CONF_NAME]
        self._attr_unique_id = config[CONF_UNIQUE_ID]
        self._cast, self._ma, self._tv = config[CONF_CAST], config[CONF_MA], config[CONF_TV]
        self._tv_app, self._tv_cast = config[CONF_TV_APP], config[CONF_TV_CAST]
        self._ha_url = config[CONF_HA_URL]
        self._attr_state = MediaPlayerState.OFF
        self._tempo = 1.0
        self._quelle = "aus"

    @property
    def extra_state_attributes(self) -> dict[str, float | str]:
        # Wiedergabetempo, mit dem die Fernbedienungen die Position selbst weiterzaehlen.
        # quelle: streamer/multiroom/cast/tv/aus - die Fernbedienung schickt Play/Pause/Weiter
        # bei "multiroom" an Music Assistant statt per Bluetooth an den Streamer.
        return {"tempo": self._tempo, "quelle": self._quelle}

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()

        @callback
        def _on_change(_event: Event[EventStateChangedData]) -> None:
            self._recompute()
            self.async_write_ha_state()

        self.async_on_remove(
            async_track_state_change_event(
                self.hass,
                [
                    SMARTTUBE_TITLE,
                    SMARTTUBE_ARTIST,
                    SMARTTUBE_STATE,
                    MEDIEN_POSITION,
                    MEDIEN_APP,
                    MEDIEN_DAUER,
                    MEDIEN_COVER,
                    *[e for e in (self._cast, self._ma, self._tv, self._tv_app, self._tv_cast) if e],
                ],
                _on_change,
            )
        )
        self._recompute()

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
        """Google Streamer: JEDE App mit Medien-Sitzung (SmartTube, Netflix, Disney+ ...)."""
        title = self._state_of(SMARTTUBE_TITLE) or ""
        artist = self._state_of(SMARTTUBE_ARTIST) or ""
        app = self._state_of(MEDIEN_APP) or ""
        cover = self._state_of(MEDIEN_COVER) or ""
        # app_name ist immer die echte App (Nutzerwunsch 2026-10-14). Liefert die App keinen Titel
        # (Titel = App-Name, z. B. Netflix), steht die Quelle "Google Streamer" als Titel.
        self._attr_media_title = "Google Streamer" if (not title or title == app) else title
        self._attr_media_artist = artist
        self._attr_app_name = app or "Google Streamer"
        self._attr_media_image_url = _absolute_url(SMARTTUBE_COVER, self._ha_url) if cover else None
        pos = self._state_of(MEDIEN_POSITION)
        try:
            if pos not in EMPTY_STATES and int(float(pos)) >= 0:
                self._attr_media_position = int(float(pos))
                self._attr_media_position_updated_at = dt_util.utcnow()
        except (TypeError, ValueError):
            pass
        try:
            self._tempo = float(self._attr_of(MEDIEN_POSITION, "tempo") or 1.0)
        except (TypeError, ValueError):
            self._tempo = 1.0
        dauer = self._state_of(MEDIEN_DAUER)
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
        streamer_title = self._state_of(SMARTTUBE_TITLE) or ""
        streamer = self._state_of(SMARTTUBE_STATE)
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
