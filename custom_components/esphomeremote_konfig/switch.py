"""Aktivitaets-Schalter: je Remote und Aktivitaet ein switch (siehe dock.py)."""

from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.const import STATE_UNAVAILABLE
from homeassistant.core import Event, callback
from homeassistant.exceptions import HomeAssistantError

from .dock import DOMAIN, Verwalter, zustand_unbekannt


async def async_setup_platform(hass, config, add, discovery_info=None) -> None:
    if discovery_info is not None:
        hass.data[DOMAIN + "_dock"].plattform("switch", add)


def _remote_name(dev: str) -> str:
    return " ".join(w.upper() if len(w) <= 2 else w.capitalize() for w in dev.split("-"))


class AktBasis:
    """Gemeinsam fuer Schalter und Auswahl (select.py): Zustand lesen, Aktivitaet setzen/zustellen."""
    _attr_should_poll = False
    _attr_icon = "mdi:remote-tv"

    def __init__(self, v: Verwalter, uid: str, dev: str, i: int) -> None:
        self.v = v
        self.dev = dev
        self.i = i
        self.slug = dev.replace("-", "_").lower()
        self._attr_unique_id = uid
        acts = (v.kd.entry(dev).get("config") or {}).get("activities") or []
        self._akt = acts[i].get("name") if i < len(acts) else f"Aktivität {i + 1}"
        self._attr_name = f"{_remote_name(dev)} Aktivität {self._akt}"
        self._wunsch: int | None = None   # noch nicht zugestellt (Remote nicht erreichbar)
        self._sensor_id: str | None = None

    def umbenennen(self, akt: str) -> None:
        self._akt = akt
        self._attr_name = f"{_remote_name(self.dev)} Aktivität {akt}"
        if self.hass:
            self.async_write_ha_state()

    @property
    def _sensor(self) -> str | None:
        # ESPHome-Sensor "Aktivität" der Remote (sensor.remote_wz_wohnzimmer_aktivitat) - einmal suchen, dann merken
        if self._sensor_id and self.hass.states.get(self._sensor_id) is not None:
            return self._sensor_id
        self._sensor_id = next((st.entity_id for st in self.hass.states.async_all("sensor")
                                if st.entity_id.startswith(f"sensor.{self.slug}_aktivit")), None)
        return self._sensor_id

    def _laufend(self) -> int | None:
        s = self._sensor
        st = self.hass.states.get(s) if s else None
        if zustand_unbekannt(st):
            return None
        try:
            return int(float(st.state))
        except ValueError:
            return None

    async def async_added_to_hass(self) -> None:
        @callback
        def neu(ev: Event) -> None:
            if self._wunsch is not None and not zustand_unbekannt(ev.data.get("new_state")):
                self.hass.async_create_task(self._zustellen())
            self.async_write_ha_state()

        @callback
        def passt(d) -> bool:
            return d["entity_id"].startswith(f"sensor.{self.slug}_aktivit")

        # Der Sensor ist beim Start evtl. noch nicht da: nach dem Namensmuster filtern statt fester ID
        self.async_on_remove(self.hass.bus.async_listen("state_changed", neu, event_filter=passt))

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        return {"remote": self.dev, "aktivitaet": self._akt, "nr": self.i,
                "wartet_auf_remote": self._wunsch is not None}

    async def _zustellen(self) -> None:
        nr, self._wunsch = self._wunsch, None
        if nr is None:
            return
        try:
            await self.hass.services.async_call("esphome", f"{self.slug}_aktivitaet_setzen", {"nr": nr}, blocking=True)
        except Exception as err:   # noqa: BLE001 - beim naechsten Verbinden erneut
            self._wunsch = nr
            raise HomeAssistantError(f"{self.dev}: Aktivität nicht gesetzt ({err})") from err
        finally:
            self.async_write_ha_state()

    async def _setzen(self, nr: int) -> None:
        if not self.hass.services.has_service("esphome", f"{self.slug}_aktivitaet_setzen"):
            raise HomeAssistantError(f"{self.dev}: Firmware kennt 'aktivitaet_setzen' noch nicht (neu flashen)")
        self._wunsch = nr
        s = self._sensor
        st = self.hass.states.get(s) if s else None
        if st is None or st.state == STATE_UNAVAILABLE:
            self.async_write_ha_state()   # Remote schlaeft: beim naechsten Verbinden
            return
        await self._zustellen()



class AktSchalter(AktBasis, SwitchEntity):
    @property
    def is_on(self) -> bool | None:
        if self._wunsch is not None:
            return self._wunsch == self.i
        a = self._laufend()
        return None if a is None else a == self.i

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self._setzen(self.i)

    async def async_turn_off(self, **kwargs: Any) -> None:
        if self.is_on:
            await self._setzen(-1)
