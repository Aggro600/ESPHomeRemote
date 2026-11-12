"""Reparatur-Hinweise (Einstellungen -> Reparaturen), 2026-11-12.

Was der Konfigurator bisher nur auf seiner Firmware-Seite zeigte, steht damit dort, wo HA alle Probleme sammelt - mit
Knopf zum Beheben, wo es einen gibt (repairs.py):
  - no_supervisor        HA Container/Core: kein ESPHome-Add-on, also kein Bauen/Flashen aus dem Konfigurator (Info)
  - old_files            alte ESPHomeRemote-Dateien im ESPHome-Ordner, die ein Update brechen (behebbar: ins Archiv)
  - usb_flash_<name>     Remote mit Firmware vor 2026.11.5.4: braucht einmal USB (neuer Bootloader, nicht per WLAN)
Akku leer / lange nicht gemeldet: warnungen.py. Freigabe "Aktionen ausfuehren" und Bruecken-Adresse: __init__.py, bridge.py.
"""
from __future__ import annotations

import asyncio
import logging

from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import issue_registry as ir

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
USB_AB = (2026, 11, 5, 4)   # ab dieser Firmware passt der Bootloader (README "Installed before 2026.11.5.4?")


def _ver(s: str) -> tuple[int, ...]:
    try:
        return tuple(int(x) for x in str(s or "").split("."))
    except ValueError:
        return ()


@callback
def beim_start(hass: HomeAssistant) -> None:
    from .firmware import supervisor
    if not supervisor():
        ir.async_create_issue(hass, DOMAIN, "no_supervisor", is_fixable=False, is_persistent=False,
                              severity=ir.IssueSeverity.WARNING, translation_key="no_supervisor",
                              translation_placeholders={"ordner": hass.config.path("esphome")},
                              learn_more_url="https://github.com/Aggro600/ESPHomeRemote#what-you-need")
        return
    ir.async_delete_issue(hass, DOMAIN, "no_supervisor")

    async def spaeter() -> None:
        await asyncio.sleep(60)   # Add-on und ESPHome-Eintraege sind dann da
        try:
            from .firmware import liste
            await liste(hass)   # ruft nach_liste()
        except Exception as err:  # noqa: BLE001 - nur Hinweise
            _LOGGER.debug("Reparaturen: erste Pruefung ohne Ergebnis (%s)", err)

    hass.async_create_background_task(spaeter(), f"{DOMAIN} reparaturen")


@callback
def nach_liste(hass: HomeAssistant, geraete: list[dict], altlasten: list[str]) -> None:
    """Nach jeder Firmware-Liste (Konfigurator offen, Start): Hinweise setzen oder loeschen."""
    if altlasten:
        ir.async_create_issue(hass, DOMAIN, "old_files", is_fixable=True, is_persistent=False,
                              severity=ir.IssueSeverity.ERROR, translation_key="old_files",
                              translation_placeholders={"dateien": ", ".join(altlasten[:6]) + (" …" if len(altlasten) > 6 else ""),
                                                        "anzahl": str(len(altlasten))})
    else:
        ir.async_delete_issue(hass, DOMAIN, "old_files")

    reg = ir.async_get(hass)
    offen = {k[1] for k in reg.issues if k[0] == DOMAIN and k[1].startswith("usb_flash_")}
    for g in geraete:
        if g.get("art") != "remote" or not g.get("geflasht"):
            continue
        iid = f"usb_flash_{g['name']}"
        v = _ver(g.get("fw"))
        if v and v < USB_AB:
            ir.async_create_issue(hass, DOMAIN, iid, is_fixable=False, is_persistent=False, severity=ir.IssueSeverity.WARNING,
                                  translation_key="usb_flash_needed",
                                  translation_placeholders={"name": g.get("friendly") or g["name"], "version": g.get("fw") or "?"},
                                  learn_more_url="https://github.com/Aggro600/ESPHomeRemote#updates")
            offen.discard(iid)
    for iid in offen:
        ir.async_delete_issue(hass, DOMAIN, iid)
