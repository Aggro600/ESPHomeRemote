"""Bilder fuer die Remotes (Klingel, Kamera, Live-Bild, Cover) in einem eigenen Ordner statt /config/www (2026-11-04).

HACS-Pruefung: alles unter /config/www liefert HA ohne Anmeldung unter /local/ aus - jeder im Netz konnte so das
letzte Klingel- und Kamerabild holen. Jetzt liegen sie in /config/.esphomeremote_images:
  - Remotes holen sie mit dem Schluessel der Integration: GET /api/esphomeremote/datei/<name>?k=<key>
  - der Konfigurator (angemeldeter Admin) ueber eine signierte Adresse: /api/esphomeremote/vorschau/<name>
Beim Start raeumt die Integration die alten Kopien aus /config/www weg. Die zwei mitgelieferten Grafiken
(leeres Cover, Netflix-Logo) bleiben zusaetzlich in www - die zeigt HA als Bild des Media Players an.
"""
from __future__ import annotations

import glob
import os
import re

from aiohttp import web

from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant

ORDNER = ".esphomeremote_images"
NAME = re.compile(r"esphomeremote_[a-z0-9_]+\.(jpg|png)")
ALT_WWW = ("esphomeremote_doorbell_*.jpg", "esphomeremote_cam.jpg", "esphomeremote_live.jpg", "esphomeremote_cover_*_*.jpg")


ZEITEN: dict[str, int] = {}   # Name -> Aenderungszeit, ohne Dateizugriff in der Ereignisschleife


def merken(name: str, t: int) -> None:
    ZEITEN[name] = t


def gemerkt(name: str) -> int:
    return ZEITEN.get(name, 0)


def zeiten_laden(hass: HomeAssistant) -> None:
    """Beim Start (Executor): Zeitstempel der vorhandenen Bilder."""
    os.makedirs(ordner(hass), exist_ok=True)
    for f in os.listdir(ordner(hass)):
        if NAME.fullmatch(f):
            try:
                ZEITEN[f] = int(os.stat(pfad(hass, f)).st_mtime)
            except OSError:
                pass


def ordner(hass: HomeAssistant) -> str:
    return hass.config.path(ORDNER)


def pfad(hass: HomeAssistant, name: str) -> str:
    return os.path.join(ordner(hass), name)


def zeit(hass: HomeAssistant, name: str) -> int:
    """Aenderungszeit (fuer ?v= auf der Remote), 0 = fehlt. Nur im Executor aufrufen."""
    try:
        return int(os.stat(pfad(hass, name)).st_mtime)
    except OSError:
        return 0


def www_umziehen(hass: HomeAssistant) -> int:
    """Alte, oeffentlich erreichbare Bilder aus /config/www in den eigenen Ordner verschieben (Executor)."""
    os.makedirs(ordner(hass), exist_ok=True)
    n = 0
    for muster in ALT_WWW:
        for f in glob.glob(os.path.join(hass.config.path("www"), muster)):
            try:
                os.replace(f, pfad(hass, os.path.basename(f)))
                n += 1
            except OSError:
                try:
                    os.remove(f)
                except OSError:
                    pass
    return n


def _lesen(p: str) -> bytes | None:
    try:
        with open(p, "rb") as f:
            return f.read()
    except OSError:
        return None


async def _antwort(hass: HomeAssistant, name: str) -> web.Response:
    if not NAME.fullmatch(name):
        return web.Response(status=404)
    daten = await hass.async_add_executor_job(_lesen, pfad(hass, name))
    if daten is None:
        return web.Response(status=404)
    return web.Response(body=daten, content_type="image/png" if name.endswith(".png") else "image/jpeg",
                        headers={"Cache-Control": "no-store"})


class DateiView(HomeAssistantView):
    """Fuer die Remotes (und das Dock, das ihre Anfragen weiterreicht): nur mit dem Schluessel der Integration."""

    url = "/api/esphomeremote/datei/{name}"
    name = "api:esphomeremote:datei"
    requires_auth = False

    def __init__(self, kd, schluessel_ok) -> None:
        self.kd = kd
        self._ok = schluessel_ok

    async def get(self, request: web.Request, name: str) -> web.Response:
        if not self._ok(request, self.kd.key):
            return web.Response(status=403)
        return await _antwort(request.app["hass"], name)


class VorschauView(HomeAssistantView):
    """Fuer den Konfigurator: normale HA-Anmeldung (im <img> ueber auth/sign_path)."""

    url = "/api/esphomeremote/vorschau/{name}"
    name = "api:esphomeremote:vorschau"
    requires_auth = True

    async def get(self, request: web.Request, name: str) -> web.Response:
        return await _antwort(request.app["hass"], name)
