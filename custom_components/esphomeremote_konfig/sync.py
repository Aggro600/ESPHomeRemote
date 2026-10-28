"""Remotes synchron halten (2026-10-22).

Eine Gruppe von Remotes bekommt dieselbe Belegung und dieselben Einstellungen - nur der Name bleibt je Remote.
Was mitlaeuft, steht je Bereich ("cfg:<schluessel>") und je Einstellung ("einst:<name>") in data["sync"]["aus"]
(abgewaehlt). Ausloeser:
  - Speichern im Konfigurator (KonfigData.save)            -> Belegung an die anderen
  - Einstellung im Konfigurator (KonfigData.einst_setzen)    -> Wert an die anderen
  - Einstellung an der Remote umgestellt (einst_gemeldet)    -> Wert an die anderen
Gespeichert in data["sync"] = {"an", "geraete", "aus", "log"}.
"""
from __future__ import annotations

import copy
import json
import logging

from homeassistant.util import dt as dt_util

_LOGGER = logging.getLogger(__name__)

# Bereiche der Belegung (Schluessel in cfg) - Reihenfolge = Anzeige im Konfigurator
CFG_BEREICHE = ("activities", "home", "menu", "modi", "gesten", "makros", "ir", "kameras", "slots", "bt", "ents",
                "begruessung", "enabled")
# Von Anfang an abgewaehlt: Begruessungstext (enthaelt meist den Namen), Debug-Modus (nur zum Entwickeln),
# Nur Bluetooth (Funkweg je Remote wie ha_link_modus; 2026-10-25: eine Fehlmeldung beim Start einer Remote schaltete
# sonst bei allen das WLAN wieder ein)
STANDARD_AUS = ("cfg:begruessung", "einst:ps_log_debug", "einst:nur_ble")
# Spaeter dazugekommen: einmal in bestehende Gruppen eintragen (danach kann man es im Konfigurator wieder anwaehlen)
NACHTRAG_AUS = ("einst:nur_ble",)
# Mehr gleichzeitig geaenderte Werte in einer Meldung der Remote gelten als Neustart/Zuruecksetzen, nicht als Bedienung
MAX_GEMELDET = 6
# Einstellungen, die die Firmware nicht mehr kennt (stehen noch im gemeldeten Stand alter Remotes)
VERALTET = ("kb_ziffern",)
LOG_MAX = 30


class Sync:
    def __init__(self, kd) -> None:
        self.kd = kd
        self._laeuft = False   # verhindert Rueckkopplung: Aenderungen aus dem Abgleich loesen keinen neuen aus
        self.pause = False     # Sicherung einspielen (2026-10-26): jede Remote genau ihren Stand, nichts weitergeben

    # ---- Stand ----
    @property
    def d(self) -> dict:
        s = self.kd.data.setdefault("sync", {})
        s.setdefault("an", False)
        s.setdefault("geraete", [])
        s.setdefault("aus", list(STANDARD_AUS))
        s.setdefault("log", [])
        for k in NACHTRAG_AUS:
            if k not in s.setdefault("nachgetragen", []):
                s["nachgetragen"].append(k)
                if k not in s["aus"]:
                    s["aus"].append(k)
        return s

    def gruppe(self, dev: str) -> list[str]:
        """Die anderen Remotes der Gruppe, wenn dev dazugehoert und der Abgleich an ist."""
        s = self.d
        if not s["an"] or dev not in s["geraete"] or self._laeuft:
            return []
        return [x for x in s["geraete"] if x != dev and x in self.kd.devices]

    def mit(self, schluessel: str) -> bool:
        return schluessel not in self.d["aus"]

    def _log(self, von: str, nach: list[str], was: str) -> None:
        log = self.d["log"]
        log.insert(0, {"zeit": dt_util.now().isoformat(timespec="seconds"), "von": von, "nach": nach, "was": was})
        del log[LOG_MAX:]

    # ---- Belegung ----
    def _cfg_uebertragen(self, quelle: dict, ziel: dict) -> tuple[dict, list[str]]:
        """Neue Belegung fuer ziel: gewaehlte Bereiche aus quelle, der Rest bleibt. Liefert die geaenderten Bereiche."""
        neu, anders = copy.deepcopy(ziel), []
        for b in CFG_BEREICHE:
            if b == "enabled" or not self.mit("cfg:" + b):
                continue
            if b in quelle:
                if neu.get(b) != quelle[b]:
                    neu[b] = copy.deepcopy(quelle[b])
                    anders.append(b)
            elif b in neu:
                neu.pop(b)
                anders.append(b)
        return neu, anders

    async def nach_speichern(self, dev: str) -> list[str]:
        """Nach dem Speichern von dev: Belegung an die Gruppe. Liefert die angepassten Remotes."""
        if self.pause:
            return []
        andere = self.gruppe(dev)
        if not andere:
            return []
        src = self.kd.entry(dev)
        geaendert = []
        self._laeuft = True
        try:
            for z in andere:
                e = self.kd.entry(z)
                neu, anders = self._cfg_uebertragen(src["config"], e["config"])
                en = bool(src.get("enabled")) if self.mit("cfg:enabled") else bool(e.get("enabled"))
                if en != bool(e.get("enabled")):
                    anders.append("enabled")
                if not anders:
                    continue
                try:
                    await self.kd.save(z, neu, en, f"Abgleich von {dev}")
                except Exception as err:  # noqa: BLE001 - z. B. zu gross: nur diese Remote auslassen
                    _LOGGER.warning("Abgleich %s -> %s: %s", dev, z, err)
                    self._log(dev, [z], f"Fehler: {err}")
                    continue
                geaendert.append(z)
                self._tasten_neu_laden(z)
            if geaendert:
                self._log(dev, geaendert, "Belegung")
        finally:
            self._laeuft = False
        return geaendert

    def _tasten_neu_laden(self, dev: str) -> None:
        from . import dev_slug
        hass = self.kd.hass
        dienst = f"{dev_slug(dev)}_tasten_neu_laden"
        if hass.services.has_service("esphome", dienst):
            hass.async_create_task(hass.services.async_call("esphome", dienst, {}, blocking=False))

    # ---- Einstellungen ----
    def _filtern(self, werte: dict) -> dict:
        return {k: v for k, v in werte.items() if self.mit("einst:" + k) and k not in VERALTET}

    async def einst_weiter(self, dev: str, werte: dict, quelle: str = "Konfigurator") -> list[str]:
        werte = self._filtern(werte)
        andere = [] if self.pause else self.gruppe(dev)
        if not werte or not andere:
            return []
        geaendert = []
        self._laeuft = True
        try:
            for z in andere:
                if await self.kd.einst_setzen(z, werte):
                    geaendert.append(z)
        finally:
            self._laeuft = False
        if geaendert:
            self._log(dev, geaendert, f"{quelle}: " + ", ".join(sorted(werte)))
        return geaendert

    def an_remote_geaendert(self, dev: str, alt: dict, neu: dict) -> None:
        """Remote meldet ihre Werte: was an ihr selbst umgestellt wurde, an die Gruppe weitergeben."""
        if not alt or not self.gruppe(dev):
            return
        soll = self.kd.entry(dev).get("einst") or {}
        werte = {k: v for k, v in neu.items() if k in alt and alt[k] != v and (k not in soll or soll[k][0] != v)}
        # Helligkeit nach Lichtsensor (licht.py) stellt disp_bri/kbd_bri je Raum - dort gleicht die gemeinsame Kurve
        # die Remotes ab (eine Handverstellung lernt sie fuer alle), der einzelne Wert bleibt je Remote
        licht = self.kd.hass.data.get("esphomeremote_konfig_licht")
        for ziel, k in (("disp", "disp_bri"), ("kbd", "kbd_bri")):
            if k in werte and licht is not None and licht._automatik(dev, ziel):
                werte.pop(k)
        if not werte:
            return
        if len(werte) > MAX_GEMELDET:
            _LOGGER.warning("Abgleich: %s meldet %d geaenderte Werte auf einmal - nicht weitergegeben (Neustart?)",
                            dev, len(werte))
            return
        self.kd.hass.async_create_task(self._an_remote(dev, werte))

    async def _an_remote(self, dev: str, werte: dict) -> None:
        if await self.einst_weiter(dev, werte, "an der Remote"):
            await self.kd.store.async_save(self.kd.data)

    # ---- Vollabgleich und Vergleich ----
    async def jetzt(self, von: str) -> dict:
        """Alles Gewaehlte von 'von' an alle anderen der Gruppe (beim Einschalten oder per Knopf)."""
        s = self.d
        ziele = [x for x in s["geraete"] if x != von and x in self.kd.devices]
        src = self.kd.entry(von)
        werte = self._filtern(self.kd.einst_werte(von)[0])
        out = {}
        self._laeuft = True
        try:
            for z in ziele:
                e = self.kd.entry(z)
                neu, anders = self._cfg_uebertragen(src["config"], e["config"])
                en = bool(src.get("enabled")) if self.mit("cfg:enabled") else bool(e.get("enabled"))
                fehler = None
                if anders or en != bool(e.get("enabled")):
                    try:
                        await self.kd.save(z, neu, en, f"Abgleich von {von}")
                        self._tasten_neu_laden(z)
                    except Exception as err:  # noqa: BLE001
                        fehler = str(err)
                n = await self.kd.einst_setzen(z, werte) if werte else 0
                out[z] = {"bereiche": anders, "einst": n, "fehler": fehler}
        finally:
            self._laeuft = False
        self._log(von, ziele, "Alles angeglichen")
        await self.kd.store.async_save(self.kd.data)
        return out

    def unterschiede(self) -> dict:
        """Je Bereich und je Einstellung: Werte, die sich zwischen den Remotes der Gruppe (bzw. allen) unterscheiden."""
        devs = [x for x in (self.d["geraete"] or self.kd.devices) if x in self.kd.devices]
        cfg = {}
        for b in CFG_BEREICHE:
            if b == "enabled":
                vals = {x: bool(self.kd.entry(x).get("enabled")) for x in devs}
            else:
                vals = {x: self.kd.entry(x)["config"].get(b) for x in devs}
            kenn = {x: json.dumps(v, sort_keys=True, ensure_ascii=False) for x, v in vals.items()}
            if len(set(kenn.values())) > 1:
                # Gleiche Staende zusammenfassen: [[remote, ...], ...]
                gr: dict[str, list] = {}
                for x, k in kenn.items():
                    gr.setdefault(k, []).append(x)
                cfg[b] = list(gr.values())
        einst = {}
        alle = {x: self.kd.einst_werte(x)[0] for x in devs}
        licht = self.kd.hass.data.get("esphomeremote_konfig_licht")
        for k in sorted({k for w in alle.values() for k in w}):
            ziel = {"disp_bri": "disp", "kbd_bri": "kbd"}.get(k)
            if ziel and licht is not None and all(licht._automatik(x, ziel) for x in devs):
                continue   # stellt der Lichtsensor je Raum (s. an_remote_geaendert)
            if k in VERALTET:
                continue
            vals = {x: alle[x].get(k) for x in devs}
            if len({json.dumps(v) for v in vals.values()}) > 1:
                einst[k] = vals
        return {"geraete": devs, "cfg": cfg, "einst": einst}
