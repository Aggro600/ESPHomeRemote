"""Tasten-Konfigurator fuer die ESPHome-Fernbedienungen (remote-wz-*).

- Seitenleiste "Fernbedienung" (Panel, Handy + PC, auch fuer Nicht-Admins).
- Speichert die Belegung je Remote in .storage/esphomeremote_konfig.
- Liefert die Belegung als JSON an die Remote aus:
    GET /api/esphomeremote_konfig/cfg/<geraet>?k=<schluessel>
- Kuendigt den Stand als Zustand sensor.esphomeremote_konfig_<geraet> (FNV-1a-Hash
  des ausgelieferten Textes) an. Die Remote abonniert ihn, holt bei Abweichung die
  Datei und prueft den Hash des Inhalts.

Konfiguration (packages/esphomeremote_konfig.yaml):
    esphomeremote_konfig:
      key: !secret esphomeremote_konfig_key
      devices: [open-remote]
"""

from __future__ import annotations

import copy
import json
import logging
from pathlib import Path

import voluptuous as vol
from aiohttp import web

from homeassistant.components import panel_custom, websocket_api
from homeassistant.components.http import HomeAssistantView, StaticPathConfig
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.storage import Store

from .defaults import default_config, default_menu, kamera_popup

_LOGGER = logging.getLogger(__name__)

DOMAIN = "esphomeremote_konfig"
STORE_VERSION = 1
PANEL_URL = "fernbedienung"
STATIC_URL = "/esphomeremote_konfig_static"
PANEL_VERSION = "14"  # bei Aenderungen am Panel-JS erhoehen (App-Cache)

MAX_BYTES = 60000
KEYCODES = {2, 3, 4, 5, 11, 13, 14, 15, 21, 22, 23, 24, 25, 31, 32, 33, 34, 35, 41, 42, 43, 44, 45, 102}
GESTURES = ("short", "double", "long")
STEP_TYPES = {"ble", "ha", "int", "ir", "wait"}

CONFIG_SCHEMA = vol.Schema(
    {
        # YAML ist optional: einfacher geht es ueber "Integration hinzufuegen" (config_flow.py)
        vol.Optional(DOMAIN): vol.Schema(
            {
                vol.Required("key"): cv.string,
                vol.Optional("devices", default=["open-remote"]): vol.All(
                    cv.ensure_list, [cv.string]
                ),
            }
        )
    },
    extra=vol.ALLOW_EXTRA,
)


def fnv1a(data: bytes) -> str:
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return f"{h:08x}"


def dev_slug(dev: str) -> str:
    return dev.replace("-", "_").lower()


def validate_config(cfg: dict) -> dict:
    """Prueft die Belegung grob und gibt eine bereinigte Kopie zurueck."""
    if not isinstance(cfg, dict):
        raise vol.Invalid("Konfiguration muss ein Objekt sein")
    acts = cfg.get("activities")
    if not isinstance(acts, list) or not 1 <= len(acts) <= 16:
        raise vol.Invalid("1 bis 16 Aktivitaeten erwartet")
    out_acts = []
    for a in acts:
        keys_out = {}
        for k, kv in (a.get("keys") or {}).items():
            if int(k) not in KEYCODES:
                raise vol.Invalid(f"Unbekannte Taste {k}")
            ko = {}
            for g in GESTURES:
                steps = kv.get(g) or []
                if not isinstance(steps, list) or len(steps) > 10:
                    raise vol.Invalid(f"Taste {k}/{g}: hoechstens 10 Schritte")
                clean = []
                for s in steps:
                    if not isinstance(s, dict) or s.get("t") not in STEP_TYPES:
                        raise vol.Invalid(f"Taste {k}/{g}: unbekannter Schritt {s}")
                    if s["t"] == "ha":
                        if "." not in str(s.get("svc", "")):
                            raise vol.Invalid(f"Taste {k}/{g}: Dienst fehlt")
                        d = s.get("data") or {}
                        if not isinstance(d, dict):
                            raise vol.Invalid(f"Taste {k}/{g}: Daten muessen ein Objekt sein")
                        # Die Remote reicht alle Werte als Text an HA weiter (HA wandelt selbst).
                        s = {**s, "data": {str(x): (y if isinstance(y, str) else json.dumps(y)) for x, y in d.items()}}
                    clean.append(s)
                if clean:
                    ko[g] = clean
            if kv.get("hold") and ko.get("short") and not ko.get("double") and not ko.get("long"):
                ko["hold"] = True
            # "Beim Gedrueckthalten: Kurz wiederholen" (ms) - schliesst Halten am Geraet aus
            if kv.get("repeat") and ko.get("short") and not ko.get("hold"):
                ko["repeat"] = max(50, min(2000, int(kv["repeat"])))
            if ko:
                keys_out[str(int(k))] = ko
        out_acts.append(
            {
                "name": str(a.get("name", ""))[:24],
                "slot": max(0, min(3, int(a.get("slot", 0)))),
                # In der Leiste der Startseite (hoechstens 4); fehlt es: die ersten vier
                "dock": bool(a.get("dock", len(out_acts) < 4)),
                "keys": keys_out,
            }
        )
    if sum(1 for a in out_acts if a["dock"]) > 4:
        raise vol.Invalid("Hoechstens 4 Aktivitaeten passen in die Leiste")
    out = {"v": 1, "activities": out_acts}
    if "menu" in cfg:
        out["menu"] = validate_menu(cfg["menu"])
    if isinstance(cfg.get("popups"), dict):
        out["popups"] = {
            str(k)[:24]: {"on": bool(v.get("on", True)), **({"page": str(v["page"])[:40]} if v.get("page") else {})}
            for k, v in cfg["popups"].items() if isinstance(v, dict)
        }
    if isinstance(cfg.get("home"), dict):
        out["home"] = validate_home(cfg["home"])
    if "slots" in cfg and isinstance(cfg["slots"], list):
        out["slots"] = [str(x)[:24] for x in cfg["slots"]][:4]
    return out


HOME_AREAS = ("status", "medien", "menu", "apps", "dock")


def validate_home(h: dict) -> dict:
    """Startseite: Bereiche im Raster 6 x 8 (Zellen 40 px), ohne Ueberlappung."""
    areas = {}
    for k in HOME_AREAS:
        b = (h.get("areas") or {}).get(k)
        if not isinstance(b, dict):
            continue
        x, y = max(0, min(5, int(b.get("x", 0)))), max(0, min(7, int(b.get("y", 0))))
        w, hh = max(1, min(6 - x, int(b.get("w", 1)))), max(1, min(8 - y, int(b.get("h", 1))))
        if k == "status":
            x, y, w, hh = 0, 0, 6, 1
        areas[k] = {"x": x, "y": y, "w": w, "h": hh, "on": bool(b.get("on", True))}
    an = [(k, b) for k, b in areas.items() if b["on"]]
    for i, (k1, a) in enumerate(an):
        for k2, b in an[i + 1:]:
            if not (a["x"] + a["w"] <= b["x"] or b["x"] + b["w"] <= a["x"] or a["y"] + a["h"] <= b["y"] or b["y"] + b["h"] <= a["y"]):
                raise vol.Invalid(f"Startseite: {k1} und {k2} ueberlappen sich")
    return {"custom": bool(h.get("custom")), "areas": areas}


ITEM_TYPES = {"text", "page", "special", "action", "toggle", "light", "cover", "sensor", "camera", "setting"}


def validate_menu(m: dict) -> dict:
    """Konfigurierbares Menue (Raster 6 Spalten x 40 px)."""
    if not isinstance(m, dict):
        raise vol.Invalid("menu muss ein Objekt sein")
    pages = m.get("pages") or []
    if not isinstance(pages, list) or len(pages) > 60:
        raise vol.Invalid("hoechstens 60 Menueseiten")
    ids = set()
    out_pages = []
    for p in pages:
        pid = str(p.get("id", "")).strip()
        if not pid or pid in ids:
            raise vol.Invalid(f"Seiten-ID fehlt oder doppelt: '{pid}'")
        ids.add(pid)
        items = []
        for it in (p.get("items") or [])[:80]:
            t = it.get("t")
            if t not in ITEM_TYPES:
                raise vol.Invalid(f"Seite {pid}: unbekannter Eintragstyp {t}")
            h = int(it.get("h", 1))
            # h = 0: Hoehe nach Textlaenge (Hinweise/Erklaerungen), sonst 1..8 Zeilen
            o = {"t": t, "w": max(1, min(6, int(it.get("w", 6)))), "h": 0 if h <= 0 and t in ("text", "setting") else max(1, min(8, h))}
            for k in ("label", "icon", "entity", "target", "page", "src"):
                if it.get(k):
                    o[k] = str(it[k])[:120]
            if "x" in it:
                o["x"] = max(0, min(5, int(it["x"])))
            if "y" in it:
                o["y"] = max(0, min(127, int(it["y"])))
            if it.get("remember"):
                o["remember"] = True
            def schritte(lst):
                return validate_config({"activities": [{"keys": {"2": {"short": lst}}}]})["activities"][0]["keys"].get("2", {}).get("short", [])
            if it.get("steps"):
                o["steps"] = schritte(it["steps"])
            # Halte-Knopf: steps beim Druecken, release beim Loslassen
            if it.get("hold"):
                o["hold"] = True
                if it.get("release"):
                    o["release"] = schritte(it["release"])
            # Tastenmodus: Hardware-Taste, die diesem Eintrag folgt (jede ausser Zurueck)
            if it.get("key") and int(it["key"]) in KEYCODES and int(it["key"]) != 13:
                o["key"] = int(it["key"])
            items.append(o)
        pg = {
            "id": pid,
            "title": str(p.get("title", pid))[:40],
            "remember": bool(p.get("remember", True)),
            "flow": bool(p.get("flow", True)),
            "items": items,
        }
        pp = p.get("popup")
        if isinstance(pp, dict):
            def trig(lst):
                out = []
                for t in (lst or [])[:12]:
                    if isinstance(t, dict) and t.get("entity") and "to" in t:
                        o = {"entity": str(t["entity"])[:80], "to": str(t["to"])[:80]}
                        if t.get("attr"):
                            o["attr"] = str(t["attr"])[:40]
                        out.append(o)
                return out
            pg["popup"] = {"open": trig(pp.get("open")), "close": trig(pp.get("close")),
                           "timeout": max(0, min(3600, int(pp.get("timeout") or 0))), "wake": bool(pp.get("wake", True))}
        out_pages.append(pg)
    for p in out_pages:
        for it in p["items"]:
            if it["t"] == "page" and it.get("target") not in ids:
                raise vol.Invalid(f"Seite {p['id']}: Ziel '{it.get('target')}' gibt es nicht")
    start = str(m.get("start") or (out_pages[0]["id"] if out_pages else ""))
    # "on" bleibt fuer aeltere Firmware im Text, ist aber immer an (Konfigurator-Menue ersetzt das eingebaute)
    return {"on": True, "start": start if start in ids else (out_pages[0]["id"] if out_pages else ""), "pages": out_pages}


def device_payload(cfg: dict) -> bytes:
    """Kompakter Text, den die Remote laedt (ohne Leerzeichen)."""
    return json.dumps(cfg, separators=(",", ":"), ensure_ascii=False).encode()


class KonfigData:
    def __init__(self, hass: HomeAssistant, key: str, devices: list[str]) -> None:
        self.hass = hass
        self.key = key
        self.devices = list(devices)
        self.store = Store(hass, STORE_VERSION, DOMAIN)
        self.data: dict = {"devices": {}}

    async def load(self) -> None:
        self.data = await self.store.async_load() or {"devices": {}}
        self.data.setdefault("devices", {})
        # Im Panel angelegte Remotes ("Neue Fernbedienung") kommen zu denen aus der YAML dazu
        for dev in self.data.setdefault("extra", []):
            if dev not in self.devices:
                self.devices.append(dev)
        for dev in self.devices:
            if dev not in self.data["devices"]:
                self.data["devices"][dev] = {"config": default_config(), "enabled": False}
            # Menue kam 2026-10-03 dazu: aeltere Staende ergaenzen (aus = eingebautes Menue)
            self.data["devices"][dev]["config"].setdefault("menu", default_menu())
            pages = self.data["devices"][dev]["config"]["menu"].setdefault("pages", [])
            if not any(p.get("id") == "kameras" for p in pages):
                pages.append(kamera_popup())
        for dev in self.devices:
            self.announce(dev)

    async def geraet_dazu(self, dev: str, vorlage: str | None = None) -> None:
        """Neue Remote aufnehmen; Belegung als Kopie einer vorhandenen (oder Standard)."""
        if dev not in self.devices:
            self.devices.append(dev)
        if dev not in self.data["extra"]:
            self.data["extra"].append(dev)
        if vorlage in self.data["devices"]:
            src = self.data["devices"][vorlage]
            self.data["devices"][dev] = {"config": copy.deepcopy(src["config"]), "enabled": bool(src.get("enabled"))}
        else:
            self.data["devices"][dev] = {"config": default_config(), "enabled": False}
            self.data["devices"][dev]["config"]["menu"] = default_menu()
        await self.store.async_save(self.data)
        self.announce(dev)

    def entfernbar(self, dev: str) -> bool:
        """Nur im Panel angelegte Remotes (nicht die aus der YAML, nicht die Vorlage Sabrina)."""
        return dev in self.data.get("extra", []) and dev != "remote-wz-sabrina"

    async def geraet_weg(self, dev: str) -> None:
        self.devices = [d for d in self.devices if d != dev]
        self.data["extra"] = [d for d in self.data.get("extra", []) if d != dev]
        self.data["devices"].pop(dev, None)
        await self.store.async_save(self.data)
        self.hass.states.async_remove(f"sensor.esphomeremote_konfig_{dev_slug(dev)}")

    def entry(self, dev: str) -> dict:
        return self.data["devices"][dev]

    def payload(self, dev: str) -> bytes:
        e = self.entry(dev)
        cfg = copy.deepcopy(e["config"])
        # "aus" = die Remote nutzt ihre eingebaute Belegung
        cfg["on"] = bool(e.get("enabled"))
        return device_payload(cfg)

    @callback
    def announce(self, dev: str) -> None:
        body = self.payload(dev)
        e = self.entry(dev)
        self.hass.states.async_set(
            f"sensor.esphomeremote_konfig_{dev_slug(dev)}",
            fnv1a(body),
            {
                "friendly_name": f"Tasten-Konfiguration {dev}",
                "icon": "mdi:remote",
                "bytes": len(body),
                "enabled": bool(e.get("enabled")),
                "saved_by": e.get("saved_by"),
                "saved_at": e.get("saved_at"),
            },
        )

    async def save(self, dev: str, cfg: dict, enabled: bool, user: str | None) -> str:
        clean = validate_config(cfg)
        e = self.entry(dev)
        e["config"] = clean
        e["enabled"] = bool(enabled)
        e["saved_by"] = user
        from homeassistant.util import dt as dt_util

        e["saved_at"] = dt_util.now().isoformat(timespec="seconds")
        body = self.payload(dev)
        if len(body) > MAX_BYTES:
            raise vol.Invalid(f"Belegung zu gross ({len(body)} Byte, max. {MAX_BYTES})")
        await self.store.async_save(self.data)
        self.announce(dev)
        return fnv1a(body)


class CfgView(HomeAssistantView):
    """Auslieferung an die Remote. Kein HA-Login (die Remote hat keinen Token),
    stattdessen ein gemeinsamer Schluessel aus secrets.yaml."""

    url = "/api/esphomeremote_konfig/cfg/{dev}"
    name = "api:esphomeremote_konfig:cfg"
    requires_auth = False

    def __init__(self, kd: KonfigData) -> None:
        self.kd = kd

    async def get(self, request: web.Request, dev: str) -> web.Response:
        if request.query.get("k") != self.kd.key:
            return web.Response(status=403)
        if dev not in self.kd.devices:
            return web.Response(status=404)
        body = self.kd.payload(dev)
        return web.Response(
            body=body,
            content_type="application/json",
            headers={"X-Konfig-Hash": fnv1a(body), "Cache-Control": "no-store"},
        )


class StatesView(HomeAssistantView):
    """Zustaende fuer die Menue-Kacheln der Remote (statt Dauer-Abos ueber die ESPHome-API,
    die auf der Remote internes RAM kosten). GET .../states?k=<key>&e=a,b,c
    Antwort: {"entity": [state, brightness 0-255|-1, current_position|-1, unit]}"""

    url = "/api/esphomeremote_konfig/states"
    name = "api:esphomeremote_konfig:states"
    requires_auth = False

    def __init__(self, kd: KonfigData) -> None:
        self.kd = kd

    async def get(self, request: web.Request) -> web.Response:
        if request.query.get("k") != self.kd.key:
            return web.Response(status=403)
        hass = request.app["hass"]
        out = {}
        for e in request.query.get("e", "").split(",")[:60]:
            e = e.strip()
            st = hass.states.get(e) if e else None
            if st is None:
                continue
            a = st.attributes
            bri = a.get("brightness")
            pos = a.get("current_position")
            out[e] = [st.state, int(bri) if isinstance(bri, (int, float)) else -1,
                      int(pos) if isinstance(pos, (int, float)) else -1, str(a.get("unit_of_measurement") or "")]
        return web.Response(body=json.dumps(out, separators=(",", ":"), ensure_ascii=False).encode(),
                            content_type="application/json", headers={"Cache-Control": "no-store"})


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    if DOMAIN in config:
        await _starten(hass, config[DOMAIN]["key"], config[DOMAIN]["devices"])
    return True


async def async_setup_entry(hass: HomeAssistant, entry) -> bool:
    """Einrichtung ueber die Oberflaeche (Schluessel + Geraete aus dem Dialog)."""
    if DOMAIN in hass.data:   # schon per YAML eingerichtet
        return True
    await _starten(hass, entry.data["key"], entry.options.get("devices", entry.data["devices"]))

    async def geaendert(hass: HomeAssistant, e) -> None:
        kd: KonfigData = hass.data[DOMAIN]
        for dev in e.options.get("devices", []):
            if dev not in kd.devices:
                await kd.geraet_dazu(dev)

    entry.async_on_unload(entry.add_update_listener(geaendert))
    return True


async def async_unload_entry(hass: HomeAssistant, entry) -> bool:
    return True   # Panel und Adressen bleiben bis zum Neustart (HA kann Views nicht abmelden)


async def _starten(hass: HomeAssistant, key: str, devices: list[str]) -> None:
    kd = KonfigData(hass, key, devices)
    await kd.load()
    hass.data[DOMAIN] = kd

    hass.http.register_view(CfgView(kd))
    hass.http.register_view(StatesView(kd))
    await hass.http.async_register_static_paths(
        [StaticPathConfig(STATIC_URL, str(Path(__file__).parent / "frontend"), False)]
    )
    await panel_custom.async_register_panel(
        hass,
        frontend_url_path=PANEL_URL,
        webcomponent_name="esphomeremote-konfigurator",
        sidebar_title="Fernbedienung",
        sidebar_icon="mdi:remote",
        module_url=f"{STATIC_URL}/konfigurator.js?v={PANEL_VERSION}",
        embed_iframe=False,
        require_admin=False,
    )

    websocket_api.async_register_command(hass, ws_get)
    websocket_api.async_register_command(hass, ws_save)
    websocket_api.async_register_command(hass, ws_default)
    from .einrichten import registrieren

    registrieren(hass, kd)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/get"})
@callback
def ws_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    kd: KonfigData = hass.data[DOMAIN]
    out = {}
    for dev in kd.devices:
        e = kd.entry(dev)
        out[dev] = {
            "config": e["config"],
            "enabled": bool(e.get("enabled")),
            "hash": fnv1a(kd.payload(dev)),
            "saved_by": e.get("saved_by"),
            "saved_at": e.get("saved_at"),
            "slug": dev_slug(dev),
            "entfernbar": kd.entfernbar(dev),
        }
    connection.send_result(msg["id"], {"devices": out, "order": kd.devices})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/default"})
@callback
def ws_default(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    connection.send_result(msg["id"], {"config": default_config()})


@websocket_api.websocket_command(
    {
        vol.Required("type"): "esphomeremote_konfig/save",
        vol.Required("device"): str,
        vol.Required("config"): dict,
        vol.Optional("enabled", default=True): bool,
    }
)
@websocket_api.async_response
async def ws_save(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    kd: KonfigData = hass.data[DOMAIN]
    dev = msg["device"]
    if dev not in kd.devices:
        connection.send_error(msg["id"], "unknown_device", f"Unbekannte Remote {dev}")
        return
    user = connection.user.name if connection.user else None
    try:
        h = await kd.save(dev, msg["config"], msg["enabled"], user)
    except (vol.Invalid, ValueError, TypeError) as err:
        connection.send_error(msg["id"], "invalid", str(err))
        return
    connection.send_result(msg["id"], {"hash": h})
