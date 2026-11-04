"""Eigene Zustands-Entitaeten der Integration (2026-11-04, HACS-Pruefung).

Bis dahin schrieb die Integration einige Zustaende nur mit hass.states.async_set (ohne Entitaet dahinter): kein
Registry-Eintrag, nicht umbenennbar, nach dem Neustart weg, bis sie wieder geschrieben wurden. Jetzt ist jeder davon
ein echter SensorEntity / BinarySensorEntity mit unique_id ("zustand:<entity_id>") und fester entity_id - die
Firmware abonniert diese Namen und liest sie weiter wie bisher:

    sensor.esphomeremote_config_<remote>          Hash der Tasten-Konfiguration (__init__.py announce)
    sensor.esphomeremote_konfig_<remote>          dasselbe fuer alte Firmware (compat.py)
    sensor.esphomeremote_tv_input, binary_sensor.esphomeremote_camera_tv, sensor.esphomeremote_camera_size (features.py)
    sensor.esphomeremote_timer_abgelaufen         (timer.py)
    sensor.esphomeremote_spiegel_<remote>_<rolle> (mirror.py)
    sensor./binary_sensor.esphomeremote_<remote>_<wert>   Werte der Remote ueber das Dock (dockstatus.py)

Aufrufer: setzen(entity_id, zustand, attribute) und entfernen(entity_id). "friendly_name", "icon" und
"entity_picture" werden zu Name/Symbol/Bild der Entitaet; "_einheit" und "_klasse" (nur fuer ausdruecklich
typisierte Werte) zu Einheit und device_class; alles andere sind Attribute. Bis die Plattformen geladen sind,
merkt sich setzen() den letzten Stand und legt die Entitaet danach an.
"""
from __future__ import annotations

import logging
from typing import Any

from homeassistant.components.binary_sensor import BinarySensorEntity
from homeassistant.components.sensor import SensorEntity
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import discovery, entity_registry as er

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
UID = "zustand:"
PLATTFORMEN = ("sensor", "binary_sensor")
FREI = ("unavailable", "unknown")


class _Basis:
    _attr_should_poll = False

    def __init__(self, eid: str, zustand: Any, attrs: dict | None) -> None:
        self.entity_id = eid
        self._attr_unique_id = UID + eid
        self.werte(zustand, attrs)

    def werte(self, zustand: Any, attrs: dict | None) -> None:
        a = dict(attrs or {})
        self._attr_name = str(a.pop("friendly_name", "") or "") or self.entity_id.split(".", 1)[1]
        self._attr_icon = a.pop("icon", None)
        self._attr_entity_picture = a.pop("entity_picture", None)
        self._einheit = a.pop("_einheit", None)
        self._klasse = a.pop("_klasse", None)
        a.pop("restored", None)
        self._attr_extra_state_attributes = a
        self._zustand = zustand
        self._attr_available = zustand != "unavailable"


class ZustandSensor(_Basis, SensorEntity):
    @property
    def native_value(self):
        return None if self._zustand in FREI else self._zustand

    @property
    def native_unit_of_measurement(self):
        return self._einheit

    @property
    def device_class(self):
        return self._klasse


class ZustandBinaer(_Basis, BinarySensorEntity):
    @property
    def is_on(self):
        return None if self._zustand in FREI else str(self._zustand) == "on"

    @property
    def device_class(self):
        return self._klasse


class Zustaende:
    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self.add: dict[str, Any] = {}
        self.ents: dict[str, _Basis] = {}
        self.offen: dict[str, tuple] = {}
        self.stand: dict[str, tuple] = {}   # entity_id -> (zustand, attrs), auch fuer deaktivierte Entitaeten

    @callback
    def plattform(self, p: str, add) -> None:
        self.add[p] = add
        for eid, (z, a) in list(self.offen.items()):
            if eid.startswith(p + "."):
                del self.offen[eid]
                self.setzen(eid, z, a)

    @callback
    def setzen(self, eid: str, zustand: Any, attrs: dict | None = None) -> None:
        self.stand[eid] = (zustand, dict(attrs or {}))
        e = self.ents.get(eid)
        if e is not None:
            e.werte(zustand, attrs)
            if e.hass is not None and e.platform is not None:
                e.async_write_ha_state()
            return
        p = eid.split(".", 1)[0]
        if p not in self.add:
            self.offen[eid] = (zustand, attrs)
            return
        e = (ZustandBinaer if p == "binary_sensor" else ZustandSensor)(eid, zustand, attrs)
        self.ents[eid] = e
        self.add[p]([e])

    def wert(self, eid: str) -> tuple | None:
        """Zuletzt gesetzter (zustand, attrs) - auch vor dem Laden der Plattform und fuer deaktivierte Entitaeten."""
        return self.stand.get(eid)

    @callback
    def aus(self, eid: str) -> None:
        """Funktion gerade nicht nutzbar: Entitaet (falls schon angelegt) auf "nicht verfuegbar", Name bleibt."""
        if eid in self.ents or eid in self.offen:
            self.setzen(eid, "unavailable", (self.stand.get(eid) or (None, {}))[1])

    @callback
    def entfernen(self, eid: str) -> None:
        """Entitaet samt Registry-Eintrag entfernen (Remote geloescht, Funktion aus)."""
        self.offen.pop(eid, None)
        self.stand.pop(eid, None)
        e = self.ents.pop(eid, None)
        reg = er.async_get(self.hass)
        rid = reg.async_get_entity_id(eid.split(".", 1)[0], DOMAIN, UID + eid)
        if rid:
            reg.async_remove(rid)   # entfernt auch die laufende Entitaet
        elif e is not None and e.hass is not None:
            self.hass.async_create_task(e.async_remove(force_remove=True))


def holen(hass: HomeAssistant) -> Zustaende:
    z = hass.data.get(DOMAIN + "_zustaende")
    if z is None:
        z = hass.data[DOMAIN + "_zustaende"] = Zustaende(hass)
        for p in PLATTFORMEN:
            hass.async_create_task(discovery.async_load_platform(hass, p, DOMAIN, {"zustand": True}, {}))
    return z


def remote_wert(hass: HomeAssistant, eid: str):
    """Zustand einer ESPHome-Entitaet der Remote; ist sie nicht verfuegbar, der zuletzt uebers Dock gemeldete Wert
    (eigene Entitaet <domain>.esphomeremote_<remote>_<wert>, dockstatus.py)."""
    st = hass.states.get(eid)
    if st is not None and st.state not in FREI:
        return st
    dom, obj = eid.split(".", 1)
    return hass.states.get(f"{dom}.esphomeremote_{obj}") or st
