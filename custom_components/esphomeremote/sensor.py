"""Sensoren der Streamer (streamer.py), angelegt ueber den Verwalter (dock.py)."""

from __future__ import annotations

from .dock import DOMAIN


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is not None:
        hass.data[DOMAIN + "_dock"].plattform("sensor", add)
