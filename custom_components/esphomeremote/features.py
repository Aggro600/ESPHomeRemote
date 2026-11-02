"""Funktionen fuer das ganze Zuhause (2026-11-01, Nutzer: "nichts massgeschneidert, alles im Konfigurator").

Vorher persoenliche Pakete (packages/esphomeremote_tv_eingang.yaml, _kamera_tv, _streamer, _smarttube, _dock_wache)
mit festen Entitaeten, IPs und Skripten. Jetzt hier, eingestellt im Konfigurator (Seite "Funktionen"), gespeichert in
kd.data["funktionen"]:

  tv_eingang   Sensor sensor.esphomeremote_tv_eingang: der Eingang, den der Fernseher gerade zeigt (Name wie in seiner
               Quellenliste, z. B. "HDMI 3 (eARC/ARC)"), oder "aus". Popups vergleichen damit direkt ("ist nicht
               HDMI 3 ..."). Optional ein CEC-Sensor des Streamers ("true" = aktive Quelle): dann gilt der Eingang des
               Streamers ("streamer"), auch wenn der Fernseher die Quelle nicht verlaesslich meldet. Attribut
               "streamer": zeigt gerade den Streamer. (Bis 2026-11-01: Werte streamer/anders/aus - Nutzer: "anders"
               versteht keiner, es soll der echte Eingang dastehen.)
  kamera_tv    Kamera als Bild-im-Bild auf dem Fernseher. Weg "app" (Standard seit 2026-11-01): Overlay unserer
               Streamer-App (tv_camera.py, per ADB gesteuert, WebRTC/HLS/Standbild, YouTube & Co. laufen weiter).
               Weg "hatv": App "HA TV Dashboard" (Benachrichtigung), Video ueber go2rtc (WebRTC) oder HLS. Dienst esphomeremote.camera_tv {kamera: camera.x | "letzte" |
               "aus"}, Groesse esphomeremote.camera_size {groesse: Klein|Mittel|Groß|naechste}.
               Zustand: binary_sensor.esphomeremote_kamera_tv (an/aus, Attribut kamera), sensor.esphomeremote_kamera_groesse.
  neustart     Geraete per ADB neu starten: Dienst esphomeremote.restart {geraet: Name}. Mit Android-TV-
               Entitaet ueber androidtv.adb_command, sonst direkt per IP mit dem ADB-Schluessel von HA.
  version_licht  Lampe, die dreimal blinkt, wenn eine Remote neue Firmware meldet (Ereignis esphome.remote_neue_version).
  dock_wache   Meldung in HA, wenn ein Dock nicht erreichbar ist oder sein Speicher knapp wird.
"""
from __future__ import annotations
from .i18n import t as _t

import asyncio
import logging
import re
import time
from datetime import timedelta

from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event, async_track_time_interval

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
GROESSEN = {"Klein": 320, "Mittel": 480, "Groß": 720, "Fast Vollbild": 1800}
POSITIONEN = ("top-right", "top-left", "bottom-right", "bottom-left", "center")
STANDARD = {
    "tv_eingang": {"tv": "", "streamer": "", "cec": "", "erkennung": ""},   # erkennung: nur noch fuer die Umstellung
    "kamera_tv": {"weg": "app", "streamer": "", "sekunden": 0, "titel": True,
                  "notify": "", "go2rtc": "", "position": "top-right", "groesse": "Mittel", "kameras": {}},
    "neustart": [],
    "version_licht": "",
    "dock_wache": True,
    "dock_block_kb": 6,
}
E_EINGANG = "sensor.esphomeremote_tv_eingang"
E_KAMERA = "binary_sensor.esphomeremote_kamera_tv"
E_GROESSE = "sensor.esphomeremote_kamera_groesse"


class Funktionen:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self._abos: list = []
        self._letzte = ""          # zuletzt gezeigte Kamera
        self._sichtbar = False
        self._dock_off: dict[str, float] = {}
        self._knapp: set[str] = set()

    @property
    def cfg(self) -> dict:
        c = self.kd.data.setdefault("funktionen", {})
        for k, v in STANDARD.items():
            if isinstance(v, dict):
                c.setdefault(k, {})
                for k2, v2 in v.items():
                    c[k].setdefault(k2, v2 if not isinstance(v2, (dict, list)) else type(v2)())
            else:
                c.setdefault(k, v if not isinstance(v, list) else [])
        return c

    # ------------------------------------------------------------------ Start / Einstellungen
    async def starten(self) -> None:
        h = self.hass
        h.services.async_register(DOMAIN, "camera_tv", self._dienst_kamera)
        h.services.async_register(DOMAIN, "camera_size", self._dienst_groesse)
        h.services.async_register(DOMAIN, "camera_position", self._dienst_position)
        h.services.async_register(DOMAIN, "restart", self._dienst_neustart)
        h.bus.async_listen("esphome.remote_neue_version", self._neue_version)
        self._abos.append(async_track_time_interval(h, self._dock_pruefen, timedelta(seconds=15)))
        self.abonnieren()

    @callback
    def abonnieren(self) -> None:
        """Nach jeder Aenderung der Einstellungen: Quellen des TV-Eingangs neu beobachten, Zustaende neu setzen."""
        for a in self._abos[1:]:
            a()
        self._abos = self._abos[:1]
        t = self.cfg["tv_eingang"]
        ids = [x for x in (t["tv"], t["cec"]) if x]
        if ids:
            self._abos.append(async_track_state_change_event(self.hass, ids, self._quelle_geaendert))
        self._eingang_setzen()
        self._kamera_zustand()

    async def setzen(self, neu: dict) -> dict:
        c = self.cfg
        if isinstance(neu.get("tv_eingang"), dict):
            for k in ("tv", "streamer", "cec"):
                if k in neu["tv_eingang"]:
                    c["tv_eingang"][k] = str(neu["tv_eingang"][k] or "")[:120]
        if isinstance(neu.get("kamera_tv"), dict):
            k2 = neu["kamera_tv"]
            for k in ("notify", "go2rtc", "streamer"):
                if k in k2:
                    c["kamera_tv"][k] = str(k2[k] or "").strip()[:200]
            if k2.get("weg") in ("app", "hatv"):
                c["kamera_tv"]["weg"] = k2["weg"]
            if "sekunden" in k2:
                c["kamera_tv"]["sekunden"] = max(0, min(3600, int(k2["sekunden"] or 0)))
            if "titel" in k2:
                c["kamera_tv"]["titel"] = bool(k2["titel"])
            if k2.get("position") in POSITIONEN:
                c["kamera_tv"]["position"] = k2["position"]
            if k2.get("groesse") in GROESSEN:
                c["kamera_tv"]["groesse"] = k2["groesse"]
            if isinstance(k2.get("kameras"), dict):   # camera.x -> {an, name, src}; None = entfernen
                for e, v in k2["kameras"].items():
                    if not str(e).startswith("camera."):
                        continue
                    if v is None:
                        c["kamera_tv"]["kameras"].pop(e, None)
                    elif isinstance(v, dict):
                        c["kamera_tv"]["kameras"][e] = {"name": str(v.get("name") or "")[:40], "src": str(v.get("src") or "")[:80]}
        if isinstance(neu.get("neustart"), list):
            c["neustart"] = [{"name": str(x.get("name") or "")[:30], "entity": str(x.get("entity") or "")[:120],
                              "ip": str(x.get("ip") or "").strip()[:60]} for x in neu["neustart"] if isinstance(x, dict) and x.get("name")][:8]
        if "version_licht" in neu:
            c["version_licht"] = str(neu["version_licht"] or "")
        if "dock_wache" in neu:
            c["dock_wache"] = bool(neu["dock_wache"])
        if "dock_block_kb" in neu:
            c["dock_block_kb"] = max(1, min(40, int(neu["dock_block_kb"])))
        await self.kd.store.async_save(self.kd.data)
        self.abonnieren()
        return self.stand()

    def stand(self) -> dict:
        st = self.hass.states
        liste = lambda dom: sorted(({"id": s.entity_id, "name": s.attributes.get("friendly_name") or s.entity_id}
                                    for s in st.async_all(dom)), key=lambda x: x["name"].lower())
        notify = sorted(s for s in self.hass.services.async_services().get("notify", {}) if s.startswith("mobile_app_"))
        return {"cfg": self.cfg, "media": liste("media_player"), "kameras": liste("camera"), "lichter": liste("light"),
                "sensoren": [x for x in liste("sensor") + liste("binary_sensor")], "notify": [f"notify.{s}" for s in notify],
                "eingang": (st.get(E_EINGANG).state if st.get(E_EINGANG) else None),
                "tv_quelle": self._tv_text(), "adb_schluessel": self._adb_schluessel() is not None,
                "streamer": [{"id": s["id"], "name": s["name"]} for s in (self.kd.data.get("streamer") or []) if s.get("adb")]}

    # ------------------------------------------------------------------ TV-Eingang
    def _tv_text(self) -> str:
        e = self.cfg["tv_eingang"]["tv"]
        s = self.hass.states.get(e) if e else None
        if s is None:
            return ""
        a = s.attributes
        return " · ".join(str(x) for x in (a.get("source"), a.get("media_content_id"), a.get("app_name")) if x)

    @callback
    def _quelle_geaendert(self, _ev) -> None:
        self._eingang_setzen()

    @callback
    def _eingang_setzen(self) -> None:
        t = self.cfg["tv_eingang"]
        if not t["tv"]:
            self.hass.states.async_remove(E_EINGANG)
            return
        s = self.hass.states.get(t["tv"])
        liste = [str(x) for x in (s.attributes.get("source_list") or [])] if s is not None else []
        if s is not None and not t["streamer"] and t.get("erkennung"):
            # Umstellung der alten Erkennung ("port=3"): passt die aktuelle Quelle dazu, ist sie der Streamer-Eingang
            text = (str(s.attributes.get("source") or "") + " " + str(s.attributes.get("media_content_id") or "")).lower()
            erk = [x.strip().lower() for x in re.split(r"[,;]", t["erkennung"]) if x.strip()]
            if s.attributes.get("source") and any(x in text for x in erk):
                t["streamer"] = str(s.attributes.get("source"))
                t["erkennung"] = ""
                self.kd.data["funktionen"] = self.cfg
                self.hass.async_create_task(self.kd.store.async_save(self.kd.data))
        if s is None or s.state in ("off", "standby", "unavailable", "unknown"):
            wert = "aus"
        else:
            cec = self.hass.states.get(t["cec"]) if t["cec"] else None
            if t["streamer"] and cec is not None and str(cec.state).lower() in ("true", "on", "1"):
                wert = t["streamer"]
            else:
                wert = str(s.attributes.get("source") or "") or "unbekannt"
        self.hass.states.async_set(E_EINGANG, wert[:250], {"friendly_name": _t(self.hass, "ESPHomeRemote TV Eingang"), "icon": "mdi:video-input-hdmi",
                                                           "options": liste + ["aus"], "streamer": bool(t["streamer"]) and wert == t["streamer"]})

    # ------------------------------------------------------------------ Kamera auf dem Fernseher
    @callback
    def _kamera_zustand(self) -> None:
        k = self.cfg["kamera_tv"]
        if not self._kamera_bereit():
            for e in (E_KAMERA, E_GROESSE):
                self.hass.states.async_remove(e)
            return
        name = self._kamera_name(self._letzte) if self._letzte else ""
        self.hass.states.async_set(E_KAMERA, "on" if self._sichtbar else "off",
                                   {"friendly_name": _t(self.hass, "ESPHomeRemote Kamera auf dem TV"), "icon": "mdi:cctv", "kamera": self._letzte, "name": name})
        self.hass.states.async_set(E_GROESSE, k["groesse"], {"friendly_name": _t(self.hass, "ESPHomeRemote Kamera-Größe"), "icon": "mdi:resize",
                                                            "options": list(GROESSEN)})

    def _app_streamer(self):
        """Streamer mit ADB fuer das Overlay der eigenen App (eingestellt oder der erste mit ADB)."""
        alle = getattr(self.hass.data.get(DOMAIN + "_streamer"), "alle", {}) or {}
        sid = self.cfg["kamera_tv"]["streamer"]
        st = alle.get(sid) if sid else None
        return st or next((s for s in alle.values() if s.cfg.get("adb")), None)

    def _kamera_bereit(self) -> bool:
        k = self.cfg["kamera_tv"]
        return bool(self._app_streamer()) if k["weg"] == "app" else bool(k["notify"])

    async def _app_senden(self, kam: str | None) -> None:
        """Overlay der Streamer-App per ADB-Broadcast: zeigen (kam) oder schliessen (None)."""
        from .tv_camera import basis_url

        st = self._app_streamer()
        if st is None:
            _LOGGER.warning("Kamera auf dem TV: kein Streamer mit ADB eingerichtet (Konfigurator > Wiedergabe)")
            return
        k = self.cfg["kamera_tv"]
        b = "am broadcast -a de.esphomeremote.streamer.CAMERA -p de.esphomeremote.streamer"
        if kam is None:
            await st.adb(f"{b} --es aktion aus")
            return
        name = re.sub(r"[^\w äöüÄÖÜß.\-]", "", self._kamera_name(kam))[:40] if k["titel"] else ""
        await st.adb(f"{b} --es aktion zeigen --es kamera {kam} --es name '{name}' --es basis {basis_url(self.hass, self.kd.basis())}"
                     f" --es groesse '{k['groesse']}' --es position {k['position']} --ei sekunden {int(k['sekunden'])}")

    def _kamera_name(self, e: str) -> str:
        v = self.cfg["kamera_tv"]["kameras"].get(e) or {}
        s = self.hass.states.get(e)
        return v.get("name") or (s.attributes.get("friendly_name") if s else "") or e

    async def _dienst_kamera(self, call) -> None:
        kam = str(call.data.get("kamera") or "letzte")
        k = self.cfg["kamera_tv"]
        if not self._kamera_bereit():
            _LOGGER.warning("Kamera auf dem TV: kein Fernseher eingestellt (Konfigurator > Funktionen)")
            return
        if kam == "aus" and k["weg"] == "app":
            await self._app_senden(None)
            self._sichtbar = False
            self._kamera_zustand()
            return
        if kam == "aus":
            await self._senden({"title": "Kamera aus", "message": " ", "data": {"notif_type": "stream", "config": {"duration": 0, "width": 120}}})
            self._sichtbar = False
            self._kamera_zustand()
            return
        if kam == "letzte":
            kam = self._letzte or next(iter(k["kameras"]), "")
        if not kam.startswith("camera."):
            return
        schon = self._sichtbar
        if k["weg"] == "app":
            await self._app_senden(kam)
            if schon:
                await self._popups_erneut()
            self._letzte, self._sichtbar = kam, True
            self._kamera_zustand()
            if k["sekunden"]:
                self._aus_timer(k["sekunden"])
            return
        v = k["kameras"].get(kam) or {}
        objekt = kam.split(".", 1)[1]
        if k["go2rtc"]:
            basis = k["go2rtc"].rstrip("/")
            video, fmt = f"{basis}/webrtc.html?src={v.get('src') or objekt}&media=video", "webrtc"
        else:
            video, fmt = "hls", "hls"
        name = self._kamera_name(kam)
        if schon:
            await self._popups_erneut()
        await self._senden({"title": name, "message": " ", "data": {
            "notif_type": "stream", "entity_id": kam, "camera_name": name, "video": video,
            "config": {"position": k["position"], "duration": 1296000, "fontsize": "small", "width": GROESSEN.get(k["groesse"], 480),
                       "transparency": 0, "show_frame": False}}})
        _LOGGER.debug("Kamera %s auf dem TV (%s)", kam, fmt)
        self._letzte, self._sichtbar = kam, True
        self._kamera_zustand()

    async def _popups_erneut(self) -> None:
        """Kamera lief schon: der Zustand bleibt "on", ein Popup mit Ausloeser "Kamera auf dem TV -> an" ginge nicht
        noch einmal auf. Nutzer 2026-11-01: "wenn die Kamera schon auf ist und ich die Taste nochmal druecke, kommt mein
        Popup nicht - der sollte aber kommen". Darum diese Popups direkt oeffnen (sonst ueber das Dock)."""
        ds = self.hass.data.get(DOMAIN + "_dockstand")
        for dev in list(self.kd.devices):
            try:
                seiten = (self.kd.entry(dev)["config"].get("menu") or {}).get("pages") or []
            except (KeyError, AttributeError, TypeError):
                continue
            for pg in seiten:
                auf = (pg.get("popup") or {}).get("open") or []
                if not any(a.get("entity") == E_KAMERA and not a.get("attr") and str(a.get("to") or "").lower() in ("on", "an") for a in auf):
                    continue
                svc = f"{dev.replace('-', '_')}_menue_popup"
                try:
                    if not self.hass.services.has_service("esphome", svc):
                        raise LookupError
                    await asyncio.wait_for(self.hass.services.async_call("esphome", svc, {"seite": pg["id"]}, blocking=True), 3)
                except Exception:  # noqa: BLE001 - keine eigene Verbindung: das Dock bringt es
                    if ds is not None:
                        ds.test_zeigen(dev, pg["id"])
                break

    def _aus_timer(self, s: int) -> None:
        """Zustand nach Ablauf zuruecksetzen (die App schliesst das Overlay selbst)."""
        if getattr(self, "_timer", None):
            self._timer()
        from homeassistant.helpers.event import async_call_later

        def weg(_now) -> None:
            self._timer = None
            self._sichtbar = False
            self._kamera_zustand()
        self._timer = async_call_later(self.hass, s, weg)

    async def _senden(self, daten: dict) -> None:
        dom, svc = self.cfg["kamera_tv"]["notify"].split(".", 1)
        await self.hass.services.async_call(dom, svc, daten, blocking=False)

    async def _dienst_position(self, call) -> None:
        """Ecke der Kamera (oder "naechste": im Uhrzeigersinn weiter); eine laufende Kamera rueckt sofort."""
        p = str(call.data.get("position") or "naechste")
        k = self.cfg["kamera_tv"]
        if p == "naechste":
            n = ["top-left", "top-right", "bottom-right", "bottom-left"]
            p = n[(n.index(k["position"]) + 1) % 4] if k["position"] in n else "top-right"
        if p not in POSITIONEN:
            return
        k["position"] = p
        await self.kd.store.async_save(self.kd.data)
        if self._sichtbar and self._letzte:
            await self._dienst_kamera(type("C", (), {"data": {"kamera": self._letzte}})())

    async def _dienst_groesse(self, call) -> None:
        g = str(call.data.get("groesse") or "naechste")
        k = self.cfg["kamera_tv"]
        if g == "naechste":
            n = list(GROESSEN)
            g = n[(n.index(k["groesse"]) + 1) % len(n)] if k["groesse"] in n else "Mittel"
        if g not in GROESSEN:
            return
        k["groesse"] = g
        await self.kd.store.async_save(self.kd.data)
        self._kamera_zustand()
        if self._sichtbar and self._letzte:   # laufende Kamera gleich in der neuen Groesse
            await self._dienst_kamera(type("C", (), {"data": {"kamera": self._letzte}})())

    # ------------------------------------------------------------------ Neustart per ADB
    def _adb_schluessel(self):
        try:
            p = self.hass.config.path(".storage/androidtv_adbkey")
            return open(p).read(), open(p + ".pub").read()
        except OSError:
            return None

    async def _dienst_neustart(self, call) -> None:
        name = str(call.data.get("geraet") or "")
        g = next((x for x in self.cfg["neustart"] if x["name"] == name), None)
        if g is None:
            _LOGGER.warning("Neustart: kein Gerät '%s' (Konfigurator > Funktionen)", name)
            return
        if g["entity"]:
            await self.hass.services.async_call("androidtv", "adb_command", {"entity_id": g["entity"], "command": "reboot"}, blocking=False)
            return
        if g["ip"]:
            await self.hass.async_add_executor_job(self._adb_reboot, g["ip"])

    def _adb_reboot(self, ip: str) -> None:
        k = self._adb_schluessel()
        if k is None:
            _LOGGER.warning("Neustart %s: kein ADB-Schlüssel (Integration Android Debug Bridge einmal einrichten)", ip)
            return
        try:
            from adb_shell.adb_device import AdbDeviceTcp
            from adb_shell.auth.sign_pythonrsa import PythonRSASigner
            d = AdbDeviceTcp(ip, 5555, default_transport_timeout_s=10)
            d.connect(rsa_keys=[PythonRSASigner(k[1], k[0])], auth_timeout_s=10)
            try:
                d.shell("reboot", timeout_s=3, read_timeout_s=3)
            except Exception:  # noqa: BLE001 - das Geraet startet, die Antwort bleibt aus
                pass
            d.close()
        except Exception as err:  # noqa: BLE001
            _LOGGER.warning("Neustart %s ging nicht: %s", ip, err)

    # ------------------------------------------------------------------ Neue Firmware: Lampe blinkt
    async def _neue_version(self, _ev) -> None:
        e = self.cfg["version_licht"]
        if not e or self.hass.states.get(e) is None:
            return
        h = self.hass
        await h.services.async_call("scene", "create", {"scene_id": "esphomeremote_vor_blinken", "snapshot_entities": [e]}, blocking=True)
        for _ in range(3):
            await h.services.async_call("light", "turn_off", {"entity_id": e, "transition": 0}, blocking=True)
            await asyncio.sleep(0.5)
            await h.services.async_call("light", "turn_on", {"entity_id": e, "brightness_pct": 100, "transition": 0}, blocking=True)
            await asyncio.sleep(0.5)
        await h.services.async_call("scene", "turn_on", {"entity_id": "scene.esphomeremote_vor_blinken", "transition": 0}, blocking=False)

    # ------------------------------------------------------------------ Dock-Wache
    async def _dock_pruefen(self, _now=None) -> None:
        c = self.cfg
        if not c["dock_wache"]:
            return
        jetzt = time.monotonic()
        for d in self.kd.dock_macs():
            st = self.hass.states.get(f"binary_sensor.{d}_status")
            name = (st.attributes.get("friendly_name") or d).replace(" Status", "") if st else d
            frei = self.hass.states.get(f"sensor.{d}_freier_speicher")
            block = self.hass.states.get(f"sensor.{d}_groesster_freier_block")
            if st is not None and st.state == "off":
                seit = self._dock_off.setdefault(d, jetzt)
                if jetzt - seit > 30 and seit != -1:
                    self._dock_off[d] = -1   # einmal je Ausfall
                    await self._melden(f"esphomeremote_dock_{d}", "Dock nicht erreichbar",
                                       f"{name} ist seit {time.strftime('%H:%M')} nicht mehr im WLAN"
                                       f"{f' (zuletzt {frei.state} kB frei, größter Block {block.state} kB)' if frei and block else ''}.")
            elif st is not None:
                self._dock_off.pop(d, None)
            try:
                b = float(block.state) if block else None
            except ValueError:
                b = None
            if b is not None and b >= c["dock_block_kb"] + 4:
                self._knapp.discard(d)
            if b is not None and b < c["dock_block_kb"] and d not in self._knapp:
                self._knapp.add(d)
                await self._melden(f"esphomeremote_dock_speicher_{d}", "Dock-Speicher knapp",
                                   f"{name} um {time.strftime('%H:%M')}: größter freier Block nur {b:g} kB.")

    async def _melden(self, nid: str, titel: str, text: str) -> None:
        await self.hass.services.async_call("persistent_notification", "create", {"notification_id": nid, "title": _t(self.hass, titel), "message": _t(self.hass, text)}, blocking=False)
