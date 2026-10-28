"""Bekannte Fehler fremder Software beobachten (2026-10-28, Konfigurator > Dockingstation > Bekannte Fehler).

Fuer Fehler, die wir nicht selbst beheben koennen, sondern nur umgehen: HA sieht einmal am Tag auf GitHub nach,
  - ob der Fehlerbericht noch offen ist (geschlossen = Espressif hat ihn behoben oder abgelehnt), und
  - ob die betroffene Bibliothek in den neuesten Releases eine andere Fassung hat als die, mit der wir den Fehler
    nachgewiesen haben (nur eine neue Fassung kann den Fehler beheben - ein Update ohne sie bringt nichts).
Trifft eines zu, lohnt es sich, die Umgehung testweise abzuschalten. Dann meldet HA das einmal als Benachrichtigung.
Alles ohne Anmeldung (GitHub erlaubt 60 Anfragen je Stunde, hier ~4 je Pruefung).
"""

from __future__ import annotations

import logging
import time
from datetime import timedelta

import voluptuous as vol

from homeassistant.components import persistent_notification, websocket_api
from homeassistant.core import HomeAssistant
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.event import async_call_later, async_track_time_interval
from homeassistant.helpers.storage import Store

_LOGGER = logging.getLogger(__name__)
TAKT = timedelta(hours=24)

FEHLER = [{
    "id": "c3_ble_wiederverbinden",
    "titel": "Dock stürzt ab, wenn sich der Fernseher per Bluetooth wieder verbindet",
    "text": "Der Bluetooth-Controller des ESP32-C3 (fertige Bibliothek von Espressif) stürzt ab, wenn er nach Geräten "
            "sucht, während sich ein Gerät neu verbindet. Nachgewiesen am 27.10.2026 (Flugschreiber: alle Abstürze "
            "21–86 s nach einer Trennung des Sony-TV) und erneut am 28.10.2026.",
    "umgehung": "Suchpause: Während sich TV oder Streamer wieder verbinden, sucht das Dock nicht nach Remotes "
                "(höchstens 2 min, danach 5 s Nachlauf).",
    "issue": "espressif/esp-idf/issues/18063",
    "bibliothek": {"name": "ESP32-C3-Bluetooth-Controller", "repo": "espressif/esp-idf",
                   "pfad": "components/bt/controller/lib_esp32c3_family",
                   "stand": "0a08c4b32f3666003080b662a1a61794da24ff0f", "idf": "5.5.5"},
}]


class Beobachter:
    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self.store = Store(hass, 1, "esphomeremote_konfig_fehler")
        self.erg: dict = {}   # id -> Ergebnis der letzten Pruefung

    async def starten(self) -> None:
        self.erg = await self.store.async_load() or {}
        async_call_later(self.hass, 300, self._takt)
        async_track_time_interval(self.hass, self._takt, TAKT)

    async def _takt(self, _jetzt=None) -> None:
        await self.pruefen()

    async def _json(self, url: str):
        sess = async_get_clientsession(self.hass)
        async with sess.get(url, headers={"Accept": "application/vnd.github+json"}, timeout=20) as r:
            if r.status != 200:
                raise ValueError(f"GitHub antwortet {r.status}")
            return await r.json()

    async def _pruefen_einer(self, f: dict) -> dict:
        alt = self.erg.get(f["id"], {})
        e: dict = {"geprueft": time.strftime("%Y-%m-%d %H:%M")}
        try:
            i = await self._json(f"https://api.github.com/repos/{f['issue']}")
            e["issue"] = {"offen": i.get("state") == "open", "grund": i.get("state_reason"),
                          "kommentare": i.get("comments", 0), "geaendert": (i.get("updated_at") or "")[:10],
                          "geschlossen": (i.get("closed_at") or "")[:10]}
            b = f["bibliothek"]
            rel = await self._json(f"https://api.github.com/repos/{b['repo']}/releases?per_page=30")
            tags = [r["tag_name"] for r in rel if not r.get("prerelease") and not r.get("draft")]
            # neuestes Release ueberhaupt und neuestes der eigenen Reihe (5.5.x) - die nimmt ESPHome zuerst
            reihe = "v" + ".".join(b["idf"].split(".")[:2]) + "."
            neueste = (await self._json(f"https://api.github.com/repos/{b['repo']}/releases/latest")).get("tag_name")
            pruef = list(dict.fromkeys([t for t in (neueste, *tags[:1]) if t] + [t for t in tags if t.startswith(reihe)][:1]))
            stand = []
            for t in pruef:
                c = await self._json(f"https://api.github.com/repos/{b['repo']}/contents/{b['pfad']}?ref={t}")
                stand.append({"tag": t, "sha": c.get("sha"), "neu": c.get("sha") != b["stand"]})
            e["bibliothek"] = stand
            e["lohnt"] = (not e["issue"]["offen"]) or any(s["neu"] for s in stand)
        except Exception as err:   # noqa: BLE001 - GitHub nicht erreichbar: alten Stand behalten
            _LOGGER.debug("Bekannte Fehler %s: %s", f["id"], err)
            return {**alt, "fehler": str(err), "versuch": e["geprueft"]}
        if e["lohnt"] and not alt.get("lohnt"):
            gruende = []
            if not e["issue"]["offen"]:
                gruende.append(f"Der Fehlerbericht ist geschlossen ({e['issue']['grund'] or 'erledigt'}).")
            gruende += [f"{s['tag']} bringt eine neue Fassung des {f['bibliothek']['name']}." for s in e["bibliothek"] if s["neu"]]
            persistent_notification.async_create(
                self.hass, f"**{f['titel']}**\n\n" + " ".join(gruende)
                + "\n\nEs lohnt sich, die Umgehung testweise abzuschalten (Konfigurator › Dockingstation › Bekannte Fehler).",
                title="ESPHomeRemote: Fehler vielleicht behoben", notification_id=f"esphomeremote_fehler_{f['id']}")
        return e

    async def pruefen(self) -> None:
        for f in FEHLER:
            self.erg[f["id"]] = await self._pruefen_einer(f)
        await self.store.async_save(self.erg)

    def stand(self) -> list[dict]:
        return [{**f, "erg": self.erg.get(f["id"])} for f in FEHLER]


async def einrichten(hass: HomeAssistant) -> None:
    b = Beobachter(hass)
    await b.starten()

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/bekannte_fehler",
                                      vol.Optional("pruefen", default=False): bool})
    @websocket_api.async_response
    async def ws_fehler(hass, connection, msg):
        if msg["pruefen"]:
            await b.pruefen()
        connection.send_result(msg["id"], {"fehler": b.stand()})

    websocket_api.async_register_command(hass, ws_fehler)
