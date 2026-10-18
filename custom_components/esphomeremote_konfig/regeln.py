"""Dienst esphomeremote_konfig.regeln (2026-10-15): schrittweises Regeln fuer Tasten und Folgemodi.

Die Remote schickt nur feste Werte (keine Templates); was vom aktuellen Zustand abhaengt
(Farbtemperatur, Drehung umschalten), rechnet deshalb dieser Dienst aus.

  art: helligkeit     schritt in %   (light; aus + positiver Schritt schaltet ein)
       farbtemperatur schritt in K   (light; nur wenn an; begrenzt auf min/max der Lampe)
       stufe          schritt in Stufen (fan; +/- wie fan.increase_speed/decrease_speed)
       drehung        -              (fan; Oszillation umschalten)
"""

from __future__ import annotations

import voluptuous as vol

from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.helpers import config_validation as cv

ARTEN = ("helligkeit", "farbtemperatur", "stufe", "drehung")


def registrieren(hass: HomeAssistant) -> None:
    async def regeln(call: ServiceCall) -> None:
        art, schritt = call.data["art"], call.data.get("schritt", 0)
        for eid in call.data["entity_id"]:
            st = hass.states.get(eid)
            if st is None:
                continue
            a = st.attributes
            if art == "helligkeit":
                if st.state != "on" and schritt <= 0:
                    continue
                await hass.services.async_call("light", "turn_on", {"entity_id": eid, "brightness_step_pct": schritt})
            elif art == "farbtemperatur":
                k = a.get("color_temp_kelvin")
                if st.state != "on" or k is None:
                    continue
                lo, hi = a.get("min_color_temp_kelvin") or 2000, a.get("max_color_temp_kelvin") or 6500
                await hass.services.async_call("light", "turn_on", {"entity_id": eid, "color_temp_kelvin": max(lo, min(hi, int(k) + schritt))})
            elif art == "stufe":
                schr = a.get("percentage_step") or 10
                svc = "increase_speed" if schritt > 0 else "decrease_speed"
                await hass.services.async_call("fan", svc, {"entity_id": eid, "percentage_step": min(100, schr * max(1, abs(schritt)))})
            elif art == "drehung":
                await hass.services.async_call("fan", "oscillate", {"entity_id": eid, "oscillating": not a.get("oscillating", False)})

    hass.services.async_register(
        "esphomeremote_konfig", "regeln", regeln,
        schema=vol.Schema({
            vol.Required("entity_id"): cv.entity_ids,
            vol.Required("art"): vol.In(ARTEN),
            # Die Remote schickt alle Werte als Text
            vol.Optional("schritt", default=0): vol.Coerce(int),
        }),
    )
