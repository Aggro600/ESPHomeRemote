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

import asyncio
import copy
import hmac
import json
import logging
import re
from pathlib import Path

import voluptuous as vol
from aiohttp import web

from homeassistant.components import panel_custom, websocket_api
from homeassistant.components.http import HomeAssistantView, StaticPathConfig
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers import config_validation as cv
from homeassistant.helpers.storage import Store
from homeassistant.util import dt as dt_util

from .defaults import default_config, default_menu
from .einstellungen import EINST_SEITEN

_LOGGER = logging.getLogger(__name__)

DOMAIN = "esphomeremote_konfig"
STORE_VERSION = 1
PANEL_URL = "fernbedienung"
STATIC_URL = "/esphomeremote_konfig_static"
PANEL_VERSION = "63"  # bei Aenderungen am Panel-JS erhoehen (App-Cache)

MAX_BYTES = 90000   # Remote: tasten_konfig MAX_BYTES (96 KB), Puffer im PSRAM
KEYCODES = {2, 3, 4, 5, 11, 13, 14, 15, 21, 22, 23, 24, 25, 31, 32, 33, 34, 35, 41, 42, 43, 44, 45, 102,
            900, 901}   # 900/901: keine Tasten - Schritte beim Starten/Beenden der Aktivitaet
GESTURES = ("short", "double", "long")
STEP_TYPES = {"ble", "ha", "int", "ir", "rf", "wait", "modus"}

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
        keys_out = validate_keys(a.get("keys") or {}, "")
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
    if isinstance(cfg.get("home"), dict):
        out["home"] = validate_home(cfg["home"])
    if isinstance(cfg.get("ir"), dict):
        out["ir"] = validate_ir(cfg["ir"])
    if isinstance(cfg.get("bt"), dict):
        # Bluetooth-Proxys, ueber die HA die Remote erreichen darf (zusaetzlich zu den festen proxy_macs der Firmware)
        macs = []
        for m in cfg["bt"].get("proxies") or []:
            m = str(m).strip().upper()
            if not re.fullmatch(r"([0-9A-F]{2}:){5}[0-9A-F]{2}", m):
                raise vol.Invalid(f"Bluetooth-Adresse ungültig: {m}")
            if m not in macs:
                macs.append(m)
        out["bt"] = {"proxies": macs[:12]}
    if "slots" in cfg and isinstance(cfg["slots"], list):
        out["slots"] = [str(x)[:24] for x in cfg["slots"]][:4]
    if isinstance(cfg.get("modi"), list) and cfg["modi"]:
        out["modi"] = validate_modi(cfg["modi"])
    if isinstance(cfg.get("kameras"), list):
        # Kameras fuer die Seiten "Kamerabild" und "Live-Bild" der Remote: Name + camera.*-Entitaet
        kam = []
        for k in cfg["kameras"][:10]:
            e = str((k or {}).get("e") or "").strip()
            if not re.fullmatch(r"camera\.[a-z0-9_]+", e):
                raise vol.Invalid(f"Kamera: '{e}' ist keine camera.-Entität")
            kam.append({"n": str(k.get("n") or e.split(".", 1)[1])[:24], "e": e})
        out["kameras"] = kam
    if isinstance(cfg.get("gesten"), dict):
        out["gesten"] = validate_gesten(cfg["gesten"])
    if isinstance(cfg.get("makros"), list):
        out["makros"] = validate_makros(cfg["makros"])
    return out


GESTEN_FEST = ("schuetteln", "kipp_l", "kipp_r")


def _gesten_taste(o: dict) -> dict:
    """k = wirkt wie ein kurzer Druck dieser Taste (an der Remote waehlbar, 2026-10-17)."""
    try:
        k = int(o.get("k") or 0)
    except (TypeError, ValueError):
        k = 0
    return {"k": k} if k in KEYCODES and k < 900 else {}


def validate_gesten(g: dict) -> dict:
    """Bewegungsgesten (2026-10-17): feste (Schuetteln, Kippen links/rechts) und eigene, aufgenommene.
    Eigene: v = bis zu 4 Vorlagen, je 96 Werte (32 Punkte x X/Y/Z, -127..127) als Hex-Text (192 Zeichen)."""
    out = {}
    for k in GESTEN_FEST:
        if isinstance(g.get(k), dict):
            out[k] = {"an": bool(g[k].get("an", True)), "s": validate_steps(g[k].get("s") or [], f"Geste {k}"),
                      **_gesten_taste(g[k])}
    eigen = []
    for e in (g.get("eigen") or [])[:8]:
        if not isinstance(e, dict):
            continue
        vs = [str(v) for v in (e.get("v") or []) if re.fullmatch(r"[0-9a-f]{192}", str(v))][:4]
        eid = str(e.get("id") or "")
        if not re.fullmatch(r"[a-z0-9_]{1,16}", eid):
            raise vol.Invalid("Geste: Kennung ungueltig")
        eigen.append({"id": eid, "n": str(e.get("n") or eid)[:24], "an": bool(e.get("an", True)), "v": vs,
                      **({"f": 2} if e.get("f") == 2 else {}),
                      "s": validate_steps(e.get("s") or [], f"Geste {e.get('n')}"), **_gesten_taste(e)})
    if eigen:
        out["eigen"] = eigen
    return out


def validate_makros(m: list) -> list:
    """Makros der Bildschirmtastatur/des Touchpads: Text tippen oder Tastenkombination (Modifier, HID-Code)."""
    out = []
    for x in m[:16]:
        if not isinstance(x, dict):
            continue
        n = str(x.get("n") or "")[:20]
        if isinstance(x.get("k"), list) and len(x["k"]) == 2:
            out.append({"n": n, "k": [max(0, min(255, int(x["k"][0]))), max(0, min(255, int(x["k"][1])))]})
        elif x.get("t"):
            out.append({"n": n, "t": str(x["t"])[:200]})
    return out


def validate_keys(keys: dict, wo: str) -> dict:
    """Tastenbelegung {keycode: {short/double/long: [...], hold, repeat}} einer Aktivitaet oder eines Modus."""
    keys_out = {}
    for k, kv in keys.items():
        if int(k) not in KEYCODES:
            raise vol.Invalid(f"{wo}Unbekannte Taste {k}")
        ko = {}
        for g in GESTURES:
            clean = validate_steps(kv.get(g) or [], f"{wo}Taste {k}/{g}")
            if clean:
                ko[g] = clean
        if kv.get("hold") and ko.get("short") and not ko.get("double") and not ko.get("long"):
            ko["hold"] = True
        # "Beim Gedrueckthalten: Kurz wiederholen" (ms) - schliesst Halten am Geraet aus
        if kv.get("repeat") and ko.get("short") and not ko.get("hold"):
            ko["repeat"] = max(50, min(2000, int(kv["repeat"])))
        # "Lang wiederholen" (ms, 2026-10-15): Lang loest nach der Haltezeit aus und wiederholt im Takt
        if kv.get("lrepeat") and ko.get("long") and not ko.get("hold") and not ko.get("repeat"):
            ko["lrepeat"] = max(100, min(2000, int(kv["lrepeat"])))
        if ko:
            keys_out[str(int(k))] = ko
    return keys_out


def validate_modi(modi: list) -> list:
    """Folgemodi (2026-10-15): ein Schritt "modus" belegt einzelne Tasten fuer s Sekunden um
    (z. B. Kanal +/- dimmt die Haengelampe, nachdem sie geschaltet wurde). Jeder Druck einer
    Modus-Taste verlaengert die Zeit."""
    if not isinstance(modi, list) or len(modi) > 16:
        raise vol.Invalid("hoechstens 16 Modi")
    out, ids = [], set()
    for m in modi:
        mid = str((m or {}).get("id") or "").strip()
        if not re.fullmatch(r"[a-z0-9_]{1,24}", mid) or mid in ids:
            raise vol.Invalid(f"Modus-ID fehlt, ungültig oder doppelt: '{mid}'")
        ids.add(mid)
        name = str(m.get("name") or mid)[:24]
        out.append({"id": mid, "name": name, "s": max(2, min(60, int(m.get("s") or 7))),
                    "keys": validate_keys(m.get("keys") or {}, f"Modus {name}: ")})
    return out


def validate_steps(steps, wo: str) -> list:
    """Schrittfolge (Taste, Menue-Eintrag): hoechstens 10 Schritte bekannter Art."""
    if not isinstance(steps, list) or len(steps) > 10:
        raise vol.Invalid(f"{wo}: hoechstens 10 Schritte")
    clean = []
    for s in steps:
        if not isinstance(s, dict) or s.get("t") not in STEP_TYPES:
            raise vol.Invalid(f"{wo}: unbekannter Schritt {s}")
        if s["t"] == "ha":
            if "." not in str(s.get("svc", "")):
                raise vol.Invalid(f"{wo}: Dienst fehlt")
            d = s.get("data") or {}
            if not isinstance(d, dict):
                raise vol.Invalid(f"{wo}: Daten muessen ein Objekt sein")
            # Die Remote reicht alle Werte als Text an HA weiter (HA wandelt selbst).
            s = {**s, "data": {str(x): (y if isinstance(y, str) else json.dumps(y)) for x, y in d.items()}}
        if s["t"] == "ir":
            s = validate_ir_step(s, wo)
        if s["t"] == "rf":
            s = validate_rf_step(s, wo)
        if s["t"] == "modus":
            if not re.fullmatch(r"[a-z0-9_]{1,24}", str(s.get("id") or "")):
                raise vol.Invalid(f"{wo}: Modus fehlt")
            s = {"t": "modus", "id": s["id"]}
        clean.append(s)
    return clean


def _docks(v, wo: str) -> list:
    """Liste von Dock-Slugs (ESPHome-Geraete mit <slug>_ir_roh / _rf_roh), hoechstens 8."""
    if isinstance(v, str):
        v = [v] if v else []
    if not isinstance(v, list):
        raise vol.Invalid(f"{wo}: Docks muessen eine Liste sein")
    out = []
    for d in v:
        d = str(d or "").strip().lower()
        if not d:
            continue
        if not re.fullmatch(r"[a-z0-9_]{1,48}", d):
            raise vol.Invalid(f"{wo}: Dock-Name ungültig: {d}")
        if d not in out:
            out.append(d)
    return out[:8]


def _schritt_weg(s: dict, o: dict, wo: str, mit_route: bool) -> None:
    """Eigener Sendeweg je Schritt (2026-10-11): r = Route 0..3, d = Docks. Fehlt es: Vorgabe aus cfg.ir."""
    if mit_route and s.get("r") is not None:
        o["r"] = max(0, min(3, int(s["r"])))
    if s.get("d") is not None:
        o["d"] = _docks(s["d"], wo)


def validate_ir_step(s: dict, wo: str) -> dict:
    """IR-Schritt: Protokoll + Adresse/Befehl oder Rohzeiten (us, + an / - aus) mit Traeger f (Hz)."""
    o = {"t": "ir"}
    if s.get("n"):
        o["n"] = str(s["n"])[:48]   # Anzeige im Konfigurator (Geraet/Taste aus der Datenbank)
    if isinstance(s.get("db"), int):
        o["db"] = s["db"]   # Geraet in der IR-Datenbank ("Anderer Befehl" im Konfigurator)
    _schritt_weg(s, o, wo, True)
    if s.get("raw") is not None:
        raw = s["raw"]
        if not isinstance(raw, list) or not 2 <= len(raw) <= 1200:
            raise vol.Invalid(f"{wo}: IR-Rohsignal braucht 2 bis 1200 Werte")
        o["raw"] = [max(-100000, min(100000, int(v))) for v in raw if int(v) != 0]
        o["f"] = max(10000, min(100000, int(s.get("f") or 38000)))
        return o
    o["proto"] = str(s.get("proto") or "NEC")[:12]
    o["addr"] = max(0, min(0xFFFFFFFF, int(s.get("addr") or 0)))
    o["cmd"] = max(0, min(0xFFFFFFFF, int(s.get("cmd") or 0)))
    return o


def validate_rf_step(s: dict, wo: str) -> dict:
    """433-MHz-Schritt (sendet das Dock): Rohzeiten in us (+ an / - aus), wdh = Wiederholungen."""
    raw = s.get("raw")
    if not isinstance(raw, list) or not 2 <= len(raw) <= 1200:
        raise vol.Invalid(f"{wo}: 433-MHz-Signal fehlt – erst anlernen")
    o = {"t": "rf", "raw": [max(-100000, min(100000, int(v))) for v in raw if int(v) != 0],
         "wdh": max(1, min(20, int(s.get("wdh") or 5)))}
    if s.get("n"):
        o["n"] = str(s["n"])[:48]
    _schritt_weg(s, o, wo, False)
    return o


def validate_ir(ir: dict) -> dict:
    """Sendeweg wie beim OpenRemote-Original: 0 nur Remote, 1 Remote und Dock, 2 nur Dock, 3 Remote, dann Dock."""
    route = max(0, min(3, int(ir.get("route") or 0)))
    docks = _docks(ir["docks"] if ir.get("docks") is not None else ir.get("dock"), "Infrarot")
    return {"route": route, "docks": docks}


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


ITEM_TYPES = {"text", "page", "special", "action", "toggle", "light", "number", "select", "cover", "sensor", "camera", "setting"}


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
                    # Texte (Erklaerungen) bis 400 Zeichen - 120 schnitt die Hinweise der Einstellungen ab
                    # ("... Halbie", 2026-10-11)
                    o[k] = str(it[k])[:400 if (k == "label" and t == "text") else 120]
            # Aussehen (2026-10-05): Farben "#rrggbb", Ausrichtung l/c/r, Schriftgroesse s/m/l
            for k in ("bg", "fg", "fc"):   # fc = Rahmenfarbe bei Markierung (2026-10-13)
                if isinstance(it.get(k), str) and re.fullmatch(r"#[0-9a-fA-F]{6}", it[k]):
                    o[k] = it[k].lower()
            if it.get("align") in ("l", "c", "r"):
                o["align"] = it["align"]
            if it.get("size") in ("s", "m", "l"):
                o["size"] = it["size"]
            if "x" in it:
                o["x"] = max(0, min(5, int(it["x"])))
            if "y" in it:
                o["y"] = max(0, min(127, int(it["y"])))
            if it.get("remember"):
                o["remember"] = True
            if it.get("steps"):
                o["steps"] = validate_steps(it["steps"], f"Seite {pid}")
            # Halte-Knopf: steps beim Druecken, release beim Loslassen
            if it.get("hold"):
                o["hold"] = True
                if it.get("release"):
                    o["release"] = validate_steps(it["release"], f"Seite {pid}")
            # Tastenmodus: Hardware-Taste, die diesem Eintrag folgt (jede ausser Zurueck)
            if it.get("key") and int(it["key"]) in KEYCODES and int(it["key"]) != 13:
                o["key"] = int(it["key"])
            # Folgemodus nach dem Bedienen (2026-10-15), z. B. Ventilator: Kanal +/- aendert die Stufe
            if re.fullmatch(r"[a-z0-9_]{1,24}", str(it.get("modus") or "")):
                o["modus"] = it["modus"]
            items.append(o)
        pg = {
            "id": pid,
            "title": str(p.get("title", pid))[:40],
            "remember": bool(p.get("remember", True)),
            "flow": bool(p.get("flow", True)),
            "items": items,
        }
        # Markierung beim Oeffnen (2026-10-07): first | none | back | i:<Index>; fehlt = Standard (Popup: none)
        fo = str(p.get("focus") or "")
        if fo in ("first", "none", "back") or (fo.startswith("i:") and fo[2:].isdigit() and int(fo[2:]) < len(items)):
            pg["focus"] = fo
        # Rahmenfarbe des markierten Zurueck/X (2026-10-13); fehlt = weiss
        if isinstance(p.get("bfc"), str) and re.fullmatch(r"#[0-9a-fA-F]{6}", p["bfc"]):
            pg["bfc"] = p["bfc"].lower()
        # Zurueck/X (2026-10-13): Farbe des Knopfs und Symbol (x | back; fehlt = automatisch)
        if isinstance(p.get("bc"), str) and re.fullmatch(r"#[0-9a-fA-F]{6}", p["bc"]):
            pg["bc"] = p["bc"].lower()
        if p.get("bsym") in ("x", "back"):
            pg["bsym"] = p["bsym"]
        pp = p.get("popup")
        if isinstance(pp, dict):
            def trig(lst):
                out = []
                for t in (lst or [])[:12]:
                    if isinstance(t, dict) and t.get("ev") == "aktivitaet" and t.get("to"):
                        out.append({"ev": "aktivitaet", "to": str(t["to"])[:40]})   # Aktivitaetswechsel (2026-10-13)
                    elif isinstance(t, dict) and t.get("entity") and "to" in t:
                        o = {"entity": str(t["entity"])[:80], "to": str(t["to"])[:80]}
                        if t.get("attr"):
                            o["attr"] = str(t["attr"])[:40]
                        if t.get("neg"):
                            o["neg"] = True   # Bedingung "ungleich"
                        out.append(o)
                return out
            pg["popup"] = {"open": trig(pp.get("open")), "close": trig(pp.get("close")),
                           "timeout": max(0, min(3600, int(pp.get("timeout") or 0))), "wake": bool(pp.get("wake", True))}
            if pp.get("excl"):
                pg["popup"]["excl"] = True   # Vorrang: andere Popups warten
            cond = [t for t in trig(pp.get("if")) if "entity" in t]   # "Nur wenn" (2026-10-13)
            if cond:
                pg["popup"]["if"] = cond
        if p.get("inhalt") in ("tastatur",):   # eingebauter Inhalt statt Eintraegen (2026-10-13)
            pg["inhalt"] = p["inhalt"]
        out_pages.append(pg)
    for p in out_pages:
        for it in p["items"]:
            if it["t"] == "page" and it.get("target") not in ids:
                raise vol.Invalid(f"Seite {p['id']}: Ziel '{it.get('target')}' gibt es nicht")
    start = str(m.get("start") or (out_pages[0]["id"] if out_pages else ""))
    return {"start": start if start in ids else (out_pages[0]["id"] if out_pages else ""), "pages": out_pages}


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
            self.data["devices"][dev]["config"].setdefault("menu", default_menu())
        # Einmalige Menue-Umbauten vom 17.10. (menue_rev 3-6) sind gelaufen und seit 2026-10-18 entfernt
        if self._neue_einst_seiten():
            await self.store.async_save(self.data)
        for dev in self.devices:
            self.announce(dev)

    def _neue_einst_seiten(self) -> bool:
        """Neue eingebaute Einstellungsseiten (z. B. "Dock & Gesten", 2026-10-17) in die gespeicherten Menues
        aufnehmen - je Seite nur einmal (data["einst_seiten"]), damit eine geloeschte Seite geloescht bleibt."""
        if "einst_seiten" not in self.data:   # erster Lauf: alles bis auf die neuen Seiten galt schon
            self.data["einst_seiten"] = [s["id"] for s in EINST_SEITEN if s["id"] not in ("set_extras",)]
        bekannt = self.data.setdefault("einst_seiten", [])
        neu = [s for s in EINST_SEITEN if s["id"] not in bekannt]
        if not neu:
            return False
        for d in self.data["devices"].values():
            m = d.get("config", {}).get("menu") or {}
            seiten = m.get("pages") or []
            ids = {p.get("id") for p in seiten}
            if "set_bright" not in ids:   # Menue ohne die Einstellungsseiten: nichts anfassen
                continue
            for s in neu:
                if s["id"] in ids:
                    continue
                seiten.append(copy.deepcopy(s))
                eint = next((p for p in seiten if p.get("id") == "settings"), None)
                if eint is None:
                    continue
                items = eint.setdefault("items", [])
                if any(i.get("target") == s["id"] for i in items):
                    continue
                pos = next((n for n, i in enumerate(items) if i.get("target") == "set_service"), len(items))
                items.insert(pos, {"t": "page", "w": 6, "h": 1, "label": s["title"], "target": s["id"]})
                _LOGGER.info("Menue: Einstellungsseite %s ergaenzt", s["title"])
        bekannt.extend(s["id"] for s in neu)
        return True

    async def geraet_dazu(self, dev: str, vorlage: str | None = None) -> None:
        """Neue Remote aufnehmen; Belegung als Kopie einer vorhandenen (oder Standard)."""
        if dev not in self.devices:
            self.devices.append(dev)
        if dev not in self.data["extra"]:
            self.data["extra"].append(dev)
        if vorlage in self.data["devices"]:
            src = self.data["devices"][vorlage]
            self.data["devices"][dev] = {"config": copy.deepcopy(src["config"]), "enabled": bool(src.get("enabled"))}
            werte = self.einst_werte(vorlage)[0]
        else:
            self.data["devices"][dev] = {"config": default_config(), "enabled": False}
            werte = self.data.get("einst_standard") or {}
        await self.store.async_save(self.data)
        self.announce(dev)
        if werte:   # Einstellungen der Vorlage bzw. die Standard-Einstellungen
            await self.einst_setzen(dev, werte)

    def entfernbar(self, dev: str) -> bool:
        """Nur im Panel angelegte Remotes (die aus der YAML bzw. dem Einrichtungsdialog bleiben)."""
        return dev in self.data.get("extra", [])

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
        if e.get("einst"):
            cfg["einst"] = e["einst"]
        return device_payload(cfg)

    # ---- Geraete-Einstellungen (2026-10-16) ----
    # e["einst"]     = {name: [wert, rev]}  Soll aus dem Konfigurator; die Remote uebernimmt nur Werte mit neuer rev
    # e["einst_ist"] = {name: wert}         zuletzt von der Remote gemeldet (Ereignis esphome.remote_einstellungen)
    # e["einst_ist_rev"]                    hoechste rev, die die Remote nachweislich geladen hatte (gleicher Hash)
    def einst_werte(self, dev: str) -> tuple[dict, list]:
        """Aktuelle Werte fuer die Anzeige + Namen, die noch auf die Remote warten."""
        e = self.entry(dev)
        ist, soll, bis = e.get("einst_ist") or {}, e.get("einst") or {}, e.get("einst_ist_rev", -1)
        out, offen = dict(ist), []
        for k, (v, r) in soll.items():
            if r > bis or k not in ist:
                if ist.get(k) != v:
                    offen.append(k)
                out[k] = v
        return out, offen

    def einst_angleichen(self, dev: str) -> None:
        """An der Remote umgestellte Werte in den Soll-Stand uebernehmen (2026-10-18).
        Die Remote wendet einen Soll-Wert nur an, wenn sich seine rev geaendert hat - an der Remote verstellte Werte
        werden beim naechsten Uebertragen also nie zurueckgesetzt. Ohne Angleichen stand im Soll aber noch der alte
        Wert, und eine Remote ohne gespeicherten Stand (neu geflasht) bekam ihn wieder. Die rev bleibt gleich, damit
        die Remote den Wert nicht noch einmal "anwendet"; nur wenn die Remote diese rev schon geladen hatte."""
        e = self.entry(dev)
        ist, bis, soll = e.get("einst_ist") or {}, e.get("einst_ist_rev", -1), e.get("einst") or {}
        for k, (v, r) in list(soll.items()):
            if r <= bis and k in ist and ist[k] != v:
                soll[k] = [ist[k], r]

    async def einst_setzen(self, dev: str, werte: dict) -> int:
        e = self.entry(dev)
        self.einst_angleichen(dev)
        aktuell, _ = self.einst_werte(dev)
        soll = e.setdefault("einst", {})
        n, neu = self.data.get("einst_rev", 0), 0
        for k, v in werte.items():
            if not re.fullmatch(r"[a-z][a-z0-9_]{0,31}", str(k)) or not isinstance(v, (bool, int)):
                raise vol.Invalid(f"Einstellung {k}: ungültig")
            if aktuell.get(k) == v and k in soll:
                continue
            n += 1
            neu += 1
            soll[k] = [v, n]
        if neu:
            self.data["einst_rev"] = n
            await self.store.async_save(self.data)
            self.announce(dev)
        return neu

    @callback
    def einst_gemeldet(self, dev: str, werte: dict, hash_: str) -> None:
        e = self.entry(dev)
        licht = self.hass.data.get(DOMAIN + "_licht")
        if licht is not None:
            licht.einst_gemeldet(dev, e.get("einst_ist") or {}, werte)
        e["einst_ist"] = werte
        e["einst_ist_at"] = dt_util.now().isoformat(timespec="seconds")
        if hash_ == fnv1a(self.payload(dev)):
            e["einst_ist_rev"] = max([r for _, r in (e.get("einst") or {}).values()] + [0])
        self.store.async_delay_save(lambda: self.data, 5)
        self.hass.bus.async_fire(DOMAIN + "_einst", {"device": dev})

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
        self.einst_angleichen(dev)   # Datei der Remote aendert sich ohnehin - dabei den Soll-Stand nachziehen
        # vor dem Uebernehmen pruefen (sonst bliebe der zu grosse Stand im Speicher); einst geht mit an die Remote
        body = device_payload({**clean, "on": bool(enabled), **({"einst": e["einst"]} if e.get("einst") else {})})
        if len(body) > MAX_BYTES:
            raise vol.Invalid(f"Belegung zu gross ({len(body)} Byte, max. {MAX_BYTES})")
        e["config"] = clean
        e["enabled"] = bool(enabled)
        e["saved_by"] = user
        e["saved_at"] = dt_util.now().isoformat(timespec="seconds")
        body = self.payload(dev)
        await self.store.async_save(self.data)
        self.announce(dev)
        if (v := self.hass.data.get(DOMAIN + "_dock")) is not None:
            await v.sync()   # Aktivitaets-Schalter an Namen/Anzahl anpassen
        return fnv1a(body)


def schluessel_ok(request: web.Request, key: str) -> bool:
    return hmac.compare_digest(request.query.get("k", ""), key)


class CfgView(HomeAssistantView):
    """Auslieferung an die Remote. Kein HA-Login (die Remote hat keinen Token),
    stattdessen ein gemeinsamer Schluessel aus secrets.yaml."""

    url = "/api/esphomeremote_konfig/cfg/{dev}"
    name = "api:esphomeremote_konfig:cfg"
    requires_auth = False

    def __init__(self, kd: KonfigData) -> None:
        self.kd = kd

    async def get(self, request: web.Request, dev: str) -> web.Response:
        if not schluessel_ok(request, self.kd.key):
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
    Antwort: {"entity": [state, brightness 0-255|-1, current_position|-1, unit(, min, max, step)]}
    media_player: current_position = Lautstaerke 0-100; number/input_number: zusaetzlich min, max, step"""

    url = "/api/esphomeremote_konfig/states"
    name = "api:esphomeremote_konfig:states"
    requires_auth = False

    def __init__(self, kd: KonfigData) -> None:
        self.kd = kd

    async def get(self, request: web.Request) -> web.Response:
        if not schluessel_ok(request, self.kd.key):
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
            if e.startswith("media_player."):   # Lautstaerke 0..100 im Feld "Position" (Eintragsart "number", 2026-10-07)
                lautst = a.get("volume_level")
                pos = round(lautst * 100) if isinstance(lautst, (int, float)) else None
            eintrag = [st.state, int(bri) if isinstance(bri, (int, float)) else -1,
                       int(pos) if isinstance(pos, (int, float)) else -1, str(a.get("unit_of_measurement") or "")]
            if e.startswith(("number.", "input_number.")):   # Bereich fuer den Regler: min, max, Schritt
                eintrag += [a.get("min", 0), a.get("max", 100), a.get("step", 1)]
            out[e] = eintrag
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
    websocket_api.async_register_command(hass, ws_bt_proxies)
    websocket_api.async_register_command(hass, ws_einst_set)
    websocket_api.async_register_command(hass, ws_einst_standard)

    @callback
    def einst_ereignis(ev) -> None:
        """Remote meldet ihre Einstellungen (nach dem Start und bei jeder Aenderung am Geraet)."""
        dev = str(ev.data.get("geraet") or "")
        if dev not in kd.devices:
            return
        try:
            werte = json.loads(ev.data.get("werte") or "{}")
        except ValueError:
            _LOGGER.warning("Einstellungen von %s nicht lesbar", dev)
            return
        if isinstance(werte, dict):
            kd.einst_gemeldet(dev, werte, str(ev.data.get("hash") or ""))

    hass.bus.async_listen("esphome.remote_einstellungen", einst_ereignis)
    from .einrichten import registrieren

    registrieren(hass, kd)

    from .regeln import registrieren as regeln_registrieren

    regeln_registrieren(hass)

    from .irdb import IrDb, registrieren as irdb_registrieren

    irdb = IrDb(hass)
    await irdb.laden()
    irdb_registrieren(hass, irdb)

    # Dockingstation-Tab: IR-Geraete und Aktivitaets-Schalter (dock.py)
    from .dock import starten as dock_starten

    await dock_starten(hass, kd)

    # Docks einrichten (Hardware): anlegen, flashen, einbinden (docks.py)
    from .docks import registrieren as docks_registrieren

    docks_registrieren(hass)

    # Firmware-Tab: Remotes und Docks bauen/flashen (firmware.py)
    from .firmware import registrieren as firmware_registrieren

    firmware_registrieren(hass)

    # Bluetooth-Bruecke aus den Remotes (bruecke.py) und Helligkeit nach Lichtsensor (licht.py), 2026-10-17
    from .bruecke import Bruecke
    from .licht import Licht

    from .anzeige import Anzeige, AnzeigeView

    anzeige = hass.data[DOMAIN + "_anzeige"] = Anzeige(hass, kd)
    hass.http.register_view(AnzeigeView(kd, anzeige))
    bruecke = hass.data[DOMAIN + "_bruecke"] = Bruecke(hass, kd)
    licht = hass.data[DOMAIN + "_licht"] = Licht(hass, kd)

    async def bruecke_start(_ev=None) -> None:
        try:
            await bruecke.abgleichen()
        except Exception:  # noqa: BLE001
            _LOGGER.exception("Bluetooth-Bruecke: Abgleich fehlgeschlagen")
        await licht.starten()

    if hass.is_running:
        hass.async_create_task(bruecke_start())
    else:
        hass.bus.async_listen_once("homeassistant_started", bruecke_start)
    async def gesten_aendern(ev) -> None:
        """Gesten-Seite der Remote (2026-10-17): an/aus, Taste, Name, loeschen, angelernt -> cfg.gesten speichern."""
        dev = str(ev.data.get("geraet") or "")
        if dev not in kd.devices or dev not in kd.data["devices"]:
            return
        aktion, gid = str(ev.data.get("aktion") or ""), str(ev.data.get("id") or "")
        name, wert = str(ev.data.get("name") or "").strip()[:24], str(ev.data.get("wert") or "")
        e = kd.data["devices"][dev]
        cfg = copy.deepcopy(e["config"])
        g = cfg.setdefault("gesten", {})
        eigen = g.setdefault("eigen", [])
        if gid in GESTEN_FEST:
            obj = g.setdefault(gid, {"an": True, "s": []})
        elif re.fullmatch(r"[a-z0-9_]{1,16}", gid):
            obj = next((x for x in eigen if x.get("id") == gid), None)
        else:
            return
        if aktion == "lernen":
            v = [x for x in str(ev.data.get("v") or "").split(",") if re.fullmatch(r"[0-9a-f]{192}", x)][:4]
            if not v or gid in GESTEN_FEST:
                return
            if obj is None:
                if len(eigen) >= 8:
                    _LOGGER.warning("Geste %s: schon 8 eigene Gesten", name)
                    return
                obj = {"id": gid, "n": name or f"Geste {len(eigen) + 1}", "an": True, "v": [], "s": []}
                eigen.append(obj)
            obj["v"] = v
            if wert == "2":   # Format Lage-Rahmen (Form-Erkennung, 2026-10-17 abends)
                obj["f"] = 2
            else:
                obj.pop("f", None)
            if name:
                obj["n"] = name
        elif obj is None:
            return
        elif aktion == "an":
            obj["an"] = wert == "1"
        elif aktion == "taste":
            obj["k"] = int(wert) if wert.isdigit() else 0
        elif aktion == "name" and name and gid not in GESTEN_FEST:
            obj["n"] = name
        elif aktion == "loeschen" and gid not in GESTEN_FEST:
            g["eigen"] = [x for x in eigen if x.get("id") != gid]
        else:
            return
        if not g.get("eigen"):
            g.pop("eigen", None)
        try:
            await kd.save(dev, cfg, bool(e.get("enabled")), "Remote")
        except (vol.Invalid, ValueError, TypeError) as err:
            _LOGGER.warning("Geste von der Remote nicht gespeichert: %s", err)
            return
        _LOGGER.info("%s: Geste %s %s", dev, aktion, name or gid)
        hass.bus.async_fire(DOMAIN + "_gesten", {"device": dev, "gesten": kd.data["devices"][dev]["config"].get("gesten") or {}})

    @callback
    def lernstand(ev) -> None:
        dev = str(ev.data.get("geraet") or "")
        if dev in kd.devices:
            hass.bus.async_fire(DOMAIN + "_lernstand", {"device": dev, "id": ev.data.get("id"), "probe": ev.data.get("probe"),
                                                         "text": str(ev.data.get("text") or "")})

    hass.bus.async_listen("esphome.remote_gesten_aendern", gesten_aendern)
    hass.bus.async_listen("esphome.remote_geste_lernstand", lernstand)
    for cmd in (ws_licht_get, ws_licht_set, ws_bruecke, ws_anzeige_get, ws_anzeige_set, ws_geste_anlernen):
        websocket_api.async_register_command(hass, cmd)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/licht_get"})
@callback
def ws_licht_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    connection.send_result(msg["id"], hass.data[DOMAIN + "_licht"].stand())


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/licht_set", vol.Required("werte"): dict})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_licht_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    licht = hass.data[DOMAIN + "_licht"]
    try:
        await licht.setzen(msg["werte"])
    except (ValueError, TypeError, KeyError) as err:
        connection.send_error(msg["id"], "ungueltig", str(err))
        return
    connection.send_result(msg["id"], licht.stand())


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/geste_anlernen", vol.Required("device"): str,
                                  vol.Required("gid"): str, vol.Required("name"): str})
@websocket_api.async_response
async def ws_geste_anlernen(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Anlernen an der Remote starten: dreimal ausfuehren, je mit OK bestaetigen. Ergebnis kommt als
    esphome.remote_gesten_aendern (gespeichert von HA), Fortschritt als esphomeremote_konfig_lernstand."""
    if msg["device"] not in hass.data[DOMAIN].devices:
        connection.send_error(msg["id"], "unknown_device", "Unbekannte Remote")
        return
    slug = dev_slug(msg["device"])
    if not re.fullmatch(r"[a-z0-9_]{1,16}", msg["gid"]):
        connection.send_error(msg["id"], "ungueltig", "Kennung ungültig")
        return
    if not hass.services.has_service("esphome", f"{slug}_geste_anlernen"):
        connection.send_error(msg["id"], "firmware", "Die Remote kennt das Anlernen noch nicht (Firmware aktualisieren).")
        return
    try:
        await asyncio.wait_for(hass.services.async_call("esphome", f"{slug}_geste_anlernen",
                                                        {"gid": msg["gid"], "name": msg["name"][:24]}, blocking=True), 15)
    except Exception as err:  # noqa: BLE001
        connection.send_error(msg["id"], "schlaeft", f"Remote nicht erreichbar ({err}) - eine Taste drücken und nochmal.")
        return
    connection.send_result(msg["id"], {"ok": True})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/anzeige_get"})
@websocket_api.async_response
async def ws_anzeige_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    a = hass.data[DOMAIN + "_anzeige"]
    connection.send_result(msg["id"], {**a.stand(), "vorschau": await a.daten()})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/anzeige_set", vol.Required("werte"): dict})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_anzeige_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    a = hass.data[DOMAIN + "_anzeige"]
    try:
        await a.setzen(msg["werte"])
    except (ValueError, TypeError) as err:
        connection.send_error(msg["id"], "ungueltig", str(err))
        return
    connection.send_result(msg["id"], {**a.stand(), "vorschau": await a.daten()})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/bruecke", vol.Optional("ota"): str})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_bruecke(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Bruecke neu abgleichen; ota = Kennung der Remote, die Updates ueber Bluetooth bekommt (OTA-Port 3232)."""
    b = hass.data[DOMAIN + "_bruecke"]
    if "ota" in msg:
        for slug, p in b.kd.data.get("bruecke", {}).items():
            p["ota"] = slug == msg["ota"]
        await b.kd.store.async_save(b.kd.data)
    await b.abgleichen()
    connection.send_result(msg["id"], b.info())


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/get"})
@callback
def ws_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    kd: KonfigData = hass.data[DOMAIN]
    out = {}
    for dev in kd.devices:
        e = kd.entry(dev)
        werte, offen = kd.einst_werte(dev)
        out[dev] = {
            "einst": werte,
            "einst_offen": offen,
            "einst_at": e.get("einst_ist_at"),
            "config": e["config"],
            "enabled": bool(e.get("enabled")),
            "hash": fnv1a(body := kd.payload(dev)),
            "bytes": len(body),
            "saved_by": e.get("saved_by"),
            "saved_at": e.get("saved_at"),
            "slug": dev_slug(dev),
            "entfernbar": kd.entfernbar(dev),
        }
    connection.send_result(msg["id"], {"devices": out, "order": kd.devices, "einst_standard": kd.data.get("einst_standard") or {},
                                       "max_bytes": MAX_BYTES})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/einst_set", vol.Required("device"): str,
                                  vol.Optional("werte"): dict, vol.Optional("von"): str})
@websocket_api.async_response
async def ws_einst_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Einstellungen setzen (sofort an die Remote); von = alle Werte einer anderen Remote uebernehmen."""
    kd: KonfigData = hass.data[DOMAIN]
    dev = msg["device"]
    if dev not in kd.devices or (msg.get("von") and msg["von"] not in kd.devices):
        connection.send_error(msg["id"], "unknown_device", "Unbekannte Remote")
        return
    werte = kd.einst_werte(msg["von"])[0] if msg.get("von") else msg.get("werte") or {}
    try:
        n = await kd.einst_setzen(dev, werte)
    except vol.Invalid as err:
        connection.send_error(msg["id"], "invalid", str(err))
        return
    w, offen = kd.einst_werte(dev)
    connection.send_result(msg["id"], {"geaendert": n, "einst": w, "einst_offen": offen})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/einst_standard", vol.Required("device"): str})
@websocket_api.async_response
async def ws_einst_standard(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Einstellungen dieser Remote als Standard fuer neue Remotes merken."""
    kd: KonfigData = hass.data[DOMAIN]
    if msg["device"] not in kd.devices:
        connection.send_error(msg["id"], "unknown_device", "Unbekannte Remote")
        return
    kd.data["einst_standard"] = kd.einst_werte(msg["device"])[0]
    await kd.store.async_save(kd.data)
    connection.send_result(msg["id"], {"einst_standard": kd.data["einst_standard"]})


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
    connection.send_result(msg["id"], {"hash": h, "bytes": len(kd.payload(dev))})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/bt_proxies"})
@callback
def ws_bt_proxies(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Bluetooth-Empfaenger in HA (ESPHome-Proxys u. a.): Adresse, Name, verbindungsfaehig."""
    out = {}
    try:
        from homeassistant.components import bluetooth

        for sc in bluetooth.async_current_scanners(hass):
            mac = str(sc.source).upper()
            out[mac] = {"mac": mac, "name": getattr(sc, "name", "") or mac,
                        "aktiv": bool(getattr(sc, "connectable", False)), "online": True}
    except Exception as err:   # noqa: BLE001 - ohne Bluetooth-Integration: leere Liste
        _LOGGER.debug("Bluetooth-Scanner nicht lesbar: %s", err)
    # Auch bekannte, gerade nicht erreichbare Proxys (2026-10-11: Dock war aus und fehlte in der Liste).
    # HA legt fuer jeden Fern-Scanner (ESPHome-Proxy) einen Eintrag der Bluetooth-Integration an.
    for e in hass.config_entries.async_entries("bluetooth"):
        mac = str(e.data.get("source") or "").upper()
        if not mac or mac in out or not e.data.get("source_domain"):
            continue
        out[mac] = {"mac": mac, "name": e.title or mac, "aktiv": True, "online": False}
    connection.send_result(msg["id"], {"proxies": sorted(out.values(), key=lambda p: (not p["online"], p["name"]))})
