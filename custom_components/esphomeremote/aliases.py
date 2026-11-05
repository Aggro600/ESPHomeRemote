"""English service fields and values, old German ones still accepted (2026-11-03).

The services were renamed to English on 2026-11-02, their fields and values followed a day later. Automations,
scripts and remote configurations written before keep working: every service call goes through normalize() first,
which turns the old field names and values into the new ones. A field given both ways keeps the new one.
"""
from __future__ import annotations

from typing import Any

FIELDS: dict[str, dict[str, str]] = {
    "to_remotes": {"dienst": "service", "daten": "data"},
    "rules": {"art": "type", "schritt": "step"},
    "timer_extend": {"minuten": "minutes"},
    "blaster_send": {"sender": "transmitter", "traeger": "carrier", "art": "type", "wiederholen": "repeat"},
    "light": {"aktion": "action", "wert": "value"},
    "camera_tv": {"kamera": "camera"},
    "camera_size": {"groesse": "size"},
    "camera_position": {},
    "restart": {"geraet": "device"},
}

VALUES: dict[tuple[str, str], dict[str, str]] = {
    ("rules", "type"): {"helligkeit": "brightness", "farbtemperatur": "color_temp", "stufe": "speed", "drehung": "oscillation"},
    ("light", "action"): {"an": "on", "aus": "off", "hell": "brightness", "farbe": "color", "szene": "scene"},
    ("camera_tv", "camera"): {"letzte": "last", "aus": "off", "naechste": "next", "nächste": "next",
                              "vorherige": "previous", "zurueck": "previous", "zurück": "previous", "prev": "previous"},
    ("camera_size", "size"): {"klein": "small", "mittel": "medium", "groß": "large", "gross": "large",
                              "fast vollbild": "almost_full", "naechste": "next", "nächste": "next",
                              "almost fullscreen": "almost_full"},
    ("camera_position", "position"): {"naechste": "next", "nächste": "next"},
}


def normalize(service: str, data: dict[str, Any]) -> dict[str, Any]:
    """New field names and values for a service call (old German ones are translated)."""
    felder = FIELDS.get(service, {})
    out = {k: v for k, v in data.items() if k not in felder}
    for alt, neu in felder.items():
        if alt in data and neu not in out:
            out[neu] = data[alt]
    # YAML liest ein ungequotetes off/on als Wahrheitswert (camera: off -> False)
    if service == "camera_tv" and isinstance(out.get("camera"), bool):
        out["camera"] = "last" if out["camera"] else "off"
    for (dienst, feld), werte in VALUES.items():
        if dienst == service and isinstance(out.get(feld), str):
            v = out[feld].strip()
            out[feld] = werte.get(v.lower(), v)
    return out


def schema_with_aliases(service: str, schema=None):
    """Service schema that first normalizes, then validates the new names (call.data then holds the new ones)."""
    def check(data):
        neu = normalize(service, dict(data))
        return schema(neu) if schema is not None else neu
    return check


# Entities HA sets itself (no registry entry), German until 2026-11-03
ENTITIES = {
    "binary_sensor.esphomeremote_kamera_tv": "binary_sensor.esphomeremote_camera_tv",
    "sensor.esphomeremote_kamera_groesse": "sensor.esphomeremote_camera_size",
    "sensor.esphomeremote_tv_eingang": "sensor.esphomeremote_tv_input",
}
# state "aus" of the TV input sensor is "off" since then
STATE_VALUES = {"sensor.esphomeremote_tv_input": {"aus": "off"}}


def migrate_config(obj) -> bool:
    """Rewrite a stored remote configuration in place: entity ids, service fields and values, popup conditions.
    Returns True if something changed."""
    changed = False
    if isinstance(obj, list):
        for x in obj:
            changed |= migrate_config(x)
        return changed
    if not isinstance(obj, dict):
        return False
    for k, v in list(obj.items()):
        if isinstance(v, str) and v in ENTITIES:
            obj[k] = ENTITIES[v]
            changed = True
    svc = obj.get("svc")
    if isinstance(svc, str) and svc.startswith("esphomeremote.") and isinstance(obj.get("data"), dict):
        neu = normalize(svc.split(".", 1)[1], obj["data"])
        if neu != obj["data"]:
            obj["data"] = neu
            changed = True
    werte = STATE_VALUES.get(obj.get("entity"))
    if werte:
        for k in ("to", "from", "state"):
            if isinstance(obj.get(k), str) and obj[k] in werte:
                obj[k] = werte[obj[k]]
                changed = True
    for v in obj.values():
        if isinstance(v, (dict, list)):
            changed |= migrate_config(v)
    return changed
