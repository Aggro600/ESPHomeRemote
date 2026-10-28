"""Aktivitaet als Auswahl (Dropdown): je Remote ein select mit allen Aktivitaeten + "Aus" (2026-10-18, siehe dock.py).

Gleiche Logik wie die Aktivitaets-Schalter (switch.py): Zustand aus dem Sensor "Aktivität" der Remote, Setzen ueber
den ESPHome-Dienst <remote>_aktivitaet_setzen; schlaeft die Remote, wird beim naechsten Verbinden zugestellt.
"""

from __future__ import annotations

from typing import Any

from homeassistant.components.select import SelectEntity

from .dock import DOMAIN, Verwalter
from .switch import AktBasis, _remote_name

AUS = "Aus"


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is not None:
        hass.data[DOMAIN + "_dock"].plattform("select", add)


class AktAuswahl(AktBasis, SelectEntity):
    _attr_icon = "mdi:remote-tv"

    def __init__(self, v: Verwalter, uid: str, dev: str, namen: list[str]) -> None:
        super().__init__(v, uid, dev, -1)
        self._namen = namen
        self._attr_name = f"{_remote_name(dev)} Aktivität"
        self._attr_options = [*namen, AUS]

    @property
    def current_option(self) -> str | None:
        nr = self._wunsch if self._wunsch is not None else self._laufend()
        if nr is None:
            return None
        return self._namen[nr] if 0 <= nr < len(self._namen) else AUS

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        return {"remote": self.dev, "wartet_auf_remote": self._wunsch is not None}

    async def async_select_option(self, option: str) -> None:
        await self._setzen(self._namen.index(option) if option in self._namen else -1)
