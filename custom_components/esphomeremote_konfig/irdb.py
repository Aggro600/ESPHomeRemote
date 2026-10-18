"""IR-Datenbank fuer den Konfigurator (2026-10-11).

Wie beim OpenRemote-Original (Studio baut daraus OpenRemote.irdb) dient die Flipper-IRDB als Quelle
(github.com/Lucaslhm/Flipper-IRDB, ~9000 Geraete). Anders als dort liegt sie NICHT auf der SD-Karte der Remote:
die Karte teilt sich bei uns die Pins mit dem Mikrofon. Die Datenbank bleibt in Home Assistant
(.storage/esphomeremote_konfig_irdb/), der Konfigurator sucht darin, und nur die gewaehlten Befehle wandern als
IR-Schritte in die Tastenbelegung (Remote speichert sie mit der Belegung im Flash).

Dateien:
  flipper.zip  - unveraendertes GitHub-Archiv (~21 MB); einzelne .ir-Dateien werden bei Bedarf daraus gelesen
  index.json   - Kategorie/Marke/Modell/Tastenzahl je Datei, fuer die Suche
"""

from __future__ import annotations

import json
import logging
import time
import zipfile
from pathlib import Path

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession

_LOGGER = logging.getLogger(__name__)

QUELLE = "Lucaslhm/Flipper-IRDB"
URL = "https://codeload.github.com/Lucaslhm/Flipper-IRDB/zip/refs/heads/main"
MAX_ZIP = 200 * 1024 * 1024

# Protokolle, die die Remote selbst kodieren kann (esphome/components/tasten_konfig/ir_encoders.h)
PROTOKOLLE = {
    "NEC", "NECext", "NEC1", "NEC5", "Onkyo", "NEC42", "NEC42ext", "SIRC", "SIRC15", "SIRC20", "RC5", "RC5X", "RC6",
    "Samsung32", "Kaseikyo", "Panasonic", "Denon", "Pioneer", "RCA", "JVC",
}
MAX_RAW = 1200   # Werte je Rohsignal (Klimaanlagen ~300-700)


def _flipper_hex(s: str) -> int:
    """'49 00 00 00' -> 0x49 (Bytes little-endian), wie flipper_hex() in der Firmware."""
    teile = s.split()
    if len(teile) <= 1:
        return int(s.strip() or "0", 16)
    v = 0
    for i, b in enumerate(teile[:4]):
        v |= int(b, 16) << (8 * i)
    return v


def parse_ir(text: str) -> list[dict]:
    """Flipper-.ir-Datei -> Liste von Befehlen (parsed oder raw)."""
    out: list[dict] = []
    cur: dict = {}

    def fertig():
        if not cur.get("name"):
            return
        n = cur["name"][:40]
        if cur.get("type") == "parsed" and cur.get("protocol"):
            try:
                out.append({"name": n, "proto": cur["protocol"], "addr": _flipper_hex(cur.get("address", "0")),
                            "cmd": _flipper_hex(cur.get("command", "0")), "ok": cur["protocol"] in PROTOKOLLE})
            except ValueError:
                pass
        elif cur.get("type") == "raw" and cur.get("data"):
            try:
                werte = [int(x) for x in cur["data"].split()]
            except ValueError:
                return
            raw = [w if i % 2 == 0 else -w for i, w in enumerate(werte) if w > 0]
            out.append({"name": n, "raw": raw[:MAX_RAW], "f": int(float(cur.get("frequency", "38000"))),
                        "ok": 0 < len(raw) <= MAX_RAW})

    for zeile in text.splitlines():
        zeile = zeile.strip()
        if not zeile or zeile.startswith("#"):
            continue
        k, _, v = zeile.partition(":")
        k, v = k.strip(), v.strip()
        if k == "name":
            fertig()
            cur = {"name": v}
        elif cur:
            cur[k] = v
    fertig()
    return out


class IrDb:
    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self.dir = Path(hass.config.path(".storage", "esphomeremote_konfig_irdb"))
        self.zip = self.dir / "flipper.zip"
        self.idx_path = self.dir / "index.json"
        self.index: list[dict] | None = None
        self.meta: dict = {}
        self.job: dict | None = None   # laufender Download {phase, pct, text, fehler}

    # ---------------------------------------------------------------- Laden
    def _laden(self) -> None:
        if self.idx_path.exists() and self.zip.exists():
            d = json.loads(self.idx_path.read_text())
            self.index, self.meta = d["geraete"], d["meta"]

    async def laden(self) -> None:
        await self.hass.async_add_executor_job(self._laden)

    def status(self) -> dict:
        return {"installiert": self.index is not None, "meta": self.meta, "job": self.job}

    # ------------------------------------------------------------- Download
    async def herunterladen(self) -> None:
        if self.job and self.job.get("phase") not in ("fertig", "fehler"):
            return
        self.job = {"phase": "laden", "pct": 0, "text": "Verbinde mit GitHub …"}
        try:
            self.dir.mkdir(parents=True, exist_ok=True)
            tmp = self.dir / "flipper.zip.tmp"
            sess = async_get_clientsession(self.hass)
            async with sess.get(URL, timeout=None) as r:
                r.raise_for_status()
                gesamt = int(r.headers.get("Content-Length") or 0)
                n = 0
                with open(tmp, "wb") as f:   # noqa: ASYNC230 - kleine Bloecke, Datei lokal
                    async for block in r.content.iter_chunked(256 * 1024):
                        n += len(block)
                        if n > MAX_ZIP:
                            raise ValueError("Archiv zu gross")
                        await self.hass.async_add_executor_job(f.write, block)
                        self.job.update(pct=int(n * 100 / gesamt) if gesamt else -1,
                                        text=f"Lädt {n / 1048576:.1f} MB" + (f" von {gesamt / 1048576:.1f} MB" if gesamt else ""))
            self.job = {"phase": "index", "pct": -1, "text": "Baue Suchindex …"}
            await self.hass.async_add_executor_job(self._index_bauen, tmp)
            self.job = {"phase": "fertig", "pct": 100, "text": f"{len(self.index)} Geräte installiert"}
        except Exception as err:   # noqa: BLE001 - Fehler an die Oberflaeche
            _LOGGER.warning("IR-Datenbank: %s", err)
            self.job = {"phase": "fehler", "pct": 0, "text": f"Fehler: {err}"}

    def _index_bauen(self, tmp: Path) -> None:
        geraete = []
        with zipfile.ZipFile(tmp) as z:
            for info in z.infolist():
                if not info.filename.endswith(".ir") or info.file_size > 2 * 1024 * 1024:
                    continue
                teile = info.filename.split("/")[1:]   # erstes Element = Flipper-IRDB-main
                if len(teile) < 2 or teile[0].startswith("."):
                    continue
                # _Converted_/<CSV|Pronto|IR_Plus>/<Buchstabe>/<Marke>/[<Geraeteart>/]<Datei>.ir:
                # umgewandelte probonopd-irdb und Pronto-Sammlungen (wie Quelle 2 beim Original)
                if teile[0] == "_Converted_":
                    if len(teile) == 6:     # CSV: .../<Marke>/<Geraeteart>/<Geraet,Untergeraet>.ir
                        teile = [f"{teile[4].replace('_', ' ')} (irdb)", teile[3], f"{teile[4]} {teile[5]}"]
                    elif len(teile) == 5:   # Pronto / IR_Plus: .../<Marke>/<Modell>.ir
                        teile = [f"Sammlung {teile[1].replace('_', ' ')}", teile[3], teile[4]]
                    else:
                        continue
                try:
                    befehle = parse_ir(z.read(info).decode("utf-8", "replace"))
                except Exception:   # noqa: BLE001 - kaputte Einzeldatei ueberspringen
                    continue
                if not befehle:
                    continue
                kat = teile[0].replace("_", " ")
                marke = teile[1].replace("_", " ") if len(teile) > 2 else ""
                modell = teile[-1][:-3].replace("_", " ")
                namen = sorted({b["name"] for b in befehle})
                geraete.append({"p": info.filename, "k": kat, "m": marke, "n": modell, "b": len(befehle),
                                "t": " ".join(namen[:40])[:300]})
        geraete.sort(key=lambda g: (g["k"].lower(), g["m"].lower(), g["n"].lower()))
        meta = {"quelle": QUELLE, "geladen": time.strftime("%Y-%m-%d %H:%M"), "anzahl": len(geraete),
                "marken": len({(g["k"], g["m"]) for g in geraete}), "mb": round(tmp.stat().st_size / 1048576, 1)}
        tmp.replace(self.zip)
        self.idx_path.write_text(json.dumps({"meta": meta, "geraete": geraete}, ensure_ascii=False))
        self.index, self.meta = geraete, meta

    def _loeschen(self) -> None:
        for p in (self.zip, self.idx_path):
            p.unlink(missing_ok=True)
        self.index, self.meta, self.job = None, {}, None

    async def loeschen(self) -> None:
        await self.hass.async_add_executor_job(self._loeschen)

    # ---------------------------------------------------------------- Suche
    def suchen(self, q: str, limit: int = 60) -> dict:
        if self.index is None:
            return {"treffer": [], "gesamt": 0}
        woerter = [w for w in q.lower().split() if w]
        treffer, gesamt = [], 0
        for i, g in enumerate(self.index):
            kopf = f"{g['k']} {g['m']} {g['n']}".lower()
            if woerter and not all(w in kopf or w in g["t"].lower() for w in woerter):
                continue
            gesamt += 1
            if len(treffer) < limit:
                # Treffer im Namen vor Treffern nur in den Tastennamen
                treffer.append({"id": i, "k": g["k"], "m": g["m"], "n": g["n"], "b": g["b"],
                                "r": (0 if all(w in kopf for w in woerter) else 2) + ("_Converted_" in g["p"])})
        treffer.sort(key=lambda t: t["r"])
        return {"treffer": treffer, "gesamt": gesamt}

    def _geraet(self, i: int) -> dict:
        g = self.index[i]
        with zipfile.ZipFile(self.zip) as z:
            befehle = parse_ir(z.read(g["p"]).decode("utf-8", "replace"))
        return {"id": i, "k": g["k"], "m": g["m"], "n": g["n"], "befehle": befehle}

    async def geraet(self, i: int) -> dict:
        if self.index is None or not 0 <= i < len(self.index):
            raise ValueError("Gerät nicht gefunden")
        return await self.hass.async_add_executor_job(self._geraet, i)


# ------------------------------------------------------------------ Websocket
def registrieren(hass: HomeAssistant, db: IrDb) -> None:
    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_status"})
    @callback
    def ws_status(hass, connection, msg):
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_laden"})
    @websocket_api.require_admin
    @callback
    def ws_laden(hass, connection, msg):
        hass.async_create_background_task(db.herunterladen(), "esphomeremote_irdb_laden")
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_loeschen"})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_loeschen(hass, connection, msg):
        await db.loeschen()
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command(
        {vol.Required("type"): "esphomeremote_konfig/irdb_suchen", vol.Optional("q", default=""): str,
         vol.Optional("limit", default=60): vol.All(int, vol.Range(1, 200))}
    )
    @websocket_api.async_response
    async def ws_suchen(hass, connection, msg):
        # im Executor: die Suche laeuft ueber den ganzen Index (zehntausende Geraete)
        connection.send_result(msg["id"], await hass.async_add_executor_job(db.suchen, msg["q"], msg["limit"]))

    @websocket_api.websocket_command(
        {vol.Required("type"): "esphomeremote_konfig/irdb_geraet", vol.Required("geraet"): int}
    )
    @websocket_api.async_response
    async def ws_geraet(hass, connection, msg):
        try:
            connection.send_result(msg["id"], await db.geraet(msg["geraet"]))   # "id" ist die Nachrichtennummer!
        except (ValueError, KeyError, OSError, zipfile.BadZipFile) as err:
            connection.send_error(msg["id"], "irdb", str(err))

    for f in (ws_status, ws_laden, ws_loeschen, ws_suchen, ws_geraet):
        websocket_api.async_register_command(hass, f)
