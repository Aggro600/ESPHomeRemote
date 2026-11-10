"""Einrichtung ueber die Oberflaeche: Einstellungen -> Geraete & Dienste -> Integration hinzufuegen -> ESPHomeRemote."""

from __future__ import annotations

import secrets

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.core import callback

DOMAIN = "esphomeremote"


def _vorschlag(hass) -> str:
    """ESPHome-Geraete, die nach Fernbedienung aussehen (Name enthaelt 'remote')."""
    namen = []
    for e in hass.config_entries.async_entries("esphome"):
        n = e.data.get("device_name") or e.title.lower().replace(" ", "-")
        if "remote" in n.lower() and n not in namen:
            namen.append(n)
    return ", ".join(namen) or "esphomeremote"


def secrets_block(key: str) -> str:
    """Alles, was die Remote in den ESPHome-Secrets braucht, zum Kopieren (2026-11-10, Nutzer: "jeder ohne Ahnung"):
    vorher mussten Neue ota_pw/ap_password selbst ausdenken und direkt_schluessel mit openssl erzeugen. Werte sind
    zufaellig und werden nur hier gezeigt - wer schon Secrets hat, uebernimmt nur esphomeremote_key."""
    return (f'ota_pw: "{secrets.token_hex(8)}"\n'
            f'ap_password: "{secrets.token_hex(6)}"\n'
            f'direkt_schluessel: "{secrets.token_hex(16)}"\n'
            f'esphomeremote_key: "{key}"')


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
        # Update von esphomeremote_konfig: Schluessel und Remotes uebernehmen, sonst kommen die Remotes nicht mehr durch
        alt = self.hass.config_entries.async_entries("esphomeremote_konfig")
        if alt and not vorher:
            vorher = {"key": alt[0].data.get("key", ""),
                      "devices": ", ".join(alt[0].options.get("devices", alt[0].data.get("devices", [])))}
        vorher = vorher or {}
        return vol.Schema({
            vol.Required("devices", default=vorher.get("devices", _vorschlag(self.hass))): str,
            vol.Required("key", default=vorher.get("key", secrets.token_urlsafe(18))): str,
        })

    async def async_step_fertig(self, user_input=None):
        if user_input is not None:
            return self.async_create_entry(title="Fernbedienungen", data=self._daten)
        return self.async_show_form(step_id="fertig", data_schema=vol.Schema({}),
                                    description_placeholders={"key": self._daten["key"],
                                                              "block": secrets_block(self._daten["key"])})

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
