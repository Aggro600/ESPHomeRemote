"""Verknuepfungen je Remote (2026-10-19): welche HA-Entitaeten die Firmware nutzt, gewaehlt im Konfigurator.

cfg["ents"] = {rolle: entity_id}. Die Firmware kann HA-Entitaeten nur abonnieren, wenn ihr Name beim Bauen feststeht.
Deshalb spiegelt die Integration die gewaehlte Entitaet auf einen festen Namen je Remote:

    sensor.esphomeremote_spiegel_<remote>_<rolle>   (Zustand + alle Attribute der gewaehlten Entitaet)

Kein Registry-Eintrag, kein Recorder (wie sensor.esphomeremote_konfig_<remote>). Aktionen (einschalten, Ansage,
ADB-Text, Multiroom-Tasten) schickt die Remote direkt an die gewaehlte Entitaet - die Namen stehen in ihrer
Konfiguration (tasten_konfig: ent("tv") ...). Ohne Auswahl: Spiegel "unavailable", die Funktion ist aus.
"""

from __future__ import annotations

import re

import voluptuous as vol

from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event

# rolle: (Domaenen, gespiegelt?, Beschreibung)
ROLLEN = {
    "jetzt": (("media_player",), True, "Wiedergabe-Karte, Media-Seite, Musik-Anzeige"),
    "tv": (("media_player",), True, "Fernseher: an/aus, Energie-Zeiten, Knopf „Fernseher einschalten“"),
    "multiroom": (("media_player",), False, "Musik-Steuerung bei Quelle Multiroom"),
    "tts": (("media_player",), False, "Lautsprecher für Antworten des Assistenten"),
    "adb": (("media_player",), False, "Streamer für Texteingabe per ADB (Android-TV-Integration)"),
    "timer": (("timer",), True, "Countdown neben der Uhr"),
}


def validate_ents(v) -> dict:
    if not isinstance(v, dict):
        raise vol.Invalid("Verknüpfungen: Objekt erwartet")
    out = {}
    for rolle, e in v.items():
        if rolle not in ROLLEN:
            continue
        e = str(e or "").strip()
        if not e:
            continue
        dom = e.split(".", 1)[0]
        if dom not in ROLLEN[rolle][0] or not re.fullmatch(r"[a-z_]+\.[a-z0-9_]+", e):
            raise vol.Invalid(f"Verknüpfung {rolle}: '{e}' passt nicht ({', '.join(ROLLEN[rolle][0])})")
        out[rolle] = e
    return out


def spiegel_id(slug: str, rolle: str) -> str:
    return f"sensor.esphomeremote_spiegel_{slug}_{rolle}"


class Spiegel:
    """Haelt die Spiegel-Zustaende aller Remotes aktuell."""

    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self._abos: dict[str, callable] = {}   # slug -> Abmelden
        self._ziele: dict[str, dict] = {}      # slug -> {rolle: entity_id}

    @callback
    def setzen(self, slug: str, ents: dict) -> None:
        ziele = {r: ents.get(r, "") for r, (_, gespiegelt, _) in ROLLEN.items() if gespiegelt}
        if self._ziele.get(slug) == ziele and slug in self._abos:
            return
        self.weg(slug, entfernen=False)
        self._ziele[slug] = ziele
        for rolle, e in ziele.items():
            self._kopieren(slug, rolle, e)
        quellen = {e: r for r, e in ziele.items() if e}

        @callback
        def geaendert(ev) -> None:
            e = ev.data["entity_id"]
            for rolle, ziel in self._ziele.get(slug, {}).items():
                if ziel == e:
                    self._kopieren(slug, rolle, e)

        if quellen:
            self._abos[slug] = async_track_state_change_event(self.hass, list(quellen), geaendert)
        else:
            self._abos[slug] = lambda: None

    @callback
    def _kopieren(self, slug: str, rolle: str, e: str) -> None:
        st = self.hass.states.get(e) if e else None
        attr = dict(st.attributes) if st else {}
        attr["friendly_name"] = f"Spiegel {slug} {rolle}"
        attr["gespiegelt_von"] = e
        self.hass.states.async_set(spiegel_id(slug, rolle), st.state if st else "unavailable", attr)

    @callback
    def weg(self, slug: str, entfernen: bool = True) -> None:
        if (ab := self._abos.pop(slug, None)) is not None:
            ab()
        if entfernen:
            for rolle, (_, gespiegelt, _) in ROLLEN.items():
                if gespiegelt:
                    self.hass.states.async_remove(spiegel_id(slug, rolle))
            self._ziele.pop(slug, None)
