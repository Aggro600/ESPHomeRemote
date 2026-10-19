"""Klingel- und Kamerabilder fuer die Fernbedienungen (2026-10-19, vorher Automationen + Shell-Skripte).

- Klingel (Konfigurator > Klingel & Kameras, kd.data["klingel"]): bis zu zwei Klingeln (so viele Plaetze hat die
  Firmware), je Name, Ausloeser (Entitaet, die beim Klingeln auf "on" geht oder - bei button/event/input_button -
  sich aendert), Kamera und Wartezeit. Beim Klingeln: Kamerabild holen, auf 216x104 zuschneiden, ablegen und den
  Remotes sagen, dass sie es laden koennen (Dienst doorbell_image {door: 1|2}).
- Kamerabild und Live-Bild: die Remote meldet esphome.remote_cam_snapshot bzw. esphome.remote_live_start mit der
  gewuenschten Kamera. Erlaubt ist jede Kamera, die im Konfigurator bei einer Remote eingetragen ist (cfg.kameras).
Bilder liegen in /config/www (= /local/...), dort holt die Remote sie per HTTP.
"""

from __future__ import annotations

import asyncio
import io
import logging
import os
import re

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"

GROESSE_KLINGEL = (216, 104)
GROESSE_KAMERA = (216, 122)
DATEI_KLINGEL = ("esphomeremote_doorbell_haus.jpg", "esphomeremote_doorbell_wohnung.jpg")   # Platz 1, 2 (Firmware)
DATEI_KAMERA = "esphomeremote_cam.jpg"
DATEI_LIVE = "esphomeremote_live.jpg"
LIVE_DAUER = 45   # Sekunden, wie lange die Remote das Live-Bild nachlaedt
IMPULS = ("button", "input_button", "event")   # Ausloeser ohne "on": jede Aenderung zaehlt


def validate_klingel(liste) -> list[dict]:
    if not isinstance(liste, list):
        raise vol.Invalid("Klingel: Liste erwartet")
    out = []
    for k in liste[:2]:
        a, c = str(k.get("ausloeser") or "").strip(), str(k.get("kamera") or "").strip()
        if a and not re.fullmatch(r"[a-z_]+\.[a-z0-9_]+", a):
            raise vol.Invalid(f"Klingel: Auslöser '{a}' ungültig")
        if c and not re.fullmatch(r"camera\.[a-z0-9_]+", c):
            raise vol.Invalid(f"Klingel: '{c}' ist keine Kamera")
        out.append({"name": str(k.get("name") or f"Klingel {len(out) + 1}")[:24], "ausloeser": a, "kamera": c,
                    "warten": max(0, min(30, int(k.get("warten") or 0)))})
    return out


def _zuschneiden(daten: bytes, groesse: tuple[int, int], ziel: str) -> None:
    """JPEG randlos auf die Groesse zuschneiden und atomar schreiben (Executor)."""
    from PIL import Image
    im = Image.open(io.BytesIO(daten))
    im.draft("RGB", (groesse[0] * 2, groesse[1] * 2))
    im = im.convert("RGB")
    ratio = groesse[0] / groesse[1]
    if im.width / im.height > ratio:
        nh, nw = groesse[1], round(groesse[1] * im.width / im.height)
    else:
        nw, nh = groesse[0], round(groesse[0] * im.height / im.width)
    im = im.resize((nw, nh), Image.LANCZOS)
    l, t = (nw - groesse[0]) // 2, (nh - groesse[1]) // 2
    tmp = ziel + ".tmp"
    im.crop((l, t, l + groesse[0], t + groesse[1])).save(tmp, "JPEG", quality=80)
    os.replace(tmp, ziel)


class Bilder:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.www = hass.config.path("www")
        self._abo = None
        self._live: asyncio.Task | None = None

    @property
    def klingeln(self) -> list[dict]:
        return self.kd.data.get("klingel") or []

    async def bild(self, kamera: str, groesse: tuple[int, int], datei: str) -> bool:
        from homeassistant.components.camera import async_get_image
        try:
            img = await async_get_image(self.hass, kamera, timeout=10)
            os.makedirs(self.www, exist_ok=True)
            await self.hass.async_add_executor_job(_zuschneiden, img.content, groesse, os.path.join(self.www, datei))
            return True
        except Exception as err:  # noqa: BLE001
            _LOGGER.warning("Kamerabild %s: %s", kamera, err)
            return False

    # ---------------------------------------------------------------- Klingel
    @callback
    def starten(self) -> None:
        if self._abo:
            self._abo()
            self._abo = None
        quellen = [k["ausloeser"] for k in self.klingeln if k["ausloeser"]]
        if quellen:
            self._abo = async_track_state_change_event(self.hass, quellen, self._geklingelt)

    @callback
    def _geklingelt(self, ev: Event) -> None:
        alt, neu = ev.data.get("old_state"), ev.data.get("new_state")
        if neu is None or alt is None or neu.state in ("unavailable", "unknown"):
            return
        dom = ev.data["entity_id"].split(".")[0]
        if dom in IMPULS:
            if neu.state == alt.state:
                return
        elif neu.state != "on" or alt.state == "on":
            return
        for platz, k in enumerate(self.klingeln):
            if k["ausloeser"] == ev.data["entity_id"]:
                self.hass.async_create_task(self.klingel(platz))

    async def klingel(self, platz: int) -> None:
        k = self.klingeln[platz]
        _LOGGER.info("Klingel %s", k["name"])
        if k["warten"]:
            await asyncio.sleep(k["warten"])
        if k["kamera"] and await self.bild(k["kamera"], GROESSE_KLINGEL, DATEI_KLINGEL[platz]):
            from .einrichten import an_alle
            await an_alle(self.hass, self.kd.devices, "doorbell_image", {"door": platz + 1})

    # ---------------------------------------------------------------- Kamera- und Live-Bild
    def erlaubt(self, kamera: str) -> bool:
        return any(k.get("e") == kamera for d in self.kd.data["devices"].values() for k in d["config"].get("kameras") or [])

    async def kamerabild(self, kamera: str, geraet: str) -> None:
        if not self.erlaubt(kamera):
            _LOGGER.warning("Kamerabild: %s ist bei keiner Remote eingetragen", kamera)
            return
        if await self.bild(kamera, GROESSE_KAMERA, DATEI_KAMERA):
            slug = geraet.replace("-", "_")
            if self.hass.services.has_service("esphome", f"{slug}_cam_image"):
                await self.hass.services.async_call("esphome", f"{slug}_cam_image", {}, blocking=False)

    async def live(self, kamera: str) -> None:
        if not self.erlaubt(kamera):
            _LOGGER.warning("Live-Bild: %s ist bei keiner Remote eingetragen", kamera)
            return
        if self._live and not self._live.done():
            self._live.cancel()
        self._live = self.hass.async_create_background_task(self._live_lauf(kamera), "esphomeremote_live")

    async def _live_lauf(self, kamera: str) -> None:
        ende = self.hass.loop.time() + LIVE_DAUER
        n = 0
        while self.hass.loop.time() < ende:
            t0 = self.hass.loop.time()
            if await self.bild(kamera, GROESSE_KAMERA, DATEI_LIVE):
                n += 1
            else:
                await asyncio.sleep(1)
            await asyncio.sleep(max(0.0, 0.15 - (self.hass.loop.time() - t0)))
        _LOGGER.debug("Live-Bild %s: %d Bilder", kamera, n)


def einrichten(hass: HomeAssistant, kd) -> Bilder:
    b = Bilder(hass, kd)
    hass.data[DOMAIN + "_bilder"] = b
    b.starten()

    @callback
    def snapshot(ev: Event) -> None:
        hass.async_create_task(b.kamerabild(str(ev.data.get("kamera") or ""), str(ev.data.get("geraet") or "")))

    @callback
    def live(ev: Event) -> None:
        hass.async_create_task(b.live(str(ev.data.get("kamera") or "")))

    hass.bus.async_listen("esphome.remote_cam_snapshot", snapshot)
    hass.bus.async_listen("esphome.remote_live_start", live)
    for fn in (ws_klingel_get, ws_klingel_save, ws_klingel_test):
        websocket_api.async_register_command(hass, fn)
    return b


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/klingel_get"})
@callback
def ws_klingel_get(hass, connection, msg) -> None:
    connection.send_result(msg["id"], {"klingel": hass.data[DOMAIN + "_bilder"].klingeln})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/klingel_save", vol.Required("klingel"): list})
@websocket_api.async_response
async def ws_klingel_save(hass, connection, msg) -> None:
    b: Bilder = hass.data[DOMAIN + "_bilder"]
    try:
        b.kd.data["klingel"] = validate_klingel(msg["klingel"])
    except (vol.Invalid, ValueError, TypeError) as e:
        connection.send_error(msg["id"], "ungueltig", str(e))
        return
    await b.kd.store.async_save(b.kd.data)
    b.starten()
    connection.send_result(msg["id"], {"klingel": b.klingeln})


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/klingel_test", vol.Required("platz"): int})
@websocket_api.async_response
async def ws_klingel_test(hass, connection, msg) -> None:
    b: Bilder = hass.data[DOMAIN + "_bilder"]
    if not 0 <= msg["platz"] < len(b.klingeln):
        connection.send_error(msg["id"], "ungueltig", "Keine solche Klingel")
        return
    await b.klingel(msg["platz"])
    connection.send_result(msg["id"], {})
