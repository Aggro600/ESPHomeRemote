"""Vorbelegung fuer eine neue Fernbedienung: neutrales Beispiel ohne eigene Entitaeten.

Tasten: Bluetooth-Befehle an Streamer bzw. Fernseher (wie eine normale Android-TV-Fernbedienung).
Menue: Medienplayer, Kameras, Bildschirmtastatur und die Einstellungen der Remote.
Alles Weitere (Licht, Szenen, Popups ...) legt man im Panel selbst an.
"""

from __future__ import annotations

from .settings import EINST_SEITEN


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

# Empfohlene Einstellungen fuer neue Remotes (2026-11-03, Stand der Remote des Entwicklers). Bewusst nicht dabei: je Person
# (Sprache, Linkshaender, X-Knopf), Helligkeit und alles, was ein Dock voraussetzt (Funkwege) - ohne Dock waere die
# Remote sonst vom Netz getrennt. Eigene Vorgabe: Konfigurator > Einstellungen > als Standard merken.
EINST_STANDARD: dict = {
    'kbd_light_on': False,
    'disp_timeout_s': 16,
    'ps_dim_s': 5,
    'ps_dim_pct': 20,
    'show_ble': True,
    'show_wifi': True,
    'show_batt': True,
    'show_clock': True,
    'show_assi_icon': True,
    'show_motion': True,
    'show_ha': True,
    'cover_ganz_player': True,
    'cover_ganz_home': True,
    'mem_s': 120,
    'media_merken': 0,
    'menu_close_s': 20,
    'sd_logos': False,
    'ps_wifi': True,
    'ps_wifi_max': True,
    'ps_cpu': True,
    'ps_cpu_s': 5,
    'ps_light': True,
    'wlan_frueh': True,
    'ps_bleidle': True,
    'ps_ble_s': 5,
    'motion_mode': 0,
    'motion_sens': 0,
    'keys_wake_all': False,
    'menue_weckt_nur': True,
    'dim_wake': True,
    'dim_hold_motion': True,
    'dim_sens': 0,
    'klopf_wake': False,
    'klopf_sens': 0,
    'ps_touch_wake': False,
    'ps_touch_ms': 75,
    'ps_deep': True,
    'deep_ohne_ble': False,
    'ps_deep_s': 600,
    'ps_deep_tv_s': -1,
    'ps_low_pct': 5,
    'assistant': 0,
    'google_mode': 0,
    'tts_speaker': False,
    'mic_allowed': True,
    'mic_vorwaermen': False,
    'ble_parallel': False,
    'bt_weg': 2,
    'bt_notweg': 1,
    'clean_s': 120,
    'ps_log_debug': True,
    'dock_disp': 2,
    'dock_von': 7,
    'dock_bis': 24,
    'musik_dock': True,
    'musik_player': True,
    'begruessung': False,
    'weck_tasten': 2,
    'tief_motion': 1,
    'bewegung_haelt': True,
    'maus_slots': 12,
    'voll_slots': 12,
    'maus_tempo': 100,
    'maus_dpad': True,
    'maus_dpad_tempo': 100,
    'maus_zurueck': 1,
    'gesten_an': False,
    'gesten_dunkel': False,
    'gesten_sens': 2
}
