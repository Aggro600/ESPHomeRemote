"""Entitaeten der Docks und Remotes sicher finden (2026-11-26, alle Stellen 2026-11-28)."""
from __future__ import annotations

from homeassistant.core import HomeAssistant


def dock_entitaet(hass: HomeAssistant, dock: str, name: str, domain: str = "sensor") -> str:
    """Entitaet eines Docks ueber das Geraet (ESPHome-Name) und den Namen aus der Firmware (original_name) - nicht ueber
    die entity_id: HA setzt teils Bereich/Anzeigenamen davor (2026-11-26: sensor.wohnzimmer_dock_wz_bluetooth_geraete;
    bei einem Tester fehlten dadurch die Bluetooth-Plaetze im Konfigurator). Ohne Treffer: die uebliche entity_id."""
    from homeassistant.helpers import entity_registry as er
    reg = er.async_get(hass)
    for ce in hass.config_entries.async_entries("esphome"):
        if str(ce.data.get("device_name") or "").replace("-", "_") != dock:
            continue
        for e in er.async_entries_for_config_entry(reg, ce.entry_id):
            if e.domain == domain and (e.original_name or "") == name:
                return e.entity_id
    slug = name.lower().replace("ä", "ae").replace("ö", "oe").replace("ü", "ue").replace("-", "_").replace(" ", "_")
    return f"{domain}.{dock}_{slug}"


def dock_entitaeten(hass: HomeAssistant, docks) -> dict:
    """{dock: {"bt": .., "status": ..}} fuer den Konfigurator."""
    return {d: {"bt": dock_entitaet(hass, d, "Bluetooth-Geraete"), "status": dock_entitaet(hass, d, "Status", "binary_sensor")}
            for d in docks}


# --------------------------------------------------------------------------------------------------- alle Stellen
# 2026-11-28: dasselbe Problem gab es auch an anderen Stellen (Akku-Auswertung, Warnungen, Licht-Kurve, Ladelimit,
# Diagnose, Konfigurator-Diagramme ...), die die entity_id fest als <geraet>_<name> annahmen. Statt jede Stelle einzeln
# ueber den Firmware-Namen suchen zu lassen: eine Karte "uebliche entity_id -> echte", neu gebaut bei Registry-Aenderungen.
KARTE = "esphomeremote_entitaeten_karte"


def _bauen(hass: HomeAssistant) -> dict:
    """{"karte": {uebliche entity_id: echte}, "ids": {geraet: {echte entity_ids}}} ueber alle ESPHome-Geraete.
    Ueblich = <domain>.<device_name>_<slugify(Name aus der Firmware)> - so vergibt HA sie ohne Bereich/Anzeigenamen.
    In "karte" stehen nur die Abweichungen."""
    from homeassistant.helpers import entity_registry as er
    from homeassistant.util import slugify
    reg = er.async_get(hass)
    karte: dict[str, str] = {}
    ids: dict[str, set] = {}
    for ce in hass.config_entries.async_entries("esphome"):
        g = str(ce.data.get("device_name") or "").replace("-", "_")
        if not g:
            continue
        menge = ids.setdefault(g, set())
        for e in er.async_entries_for_config_entry(reg, ce.entry_id):
            menge.add(e.entity_id)
            if e.original_name:
                ueblich = f"{e.domain}.{g}_{slugify(e.original_name)}"
                if ueblich != e.entity_id:
                    karte[ueblich] = e.entity_id
    return {"karte": karte, "ids": ids}


def _daten(hass: HomeAssistant) -> dict:
    d = hass.data.get(KARTE)
    if d is None:
        d = hass.data[KARTE] = _bauen(hass)
        if not hass.data.get(KARTE + "_abo"):
            from homeassistant.core import callback
            from homeassistant.helpers import entity_registry as er

            @callback
            def _neu(_ev) -> None:
                hass.data.pop(KARTE, None)   # beim naechsten Zugriff neu bauen

            hass.data[KARTE + "_abo"] = hass.bus.async_listen(er.EVENT_ENTITY_REGISTRY_UPDATED, _neu)
    return d


def aufloesen(hass: HomeAssistant, eid: str) -> str:
    """Uebliche entity_id einer Remote-/Dock-Entitaet (sensor.remote_wz_wohnzimmer_akkustand) -> die, die HA wirklich vergeben
    hat (z. B. sensor.wohnzimmer_remote_wz_wohnzimmer_akkustand). Unbekannt: unveraendert."""
    try:
        return _daten(hass)["karte"].get(eid, eid)
    except Exception:  # noqa: BLE001 - z. B. Tests ohne Registry
        return eid


def entitaeten_karte(hass: HomeAssistant, geraete=None) -> dict:
    """Fuer den Konfigurator: {uebliche entity_id: echte} (nur Abweichungen), optional nur fuer diese Geraete."""
    try:
        karte = _daten(hass)["karte"]
        if geraete is None:
            return dict(karte)
        vorn = tuple(f".{str(g).replace('-', '_')}_" for g in geraete)
        return {k: v for k, v in karte.items() if any(x in k for x in vorn)}
    except Exception:  # noqa: BLE001
        return {}


def geraet_ids(hass: HomeAssistant, geraet: str) -> set:
    """Alle echten entity_ids eines ESPHome-Geraets (device_name, - oder _)."""
    try:
        return _daten(hass)["ids"].get(geraet.replace("-", "_"), set())
    except Exception:  # noqa: BLE001
        return set()


def ueblich(hass: HomeAssistant, eid: str) -> str:
    """Umkehrung von aufloesen: echte entity_id -> uebliche (unbekannt: unveraendert)."""
    try:
        d = _daten(hass)
        if "zurueck" not in d:
            d["zurueck"] = {v: k for k, v in d["karte"].items()}
        return d["zurueck"].get(eid, eid)
    except Exception:  # noqa: BLE001
        return eid
