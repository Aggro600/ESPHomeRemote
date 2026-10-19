"""Cover fuer die Fernbedienungen (2026-10-19, vorher Automation + smarttube_cover_resize.py nur fuer den Streamer).

Jede Remote zeigt das Bild ihres "Jetzt laeuft"-Players (Verknuepfung "jetzt", gespiegelt als
sensor.esphomeremote_spiegel_<remote>_jetzt). Aendert sich dessen entity_picture, rechnet die Integration es in vier
feste JPEGs um - die Remote skaliert nicht selbst:
    /local/esphomeremote_cover_<remote>_{player,home}_{ganz,voll}.jpg
    player 240x320 (Media-Seite), home 216x172 (Startseiten-Karte); ganz = ganzes Bild mit Balken, voll = randlos
und ruft danach esphome.<remote>_refresh_cover auf. Gilt fuer jede Quelle (Streamer, Multiroom, Chromecast, TV).
"""

from __future__ import annotations

import asyncio
import io
import logging
import os
import time

from homeassistant.core import Event, HomeAssistant, callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.event import async_track_state_change_event

from .spiegel import spiegel_id

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote_konfig"
GROESSEN = {"player": (240, 320), "home": (216, 172)}
LEER = "/local/esphomeremote_cover_leer.png"
# Mitgelieferte Bilder (Ordner bilder/), beim Start nach www kopiert: neutrales Cover, Netflix-Logo (Streamer)
MITGELIEFERT = ("esphomeremote_cover_leer.png", "esphomeremote_netflix_n.png")


def bilder_bereitstellen(www: str) -> None:
    import shutil
    os.makedirs(www, exist_ok=True)
    for n in MITGELIEFERT:
        if not os.path.exists(os.path.join(www, n)):
            shutil.copy2(os.path.join(os.path.dirname(__file__), "bilder", n), os.path.join(www, n))


def _varianten(daten: bytes) -> dict[str, bytes]:
    """Vier JPEGs aus einem Bild (Executor)."""
    from PIL import Image
    src = Image.open(io.BytesIO(daten)).convert("RGB")
    out = {}
    for name, (bw, bh) in GROESSEN.items():
        r = src.width / src.height
        # ganz: ganzes Bild, schwarze Balken
        w, h = (bw, round(bw / r)) if r > bw / bh else (round(bh * r), bh)
        c = Image.new("RGB", (bw, bh), (0, 0, 0))
        c.paste(src.resize((w, h), Image.LANCZOS), ((bw - w) // 2, (bh - h) // 2))
        b = io.BytesIO(); c.save(b, "JPEG", quality=82); out[f"{name}_ganz"] = b.getvalue()
        # voll: randlos, beschnitten
        w, h = (round(bh * r), bh) if r > bw / bh else (bw, round(bw / r))
        l, t = (w - bw) // 2, (h - bh) // 2
        b = io.BytesIO(); src.resize((w, h), Image.LANCZOS).crop((l, t, l + bw, t + bh)).save(b, "JPEG", quality=82)
        out[f"{name}_voll"] = b.getvalue()
    return out


def _schreiben(www: str, slug: str, bilder: dict[str, bytes]) -> None:
    os.makedirs(www, exist_ok=True)
    for k, d in bilder.items():
        ziel = os.path.join(www, f"esphomeremote_cover_{slug}_{k}.jpg")
        with open(ziel + ".tmp", "wb") as f:
            f.write(d)
        os.replace(ziel + ".tmp", ziel)


class Cover:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass = hass
        self.kd = kd
        self.www = hass.config.path("www")
        self._abo = None
        self._stand: dict[str, str] = {}             # slug -> zuletzt verarbeitete Bild-URL
        self._cache: tuple[str, float, dict] | None = None   # (url, zeit, varianten) - mehrere Remotes, ein Player
        self._sperre = asyncio.Lock()
        self.slugs: list[str] = []

    @callback
    def starten(self) -> None:
        if not self.slugs:
            self.hass.async_add_executor_job(bilder_bereitstellen, self.www)
        if self._abo:
            self._abo()
        slugs = self.slugs = [d.replace("-", "_") for d in self.kd.devices]
        self._abo = async_track_state_change_event(self.hass, [spiegel_id(s, "jetzt") for s in slugs], self._geaendert)
        for s in slugs:
            self.hass.async_create_task(self.pruefen(s))

    @callback
    def _geaendert(self, ev: Event) -> None:
        slug = ev.data["entity_id"].removeprefix("sensor.esphomeremote_spiegel_").removesuffix("_jetzt")
        self.hass.async_create_task(self.pruefen(slug))

    async def pruefen(self, slug: str) -> None:
        st = self.hass.states.get(spiegel_id(slug, "jetzt"))
        url = str(st.attributes.get("entity_picture") or "") if st else ""
        if not url and st and st.state not in ("off", "unavailable", "unknown"):
            url = LEER   # laeuft etwas ohne Bild: neutrales Bild statt des alten Covers (die Remote zeigt ggf. das App-Logo)
        if not url or url == self._stand.get(slug):
            return
        self._stand[slug] = url
        async with self._sperre:
            try:
                bilder = await self._bilder(url)
                await self.hass.async_add_executor_job(_schreiben, self.www, slug, bilder)
            except Exception as err:  # noqa: BLE001
                _LOGGER.warning("Cover fuer %s (%s): %s", slug, url[:80], err)
                self._stand.pop(slug, None)
                return
        if self.hass.services.has_service("esphome", f"{slug}_refresh_cover"):
            try:
                await asyncio.wait_for(self.hass.services.async_call("esphome", f"{slug}_refresh_cover", {}, blocking=True), 8)
            except Exception as err:  # noqa: BLE001 - schlafende Remote: holt das Bild beim Verbinden selbst
                _LOGGER.debug("%s_refresh_cover: %s", slug, err)

    async def _bilder(self, url: str) -> dict[str, bytes]:
        if self._cache and self._cache[0] == url and time.monotonic() - self._cache[1] < 60:
            return self._cache[2]
        holen = url
        if not holen.startswith(("http://", "https://")):   # HA-eigene Adresse (/api/media_player_proxy/..., /local/...)
            holen = "http://127.0.0.1:%d%s" % (self.hass.http.server_port or 8123, holen if holen.startswith("/") else "/" + holen)
        async with async_get_clientsession(self.hass, verify_ssl=False).get(holen, timeout=15) as r:
            r.raise_for_status()
            daten = await r.read()
        bilder = await self.hass.async_add_executor_job(_varianten, daten)
        self._cache = (url, time.monotonic(), bilder)
        return bilder
