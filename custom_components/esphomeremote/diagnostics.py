"""Diagnose-Datei fuer Fehlerberichte (2026-11-12).

Einstellungen -> Geraete & Dienste -> ESPHomeRemote -> "Diagnose herunterladen". Ohne sie wird jeder Fehlerbericht zum
Frage-Antwort-Spiel (welche Version, welches Dock, haengt die Remote am WLAN ...). Die Datei haengt der Nutzer an ein
GitHub-Issue - darum steht NICHTS Persoenliches drin:
  - Geraete heissen remote_1, remote_2, dock_1 ... (gleiche Nummer = gleiches Geraet in der ganzen Datei)
  - keine IP-, MAC- oder Bluetooth-Adressen, keine Schluessel, keine Entitaets-IDs, keine Tastenbelegung
    (nur Zahlen: wie viele Seiten, Aktivitaeten, Tasten)
  - keine Namen aus dem Menue, keine Mediendaten
Enthalten ist, was fuer die Fehlersuche zaehlt: Versionen (Integration, Firmware je Geraet, ESPHome-Add-on, HA),
Umgebung (Supervisor ja/nein, ESPHome-Ordner), Verbindungsweg je Remote, offene Einstellungen, Firmware-Auftraege
und ihre letzten Fehlerzeilen, Reparatur-Hinweise, Groesse der gespeicherten Daten.
"""
from __future__ import annotations

import json
import os
import re
import time
from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import __version__ as HA_VERSION
from homeassistant.core import HomeAssistant
from homeassistant.helpers import issue_registry as ir

DOMAIN = "esphomeremote"

# Geraete-Felder aus firmware.liste(), die unveraendert uebernommen werden (alles andere faellt weg)
_GERAET_FELDER = ("art", "online", "version", "geflasht", "update", "in_ha", "gesperrt", "vorgemerkt", "fw", "veraltet",
                  "ha_da", "wlan", "alt", "bruecke_ota")
_IP = re.compile(r"\b\d{1,3}(?:\.\d{1,3}){3}\b")
_MAC = re.compile(r"\b(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\b")


class _Anonym:
    """Geraetenamen -> remote_1/dock_1; in Texten auch IPs/MACs/Namen tilgen."""

    def __init__(self, remotes: list[str]) -> None:
        self.namen: dict[str, str] = {}
        self.zaehler = {"remote": 0, "dock": 0, "sonst": 0}
        for r in remotes:
            self.name(r, "remote")

    def name(self, n: str | None, art: str = "sonst") -> str:
        if not n:
            return ""
        if n not in self.namen:
            art = art if art in self.zaehler else "sonst"
            self.zaehler[art] += 1
            self.namen[n] = f"{art}_{self.zaehler[art]}"
        return self.namen[n]

    def text(self, t: Any) -> str:
        s = str(t)
        for n in sorted(self.namen, key=len, reverse=True):
            for form in (n, n.replace("-", "_"), n.replace("_", "-")):
                s = s.replace(form, self.namen[n])
        s = _IP.sub("<ip>", s)
        s = _MAC.sub("<mac>", s)
        return s


def _zaehlen(cfg: dict | None) -> dict[str, int]:
    """Nur Mengen aus der Belegung - keine Inhalte."""
    cfg = cfg or {}
    menu = cfg.get("menu") or {}
    seiten = menu.get("pages") or []
    tasten = 0
    for a in cfg.get("activities") or []:
        tasten += len((a.get("keys") or {}) if isinstance(a.get("keys"), dict) else [])
    return {"aktivitaeten": len(cfg.get("activities") or []), "menue_seiten": len(seiten),
            "menue_eintraege": sum(len(p.get("items") or []) for p in seiten),
            "tasten_belegt": tasten, "bytes": len(json.dumps(cfg, separators=(",", ":"), ensure_ascii=False))}


async def async_get_config_entry_diagnostics(hass: HomeAssistant, entry: ConfigEntry) -> dict[str, Any]:
    kd = hass.data.get(DOMAIN)
    remotes = list(kd.devices) if kd else list(entry.options.get("devices", entry.data.get("devices", [])))
    an = _Anonym(remotes)

    from homeassistant.loader import async_get_integration
    integ = await async_get_integration(hass, DOMAIN)
    esphome_dir = hass.config.path("esphome")
    out: dict[str, Any] = {
        "erstellt": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "umgebung": {
            "integration": str(integ.version or ""),
            "home_assistant": HA_VERSION,
            "supervisor": bool(os.environ.get("SUPERVISOR_TOKEN")),
            "esphome_ordner": await hass.async_add_executor_job(os.path.isdir, esphome_dir),
            "sprache": str(getattr(hass.config, "language", "") or ""),
            "remotes_konfiguriert": len(remotes),
        },
    }

    # ESPHome-Add-on (nur mit Supervisor)
    try:
        from .firmware import ADDON
        from homeassistant.helpers.aiohttp_client import async_get_clientsession
        tok = os.environ.get("SUPERVISOR_TOKEN")
        if tok:
            s = async_get_clientsession(hass)
            async with s.get(f"http://supervisor/addons/{ADDON}/info", headers={"Authorization": "Bearer " + tok}) as r:
                d = (await r.json()).get("data") or {}
            out["umgebung"]["esphome_addon"] = {"version": d.get("version"), "state": d.get("state")}
    except Exception as err:  # noqa: BLE001
        out["umgebung"]["esphome_addon"] = {"fehler": an.text(err)}

    # Geraete (Firmware-Sicht) - ohne Adressen und Dateinamen
    try:
        from .firmware import liste
        fw = await liste(hass)
        geraete = []
        for g in fw.get("geraete", []):
            art = g.get("art") or "sonst"
            x: dict[str, Any] = {"geraet": an.name(g.get("name") or g.get("datei"), art)}
            for k in _GERAET_FELDER:
                if k in g:
                    x[k] = g[k]
            if g.get("bruecke") is not None:
                x["bruecke"] = True
            if art == "dock":
                x["ir_sender"] = bool(g.get("sender"))
            geraete.append(x)
        out["geraete"] = geraete
        out["firmware"] = {
            "version_integration": fw.get("version"), "addon_fehler": an.text(fw.get("fehler") or ""),
            "entwickler": bool(fw.get("entwickler")), "lokale_vorlagen": bool(fw.get("lokal")),
            "altlasten": len(fw.get("altlasten") or []), "usb_ports": len(fw.get("ports") or []),
            "auftraege_offen": [{"schritt": an.text(j.get("schritt") or ""), "fehler": an.text(j.get("fehler") or "")}
                                for j in fw.get("jobs") or []],
            "auftraege_fertig": [{"titel": an.text(j.get("titel") or ""), "art": j.get("art"), "status": j.get("status"),
                                  "dauer_s": int(j["t_ende"] - j["t_start"]) if j.get("t_ende") and j.get("t_start") else None,
                                  "fehler": an.text(j.get("fehler") or "")}
                                 for j in (fw.get("fertige") or [])[:5]],
        }
    except Exception as err:  # noqa: BLE001
        out["firmware"] = {"fehler": an.text(err)}

    # Remotes aus Sicht der Integration
    if kd is not None:
        rem = {}
        for dev in kd.devices:
            e = kd.entry(dev) or {}
            try:
                soll, offen = kd.einst_werte(dev)
            except Exception:  # noqa: BLE001
                soll, offen = {}, []
            r: dict[str, Any] = {"enabled": bool(e.get("enabled")), "belegung": _zaehlen(e.get("config")),
                                 "einstellungen_soll": len(soll or {}), "einstellungen_offen": len(offen),
                                 "gespeichert_vor_s": int(time.time() - e["saved_at"]) if e.get("saved_at") else None}
            for k in ("ha_link_modus", "bt_weg", "ps_low_pct", "dock_modus", "sprache"):
                if k in (soll or {}):
                    r[k] = soll[k]
            # Akku/Verbindung aus den Entitaeten (nur Werte)
            pre = f"remote_wz_{dev[10:]}" if dev.startswith("remote-wz-") else dev.replace("-", "_")
            for feld, eid in (("akku", f"sensor.{pre}_akku"), ("ha_verbunden", f"binary_sensor.{pre}_ha_verbunden"),
                              ("wlan_aktiv", f"binary_sensor.{pre}_wlan_aktiv")):
                st = hass.states.get(eid) or hass.states.get(f"{eid.split('.')[0]}.esphomeremote_{eid.split('.', 1)[1]}")
                if st is not None:
                    r[feld] = st.state
                    r[feld + "_vor_s"] = int(time.time() - st.last_reported.timestamp())
            w = hass.data.get(DOMAIN + "_warnungen")
            if w is not None and pre in getattr(w, "flash_rate", {}):
                r["flash_schreibzugriffe_je_tag"] = round(w.flash_rate[pre])
            st = hass.states.get(f"sensor.{pre}_flash_schreibzugriffe") or hass.states.get(f"sensor.esphomeremote_{pre}_flash_schreibzugriffe")
            if st is not None:
                r["flash_schreibzugriffe_seit_start"] = st.state
            rem[an.name(dev, "remote")] = r
        out["remotes"] = rem
        sy = getattr(kd, "sync", None)
        if sy is not None:
            out["abgleich"] = {"an": bool(sy.d.get("an")), "geraete": len(sy.d.get("geraete") or []),
                               "aus": list(sy.d.get("aus") or []), "pause": bool(getattr(sy, "pause", False))}
        out["speicher"] = {k: len(json.dumps(v, ensure_ascii=False)) for k, v in kd.data.items()
                           if k != "devices" and v is not None}

    # Reparatur-Hinweise dieser Integration
    reg = ir.async_get(hass)
    out["reparaturen"] = [{"id": re.sub(r"_[0-9a-f]{32}$", "", an.text(k[1])), "schwere": str(i.severity.value if i.severity else ""),
                           "ignoriert": bool(i.dismissed_version)}
                          for k, i in reg.issues.items() if k[0] == DOMAIN]

    # Alles, was durchgerutscht sein koennte, noch einmal durch den Filter
    return json.loads(an.text(json.dumps(out, ensure_ascii=False, default=str)))
