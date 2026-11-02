"""Remote-Entitaet je IR-Geraet: remote.send_command mit den Befehlsnamen (wie bei Harmony: VolumeUp, InputNext ...)."""

from __future__ import annotations

import asyncio
from collections.abc import Iterable
from typing import Any

from homeassistant.components.remote import ATTR_DELAY_SECS, ATTR_NUM_REPEATS, RemoteEntity
from homeassistant.helpers.restore_state import RestoreEntity

from .dock import DOMAIN, IrBasis, Verwalter, befehle


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is not None:
        hass.data[DOMAIN + "_dock"].plattform("remote", add)


class IrRemote(IrBasis, RemoteEntity, RestoreEntity):
    _attr_assumed_state = True

    def __init__(self, v: Verwalter, g: dict) -> None:
        self._ir_init(v, g)
        self._attr_name = g["name"]
        self._attr_icon = "mdi:remote-tv"
        self._attr_unique_id = f"ir_{g['id']}_remote"
        self.entity_id = f"remote.ir_{g['id']}"

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        if self.z["an"] is None and (alt := await self.async_get_last_state()) is not None and alt.state in ("on", "off"):
            self.z["an"] = alt.state == "on"

    @property
    def is_on(self) -> bool | None:
        return self.z["an"]

    @property
    def extra_state_attributes(self) -> dict:
        return {"befehle": list(befehle(self.g)), "sender": self._sender}

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self.schalten(True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self.schalten(False)

    async def async_send_command(self, command: Iterable[str], **kwargs: Any) -> None:
        pause = float(kwargs.get(ATTR_DELAY_SECS) or 0.4)
        erst = True
        for _ in range(int(kwargs.get(ATTR_NUM_REPEATS) or 1)):
            for c in command:
                if not erst:
                    await asyncio.sleep(pause)
                erst = False
                await self.senden(c)
