"""Showtime (2026-11-26): YouTube-Listen mit Vorschaubildern auf der Remote, abgespielt in SmartTube am Streamer.

Nutzer: "auf der Remote die Videos mit Cover auswaehlbar machen und dann auf dem Streamer abspielen - kategorisiert
(Vorschlaege, Abos, ...), Als Naechstes / Aehnliche. Login ohne Cookies von woanders - jeder soll es ohne KI-Hilfe
koennen."

Anmeldung wie an einem Fernseher (Geraete-Code): der Konfigurator zeigt einen Code, man gibt ihn am Handy unter
google.com/device ein und bestaetigt. HA bekommt einen Zugangsschluessel (OAuth, Refresh-Token in kd.data, nie im
Repo) und erneuert ihn selbst. Die Kenndaten des YouTube-TV-Clients stehen in der Web-App youtube.com/tv (base.js)
und werden zur Laufzeit von dort gelesen - genau wie SmartTube es macht.

Listen: InnerTube (TV-Client) - Empfohlen, Abos, Spaeter ansehen, Verlauf, Mag ich, Playlists und "Aehnliche" zum
zuletzt gestarteten/gesehenen Video. Weitere Eintraege werden beim Blaettern nachgeladen (Fortsetzungs-Token).

Remote:
  GET /api/esphomeremote/showtime/<geraet>?k=<schluessel>&kat=<kat>&s=<seite>[&tipp=<0..3>]
    -> {"a":1, "k":[[kat,name],...], "kat":kat, "kn":name, "s":seite, "n":seiten, "mehr":0/1,
        "v":[{"t":Titel, "z":Kanal · Alter, "d":Dauer, "p":1 bei Playlist}], "b":Bild-Pfad, "m":Meldung}
    tipp = Eintrag der Seite antippen: Video -> am Streamer abspielen, Playlist -> oeffnen (kat "pl:<id>")
  GET /api/esphomeremote/showtime_bild/<geraet>?k=..&kat=..&s=..  -> JPEG 112x280: 4 Vorschaubilder (je 112x63,
    Zeilenabstand 70 px) - die Remote laedt pro Seite ein Bild, die Texte zeichnet sie selbst.
Aktion esphomeremote.showtime: video (ID oder Link), streamer (optional).
"""
from __future__ import annotations

import asyncio
import hmac
import io
import json
import logging
import re
import time
import uuid
from urllib.parse import quote

import aiohttp
import voluptuous as vol
from aiohttp import web

from homeassistant.components import websocket_api
from homeassistant.components.http import HomeAssistantView
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession

_LOGGER = logging.getLogger(__name__)
DOMAIN = "esphomeremote"
KEY = "showtime"
SMARTTUBE = "org.smarttube.stable"
TV_UA = "Mozilla/5.0 (ChromiumStylePlatform) Cobalt/25.lts.30.1034943-gold (unlike Gecko), Unknown_TV_Unknown_0/Unknown"
SCOPE = "http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content"
API = "https://www.youtube.com/youtubei/v1/"
PRO_SEITE = 4
ZEILE, BB, BH = 70, 112, 63          # Bild: Zeilenhoehe, Vorschaubild-Breite/-Hoehe
LISTE_GUELTIG = 600                  # Listen 10 min zwischenspeichern
VID_RE = re.compile(r"^[\w-]{11}$")

# Kategorie -> (browseId, Name de, Name en); "aehnlich" ueber /next zum zuletzt gestarteten/gesehenen Video
KATS = [
    ("empfohlen", "FEwhat_to_watch", "Empfohlen", "Recommended"),
    ("abos", "FEsubscriptions", "Abos", "Subscriptions"),
    ("aehnlich", None, "Ähnliche", "Up next"),
    ("weiter", None, "Weiterschauen", "Continue watching"),
    ("spaeter", "VLWL", "Später ansehen", "Watch later"),
    ("verlauf", "FEhistory", "Verlauf", "History"),
    ("mag_ich", "VLLL", "Mag ich", "Liked"),
    ("playlists", "FEplaylist_aggregation", "Playlists", "Playlists"),
]


# Zeichen, die die Schriften der Remote haben (templates/remote.yaml, glyphs) - Emojis usw. fallen weg
ZEICHEN = set(" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~€°±·«»¿¡"
              "ÀÁÂÃÄÅÆÇÈÉÊËÌÍÎÏÑÒÓÔÕÖØÙÚÛÜÝßàáâãäåæçèéêëìíîïñòóôõöøùúûüýÿ–—’‘”“„…−‹›")


def sauber(t: str) -> str:
    t = (t or "").replace("\xa0", " ").replace("|", "·")
    return re.sub(r"\s{2,}", " ", "".join(c for c in t if c in ZEICHEN)).strip()


def _txt(t) -> str:
    if not isinstance(t, dict):
        return ""
    return t.get("simpleText") or "".join(r.get("text", "") for r in t.get("runs", []))


def _kacheln(o):
    """Alle tileRenderer einer InnerTube-Antwort (TV-Oberflaeche)."""
    if isinstance(o, dict):
        if "tileRenderer" in o:
            yield o["tileRenderer"]
        for v in o.values():
            yield from _kacheln(v)
    elif isinstance(o, list):
        for v in o:
            yield from _kacheln(v)


def _finde(o, schluessel: str):
    if isinstance(o, dict):
        if schluessel in o:
            return o[schluessel]
        for v in o.values():
            r = _finde(v, schluessel)
            if r is not None:
                return r
    elif isinstance(o, list):
        for v in o:
            r = _finde(v, schluessel)
            if r is not None:
                return r
    return None


def _eintrag(t: dict) -> dict | None:
    """Kachel -> {id|browse, t, z, d, bild}"""
    cmd = t.get("onSelectCommand") or {}
    md = (t.get("metadata") or {}).get("tileMetadataRenderer") or {}
    titel = _txt(md.get("title")).strip()
    zeilen = []
    for l in md.get("lines") or []:
        teile = [_txt(i["lineItemRenderer"].get("text")).strip() for i in (l.get("lineRenderer") or {}).get("items", [])
                 if "lineItemRenderer" in i]
        zeilen.append([x for x in teile if x and x not in ("•", "·")])
    kanal = zeilen[0][0] if zeilen and zeilen[0] else ""
    alter = ""
    for z in zeilen[1:]:
        for x in z:
            if x.startswith(("vor ", "Vor ")) or x.endswith((" ago", " ago)")) or "Std." in x:
                alter = x
    hdr = (t.get("header") or {}).get("tileHeaderRenderer") or {}
    daumen = ((hdr.get("thumbnail") or {}).get("thumbnails") or [{}])
    bild = daumen[0].get("url", "") if daumen else ""
    dauer = ""
    for ov in hdr.get("thumbnailOverlays") or []:
        if "thumbnailOverlayTimeStatusRenderer" in ov:
            dauer = _txt(ov["thumbnailOverlayTimeStatusRenderer"].get("text"))
    we = cmd.get("watchEndpoint") or _finde(cmd, "watchEndpoint") or {}
    vid = we.get("videoId")
    if not vid:
        m = re.search(r"/vi/([\w-]{11})/", bild)
        vid = m.group(1) if m and t.get("contentType") == "TILE_CONTENT_TYPE_VIDEO" else None
    if not titel:   # geloeschte/private Videos haben keinen Titel
        return None
    gesehen = 0
    for ov in hdr.get("thumbnailOverlays") or []:
        if "thumbnailOverlayResumePlaybackRenderer" in ov:
            gesehen = int(ov["thumbnailOverlayResumePlaybackRenderer"].get("percentDurationWatched") or 0)
    extra = {"w": gesehen} if gesehen else {}
    m = re.search(r'"browseId": "(UC[\w-]{20,})"', json.dumps(t.get("onLongPressCommand") or {}))
    if m:
        extra["kanal"] = m.group(1)
        extra["kanal_name"] = kanal
    if t.get("contentType") == "TILE_CONTENT_TYPE_SHORTS":
        extra["short"] = True
    if vid and VID_RE.match(vid):
        return {"id": vid, "t": titel, "z": " · ".join(x for x in (kanal, alter) if x), "d": dauer,
                "bild": f"https://i.ytimg.com/vi/{vid}/mqdefault.jpg", **extra}
    be = cmd.get("browseEndpoint") or _finde(cmd, "browseEndpoint") or {}
    if be.get("browseId"):
        anzahl = " · ".join(" ".join(z) for z in zeilen[1:] if z)
        return {"browse": be["browseId"], "t": titel, "z": " · ".join(x for x in (kanal, anzahl) if x), "d": "",
                "bild": ("https:" + bild) if bild.startswith("//") else bild}
    return None


def _fortsetzung(d) -> str | None:
    c = _finde(d, "nextContinuationData")
    if isinstance(c, dict) and c.get("continuation"):
        return c["continuation"]
    c = _finde(d, "continuationCommand")
    if isinstance(c, dict) and c.get("token"):
        return c["token"]
    return None


def _bild_bauen(bilder: list[bytes | None], gesehen: list[int] | None = None) -> bytes:
    """4 Vorschaubilder untereinander (Executor); gesehen = Prozent je Video -> roter Balken unten wie bei YouTube."""
    from PIL import Image, ImageDraw
    out = Image.new("RGB", (BB, ZEILE * PRO_SEITE), (0, 0, 0))
    for i, b in enumerate(bilder[:PRO_SEITE]):
        if not b:
            continue
        try:
            src = Image.open(io.BytesIO(b)).convert("RGB")
        except Exception:  # noqa: BLE001 - kaputtes Bild: Platz bleibt schwarz
            continue
        # 16:9 randlos auf 112x63
        r = src.width / src.height
        w, h = (round(BH * r), BH) if r > BB / BH else (BB, round(BB / r))
        src = src.resize((w, h), Image.LANCZOS)
        l, o = (w - BB) // 2, (h - BH) // 2
        y = i * ZEILE + (ZEILE - BH) // 2
        out.paste(src.crop((l, o, l + BB, o + BH)), (0, y))
        p = (gesehen or [0] * PRO_SEITE)[i] if gesehen and i < len(gesehen) else 0
        if p > 0:
            d = ImageDraw.Draw(out)
            d.rectangle((0, y + BH - 3, BB - 1, y + BH - 1), fill=(80, 80, 80))
            d.rectangle((0, y + BH - 3, max(2, BB * min(p, 100) // 100) - 1, y + BH - 1), fill=(229, 57, 53))
    b = io.BytesIO()
    out.save(b, "JPEG", quality=80)
    return b.getvalue()


class Showtime:
    def __init__(self, hass: HomeAssistant, kd) -> None:
        self.hass, self.kd = hass, kd
        self._client: dict | None = None      # client_id, client_secret, version, zeit
        self._zugang: tuple[str, float] = ("", 0.0)
        self._sperre = asyncio.Lock()
        self.anmeldung: dict | None = None    # laufende Anmeldung: user_code, url, ablauf
        self._anm_task: asyncio.Task | None = None
        self.listen: dict[str, dict] = {}     # kat -> {"v": [...], "cont": token, "zeit": t}
        self.bilder: dict[tuple, bytes] = {}
        self.letztes: str | None = None       # zuletzt per Showtime gestartetes Video
        self.letztes_zeit = 0.0
        self.konto = ""
        self.pl_namen: dict[str, str] = {}   # "pl:<id>" -> Name (Playlist/Kanal) fuers Blaettern

    # ------------------------------------------------------------ Einstellungen
    @property
    def cfg(self) -> dict:
        return self.kd.data.setdefault(KEY, {})

    @property
    def angemeldet(self) -> bool:
        return bool((self.cfg.get("token") or {}).get("refresh_token"))

    def sprache_de(self) -> bool:
        return str(self.hass.config.language or "de").startswith("de")

    def kat_reihe(self) -> list[tuple[str, bool]]:
        """Reihenfolge und Sichtbarkeit aus dem Konfigurator (cfg["kategorien"] = [[kat, an], ...]); neue Kategorien hinten."""
        alle = [k for k, *_ in KATS]
        gespeichert = [(str(k), bool(an)) for k, an in (self.cfg.get("kategorien") or []) if k in alle]
        bekannt = {k for k, _ in gespeichert}
        return gespeichert + [(k, True) for k in alle if k not in bekannt]

    def kategorien(self) -> list[list[str]]:
        """Sichtbare Kategorien in der eingestellten Reihenfolge (mindestens eine)."""
        de = self.sprache_de()
        namen = {k: (n_de if de else n_en) for k, _b, n_de, n_en in KATS}
        an = [[k, namen[k]] for k, sichtbar in self.kat_reihe() if sichtbar]
        return an or [["empfohlen", namen["empfohlen"]]]

    # ------------------------------------------------------------ YouTube-TV-Client
    async def client(self, neu: bool = False) -> dict:
        if self._client and not neu and time.time() - self._client["zeit"] < 43200:
            return self._client
        s = async_get_clientsession(self.hass)
        async with s.get("https://www.youtube.com/tv", headers={"User-Agent": TV_UA}) as r:
            html = await r.text()
        ver = re.search(r'"INNERTUBE_CLIENT_VERSION":"([^"]+)"', html)
        pfad = re.search(r"(/s/_/kabuki/_/js/[^'\"]+)", html)
        if not pfad:
            raise RuntimeError("youtube.com/tv: Skript nicht gefunden")
        async with s.get("https://www.youtube.com" + pfad.group(1), headers={"User-Agent": TV_UA}) as r:
            js = await r.text()
        m = re.search(r'clientId:"([^"]+)",[^"]*?:"([^"]+)"', js)
        if not m:
            raise RuntimeError("youtube.com/tv: Client-Kennung nicht gefunden")
        self._client = {"client_id": m.group(1), "client_secret": m.group(2),
                        "version": ver.group(1) if ver else "7.20261123.19.00", "zeit": time.time()}
        return self._client

    async def _oauth(self, pfad: str, body: dict) -> dict:
        s = async_get_clientsession(self.hass)
        async with s.post("https://www.youtube.com/o/oauth2/" + pfad, json=body) as r:
            try:
                return await r.json(content_type=None)
            except Exception:  # noqa: BLE001
                return {"error": f"http {r.status}"}

    # ------------------------------------------------------------ Anmeldung (Geraete-Code)
    async def anmelden(self) -> dict:
        c = await self.client()
        d = await self._oauth("device/code", {"client_id": c["client_id"], "scope": SCOPE,
                                              "device_id": str(uuid.uuid4()), "device_model": "ytlr::"})
        if "user_code" not in d:
            raise RuntimeError(f"Anmeldung nicht möglich: {d.get('error') or d}")
        self.anmeldung = {"code": d["user_code"], "url": d.get("verification_url") or "https://www.google.com/device",
                          "ablauf": time.time() + int(d.get("expires_in", 1800)), "fehler": ""}
        if self._anm_task:
            self._anm_task.cancel()
        self._anm_task = self.hass.async_create_background_task(
            self._warten(d["device_code"], int(d.get("interval", 5))), "esphomeremote_showtime_login")
        return self.status()

    async def _warten(self, device_code: str, intervall: int) -> None:
        c = await self.client()
        while self.anmeldung and time.time() < self.anmeldung["ablauf"]:
            await asyncio.sleep(max(intervall, 5))
            r = await self._oauth("token", {"client_id": c["client_id"], "client_secret": c["client_secret"],
                                            "code": device_code, "grant_type": "http://oauth.net/grant_type/device/1.0"})
            if r.get("refresh_token"):
                self.cfg["token"] = {"refresh_token": r["refresh_token"]}
                self._zugang = (r["access_token"], time.time() + int(r.get("expires_in", 3600)) - 120)
                self.anmeldung = None
                self.listen.clear()
                await self.kd.store.async_save(self.kd.data)
                await self._konto_lesen()
                _LOGGER.info("Showtime: bei YouTube angemeldet (%s)", self.konto or "Konto")
                return
            if r.get("error") not in ("authorization_pending", "slow_down"):
                if self.anmeldung:
                    self.anmeldung["fehler"] = str(r.get("error") or "abgelehnt")
                return
        if self.anmeldung:
            self.anmeldung["fehler"] = "abgelaufen"

    async def abmelden(self) -> None:
        if self._anm_task:
            self._anm_task.cancel()
        self.anmeldung = None
        self.cfg.pop("token", None)
        self._zugang = ("", 0.0)
        self.konto = ""
        self.listen.clear()
        self.bilder.clear()
        await self.kd.store.async_save(self.kd.data)

    async def zugang(self) -> str:
        if self._zugang[0] and time.time() < self._zugang[1]:
            return self._zugang[0]
        if not self.angemeldet:
            raise PermissionError("nicht angemeldet")
        c = await self.client()
        r = await self._oauth("token", {"client_id": c["client_id"], "client_secret": c["client_secret"],
                                        "refresh_token": self.cfg["token"]["refresh_token"], "grant_type": "refresh_token"})
        if r.get("error") == "invalid_client":   # YouTube hat die TV-Kennung gewechselt
            c = await self.client(neu=True)
            r = await self._oauth("token", {"client_id": c["client_id"], "client_secret": c["client_secret"],
                                            "refresh_token": self.cfg["token"]["refresh_token"], "grant_type": "refresh_token"})
        if not r.get("access_token"):
            if r.get("error") == "invalid_grant":   # widerrufen: neu anmelden
                _LOGGER.warning("Showtime: Anmeldung bei YouTube ungültig – im Konfigurator neu anmelden")
                self.cfg.pop("token", None)
                await self.kd.store.async_save(self.kd.data)
            raise PermissionError(f"Token: {r.get('error') or r}")
        self._zugang = (r["access_token"], time.time() + int(r.get("expires_in", 3600)) - 120)
        return self._zugang[0]

    async def api(self, ep: str, body: dict) -> dict:
        c = await self.client()
        tok = await self.zugang()
        de = self.sprache_de()
        ctx = {"client": {"clientName": "TVHTML5", "clientVersion": c["version"], "hl": "de" if de else "en",
                          "gl": str(self.hass.config.country or "DE")}}
        s = async_get_clientsession(self.hass)
        async with s.post(API + ep + "?prettyPrint=false", json={"context": ctx, **body}, timeout=aiohttp.ClientTimeout(total=15),
                          headers={"Authorization": "Bearer " + tok, "User-Agent": TV_UA}) as r:
            if r.status == 401:
                self._zugang = ("", 0.0)
            r.raise_for_status()
            return await r.json(content_type=None)

    async def _konto_lesen(self) -> None:
        try:
            d = await self.api("account/accounts_list", {})
            self.konto = _txt(_finde(d, "accountName")) or ""
        except Exception as err:  # noqa: BLE001
            _LOGGER.debug("Showtime: Kontoname: %s", err)

    def status(self) -> dict:
        a = self.anmeldung
        return {"angemeldet": self.angemeldet, "konto": self.konto,
                "anmeldung": {"code": a["code"], "url": a["url"], "rest": max(0, int(a["ablauf"] - time.time())),
                              "fehler": a["fehler"]} if a else None,
                "streamer": self.cfg.get("streamer") or "", "streamers": self._streamer_liste(),
                "kategorien": [{"id": k, "an": an, "name": next(n for kk, _b, n, _e in KATS if kk == k)}
                               for k, an in self.kat_reihe()],
                "optionen": {k: self.opt(k) for k in self.OPTIONEN}}

    # ------------------------------------------------------------ Optionen (Konfigurator)
    OPTIONEN = {"start": "zuletzt", "nach": "bleiben", "shorts": False, "gesehene": False}

    def opt(self, k: str):
        return self.cfg.get(k, self.OPTIONEN[k])

    def _zeigen(self, e: dict) -> bool:
        """Filter: Shorts ausblenden, fertig gesehene (>= 90 %) ausblenden - nie im Verlauf."""
        if self.opt("shorts") and e.get("short"):
            return False
        if self.opt("gesehene") and e.get("w", 0) >= 90 and not getattr(self, "_im_verlauf", False):
            return False
        return True

    def laeuft(self) -> bool:
        """Spielt der Streamer gerade SmartTube? (Streamer-App: Status + App)"""
        st = self._streamer(None)
        if st is None:
            return False
        sid = st.cfg.get("id")
        a = self.hass.states.get(f"sensor.esphomeremote_streamer_{sid}_app")
        z = self.hass.states.get(f"sensor.esphomeremote_streamer_{sid}_status")
        return bool(a and a.state == "SmartTube" and z and z.state in ("playing", "paused"))

    def start_kat(self, sichtbar: list[str]) -> str | None:
        """Liste beim Oeffnen der Seite: None = die zuletzt gezeigte (Remote merkt sie sich)."""
        st = self.opt("start")
        if st == "aehnlich_wenn":
            return "aehnlich" if "aehnlich" in sichtbar and self.laeuft() else sichtbar[0]
        return st if st in sichtbar else None

    # ------------------------------------------------------------ Listen
    def _streamer_liste(self) -> list[dict]:
        s = self.hass.data.get(DOMAIN + "_streamer")
        if not s:
            return []
        return [{"id": sid, "name": st.cfg.get("name")} for sid, st in s.alle.items()
                if st.cfg.get("adb") and not st.cfg.get("nur_kamera")]

    async def _aehnlich_zu(self) -> tuple[str | None, str]:
        """Video, zu dem "Aehnliche" gezeigt wird: das gerade/zuletzt in SmartTube gesehene. SmartTube traegt jedes Video
        in den YouTube-Verlauf ein (2026-11-26 geprueft) - oben steht also das laufende, egal wie es gestartet wurde.
        Direkt nach einem Start ueber Showtime (90 s) dieses, der Verlauf haengt ein paar Sekunden nach."""
        if self.letztes and time.time() - self.letztes_zeit < 90:
            return self.letztes, ""
        l = self.listen.get("verlauf")
        if not l or time.time() - l["zeit"] > 30:   # Verlauf fuer "Aehnliche" frisch halten
            self.listen.pop("verlauf", None)
        v = await self.liste("verlauf")
        for e in v["v"]:
            if e.get("id"):
                return e["id"], e["t"]
        return self.letztes, ""

    async def liste(self, kat: str, mehr: bool = False) -> dict:
        """kat: Kategorie-Schluessel oder "pl:<browseId>" (geoeffnete Playlist)."""
        # "Aehnliche" braucht das Video vorher (Verlauf) - nicht unter der Sperre holen, sie ist nicht wiedereintrittsfaehig
        if kat == "weiter":
            # Angefangene Videos (5-90 % gesehen) aus dem Verlauf - kein eigener Abruf bei YouTube
            l = self.listen.get("verlauf")
            if not l or time.time() - l["zeit"] > 60:
                self.listen.pop("verlauf", None)
            v = await self.liste("verlauf")
            return {"v": [e for e in v["v"] if e.get("id") and 5 <= e.get("w", 0) < 90], "cont": None, "zeit": time.time()}
        vid, vid_titel = None, ""
        if kat == "aehnlich" and not mehr:
            vid, vid_titel = await self._aehnlich_zu()
        async with self._sperre:
            l = self.listen.get(kat)
            if l and not mehr and time.time() - l["zeit"] < LISTE_GUELTIG and (kat != "aehnlich" or l.get("zu") == vid):
                return l
            if mehr and l and l.get("cont"):
                self._im_verlauf = kat == "verlauf"
                d = await self.api("browse", {"continuation": l["cont"]})
                neu = [e for e in (_eintrag(t) for t in _kacheln(d)) if e and self._zeigen(e)]
                bekannt = {e.get("id") or e.get("browse") for e in l["v"]}
                l["v"] += [e for e in neu if (e.get("id") or e.get("browse")) not in bekannt]
                l["cont"] = _fortsetzung(d) if neu else None
                return l
            if kat == "aehnlich":
                d = await self.api("next", {"videoId": vid}) if vid else {}
            elif kat.startswith("zu:"):   # Aktionsmenue "Aehnliche Videos" zu einem bestimmten Video
                d = await self.api("next", {"videoId": kat[3:]})
            else:
                browse = kat[3:] if kat.startswith("pl:") else next((b for k, b, *_ in KATS if k == kat), None)
                if not browse:
                    raise KeyError(kat)
                d = await self.api("browse", {"browseId": browse})
            self._im_verlauf = kat == "verlauf"
            v, gesehen = [], set()
            for e in (_eintrag(t) for t in _kacheln(d)):
                schl = e and self._zeigen(e) and (e.get("id") or e.get("browse"))
                if schl and schl not in gesehen:
                    gesehen.add(schl)
                    v.append(e)
            l = self.listen[kat] = {"v": v, "cont": _fortsetzung(d), "zeit": time.time()}
            if kat == "aehnlich":
                l["zu"] = vid
                l["zu_titel"] = vid_titel or next((e["t"] for x in self.listen.values() for e in x["v"]
                                                   if e.get("id") == vid), "")
            return l

    async def seite(self, kat: str, s: int) -> tuple[list[dict], int, bool]:
        l = await self.liste(kat)
        # am Ende der geladenen Eintraege nachladen
        while (s + 1) * PRO_SEITE > len(l["v"]) and l.get("cont"):
            vorher = len(l["v"])
            l = await self.liste(kat, mehr=True)
            if len(l["v"]) == vorher:
                break
        n = max(1, -(-len(l["v"]) // PRO_SEITE))
        s = max(0, min(s, n - 1))
        return l["v"][s * PRO_SEITE:(s + 1) * PRO_SEITE], n, bool(l.get("cont"))

    async def bild(self, kat: str, s: int) -> bytes:
        eintraege, _n, _m = await self.seite(kat, s)
        schl = tuple((e.get("bild"), e.get("w", 0)) for e in eintraege)
        if schl in self.bilder:
            return self.bilder[schl]
        sess = async_get_clientsession(self.hass)

        async def holen(url: str) -> bytes | None:
            if not url:
                return None
            try:
                async with sess.get(url, timeout=aiohttp.ClientTimeout(total=10)) as r:
                    return await r.read() if r.status == 200 else None
            except Exception:  # noqa: BLE001
                return None

        roh = await asyncio.gather(*(holen(u) for u, _w in schl))
        jpg = await self.hass.async_add_executor_job(_bild_bauen, list(roh), [w for _u, w in schl])
        if len(self.bilder) > 60:
            self.bilder.clear()
        self.bilder[schl] = jpg
        return jpg

    async def spaeter(self, vid: str, hinzu: bool) -> None:
        """Video in "Spaeter ansehen" legen bzw. herausnehmen (InnerTube browse/edit_playlist)."""
        aktion = ({"addedVideoId": vid, "action": "ACTION_ADD_VIDEO"} if hinzu else
                  {"removedVideoId": vid, "action": "ACTION_REMOVE_VIDEO_BY_VIDEO_ID"})
        r = await self.api("browse/edit_playlist", {"playlistId": "WL", "actions": [aktion]})
        if r.get("status") != "STATUS_SUCCEEDED":
            raise RuntimeError(f"Später ansehen: {r.get('status') or r}")
        self.listen.pop("spaeter", None)

    # ------------------------------------------------------------ Abspielen
    def _streamer(self, wahl: str | None):
        from .streamer import _streamer_finden
        s = self.hass.data.get(DOMAIN + "_streamer")
        if not s:
            return None
        st = _streamer_finden(s, wahl or self.cfg.get("streamer") or "")
        if st is None:
            st = next((x for x in s.alle.values() if x.cfg.get("adb") and not x.cfg.get("nur_kamera")), None)
        return st

    async def abspielen(self, video: str, streamer: str | None = None, als_naechstes: bool = False,
                        ab: int = 0) -> None:
        m = re.search(r"(?:v=|youtu\.be/|/shorts/|/live/|^)([\w-]{11})(?:[?&#]|$)", str(video or "").strip())
        if not m:
            raise vol.Invalid(f"Keine YouTube-Video-ID: {video}")
        vid = m.group(1)
        st = self._streamer(streamer)
        if st is None:
            raise vol.Invalid("Kein Streamer mit ADB eingerichtet (Konfigurator › Wiedergabe)")
        if als_naechstes:
            # "Als Naechstes" (2026-11-26): SmartTube ab 32.80 haengt das Video an die Wiedergabe-Warteschlange an
            # (Extra add_to_queue, SmartTube PR #6401); laeuft der Player nicht, spielt es sofort. Nicht beenden.
            # Achtung (2026-11-26 getestet): der Aufruf oeffnet kurz SmartTubes Startbildschirm - ist in SmartTube
            # Bild-im-Bild als Hintergrundwiedergabe eingestellt, rutscht der laufende Player dabei in die Ecke.
            await st.adb(f"am start -a android.intent.action.VIEW -d 'https://www.youtube.com/watch?v={vid}' "
                         f"--ez add_to_queue true -p {SMARTTUBE}")
            return
        # Aufwecken (schaltet per CEC auch den Fernseher ein), SmartTube beenden, dann mit dem Video starten.
        # Ohne Beenden landete das Video im Bild-im-Bild-Fenster (2026-11-26): der Start schiebt SmartTubes
        # Startbildschirm vor den laufenden Player, der wechselt dabei in Bild-im-Bild und laedt dort das neue Video.
        await st.adb(f"input keyevent KEYCODE_WAKEUP; am force-stop {SMARTTUBE}; am start -a android.intent.action.VIEW "
                     f"-d 'https://www.youtube.com/watch?v={vid}{f'&t={ab}s' if ab > 0 else ''}' -p {SMARTTUBE}")
        self.letztes, self.letztes_zeit = vid, time.time()


# ---------------------------------------------------------------- Remote-Schnittstelle
class ShowtimeView(HomeAssistantView):
    url = "/api/esphomeremote/showtime/{dev}"
    name = "api:esphomeremote:showtime"
    requires_auth = False

    def __init__(self, kd, sh: Showtime) -> None:
        self.kd, self.sh = kd, sh

    async def get(self, request: web.Request, dev: str) -> web.Response:
        if not hmac.compare_digest(request.query.get("k", ""), self.kd.key):
            return web.Response(status=403)
        sh = self.sh
        de = sh.sprache_de()
        antwort: dict = {"a": 1 if sh.angemeldet else 0, "k": sh.kategorien()}
        if not sh.angemeldet:
            antwort["m"] = ("Showtime: im Konfigurator (Wiedergabe › Showtime) bei YouTube anmelden" if de else
                            "Showtime: sign in to YouTube in the configurator (Playback › Showtime)")
            return _json(antwort)
        sichtbar = [k for k, _n in antwort["k"]]
        kat = request.query.get("kat") or sichtbar[0]
        if request.query.get("auf") == "1" and (sk := sh.start_kat(sichtbar)) and sk != kat:
            kat = sk   # Seite geoeffnet: Start-Liste aus dem Konfigurator
            request = request.clone(rel_url=request.rel_url.update_query({"s": "0"}))
        if not (kat.startswith(("pl:", "zu:")) or kat in sichtbar):
            kat = sichtbar[0]   # ausgeblendet (Konfigurator) oder unbekannt: erste sichtbare
        try:
            s = max(0, int(request.query.get("s", "0")))
        except ValueError:
            s = 0
        try:
            tipp = request.query.get("tipp")
            if tipp not in (None, ""):
                eintraege, _n, _m = await sh.seite(kat, s)
                i = int(tipp)
                aktion = request.query.get("aktion") or ""   # Aktionsmenue der Remote (langer Druck)
                if 0 <= i < len(eintraege):
                    e = eintraege[i]
                    titel = sauber(e["t"])[:70]
                    if e.get("id") and aktion == "naechstes":
                        await sh.abspielen(e["id"], als_naechstes=True)
                        antwort["m"] = ("In der Warteschlange (Ende): " if de else "Queued (end): ") + titel
                    elif e.get("id") and aktion == "spaeter":
                        raus = kat == "spaeter"
                        await sh.spaeter(e["id"], not raus)
                        antwort["m"] = (("Aus „Später ansehen“ entfernt: " if raus else "Zu „Später ansehen“: ") if de else
                                        ("Removed from Watch later: " if raus else "Added to Watch later: ")) + titel
                    elif e.get("id") and aktion == "aehnlich":
                        kat, s = "zu:" + e["id"], 0
                        antwort["titel"] = ("Ähnlich: " if de else "Like: ") + titel[:40]
                    elif e.get("id") and aktion == "mag":
                        await sh.api("like/like", {"target": {"videoId": e["id"]}})
                        sh.listen.pop("mag_ich", None)
                        antwort["m"] = ("Mag ich: " if de else "Liked: ") + titel
                    elif aktion == "kanal" and e.get("kanal"):
                        kat, s = "pl:" + e["kanal"], 0
                        antwort["titel"] = sauber(e.get("kanal_name") or "Kanal")[:40]
                    elif e.get("id"):
                        # Weiterschauen: an der gesehenen Stelle einsteigen (Prozent x Dauer, 5 s davor)
                        ab = 0
                        if kat == "weiter" and e.get("w") and e.get("d"):
                            sek = 0
                            for teil in e["d"].split(":"):
                                sek = sek * 60 + int(teil) if teil.isdigit() else sek
                            ab = max(0, sek * e["w"] // 100 - 5)
                        await sh.abspielen(e["id"], ab=ab)
                        antwort["m"] = "» " + titel
                        antwort["play"] = 1
                        if (n := sh.opt("nach")) != "bleiben":
                            antwort["nach"] = n   # Remote: "media" = Jetzt-laeuft-Seite, "home" = Startseite
                    elif e.get("browse"):
                        kat, s = "pl:" + e["browse"], 0
                        antwort["titel"] = e["t"][:40]
            eintraege, n, mehr = await sh.seite(kat, s)
        except PermissionError:
            antwort["a"] = 0
            antwort["m"] = "Showtime: Anmeldung abgelaufen – im Konfigurator neu anmelden" if de else \
                "Showtime: sign-in expired – sign in again in the configurator"
            return _json(antwort)
        except Exception as err:  # noqa: BLE001
            _LOGGER.warning("Showtime %s/%s: %s", kat, s, err)
            antwort.update({"kat": kat, "s": s, "n": 1, "v": [], "m": ("YouTube nicht erreichbar" if de else
                                                                         "YouTube not reachable")})
            return _json(antwort)
        if antwort.get("titel"):
            sh.pl_namen[kat] = antwort["titel"]
        name = next((n_de if de else n_en for k, _b, n_de, n_en in KATS if k == kat),
                    antwort.get("titel") or sh.pl_namen.get(kat) or "Playlist")
        if kat == "aehnlich" and (zu := (sh.listen.get("aehnlich") or {}).get("zu_titel")):
            name = ("Ähnlich: " if de else "Like: ") + zu
        antwort.update({"kat": kat, "kn": sauber(name), "s": min(s, n - 1), "n": n, "mehr": 1 if mehr else 0,
                        "v": [{"t": sauber(e["t"])[:120], "z": sauber(e["z"])[:80], "d": sauber(e["d"]),
                               **({"p": 1} if e.get("browse") else {}), **({"k": 1} if e.get("kanal") else {})}
                              for e in eintraege],
                        "b": f"/api/esphomeremote/showtime_bild/{dev}?kat={quote(kat)}&s={min(s, n - 1)}"
                             f"&v={abs(hash(tuple(e.get('bild') for e in eintraege))) % 100000}"})
        return _json(antwort)


class ShowtimeBildView(HomeAssistantView):
    url = "/api/esphomeremote/showtime_bild/{dev}"
    name = "api:esphomeremote:showtime_bild"
    requires_auth = False

    def __init__(self, kd, sh: Showtime) -> None:
        self.kd, self.sh = kd, sh

    async def get(self, request: web.Request, dev: str) -> web.Response:
        if not hmac.compare_digest(request.query.get("k", ""), self.kd.key):
            return web.Response(status=403)
        try:
            jpg = await self.sh.bild(request.query.get("kat") or "empfohlen", max(0, int(request.query.get("s", "0"))))
        except Exception as err:  # noqa: BLE001
            _LOGGER.debug("Showtime-Bild: %s", err)
            return web.Response(status=404)
        return web.Response(body=jpg, content_type="image/jpeg", headers={"Cache-Control": "no-store"})


def _json(o: dict) -> web.Response:
    return web.json_response(o, headers={"Cache-Control": "no-store"},
                             dumps=lambda x: json.dumps(x, ensure_ascii=False, separators=(",", ":")))


# ---------------------------------------------------------------- Konfigurator
@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/showtime"})
@callback
def ws_status(hass, connection, msg) -> None:
    connection.send_result(msg["id"], hass.data[DOMAIN + "_showtime"].status())


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/showtime_login"})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_login(hass, connection, msg) -> None:
    try:
        connection.send_result(msg["id"], await hass.data[DOMAIN + "_showtime"].anmelden())
    except Exception as err:  # noqa: BLE001
        connection.send_error(msg["id"], "showtime", str(err))


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/showtime_logout"})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_logout(hass, connection, msg) -> None:
    sh = hass.data[DOMAIN + "_showtime"]
    await sh.abmelden()
    connection.send_result(msg["id"], sh.status())


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote/showtime_set", vol.Optional("streamer"): str,
                                  vol.Optional("kategorien"): [[vol.Any(str, bool)]],
                                  vol.Optional("start"): str, vol.Optional("nach"): vol.In(["bleiben", "media", "home"]),
                                  vol.Optional("shorts"): bool, vol.Optional("gesehene"): bool,
                                  vol.Optional("test"): str})
@websocket_api.require_admin
@websocket_api.async_response
async def ws_set(hass, connection, msg) -> None:
    sh = hass.data[DOMAIN + "_showtime"]
    if "streamer" in msg:
        sh.cfg["streamer"] = str(msg.get("streamer") or "")
    if "kategorien" in msg:   # [[kat, an], ...] in der gewuenschten Reihenfolge
        alle = {k for k, *_ in KATS}
        sh.cfg["kategorien"] = [[str(k), bool(an)] for k, an in msg["kategorien"] if k in alle]
    for k in ("start", "nach", "shorts", "gesehene"):
        if k in msg:
            sh.cfg[k] = msg[k]
    if any(k in msg for k in ("shorts", "gesehene")):   # Filter geaendert: Listen neu holen
        sh.listen.clear()
        sh.bilder.clear()
    if msg.get("test"):   # Konfigurator: Video zum Testen abspielen
        try:
            await sh.abspielen(msg["test"])
        except vol.Invalid as err:
            connection.send_error(msg["id"], "showtime", str(err))
            return
    await sh.kd.store.async_save(sh.kd.data)
    connection.send_result(msg["id"], sh.status())


def einrichten(hass: HomeAssistant, kd) -> Showtime:
    sh = Showtime(hass, kd)
    hass.data[DOMAIN + "_showtime"] = sh
    hass.http.register_view(ShowtimeView(kd, sh))
    hass.http.register_view(ShowtimeBildView(kd, sh))
    for fn in (ws_status, ws_login, ws_logout, ws_set):
        websocket_api.async_register_command(hass, fn)

    async def dienst(call) -> None:
        await sh.abspielen(call.data["video"], call.data.get("streamer"), als_naechstes=call.data.get("queue", False))

    hass.services.async_register(DOMAIN, "showtime", dienst, schema=vol.Schema({
        vol.Required("video"): str, vol.Optional("streamer"): str, vol.Optional("queue", default=False): bool}))
    if sh.angemeldet:
        hass.async_create_background_task(sh._konto_lesen(), "esphomeremote_showtime_konto")
    return sh
