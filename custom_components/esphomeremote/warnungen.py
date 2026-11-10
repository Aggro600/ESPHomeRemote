"""Warnungen in Home Assistant (2026-11-10, Nutzer: "nur Warnung in HA, nicht Handy").

Unter Einstellungen -> Reparaturen erscheint je Remote:
  - battery_low_<remote>  Akku unter AKKU_WARN %, solange sie nicht laedt (weg ab AKKU_OK % oder am Kabel)
  - not_seen_<remote>     seit STILL_STUNDEN nichts mehr gemeldet (eigene Verbindung oder ueber das Dock)

Quelle Akku: die ESPHome-Entitaet der Remote, sonst der zuletzt uebers Dock gemeldete Wert (zustaende.remote_wert).
"Gemeldet" = irgendeine Entitaet der Remote (sensor./binary_sensor.<slug>_..., auch die Dock-Werte
esphomeremote_<slug>_...) hat einen echten Zustand geschrieben (State.last_reported). Nach einem Neustart von HA
zaehlt die Zeit ab dem Start - so warnt ein Neustart nicht faelschlich. Wiederhergestellte Dock-Werte zaehlen nicht.
Hinweise lassen sich in HA wie gewohnt ignorieren.
"""
from __future__ import annotations

import logging
from datetime import timedelta

from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import entity_registry as er, issue_registry as ir
from homeassistant.helpers.event import async_call_later, async_track_time_interval
from homeassistant.util import dt as dt_util

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
AKKU_WARN = 15
AKKU_OK = 20
STILL_STUNDEN = 24
TAKT = timedelta(minutes=10)
FREI = ("unavailable", "unknown")


def _zahl(x) -> float | None:
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


class Warnungen:
    def __init__(self, hass: HomeAssistant, geraete) -> None:
        self.hass = hass
        self.geraete = geraete            # Callable -> aktuelle Liste der Remotes (device_name)
        self.start = dt_util.utcnow()
        self.akku_aktiv: set[str] = set()
        self.abmelden: list = []
        # Letzte echte Meldung je Remote, ueber Neustarts gemerkt (sonst hiesse es nach jedem Neustart "zuletzt: Start")
        from homeassistant.helpers.storage import Store
        self.store = Store(hass, 1, f"{DOMAIN}_zuletzt")
        self.gesehen: dict[str, str] = {}
        self.geladen = False

    def _zuletzt(self, slug: str):
        """Juengste echte Meldung der Remote (oder None)."""
        vorne = (f"sensor.{slug}_", f"binary_sensor.{slug}_", f"sensor.esphomeremote_{slug}_",
                 f"binary_sensor.esphomeremote_{slug}_")
        best = None
        reg = er.async_get(self.hass)
        for st in self.hass.states.async_all(("sensor", "binary_sensor")):
            if not st.entity_id.startswith(vorne) or st.state in FREI or st.attributes.get("wiederhergestellt"):
                continue
            # Nur was die Remote selbst meldet: ihre ESPHome-Entitaeten oder Werte ueber das Dock. Andere Entitaeten mit
            # ihrem Namen (z. B. Vorlagen wie "..._ha_verbunden") schreibt HA auch, wenn sie stumm ist.
            ent = reg.async_get(st.entity_id)
            if ent is None or not (ent.platform == "esphome" or (ent.platform == DOMAIN and st.attributes.get("quelle") == "dock")):
                continue
            t = getattr(st, "last_reported", None) or st.last_updated
            if t < self.start + timedelta(seconds=90):   # beim Start von HA wiederhergestellt, keine Meldung
                continue
            if best is None or t > best:
                best = t
        return best

    @callback
    def pruefen(self, _jetzt=None) -> None:
        from .zustaende import remote_wert
        jetzt = dt_util.utcnow()
        geraete = list(self.geraete())
        # Hinweise entfernter Remotes aufraeumen
        slugs = {d.replace("-", "_") for d in geraete}
        for dom, iid in list(ir.async_get(self.hass).issues):
            for vorn in ("battery_low_", "not_seen_"):
                if dom == DOMAIN and iid.startswith(vorn) and iid[len(vorn):] not in slugs:
                    ir.async_delete_issue(self.hass, DOMAIN, iid)
        for dev in geraete:
            slug = dev.replace("-", "_")
            name = dev
            # Akku
            akku = remote_wert(self.hass, f"sensor.{slug}_akkustand")
            kabel = remote_wert(self.hass, f"binary_sensor.{slug}_ladekabel")
            wert = _zahl(akku.state) if akku is not None else None
            laedt = kabel is not None and kabel.state == "on"
            iid = f"battery_low_{slug}"
            if wert is not None and wert < AKKU_WARN and not laedt:
                if iid not in self.akku_aktiv:
                    self.akku_aktiv.add(iid)
                    ir.async_create_issue(self.hass, DOMAIN, iid, is_fixable=False, severity=ir.IssueSeverity.WARNING,
                                          translation_key="battery_low",
                                          translation_placeholders={"name": name, "level": f"{wert:.0f}"})
            elif iid in self.akku_aktiv and (laedt or (wert is not None and wert >= AKKU_OK)):
                self.akku_aktiv.discard(iid)
                ir.async_delete_issue(self.hass, DOMAIN, iid)
            elif iid not in self.akku_aktiv:
                ir.async_delete_issue(self.hass, DOMAIN, iid)   # Reste aus dem letzten Lauf
            # Nicht gemeldet
            frisch = self._zuletzt(slug)
            alt = dt_util.parse_datetime(self.gesehen.get(slug, "")) if self.gesehen.get(slug) else None
            zuletzt = max(t for t in (frisch, alt) if t is not None) if (frisch or alt) else None
            if frisch is not None and frisch != alt:
                self.gesehen[slug] = frisch.isoformat()
                self.store.async_delay_save(lambda: self.gesehen, 60)
            # Ohne gemerkte Meldung zaehlt die Zeit ab dem Start von HA
            bezug = zuletzt if zuletzt is not None else self.start
            iid = f"not_seen_{slug}"
            if jetzt - bezug > timedelta(hours=STILL_STUNDEN):
                seit = dt_util.as_local(zuletzt or self.start).strftime("%d.%m.%Y %H:%M")
                ir.async_create_issue(self.hass, DOMAIN, iid, is_fixable=False, severity=ir.IssueSeverity.WARNING,
                                      translation_key="not_seen" if zuletzt is not None else "not_seen_since_start",
                                      translation_placeholders={"name": name, "hours": str(STILL_STUNDEN), "since": seit})
            else:
                ir.async_delete_issue(self.hass, DOMAIN, iid)


def einrichten(hass: HomeAssistant, geraete) -> Warnungen:
    alt = hass.data.get(DOMAIN + "_warnungen")
    if alt is not None:   # Integration neu geladen: alte Takte beenden
        for ab in alt.abmelden:
            ab()
    w = Warnungen(hass, geraete)
    hass.data[DOMAIN + "_warnungen"] = w

    @callback
    def _los(_=None) -> None:
        hass.async_create_task(_pruefen())

    async def _pruefen() -> None:
        try:
            if not w.geladen:
                w.gesehen = dict(await w.store.async_load() or {})
                w.geladen = True
            w.pruefen()
        except Exception:  # noqa: BLE001 - Warnungen duerfen die Integration nie stoeren
            _LOGGER.exception("Warnungen pruefen")

    w.abmelden = [async_call_later(hass, 120, _los),   # erst, wenn die Remotes nach dem Start Zeit zum Melden hatten
                  async_track_time_interval(hass, _los, TAKT)]
    return w
