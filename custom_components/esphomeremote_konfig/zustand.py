"""Stand der Remotes fuer die Docks (2026-10-19, Stufe 3 "Dock als Hauptweg").

Die Remote zeigt nach dem Tiefschlaf sofort den aktuellen Stand, bevor ihre eigene HA-Verbindung steht: sie fragt per
Funk beim Dock (dock_direkt typ 4), das Dock antwortet mit dem zuletzt von HA abgelegten Stand (typ 5). Hier wird
dieser Stand je Remote gebaut - aus ihren Spiegeln (spiegel.py: Wiedergabe, Fernseher, Timer) - und bei jeder
Aenderung an alle Docks geschickt (ESPHome-Aktion esphome.<dock>_zustand {remote, inhalt}), dazu alle 10 min erneut
(Dock neu gestartet).
Schluessel = Felder, die die Remote damit fuellt (templates/remote.yaml, dock_direkt on_zustand).
"""

from __future__ import annotations

import json
import time
import logging
import os
from datetime import timedelta

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.helpers.event import async_call_later, async_track_state_change_event, async_track_time_interval
from homeassistant.util import dt as dt_util

from .spiegel import spiegel_id

_LOGGER = logging.getLogger(__name__)


def popup_ausloeser(menu: dict | None) -> list[tuple[str, str]]:
    """Popup-Ausloeser (Entitaet, Attribut) in der Reihenfolge der Remote (menue.cpp zustaende_von_dock)."""
    out: list[tuple[str, str]] = []
    for p in (menu or {}).get("pages") or []:
        if not p.get("id"):
            continue
        for art in ("open", "close", "if"):
            for a in ((p.get("popup") or {}).get(art)) or []:
                k = (str(a.get("entity") or ""), str(a.get("attr") or ""))
                if k[0] and k not in out:
                    out.append(k)
    return out


def popup_werte(menu: dict | None) -> dict[tuple[str, str], set[str]]:
    """Werte, bei denen ein Popup auf- oder zugeht ("to" aus open/close), je (Entitaet, Attribut) - klein geschrieben."""
    out: dict[tuple[str, str], set[str]] = {}
    for p in (menu or {}).get("pages") or []:
        for art in ("open", "close"):
            for a in ((p.get("popup") or {}).get(art)) or []:
                k = (str(a.get("entity") or ""), str(a.get("attr") or ""))
                if k[0]:
                    out.setdefault(k, set()).add(str(a.get("to") or "").lower())
    return out


def _wert(hass: HomeAssistant, entity: str, attr: str):
    st = hass.states.get(entity)
    if st is None:
        return None
    if not attr:
        return st.state[:40]
    v = st.attributes.get(attr)
    return "" if v is None else str(v)[:40]


def _gehalten(hass: HomeAssistant, entity: str, attr: str, gehalten: dict | None):
    """Aktueller Wert - oder ein kurz zuvor erreichter Ausloese-Wert, solange er gehalten wird (s. DockStand.HALTEN_S)."""
    h = (gehalten or {}).get((entity, attr))
    if h is not None and h[1] > time.monotonic():
        return h[0]
    return _wert(hass, entity, attr)


def _bild_ts(name: str) -> int:
    try:
        return int(os.stat(f"/config/www/{name}").st_mtime)
    except OSError:
        return 0


def _stand(hass: HomeAssistant, slug: str, ausl: list[tuple[str, str]] | None = None,
           gehalten: dict[tuple[str, str], tuple[str, float]] | None = None) -> dict:
    j = hass.states.get(spiegel_id(slug, "jetzt"))
    tv = hass.states.get(spiegel_id(slug, "tv"))
    tm = hass.states.get(spiegel_id(slug, "timer"))
    a = j.attributes if j else {}
    pos = a.get("media_position")
    try:
        pos = float(pos) if pos is not None else -1.0
    except (TypeError, ValueError):
        pos = -1.0
    if pos >= 0 and j is not None and j.state == "playing" and (t := a.get("media_position_updated_at")):
        try:
            pos += max((dt_util.utcnow() - (t if not isinstance(t, str) else dt_util.parse_datetime(t))).total_seconds(), 0) \
                * float(a.get("tempo") or 1)
        except (TypeError, ValueError, AttributeError):
            pass
    ta = tm.attributes if tm else {}
    kh = hass.states.get(f"sensor.esphomeremote_konfig_{slug}")
    return {
        # Soll-Stand der Tasten-Konfiguration (2026-10-23, Umbau "Dock als Zentrale"): die Remote holt ihn ohne eigene
        # HA-Verbindung ueber das Dock (HTTP typ 6-9), die Einstellungen stecken mit darin
        "kh": kh.state if kh is not None and len(kh.state) == 8 else "",
        "mp_state": j.state if j else "off",
        "mp_title": str(a.get("media_title") or "")[:120],
        "mp_artist": str(a.get("media_artist") or "")[:80],
        "mp_app": str(a.get("app_name") or "")[:40],
        "mp_cover": str(a.get("entity_picture") or "")[:250],
        "mp_quelle": str(a.get("quelle") or ""),
        "mp_tempo": float(a.get("tempo") or 1),
        "mp_dur": float(a.get("media_duration") or 0),
        "mp_pos": round(pos, 1),
        "tv": tv.state if tv else "unavailable",
        "tm": tm.state if tm else "idle",
        "tm_rest": str(ta.get("remaining") or ""),
        "tm_ende": str(ta.get("finishes_at") or ""),
        # Popups ueber das Dock (2026-10-24): Zustaende der Popup-Ausloeser (Reihenfolge s. popup_ausloeser) und
        # Zeitstempel der Klingelbilder (Haus, Wohnung) - die Remote holt ein neues Bild dann ueber das Dock
        "pz": [_gehalten(hass, e, a, gehalten) for e, a in (ausl or [])],
        "db": [_bild_ts("esphomeremote_doorbell_haus.jpg"), _bild_ts("esphomeremote_doorbell_wohnung.jpg")],
        # alle Klingeln (2026-10-30): Kennung -> Zeitstempel des Bilds (die Remote laedt das gerade gezeigte neu)
        "dbk": {k["id"]: _bild_ts(f"esphomeremote_doorbell_{k['id']}.jpg") for k in (hass.data.get("esphomeremote_konfig_bilder").klingeln
                if hass.data.get("esphomeremote_konfig_bilder") else []) if k.get("id")},
    }


class DockStand:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self._abo = None
        self._geplant: dict[str, object] = {}
        self._zuletzt: dict[str, str] = {}
        self.devs: list[str] = []
        self._ausl: dict[str, list[tuple[str, str]]] = {}   # geraet -> Popup-Ausloeser
        self._wartung: dict[str, float] = {}   # geraet -> Wartungsfenster angefordert bis (monotonic)
        self._wartung_abo: dict[str, object] = {}   # geraet -> Abo "WLAN aktiv" (beendet die Anforderung)
        self._wdauer: dict[str, tuple[int, str]] = {}
        self._wa: dict[str, tuple[int, float]] = {}   # geraet -> ("WLAN jetzt aus"-Nummer, gueltig bis)
        self._wa_nr = int(time.time()) % 100000
        # Kurze Ausloeser halten (2026-10-25): ein Skript steht nur Sekundenbruchteile auf "on" - die Remote fragt das
        # Dock aber nur alle 2 s und verpasste es (Kamera-Popup). Erreicht ein Ausloeser einen Popup-Wert, meldet das
        # Dock ihn HALTEN_S lang, auch wenn er schon zurueckgesprungen ist.
        self._gehalten: dict[tuple[str, str], tuple[str, float]] = {}
        self._werte: dict[str, dict[tuple[str, str], set[str]]] = {}
        # "Auf der Remote zeigen" (Konfigurator, 2026-10-25): Seite + Zaehler, gilt TEST_S lang ("tp" im Stand)
        self._test: dict[str, tuple[str, int, float]] = {}
        self._test_nr = int(time.time()) % 100000

    @callback
    def starten(self) -> None:
        if self._abo:
            self._abo()
        self.devs = list(self.kd.devices)
        slugs = [d.replace("-", "_") for d in self.devs]
        ids = [spiegel_id(s, r) for s in slugs for r in ("jetzt", "tv", "timer")]
        self._ausl = {d: self.ausloeser(d) for d in self.devs}
        self._werte = {d: self._popup_werte(d) for d in self.devs}
        ids += sorted({e for l in self._ausl.values() for e, _ in l} - set(ids))
        self._abo = async_track_state_change_event(self.hass, ids, self._geaendert)
        # Dock (wieder) online (Neustart, Strom): gleich alle Staende schicken - es hat sie im RAM verloren
        if getattr(self, "_dock_abo", None):
            self._dock_abo()
        docks = [f"binary_sensor.{d}_status" for d in self.kd.dock_macs()]
        self._dock_abo = async_track_state_change_event(self.hass, docks, self._dock_da) if docks else None
        for d in self.kd.devices:
            self._planen(d, 3)

    def ausloeser(self, dev: str) -> list[tuple[str, str]]:
        try:
            return popup_ausloeser(self.kd.entry(dev)["config"].get("menu"))
        except (KeyError, AttributeError, TypeError):
            return []

    def _popup_werte(self, dev: str) -> dict[tuple[str, str], set[str]]:
        try:
            return popup_werte(self.kd.entry(dev)["config"].get("menu"))
        except (KeyError, AttributeError, TypeError):
            return {}

    @callback
    def konfig_neu(self, dev: str) -> None:
        """Neue Tasten-Konfiguration: andere Popup-Ausloeser -> Abo neu (announce)."""
        self._werte[dev] = self._popup_werte(dev)
        if self.ausloeser(dev) != self._ausl.get(dev):
            self.starten()

    @callback
    def _geaendert(self, ev: Event) -> None:
        eid = ev.data["entity_id"]
        for d in self.kd.devices:
            if eid.startswith(f"sensor.esphomeremote_spiegel_{d.replace('-', '_')}_"):
                self._planen(d, 1)
            elif any(e == eid for e, _ in self._ausl.get(d, ())):
                self._halten(d, eid, ev)
                self._planen(d, 0.05)   # Popup: schnell ans Dock (0,2 s bis 2026-10-26)

    HALTEN_S = 5.0

    @callback
    def _halten(self, dev: str, eid: str, ev: Event) -> None:
        neu = ev.data.get("new_state")
        if neu is None:
            return
        for (e, a), werte in self._werte.get(dev, {}).items():
            if e != eid:
                continue
            w = neu.state[:40] if not a else ("" if neu.attributes.get(a) is None else str(neu.attributes.get(a))[:40])
            if w.lower() not in werte:
                continue
            self._gehalten[(e, a)] = (w, time.monotonic() + self.HALTEN_S)
            # danach den echten Wert nachschicken
            @callback
            def _nachschicken(_now, d=dev) -> None:
                self._planen(d, 0)

            async_call_later(self.hass, self.HALTEN_S + 0.1, _nachschicken)

    @callback
    def _dock_da(self, ev: Event) -> None:
        neu = ev.data.get("new_state")
        if neu is not None and neu.state == "on":
            self.hass.async_create_task(self._nach_dockstart())

    async def _nach_dockstart(self) -> None:
        import asyncio
        await asyncio.sleep(3)   # API-Aktionen des Docks sind gleich nach dem Verbinden noch nicht registriert
        await self.alle()

    @callback
    def _planen(self, dev: str, s: float) -> None:
        if dev in self._geplant:
            return
        @callback
        def _los(_now, dev=dev) -> None:
            self.hass.async_create_task(self.senden(dev))

        self._geplant[dev] = async_call_later(self.hass, s, _los)

    def wartung_anfordern(self, dev: str, sekunden: float = 900, dauer: int = 180, grund: str = "Konfigurator") -> None:
        """Wartungsfenster ueber das Dock (2026-10-25): die Remote sieht "w" im Stand und schaltet ihr WLAN 3 min an -
        Firmware-Update ohne Bluetooth-Link zu HA. Gilt, bis sie sich meldet oder die Zeit um ist."""
        self._wartung[dev] = time.monotonic() + sekunden
        self._wdauer[dev] = (int(dauer), str(grund)[:16])   # Dauer/Grund des Fensters auf der Remote (2026-10-29)
        self._planen(dev, 0.2)
        # Sobald das WLAN der Remote steht, ist die Anforderung erledigt (2026-10-28): sonst sah die Remote nach dem Ende
        # ihres 3-min-Fensters wieder "w" und schaltete das WLAN erneut an - bis zu 10 min lang (WLAN-Knopf). Jede
        # WLAN-Phase stoert ausserdem den Dock-Weg, s. dock_direkt "Kam das Paket per Funk".
        if (alt := self._wartung_abo.pop(dev, None)) is not None:
            alt()
        eid = f"binary_sensor.{dev.replace('-', '_')}_wlan_aktiv"

        @callback
        def _wlan(ev: Event, d=dev) -> None:
            neu = ev.data.get("new_state")
            if neu is not None and neu.state == "on":
                if (ab := self._wartung_abo.pop(d, None)) is not None:
                    ab()
                self.wartung_fertig(d)

        self._wartung_abo[dev] = async_track_state_change_event(self.hass, [eid], _wlan)

    def wlan_aus(self, dev: str) -> None:
        """WLAN der Remote jetzt aus (2026-10-29): "wa" (Nummer) steht 60 s im Stand, die Remote fuehrt es einmal aus."""
        self.wartung_fertig(dev)
        self._wa_nr += 1
        self._wa[dev] = (self._wa_nr, time.monotonic() + 60)
        self._planen(dev, 0)
        async_call_later(self.hass, 60.5, lambda _now, d=dev: self._planen(d, 0))

    def wartung_fertig(self, dev: str) -> None:
        if (ab := self._wartung_abo.pop(dev, None)) is not None:
            ab()
        if self._wartung.pop(dev, None) is not None:
            self._planen(dev, 0.2)

    TEST_S = 30.0

    def test_zeigen(self, dev: str, seite: str) -> None:
        """Menueseite/Popup testweise auf der Remote zeigen: steht TEST_S lang als "tp" [seite, nr] im Stand - die
        Remote fragt das Dock, solange sie benutzt wird, und oeffnet die Seite einmal je nr."""
        self._test_nr += 1
        self._test[dev] = (seite, self._test_nr, time.monotonic() + self.TEST_S)
        self._planen(dev, 0)

        @callback
        def _weg(_now, d=dev, nr=self._test_nr) -> None:
            if self._test.get(d, ("", 0, 0))[1] == nr:
                self._test.pop(d, None)
                self._planen(d, 0)

        async_call_later(self.hass, self.TEST_S + 0.1, _weg)

    async def senden(self, dev: str, immer: bool = False) -> None:
        self._geplant.pop(dev, None)
        st = _stand(self.hass, dev.replace("-", "_"), self._ausl.get(dev), self._gehalten)
        if self._wartung.get(dev, 0) > time.monotonic():
            st["w"] = 1
            st["wd"], st["wg"] = self._wdauer.get(dev, (180, "Konfigurator"))
        if (wa := self._wa.get(dev)) and wa[1] > time.monotonic():
            st["wa"] = wa[0]
        if (t := self._test.get(dev)) and t[2] > time.monotonic():
            st["tp"] = [t[0], t[1]]
        # Docks, die gerade online sind (2026-10-25): die Remote probiert sie zuerst
        st["do"] = [d for d in self.kd.dock_macs()
                    if (s := self.hass.states.get(f"binary_sensor.{d}_status")) is not None and s.state == "on"]
        vergleich = json.dumps({k: v for k, v in st.items() if k != "mp_pos"}, sort_keys=True)
        if not immer and self._zuletzt.get(dev) == vergleich:
            return
        self._zuletzt[dev] = vergleich
        st["ts"] = int(dt_util.utcnow().timestamp())
        text = json.dumps(st, separators=(",", ":"), ensure_ascii=False)
        for dock in self.kd.dock_macs():
            if self.hass.services.has_service("esphome", f"{dock}_zustand"):
                try:
                    await self.hass.services.async_call("esphome", f"{dock}_zustand", {"remote": dev, "inhalt": text}, blocking=False)
                except Exception as err:  # noqa: BLE001 - Dock offline
                    _LOGGER.debug("%s_zustand: %s", dock, err)

    async def alle(self, _now=None) -> None:
        for d in self.kd.devices:
            await self.senden(d, immer=True)


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/test_zeigen", vol.Required("device"): str,
                                  vol.Required("seite"): str})
@websocket_api.async_response
async def ws_test_zeigen(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict) -> None:
    """Konfigurator "Auf der Remote zeigen": direkt (eigene HA-Verbindung) und ueber das Dock."""
    import asyncio
    ds: DockStand = hass.data["esphomeremote_konfig_dockstand"]
    dev, seite = msg["device"], msg["seite"][:40]
    if dev not in ds.kd.devices:
        connection.send_error(msg["id"], "unknown_device", "Unbekannte Remote")
        return
    # Klingelbilder auf der Seite: vorher frisch aufnehmen (sonst zeigt die Remote das Bild vom letzten Klingeln - oder keins)
    try:
        cfg = ds.kd.entry(dev)["config"]
        page = next((p for p in (cfg.get("menu") or {}).get("pages") or [] if p.get("id") == seite), {})
        quellen = {it.get("src") for it in page.get("items") or [] if it.get("t") == "camera"}
        b = hass.data.get("esphomeremote_konfig_bilder")
        for src in quellen:
            if b is not None and str(src or "").startswith("klingel"):
                await asyncio.wait_for(b.klingel(src, warten=False), 8)
    except Exception as err:  # noqa: BLE001 - Bild ist Beiwerk, die Seite kommt trotzdem
        _LOGGER.debug("Testbild: %s", err)
    direkt = False
    svc = f"{dev.replace('-', '_')}_menue_popup"
    if hass.services.has_service("esphome", svc):
        try:
            await asyncio.wait_for(hass.services.async_call("esphome", svc, {"seite": seite}, blocking=True), 3)
            direkt = True
        except Exception:  # noqa: BLE001 - keine eigene Verbindung: das Dock bringt es
            pass
    if not direkt:
        ds.test_zeigen(dev, seite)
    connection.send_result(msg["id"], {"direkt": direkt, "sekunden": int(ds.TEST_S)})


def einrichten(hass: HomeAssistant, kd) -> DockStand:
    websocket_api.async_register_command(hass, ws_test_zeigen)
    ds = DockStand(hass, kd)
    hass.data["esphomeremote_konfig_dockstand"] = ds
    ds.starten()
    async_track_time_interval(hass, ds.alle, timedelta(minutes=10))
    return ds
