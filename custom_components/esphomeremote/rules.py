"""Dienst esphomeremote.rules (2026-10-15): schrittweises Regeln fuer Tasten und Folgemodi.

Die Remote schickt nur feste Werte (keine Templates); was vom aktuellen Zustand abhaengt
(Farbtemperatur, Drehung umschalten), rechnet deshalb dieser Dienst aus.

  type: brightness   step in %   (light; aus + positiver Schritt schaltet ein)
        color_temp   step in K   (light; nur wenn an; begrenzt auf min/max der Lampe)
        speed        step in Stufen (fan; +/- wie fan.increase_speed/decrease_speed)
        oscillation  -           (fan; Oszillation umschalten)
  Alte Namen (art/schritt, helligkeit/farbtemperatur/stufe/drehung) gehen weiter (aliases.py, 2026-11-03).
"""

from __future__ import annotations

import voluptuous as vol

from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.helpers import config_validation as cv

from .aliases import schema_with_aliases

ARTEN = ("brightness", "color_temp", "speed", "oscillation")


def registrieren(hass: HomeAssistant) -> None:
    async def regeln(call: ServiceCall) -> None:
        art, schritt = call.data["type"], call.data.get("step", 0)
        for eid in call.data["entity_id"]:
            st = hass.states.get(eid)
            if st is None:
                continue
            a = st.attributes
            if art == "brightness":
                if st.state != "on" and schritt <= 0:
                    continue
                await hass.services.async_call("light", "turn_on", {"entity_id": eid, "brightness_step_pct": schritt})
            elif art == "color_temp":
                k = a.get("color_temp_kelvin")
                if st.state != "on" or k is None:
                    continue
                lo, hi = a.get("min_color_temp_kelvin") or 2000, a.get("max_color_temp_kelvin") or 6500
                await hass.services.async_call("light", "turn_on", {"entity_id": eid, "color_temp_kelvin": max(lo, min(hi, int(k) + schritt))})
            elif art == "speed":
                schr = a.get("percentage_step") or 10
                svc = "increase_speed" if schritt > 0 else "decrease_speed"
                await hass.services.async_call("fan", svc, {"entity_id": eid, "percentage_step": min(100, schr * max(1, abs(schritt)))})
            elif art == "oscillation":
                await hass.services.async_call("fan", "oscillate", {"entity_id": eid, "oscillating": not a.get("oscillating", False)})

    hass.services.async_register(
        "esphomeremote", "rules", regeln,
        schema=schema_with_aliases("rules", vol.Schema({
            vol.Required("entity_id"): cv.entity_ids,
            vol.Required("type"): vol.In(ARTEN),
            # Die Remote schickt alle Werte als Text
            vol.Optional("step", default=0): vol.Coerce(int),
        })),
    )
