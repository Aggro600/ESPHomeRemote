"""Streamer-Funktion (2026-10-19, vorher Template-Paket packages/esphomeremote_smarttube.yaml mit festen Entitaeten).

Auf einem Android-TV-Streamer laeuft die App "ESPHomeRemote Streamer" (esphomeremote_streamer_app). Sie schickt bei
jeder Aenderung den Text ihrer Abfrage (Medien-Sitzungen, Vordergrund-App, Tastatur, Laufzeit) an einen Webhook.
Die Integration wertet ihn aus und liefert:
  - die Daten fuer den "Jetzt laeuft"-Player (now_playing.py: App, Titel, Interpret, Cover, Position, Dauer, Tempo)
  - Sensoren je Streamer (Tastatur offen? fuer Popups, Wiedergabestatus, App, Titel, am Fernseher sichtbar)
  - Knoepfe "App beenden" und "Neu starten" (ueber die ADB-Entitaet)
Eingestellt im Konfigurator (Seite "Wiedergabe", kd.data["streamer"]): Name, ADB-Entitaet (androidtv), Webhook.
Die Ziel-Adresse traegt der Knopf "Am Streamer eintragen" per ADB ein (settings put global esphomeremote_webhook).
"""

from __future__ import annotations
from .i18n import t as _t

import json
import logging
from pathlib import Path
import re
import secrets
import shutil
from typing import Any, Callable

import voluptuous as vol

from homeassistant.components import webhook, websocket_api
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.event import async_track_time_interval
from homeassistant.util import dt as dt_util

_LOGGER = logging.getLogger(__name__)
APP_PKG = "de.esphomeremote.streamer"
APP_DIR = Path(__file__).parent / "streamer_app"   # mitgelieferte App (Release-Werkzeug legt sie hier ab)
DOMAIN = "esphomeremote"

NAMEN = {
    "de.telekom.magentatv.androidtv": "MagentaTV", "com.netflix.ninja": "Netflix", "org.smarttube.stable": "SmartTube", "com.google.android.youtube.tv": "YouTube",
    "com.google.android.apps.youtube.music": "YouTube Music", "com.disney.disneyplus": "Disney+", "de.rtli.tvnow": "RTL+",
    "org.xbmc.kodi": "Kodi", "com.amazon.amazonvideo.livingroom": "Prime Video", "com.spotify.tv.android": "Spotify",
    "com.plexapp.android": "Plex", "de.sky.bw": "Sky", "com.apple.atve.androidtv.appletv": "Apple TV",
    "com.google.android.apps.tv.launcherx": "Google TV", "com.wbd.stream": "HBO Max", "de.zdf.mediathek": "ZDF",
    "com.zdf.android.mediathek": "ZDF", "com.google.android.youtube.tvmusic": "YouTube Music",
    "de.prosiebensat1digital.seventv": "Joyn", "de.swr.avp.ard.tv": "ARD Mediathek", "org.videolan.vlc": "VLC",
    "com.google.android.videos": "Google Play Filme", "io.homeassistant.companion.androidtv": "HA Dashboard",
    "com.lonelycatgames.Xplore": "X-plore", "com.android.gallery3d": "Galerie", "com.google.android.play.games": "Play Games",
    "com.android.vending": "Play Store", "com.rma.speedtesttv": "Speedtest", "flar2.homebutton": "Button Mapper",
}
LAUNCHER = ("com.google.android.apps.tv.launcherx", "com.google.android.tvlauncher", "com.spocky.projengmenu",
            "com.android.systemui", "com.android.tv.settings", "com.google.android.tvsettings")
MAGENTATV = "de.telekom.magentatv.androidtv"
SMARTTUBE = "org.smarttube.stable"
# Zustand als "state=PLAYING(3)" (Android 13+, Google TV Streamer) oder nur "state=3" (Android 12, MagentaTV Stick Gen 2 -
# 2026-11-05: dort wurde sonst keine Sitzung erkannt, Titel war der App-Name, Status "paused")
RE_SITZUNG = re.compile(r"package=([^ \n]+)[ \n]+state=PlaybackState [{]state=(?:[A-Z_]+[(])?([0-9]+)[)]?, position=(-?[0-9]+), "
                        r"buffered position=[0-9]+, speed=([-0-9.]+), updated=([0-9]+)[^\n]*[\n][ ]+metadata: ([^\n]*)")
RE_TOP = re.compile(r"topResumedActivity=ActivityRecord[{][0-9a-f]+ u[0-9]+ ([A-Za-z0-9_.]+)/")
LEER, NETFLIX = "/local/esphomeremote_cover_leer.png", "/local/esphomeremote_netflix_n.png"


GERAETE = ("google", "magenta", "android")   # Google TV Streamer, MagentaTV Stick (Gen 2, Typ A), anderes Android-TV


def validate_streamer(liste) -> list[dict]:
    if not isinstance(liste, list):
        raise vol.Invalid("Streamer: Liste erwartet")
    out, ids = [], set()
    for st in liste[:4]:
        sid = str(st.get("id") or "").strip()
        if not re.fullmatch(r"[a-z0-9_]{1,24}", sid) or sid in ids:
            raise vol.Invalid(f"Streamer: Kennung '{sid}' ungültig oder doppelt")
        ids.add(sid)
        adb = str(st.get("adb") or "").strip()
        if adb and not re.fullmatch(r"media_player\.[a-z0-9_]+", adb):
            raise vol.Invalid(f"Streamer: '{adb}' ist kein Media Player (Android-TV-Integration)")
        wh = str(st.get("webhook") or "").strip() or f"esphomeremote_streamer_{secrets.token_hex(12)}"
        if not re.fullmatch(r"[A-Za-z0-9_-]{8,80}", wh):
            raise vol.Invalid("Streamer: Webhook-Kennung ungültig")
        # Geraetetyp (2026-11-05): nur fuer die Anleitung im Konfigurator - die App ist fuer alle dieselbe
        typ = str(st.get("typ") or "google")
        # nur_kamera (2026-11-05, Nutzer: "dass die App nur das mit dem Kamerastream macht, die ganzen anderen Sachen
        # nicht ... dass der Fernseher nicht so viele Anfragen verarbeiten muss"): Software-Schalter, keine zweite APK
        # Soundbar/AV-Receiver am Fernseher per HDMI-CEC ueber diesen Streamer (2026-11-05): Schalter + Dienst
        # streamer_cec; Zustand optional aus einer Entitaet (z. B. Tonausgabe des Fernsehers = ARC)
        az = str(st.get("audio_zustand") or "").strip()
        out.append({"id": sid, "name": str(st.get("name") or "Streamer")[:32], "adb": adb, "webhook": wh,
                    "typ": typ if typ in GERAETE else "android", "nur_kamera": bool(st.get("nur_kamera")),
                    "audio": bool(st.get("audio")), "audio_zustand": az if re.fullmatch(r"[a-z_]+\.[a-z0-9_]+", az) else "",
                    "audio_attr": str(st.get("audio_attr") or "")[:40], "audio_wert": str(st.get("audio_wert") or "")[:40]})
    return out


def auswerten(r: str, alt: dict, basis: str) -> dict:
    """Text der Streamer-App -> Daten (wie frueher die Template-Sensoren)."""
    ss = [m for m in RE_SITZUNG.findall(r) if m[0] not in ("com.google.android.bluetooth", "com.android.bluetooth")]
    tm = RE_TOP.findall(r)
    top = tm[0] if tm else ""
    playing = [x for x in ss if x[1] == "3"]
    paused = [x for x in ss if x[1] == "2" and x[0] == top]
    sel = playing[0] if playing else (paused[0] if paused else None)
    pkg = sel[0] if sel else ""
    name = lambda p: NAMEN.get(p) or p.split(".")[-1].capitalize()   # noqa: E731
    fg = name(top) if (top and top not in LAUNCHER and not sel) else ""
    app = name(pkg) if sel else fg
    ist_st = pkg == SMARTTUBE
    feld = lambda k: ((re.findall(k + r"=([^\n]*)", r) or [""])[0]).strip()   # noqa: E731
    # Titel: SmartTube genau aus dem Helfer, andere Apps aus der Beschreibung der Sitzung, sonst der App-Name
    if not sel:
        titel = fg
    else:
        d = (re.findall(r"description=(.*)", sel[5]) or [""])[0]
        d = re.sub(r"(, null)+$", "", d)
        st_titel = feld("TITEL")
        if ist_st and st_titel:
            titel = st_titel
        elif d and d != "null":
            titel = app if d.lower().replace(" ", "") == app.lower().replace(" ", "") else d
        else:
            titel = app
    interpret = feld("ARTIST") if (sel and ist_st) else ""
    if pkg == MAGENTATV and ", " in titel:
        # MagentaTV (2026-11-05): Beschreibung "Sendung, Sender" -> Titel = Sendung, Interpret = Sender
        # (am letzten Komma trennen: Sendungen haben selbst Kommas, Sender nie)
        titel, interpret = titel.rsplit(", ", 1)
    status = ("paused" if fg else "") if not sel else ("playing" if sel[1] == "3" else "paused")
    leer, netflix = f"http://{basis}{LEER}", f"http://{basis}{NETFLIX}"
    art = ""
    for a_pkg, a_url in re.findall(r"ART:([^=\n]+)=([^\n]*)", r):
        if a_pkg == pkg:
            art = a_url.strip()
    if not sel:
        cover = leer if fg else ""
    elif pkg == "com.netflix.ninja":
        cover = netflix
    else:
        cover = feld("COVER") if (ist_st and feld("COVER")) else (art or leer)
    # Position (s) hochgerechnet mit der Laufzeit des Streamers; der alte Wert bleibt, solange er hoechstens 2 s
    # daneben liegt (sonst ginge jede neue Zahl an alle Fernbedienungen, obwohl sie selbst weiterzaehlen)
    pos, tempo = -1, 1.0
    if sel:
        up = float((re.findall(r"---UP[ \n]+([0-9.]+)", r) or [0])[0]) * 1000
        laeuft = sel[1] == "3"
        jetzt = float(sel[2]) + ((up - float(sel[4])) * float(sel[3]) if (laeuft and up > 0) else 0)
        neu = max(int(jetzt / 1000), 0)
        a_pos, a_zeit, a_lief = alt.get("position", -1), alt.get("pos_zeit"), alt.get("laeuft")
        erwartet = a_pos + ((dt_util.utcnow() - a_zeit).total_seconds() * float(sel[3]) if (a_zeit and laeuft) else 0)
        pos = a_pos if (a_pos >= 0 and a_lief == laeuft and abs(neu - erwartet) <= 2) else neu
        if pos > 10 * 365 * 86400:   # Live-TV (MagentaTV): "Position" ist die Uhrzeit - keine echte Position
            pos = -1
        tempo = float(playing[0][3]) if playing else 1.0
    dm = re.findall("DUR:" + re.escape(pkg) + "=([0-9]+)", r) if sel else []
    dauer = int(int(dm[0]) / 1000) if dm else 0
    if "mInputShown=true" not in r:
        tastatur = "aus"
    else:
        t = int((re.findall(r"editorInfo: +inputType=([0-9]+)", r) or ["1"])[-1])
        tastatur = "zahlen" if (t % 16) in (2, 3, 4) else "text"
    out = {"titel": titel, "interpret": interpret, "status": status, "cover": cover, "app": app, "paket": pkg,
           "position": pos, "tempo": tempo, "dauer": dauer, "tastatur": tastatur, "laeuft": bool(playing),
           "zuletzt": dt_util.utcnow()}
    out["pos_zeit"] = dt_util.utcnow() if pos != alt.get("position") or not alt.get("pos_zeit") else alt["pos_zeit"]
    return out


class Streamer:
    """Ein Streamer: empfaengt den Webhook, haelt die ausgewerteten Daten, meldet Aenderungen an Abonnenten."""

    def __init__(self, hass: HomeAssistant, kd, cfg: dict) -> None:
        self.hass, self.kd, self.cfg = hass, kd, cfg
        self.daten: dict[str, Any] = {}
        self.cec: str | None = None   # zeigt der Fernseher gerade den Streamer? "true"/"false" (HDMI-CEC per ADB)
        self._abos: list[Callable[[], None]] = []
        self._weg: list[Callable[[], None]] = []

    @callback
    def abonnieren(self, fn: Callable[[], None]) -> Callable[[], None]:
        self._abos.append(fn)
        return lambda: self._abos.remove(fn) if fn in self._abos else None

    @callback
    def _melden(self) -> None:
        for fn in list(self._abos):
            fn()

    @callback
    def starten(self) -> None:
        try:
            webhook.async_register(self.hass, DOMAIN, f"Streamer {self.cfg['name']}", self.cfg["webhook"], self._webhook,
                                   local_only=True, allowed_methods=["POST"])
            wh = self.cfg["webhook"]
            self._weg.append(lambda: webhook.async_unregister(self.hass, wh))
        except ValueError as err:   # Kennung schon vergeben (z. B. noch ein altes Template-Paket mit demselben Webhook)
            _LOGGER.warning("Streamer %s: Webhook %s nicht moeglich: %s", self.cfg["name"], self.cfg["webhook"], err)
        if self.cfg["adb"] and not self.cfg.get("nur_kamera"):
            from datetime import timedelta
            self._weg.append(async_track_time_interval(self.hass, self._cec_fragen, timedelta(seconds=30)))
            self.hass.async_create_task(self._cec_fragen(None))

    @callback
    def stoppen(self) -> None:
        for w in self._weg:
            w()
        self._weg.clear()

    async def _webhook(self, hass, webhook_id, request):
        try:
            daten = await request.json()
        except Exception:  # noqa: BLE001
            return None
        # Tasten umbelegen (2026-11-05, App 1.16): {"taste": {code, name, art (kurz/lang/lernen), quelle, cec}}
        # -> Ereignis esphomeremote_streamer_key (englisch: press short/long/learn, source, cec)
        taste = daten.get("taste") if isinstance(daten, dict) else None
        if isinstance(taste, dict):
            hass.bus.async_fire(f"{DOMAIN}_streamer_key", {
                "streamer": self.cfg.get("id"), "streamer_name": self.cfg.get("name"),
                "code": taste.get("code"), "name": taste.get("name"),
                "press": {"kurz": "short", "lang": "long", "lernen": "learn"}.get(taste.get("art"), taste.get("art")),
                "source": taste.get("quelle"), "cec": bool(taste.get("cec"))})
            return None
        # Einfaches Popup-Menue (App 1.16, Menue.java): {"menue": {id, wahl, text}} -> esphomeremote_streamer_menu_choice
        menue = daten.get("menue") if isinstance(daten, dict) else None
        if isinstance(menue, dict):
            hass.bus.async_fire(f"{DOMAIN}_streamer_menu_choice", {
                "streamer": self.cfg.get("id"), "streamer_name": self.cfg.get("name"),
                "id": menue.get("id"), "choice": menue.get("wahl"), "text": menue.get("text")})
            return None
        text = str(daten.get("text") or "") if isinstance(daten, dict) else ""
        try:
            self.daten = auswerten(text, self.daten, self.kd.basis() or "homeassistant.local:8123")
        except Exception:  # noqa: BLE001 - eine kaputte Meldung darf den Webhook nicht abschiessen
            _LOGGER.exception("Streamer %s: Meldung nicht auswertbar", self.cfg["name"])
            return None
        self._melden()
        return None

    # ---- ADB (androidtv-Integration): HDMI-CEC, App beenden, Neustart
    async def adb(self, befehl: str) -> str:
        if not self.cfg["adb"]:
            raise vol.Invalid("Kein ADB-Gerät gewählt")
        st = self.hass.states.get(self.cfg["adb"])
        if st is None or st.state == "unavailable":
            # Nur-Kamera: die Android-TV-Entitaet wird nicht abgefragt und verbindet sich nicht selbst neu
            await self.hass.services.async_call("homeassistant", "update_entity", {"entity_id": self.cfg["adb"]}, blocking=True)
        await self.hass.services.async_call("androidtv", "adb_command", {"entity_id": self.cfg["adb"], "command": befehl}, blocking=True)
        st = self.hass.states.get(self.cfg["adb"])
        return str(st.attributes.get("adb_response") or "") if st else ""

    async def _cec_fragen(self, _now) -> None:
        st = self.hass.states.get(self.cfg["adb"])
        if st is None or st.state in ("off", "standby", "unavailable", "unknown"):
            return
        try:
            r = await self.adb("dumpsys hdmi_control 2>/dev/null | grep -m1 'isActiveSource()'")
        except Exception as err:  # noqa: BLE001
            _LOGGER.debug("Streamer %s CEC: %s", self.cfg["name"], err)
            return
        m = re.findall(r"isActiveSource\(\): (true|false)", r)
        if m and m[0] != self.cec:
            self.cec = m[0]
            self._melden()

    async def app_beenden(self) -> None:
        st = self.hass.states.get(self.cfg["adb"]) if self.cfg["adb"] else None
        app = str(st.attributes.get("app_id") or "").strip() if st else ""
        if app and "launcher" not in app.lower() and app != "com.android.systemui":
            await self.adb(f"am force-stop {app}")

    async def eintragen(self) -> str:
        """Ziel-Adresse des Webhooks auf dem Streamer setzen (die App liest sie aus den globalen Einstellungen)."""
        url = f"http://{self.kd.basis()}/api/webhook/{self.cfg['webhook']}"
        await self.adb(f"settings put global esphomeremote_webhook {url}")
        return url


    # ---- App verwalten (2026-10-26): einrichten/aktualisieren, Status, entfernen - alles ueber die ADB-Entitaet
    async def app_status(self) -> dict:
        """Was auf dem Streamer ist: installierte Version, Dienste an, Webhook-Ziel."""
        r = await self.adb(f"dumpsys package {APP_PKG} | grep -E 'versionCode=|versionName='; echo ---; "
                           "settings get secure enabled_accessibility_services; echo ---; "
                           "settings get secure enabled_notification_listeners; echo ---; settings get global esphomeremote_webhook")
        teile = (r + "\n---\n---\n---").split("---")
        code = re.findall(r"versionCode=(\d+)", teile[0])
        name = re.findall(r"versionName=(\S+)", teile[0])
        url = teile[3].strip()
        soll = f"http://{self.kd.basis()}/api/webhook/{self.cfg['webhook']}"
        z = self.daten.get("zuletzt")
        return {"installiert": bool(code), "code": int(code[0]) if code else 0, "name": name[0] if name else "",
                "bedienung": f"{APP_PKG}/" in teile[1], "medien": f"{APP_PKG}/" in teile[2],
                "webhook": url if url not in ("", "null") else "", "webhook_ok": url == soll, "nur_kamera": bool(self.cfg.get("nur_kamera")),
                "zuletzt": z.isoformat() if z else None}

    async def app_einrichten(self) -> dict:
        """Mitgelieferte App aufspielen (auch als Update), Dienste freigeben, Webhook eintragen."""
        quelle = APP_DIR / "esphomeremote-streamer.apk"
        if not await self.hass.async_add_executor_job(quelle.exists):
            raise vol.Invalid("App fehlt in der Integration")
        ziel = Path(self.hass.config.path("www")) / ".esphomeremote-streamer.apk"
        def kopieren():
            ziel.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(quelle, ziel)
        await self.hass.async_add_executor_job(kopieren)
        try:
            await self.hass.services.async_call("androidtv", "upload", {"entity_id": self.cfg["adb"], "local_path": str(ziel),
                                                                     "device_path": "/data/local/tmp/esphomeremote.apk"}, blocking=True)
            r = await self.adb("pm install -r /data/local/tmp/esphomeremote.apk 2>&1; rm -f /data/local/tmp/esphomeremote.apk")
        finally:
            await self.hass.async_add_executor_job(lambda: ziel.unlink(missing_ok=True))
        if "Success" not in r:
            # Die Antwort teilen sich alle ADB-Befehle der Entitaet (z. B. die CEC-Abfrage alle 30 s) - entscheidend ist,
            # welche Version danach installiert ist
            ist = await self.app_status()
            if ist["code"] != (await self.hass.async_add_executor_job(app_mitgeliefert)).get("code"):
                raise vol.Invalid(f"Installieren ging nicht: {r.strip()[:200] or 'keine Antwort'}")
        # Dienste freigeben: Bedienungshilfe an die vorhandenen anhaengen (andere Apps behalten ihre), Benachrichtigungen
        acc = (await self.adb("settings get secure enabled_accessibility_services")).strip()
        liste = [x for x in acc.split(":") if x and x != "null" and not x.startswith(APP_PKG + "/")]
        liste.append(f"{APP_PKG}/{APP_PKG}.AccessibilityHelper")
        await self.adb(f"settings put secure enabled_accessibility_services {':'.join(liste)}; settings put secure accessibility_enabled 1")
        await self.modus_anwenden()
        return await self.app_status()

    async def modus_anwenden(self) -> None:
        """Voll: Titel/Cover/Tastatur melden (Benachrichtigungszugriff, Webhook), Android-TV-Entitaet wird abgefragt.
        Nur Kamera: App meldet nichts (kein Benachrichtigungszugriff, kein Ziel), HA fragt den Streamer nicht ab -
        nur das Kamera-Overlay (Bedienungshilfe) und ADB-Befehle auf Abruf."""
        nur = bool(self.cfg.get("nur_kamera"))
        lis = f"{APP_PKG}/{APP_PKG}.MediaListener"
        if nur:
            # esphomeremote_modus: die App (ab 1.15) abonniert dann keine Fenster-Ereignisse mehr, Statusseite gruen
            # Ziel-Adresse bleibt (2026-11-05): die App meldet darueber umbelegte Tasten - Zustaende schickt sie in
            # diesem Modus ohnehin nicht
            await self.adb(f"cmd notification disallow_listener {lis}; settings put global esphomeremote_modus kamera")
            await self.eintragen()
            self.daten = {}
            self.cec = None
            self._melden()
        else:
            await self.adb(f"cmd notification allow_listener {lis}; settings delete global esphomeremote_modus")
            await self.eintragen()
        await self._abfragen(not nur)

    async def _abfragen(self, an: bool) -> None:
        """Regelmaessiges Abfragen der Android-TV-Integration (Systemoption des Eintrags) an/aus."""
        from homeassistant.helpers import entity_registry as er

        ent = er.async_get(self.hass).async_get(self.cfg["adb"]) if self.cfg["adb"] else None
        ce = self.hass.config_entries.async_get_entry(ent.config_entry_id) if ent and ent.config_entry_id else None
        if ce is None or ce.domain != "androidtv" or ce.pref_disable_polling == (not an):
            return
        self.hass.config_entries.async_update_entry(ce, pref_disable_polling=not an)
        await self.hass.config_entries.async_reload(ce.entry_id)

    async def app_entfernen(self) -> dict:
        acc = (await self.adb("settings get secure enabled_accessibility_services")).strip()
        liste = [x for x in acc.split(":") if x and x != "null" and not x.startswith(APP_PKG + "/")]
        befehl = f"settings put secure enabled_accessibility_services {':'.join(liste)}" if liste else "settings delete secure enabled_accessibility_services"
        r = await self.adb(f"{befehl}; settings delete global esphomeremote_webhook; settings delete global esphomeremote_modus; "
                           f"pm uninstall {APP_PKG} 2>&1")
        if "Success" not in r and "not installed" not in r.lower() and "Unknown package" not in r:
            raise vol.Invalid(f"Deinstallieren ging nicht: {r.strip()[:200]}")
        return await self.app_status()


def app_mitgeliefert() -> dict:
    try:
        return json.loads((APP_DIR / "version.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {"code": 0, "name": ""}


class Streamers:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self.alle: dict[str, Streamer] = {}

    @property
    def liste(self) -> list[dict]:
        return self.kd.data.get("streamer") or []

    @callback
    def sync(self) -> None:
        soll = {s["id"]: s for s in self.liste}
        for sid in list(self.alle):
            if sid not in soll:
                self.alle.pop(sid).stoppen()
            elif self.alle[sid].cfg != soll[sid]:   # Einstellungen geaendert: dasselbe Objekt (Abonnenten bleiben)
                st = self.alle[sid]
                st.stoppen()
                st.cfg = soll[sid]
                st.starten()
        for sid, cfg in soll.items():
            if sid not in self.alle:
                self.alle[sid] = Streamer(self.hass, self.kd, cfg)
                self.alle[sid].starten()


# ----------------------------------------------------------------- HDMI-CEC ueber den Streamer (2026-11-05)
# Android (12-14) laesst ohne Root keine freien CEC-Befehle zu, der HDMI-Dienst aber gezielt an ein Geraet:
#   IHdmiControlService.powerOnRemoteDevice (Transaktion 33): <User Control Pressed: Power On Function>
#   IHdmiControlService.powerOffRemoteDevice (Transaktion 32): <Standby> nur an dieses Geraet
# Logische Adressen: 0 = Fernseher, 5 = Audiosystem (Soundbar/AV-Receiver). Getestet: MagentaTV Stick + Hisense HS214.
CEC_ADRESSE = {"soundbar": 5, "tv": 0}


async def cec_befehl(st, geraet: str, an: bool) -> None:
    adr = CEC_ADRESSE[geraet]
    await st.adb(f"service call hdmi_control {33 if an else 32} i32 {adr} i32 {1 if an else 0}")


def _streamer_finden(s, wahl: str):
    w = str(wahl or "").strip().lower()
    for sid, st in s.alle.items():
        if w in (sid, str(st.cfg.get("name") or "").strip().lower()):
            return st
    return None


def einrichten(hass: HomeAssistant, kd) -> Streamers:
    s = Streamers(hass, kd)
    hass.data[DOMAIN + "_streamer"] = s
    s.sync()

    async def dienst_cec(call) -> None:
        # Felder englisch (device, command), alte deutsche Namen ueber aliases.py
        st = _streamer_finden(s, call.data.get("streamer"))
        if st is None:
            raise vol.Invalid(f"Unknown streamer '{call.data.get('streamer')}'")
        geraet = str(call.data.get("device") or "soundbar").lower()
        if geraet not in CEC_ADRESSE:
            raise vol.Invalid("device: soundbar or tv")
        cmd = call.data.get("command", "on")
        an = cmd is True or str(cmd).lower() in ("on", "true", "1")
        await cec_befehl(st, geraet, an)
        v = hass.data.get(DOMAIN + "_dock")
        if geraet == "soundbar" and v and (e := v.ents.get(f"streamer_{st.cfg['id']}_audio")):
            e.merken(an)

    from .aliases import schema_with_aliases
    hass.services.async_register(DOMAIN, "streamer_cec", dienst_cec, schema=schema_with_aliases("streamer_cec"))
    for fn in (ws_streamer_get, ws_streamer_save, ws_streamer_eintragen, ws_streamer_app):
        websocket_api.async_register_command(hass, fn)
    return s


def _antwort(hass) -> dict:
    s: Streamers = hass.data[DOMAIN + "_streamer"]
    return {"streamer": s.liste, "basis": s.kd.basis(),
            "zuletzt": {sid: (st.daten.get("zuletzt").isoformat() if st.daten.get("zuletzt") else None) for sid, st in s.alle.items()},
            "app": {sid: st.daten.get("app") or "" for sid, st in s.alle.items()},
            # was gerade laeuft (2026-11-05, zur Kontrolle im Konfigurator / bei der Fehlersuche)
            "jetzt": {sid: {k: st.daten.get(k) for k in ("titel", "interpret", "status", "cover", "paket", "position", "dauer")} for sid, st in s.alle.items()}}


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/streamer_app", vol.Required("streamer"): str,
                                  vol.Required("aktion"): vol.In(["status", "einrichten", "entfernen"])})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_streamer_app(hass, connection, msg) -> None:
    """Streamer-App: status | einrichten (installieren/aktualisieren + freigeben + eintragen) | entfernen."""
    st = hass.data[DOMAIN + "_streamer"].alle.get(msg["streamer"])
    if st is None:
        connection.send_error(msg["id"], "ungueltig", "Unbekannter Streamer")
        return
    adb = hass.states.get(st.cfg["adb"]) if st.cfg["adb"] else None
    if adb is None or adb.state in ("unavailable", "unknown"):
        connection.send_error(msg["id"], "kein_adb", "Keine ADB-Verbindung zum Streamer")
        return
    try:
        if msg["aktion"] == "einrichten":
            r = await st.app_einrichten()
        elif msg["aktion"] == "entfernen":
            r = await st.app_entfernen()
        else:
            r = await st.app_status()
    except Exception as err:  # noqa: BLE001 - ADB/Upload: Meldung an den Konfigurator
        connection.send_error(msg["id"], "fehler", str(err))
        return
    connection.send_result(msg["id"], {**r, "verfuegbar": await hass.async_add_executor_job(app_mitgeliefert)})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/streamer_get"})
@websocket_api.require_admin
@callback
def ws_streamer_get(hass, connection, msg) -> None:
    connection.send_result(msg["id"], _antwort(hass))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/streamer_save", vol.Required("streamer"): list})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_streamer_save(hass, connection, msg) -> None:
    s: Streamers = hass.data[DOMAIN + "_streamer"]
    try:
        s.kd.data["streamer"] = validate_streamer(msg["streamer"])
    except (vol.Invalid, ValueError, TypeError, AttributeError) as e:
        connection.send_error(msg["id"], "ungueltig", str(e))
        return
    # "Jetzt laeuft"-Player ohne den geloeschten Streamer
    ids = {x["id"] for x in s.liste}
    for j in s.kd.data.get("jetzt") or []:
        if j.get("streamer") and j["streamer"] not in ids:
            j["streamer"] = ""
    vorher = {sid: bool(st.cfg.get("nur_kamera")) for sid, st in s.alle.items()}   # Stand vor sync()
    await s.kd.store.async_save(s.kd.data)
    s.sync()
    if (v := hass.data.get(DOMAIN + "_dock")) is not None:
        await v.sync()
    for sid, st in s.alle.items():   # Nur-Kamera umgeschaltet: gleich am Streamer anwenden
        if sid in vorher and vorher[sid] != bool(st.cfg.get("nur_kamera")) and st.cfg.get("adb"):
            try:
                await st.modus_anwenden()
            except Exception as err:  # noqa: BLE001 - Streamer aus: gilt beim naechsten "Einrichten"
                _LOGGER.warning("Streamer %s: Modus nicht angewendet: %s", st.cfg["name"], err)
    connection.send_result(msg["id"], _antwort(hass))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/streamer_eintragen", vol.Required("streamer"): str})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_streamer_eintragen(hass, connection, msg) -> None:
    st = hass.data[DOMAIN + "_streamer"].alle.get(msg["streamer"])
    if st is None:
        connection.send_error(msg["id"], "ungueltig", "Unbekannter Streamer")
        return
    try:
        url = await st.eintragen()
    except Exception as e:  # noqa: BLE001
        connection.send_error(msg["id"], "adb", str(e))
        return
    connection.send_result(msg["id"], {"url": url})


# ---------------------------------------------------------------------------------------------- Entitaeten
from homeassistant.components.button import ButtonEntity  # noqa: E402
from homeassistant.components.sensor import SensorEntity  # noqa: E402

# key: (Name, Symbol, Wert aus dem Streamer)
SENSOREN = {
    "tastatur": ("Tastatur", "mdi:keyboard-outline", lambda s: s.daten.get("tastatur") or "aus"),
    "status": ("Wiedergabe", "mdi:play-pause", lambda s: s.daten.get("status") or "aus"),
    "app": ("App", "mdi:application-outline", lambda s: s.daten.get("app") or ""),
    "titel": ("Titel", "mdi:format-title", lambda s: s.daten.get("titel") or ""),
    "am_tv": ("Am Fernseher sichtbar", "mdi:television-shimmer", lambda s: s.cec or "unbekannt"),
}


class StreamerSensor(SensorEntity):
    """Wert eines Streamers, z. B. ob seine Bildschirmtastatur offen ist (Ausloeser fuer das Tastatur-Popup)."""

    _attr_should_poll = False

    def __init__(self, cfg: dict, key: str) -> None:
        self._sid, self._key = cfg["id"], key
        name, self._attr_icon, self._wert = SENSOREN[key]
        self._attr_name = f"{cfg['name']} {_t(None, name)}"
        self._attr_unique_id = f"streamer_{cfg['id']}_{key}"
        self.entity_id = f"sensor.esphomeremote_streamer_{cfg['id']}_{key}"

    def _st(self) -> Streamer | None:
        return self.hass.data[DOMAIN + "_streamer"].alle.get(self._sid)

    @property
    def native_value(self) -> str:
        st = self._st()
        return self._wert(st) if st else "unbekannt"

    async def async_added_to_hass(self) -> None:
        if (st := self._st()) is not None:
            self.async_on_remove(st.abonnieren(self.async_write_ha_state))


class StreamerKnopf(ButtonEntity):
    """App beenden (force-stop der laufenden App) bzw. Streamer neu starten - ueber die ADB-Entitaet."""

    def __init__(self, cfg: dict, key: str) -> None:
        self._sid, self._key = cfg["id"], key
        self._attr_name = f"{cfg['name']} " + _t(None, "App beenden" if key == "app_beenden" else "neu starten")
        self._attr_icon = "mdi:close-box-outline" if key == "app_beenden" else "mdi:restart"
        self._attr_unique_id = f"streamer_{cfg['id']}_{key}"
        self.entity_id = f"button.esphomeremote_streamer_{cfg['id']}_{key}"

    async def async_press(self) -> None:
        st = self.hass.data[DOMAIN + "_streamer"].alle.get(self._sid)
        if st is None:
            return
        if self._key == "app_beenden":
            await st.app_beenden()
        else:
            await st.adb("reboot")


# ----------------------------------------------------------------- Soundbar-Schalter (2026-11-05)
from homeassistant.components.switch import SwitchEntity  # noqa: E402
from homeassistant.helpers.event import async_track_state_change_event  # noqa: E402
from homeassistant.helpers.restore_state import RestoreEntity  # noqa: E402


class CecSchalter(SwitchEntity, RestoreEntity):
    """Soundbar/AV-Receiver am Fernseher, geschaltet per HDMI-CEC ueber den Streamer."""
    _attr_should_poll = False
    _attr_icon = "mdi:soundbar"

    def __init__(self, hass: HomeAssistant, sid: str) -> None:
        self._h, self.sid = hass, sid   # nicht self.hass: das setzt HA erst beim Hinzufuegen
        self._attr_unique_id = f"streamer_{sid}_audio"
        self.entity_id = f"switch.esphomeremote_streamer_{sid}_soundbar"
        self._an: bool | None = None
        self._ab = None

    @property
    def _st(self):
        return self._h.data[DOMAIN + "_streamer"].alle.get(self.sid)

    @property
    def name(self) -> str:
        st = self._st
        return f"{st.cfg.get('name') if st else self.sid} Soundbar"

    @property
    def available(self) -> bool:
        st = self._st
        return st is not None and bool(st.cfg.get("audio")) and bool(st.cfg.get("adb"))

    @property
    def assumed_state(self) -> bool:
        st = self._st
        return not (st and st.cfg.get("audio_zustand"))

    @property
    def is_on(self) -> bool | None:
        st = self._st
        q = st.cfg.get("audio_zustand") if st else ""
        if q:
            z = self._h.states.get(q)
            if z is not None:
                attr = st.cfg.get("audio_attr")
                ist = z.attributes.get(attr) if attr else z.state
                soll = st.cfg.get("audio_wert") or "on"
                return str(ist).lower() == soll.lower()
        return self._an

    def umbenennen(self, _sig) -> None:   # dock.py ruft das bei geaendertem Streamer-Namen
        if self.entity_id and self.hass is not None:
            self.async_write_ha_state()

    def merken(self, an: bool) -> None:
        self._an = an
        if self.hass is not None and self.entity_id:
            self.async_write_ha_state()

    async def async_added_to_hass(self) -> None:
        if (alt := await self.async_get_last_state()) is not None and alt.state in ("on", "off"):
            self._an = alt.state == "on"
        st = self._st
        if st and st.cfg.get("audio_zustand"):
            self.async_on_remove(async_track_state_change_event(self.hass, [st.cfg["audio_zustand"]], self._quelle_neu))

    @callback
    def _quelle_neu(self, _ereignis) -> None:   # im Event-Loop (ohne @callback liefe es im Executor-Thread)
        self.async_write_ha_state()

    async def async_turn_on(self, **kwargs: Any) -> None:
        await cec_befehl(self._st, "soundbar", True)
        self.merken(True)

    async def async_turn_off(self, **kwargs: Any) -> None:
        await cec_befehl(self._st, "soundbar", False)
        self.merken(False)
