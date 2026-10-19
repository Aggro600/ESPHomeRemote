"""Dockingstation-Tab (2026-10-16): IR-Geraete, die HA ueber einen IR-Sender (infrared-Entitaet,
z. B. das Dock) steuert, und die Aktivitaets-Schalter der Remotes.

IR-Geraete (Speicher: kd.data["dock"]["geraete"], im Panel unter "Dockingstation"):
    {"id": "tv_wz", "name": "TV Wohnzimmer", "vorlage": "sony_bravia" | "logitech_z906" | "leer",
     "sender": "infrared.dock_wz_ir_sender", "befehle": {Name: IR-Schritt}}
Befehle = Vorlage + eigene (eigene ueberschreiben). Namen wie bei Harmony (PowerOn, VolumeUp,
InputNext, DirectionUp, Select ...), damit Skripte von der Harmony einfach umziehen.
Daraus entstehen je Geraet ein Media-Player, eine Remote-Entitaet (remote.send_command mit den
Befehlsnamen) und je Befehl ein Button.

Aktivitaets-Schalter: je Remote und Aktivitaet ein switch. An = diese Aktivitaet laeuft auf der Remote
(Sensor "Aktivitaet" der Firmware: Index, -1 = keine). Einschalten startet sie auf der Remote, Ausschalten
beendet sie (Aktion esphome.<remote>_aktivitaet_setzen). Ist die Remote gerade nicht erreichbar, wird der
Wunsch beim naechsten Verbinden nachgeholt.
"""

from __future__ import annotations

import asyncio
import hashlib
import json
import logging
import re
from typing import Any

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.const import STATE_UNAVAILABLE, STATE_UNKNOWN
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError, ServiceValidationError
from homeassistant.helpers import discovery, entity_registry as er
from homeassistant.helpers.event import async_track_state_change_event

from .ir_kodierer import PROTOKOLLE, kodieren

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"
PLATTFORMEN = ("switch", "media_player", "remote", "button", "select", "sensor")


# ---------------------------------------------------------------------------------------------- Vorlagen
def _s(a: int, c: int, p: str = "SIRC") -> dict:
    return {"t": "ir", "proto": p, "addr": a, "cmd": c}


def _z906(c: int) -> dict:
    # Flipper "NECext 02 A0 / cc ~cc": 16-Bit-Adresse 0xA002, Kommando mit Komplement
    return {"t": "ir", "proto": "NECext", "addr": 0xA002, "cmd": c | ((~c & 0xFF) << 8)}


# Sony Bravia: Codes der Fernbedienung RMF-TX500U (Flipper-IRDB, passt zu KD-xxXG95) + diskretes Ein/Aus
_SONY = {
    "PowerToggle": _s(1, 0x15), "PowerOn": _s(1, 0x2E), "PowerOff": _s(1, 0x2F),
    "VolumeUp": _s(1, 0x12), "VolumeDown": _s(1, 0x13), "Mute": _s(1, 0x14),
    "ChannelUp": _s(1, 0x10), "ChannelDown": _s(1, 0x11), "ChannelPrev": _s(1, 0x3B),
    "InputNext": _s(1, 0x25), "InputTv": _s(1, 0x24),
    "DirectionUp": _s(1, 0x74), "DirectionDown": _s(1, 0x75), "DirectionLeft": _s(1, 0x34),
    "DirectionRight": _s(1, 0x33), "Select": _s(1, 0x65), "Enter": _s(1, 0x0B),
    "Home": _s(1, 0x60), "Return": _s(0x97, 0x23, "SIRC15"), "Exit": _s(1, 0x63),
    "Options": _s(0xC4, 0x4B, "SIRC15"), "Apps": _s(0xC4, 0x2A, "SIRC15"), "Guide": _s(0xA4, 0x5B, "SIRC15"),
    "Help": _s(0xC4, 0x4D, "SIRC15"), "Netflix": _s(0x1A, 0x7C, "SIRC15"), "GooglePlay": _s(0xC4, 0x46, "SIRC15"),
    "Info": _s(1, 0x3A), "Audio": _s(1, 0x17), "Subtitle": _s(0x97, 0x28, "SIRC15"), "Teletext": _s(1, 0x3F),
    "Sleep": _s(1, 0x36),
    "Play": _s(0x97, 0x1A, "SIRC15"), "Pause": _s(0x97, 0x19, "SIRC15"), "Stop": _s(0x97, 0x18, "SIRC15"),
    "FastForward": _s(0x97, 0x1C, "SIRC15"), "Rewind": _s(0x97, 0x1B, "SIRC15"),
    "SkipForward": _s(0x97, 0x3D, "SIRC15"), "SkipBack": _s(0x97, 0x3C, "SIRC15"), "Record": _s(0x97, 0x20, "SIRC15"),
    "Red": _s(0x97, 0x25, "SIRC15"), "Green": _s(0x97, 0x26, "SIRC15"), "Yellow": _s(0x97, 0x27, "SIRC15"),
    "Blue": _s(0x97, 0x24, "SIRC15"),
    **{str(i): _s(1, 0x09 if i == 0 else i - 1) for i in range(10)},
}

_Z906 = {
    "PowerToggle": _z906(0x80), "VolumeUp": _z906(0xAA), "VolumeDown": _z906(0x6A), "Mute": _z906(0xEA),
    "InputNext": _z906(0x08), "Effect": _z906(0x0E), "Level": _z906(0x0A), "Test": _z906(0x01),
    "6CHDirect": _z906(0x04), "InputRca": _z906(0x82), "InputOptical1": _z906(0x0C),
    "InputOptical2": _z906(0x8C), "InputCoax": _z906(0x02), "InputAux": _z906(0x42),
}

VORLAGEN: dict[str, dict] = {
    "sony_bravia": {"name": "Sony Bravia (Fernseher)", "art": "tv", "befehle": _SONY,
                    "quellen": {"TV": "InputTv"}},
    "logitech_z906": {"name": "Logitech Z906 (Lautsprecher)", "art": "speaker", "befehle": _Z906,
                      "quellen": {"6-Kanal direkt": "6CHDirect", "Cinch": "InputRca", "Optisch 1": "InputOptical1",
                                  "Optisch 2": "InputOptical2", "Koaxial": "InputCoax", "AUX": "InputAux"}},
    "leer": {"name": "Eigenes Gerät (nur eigene Befehle)", "art": "tv", "befehle": {}, "quellen": {}},
}

# Anzeigenamen und Symbole der Harmony-Befehle (Buttons); unbekannte Namen bleiben wie sie sind
NAMEN = {
    "PowerToggle": ("Ein/Aus", "mdi:power"), "PowerOn": ("Einschalten", "mdi:power-on"),
    "PowerOff": ("Ausschalten", "mdi:power-off"), "VolumeUp": ("Lauter", "mdi:volume-plus"),
    "VolumeDown": ("Leiser", "mdi:volume-minus"), "Mute": ("Stumm", "mdi:volume-mute"),
    "ChannelUp": ("Kanal hoch", "mdi:chevron-up-box"), "ChannelDown": ("Kanal runter", "mdi:chevron-down-box"),
    "ChannelPrev": ("Letzter Kanal", "mdi:history"), "InputNext": ("Eingang wechseln", "mdi:import"),
    "InputTv": ("TV", "mdi:television-classic"), "DirectionUp": ("Hoch", "mdi:arrow-up"),
    "DirectionDown": ("Runter", "mdi:arrow-down"), "DirectionLeft": ("Links", "mdi:arrow-left"),
    "DirectionRight": ("Rechts", "mdi:arrow-right"), "Select": ("OK", "mdi:checkbox-blank-circle"),
    "Enter": ("Enter", "mdi:keyboard-return"), "Home": ("Home", "mdi:home"), "Return": ("Zurück", "mdi:undo"),
    "Exit": ("Exit", "mdi:exit-to-app"), "Options": ("Optionen", "mdi:dots-vertical"), "Apps": ("Apps", "mdi:apps"),
    "Guide": ("Programmführer", "mdi:television-guide"), "Help": ("Hilfe", "mdi:help-circle-outline"),
    "Netflix": ("Netflix", "mdi:netflix"), "GooglePlay": ("Google Play", "mdi:google-play"),
    "Info": ("Info", "mdi:information-outline"), "Audio": ("Audio", "mdi:music-note"),
    "Subtitle": ("Untertitel", "mdi:subtitles-outline"), "Teletext": ("Videotext", "mdi:text-box-outline"),
    "Sleep": ("Sleep", "mdi:sleep"), "Play": ("Play", "mdi:play"), "Pause": ("Pause", "mdi:pause"),
    "Stop": ("Stopp", "mdi:stop"), "FastForward": ("Vorspulen", "mdi:fast-forward"),
    "Rewind": ("Zurückspulen", "mdi:rewind"), "SkipForward": ("Weiter", "mdi:skip-next"),
    "SkipBack": ("Zurück (Titel)", "mdi:skip-previous"), "Record": ("Aufnahme", "mdi:record"),
    "Red": ("Rot", "mdi:circle"), "Green": ("Grün", "mdi:circle"), "Yellow": ("Gelb", "mdi:circle"),
    "Blue": ("Blau", "mdi:circle"), "Effect": ("Effekt", "mdi:surround-sound"),
    "Level": ("Lautsprecher-Pegel", "mdi:tune-vertical"), "Test": ("Lautsprechertest", "mdi:speaker-wireless"),
    "6CHDirect": ("6-Kanal direkt", "mdi:numeric-1-box"), "InputRca": ("Cinch", "mdi:numeric-2-box"),
    "InputOptical1": ("Optisch 1", "mdi:numeric-3-box"), "InputOptical2": ("Optisch 2", "mdi:numeric-4-box"),
    "InputCoax": ("Koaxial", "mdi:numeric-5-box"), "InputAux": ("AUX", "mdi:audio-input-stereo-minijack"),
    **{str(i): (f"Ziffer {i}", f"mdi:numeric-{i}") for i in range(10)},
}


def befehle(g: dict) -> dict[str, dict]:
    return {**VORLAGEN.get(g.get("vorlage"), VORLAGEN["leer"])["befehle"], **(g.get("befehle") or {})}


def quellen(g: dict) -> dict[str, str]:
    """Quellen des Media-Players: aus der Vorlage, sonst eigene Befehle "Input..." (ausser InputNext)."""
    q = dict(VORLAGEN.get(g.get("vorlage"), VORLAGEN["leer"])["quellen"])
    for n in g.get("befehle") or {}:
        if n.startswith("Input") and n != "InputNext" and n not in q.values():
            q[NAMEN.get(n, (n[5:] or n,))[0]] = n
    return q


# ---------------------------------------------------------------------------------------------- Pruefen
def validate_dock(d: Any) -> dict:
    from . import validate_ir_step

    if not isinstance(d, dict):
        raise vol.Invalid("Dock-Konfiguration muss ein Objekt sein")
    out, ids = [], set()
    for g in d.get("geraete") or []:
        gid = str(g.get("id") or "").strip()
        if not re.fullmatch(r"[a-z0-9_]{1,32}", gid) or gid in ids:
            raise vol.Invalid(f"Geräte-ID fehlt, ungültig oder doppelt: '{gid}'")
        ids.add(gid)
        name = str(g.get("name") or gid).strip()[:32]
        vorlage = g.get("vorlage") if g.get("vorlage") in VORLAGEN else "leer"
        sender = str(g.get("sender") or "")
        if not sender.startswith("infrared."):
            raise vol.Invalid(f"{name}: IR-Sender fehlt")
        bf = {}
        for n, st in (g.get("befehle") or {}).items():
            n = str(n).strip()[:32]
            if not n:
                continue
            st = validate_ir_step({**st, "t": "ir"}, f"{name} / {n}")
            st.pop("r", None)
            st.pop("d", None)
            if "proto" in st and st["proto"] not in PROTOKOLLE:
                raise vol.Invalid(f"{name} / {n}: Protokoll {st['proto']} kann Home Assistant nicht senden")
            bf[n] = st
        if len(bf) > 150:
            raise vol.Invalid(f"{name}: höchstens 150 eigene Befehle")
        out.append({"id": gid, "name": name, "vorlage": vorlage, "sender": sender, "befehle": bf})
    if len(out) > 24:
        raise vol.Invalid("höchstens 24 IR-Geräte")
    return {"geraete": out}


# ---------------------------------------------------------------------------------------------- Senden
class RohBefehl:
    """Fertige Rohzeiten als infrared-Command (gleiche Schnittstelle wie infrared_protocols.commands.Command)."""

    def __init__(self, zeiten: list[int], f: int) -> None:
        self.modulation = f
        self.repeat_count = 0
        self._z = zeiten

    def get_raw_timings(self) -> list[int]:
        return list(self._z)


async def ir_senden(hass: HomeAssistant, sender: str, schritt: dict, context=None) -> None:
    from homeassistant.components.infrared import async_send_command

    try:
        zeiten, f = kodieren(schritt)
    except ValueError as err:
        raise ServiceValidationError(str(err)) from err
    await async_send_command(hass, sender, RohBefehl(zeiten, f), context=context)


# ---------------------------------------------------------------------------------------------- Verwalter
class Verwalter:
    """Haelt die Entitaeten passend zur Konfiguration (Aktivitaeten der Remotes, IR-Geraete)."""

    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.add: dict[str, Any] = {}
        self.ents: dict[str, Any] = {}
        self.sig: dict[str, str] = {}
        self.zust: dict[str, dict] = {}   # Geraete-ID -> {"an": bool|None, "quelle": str|None}
        self._lock = asyncio.Lock()

    @property
    def geraete(self) -> list[dict]:
        return self.kd.data.setdefault("dock", {"geraete": []})["geraete"]

    async def speichern(self, d: dict) -> None:
        self.kd.data["dock"] = validate_dock(d)
        await self.kd.store.async_save(self.kd.data)
        await self.sync()

    def plattform(self, p: str, add) -> None:
        self.add[p] = add
        if len(self.add) == len(PLATTFORMEN):
            self.hass.async_create_task(self.sync())

    def _soll(self) -> dict[str, tuple[str, str, Any]]:
        from .button import IrButton
        from .jetzt import JetztPlayer
        from .media_player import IrMediaPlayer
        from .remote import IrRemote
        from .select import AktAuswahl
        from .switch import AktSchalter

        soll: dict[str, tuple[str, str, Any]] = {}
        for dev in self.kd.devices:
            acts = (self.kd.entry(dev).get("config") or {}).get("activities") or []
            for i, a in enumerate(acts):
                uid = f"{dev}_akt_{i}"
                soll[uid] = ("switch", a.get("name") or f"Aktivität {i + 1}",
                             lambda dev=dev, i=i, uid=uid: AktSchalter(self, uid, dev, i))
            # Auswahl (Dropdown) mit allen Aktivitaeten; neu gebaut, wenn sich die Namen aendern (2026-10-18)
            namen = [a.get("name") or f"Aktivität {i + 1}" for i, a in enumerate(acts)]
            if namen:
                soll[f"{dev}_akt_auswahl"] = ("select", "|".join(namen),
                                              lambda dev=dev, namen=namen: AktAuswahl(self, f"{dev}_akt_auswahl", dev, namen))
        # Streamer (streamer.py): Sensoren immer, Knoepfe nur mit ADB-Entitaet
        from .streamer import SENSOREN, StreamerKnopf, StreamerSensor
        for st in self.kd.data.get("streamer") or []:
            sig = st["name"]
            for k in SENSOREN:
                if k != "am_tv" or st["adb"]:
                    soll[f"streamer_{st['id']}_{k}"] = ("sensor", sig, lambda st=st, k=k: StreamerSensor(st, k))
            if st["adb"]:
                for k in ("app_beenden", "neustart"):
                    soll[f"streamer_{st['id']}_{k}"] = ("button", sig, lambda st=st, k=k: StreamerKnopf(st, k))
        # Zusammengefasste "Jetzt laeuft"-Player (jetzt.py, Konfigurator-Seite "Wiedergabe")
        for j in self.kd.data.get("jetzt") or []:
            sig = hashlib.sha1(json.dumps(j, sort_keys=True).encode()).hexdigest()[:12]
            soll[f"jetzt_{j['id']}"] = ("media_player", sig, lambda j=j: JetztPlayer(self.kd, j))
        for g in self.geraete:
            sig = hashlib.sha1(json.dumps(g, sort_keys=True).encode()).hexdigest()[:12]
            gid = g["id"]
            soll[f"ir_{gid}_mp"] = ("media_player", sig, lambda g=g: IrMediaPlayer(self, g))
            soll[f"ir_{gid}_remote"] = ("remote", sig, lambda g=g: IrRemote(self, g))
            for n in befehle(g):
                soll[f"ir_{gid}_b_{n}"] = ("button", sig, lambda g=g, n=n: IrButton(self, g, n))
        return soll

    async def sync(self) -> None:
        if len(self.add) < len(PLATTFORMEN):
            return
        async with self._lock:
            soll = self._soll()
            reg = er.async_get(self.hass)
            for uid in list(self.ents):
                if uid not in soll or (soll[uid][0] != "switch" and self.sig.get(uid) != soll[uid][1]):
                    ent = self.ents.pop(uid)
                    self.sig.pop(uid, None)
                    weg = uid not in soll
                    await ent.async_remove(force_remove=True)
                    if weg and ent.entity_id and reg.async_get(ent.entity_id):
                        reg.async_remove(ent.entity_id)
            neu: dict[str, list] = {p: [] for p in PLATTFORMEN}
            for uid, (p, sig, bauen) in soll.items():
                if uid in self.ents:
                    if p == "switch" and self.sig.get(uid) != sig:   # Aktivitaet umbenannt
                        self.ents[uid].umbenennen(sig)
                        self.sig[uid] = sig
                    continue
                ent = bauen()
                self.ents[uid] = ent
                self.sig[uid] = sig
                neu[p].append(ent)
            for p, ents in neu.items():
                if ents:
                    self.add[p](ents)
            # Verwaiste Registry-Eintraege (geloeschte Geraete/Aktivitaeten aus frueheren Laeufen)
            for e in list(reg.entities.values()):
                if e.platform == DOMAIN and e.unique_id not in soll:
                    reg.async_remove(e.entity_id)


async def starten(hass: HomeAssistant, kd) -> Verwalter:
    v = Verwalter(hass, kd)
    hass.data[DOMAIN + "_dock"] = v
    for p in PLATTFORMEN:
        hass.async_create_task(discovery.async_load_platform(hass, p, DOMAIN, {}, {}))
    from .jetzt import ws_jetzt_get, ws_jetzt_save
    for fn in (ws_dock_get, ws_dock_save, ws_dock_senden, ws_jetzt_get, ws_jetzt_save):
        websocket_api.async_register_command(hass, fn)
    return v


# ---------------------------------------------------------------------------------------------- Websocket
def _sender(hass: HomeAssistant) -> list[dict]:
    try:
        from homeassistant.components.infrared import async_get_emitters

        ids = async_get_emitters(hass)
    except Exception:   # noqa: BLE001 - infrared-Integration fehlt
        ids = []
    out = []
    for e in ids:
        st = hass.states.get(e)
        out.append({"entity_id": e, "name": (st.attributes.get("friendly_name") if st else None) or e,
                    "online": bool(st and st.state != STATE_UNAVAILABLE)})
    return out


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/dock_get"})
@callback
def ws_dock_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    v: Verwalter = hass.data[DOMAIN + "_dock"]
    reg = er.async_get(hass)
    ents = {}
    for g in v.geraete:
        ents[g["id"]] = {k: reg.async_get_entity_id(p, DOMAIN, f"ir_{g['id']}_{k}")
                         for p, k in (("media_player", "mp"), ("remote", "remote"))}
    schalter = {}
    for dev in v.kd.devices:
        acts = (v.kd.entry(dev).get("config") or {}).get("activities") or []
        schalter[dev] = [reg.async_get_entity_id("switch", DOMAIN, f"{dev}_akt_{i}") for i in range(len(acts))]
    connection.send_result(msg["id"], {
        "geraete": v.geraete,
        "vorlagen": {k: {"name": t["name"], "befehle": t["befehle"], "quellen": t["quellen"]} for k, t in VORLAGEN.items()},
        "namen": {k: list(n) for k, n in NAMEN.items()},
        "sender": _sender(hass),
        "protokolle": list(PROTOKOLLE),
        "entitaeten": ents,
        "schalter": schalter,
    })


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/dock_save", vol.Required("dock"): dict})
@websocket_api.async_response
async def ws_dock_save(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    v: Verwalter = hass.data[DOMAIN + "_dock"]
    try:
        await v.speichern(msg["dock"])
    except (vol.Invalid, ValueError, TypeError) as err:
        connection.send_error(msg["id"], "invalid", str(err))
        return
    connection.send_result(msg["id"], {"geraete": v.geraete})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/dock_senden",
                                  vol.Required("sender"): str, vol.Required("schritt"): dict})
@websocket_api.async_response
async def ws_dock_senden(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Testen aus dem Panel: einen IR-Schritt sofort ueber den gewaehlten Sender schicken."""
    try:
        await ir_senden(hass, msg["sender"], msg["schritt"], connection.context(msg))
    except (HomeAssistantError, ValueError, TypeError) as err:
        connection.send_error(msg["id"], "senden", str(err))
        return
    connection.send_result(msg["id"], {})


# ---------------------------------------------------------------------------------------------- Basis IR-Entitaet
class IrBasis:
    """Gemeinsames fuer Media-Player, Remote und Buttons eines IR-Geraets (Mixin vor der HA-Entitaet)."""

    _attr_should_poll = False
    _attr_has_entity_name = False

    def _ir_init(self, v: Verwalter, g: dict) -> None:
        self.v = v
        self.g = g
        self.z = v.zust.setdefault(g["id"], {"an": None, "quelle": None, "hoerer": []})
        self._sender = g["sender"]

    async def async_added_to_hass(self) -> None:
        await super().async_added_to_hass()
        self.z["hoerer"].append(self)
        self.async_on_remove(lambda: self.z["hoerer"].remove(self) if self in self.z["hoerer"] else None)

        @callback
        def sender_neu(ev: Event) -> None:
            self.async_write_ha_state()

        self.async_on_remove(async_track_state_change_event(self.hass, [self._sender], sender_neu))

    @property
    def available(self) -> bool:
        st = self.hass.states.get(self._sender)
        return st is not None and st.state != STATE_UNAVAILABLE

    def _melden(self) -> None:
        for e in list(self.z["hoerer"]):
            if e.hass is not None and e.entity_id:
                e.async_write_ha_state()

    async def senden(self, name: str, wdh: int = 1) -> None:
        bf = befehle(self.g)
        if name not in bf:
            raise ServiceValidationError(f"{self.g['name']}: unbekannter Befehl '{name}'. Möglich: {', '.join(bf)}")
        for i in range(max(1, wdh)):
            if i:
                await asyncio.sleep(0.15)
            await ir_senden(self.hass, self._sender, bf[name], self._context)
        self._zustand_nach(name)

    def _zustand_nach(self, name: str) -> None:
        z = self.z
        if name == "PowerOn":
            z["an"] = True
        elif name == "PowerOff":
            z["an"] = False
        elif name == "PowerToggle" and z["an"] is not None:
            z["an"] = not z["an"]
        else:
            for q, n in quellen(self.g).items():
                if n == name:
                    z["quelle"] = q
        self._melden()

    async def schalten(self, an: bool) -> None:
        bf = befehle(self.g)
        n = "PowerOn" if an else "PowerOff"
        if n in bf:
            await self.senden(n)
        elif "PowerToggle" in bf:
            if self.z["an"] is not an:   # nur Umschalten: senden, wenn der angenommene Zustand abweicht
                await self.senden("PowerToggle")
            self.z["an"] = an
            self._melden()
        else:
            raise ServiceValidationError(f"{self.g['name']}: kein Ein/Aus-Befehl")


def zustand_unbekannt(state) -> bool:
    return state is None or state.state in (STATE_UNAVAILABLE, STATE_UNKNOWN)
