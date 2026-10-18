"""Media-Player je IR-Geraet (angenommener Zustand): Ein/Aus, Lautstaerke in Schritten, Stumm, Quelle, Wiedergabe."""

from __future__ import annotations

from homeassistant.components.media_player import (
    MediaPlayerDeviceClass,
    MediaPlayerEntity,
    MediaPlayerEntityFeature as F,
    MediaPlayerState,
)
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers.restore_state import RestoreEntity

from .dock import DOMAIN, VORLAGEN, IrBasis, Verwalter, befehle, quellen


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is not None:
        hass.data[DOMAIN + "_dock"].plattform("media_player", add)


# Funktion -> noetige Befehle (alle muessen vorhanden sein)
_FUNKTIONEN = (
    (F.VOLUME_STEP, ("VolumeUp", "VolumeDown")), (F.VOLUME_MUTE, ("Mute",)),
    (F.PLAY, ("Play",)), (F.PAUSE, ("Pause",)), (F.STOP, ("Stop",)),
    (F.NEXT_TRACK, ("ChannelUp",)), (F.PREVIOUS_TRACK, ("ChannelDown",)),
)


class IrMediaPlayer(IrBasis, MediaPlayerEntity, RestoreEntity):
    _attr_assumed_state = True

    def __init__(self, v: Verwalter, g: dict) -> None:
        self._ir_init(v, g)
        self._attr_name = g["name"]
        self._attr_unique_id = f"ir_{g['id']}_mp"
        self.entity_id = f"media_player.ir_{g['id']}"
        tv = VORLAGEN.get(g["vorlage"], VORLAGEN["leer"])["art"] == "tv"
        self._attr_device_class = MediaPlayerDeviceClass.TV if tv else MediaPlayerDeviceClass.SPEAKER
        bf = befehle(g)
        f = F(0)
        if bf.keys() & {"PowerOn", "PowerToggle"}:
            f |= F.TURN_ON
        if bf.keys() & {"PowerOff", "PowerToggle"}:
            f |= F.TURN_OFF
        for flag, noetig in _FUNKTIONEN:
            if all(n in bf for n in noetig):
                f |= flag
        if "SkipForward" in bf and not f & F.NEXT_TRACK:
            f |= F.NEXT_TRACK
        if "SkipBack" in bf and not f & F.PREVIOUS_TRACK:
            f |= F.PREVIOUS_TRACK
        self._q = quellen(g)
        if self._q:
            f |= F.SELECT_SOURCE
            self._attr_source_list = list(self._q)
        self._attr_supported_features = f

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        if (alt := await self.async_get_last_state()) is not None:
            if self.z["an"] is None and alt.state in (MediaPlayerState.ON, MediaPlayerState.OFF):
                self.z["an"] = alt.state == MediaPlayerState.ON
            if self.z["quelle"] is None and alt.attributes.get("source") in self._q:
                self.z["quelle"] = alt.attributes["source"]

    @property
    def state(self) -> MediaPlayerState | None:
        if self.z["an"] is None:
            return None
        return MediaPlayerState.ON if self.z["an"] else MediaPlayerState.OFF

    @property
    def source(self) -> str | None:
        return self.z["quelle"]

    async def async_turn_on(self) -> None:
        await self.schalten(True)

    async def async_turn_off(self) -> None:
        await self.schalten(False)

    async def async_volume_up(self) -> None:
        await self.senden("VolumeUp")

    async def async_volume_down(self) -> None:
        await self.senden("VolumeDown")

    async def async_mute_volume(self, mute: bool) -> None:
        await self.senden("Mute")

    async def async_media_play(self) -> None:
        await self.senden("Play")

    async def async_media_pause(self) -> None:
        await self.senden("Pause")

    async def async_media_stop(self) -> None:
        await self.senden("Stop")

    async def async_media_next_track(self) -> None:
        await self.senden("ChannelUp" if "ChannelUp" in befehle(self.g) else "SkipForward")

    async def async_media_previous_track(self) -> None:
        await self.senden("ChannelDown" if "ChannelDown" in befehle(self.g) else "SkipBack")

    async def async_select_source(self, source: str) -> None:
        if source not in self._q:
            raise ServiceValidationError(f"Unbekannte Quelle '{source}'. Möglich: {', '.join(self._q)}")
        await self.senden(self._q[source])
