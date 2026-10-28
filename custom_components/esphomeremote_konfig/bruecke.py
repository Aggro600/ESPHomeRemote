"""Bluetooth-Bruecke (ble_link.py) aus den Remotes ableiten (2026-10-17).

Vorher stand je Remote ein Eintrag von Hand in packages/esphome_ble_link.yaml (Name, Port, WLAN-Adresse). Jetzt:
  - Remotes und Bluetooth-Namen aus esphome/tools/remotes.json ("Remote <Name>" = device_name der BLE-Tastatur)
  - WLAN-Adresse = feste IP aus esphome/secrets.yaml (ip_remote_wz_<kennung>)
  - Port je Remote fest vergeben und gespeichert (data["bruecke"]), ab 16053; OTA-Port 3232 hat hoechstens eine Remote
  - HAs ESPHome-Eintrag der Remote zeigt auf 127.0.0.1:<port> (wird bei Bedarf umgestellt)
Neu abgeglichen beim Start und nach Einrichten/Entfernen einer Remote (von Hand: WS esphomeremote_konfig/bruecke,
dort auch "ota" = welche Remote Updates ueber Bluetooth bekommt)
"""
from __future__ import annotations

import importlib
import json
import logging
import re

from homeassistant.core import HomeAssistant

from .firmware import ESPHOME_DIR

_LOGGER = logging.getLogger(__name__)
PORT_START = 16053
OTA_PORT = 3232


def _remotes() -> list[dict]:
    """[{slug, name, ip}] aus remotes.json und secrets.yaml."""
    try:
        rs = json.loads((ESPHOME_DIR / "tools" / "remotes.json").read_text(encoding="utf-8"))
        sec = (ESPHOME_DIR / "secrets.yaml").read_text(encoding="utf-8")
    except (OSError, ValueError) as err:
        _LOGGER.warning("Bruecke: Remote-Liste nicht lesbar: %s", err)
        return []
    out = []
    for r in rs:
        m = re.search(rf"^ip_remote_(?:wz_)?{re.escape(r['slug'])}:\s*([\d.]+)", sec, re.M)
        out.append({"slug": r["slug"], "name": r["name"], "ip": m.group(1) if m else None})
    return out


class Bruecke:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.mod = None
        self.stand: list[dict] = []

    async def _modul(self):
        if self.mod is None:
            try:
                self.mod = await self.hass.async_add_import_executor_job(
                    importlib.import_module, __package__ + ".ble_link")
            except ImportError:
                self.mod = False
        return self.mod or None

    async def abgleichen(self) -> list[dict]:
        """Links setzen und ESPHome-Eintraege der Remotes auf die Bruecke zeigen lassen."""
        rs = await self.hass.async_add_executor_job(_remotes)
        geaendert = False
        ports: dict = self.kd.data.setdefault("bruecke", {})
        for r in rs:
            if r["slug"] not in ports:
                belegt = {p["port"] for p in ports.values()}
                port = next(p for p in range(PORT_START, PORT_START + 200) if p not in belegt)
                ota = not any(p.get("ota") for p in ports.values())
                ports[r["slug"]] = {"port": port, "ota": ota}
                geaendert = True
        for slug in [s for s in ports if s not in {r["slug"] for r in rs}]:
            del ports[slug]
            geaendert = True
        if geaendert:
            await self.kd.store.async_save(self.kd.data)
        liste = []
        for r in rs:
            p = ports[r["slug"]]
            rc = {"name": f"Remote {r['name']}", "port": p["port"]}
            if r["ip"]:
                rc["wifi_host"] = r["ip"]
            if p.get("ota"):
                rc["ota_port"] = OTA_PORT
            liste.append(rc)
            r.update(port=p["port"], ota=bool(p.get("ota")))
        mod = await self._modul()
        if mod is None:
            _LOGGER.warning("Bruecke: Bluetooth-Modul (ble_link) nicht ladbar")
            self.stand = rs
            return rs
        await mod.async_links_setzen(self.hass, "konfig", liste)
        for r in rs:
            await self._eintrag(f"remote-wz-{r['slug']}", r["port"])
        self.stand = rs
        return rs

    async def _eintrag(self, dev: str, port: int) -> None:
        for e in self.hass.config_entries.async_entries("esphome"):
            if e.data.get("device_name") != dev:
                continue
            if e.data.get("host") == "127.0.0.1" and e.data.get("port") == port:
                return
            _LOGGER.info("Bruecke: ESPHome-Eintrag %s -> 127.0.0.1:%s (vorher %s:%s)", dev, port, e.data.get("host"), e.data.get("port"))
            self.hass.config_entries.async_update_entry(e, data={**e.data, "host": "127.0.0.1", "port": port})
            self.hass.config_entries.async_schedule_reload(e.entry_id)
            return

    def info(self) -> dict:
        """{remote-wz-<slug>: {ip, port, ota, aktiv}} fuer den Firmware-Bereich."""
        links = self.mod.async_links(self.hass) if self.mod else {}
        return {f"remote-wz-{r['slug']}": {"ip": r["ip"], "port": r.get("port"), "ota": r.get("ota"),
                                            "aktiv": f"Remote {r['name']}" in links} for r in self.stand}
