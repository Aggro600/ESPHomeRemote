"""Ein Button je Befehl eines IR-Geraets (Dockingstation-Tab, siehe dock.py)."""

from __future__ import annotations

import re

from homeassistant.components.button import ButtonEntity
from homeassistant.helpers.restore_state import RestoreEntity

from .dock import DOMAIN, NAMEN, IrBasis, Verwalter


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is not None:
        hass.data[DOMAIN + "_dock"].plattform("button", add)


class IrButton(IrBasis, ButtonEntity, RestoreEntity):
    def __init__(self, v: Verwalter, g: dict, n: str) -> None:
        self._ir_init(v, g)
        self._n = n
        name, icon = NAMEN.get(n, (n, "mdi:remote"))
        self._attr_name = f"{g['name']} {name}"
        self._attr_icon = icon
        self._attr_unique_id = f"ir_{g['id']}_b_{n}"
        self.entity_id = f"button.ir_{g['id']}_{re.sub(r'[^a-z0-9]+', '_', n.lower()).strip('_')}"
        self._attr_extra_state_attributes = {"befehl": n}

    async def async_press(self) -> None:
        await self.senden(self._n)
