"""Licht-Widget der Remote (2026-10-29): Zustaende je Lampe fuer die Remote und ein Dienst, der die Bedienung des
Widgets in passende light-Aufrufe uebersetzt (Farben als Liste gehen ueber die ESPHome-Aktion nicht, nur Texte).

Dienst esphomeremote_konfig.licht: entity_id "light.a,light.b", aktion an|aus|toggle|hell|kelvin|farbe|szene, wert
- hell: Prozent (0 = aus); kelvin: Farbtemperatur; farbe: "Farbton,Saettigung"; szene: scene.*/script.*
Je Lampe nur, was sie kann: Farbtemperatur an Lampen ohne Weisston wird zur Farbe, Farbe an reinen Weisslampen entfaellt.
"""
from __future__ import annotations

import logging

import voluptuous as vol

from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.util import color as color_util

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"

FARBMODI = {"hs", "xy", "rgb", "rgbw", "rgbww"}


def faehig(st) -> int:
    """1 dimmbar, 2 Farbtemperatur, 4 Farbe."""
    modi = set(st.attributes.get("supported_color_modes") or [])
    f = 0
    if modi - {"onoff"}:
        f |= 1
    if "color_temp" in modi:
        f |= 2
    if modi & FARBMODI:
        f |= 4
    return f


def licht_eintrag(st) -> list:
    """Zusatz fuer StatesView: [Farbmodus, Farbton, Saettigung, Kelvin, Kelvin min, Kelvin max, Faehigkeiten, Name]."""
    a = st.attributes
    hs = a.get("hs_color") or (None, None)
    k = a.get("color_temp_kelvin")
    return [str(a.get("color_mode") or ""), round(hs[0]) if isinstance(hs[0], (int, float)) else -1,
            round(hs[1]) if isinstance(hs[1], (int, float)) else -1, int(k) if isinstance(k, (int, float)) else -1,
            int(a.get("min_color_temp_kelvin") or 2000), int(a.get("max_color_temp_kelvin") or 6500), faehig(st),
            str(a.get("friendly_name") or st.entity_id)[:24]]


SCHEMA = vol.Schema({vol.Required("entity_id"): str, vol.Required("aktion"): str, vol.Optional("wert", default=""): str},
                    extra=vol.ALLOW_EXTRA)


def registrieren(hass: HomeAssistant) -> None:
    async def dienst(call: ServiceCall) -> None:
        ids = [e.strip() for e in call.data["entity_id"].split(",") if e.strip().startswith(("light.", "scene.", "script."))]
        akt, wert = call.data["aktion"], call.data.get("wert", "")
        lichter = [e for e in ids if e.startswith("light.")]
        an = lambda daten, es: hass.services.async_call("light", "turn_on", {"entity_id": es, **daten}, blocking=False)  # noqa: E731
        if akt == "szene":
            if wert.startswith(("scene.", "script.")):
                await hass.services.async_call(wert.split(".")[0], "turn_on", {"entity_id": wert}, blocking=False)
            return
        if not lichter:
            return
        if akt == "aus" or (akt == "hell" and wert.strip() in ("0", "")):
            await hass.services.async_call("light", "turn_off", {"entity_id": lichter}, blocking=False)
            return
        if akt == "toggle":
            irgendwas = any((s := hass.states.get(e)) is not None and s.state == "on" for e in lichter)
            await hass.services.async_call("light", "turn_off" if irgendwas else "turn_on", {"entity_id": lichter}, blocking=False)
            return
        if akt == "an":
            await an({}, lichter)
            return
        f = {e: faehig(s) for e in lichter if (s := hass.states.get(e)) is not None}
        try:
            if akt == "hell":
                pct = max(1, min(100, int(float(wert))))
                dimm = [e for e in lichter if f.get(e, 0) & 1]
                if dimm:
                    await an({"brightness_pct": pct}, dimm)
                if rest := [e for e in lichter if e not in dimm]:
                    await an({}, rest)
            elif akt == "kelvin":
                k = max(1500, min(9000, int(float(wert))))
                ct = [e for e in lichter if f.get(e, 0) & 2]
                farbig = [e for e in lichter if not f.get(e, 0) & 2 and f.get(e, 0) & 4]
                if ct:
                    await an({"color_temp_kelvin": k}, ct)
                if farbig:
                    await an({"hs_color": list(color_util.color_temperature_to_hs(k))}, farbig)
            elif akt == "farbe":
                h, _, s = wert.partition(",")
                hs = [max(0.0, min(360.0, float(h))), max(0.0, min(100.0, float(s or 100)))]
                if farbig := [e for e in lichter if f.get(e, 0) & 4]:
                    await an({"hs_color": hs}, farbig)
        except (TypeError, ValueError) as err:
            _LOGGER.warning("licht: %s %s %s: %s", ids, akt, wert, err)

    hass.services.async_register(DOMAIN, "licht", dienst, schema=SCHEMA)
