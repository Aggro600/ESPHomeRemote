"""Einrichtung ueber die Oberflaeche: Einstellungen -> Geraete & Dienste -> Integration hinzufuegen -> ESPHomeRemote."""

from __future__ import annotations

import os
import re
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


def secrets_pfad(hass) -> str | None:
    """secrets.yaml des ESPHome-Add-ons (/config/esphome) - None, wenn es keinen ESPHome-Ordner gibt."""
    ordner = hass.config.path("esphome")
    return os.path.join(ordner, "secrets.yaml") if os.path.isdir(ordner) else None


def secrets_lesen(pfad: str | None) -> dict[str, str]:
    """Einfache Zeilen "name: wert" aus der secrets.yaml (genug, um vorhandene Eintraege zu erkennen)."""
    if not pfad or not os.path.isfile(pfad):
        return {}
    werte = {}
    with open(pfad, encoding="utf-8") as f:
        for z in f:
            m = re.match(r'^([A-Za-z0-9_]+)\s*:\s*(.*?)\s*$', z)
            if m:
                werte[m.group(1)] = m.group(2).split(" #")[0].strip().strip('"').strip("'")
    return werte


def secrets_eintragen(pfad: str, block: str) -> list[str]:
    """Fehlende Zeilen des Blocks an die secrets.yaml anhaengen (2026-11-10, Nutzer: "komisch, dass ich die Secrets
    vergeben muss"). Vorhandene Eintraege bleiben unangetastet. Gibt die Namen der ergaenzten Zeilen zurueck."""
    da = secrets_lesen(pfad)
    neu = [z for z in block.splitlines() if z.split(":", 1)[0].strip() not in da]
    if not neu:
        return []
    alt = ""
    if os.path.isfile(pfad):
        with open(pfad, encoding="utf-8") as f:
            alt = f.read()
    with open(pfad, "a", encoding="utf-8") as f:
        f.write(("" if not alt else "\n" if alt.endswith("\n") else "\n\n") + "# ESPHomeRemote (von der Integration eingetragen)\n"
                + "\n".join(neu) + "\n")
    return [z.split(":", 1)[0] for z in neu]


def _liste(text: str) -> list[str]:
    return [x.strip().lower().replace(" ", "-") for x in text.replace(";", ",").split(",") if x.strip()]


class EsphomeremoteFlow(config_entries.ConfigFlow, domain=DOMAIN):
    VERSION = 1

    async def async_step_user(self, user_input=None):
        if self._async_current_entries() or DOMAIN in self.hass.data:
            return self.async_abort(reason="single_instance_allowed")
        # HA Container/Core (2026-11-12): sagen, was ohne ESPHome-Add-on fehlt, statt spaeter unklar zu scheitern
        if not os.environ.get("SUPERVISOR_TOKEN") and not getattr(self, "_hinweis_gezeigt", False):
            return await self.async_step_hinweis()
        if not hasattr(self, "_vorhanden"):
            self._pfad = secrets_pfad(self.hass)
            self._vorhanden = await self.hass.async_add_executor_job(secrets_lesen, self._pfad)
        if user_input is not None:
            geraete = _liste(user_input["devices"])
            if not geraete:
                return self.async_show_form(step_id="user", data_schema=self._schema(user_input), errors={"devices": "leer"})
            self._daten = {"key": user_input["key"].strip(), "devices": geraete}
            return await self.async_step_fertig()
        return self.async_show_form(step_id="user", data_schema=self._schema())

    async def async_step_hinweis(self, user_input=None):
        if user_input is not None:
            self._hinweis_gezeigt = True
            return await self.async_step_user()
        return self.async_show_form(step_id="hinweis", data_schema=vol.Schema({}),
                                    description_placeholders={"ordner": self.hass.config.path("esphome")})

    def _schema(self, vorher=None):
        # Update von esphomeremote_konfig: Schluessel und Remotes uebernehmen, sonst kommen die Remotes nicht mehr durch
        alt = self.hass.config_entries.async_entries("esphomeremote_konfig")
        if alt and not vorher:
            vorher = {"key": alt[0].data.get("key", ""),
                      "devices": ", ".join(alt[0].options.get("devices", alt[0].data.get("devices", [])))}
        vorher = vorher or {}
        # Schon ein esphomeremote_key in den ESPHome-Secrets (Neuinstallation)? Den nehmen - sonst passten Remote und
        # Integration nicht zusammen (2026-11-10)
        schon = (getattr(self, "_vorhanden", None) or {}).get("esphomeremote_key") or None
        return vol.Schema({
            vol.Required("devices", default=vorher.get("devices", _vorschlag(self.hass))): str,
            vol.Required("key", default=vorher.get("key", schon or secrets.token_urlsafe(18))): str,
        })

    async def async_step_fertig(self, user_input=None):
        if not hasattr(self, "_block"):
            self._block = secrets_block(self._daten["key"])
        if user_input is not None:
            if user_input.get("eintragen") and self._pfad:
                try:
                    await self.hass.async_add_executor_job(secrets_eintragen, self._pfad, self._block)
                except OSError:
                    return self.async_show_form(step_id="fertig", data_schema=self._fertig_schema(), errors={"base": "schreiben"},
                                                description_placeholders={"key": self._daten["key"], "block": self._block})
            return self.async_create_entry(title="Fernbedienungen", data=self._daten)
        return self.async_show_form(step_id="fertig", data_schema=self._fertig_schema(),
                                    description_placeholders={"key": self._daten["key"], "block": self._block})

    def _fertig_schema(self):
        if not self._pfad:   # kein ESPHome-Ordner: nur zum Kopieren
            return vol.Schema({})
        return vol.Schema({vol.Optional("eintragen", default=True): bool})

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
