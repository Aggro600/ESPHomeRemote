"""Einrichtung ueber die Oberflaeche: Einstellungen -> Geraete & Dienste -> Integration hinzufuegen -> ESPHomeRemote."""

from __future__ import annotations

import secrets

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import callback

DOMAIN = "esphomeremote_konfig"


def _vorschlag(hass) -> str:
    """ESPHome-Geraete, die nach Fernbedienung aussehen (Name enthaelt 'remote')."""
    namen = []
    for e in hass.config_entries.async_entries("esphome"):
        n = e.data.get("device_name") or e.title.lower().replace(" ", "-")
        if "remote" in n.lower() and n not in namen:
            namen.append(n)
    return ", ".join(namen) or "open-remote"


def _liste(text: str) -> list[str]:
    return [x.strip().lower().replace(" ", "-") for x in text.replace(";", ",").split(",") if x.strip()]


class EsphomeremoteFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        if self._async_current_entries() or DOMAIN in self.hass.data:
            return self.async_abort(reason="single_instance_allowed")
        if user_input is not None:
            geraete = _liste(user_input["devices"])
            if not geraete:
                return self.async_show_form(step_id="user", data_schema=self._schema(user_input), errors={"devices": "leer"})
            self._daten = {"key": user_input["key"].strip(), "devices": geraete}
            return await self.async_step_fertig()
        return self.async_show_form(step_id="user", data_schema=self._schema())

    def _schema(self, vorher=None):
        vorher = vorher or {}
        return vol.Schema({
            vol.Required("devices", default=vorher.get("devices", _vorschlag(self.hass))): str,
            vol.Required("key", default=vorher.get("key", secrets.token_urlsafe(18))): str,
        })

    async def async_step_fertig(self, user_input=None):
        if user_input is not None:
            return self.async_create_entry(title="Fernbedienungen", data=self._daten)
        return self.async_show_form(step_id="fertig", data_schema=vol.Schema({}),
                                    description_placeholders={"key": self._daten["key"]})

    @staticmethod
    @callback
    def async_get_options_flow(entry):
        return Optionen()


class Optionen(config_entries.OptionsFlow):
    async def async_step_init(self, user_input=None):
        e = self.config_entry
        if user_input is not None:
            return self.async_create_entry(data={"devices": _liste(user_input["devices"])})
        jetzt = ", ".join(e.options.get("devices", e.data["devices"]))
        return self.async_show_form(step_id="init", data_schema=vol.Schema({vol.Required("devices", default=jetzt): str}),
                                    description_placeholders={"key": e.data["key"]})
