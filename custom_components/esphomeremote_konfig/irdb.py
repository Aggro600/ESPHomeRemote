"""IR-Datenbanken fuer den Konfigurator (2026-10-11, mehrere Quellen und eigene Codes seit 2026-10-28).

Wie beim OpenRemote-Original (Studio baut daraus OpenRemote.irdb) dient die Flipper-IRDB als Quelle
(github.com/Lucaslhm/Flipper-IRDB, ~9000 Geraete). Anders als dort liegt sie NICHT auf der SD-Karte der Remote:
die Karte teilt sich bei uns die Pins mit dem Mikrofon. Die Datenbanken bleiben in Home Assistant
(.storage/esphomeremote_konfig_irdb/), der Konfigurator sucht darin, und nur die gewaehlten Befehle wandern als
IR-Schritte in die Tastenbelegung (Remote speichert sie mit der Belegung im Flash).

Quellen (je ein unveraendertes GitHub-Archiv, einzelne Dateien werden bei Bedarf daraus gelesen):
  flipper  - Lucaslhm/Flipper-IRDB: flipper.zip + index.json (Kategorie/Marke/Modell/Tastenzahl je Datei)
  harmony  - pickysysadmin/logitech-harmony-ir-archive (Logitech Harmony, ~276 000 Geraete, CC0): harmony.zip +
             harmony.tsv (eine Zeile je Geraet). Befehle sind Pronto-Hex und werden zu Rohsignalen.
  eigen    - "Meine Codes": angelernte und gesammelte Codes, nach Geraeten geordnet (HA-Store
             esphomeremote_konfig_ircodes). Erscheinen in derselben Suche.

Abgleich mit GitHub: je Quelle wird der Commit gemerkt, aus dem das Archiv stammt. Zweimal am Tag fragt HA
GitHub nach dem neuesten Commit (2 Anfragen, ohne Anmeldung) - der Konfigurator zeigt dann "Neu auf GitHub",
mit "Automatisch aktualisieren" laedt HA die neue Fassung selbst.
"""

from __future__ import annotations

import json
import logging
import re
import time
import zipfile
from datetime import timedelta
from pathlib import Path

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.event import async_call_later, async_track_time_interval
from homeassistant.helpers.storage import Store

_LOGGER = logging.getLogger(__name__)

MAX_ZIP = 1500 * 1024 * 1024
PRUEF_TAKT = timedelta(hours=12)

# Protokolle, die die Remote selbst kodieren kann (esphome/components/tasten_konfig/ir_encoders.h)
PROTOKOLLE = {
    "NEC", "NECext", "NEC1", "NEC5", "Onkyo", "NEC42", "NEC42ext", "SIRC", "SIRC15", "SIRC20", "RC5", "RC5X", "RC6",
    "Samsung32", "Kaseikyo", "Panasonic", "Denon", "Pioneer", "RCA", "JVC",
}
MAX_RAW = 1200   # Werte je Rohsignal (Klimaanlagen ~300-700)

QUELLEN = {
    "flipper": {"name": "Flipper-IRDB", "repo": "Lucaslhm/Flipper-IRDB", "zip": "flipper.zip", "index": "index.json",
                "lizenz": "frei (GitHub)", "text": "Gemeinschafts-Sammlung für den Flipper Zero, wird laufend gepflegt"},
    "harmony": {"name": "Logitech Harmony", "repo": "pickysysadmin/logitech-harmony-ir-archive", "zip": "harmony.zip",
                "index": "harmony.tsv", "lizenz": "CC0",
                "text": "Archiv der eingestellten Harmony-Fernbedienungen – sehr viele Marken und Modelle"},
}
# Logitech-Geraetearten (Harmony deviceType), soweit dokumentiert
HARMONY_ARTEN = {1: "TV", 2: "Videorekorder", 3: "CD", 4: "DVD / Blu-ray"}


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


def pronto_roh(pronto: str, wdh: int, keycode: str = "") -> tuple[list[int], int] | None:
    """Pronto-Hex (Format 0000, gelernt) -> (Rohsignal in us, +an/-aus; Traegerfrequenz). Wie die Harmony sendet:
    Vorspann einmal, dann der Wiederholteil wdh-mal (pressMinRepeats); ohne Vorspann mindestens einmal.
    Das Archiv legt einen reinen Wiederholteil (Keycode "G:<Protokoll>:()(<Code>)():..", z. B. Sony) in den ERSTEN
    Pronto-Abschnitt - dann ist der erste Abschnitt der Wiederholteil (Sony-Geraete reagieren erst ab 3 Rahmen)."""
    try:
        w = [int(x, 16) for x in pronto.split()]
    except ValueError:
        return None
    if len(w) < 6 or w[0] != 0 or w[1] == 0:
        return None
    einheit = w[1] * 0.241246   # us je Traegerperiode
    n1, n2 = w[2], w[3]
    seq1, seq2 = w[4:4 + 2 * n1], w[4 + 2 * n1:4 + 2 * n1 + 2 * n2]
    if len(seq1) != 2 * n1 or len(seq2) != 2 * n2 or not (seq1 or seq2):
        return None
    if not seq2 and _nur_wiederholung(keycode):
        seq1, seq2 = [], seq1
    n = max(0 if seq1 else 1, wdh) if seq2 else 0
    while n > 1 and len(seq1) + n * len(seq2) > MAX_RAW:
        n -= 1
    roh = [round(x * einheit) for x in seq1 + seq2 * n]
    return [v if i % 2 == 0 else -v for i, v in enumerate(roh) if v > 0], round(1_000_000 / einheit)


def _nur_wiederholung(keycode: str) -> bool:
    """Keycode G:<Protokoll>:(<Start>)(<Wiederholung>)(<Ende>):<Rest> - nur die Wiederholgruppe belegt?"""
    m = re.match(r"G:.*?:\(([^)]*)\)\(([^)]*)\)\(([^)]*)\)", keycode or "")
    return bool(m) and not m.group(1) and bool(m.group(2)) and not m.group(3)


class Quelle:
    """Ein heruntergeladenes GitHub-Archiv mit Suchindex."""

    def __init__(self, db: "IrDb", key: str) -> None:
        self.db, self.key, self.info = db, key, QUELLEN[key]
        self.zip = db.dir / self.info["zip"]
        self.idx_path = db.dir / self.info["index"]
        self.meta_path = db.dir / f"{key}_meta.json"
        self.meta: dict = {}
        self.job: dict | None = None
        self.geraete: list | None = None   # flipper: Liste von dicts, harmony: Liste von TSV-Zeilen
        self.such: list[str] = []          # klein geschriebener Suchtext je Geraet

    @property
    def installiert(self) -> bool:
        return self.geraete is not None

    def laeuft(self) -> bool:
        return bool(self.job) and self.job.get("phase") not in ("fertig", "fehler")

    # ------------------------------------------------------------ Laden
    def _laden(self) -> None:
        if not (self.idx_path.exists() and self.zip.exists()):
            return
        if self.key == "flipper":
            d = json.loads(self.idx_path.read_text())
            self.geraete, self.meta = d["geraete"], d["meta"]
            if self.meta_path.exists():   # Commit-Stand (seit 2026-10-28 getrennt gemerkt)
                self.meta.update(json.loads(self.meta_path.read_text()))
            self.such = [f"{g['k']} {g['m']} {g['n']}".lower() for g in self.geraete]
        else:
            zeilen = self.idx_path.read_text().splitlines()
            self.geraete = zeilen
            self.such = [" ".join(z.split("\t", 3)[:3]).lower() for z in zeilen]
            self.meta = json.loads(self.meta_path.read_text()) if self.meta_path.exists() else {}

    def status(self) -> dict:
        return {"name": self.info["name"], "repo": self.info["repo"], "text": self.info["text"],
                "lizenz": self.info["lizenz"], "installiert": self.installiert, "meta": self.meta, "job": self.job,
                "neu": self.db.einst.get("neu", {}).get(self.key), "auto": bool(self.db.einst.get("auto", {}).get(self.key))}

    # --------------------------------------------------------- Download
    async def herunterladen(self) -> None:
        self.job = {"phase": "laden", "pct": 0, "text": "Frage GitHub nach dem neuesten Stand …"}
        tmp = self.db.dir / (self.info["zip"] + ".tmp")
        try:
            self.db.dir.mkdir(parents=True, exist_ok=True)
            stand = await self.db.github_stand(self.key)
            url = f"https://codeload.github.com/{self.info['repo']}/zip/{stand['sha'] if stand else 'refs/heads/main'}"
            sess = async_get_clientsession(self.db.hass)
            async with sess.get(url, timeout=None) as r:
                r.raise_for_status()
                gesamt = int(r.headers.get("Content-Length") or 0)
                n = 0
                with open(tmp, "wb") as f:   # noqa: ASYNC230 - kleine Bloecke, Datei lokal
                    async for block in r.content.iter_chunked(1024 * 1024):
                        n += len(block)
                        if n > MAX_ZIP:
                            raise ValueError("Archiv zu gross")
                        await self.db.hass.async_add_executor_job(f.write, block)
                        self.job.update(pct=int(n * 100 / gesamt) if gesamt else -1,
                                        text=f"Lädt {n / 1048576:.0f} MB" + (f" von {gesamt / 1048576:.0f} MB" if gesamt else ""))
            self.job = {"phase": "index", "pct": -1, "text": "Baue Suchindex …"}
            await self.db.hass.async_add_executor_job(self._index_bauen, tmp, stand or {})
            self.db.einst.get("neu", {}).pop(self.key, None)
            await self.db.einst_speichern()
            self.job = {"phase": "fertig", "pct": 100, "text": f"{self.meta['anzahl']:,} Geräte installiert".replace(",", ".")}
        except Exception as err:   # noqa: BLE001 - Fehler an die Oberflaeche
            _LOGGER.warning("IR-Datenbank %s: %s", self.key, err)
            self.job = {"phase": "fehler", "pct": 0, "text": f"Fehler: {err}"}
            tmp.unlink(missing_ok=True)

    def _index_bauen(self, tmp: Path, stand: dict) -> None:
        if self.key == "flipper":
            geraete = self._index_flipper(tmp)
            anzahl, marken = len(geraete), len({(g["k"], g["m"]) for g in geraete})
        else:
            anzahl, marken = self._index_harmony(tmp)
        meta = {"quelle": self.info["repo"], "geladen": time.strftime("%Y-%m-%d %H:%M"), "anzahl": anzahl,
                "marken": marken, "mb": round(tmp.stat().st_size / 1048576, 1),
                "sha": stand.get("sha"), "stand": stand.get("datum")}
        tmp.replace(self.zip)
        if self.key == "flipper":
            self.idx_path.write_text(json.dumps({"meta": meta, "geraete": geraete}, ensure_ascii=False))
        self.meta_path.write_text(json.dumps(meta))
        self._laden()
        self.meta = meta

    def _index_flipper(self, tmp: Path) -> list[dict]:
        geraete = []
        with zipfile.ZipFile(tmp) as z:
            for info in z.infolist():
                if not info.filename.endswith(".ir") or info.file_size > 2 * 1024 * 1024:
                    continue
                teile = info.filename.split("/")[1:]   # erstes Element = Flipper-IRDB-<Stand>
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
        return geraete

    def _index_harmony(self, tmp: Path) -> tuple[int, int]:
        """harmony.tsv: Art, Marke, Modell, Id, Geraetedatei, Befehle, Wiederholungen - je Zeile ein Geraet mit Codes."""
        zeilen, marken = [], set()
        with zipfile.ZipFile(tmp) as z:
            namen = z.namelist()
            wurzel = namen[0].split("/")[0] + "/"
            befehle: dict[str, int] = {}
            sets = [n for n in namen if n.startswith(wurzel + "codesets/") and n.endswith(".json")]
            for i, n in enumerate(sets):
                befehle[n[len(wurzel):]] = z.read(n).count(b'"name"')
                if i % 2000 == 0:
                    self.job = {"phase": "index", "pct": int(i * 25 / max(1, len(sets))), "text": "Zählt Befehle …"}
            geraete = [n for n in namen if n.startswith(wurzel + "devices/") and n.endswith(".json")
                       and not n.endswith("/index.json")]
            for i, n in enumerate(geraete):
                if i % 2000 == 0:
                    self.job = {"phase": "index", "pct": 25 + int(i * 75 / max(1, len(geraete))),
                                "text": f"Liest Geräte … {i:,} von {len(geraete):,}".replace(",", ".")}
                try:
                    d = json.loads(z.read(n))
                except Exception:   # noqa: BLE001 - kaputte Einzeldatei ueberspringen
                    continue
                cs = d.get("codeset")
                if not cs or not befehle.get(cs):
                    continue
                marke = " ".join(str(d.get("manufacturer") or "").split())
                modell = " ".join(str(d.get("model") or "").split()) or "(ohne Modell)"
                art = HARMONY_ARTEN.get(d.get("deviceType"), "")
                pmr = (d.get("timing") or {}).get("pressMinRepeats")
                pmr = 3 if not isinstance(pmr, int) or pmr < 0 else min(pmr, 10)
                zeilen.append("\t".join(x.replace("\t", " ") for x in (
                    art, marke, modell, str(d.get("globalDeviceId", i)), n[len(wurzel):], str(befehle[cs]), str(pmr))))
                marken.add(marke.lower())
        zeilen.sort(key=lambda z: (z.split("\t")[1].lower(), z.split("\t")[2].lower()))
        self.idx_path.write_text("\n".join(zeilen))
        return len(zeilen), len(marken)

    def _loeschen(self) -> None:
        for p in (self.zip, self.idx_path, self.meta_path):
            p.unlink(missing_ok=True)
        self.geraete, self.such, self.meta, self.job = None, [], {}, None

    # ------------------------------------------------------------ Suche
    def suchen(self, woerter: list[str], limit: int) -> tuple[list[dict], int]:
        if self.geraete is None:
            return [], 0
        treffer, gesamt = [], 0
        for i, kopf in enumerate(self.such):
            if self.key == "flipper":
                g = self.geraete[i]
                if woerter and not all(w in kopf or w in g["t"].lower() for w in woerter):
                    continue
                gesamt += 1
                if len(treffer) < limit:
                    # Treffer im Namen vor Treffern nur in den Tastennamen
                    treffer.append({"id": i, "q": "flipper", "k": g["k"], "m": g["m"], "n": g["n"], "b": g["b"],
                                    "r": (0 if all(w in kopf for w in woerter) else 2) + ("_Converted_" in g["p"])})
            else:
                if woerter and not all(w in kopf for w in woerter):
                    continue
                gesamt += 1
                if len(treffer) < limit:
                    art, marke, modell, gid, _p, b, _w = self.geraete[i].split("\t")
                    treffer.append({"id": f"h{gid}", "q": "harmony", "k": art or "Harmony", "m": marke, "n": modell,
                                    "b": int(b), "r": 1})
        return treffer, gesamt

    def _geraet(self, gid: str) -> dict:
        if self.key == "flipper":
            i = int(gid)
            g = self.geraete[i]
            with zipfile.ZipFile(self.zip) as z:
                befehle = parse_ir(z.read(g["p"]).decode("utf-8", "replace"))
            return {"id": i, "q": "flipper", "k": g["k"], "m": g["m"], "n": g["n"], "befehle": befehle}
        such = "\t" + gid[1:] + "\t"
        zeile = next((z for z in self.geraete if such in z), None)
        if zeile is None:
            raise ValueError("Gerät nicht gefunden")
        art, marke, modell, _gid, pfad, _b, wdh = zeile.split("\t")
        with zipfile.ZipFile(self.zip) as z:
            wurzel = z.namelist()[0].split("/")[0] + "/"
            d = json.loads(z.read(wurzel + pfad))
            cs = json.loads(z.read(wurzel + d["codeset"]))
        befehle = []
        for c in cs.get("commands", []):
            r = pronto_roh(c["pronto"], int(wdh), c.get("keycode", "")) if c.get("pronto") else None
            name = str(c.get("name", ""))[:40]
            if r is None:
                befehle.append({"name": name, "ok": False, "proto": c.get("protocol", "?"), "addr": 0, "cmd": 0})
            else:
                befehle.append({"name": name, "raw": r[0], "f": r[1], "ok": 0 < len(r[0]) <= MAX_RAW,
                                "hp": c.get("protocol", "")})
        power = (d.get("power") or {}).get("type")
        return {"id": gid, "q": "harmony", "k": art or "Harmony", "m": marke, "n": modell, "befehle": befehle,
                **({"power": power} if power else {})}


class IrDb:
    def __init__(self, hass: HomeAssistant) -> None:
        self.hass = hass
        self.dir = Path(hass.config.path(".storage", "esphomeremote_konfig_irdb"))
        self.einst_path = self.dir / "einstellungen.json"
        self.einst: dict = {}   # {"auto": {quelle: bool}, "neu": {quelle: {sha, datum}}, "geprueft": "..."}
        self.quellen = {k: Quelle(self, k) for k in QUELLEN}
        self.eigen_store = Store(hass, 1, "esphomeremote_konfig_ircodes")
        self.eigen: dict = {"geraete": []}

    # ---------------------------------------------------------------- Laden
    def _laden(self) -> None:
        for q in self.quellen.values():
            try:
                q._laden()
            except Exception as err:   # noqa: BLE001 - kaputter Index: wie nicht installiert
                _LOGGER.warning("IR-Datenbank %s nicht lesbar: %s", q.key, err)
        if self.einst_path.exists():
            self.einst = json.loads(self.einst_path.read_text())

    async def laden(self) -> None:
        await self.hass.async_add_executor_job(self._laden)
        self.eigen = await self.eigen_store.async_load() or {"geraete": []}
        # GitHub-Abgleich: kurz nach dem Start und dann zweimal am Tag
        async_call_later(self.hass, 120, self._pruefen_takt)
        async_track_time_interval(self.hass, self._pruefen_takt, PRUEF_TAKT)

    async def einst_speichern(self) -> None:
        self.dir.mkdir(parents=True, exist_ok=True)
        await self.hass.async_add_executor_job(self.einst_path.write_text, json.dumps(self.einst))

    def status(self) -> dict:
        qs = {k: q.status() for k, q in self.quellen.items()}
        fl = self.quellen["flipper"]
        laeuft = next((q.job for q in self.quellen.values() if q.laeuft()), None)
        return {"installiert": any(q.installiert for q in self.quellen.values()), "meta": fl.meta,
                "job": laeuft or fl.job, "quellen": qs, "geprueft": self.einst.get("geprueft"),
                "eigen": sum(len(g.get("codes", [])) for g in self.eigen.get("geraete", []))}

    # ------------------------------------------------------ GitHub-Abgleich
    async def github_stand(self, key: str) -> dict | None:
        """Neuester Commit des Hauptzweigs: {sha, datum} (None, wenn GitHub nicht antwortet)."""
        try:
            sess = async_get_clientsession(self.hass)
            async with sess.get(f"https://api.github.com/repos/{QUELLEN[key]['repo']}/commits/HEAD",
                                headers={"Accept": "application/vnd.github+json"}, timeout=20) as r:
                if r.status != 200:
                    return None
                d = await r.json()
            return {"sha": d["sha"], "datum": d["commit"]["committer"]["date"][:10]}
        except Exception as err:   # noqa: BLE001
            _LOGGER.debug("GitHub-Stand %s: %s", key, err)
            return None

    async def pruefen(self) -> None:
        neu = self.einst.setdefault("neu", {})
        for key, q in self.quellen.items():
            if not q.installiert:
                neu.pop(key, None)
                continue
            st = await self.github_stand(key)
            if st is None:
                continue
            if not q.meta.get("sha") and str(q.meta.get("geladen", ""))[:10] >= st["datum"]:
                # vor 2026-10-28 geladen (ohne Commit gemerkt), aber nach dem letzten Commit: ist schon dieser Stand
                q.meta.update(sha=st["sha"], stand=st["datum"])
                await self.hass.async_add_executor_job(q.meta_path.write_text, json.dumps(q.meta))
            if st["sha"] != q.meta.get("sha"):
                neu[key] = st
                if self.einst.get("auto", {}).get(key) and not q.laeuft():
                    _LOGGER.info("IR-Datenbank %s: neuer Stand %s, aktualisiere", key, st["datum"])
                    self.hass.async_create_background_task(q.herunterladen(), f"esphomeremote_irdb_{key}")
            else:
                neu.pop(key, None)
        self.einst["geprueft"] = time.strftime("%Y-%m-%d %H:%M")
        await self.einst_speichern()

    async def _pruefen_takt(self, _jetzt=None) -> None:
        await self.pruefen()

    # ---------------------------------------------------------------- Suche
    def suchen(self, q: str, limit: int = 60, quelle: str | None = None) -> dict:
        woerter = [w for w in q.lower().split() if w]
        treffer, gesamt = [], 0
        if quelle in (None, "eigen"):
            for gi, g in enumerate(self.eigen.get("geraete", [])):
                kopf = f"{g.get('name', '')} meine codes eigene".lower()
                namen = " ".join(c.get("name", "") for c in g.get("codes", [])).lower()
                if woerter and not all(w in kopf or w in namen for w in woerter):
                    continue
                gesamt += 1
                treffer.append({"id": f"e{gi}", "q": "eigen", "k": "Meine Codes", "m": "", "n": g.get("name", ""),
                                "b": len(g.get("codes", [])), "r": -1})
        for key, qu in self.quellen.items():
            if quelle not in (None, key):
                continue
            t, n = qu.suchen(woerter, limit)
            treffer += t
            gesamt += n
        treffer.sort(key=lambda t: t["r"])
        return {"treffer": treffer[:limit], "gesamt": gesamt}

    def _geraet(self, gid) -> dict:
        s = str(gid)
        if s.startswith("e"):
            g = self.eigen["geraete"][int(s[1:])]
            befehle = [{k: v for k, v in c.items() if k in ("name", "proto", "addr", "cmd", "raw", "f")} | {"ok": True}
                       for c in g.get("codes", [])]
            return {"id": s, "q": "eigen", "k": "Meine Codes", "m": "", "n": g.get("name", ""), "befehle": befehle}
        qu = self.quellen["harmony" if s.startswith("h") else "flipper"]
        if not qu.installiert:
            raise ValueError(f"{qu.info['name']} ist nicht installiert")
        return qu._geraet(s)

    async def geraet(self, gid) -> dict:
        try:
            return await self.hass.async_add_executor_job(self._geraet, gid)
        except (IndexError, KeyError) as err:
            raise ValueError("Gerät nicht gefunden") from err


GERAET = vol.Any(int, str)
QUELLE = vol.In(list(QUELLEN))
CODE = vol.Schema({vol.Required("name"): str, vol.Optional("proto"): str, vol.Optional("addr"): int,
                   vol.Optional("cmd"): int, vol.Optional("raw"): [int], vol.Optional("f"): int,
                   vol.Optional("quelle"): str, vol.Optional("zeit"): str}, extra=vol.REMOVE_EXTRA)
EIGEN = vol.Schema({vol.Required("geraete"): [vol.Schema({vol.Required("name"): str,
                                                          vol.Optional("codes", default=list): [CODE]},
                                                         extra=vol.REMOVE_EXTRA)]}, extra=vol.REMOVE_EXTRA)


# ------------------------------------------------------------------ Websocket
def registrieren(hass: HomeAssistant, db: IrDb) -> None:
    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_status"})
    @callback
    def ws_status(hass, connection, msg):
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_laden",
                                      vol.Optional("quelle", default="flipper"): QUELLE})
    @websocket_api.require_admin
    @callback
    def ws_laden(hass, connection, msg):
        q = db.quellen[msg["quelle"]]
        if not q.laeuft():
            q.job = {"phase": "laden", "pct": 0, "text": "Startet …"}
            hass.async_create_background_task(q.herunterladen(), f"esphomeremote_irdb_{q.key}")
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_loeschen",
                                      vol.Optional("quelle", default="flipper"): QUELLE})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_loeschen(hass, connection, msg):
        await hass.async_add_executor_job(db.quellen[msg["quelle"]]._loeschen)
        db.einst.get("neu", {}).pop(msg["quelle"], None)
        await db.einst_speichern()
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_pruefen"})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_pruefen(hass, connection, msg):
        await db.pruefen()
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/irdb_auto",
                                      vol.Required("quelle"): QUELLE, vol.Required("an"): bool})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_auto(hass, connection, msg):
        db.einst.setdefault("auto", {})[msg["quelle"]] = msg["an"]
        await db.einst_speichern()
        connection.send_result(msg["id"], db.status())

    @websocket_api.websocket_command(
        {vol.Required("type"): "esphomeremote_konfig/irdb_suchen", vol.Optional("q", default=""): str,
         vol.Optional("limit", default=60): vol.All(int, vol.Range(1, 200)),
         vol.Optional("quelle"): vol.In([*QUELLEN, "eigen"])}
    )
    @websocket_api.async_response
    async def ws_suchen(hass, connection, msg):
        # im Executor: die Suche laeuft ueber den ganzen Index (bis ~280 000 Geraete)
        connection.send_result(msg["id"], await hass.async_add_executor_job(
            db.suchen, msg["q"], msg["limit"], msg.get("quelle")))

    @websocket_api.websocket_command(
        {vol.Required("type"): "esphomeremote_konfig/irdb_geraet", vol.Required("geraet"): GERAET}
    )
    @websocket_api.async_response
    async def ws_geraet(hass, connection, msg):
        try:
            connection.send_result(msg["id"], await db.geraet(msg["geraet"]))   # "id" ist die Nachrichtennummer!
        except (ValueError, KeyError, OSError, zipfile.BadZipFile) as err:
            connection.send_error(msg["id"], "irdb", str(err))

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/ircodes_get"})
    @callback
    def ws_eigen_get(hass, connection, msg):
        connection.send_result(msg["id"], db.eigen)

    @websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/ircodes_save",
                                      vol.Required("daten"): dict})
    @websocket_api.require_admin
    @websocket_api.async_response
    async def ws_eigen_save(hass, connection, msg):
        try:
            daten = EIGEN(msg["daten"])
        except vol.Invalid as err:
            connection.send_error(msg["id"], "ungueltig", str(err))
            return
        db.eigen = daten
        await db.eigen_store.async_save(daten)
        connection.send_result(msg["id"], {"ok": True})

    for f in (ws_status, ws_laden, ws_loeschen, ws_pruefen, ws_auto, ws_suchen, ws_geraet, ws_eigen_get, ws_eigen_save):
        websocket_api.async_register_command(hass, f)
