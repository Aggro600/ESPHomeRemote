"""Vorbelegung fuer eine neue Fernbedienung: neutrales Beispiel ohne eigene Entitaeten.

Tasten: Bluetooth-Befehle an Streamer bzw. Fernseher (wie eine normale Android-TV-Fernbedienung).
Menue: Medienplayer, Kameras, Bildschirmtastatur und die Einstellungen der Remote.
Alles Weitere (Licht, Szenen, Popups ...) legt man im Panel selbst an.
"""

from __future__ import annotations

from .einstellungen import EINST_SEITEN


def ble(code: int, kind: str = "consumer") -> dict:
    return {"t": "ble", "kind": kind, "code": code, "dev": -1}


def fn(name: str, arg: int | None = None) -> dict:
    s = {"t": "int", "fn": name}
    if arg is not None:
        s["arg"] = arg
    return s


def held(code: int) -> dict:
    """Taste wird am Geraet gehalten (es wiederholt selbst), Loslassen gibt frei."""
    return {"short": [ble(code)], "hold": True}


def tasten() -> dict:
    return {
        "14": held(0x0042),  # Hoch
        "35": held(0x0043),  # Runter
        "15": held(0x0044),  # Links
        "32": held(0x0045),  # Rechts
        "34": held(0x0041),  # OK
        "13": held(0x0224),  # Zurueck
        "3": held(0x0223),  # "Menu" -> Home am Geraet
        "33": held(0x00E9),  # Lauter
        "43": held(0x00EA),  # Leiser
        "44": held(0x00E2),  # Stumm
        "2": held(0x00CD),  # Play/Pause
        "5": held(0x00B7),  # Stop
        "4": held(0x00B4),  # Ruecklauf
        "11": held(0x00B3),  # Vorlauf
        "22": {"long": [ble(0x009D)]},  # Kanal -
        "42": {"long": [ble(0x009C)]},  # Kanal +
        "31": {"short": [fn("menu_smart")], "double": [fn("display_off")], "long": [fn("menu_top")]},  # Menuetaste
        "102": {"short": [ble(0x0030)], "double": [fn("clean")]},  # Aus-Taste: Power, doppelt Reinigungsmodus
        "45": {"short": [ble(0x0221)], "double": [fn("assistant_toggle")], "long": [fn("voice_ptt")]},  # Mikrofon
    }


def _pg(target: str, label: str) -> dict:
    return {"t": "page", "target": target, "label": label, "w": 6, "h": 1}


def _sp(page: str, label: str, w: int = 6) -> dict:
    return {"t": "special", "page": page, "label": label, "w": w, "h": 1}


def default_menu() -> dict:
    seiten = [
        {"id": "main", "title": "Menü", "items": [
            _sp("media", "Medienplayer"),
            _pg("kameras", "Kameras"),
            _sp("ziffern", "Ziffern", 2), _sp("tastatur", "Tastatur", 2), _sp("touchpad", "Touchpad", 2),
            _pg("settings", "Einstellungen"),
        ]},
        {"id": "kameras", "title": "Kameras", "items": [
            {"t": "text", "label": "Kameras unter „Kameras“ im Panel eintragen.", "w": 6, "h": 0},
            _sp("cam_view", "Kamerabild"), _sp("cam_live", "Live-Bild (schnell)"),
        ]},
        {"id": "settings", "title": "Einstellungen", "items": [x for s in EINST_SEITEN for x in
                                                              ([_pg(s["id"], s["title"])] + ([_sp("gesten", "Gesten")] if s["id"] == "set_extras" else []))]},
    ]
    for s in seiten:
        s.setdefault("remember", True)
        s.setdefault("flow", True)
    return {"start": "main", "pages": [*seiten, *EINST_SEITEN]}


def default_config() -> dict:
    return {
        "v": 1,
        "slots": ["Streamer", "Fernseher", "", ""],
        "activities": [
            {"name": "Streamer", "slot": 0, "keys": tasten()},
            {"name": "TV", "slot": 1, "keys": tasten()},
        ],
        "menu": default_menu(),
    }
