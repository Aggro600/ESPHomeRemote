"""Vorbelegung = die heute fest eingebaute Belegung der Remote (Stand 2026-10-03).

Damit aendert das Einschalten des Konfigurators nichts am Verhalten, bis man
selbst etwas umstellt.
"""

from __future__ import annotations

from .einstellungen import EINST_SEITEN

import copy

ADB = "media_player.android_tv_192_168_2_79"
SMARTTUBE = "am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -p org.smarttube.stable"


def ble(code: int, kind: str = "consumer", dev: int = -1) -> dict:
    return {"t": "ble", "kind": kind, "code": code, "dev": dev}


def ha(svc: str, entity: str | None = None, **data) -> dict:
    s = {"t": "ha", "svc": svc, "data": {k: str(v) for k, v in data.items()}}
    if entity:
        s["entity"] = entity
    return s


def fn(name: str, arg: int | None = None) -> dict:
    s = {"t": "int", "fn": name}
    if arg is not None:
        s["arg"] = arg
    return s


def held(code: int) -> dict:
    return {"short": [ble(code)], "hold": True}


def _common() -> dict:
    return {
        "14": held(0x0042),  # Hoch
        "35": held(0x0043),  # Runter
        "15": held(0x0044),  # Links
        "32": held(0x0045),  # Rechts
        "34": held(0x0041),  # OK
        "13": held(0x0224),  # Zurueck
        "3": held(0x0223),  # "Menu" S23 -> Home am Geraet
        "33": held(0x00E9),  # Lauter
        "43": held(0x00EA),  # Leiser
        "44": held(0x00E2),  # Mute
        "2": held(0x00CD),  # Play/Pause
        "5": held(0x00B7),  # Stop
        "4": held(0x00B4),  # Rewind
        "11": held(0x00B3),  # Forward
        "22": {"long": [ble(0x009D)]},  # Kanal -
        "42": {"long": [ble(0x009C)]},  # Kanal +
        "23": {  # Rot
            "short": [ha("light.toggle", "light.hangelampe", brightness_pct=100, color_temp_kelvin=2000)],
            "double": [ha("scene.turn_on", "scene.wohnzimmer_tv")],
            "long": [ha("light.turn_off", "light.wohnzimmer")],
        },
        "41": {  # Return / Taste rund ums D-Pad
            "short": [ha("androidtv.adb_command", ADB, command=SMARTTUBE)],
            "long": [ha("script.turn_on", "script.esphomeremote_streamer_close_app")],
        },
        "31": {  # Menuetaste der Remote
            "short": [fn("menu_smart")],
            "double": [fn("display_off")],
            "long": [fn("menu_top")],
        },
        "102": {  # Aus-Taste
            "short": [ha("script.turn_on", "script.tv_wohnzimmer_bildschirm_aus")],
            "double": [fn("clean")],
            "long": [ble(0x0030)],
        },
        "45": {  # Mikrofon
            "short": [ble(0x0221)],
            "double": [fn("assistant_toggle")],
            "long": [fn("voice_ptt")],
        },
    }


def sp(page: str, label: str, icon: str = "") -> dict:
    d = {"t": "special", "page": page, "label": label, "w": 6, "h": 1}
    if icon:
        d["icon"] = icon
    return d


def btn(label: str, steps: list, w: int = 2, icon: str = "") -> dict:
    d = {"t": "action", "label": label, "w": w, "h": 1, "steps": steps}
    if icon:
        d["icon"] = icon
    return d


def kamera_groesse() -> list:
    """Groesse der Kamera auf dem TV (gilt fuer alle, input_select.tv_wz_kamera_groesse)."""
    w = lambda o: [ha("input_select.select_option", "input_select.tv_wz_kamera_groesse", option=o)]   # noqa: E731
    return [
        {"t": "sensor", "label": "Größe auf dem TV", "entity": "input_select.tv_wz_kamera_groesse", "w": 6, "h": 1,
         "steps": [ha("input_select.select_next", "input_select.tv_wz_kamera_groesse", cycle="true")]},
        btn("Klein", w("Klein")), btn("Mittel", w("Mittel")), btn("Groß", w("Groß")),
    ]


def kamera_popup() -> dict:
    """Popup, das HA bei jeder Kamera auf dem TV WZ oeffnet (packages/esphomeremote_kamera_tv.yaml)."""
    s = lambda e: [ha("script.turn_on", e)]          # noqa: E731
    p = lambda e: [ha("button.press", e)]             # noqa: E731
    return {"id": "kameras", "title": "Kamera", "remember": False, "flow": True, "items": [
        {"t": "text", "label": "Auf dem TV zeigen", "w": 6, "h": 1},
        btn("Haustür", s("script.kamera_haustur_auf_tv_wz")),
        btn("Garten", s("script.kamera_garten_auf_tv_wz")),
        btn("Wohnungst.", s("script.kamera_wohnungstur_auf_tv_wz")),
        btn("Zelt", s("script.kamera_zelt_auf_tv_wz")),
        btn("3D-Drucker", s("script.kamera_3d_drucker_auf_tv_wz")),
        btn("Aus", s("script.kamera_auf_tv_wz_aus")),
        *kamera_groesse(),
        {"t": "text", "label": "Garten-Kamera", "w": 6, "h": 1},
        btn("Schwenken", s("script.kamera_garten_schwenken"), 3),
        {"t": "sensor", "label": "Position", "entity": "input_select.kamera_garten_position", "w": 3, "h": 1},
        btn("", p("button.garten_ptz_links"), icon="arrow-left"),
        btn("Stopp", p("button.garten_ptz_stopp")),
        btn("", p("button.garten_ptz_rechts"), icon="arrow-right"),
        btn("", p("button.garten_ptz_hoch"), icon="arrow-up"),
        btn("Zoom +", p("button.garten_ptz_zoom_vergrossern")),
        btn("", p("button.garten_ptz_runter"), icon="arrow-down"),
        {"t": "text", "label": "Auf dem Tablet", "w": 6, "h": 1},
        btn("Garten", s("script.kamera_garten_popup_auf_tablet"), 3),
        btn("Haustür", s("script.kamera_haustur_popup_auf_tablet"), 3),
    ]}


def _t(label):
    return {"t": "text", "label": label, "w": 6, "h": 1}


def _e(t, label, entity, w=6, h=1, icon=""):
    d = {"t": t, "label": label, "entity": entity, "w": w, "h": h}
    if icon:
        d["icon"] = icon
    return d


def _scene(label, entity, w=2):
    return btn(label, [ha("scene.turn_on", entity)], w)


def _rollo(entity):
    pos = lambda n: [ha("cover.set_cover_position", entity, position=n)]  # noqa: E731
    return [_e("cover", "Rollo", entity, 6, 2, "window-shutter"), btn("20 %", pos(20), 3), btn("50 %", pos(50), 3)]


def raum_seiten() -> list:
    """Raumseiten wie die eingebauten (page_room_*), als editierbare Menueseiten."""
    fan = lambda svc, e: [ha(svc, e)]  # noqa: E731
    seiten = [
        ("raum_wz", "Wohnzimmer", [
            _t("Szenen"), _scene("TV", "scene.wohnzimmer_tv"), _scene("Kino", "scene.wohnzimmer_kino"), _scene("Hell", "scene.wohnzimmer_hell"),
            _e("light", "Licht", "light.wohnzimmer", 6, 2, "ceiling-light"),
            sp("media", "Media Player"),
            _e("toggle", "Sternenhimmel", "light.sternenhimmel"),
            _e("toggle", "Ambilight", "switch.ambilight_wz_steckdose"),
            _e("toggle", "Ventilator", "fan.ventilator_wz"),
            btn("–", fan("fan.decrease_speed", "fan.ventilator_wz")), btn("+", fan("fan.increase_speed", "fan.ventilator_wz")),
            btn("Drehen", [ha("script.turn_on", "script.ventilator_wz_drehung")]),
            *_rollo("cover.wohnzimmer_rollo"),
        ]),
        ("raum_sz", "Schlafzimmer", [
            _e("light", "Licht", "light.schlafzimmer", 6, 2, "ceiling-light"),
            *_rollo("cover.schlafzimmer_rollo"),
        ]),
        ("raum_ku", "Küche", [
            _t("Szenen"), _scene("Hell", "scene.kuche_hell", 3), _scene("Kochen", "scene.kuche_kochen", 3),
            _scene("Kaffeeecke", "scene.kuche_kaffeeecke_hell", 3), _scene("Küchenzeile", "scene.kuche_kuchenzeile_hell", 3),
            _e("light", "Licht", "light.kuche", 6, 2, "ceiling-light"),
            _e("toggle", "Kochen", "input_boolean.kochen"),
            _e("toggle", "Dunstabzug", "fan.dunstabzugshaube"),
            btn("Lüfter –", fan("fan.decrease_speed", "fan.dunstabzugshaube"), 3), btn("Lüfter +", fan("fan.increase_speed", "fan.dunstabzugshaube"), 3),
            _e("toggle", "Licht Dunstabzug", "light.lampe_dunstabzug"),
            _e("sensor", "Backofen", "sensor.backofen_status"),
            *_rollo("cover.kuche_rollo"),
        ]),
        ("raum_bad", "Badezimmer", [
            _t("Szenen"), _scene("Hell", "scene.bad_hell"), _scene("Duschen", "scene.bad_duschen"), _scene("Nacht", "scene.bad_nachts"),
            _e("light", "Licht", "light.bad", 6, 2, "ceiling-light"),
            _e("toggle", "Lüfter", "switch.badlufter"),
        ]),
        ("raum_flur", "Flur", [
            _t("Szenen"), _scene("Hell", "scene.flur_hell"), _scene("Abend", "scene.flur_abends"), _scene("Nacht", "scene.flur_nachts"),
            _e("light", "Licht", "light.flur", 6, 2, "ceiling-light"),
        ]),
        ("raum_alle", "Alle Räume", [
            btn("Alle Lichter aus", [ha("light.turn_off", e) for e in
                ("light.wohnzimmer", "light.schlafzimmer", "light.kuche", "light.bad", "light.flur")], 6, "lightbulb-off"),
        ]),
    ]
    return [{"id": i, "title": t, "remember": True, "flow": True, "items": items} for i, t, items in seiten]


def raeume_liste() -> dict:
    return {"id": "rooms", "title": "Räume", "remember": True, "flow": True, "items": [
        {"t": "page", "target": i, "label": t, "w": 6, "h": 1} for i, t in
        (("raum_wz", "Wohnzimmer"), ("raum_sz", "Schlafzimmer"), ("raum_ku", "Küche"),
         ("raum_bad", "Badezimmer"), ("raum_flur", "Flur"), ("raum_alle", "Alle Räume"))]}


def _pg(target, label, icon=""):
    d = {"t": "page", "target": target, "label": label, "w": 6, "h": 1}
    if icon:
        d["icon"] = icon
    return d


def _app(label, cmd):
    return btn(label, [ha("androidtv.adb_command", ADB, command=cmd), fn("home")], 6)


def menue_seiten() -> list:
    """Das bisherige (eingebaute) Menue als editierbare Seiten. Seiten mit Remote-interner
    Logik (TV, Streamer, Tablet, Lautsprecher, Sleeptimer, Tastatur, Einstellungs-Unterseiten)
    bleiben eingebaut und sind als "special" an ihrer Stelle eingehaengt."""
    lc = "am start -a android.intent.action.MAIN -c android.intent.category.LAUNCHER "
    s = lambda e: [ha("script.turn_on", e)]  # noqa: E731
    seiten = [
        ("main", "Menü", [
            _pg("rooms", "Räume"), _pg("apps", "Apps"), _pg("smarthome", "SmartHome"),
            _pg("devices", "Geräte & Medien"), _pg("settings", "Einstellungen"),
        ]),
        ("apps", "Apps", [
            _app("SmartTube", lc + "-p org.smarttube.stable"),
            _app("Netflix", lc + "-n com.netflix.ninja/.MainActivity"),
            _app("Disney+", lc + "-n com.disney.disneyplus/com.bamtechmedia.dominguez.main.MainActivity"),
            _app("Amazon Prime", lc + "-n com.amazon.amazonvideo.livingroom/com.amazon.ignition.IgnitionActivity"),
            _app("RTL+", lc + "-n de.rtli.tvnow/com.bedrockstreaming.shared.tv.activity.SplashActivity"),
            _app("Kodi", lc + "-n org.xbmc.kodi/.Splash"),
            _app("YouTube", lc + "-p com.google.android.youtube.tv"),
        ]),
        ("smarthome", "SmartHome", [
            _pg("cams", "Kameras"),
            sp("sleeptimer", "Sleeptimer"),
        ]),
        ("cams", "Kameras", [
            _t("Auf der Remote"),
            sp("cam_view", "Kamerabild"), sp("cam_live", "Live-Bild (schnell)"),
            _t("Auf dem Fernseher"),
            btn("Haustür auf TV", s("script.kamera_haustur_auf_tv_wz"), 6),
            btn("Wohnungstür auf TV", s("script.kamera_wohnungstur_auf_tv_wz"), 6),
            btn("Garten auf TV", s("script.kamera_garten_auf_tv_wz"), 6),
            btn("Kamera Garten schwenken", s("script.kamera_garten_schwenken"), 6),
            btn("3D-Drucker auf TV", s("script.kamera_3d_drucker_auf_tv_wz"), 6),
            btn("Zelt auf TV", s("script.kamera_zelt_auf_tv_wz"), 6),
            btn("Kamera auf TV aus", s("script.kamera_auf_tv_wz_aus"), 6),
            _t("Auf dem Tablet"),
            _e("toggle", "Kamera auf Tablet WZ", "input_boolean.dashboard_wz_kamera"),
        ]),
        ("devices", "Geräte & Medien", [
            _pg("activities", "Aktivitäten"),
            _t("Geräte"),
            sp("tv", "TV"), sp("streamer", "Streamer"), sp("tablet", "Tablet"),
            _t("Medien"),
            _pg("music", "Musik"), sp("speaker", "Lautsprecher"), sp("sd_menu", "SD-Menü"),
            _t("Eingabe"),
            sp("ziffern", "Ziffern"), sp("tastatur", "Tastatur"),
        ]),
        ("activities", "Aktivitäten", [
            btn("Tablet Wohnzimmer", [fn("activity", 2)], 6),
            btn("Google Streamer", [fn("activity", 1)], 6),
            btn("Sony TV", [fn("activity", 0)], 6),
        ]),
        ("music", "Musik", [
            _e("sensor", "Läuft in", "input_text.multiroom_audio_aktive_raeume"),
            sp("music_group", "Lautsprecher"),
            btn("Play / Pause", [ha("media_player.media_play_pause", "media_player.multiroom_audio")], 3),
            btn("Aus", [ha("media_player.turn_off", "media_player.multiroom_audio")], 3),
            _pg("music_google", "Musik über Google"),
            _e("toggle", "Multiroom Video Bad", "input_boolean.multiroom_video_bad"),
            _e("toggle", "Video automatisch", "input_boolean.multiroom_video_bad_auto_erlauben"),
        ]),
        ("music_google", "Musik über Google", [
            _t("Startet die Google-Skripte wie bisher."),
            btn("Ganze Wohnung", s("script.spiel_musik_in_der_wohnung"), 6),
            btn("Wohnzimmer", s("script.spiel_musik_im_wohnzimmer"), 6),
            btn("Küche", s("script.spiel_musik_in_der_kuche"), 6),
            btn("Bad", s("script.spiel_musik_im_bad"), 6),
            btn("Flur", s("script.spiel_musik_im_flur"), 6),
        ]),
        # Einstellungen als Rasterseiten (einstellungen.py, 2026-10-04)
        ("settings", "Einstellungen", [_pg(s["id"], s["title"]) for s in EINST_SEITEN]),
    ]
    return [{"id": i, "title": t, "remember": True, "flow": True, "items": items} for i, t, items in seiten]


def default_menu() -> dict:
    """Wie das eingebaute Menue: Hauptmenue + Raeume als konfigurierbare Seiten,
    dahinter die eingebauten Seiten. Zum Umbauen im Panel."""
    return {
        "on": True,
        "start": "main",
        "pages": [
            *menue_seiten(),
            raeume_liste(),
            *raum_seiten(),
            kamera_popup(),
            *EINST_SEITEN,
        ],
    }


def default_config() -> dict:
    streamer = _common()
    tv = _common()
    tv["41"] = {
        "short": [ble(2, "button")],  # Sony Schnelleinstellungen (KEYCODE_BUTTON_2)
        "long": [ha("script.turn_on", "script.esphomeremote_streamer_close_app")],
    }
    tablet = _common()
    tablet["13"] = {
        "short": [ble(0x0224)],
        "long": [ha("script.turn_on", "script.1706872315283")],
    }
    return copy.deepcopy(
        {
            "v": 1,
            "slots": ["Google Streamer", "Sony TV", "Tablet WZ", "Platz 4"],
            "activities": [
                # Leiste: TV links, Streamer in der Mitte, Tablet rechts (Nutzerwunsch 2026-10-04)
                {"name": "TV", "slot": 1, "keys": tv},
                {"name": "Streamer", "slot": 0, "keys": streamer},
                {"name": "Tablet", "slot": 2, "keys": tablet},
            ],
            "menu": default_menu(),
        }
    )
