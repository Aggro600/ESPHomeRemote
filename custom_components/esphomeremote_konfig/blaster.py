"""Fremde IR-/RF-Sender (2026-10-28): Geraete anderer Integrationen senden die Codes wie ein Dock.

Waehlbar (Konfigurator > Infrarot, Sendeweg eines Befehls, Dockingstation-Tab), nach dem HA-Standard:
  - infrared.*  jede infrared-Entitaet (HA-Schnittstelle infrared.async_send_command, die jede Integration mit
                IR-Sender anbieten kann: ESPHome, ...) - nur IR
  - remote.*    Integrationen, deren remote.send_command rohe Codes annimmt (Formate aus der HA-Doku):
                Broadlink "b64:<Broadlink-Paket>", IR (0x26) oder 433 MHz (0xB2, nur RM Pro / RM4 Pro);
                Xiaomi Miio (Chuangmi IR) "pronto:<Pronto-Hex>:<Wiederholungen>" - nur IR
                Andere remote-Plattformen (Harmony, Android TV, ...) kennen nur eigene Befehlsnamen - nicht waehlbar.
Die Remote ruft dafuer die Aktion esphomeremote_konfig.blaster_senden auf (statt esphome.<dock>_ir_roh / _rf_roh),
der Konfigurator zum Testen ebenso. In den Listen steht die Entitaets-Id an Stelle des Dock-Namens.
"""

from __future__ import annotations

import base64
import logging
import struct

import voluptuous as vol

from homeassistant.const import STATE_UNAVAILABLE
from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.exceptions import ServiceValidationError
from homeassistant.helpers import config_validation as cv, entity_registry as er

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"
# remote-Plattform -> Format fuer remote.send_command, RF moeglich
REMOTE_PLATTFORMEN = {"broadlink": ("broadlink", True), "xiaomi_miio": ("pronto", False)}


def pronto(zeiten: list[int], f: int = 38000) -> str:
    """Rohzeiten -> Pronto-Hex (gelernt, 0000), alles im ersten Abschnitt."""
    einheit = 1_000_000 / f
    code = max(1, round(einheit / 0.241246))
    werte = [max(1, round(abs(us) / (code * 0.241246))) for us in zeiten]
    if len(werte) % 2:
        werte.append(max(1, round(40000 / (code * 0.241246))))   # Pause nach dem letzten Puls
    return " ".join(f"{w:04X}" for w in [0, code, len(werte) // 2, 0, *werte])


def broadlink_paket(zeiten: list[int], rf: bool = False, wdh: int = 0) -> str:
    """Rohzeiten (us, +an/-aus) -> Broadlink-Paket als "b64:..." (Takt 269/8192 ms, wie python-broadlink)."""
    daten = bytearray()
    for us in zeiten:
        n = max(1, round(abs(us) * 269 / 8192))
        daten += struct.pack(">B", n) if n < 256 else b"\x00" + struct.pack(">H", min(n, 0xFFFF))
    paket = bytearray([0xB2 if rf else 0x26, max(0, min(255, wdh))]) + struct.pack("<H", len(daten)) + daten
    paket += b"\x0d\x05"
    rest = (len(paket) + 4) % 16
    if rest:
        paket += bytes(16 - rest)
    return "b64:" + base64.b64encode(bytes(paket)).decode()


def ist_blaster(eid: str) -> bool:
    return "." in eid


def sender_liste(hass: HomeAssistant) -> list[dict]:
    """Fremde Sender: infrared-Entitaeten (ausser den eigenen Docks) und Broadlink-Remotes."""
    reg = er.async_get(hass)
    out = []
    for st in hass.states.async_all(("infrared", "remote")):
        e = reg.async_get(st.entity_id)
        plattform = e.platform if e else ""
        if st.domain == "remote" and plattform not in REMOTE_PLATTFORMEN:
            continue   # kennt nur eigene Befehlsnamen
        if st.domain == "infrared" and plattform == "esphome" and st.entity_id.endswith("_ir_sender") \
                and hass.services.has_service("esphome", st.entity_id[len("infrared."):-len("_ir_sender")] + "_ir_roh"):
            continue   # eigenes Dock - steht schon als Dock in der Liste
        out.append({"entity_id": st.entity_id, "name": st.attributes.get("friendly_name") or st.entity_id,
                    "plattform": plattform, "rf": st.domain == "remote" and REMOTE_PLATTFORMEN[plattform][1],
                    "online": st.state != STATE_UNAVAILABLE})
    out.sort(key=lambda x: x["name"].lower())
    return out


async def senden(hass: HomeAssistant, sender: str, zeiten: list[int], f: int = 38000, art: str = "ir",
                 wdh: int = 1, context=None) -> None:
    if not zeiten:
        raise ServiceValidationError("Kein Code")
    dom = sender.split(".", 1)[0]
    if dom == "infrared":
        if art != "ir":
            raise ServiceValidationError(f"{sender} kann nur Infrarot senden")
        from homeassistant.components.infrared import async_send_command

        from .dock import RohBefehl

        for _ in range(max(1, min(wdh, 10))):
            await async_send_command(hass, sender, RohBefehl(zeiten, f), context=context)
        return
    if dom == "remote":
        e = er.async_get(hass).async_get(sender)
        fmt, rf = REMOTE_PLATTFORMEN.get(e.platform if e else "", (None, False))
        if fmt is None:
            raise ServiceValidationError(f"{sender}: diese Integration nimmt keine rohen Codes an")
        if art == "rf" and not rf:
            raise ServiceValidationError(f"{sender} kann kein 433 MHz senden")
        befehl = broadlink_paket(zeiten, art == "rf", max(0, wdh - 1)) if fmt == "broadlink" \
            else f"pronto:{pronto(zeiten, f)}:{max(0, wdh - 1)}"
        await hass.services.async_call("remote", "send_command", {"entity_id": sender, "command": [befehl]},
                                       blocking=True, context=context)
        return
    raise ServiceValidationError(f"{sender}: kein unterstützter Sender (infrared.* oder remote.* von Broadlink/Xiaomi)")


def _zeiten(text) -> list[int]:
    if isinstance(text, list):
        return [int(x) for x in text if int(x)]
    return [int(float(x)) for x in str(text).replace(";", ",").replace(" ", ",").split(",") if x.strip() and int(float(x))]


SCHEMA = vol.Schema({
    vol.Required("sender"): cv.entity_id,
    vol.Required("code"): vol.Any(str, [vol.Coerce(int)]),
    vol.Optional("traeger", default=38000): vol.Coerce(int),
    vol.Optional("art", default="ir"): vol.In(["ir", "rf"]),
    vol.Optional("wiederholen", default=1): vol.All(vol.Coerce(int), vol.Range(1, 30)),
}, extra=vol.ALLOW_EXTRA)


def registrieren(hass: HomeAssistant) -> None:
    async def dienst(call: ServiceCall) -> None:
        d = call.data
        try:
            zeiten = _zeiten(d["code"])
        except (TypeError, ValueError) as err:
            raise ServiceValidationError(f"Code nicht lesbar: {err}") from err
        await senden(hass, d["sender"], zeiten, d["traeger"], d["art"], d["wiederholen"], call.context)

    hass.services.async_register(DOMAIN, "blaster_senden", dienst, schema=SCHEMA)
