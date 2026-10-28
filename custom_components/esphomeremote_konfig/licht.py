"""Helligkeit nach Lichtsensor (2026-10-17): Remote-Display, Tastenbeleuchtung und Dock-LED.

Eine Kurve je Ziel: Punkte (Lux -> Prozent), dazwischen linear ueber log10(Lux) - so wie das Auge Helligkeit
empfindet (von 1 auf 10 lx wirkt wie von 100 auf 1000 lx). Unter dem ersten / ueber dem letzten Punkt gilt dessen Wert.

Lernen: Verstellt jemand die Helligkeit von Hand (Regler auf der Remote, Konfigurator, LED-Helligkeit des Docks
in HA), wird daraus ein Punkt bei der aktuellen Helligkeit im Raum. Punkte in der Naehe (+-0,3 Dekaden, also etwa
Faktor 2) ersetzt er; die Kurve bleibt steigend (heller Raum -> nie dunkler als bei weniger Licht).

Gespeichert in data["licht"] (Konfigurator-Speicher):
  sensor (Standard), je: {geraet: sensor} (eigener Sensor je Remote/Dock - jeder Raum ist anders hell, 2026-10-17),
  lernen, ziele: {disp|kbd|dock: {an, punkte: [[lux, pct], ...]}}, aus: [Geraete ohne Automatik],
  docks: {dock-slug: {led, blinkt}}  (Dock-Einstellungen; led = feste Helligkeit, wenn die Automatik dort aus ist)
Die Remote bekommt die Werte ueber die Aktion helligkeit_auto (Firmware), das Dock ueber seine number-Entitaet.
"""
from __future__ import annotations

import asyncio
import copy
import logging
import math

from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event

_LOGGER = logging.getLogger(__name__)
ZIELE = ("disp", "kbd", "dock")
STANDARD = {
    "sensor": "",   # im Konfigurator waehlen (Einstellungen > Helligkeit nach Lichtsensor)
    "lernen": True,
    "ziele": {
        "disp": {"an": True, "punkte": [[2, 55], [50, 75], [400, 100]]},
        "kbd": {"an": True, "punkte": [[2, 45], [50, 60], [400, 80]]},
        "dock": {"an": True, "punkte": [[2, 35], [50, 50], [400, 80]]},
    },
    "aus": [],
    "docks": {},
    "je": {},
}
SCHWELLE = 3        # Prozentpunkte: kleinere Aenderungen nicht senden (kein Flackern, wenig Flash-Schreiben)
NAH = 0.3           # Dekaden: so nah liegende Punkte ersetzt ein gelernter Punkt
MAX_PUNKTE = 8


def _x(lux: float) -> float:
    return math.log10(max(float(lux), 0.5))


def kurve(punkte: list, lux: float) -> int:
    p = sorted(punkte)
    if not p:
        return 50
    x = _x(lux)
    if x <= _x(p[0][0]):
        return round(p[0][1])
    for (l1, v1), (l2, v2) in zip(p, p[1:]):
        x1, x2 = _x(l1), _x(l2)
        if x <= x2:
            return round(v1 + (v2 - v1) * ((x - x1) / (x2 - x1) if x2 > x1 else 1))
    return round(p[-1][1])


def lerne(punkte: list, lux: float, wert: int) -> list:
    lux = round(max(float(lux), 0.5), 1)
    x = _x(lux)
    neu = [[l, v] for l, v in punkte if abs(_x(l) - x) > NAH]
    neu = [[l, min(v, wert)] if l < lux else [l, max(v, wert)] for l, v in neu]
    neu.append([lux, int(wert)])
    neu.sort()
    while len(neu) > MAX_PUNKTE:   # den Punkt mit dem engsten Nachbarn (ausser dem neuen) weglassen
        i = min((j for j in range(1, len(neu) - 1) if neu[j][0] != lux),
                key=lambda j: _x(neu[j + 1][0]) - _x(neu[j - 1][0]), default=None)
        if i is None:
            break
        del neu[i]
    return neu


class Licht:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.gesendet: dict[str, dict] = {}     # Geraet -> {ziel: wert}, zuletzt von hier geschickt
        self.zeit: dict[str, float] = {}
        self._unsub = None

    @property
    def cfg(self) -> dict:
        c = self.kd.data.setdefault("licht", {})
        for k, v in STANDARD.items():
            if k not in c:
                c[k] = copy.deepcopy(v)
        for z in ZIELE:
            if z not in c["ziele"]:
                c["ziele"][z] = copy.deepcopy(STANDARD["ziele"][z])
        return c

    def sensor(self, geraet: str | None = None) -> str:
        """Sensor eines Geraets: eigener (cfg.je) oder der Standard."""
        return (self.cfg["je"].get(geraet) if geraet else None) or self.cfg["sensor"] or ""

    def lux(self, geraet: str | None = None) -> float | None:
        st = self.hass.states.get(self.sensor(geraet))
        try:
            return float(st.state) if st else None
        except ValueError:
            return None

    def wert(self, ziel: str, lux: float | None = None, geraet: str | None = None) -> int | None:
        z = self.cfg["ziele"][ziel]
        lux = self.lux(geraet) if lux is None else lux
        return kurve(z["punkte"], lux) if z.get("an") and lux is not None else None

    # ------------------------------------------------------------------ Start / Ereignisse
    async def starten(self) -> None:
        self._sensor_folgen()
        self.hass.bus.async_listen("esphome.remote_verbunden", self._remote_verbunden)
        self.hass.bus.async_listen("state_changed", self._dock_zustand)
        self.hass.async_create_task(self.alle_senden(erzwingen=True))

    def _sensor_folgen(self) -> None:
        if self._unsub:
            self._unsub()
        alle = sorted({x for x in [self.cfg["sensor"], *self.cfg["je"].values()] if x})
        self._unsub = async_track_state_change_event(self.hass, alle, self._sensor_neu) if alle else None

    @callback
    def _sensor_neu(self, _ev: Event) -> None:
        self.hass.async_create_task(self.alle_senden())

    @callback
    def _remote_verbunden(self, ev: Event) -> None:
        dev = str(ev.data.get("geraet") or "")
        if dev in self.kd.devices:
            self.gesendet.pop(dev, None)
            self.hass.async_create_task(self.remote_senden(dev, erzwingen=True))

    @callback
    def _dock_zustand(self, ev: Event) -> None:
        eid = ev.data.get("entity_id", "")
        if not (eid.startswith("number.dock_") and eid.endswith("_led_helligkeit")):
            return
        dock = eid[len("number."):-len("_led_helligkeit")]
        alt, neu = ev.data.get("old_state"), ev.data.get("new_state")
        if neu is None or neu.state in ("unavailable", "unknown"):
            return
        if alt is None or alt.state in ("unavailable", "unknown"):   # Dock (wieder) da: Werte schicken
            self.gesendet.pop(dock, None)
            self.hass.async_create_task(self.dock_senden(dock, erzwingen=True))
            return
        try:
            v = round(float(neu.state))
        except ValueError:
            return
        if abs(v - self.gesendet.get(dock, {}).get("dock", -99)) <= 1 or round(float(alt.state)) == v:
            return
        # Von Hand verstellt (HA, Dashboard): Automatik an -> lernen, sonst als feste Helligkeit merken
        if self._automatik(dock, "dock"):
            self.lernen("dock", v, dock)
        else:
            self.cfg["docks"].setdefault(dock, {})["led"] = v
            self.kd.store.async_delay_save(lambda: self.kd.data, 5)

    @callback
    def einst_gemeldet(self, dev: str, alt: dict, neu: dict) -> None:
        """Einstellungs-Abgleich der Remote: disp_bri/kbd_bri von Hand verstellt -> Kurve lernt."""
        for ziel, k in (("disp", "disp_bri"), ("kbd", "kbd_bri")):
            v = neu.get(k)
            if not isinstance(v, int) or isinstance(v, bool) or alt.get(k) is None or alt.get(k) == v:
                continue
            if abs(v - self.gesendet.get(dev, {}).get(ziel, -99)) <= 1 or not self._automatik(dev, ziel):
                continue
            self.gesendet.setdefault(dev, {})[ziel] = v
            self.lernen(ziel, v, dev)

    # ------------------------------------------------------------------ Lernen
    def _automatik(self, geraet: str, ziel: str) -> bool:
        return bool(self.cfg["ziele"][ziel].get("an")) and geraet not in self.cfg["aus"] and self.lux(geraet) is not None

    def lernen(self, ziel: str, wert: int, geraet: str | None = None) -> None:
        lux = self.lux(geraet)
        if lux is None or not self.cfg.get("lernen"):
            return
        z = self.cfg["ziele"][ziel]
        z["punkte"] = lerne(z["punkte"], lux, wert)
        _LOGGER.info("Licht: %s gelernt %.1f lx (%s) -> %d %% (%s)", ziel, lux, geraet or "-", wert, z["punkte"])
        self.kd.store.async_delay_save(lambda: self.kd.data, 5)
        self.hass.bus.async_fire("esphomeremote_konfig_licht", {"ziel": ziel})
        self.hass.async_create_task(self.alle_senden())

    # ------------------------------------------------------------------ Senden
    def docks(self) -> list[str]:
        return sorted(s.entity_id[len("number."):-len("_led_helligkeit")] for s in self.hass.states.async_all("number")
                      if s.entity_id.startswith("number.dock_") and s.entity_id.endswith("_led_helligkeit"))

    async def alle_senden(self, erzwingen: bool = False) -> None:
        for dev in list(self.kd.devices):
            await self.remote_senden(dev, erzwingen)
        for dock in self.docks():
            await self.dock_senden(dock, erzwingen)

    async def remote_senden(self, dev: str, erzwingen: bool = False) -> None:
        slug = dev.replace("-", "_").lower()
        if not self.hass.services.has_service("esphome", f"{slug}_helligkeit_auto"):
            return
        aus = dev in self.cfg["aus"]
        werte = {z: (None if aus else self.wert(z, geraet=dev)) for z in ("disp", "kbd")}
        alt = self.gesendet.get(dev, {})
        schicken = {z: v for z, v in werte.items() if v is not None and (erzwingen or abs(v - alt.get(z, -99)) >= SCHWELLE)}
        if not schicken:
            return
        self.gesendet.setdefault(dev, {}).update(schicken)
        try:
            await asyncio.wait_for(self.hass.services.async_call(
                "esphome", f"{slug}_helligkeit_auto", {"disp": schicken.get("disp", -1), "kbd": schicken.get("kbd", -1)},
                blocking=True), 15)
            _LOGGER.debug("Licht: %s <- %s", dev, schicken)
        except Exception as err:  # noqa: BLE001 - Remote schlaeft: beim naechsten Verbinden (remote_verbunden)
            _LOGGER.debug("Licht: %s nicht erreichbar: %s", dev, err)
            for z in schicken:
                self.gesendet[dev].pop(z, None)

    async def dock_senden(self, dock: str, erzwingen: bool = False) -> None:
        eid = f"number.{dock}_led_helligkeit"
        st = self.hass.states.get(eid)
        if st is None or st.state in ("unavailable", "unknown"):
            return
        einst = self.cfg["docks"].get(dock, {})
        v = self.wert("dock", geraet=dock) if dock not in self.cfg["aus"] else None
        if v is None:
            v = einst.get("led")
        alt = self.gesendet.get(dock, {}).get("dock", -99)
        if v is not None and (erzwingen or abs(v - alt) >= SCHWELLE):
            v = max(5, min(100, 5 * round(v / 5)))   # Schrittweite der Entitaet
            self.gesendet.setdefault(dock, {})["dock"] = v
            if round(float(st.state)) != v:
                await self.hass.services.async_call("number", "set_value", {"entity_id": eid, "value": v}, blocking=False)
        sw = f"switch.{dock}_led_beim_senden_blinken"
        b = einst.get("blinkt")
        s2 = self.hass.states.get(sw)
        if b is not None and s2 is not None and s2.state in ("on", "off") and (s2.state == "on") != b:
            await self.hass.services.async_call("switch", "turn_on" if b else "turn_off", {"entity_id": sw}, blocking=False)

    # ------------------------------------------------------------------ Konfigurator
    def stand(self) -> dict:
        c = self.cfg
        lux = self.lux()
        sensoren = sorted(
            ({"id": s.entity_id, "name": s.attributes.get("friendly_name") or s.entity_id, "wert": s.state}
             for s in self.hass.states.async_all("sensor") if s.attributes.get("device_class") == "illuminance"),
            key=lambda x: x["name"].lower())
        docks = []
        for d in self.docks():
            st = self.hass.states.get(f"number.{d}_led_helligkeit")
            sw = self.hass.states.get(f"switch.{d}_led_beim_senden_blinken")
            e = c["docks"].get(d, {})
            docks.append({"id": d, "name": (st.attributes.get("friendly_name") or d).replace(" LED-Helligkeit", ""),
                          "online": st.state not in ("unavailable", "unknown"),
                          "led_ist": None if st.state in ("unavailable", "unknown") else round(float(st.state)),
                          "led": e.get("led", 40), "blinkt": e.get("blinkt", sw.state == "on" if sw and sw.state in ("on", "off") else True),
                          "auto": d not in c["aus"], "sensor": c["je"].get(d, ""), "lux": self.lux(d)})
        remotes = {dev: {"sensor": c["je"].get(dev, ""), "lux": self.lux(dev)} for dev in self.kd.devices}
        return {"cfg": {k: c[k] for k in ("sensor", "lernen", "ziele", "aus", "je")}, "lux": lux, "sensoren": sensoren,
                "werte": {z: self.wert(z, lux) for z in ZIELE}, "docks": docks, "remotes": remotes}

    async def setzen(self, neu: dict) -> None:
        c = self.cfg
        if "sensor" in neu and neu["sensor"] != c["sensor"]:
            c["sensor"] = neu["sensor"]
            self._sensor_folgen()
        if "je" in neu:   # {geraet: sensor}; "" = Standard
            for g, sid in (neu["je"] or {}).items():
                if sid:
                    c["je"][str(g)] = str(sid)
                else:
                    c["je"].pop(str(g), None)
            self._sensor_folgen()
        if "lernen" in neu:
            c["lernen"] = bool(neu["lernen"])
        for z, d in (neu.get("ziele") or {}).items():
            if z not in ZIELE:
                continue
            if "an" in d:
                c["ziele"][z]["an"] = bool(d["an"])
            if "punkte" in d:
                pk = sorted([max(0.5, min(100000.0, float(l))), max(5, min(100, int(v)))] for l, v in d["punkte"])
                c["ziele"][z]["punkte"] = pk[:MAX_PUNKTE] or [list(p) for p in STANDARD["ziele"][z]["punkte"]]
            if d.get("zuruecksetzen"):
                c["ziele"][z]["punkte"] = [list(p) for p in STANDARD["ziele"][z]["punkte"]]
        if "aus" in neu:
            c["aus"] = [str(x) for x in neu["aus"]]
        for dock, e in (neu.get("docks") or {}).items():
            ziel = c["docks"].setdefault(dock, {})
            if "led" in e:
                ziel["led"] = max(5, min(100, int(e["led"])))
            if "blinkt" in e:
                ziel["blinkt"] = bool(e["blinkt"])
        await self.kd.store.async_save(self.kd.data)
        self.gesendet.clear()
        await self.alle_senden(erzwingen=True)
