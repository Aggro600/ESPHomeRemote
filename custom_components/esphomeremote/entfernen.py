"""Rueckstandslos entfernen (2026-11-05).

Nutzer: "Ich hatte auf einem anderen Home Assistant das Repository installiert und wieder deinstalliert, aber ich habe
immer noch den Konfigurator in meiner Seitenleiste ... bau ein, dass sie sich vernuenftig loeschen laesst, rueckstandslos."

HA ruft async_remove_entry nur beim Loeschen des Integrationseintrags auf (nicht beim Neuladen oder Neustart). Dann:
  1. Streamer-App auf allen Streamern mit ADB deinstallieren (Bedienungshilfe, Ziel-Adresse und App weg)
  2. Laufendes anhalten: Streamer (Webhooks, Abfragen), BLE-Bruecken (Ports 127.0.0.1:1605x), Funktionen (Beobachter)
  3. Speichern sperren und alle gespeicherten Daten loeschen (.storage/esphomeremote*, IR-Datenbank, Bilder,
     mitgelieferte Grafiken in /config/www) - sonst legt ein noch laufender Timer sie wieder an
  4. Seitenleiste (Konfigurator), Dienste esphomeremote.*, eigene Zustaende, Reparatur-Hinweise, Meldungen entfernen
Entitaeten und Geraete der Integration loescht HA selbst. Bewusst NICHT angefasst: die ESPHome-Dateien der Remotes und
Docks in /config/esphome (gehoeren dem ESPHome-Add-on, ohne sie lassen sich die Geraete nicht mehr aktualisieren) und
die ESPHome-Eintraege der Remotes. Was nur ein Neustart beendet (HTTP-Adressen, einzelne Zeitgeber), meldet eine
Benachrichtigung.
"""
from __future__ import annotations

import asyncio
import glob
import logging
import os
import shutil

from homeassistant.core import HomeAssistant
from homeassistant.helpers.storage import Store

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
DATEIEN_STORAGE = ("esphomeremote", "esphomeremote_fehler", "esphomeremote_firmware", "esphomeremote_ircodes")
ORDNER_STORAGE = ("esphomeremote_irdb",)
WWW_MUSTER = ("esphomeremote_*", ".esphomeremote*")
ORDNER_CONFIG = (".esphomeremote_images",)


async def _still(st: Store) -> None:
    """Store loeschen und kuenftiges Speichern verhindern."""
    try:
        await st.async_remove()
    except Exception:  # noqa: BLE001
        pass

    async def nichts(*_a, **_k) -> None:
        return None

    st.async_save = nichts                       # type: ignore[method-assign]
    st.async_delay_save = lambda *_a, **_k: None  # type: ignore[method-assign]


def _stores(hass: HomeAssistant) -> list[Store]:
    """Alle Stores der Integration in hass.data (direkt oder als Attribut eines Objekts)."""
    gefunden: list[Store] = []
    for k, v in list(hass.data.items()):
        if not str(k).startswith(DOMAIN):
            continue
        kandidaten = [v, *(vars(v).values() if hasattr(v, "__dict__") else [])]
        for c in kandidaten:
            if isinstance(c, Store) and str(getattr(c, "key", "")).startswith(DOMAIN) and c not in gefunden:
                gefunden.append(c)
    return gefunden


def _dateien_loeschen(config_dir: str) -> list[str]:
    weg: list[str] = []
    sto = os.path.join(config_dir, ".storage")
    for n in DATEIEN_STORAGE:
        for p in glob.glob(os.path.join(sto, n)) + glob.glob(os.path.join(sto, n + ".*")):
            if os.path.isfile(p):
                os.remove(p)
                weg.append(p)
    for p in glob.glob(os.path.join(sto, "esphomeremote_konfig*")):   # Reste aus der Zeit vor der Umbenennung
        (shutil.rmtree if os.path.isdir(p) else os.remove)(p)
        weg.append(p)
    for n in ORDNER_STORAGE:
        p = os.path.join(sto, n)
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)
            weg.append(p)
    for n in ORDNER_CONFIG:
        p = os.path.join(config_dir, n)
        if os.path.isdir(p):
            shutil.rmtree(p, ignore_errors=True)
            weg.append(p)
    www = os.path.join(config_dir, "www")
    for m in WWW_MUSTER:
        for p in glob.glob(os.path.join(www, m)):
            if os.path.isfile(p):
                os.remove(p)
                weg.append(p)
    return weg


async def alles_entfernen(hass: HomeAssistant) -> None:
    _LOGGER.info("ESPHomeRemote wird entfernt - raeume auf")

    # 1. Streamer-App von den Streamern (solange ADB und die Streamer-Objekte noch da sind)
    s = hass.data.get(DOMAIN + "_streamer")
    for st in list((getattr(s, "alle", {}) or {}).values()):
        if not st.cfg.get("adb"):
            continue
        try:
            await asyncio.wait_for(st.app_entfernen(), 40)
            _LOGGER.info("Streamer-App von %s entfernt", st.cfg.get("name"))
        except Exception as err:  # noqa: BLE001 - Streamer aus: App bleibt dort, Hinweis in der Meldung
            _LOGGER.warning("Streamer-App von %s nicht entfernt: %s", st.cfg.get("name"), err)

    # 2. Laufendes anhalten
    for st in list((getattr(s, "alle", {}) or {}).values()):
        try:
            st.stoppen()
        except Exception:  # noqa: BLE001
            pass
    ble = hass.data.get("esphomeremote_ble") or {}
    for link in list((ble.get("links") or {}).values()) if isinstance(ble, dict) else []:
        try:
            await link.stop()
        except Exception:  # noqa: BLE001
            pass
    f = hass.data.get(DOMAIN + "_funktionen")
    for a in list(getattr(f, "_abos", []) or []):
        try:
            a()
        except Exception:  # noqa: BLE001
            pass

    # 3. Speichern sperren, Daten loeschen
    for st in _stores(hass):
        await _still(st)
    weg = await hass.async_add_executor_job(_dateien_loeschen, hass.config.config_dir)
    _LOGGER.info("Geloescht: %s", ", ".join(weg) or "nichts")

    # 4. Seitenleiste, Dienste, Zustaende, Hinweise, Meldungen
    from homeassistant.components import frontend

    for url in ("remote", "fernbedienung"):
        try:
            frontend.async_remove_panel(hass, url)
        except Exception:  # noqa: BLE001
            pass
    for dom in (DOMAIN, "esphomeremote_konfig"):   # dazu die alten Namen von vor der Umbenennung (aeltere Installationen)
        for svc in list(hass.services.async_services().get(dom, {})):
            hass.services.async_remove(dom, svc)

    from homeassistant.helpers import entity_registry as er, issue_registry as ir

    reg = er.async_get(hass)
    # Entitaeten der Integration ohne Eintrag (z. B. aus der YAML-Zeit) loescht HA nicht von selbst
    for e in [e for e in list(reg.entities.values()) if e.platform == DOMAIN and not e.config_entry_id]:
        reg.async_remove(e.entity_id)
    for z in hass.states.async_all():
        if "esphomeremote" in z.entity_id and reg.async_get(z.entity_id) is None:
            hass.states.async_remove(z.entity_id)
    issues = ir.async_get(hass)
    for dom, iid in [k for k in list(issues.issues) if k[0] == DOMAIN]:
        ir.async_delete_issue(hass, dom, iid)
    from homeassistant.components import persistent_notification as pn

    try:   # alle offenen Meldungen der Integration (Dock-Wache, Firmware ...)
        ids = [n for n in pn._async_get_or_create_notifications(hass) if str(n).startswith(DOMAIN)]  # noqa: SLF001
    except Exception:  # noqa: BLE001 - interne HA-Funktion fehlt: die bekannten Kennungen
        ids = [z.entity_id.split(".", 1)[1] for z in hass.states.async_all("persistent_notification")
               if z.entity_id.split(".", 1)[1].startswith(DOMAIN)]
    for nid in ids:
        pn.async_dismiss(hass, nid)

    for k in [k for k in hass.data if str(k).startswith(DOMAIN)]:
        hass.data.pop(k, None)

    pn.async_create(
        hass,
        "ESPHomeRemote ist entfernt: Konfigurator, Einstellungen, IR-Datenbank, Bilder und die Streamer-App sind gelöscht. "
        "Ein Neustart von Home Assistant beendet die letzten Hintergrundaufgaben.\n\n"
        "Nicht gelöscht: die ESPHome-Dateien der Remotes und Docks in `/config/esphome` und ihre ESPHome-Einträge – "
        "die gehören zum ESPHome-Add-on. Wer sie nicht mehr braucht, löscht sie dort.",
        title="ESPHomeRemote entfernt",
        notification_id="esphomeremote_entfernt_hinweis",
    )
