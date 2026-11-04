"""Sensoren: die der Streamer (streamer.py, angelegt ueber den Verwalter dock.py) und die Zustands-Sensoren der
Integration (zustaende.py)."""

from __future__ import annotations

from .dock import DOMAIN


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is None:
        return
    if discovery_info.get("zustand"):
        hass.data[DOMAIN + "_zustaende"].plattform("sensor", add)
    else:
        hass.data[DOMAIN + "_dock"].plattform("sensor", add)
