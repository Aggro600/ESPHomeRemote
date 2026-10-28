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
      devices: [esphomeremote]
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
from .spiegel import Spiegel, validate_ents
from .widgets import BildView, WetterCache, timer_eintrag, wetter_eintrag

_LOGGER = logging.getLogger(__name__)

DOMAIN = "esphomeremote_konfig"
STORE_VERSION = 1
PANEL_URL = "fernbedienung"
STATIC_URL = "/esphomeremote_konfig_static"
PANEL_VERSION = "133"  # bei Aenderungen am Panel-JS erhoehen (App-Cache)

MAX_BYTES = 90000   # Remote: tasten_konfig MAX_BYTES (96 KB), Puffer im PSRAM
KEYCODES = {2, 3, 4, 5, 11, 13, 14, 15, 21, 22, 23, 24, 25, 31, 32, 33, 34, 35, 41, 42, 43, 44, 45, 102,
            900, 901}   # 900/901: keine Tasten - Schritte beim Starten/Beenden der Aktivitaet
GESTURES = ("short", "double", "long")
# Ersatz ohne HA-Verbindung (2026-10-18): "<geste>_offline" laeuft statt "<geste>", solange die Remote weder per WLAN
# noch ueber die Bluetooth-Bruecke mit HA verbunden ist (tasten_konfig: api_ok_()). Leer = normale Schritte.
OFFLINE = tuple(f"{g}_offline" for g in GESTURES)
STEP_TYPES = {"ble", "ha", "int", "ir", "rf", "wait", "modus"}

CONFIG_SCHEMA = vol.Schema(
    {
        # YAML ist optional: einfacher geht es ueber "Integration hinzufuegen" (config_flow.py)
        vol.Optional(DOMAIN): vol.Schema(
            {
                vol.Required("key"): cv.string,
                vol.Optional("devices", default=[]): vol.All(
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
        # Eigene Startseite (2026-10-27): ID einer Menueseite, fehlt = eingebaute Startseite
        if re.fullmatch(r"[A-Za-z0-9_\-]{1,40}", str(a.get("home") or "")):
            out_acts[-1]["home"] = str(a["home"])
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
        # Gleiches Geraet am Dock je Remote-Platz (2026-10-22): {"0": {"d": dock-slug, "p": Dock-Platz}}
        # (fehlte hier bis 2026-10-23 - die Auswahl "Am Dock" ging beim Speichern verloren)
        dock = {}
        for k, z in (cfg["bt"].get("dock") or {}).items():
            if str(k) in ("0", "1", "2", "3") and isinstance(z, dict):
                d = str(z.get("d") or "").strip().lower()
                if re.fullmatch(r"[a-z0-9_]{1,48}", d):
                    dock[str(k)] = {"d": d, "p": max(0, min(3, int(z.get("p") or 0)))}
        if dock:
            out["bt"]["dock"] = dock
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
    if isinstance(cfg.get("ents"), dict) and (ents := validate_ents(cfg["ents"])):
        out["ents"] = ents   # Verknuepfungen (spiegel.py)
    if isinstance(cfg.get("begruessung"), str) and cfg["begruessung"].strip():
        out["begruessung"] = cfg["begruessung"].strip()[:40]   # Text der Begruessung auf der Remote (2026-10-20)
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


def validate_keys(keys: dict, wo: str, modus: bool = False) -> dict:
    """Tastenbelegung {keycode: {short/double/long: [...], hold, repeat}} einer Aktivitaet oder eines Modus."""
    keys_out = {}
    for k, kv in keys.items():
        if int(k) not in KEYCODES:
            raise vol.Invalid(f"{wo}Unbekannte Taste {k}")
        ko = {}
        for g in GESTURES + OFFLINE:
            clean = validate_steps(kv.get(g) or [], f"{wo}Taste {k}/{g}")
            if clean:
                ko[g] = clean
        hat = lambda g: bool(ko.get(g) or ko.get(f"{g}_offline"))  # noqa: E731
        if kv.get("hold") and hat("short") and not hat("double") and not hat("long"):
            ko["hold"] = True
        # "Beim Gedrueckthalten: Kurz wiederholen" (ms) - schliesst Halten am Geraet aus
        if kv.get("repeat") and hat("short") and not ko.get("hold"):
            ko["repeat"] = max(50, min(2000, int(kv["repeat"])))
        # "Lang wiederholen" (ms, 2026-10-15): Lang loest nach der Haltezeit aus und wiederholt im Takt
        if kv.get("lrepeat") and hat("long") and not ko.get("hold") and not ko.get("repeat"):
            ko["lrepeat"] = max(100, min(2000, int(kv["lrepeat"])))
        # Folgemodus: eigene Zeit nach dem Druck dieser Taste (s), sonst die Dauer des Modus (2026-10-18)
        if modus and kv.get("s") and ko:
            ko["s"] = max(1, min(60, int(kv["s"])))
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
                    "keys": validate_keys(m.get("keys") or {}, f"Modus {name}: ", modus=True)})
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
        # Dock-Slug oder fremder Sender als Entitaet (infrared.* / Broadlink remote.*, 2026-10-28, blaster.py)
        if not re.fullmatch(r"[a-z0-9_]{1,48}|(infrared|remote)\.[a-z0-9_]{1,64}", d):
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
        if k in ("menu", "apps"):   # Ziel (Seite/Popup) und Beschriftung der Knoepfe (2026-10-26)
            if re.fullmatch(r"[a-z0-9_]{1,40}", str(b.get("ziel") or "")):
                areas[k]["ziel"] = b["ziel"]
            if str(b.get("label") or "").strip():
                areas[k]["label"] = str(b["label"]).strip()[:12]
    an = [(k, b) for k, b in areas.items() if b["on"]]
    for i, (k1, a) in enumerate(an):
        for k2, b in an[i + 1:]:
            if not (a["x"] + a["w"] <= b["x"] or b["x"] + b["w"] <= a["x"] or a["y"] + a["h"] <= b["y"] or b["y"] + b["h"] <= a["y"]):
                raise vol.Invalid(f"Startseite: {k1} und {k2} ueberlappen sich")
    return {"custom": bool(h.get("custom")), "areas": areas}


ITEM_TYPES = {"text", "page", "special", "action", "toggle", "light", "number", "select", "cover", "sensor", "camera", "setting",
              "mtitel", "mtaste", "vpreset", "player", "widget"}
# Medien-Elemente (2026-10-26, Umbau): jedes Teil frei verschieb- und vergroesserbar, mit eigener Entitaet.
# mtitel = Titel/Interpret/Zustand, mtaste = Taste (fn), vpreset = Lautstaerke auf wert %. "player" (erste Fassung,
# ein fester Block) wird beim Laden/Speichern in diese Teile zerlegt.
MTASTEN = ("play_pause", "play", "pause", "stop", "prev", "next", "mute", "vol_up", "vol_down", "shuffle", "repeat", "power")


def player_aufteilen(it: dict) -> list[dict]:
    """Alter Block "player" -> Einzelteile (gleiche Anordnung wie vorher auf der Remote)."""
    e = str(it.get("entity") or "")
    y0 = int(it["y"]) if "y" in it else None
    def teil(o, x, dy):
        o = {**o, "entity": e}
        if y0 is not None:
            o["x"], o["y"] = x, y0 + dy
        return o
    stumm = it.get("mute") is not False
    out = [teil({"t": "mtitel", "w": 4 if stumm else 6, "h": 2}, 0, 0)]
    if stumm:
        out.append(teil({"t": "mtaste", "fn": "mute", "w": 2, "h": 2, "label": "Stumm"}, 4, 0))
    for n, fn in enumerate(("prev", "play_pause", "next")):
        out.append(teil({"t": "mtaste", "fn": fn, "w": 2, "h": 1}, n * 2, 2))
    out.append(teil({"t": "number", "w": 6, "h": 2, "label": "Lautstärke", "ok": "mute", "st": 5}, 0, 3))
    for n, v in enumerate((it.get("presets") or [])[:6]):
        out.append(teil({"t": "vpreset", "w": 2, "h": 1, "wert": max(0, min(100, int(v)))}, (n % 3) * 2, 5 + n // 3))
    return out


def player_umbauen(pages: list) -> bool:
    """Seiten mit altem "player"-Block umbauen; die Lautstaerke-Tasten gelten dann fuer die Seite ("vol")."""
    geaendert = False
    for p in pages or []:
        items, neu = p.get("items") or [], []
        allein = len(items) == 1 and items[0].get("t") == "player"
        for it in items:
            if it.get("t") != "player":
                neu.append(it)
                continue
            geaendert = True
            if allein:   # nur der Block auf der Seite: feste Positionen, damit sich jedes Teil frei verschieben laesst
                p["flow"] = False
                it = {**it, "y": 0}
            neu += player_aufteilen(it)
            if it.get("vk") is not False and it.get("entity") and not p.get("vol"):
                p["vol"] = it["entity"]
        if geaendert:
            p["items"] = neu
    return geaendert


FARBE = re.compile(r"#[0-9a-fA-F]{6}")


def _quelle(q) -> dict | None:
    """Quelle fuer zweite Zeile/Abzeichen/Bedingung: fester Text t, oder Entitaet e (leer = eigene) mit Attribut a."""
    if not isinstance(q, dict):
        return None
    if q.get("t"):
        return {"t": str(q["t"])[:40]}
    o = {}
    if q.get("e"):
        o["e"] = str(q["e"])[:80]
    if q.get("a"):
        o["a"] = re.sub(r"[^a-z0-9_]", "", str(q["a"]).lower())[:40]
    return o or None


def kachel_optionen(it: dict, pid: str) -> dict:
    """Kachel-Optionen (2026-10-26), s. components/menue/menue.h (layout_eigen_, extras_, extras_zustand_)."""
    o: dict = {}
    if it.get("lay") in ("1", "2", "k", "g", "i"):
        o["lay"] = it["lay"]
    for k in ("sub", "badge"):
        q = _quelle(it.get(k))
        if q:
            if k == "badge" and isinstance(it[k].get("bg"), str) and FARBE.fullmatch(it[k]["bg"]):
                q["bg"] = it[k]["bg"].lower()
            o[k] = q
    w = it.get("wenn")
    if isinstance(w, dict) and "to" in w:
        q = _quelle({"e": w.get("e"), "a": w.get("a")}) or {}
        q["to"] = str(w["to"])[:40]
        if w.get("neg"):
            q["neg"] = True
        if w.get("art") == "weg":
            q["art"] = "weg"
        o["wenn"] = q
    if isinstance(it.get("map"), dict) and it["map"]:
        o["map"] = {str(k)[:40]: str(v)[:24] for k, v in list(it["map"].items())[:12] if str(k)}
    if it.get("nk") not in (None, ""):
        o["nk"] = max(0, min(3, int(it["nk"])))
    if it.get("eh") is not None and it.get("eh") != "":
        o["eh"] = str(it["eh"])[:8]
    zst = []
    for z in (it.get("zst") or [])[:6] if isinstance(it.get("zst"), list) else []:
        if not isinstance(z, dict) or "w" not in z:
            continue
        e = {"w": str(z["w"])[:40]}
        for k in ("bg", "fg", "ic"):
            if isinstance(z.get(k), str) and FARBE.fullmatch(z[k]):
                e[k] = z[k].lower()
        if z.get("icon"):
            e["icon"] = str(z["icon"])[:40]
        zst.append(e)
    if zst:
        o["zst"] = zst
    for k in ("vmin", "vmax"):
        if it.get(k) not in (None, ""):
            o[k] = int(float(it[k]))
    for k in ("live", "transp", "sicher", "lauf", "bar"):
        if it.get(k):
            o[k] = True
    for k in ("rf", "rc"):
        if isinstance(it.get(k), str) and FARBE.fullmatch(it[k]):
            o[k] = it[k].lower()
    if it.get("rad") not in (None, ""):
        o["rad"] = max(0, min(20, int(it["rad"])))
    if it.get("lang"):
        o["lang"] = validate_steps(it["lang"], f"Seite {pid}")
    if it.get("t") == "vpreset" and isinstance(it.get("werte"), list):
        ws = [max(0, min(100, int(v))) for v in it["werte"][:6]]
        if len(ws) >= 2:
            o["werte"] = ws
    if it.get("t") == "mtitel" and it.get("z2") in ("artist", "app", "source", "none"):
        o["z2"] = it["z2"]
    return o


def widget_optionen(it: dict) -> dict:
    """Felder eines Widgets: art, teile (Kombi), we/me (Entitaeten), mz, leer, datum, takt, bild"""
    o = {"art": it.get("art") if it.get("art") in ("uhr", "wetter", "akku", "medien", "timer", "kamera", "kombi") else "uhr"}
    if isinstance(it.get("teile"), list):
        o["teile"] = [x for x in it["teile"] if x in ("uhr", "wetter", "akku", "medien")][:3]
    for k, dom in (("we", "weather."), ("me", "media_player.")):
        if str(it.get(k) or "").startswith(dom):
            o[k] = str(it[k])[:80]
    for k, werte in (("mz", ("immer", "nur", "ueber")), ("leer", ("weg",)), ("datum", ("nach", "vor", "aus")), ("bild", ("ganz",))):
        if it.get(k) in werte:
            o[k] = it[k]
    if it.get("takt"):
        o["takt"] = max(3, min(600, int(it["takt"])))
    return o


def validate_menu(m: dict) -> dict:
    """Konfigurierbares Menue (Raster 6 Spalten x 40 px)."""
    if not isinstance(m, dict):
        raise vol.Invalid("menu muss ein Objekt sein")
    pages = m.get("pages") or []
    if isinstance(pages, list):
        player_umbauen(pages)
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
            # fc = Rahmenfarbe bei Markierung (2026-10-13), ic = Farbe des Symbols (2026-10-18)
            for k in ("bg", "fg", "fc", "ic"):
                if isinstance(it.get(k), str) and re.fullmatch(r"#[0-9a-fA-F]{6}", it[k]):
                    o[k] = it[k].lower()
            if it.get("align") in ("l", "c", "r"):
                o["align"] = it["align"]
            if it.get("size") in ("xs", "s", "m", "l", "xl"):   # xs/xl seit 2026-10-18 (11 / 24 px)
                o["size"] = it["size"]
            if it.get("ia") in ("l", "c", "r"):   # Ausrichtung des Symbols (2026-10-18)
                o["ia"] = it["ia"]
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
            # Medien-Elemente (2026-10-26): Taste mit Funktion, Preset mit Prozentwert
            if t == "mtaste":
                o["fn"] = it.get("fn") if it.get("fn") in MTASTEN else "play_pause"
            if t == "vpreset":
                o["wert"] = max(0, min(100, int(it.get("wert") or 0)))
            # Lautstaerke-Regler (number mit media_player): OK = stumm statt an/aus, eigene Schrittweite in %
            if t == "number" and it.get("ok") == "mute":
                o["ok"] = "mute"
            if t == "number" and it.get("st"):
                o["st"] = max(1, min(20, int(it["st"])))
            o.update(kachel_optionen(it, pid))
            if t == "widget":   # Widgets (2026-10-27, widgets.py / Firmware widgets.cpp)
                o.update(widget_optionen(it))
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
        # Popup-Knopf oben links (2026-10-27): "min" = minimieren (Menuetaste holt es zurueck), fehlt = X schliesst
        if p.get("pknopf") == "min":
            pg["pknopf"] = "min"
        # Lauter/Leiser/Stumm der Remote steuern auf dieser Seite einen Media Player (2026-10-26), Schritt in %
        if str(p.get("vol") or "").startswith("media_player."):
            pg["vol"] = str(p["vol"])[:80]
            pg["vst"] = max(1, min(20, int(p.get("vst") or 5)))
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
        if p.get("startseite"):   # Startseite einer Aktivitaet (2026-10-27)
            pg["startseite"] = True
        if p.get("inhalt") in ("tastatur", "ziffern", "touchpad"):   # eingebauter Inhalt statt Eintraegen (2026-10-13, Ziffern/Touchpad 2026-10-27)
            pg["inhalt"] = p["inhalt"]
        out_pages.append(pg)
    for p in out_pages:
        for it in p["items"]:
            if it["t"] == "page" and it.get("target") not in ids:
                raise vol.Invalid(f"Seite {p['id']}: Ziel '{it.get('target')}' gibt es nicht")
    start = str(m.get("start") or (out_pages[0]["id"] if out_pages else ""))
    out = {"start": start if start in ids else (out_pages[0]["id"] if out_pages else ""), "pages": out_pages}
    # Hervorhebung (Rahmen der Markierung, Regler, Zurueck-Knopf) fuer das ganze Menue, 2026-10-18
    if isinstance(m.get("hl"), str) and re.fullmatch(r"#[0-9a-fA-F]{6}", m["hl"]):
        out["hl"] = m["hl"].lower()
    return out


def device_payload(cfg: dict) -> bytes:
    """Kompakter Text, den die Remote laedt (ohne Leerzeichen)."""
    return json.dumps(cfg, separators=(",", ":"), ensure_ascii=False).encode()


def ist_dock(hass: HomeAssistant, ce) -> bool:
    """ESPHome-Eintrag ein Dock? Name "dock-..." oder Firmware-Projekt aggro600.esphomeremote-dock (2026-10-26: von
    Hand eingerichtete Docks mit anderem Namen fehlten im Konfigurator)."""
    if str(ce.data.get("device_name") or "").startswith("dock-"):
        return True
    from homeassistant.helpers import device_registry as dr
    return any((d.model or "") == "esphomeremote-dock" for d in dr.async_entries_for_config_entry(dr.async_get(hass), ce.entry_id))


class KonfigData:
    def __init__(self, hass: HomeAssistant, key: str, devices: list[str]) -> None:
        self.hass = hass
        self.key = key
        self.devices = list(devices)
        self.store = Store(hass, STORE_VERSION, DOMAIN)
        self.data: dict = {"devices": {}}
        self.spiegel = Spiegel(hass)
        from .sync import Sync
        self.sync = Sync(self)   # Remotes synchron halten (sync.py, 2026-10-22)
        self.abgeglichen: list[str] = []   # Remotes, die das letzte save/einst_setzen mit angepasst hat

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
        umgebaut = self._timer_popup() | self._vol_aufraeumen()
        for d in self.data["devices"].values():   # alter Block "player" -> Medien-Elemente (2026-10-26)
            umgebaut |= player_umbauen((d.get("config", {}).get("menu") or {}).get("pages"))
        if self._neue_einst_seiten() | self._eingabe_je_platz() | umgebaut:
            await self.store.async_save(self.data)
        for dev in self.devices:
            self.announce(dev)

    def _vol_aufraeumen(self) -> bool:
        """Einmalig (2026-10-26): Lautstaerke-Tasten einer Seite ("vol"), die die Umwandlung des alten Medien-Blocks
        gesetzt hat und die nicht zum Regler der Seite passen, entfernen - dann gilt im Popup die normale Belegung
        der Tasten (Nutzer: "im Medien-Popup gehen Lauter/Leiser nicht")."""
        if self.data.get("vol_rev"):
            return False
        self.data["vol_rev"] = 1
        for d in self.data["devices"].values():
            for p in (d.get("config", {}).get("menu") or {}).get("pages") or []:
                regler = [it.get("entity") for it in p.get("items") or [] if it.get("t") == "number"
                          and str(it.get("entity") or "").startswith("media_player.")]
                if p.get("vol") and regler and p["vol"] not in regler:
                    p.pop("vol", None)
                    p.pop("vst", None)
        return True

    def _timer_popup(self) -> bool:
        """Einmalig (2026-10-26): Popup "Timer abgelaufen" in jedes Menue (laesst sich danach frei aendern/loeschen)."""
        if self.data.get("timer_popup"):
            return False
        self.data["timer_popup"] = 1
        s = "sensor.esphomeremote_timer_abgelaufen"
        seite = {
            "id": "popup_timer", "title": "Timer", "remember": False, "flow": False,
            "popup": {"open": [{"entity": s, "attr": "status", "to": "abgelaufen"}], "close": [{"entity": s, "attr": "status", "to": "aus"}],
                      "timeout": 0, "wake": True, "excl": True},
            "items": [
                {"t": "sensor", "entity": s, "label": "Abgelaufen", "lay": "g", "icon": "timer-outline", "bg": "#7c2d12", "x": 0, "y": 0, "w": 6, "h": 2},
                {"t": "action", "label": "Dauer", "sub": {"e": s, "a": "dauer"}, "x": 0, "y": 2, "w": 6, "h": 1},
                {"t": "action", "label": "Abgelaufen um", "sub": {"e": s, "a": "abgelaufen_um"}, "x": 0, "y": 3, "w": 6, "h": 1},
                {"t": "action", "label": "+5 min", "icon": "plus", "align": "c", "x": 0, "y": 4, "w": 3, "h": 1,
                 "steps": [{"t": "ha", "svc": "esphomeremote_konfig.timer_verlaengern", "data": {"minuten": "5"}}, {"t": "int", "fn": "popup_close"}]},
                {"t": "action", "label": "Nochmal", "icon": "timer-outline", "align": "c", "x": 3, "y": 4, "w": 3, "h": 1,
                 "steps": [{"t": "ha", "svc": "esphomeremote_konfig.timer_neu_starten", "data": {}}, {"t": "int", "fn": "popup_close"}]},
                {"t": "action", "label": "OK", "align": "c", "size": "l", "bg": "#16a34a", "fg": "#ffffff", "x": 0, "y": 5, "w": 6, "h": 2,
                 "steps": [{"t": "int", "fn": "popup_close"}]},
            ]}
        n = False
        for d in self.data["devices"].values():
            m = d.get("config", {}).get("menu")
            if isinstance(m, dict) and not any(p.get("id") == "popup_timer" for p in m.get("pages") or []):
                m.setdefault("pages", []).append(json.loads(json.dumps(seite)))
                n = True
        return True

    def _eingabe_je_platz(self) -> bool:
        """2026-10-22: der eine Knopf "Eingabe" (galt fuer den gewaehlten Platz) ist ersetzt durch je einen Knopf pro
        Platz - in den gespeicherten Menues den alten Eintrag gegen die vier neuen tauschen."""
        geaendert = False
        for d in self.data["devices"].values():
            for p in (d.get("config", {}).get("menu") or {}).get("pages") or []:
                items = p.get("items") or []
                for n, it in enumerate(items):
                    if it.get("t") == "setting" and it.get("src") == "ble_eingabe_btn":
                        items[n:n + 1] = [{"t": "text", "w": 6, "h": 1, "label": "Eingabe je Platz"}] + [
                            {"t": "setting", "w": 6, "h": 1, "label": f"Eingabe Platz {i + 1}", "src": f"ble_eing{i}_btn"} for i in range(4)]
                        geaendert = True
                        break
        if geaendert:
            _LOGGER.info("Menue: Eingabe je Bluetooth-Platz eingebaut")
        return geaendert

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
        self.spiegel.weg(dev_slug(dev))

    def entry(self, dev: str) -> dict:
        return self.data["devices"][dev]

    def payload(self, dev: str) -> bytes:
        e = self.entry(dev)
        cfg = copy.deepcopy(e["config"])
        # "aus" = die Remote nutzt ihre eingebaute Belegung
        cfg["on"] = bool(e.get("enabled"))
        if e.get("einst"):
            cfg["einst"] = e["einst"]
        if dmac := self.dock_macs():
            cfg["dmac"] = dmac
        if dip := self.dock_ips():
            cfg["dip"] = dip
        return device_payload(cfg)

    def dock_macs(self) -> dict:
        """WLAN-MAC je Dock ({"dock_wz_couchtisch": "AC:27:..."}) fuer den direkten Funkweg Remote -> Dock
        (components/dock_direkt, 2026-10-18). Die ESPHome-Integration fuehrt die MAC als unique_id."""
        out = {}
        for ce in self.hass.config_entries.async_entries("esphome"):
            name, mac = ce.data.get("device_name") or "", ce.unique_id or ""
            if ist_dock(self.hass, ce) and re.fullmatch(r"[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}", mac):
                out[name.replace("-", "_")] = mac.upper()
        return dict(sorted(out.items()))

    def dock_ips(self) -> dict:
        """IP je Dock ({"dock_wz_couchtisch": "192.168.1.91"}) fuer den Heimnetz-Weg Remote -> Dock (2026-10-23):
        Funk (ESP-NOW) klappt nur auf demselben WLAN-Kanal, das Heimnetz auch ueber Box und Repeater."""
        out = {}
        for ce in self.hass.config_entries.async_entries("esphome"):
            name, host = ce.data.get("device_name") or "", str(ce.data.get("host") or "")
            if ist_dock(self.hass, ce) and re.fullmatch(r"\d{1,3}(\.\d{1,3}){3}", host):
                out[name.replace("-", "_")] = host
        return dict(sorted(out.items()))

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
            self.abgeglichen = await self.sync.einst_weiter(dev, {k: v for k, v in werte.items() if k in soll and soll[k][1] > n - neu})
        return neu

    @callback
    def einst_gemeldet(self, dev: str, werte: dict, hash_: str) -> None:
        e = self.entry(dev)
        licht = self.hass.data.get(DOMAIN + "_licht")
        if licht is not None:
            licht.einst_gemeldet(dev, e.get("einst_ist") or {}, werte)
        self.sync.an_remote_geaendert(dev, e.get("einst_ist") or {}, werte)   # an der Remote umgestellt -> Gruppe
        e["einst_ist"] = werte
        e["einst_ist_at"] = dt_util.now().isoformat(timespec="seconds")
        if hash_ == fnv1a(self.payload(dev)):
            e["einst_ist_rev"] = max([r for _, r in (e.get("einst") or {}).values()] + [0])
        self.store.async_delay_save(lambda: self.data, 5)
        self.hass.bus.async_fire(DOMAIN + "_einst", {"device": dev})

    def basis(self) -> str:
        """HA-Adresse fuer die Remote ("IP:Port" im Heimnetz): Bilder und Konfiguration holt sie per HTTP von dort."""
        from homeassistant.helpers.network import NoURLAvailableError, get_url
        from yarl import URL
        try:
            u = URL(get_url(self.hass, allow_external=False, allow_cloud=False, prefer_external=False))
        except NoURLAvailableError:
            return ""
        return f"{u.host}:{u.port}" if u.scheme == "http" and u.host else ""

    @callback
    def announce(self, dev: str) -> None:
        body = self.payload(dev)
        e = self.entry(dev)
        self.spiegel.setzen(dev_slug(dev), e["config"].get("ents") or {})
        if (c := self.hass.data.get(DOMAIN + "_cover")) is not None and dev_slug(dev) not in c.slugs:
            c.starten()   # neue Remote: ihr Cover mitverfolgen
        if (ds := self.hass.data.get(DOMAIN + "_dockstand")) is not None and dev not in ds.devs:
            ds.starten()   # neue Remote: ihren Stand an die Docks
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
                "basis": self.basis(),
            },
        )
        if ds is not None:
            ds.konfig_neu(dev)   # andere Popup-Ausloeser (2026-10-24)
            ds._planen(dev, 1)   # neuer Soll-Stand -> auch an die Docks (Remote ohne eigene HA-Verbindung)

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
        self.abgeglichen = await self.sync.nach_speichern(dev)   # Remotes synchron halten (sync.py)
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
        h = fnv1a(body)
        # Fortsetzen (2026-10-25): die Remote hat ab=<n> Byte von Stand h schon - Rest als 206 (Weg ueber das Dock brach
        # oft mitten drin ab). Anderer Stand oder unpassend: alles mit 200.
        try:
            ab = int(request.query.get("ab", "0"))
        except ValueError:
            ab = 0
        if 0 < ab < len(body) and request.query.get("h") == h:
            return web.Response(
                status=206,
                body=body[ab:],
                content_type="application/json",
                headers={"X-Konfig-Hash": h, "Cache-Control": "no-store"},
            )
        return web.Response(
            body=body,
            content_type="application/json",
            headers={"X-Konfig-Hash": h, "Cache-Control": "no-store"},
        )


class StatesView(HomeAssistantView):
    """Zustaende fuer die Menue-Kacheln der Remote (statt Dauer-Abos ueber die ESPHome-API,
    die auf der Remote internes RAM kosten). GET .../states?k=<key>&e=a,b,c
    Antwort: {"entity": [state, brightness 0-255|-1, current_position|-1, unit(, min, max, step)]}
    media_player zusaetzlich: Titel, Interpret/App, stumm, Zufall, Wiederholen (Medien-Elemente)
    media_player: current_position = Lautstaerke 0-100; number/input_number: zusaetzlich min, max, step"""

    url = "/api/esphomeremote_konfig/states"
    name = "api:esphomeremote_konfig:states"
    requires_auth = False

    def __init__(self, kd: KonfigData, wetter=None) -> None:
        self.kd = kd
        self.wetter = wetter

    async def get(self, request: web.Request) -> web.Response:
        if not schluessel_ok(request, self.kd.key):
            return web.Response(status=403)
        hass = request.app["hass"]
        out = {}
        # Kachel-Quellen (2026-10-26): a=entity:attr,... -> {"entity:attr": ["Wert"]}
        for ea in request.query.get("a", "").split(",")[:40]:
            e, _, at = ea.strip().partition(":")
            st = hass.states.get(e) if e and at else None
            if st is None:
                continue
            v = st.attributes.get(at)
            v = "" if v is None else ("true" if v is True else "false" if v is False
                                      else (f"{v:g}" if isinstance(v, float) else str(v)))
            out[f"{e}:{at}"] = [v[:60]]
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
            elif e.startswith("media_player."):   # Medien-Steuerung (2026-10-26): Titel, Interpret/App, stumm
                eintrag += [str(a.get("media_title") or "")[:80],
                            str(a.get("media_artist") or a.get("media_series_title") or a.get("app_name") or "")[:60],
                            bool(a.get("is_volume_muted")), bool(a.get("shuffle")), str(a.get("repeat") or ""),
                            str(a.get("app_name") or "")[:40], str(a.get("source") or "")[:40]]
                # Fortschritt: Position, Dauer (s) und wie alt die Position ist (s)
                p, d, u = a.get("media_position"), a.get("media_duration"), a.get("media_position_updated_at")
                alter = 0.0
                if u is not None:
                    try:
                        u = u if not isinstance(u, str) else dt_util.parse_datetime(u)
                        alter = max(0.0, (dt_util.utcnow() - u).total_seconds())
                    except (TypeError, ValueError, AttributeError):
                        alter = 0.0
                eintrag += [float(p) if isinstance(p, (int, float)) else -1.0,
                            float(d) if isinstance(d, (int, float)) else 0.0, round(alter, 1)]
            elif e.startswith("weather.") and self.wetter is not None:   # Widget Wetter (2026-10-27)
                eintrag += wetter_eintrag(self.wetter, st)
            elif e.startswith("timer."):   # Widget Timer: Restzeit, Dauer, Name
                eintrag += timer_eintrag(st)
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

    # Neues/entferntes Dock: Remotes bekommen die Funk-Adressen (dmac) mit der naechsten Konfiguration
    @callback
    def _freigaben() -> None:
        """Remotes und Docks muessen HA-Aktionen ausfuehren duerfen (Tasten, Menue, HA-Befehle ueber das Dock).
        Beim Einbinden ueber den Konfigurator blieb die Option aus (Gast, 2026-10-19) - hier immer nachziehen."""
        for e in hass.config_entries.async_entries("esphome"):
            name = str(e.data.get("device_name") or "")
            if (ist_dock(hass, e) or name in kd.devices) and not e.options.get("allow_service_calls"):
                _LOGGER.info("%s: erlaube HA-Aktionen", name)
                hass.config_entries.async_update_entry(e, options={**e.options, "allow_service_calls": True})

    @callback
    def _eintrag_geaendert(_aenderung, eintrag) -> None:
        if eintrag.domain == "esphome":
            hass.async_create_task(_freigaben_spaeter())
        if eintrag.domain == "esphome" and ist_dock(hass, eintrag):
            for dev in kd.devices:
                kd.announce(dev)

    async def _freigaben_spaeter() -> None:
        await asyncio.sleep(2)   # nicht mitten im Einrichten des Eintrags
        _freigaben()

    _freigaben()

    from homeassistant.config_entries import SIGNAL_CONFIG_ENTRY_CHANGED
    from homeassistant.helpers.dispatcher import async_dispatcher_connect
    async_dispatcher_connect(hass, SIGNAL_CONFIG_ENTRY_CHANGED, _eintrag_geaendert)

    hass.http.register_view(CfgView(kd))
    hass.http.register_view(StatesView(kd, WetterCache(hass)))
    hass.http.register_view(BildView(kd, schluessel_ok))   # Widget-Bilder (Kamera, Cover), 2026-10-27
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
    websocket_api.async_register_command(hass, ws_sicherung)
    websocket_api.async_register_command(hass, ws_wlan_an)
    websocket_api.async_register_command(hass, ws_einspielen)
    websocket_api.async_register_command(hass, ws_default)
    websocket_api.async_register_command(hass, ws_werk_speichern)
    websocket_api.async_register_command(hass, ws_werk_get)
    websocket_api.async_register_command(hass, ws_bt_proxies)
    websocket_api.async_register_command(hass, ws_einst_set)
    websocket_api.async_register_command(hass, ws_einst_standard)
    for fn in (ws_sync_get, ws_sync_set, ws_sync_jetzt):
        websocket_api.async_register_command(hass, fn)

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

    from .klingel import einrichten as klingel_einrichten

    klingel_einrichten(hass, kd)   # Klingel-, Kamera- und Live-Bilder (2026-10-19)
    from .streamer import einrichten as streamer_einrichten

    streamer_einrichten(hass, kd)   # Streamer-App per Webhook (vor dem Dock: die "Jetzt laeuft"-Player brauchen sie)
    from .cover import Cover

    hass.data[DOMAIN + "_cover"] = cover = Cover(hass, kd)   # Cover je Remote aus ihrem "Jetzt laeuft"-Player
    cover.starten()
    from .auswertung import registrieren as auswertung_registrieren

    auswertung_registrieren(hass)   # Akku-Auswertung (Konfigurator-Seite "Akku")
    from .zustand import einrichten as zustand_einrichten

    zustand_einrichten(hass, kd)   # Stand je Remote an die Docks (Startseite sofort nach dem Tiefschlaf)
    from .timer import einrichten as timer_einrichten

    timer_einrichten(hass, kd)   # Timer abgelaufen -> Popup (2026-10-26)
    from .dockscan import einrichten as dockscan_einrichten

    dockscan_einrichten(hass, kd)   # Suchtempo der Docks je nach Lage (2026-10-22)
    from .dockstatus import einrichten as dockstatus_einrichten
    dockstatus_einrichten(hass)     # Werte der Remotes ueber das Dock (2026-10-23, Umbau "Dock als Zentrale")

    from .regeln import registrieren as regeln_registrieren

    regeln_registrieren(hass)

    from .blaster import registrieren as blaster_registrieren

    blaster_registrieren(hass)   # fremde IR-/RF-Sender wie ein Dock (Aktion blaster_senden)

    from .fehler import einrichten as fehler_einrichten

    await fehler_einrichten(hass)   # bekannte Fehler fremder Software auf GitHub beobachten (Dockingstation)

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
    kd.abgeglichen = []
    try:
        n = await kd.einst_setzen(dev, werte)
    except vol.Invalid as err:
        connection.send_error(msg["id"], "invalid", str(err))
        return
    w, offen = kd.einst_werte(dev)
    connection.send_result(msg["id"], {"geaendert": n, "einst": w, "einst_offen": offen, "abgeglichen": kd.abgeglichen})


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


# ---- Werkseinstellung (2026-10-27, Nutzerwunsch): Belegung + Einstellungen einer Remote als Datensatz merken
# (kd.data["werk"], nur in .storage - nicht im oeffentlichen Repo). Laden im Konfigurator: alles, nur Menue, nur Tasten.
@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/werk_speichern", vol.Required("device"): str})
@websocket_api.async_response
async def ws_werk_speichern(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    kd: KonfigData = hass.data[DOMAIN]
    dev = msg["device"]
    if dev not in kd.devices:
        connection.send_error(msg["id"], "unknown_device", "Unbekannte Remote")
        return
    kd.data["werk"] = {"config": copy.deepcopy(kd.data["devices"][dev]["config"]), "einst": kd.einst_werte(dev)[0],
                       "quelle": dev, "zeit": dt_util.utcnow().isoformat()}
    kd.data["einst_standard"] = kd.data["werk"]["einst"]   # neue Remotes starten mit diesen Einstellungen
    await kd.store.async_save(kd.data)
    connection.send_result(msg["id"], {"quelle": dev, "zeit": kd.data["werk"]["zeit"]})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/werk_get"})
@callback
def ws_werk_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    kd: KonfigData = hass.data[DOMAIN]
    w = kd.data.get("werk")
    if w:
        connection.send_result(msg["id"], {**w, "eigen": True})
    else:   # noch nichts gemerkt: eingebaute Belegung der Firmware
        connection.send_result(msg["id"], {"config": default_config(), "einst": kd.data.get("einst_standard") or {}, "eigen": False})


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
    kd.abgeglichen = []
    try:
        h = await kd.save(dev, msg["config"], msg["enabled"], user)
    except (vol.Invalid, ValueError, TypeError) as err:
        connection.send_error(msg["id"], "invalid", str(err))
        return
    connection.send_result(msg["id"], {"hash": h, "bytes": len(kd.payload(dev)), "abgeglichen": kd.abgeglichen})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/wlan_an", vol.Required("device"): str})
@websocket_api.async_response
async def ws_wlan_an(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """WLAN der Remote fuer ein paar Minuten einschalten (2026-10-26, Konfigurator Bluetooth-Plaetze): ueber den
    HA-Knopf "Wartungsfenster jetzt" (eigene Verbindung/Bruecke), sonst ueber das Dock - dann, sobald die Remote das
    Dock das naechste Mal fragt (beim Benutzen sofort)."""
    kd: KonfigData = hass.data[DOMAIN]
    dev = msg["device"]
    if dev not in kd.devices:
        connection.send_error(msg["id"], "unknown_device", "Unbekannte Remote")
        return
    knopf = f"button.{dev.replace('-', '_')}_wartungsfenster_jetzt"
    st = hass.states.get(knopf)
    if st is not None and st.state != "unavailable":
        try:
            await hass.services.async_call("button", "press", {"entity_id": knopf}, blocking=True)
            connection.send_result(msg["id"], {"weg": "direkt"})
            return
        except Exception:  # noqa: BLE001 - Knopf nach HA-Neustart "verfuegbar", Remote aber nie verbunden: Dock
            pass
    ds = hass.data.get("esphomeremote_konfig_dockstand")
    if ds is None:
        connection.send_error(msg["id"], "kein_weg", "Weder direkte Verbindung noch Dock")
        return
    ds.wartung_anfordern(dev, 600)
    connection.send_result(msg["id"], {"weg": "dock"})


# ---- Sicherung (2026-10-26): alles, was der Konfigurator verwaltet, und Einspielen ohne Abgleich ----
SICHERUNG_GLOBAL = ("jetzt", "streamer", "klingel", "licht", "anzeige", "dock", "dockscan")


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/sicherung"})
@websocket_api.async_response
async def ws_sicherung(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Gespeicherter Stand: je Remote Belegung, An/Aus und Soll-Einstellungen; dazu die gemeinsamen Bereiche."""
    kd: KonfigData = hass.data[DOMAIN]
    import copy
    geraete = {}
    for dev in kd.devices:
        e = kd.entry(dev)
        geraete[dev] = {"config": copy.deepcopy(e["config"]), "enabled": bool(e.get("enabled")),
                        "einst": {k: v[0] if isinstance(v, list) else v for k, v in (e.get("einst") or {}).items()}}
    glob = {k: copy.deepcopy(kd.data[k]) for k in SICHERUNG_GLOBAL if kd.data.get(k) is not None}
    if kd.data.get("sync"):
        glob["sync"] = {k: copy.deepcopy(kd.data["sync"].get(k)) for k in ("an", "geraete", "aus")}
    connection.send_result(msg["id"], {"order": list(kd.devices), "remotes": geraete, "global": glob})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/einspielen", vol.Required("device"): str,
                                  vol.Optional("config"): dict, vol.Optional("enabled", default=True): bool,
                                  vol.Optional("einst"): dict, vol.Optional("abgleich", default=False): bool})
@websocket_api.async_response
async def ws_einspielen(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Eine Remote aus der Sicherung: Belegung und/oder Einstellungen. abgleich=False: die Abgleich-Gruppe bekommt
    nichts davon ab (sonst ueberschriebe das Einspielen einer Remote die anderen mit)."""
    kd: KonfigData = hass.data[DOMAIN]
    dev = msg["device"]
    if dev not in kd.devices:
        connection.send_error(msg["id"], "unknown_device", f"Unbekannte Remote {dev}")
        return
    user = connection.user.name if connection.user else None
    kd.sync.pause = not msg["abgleich"]
    try:
        h, n = None, 0
        if "config" in msg:
            h = await kd.save(dev, msg["config"], msg["enabled"], f"Sicherung ({user})" if user else "Sicherung")
        if msg.get("einst"):
            werte = {k: v for k, v in msg["einst"].items() if isinstance(v, (bool, int))}
            n = await kd.einst_setzen(dev, werte)
    except (vol.Invalid, ValueError, TypeError) as err:
        connection.send_error(msg["id"], "invalid", str(err))
        return
    finally:
        kd.sync.pause = False
    connection.send_result(msg["id"], {"hash": h, "einst": n})


# ---- Remotes synchron halten (sync.py, 2026-10-22) ----
def _sync_stand(kd: KonfigData) -> dict:
    from .sync import CFG_BEREICHE, STANDARD_AUS
    s = kd.sync.d
    return {"an": s["an"], "geraete": s["geraete"], "aus": s["aus"], "log": s["log"], "bereiche": list(CFG_BEREICHE),
            "standard_aus": list(STANDARD_AUS), "unterschiede": kd.sync.unterschiede()}


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/sync_get"})
@callback
def ws_sync_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    connection.send_result(msg["id"], _sync_stand(hass.data[DOMAIN]))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/sync_set", vol.Optional("an"): bool,
                                  vol.Optional("geraete"): [str], vol.Optional("aus"): [str]})
@websocket_api.async_response
async def ws_sync_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Abgleich ein/aus, Remotes der Gruppe, abgewaehlte Bereiche/Einstellungen. Gleicht selbst noch nichts an."""
    kd: KonfigData = hass.data[DOMAIN]
    s = kd.sync.d
    if "geraete" in msg:
        s["geraete"] = [d for d in kd.devices if d in msg["geraete"]]
    if "aus" in msg:
        s["aus"] = sorted({str(x)[:48] for x in msg["aus"] if re.fullmatch(r"(cfg|einst):[a-z][a-z0-9_]{0,31}", str(x))})
    if "an" in msg:
        s["an"] = msg["an"]
    await kd.store.async_save(kd.data)
    connection.send_result(msg["id"], _sync_stand(kd))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/sync_jetzt", vol.Required("von"): str})
@websocket_api.async_response
async def ws_sync_jetzt(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Alles Gewaehlte von einer Remote an die anderen der Gruppe."""
    kd: KonfigData = hass.data[DOMAIN]
    if msg["von"] not in kd.sync.d["geraete"]:
        connection.send_error(msg["id"], "unknown_device", "Die Vorlage gehört nicht zur Gruppe")
        return
    erg = await kd.sync.jetzt(msg["von"])
    connection.send_result(msg["id"], {**_sync_stand(kd), "ergebnis": erg})


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
