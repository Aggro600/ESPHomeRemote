#!/usr/bin/env python3
"""Alle Fernbedienungs-YAMLs werden aus den Vorlagen in templates/ ERZEUGT (seit 2026-10-16 neutral,
vorher war die erste Remote Datei die Vorlage).

  templates/remote.yaml         -> remote-wz-<kennung>.yaml
  templates/remote-halink.yaml  -> remote-wz-<kennung>-halink.yaml (Bluetooth-Schicht, die geflasht wird)

Nur die Vorlagen werden von Hand gepflegt, KEINE remote-wz-*.yaml. Platzhalter in Code-Zeilen:
remotename -> <kennung>, Remotename -> <Name>, REMOTENAME -> <KENNUNG>. Welche Remotes es gibt, steht in
tools/remotes.json (alle, auch Remote 1). Neue Remotes legt der Konfigurator in HA an
("Neue Fernbedienung"), er ruft dafuer dieses Skript auf.

    python3 tools/remotes.py                       # alle erzeugten Dateien neu schreiben
    python3 tools/remotes.py --check               # nur pruefen (Exit 1 = eine ist veraltet)
    python3 tools/remotes.py --neu "Gast" 192.168.1.70   # neue Remote eintragen + Datei + IP-Secret
    python3 tools/remotes.py --neu "Gast" dhcp           # erst IP vom Router (ohne manual_ip) ...
    python3 tools/remotes.py --fest gast 192.168.1.57    # ... dann die bekommene IP festschreiben
    python3 tools/remotes.py --entfernen gast             # wieder austragen (Datei + Secret weg)
    python3 tools/remotes.py --entwickler an|aus           # Testwerkzeuge (remote-dev-tests.yaml) mitbauen?

Entwicklermodus (2026-10-18, Schalter im Konfigurator): steht in tools/entwickler.json ({"an": true}), fehlt die Datei,
ist er AN. Aus = die Zeile mit remote-dev-tests.yaml in der -halink-Vorlage wird beim Erzeugen auskommentiert.

Kommentarzeilen bleiben unveraendert.
"""
import difflib
import json
import re
import sys
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
SRC = HERE / "templates" / "remote.yaml"
SRC_HALINK = HERE / "templates" / "remote-halink.yaml"
LISTE = Path(__file__).resolve().parent / "remotes.json"
ENTWICKLER = Path(__file__).resolve().parent / "entwickler.json"
DEV_PAKET = "remote-dev-tests.yaml"
SECRETS = HERE / "secrets.yaml"
MAX_NAME = 14   # WLAN-Fallback-SSID "Remote WZ <Name> Fallback" hoechstens 32 Zeichen


def kennung(name: str) -> str:
    """'Gäste Zimmer' -> 'gaestezimmer' (nur a-z0-9, passt in Hostnamen, Entity-IDs und C++)."""
    s = name.strip().lower().replace("ä", "ae").replace("ö", "oe").replace("ü", "ue").replace("ß", "ss")
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-z0-9]", "", s)


def ohne_feste_ip(text: str) -> str:
    """manual_ip-Block entfernen (erster Start mit IP vom Router)."""
    out, im = [], False
    for line in text.splitlines(keepends=True):
        if line.startswith("  manual_ip:"):
            im = True
            out.append("  # manual_ip: kommt nach dem ersten Start (IP vom Router wird festgeschrieben, tools/remotes.py --fest)\n")
            continue
        if im and line.startswith("    "):
            continue
        im = False
        out.append(line)
    return "".join(out)


def entwickler() -> bool:
    try:
        return bool(json.loads(ENTWICKLER.read_text(encoding="utf-8")).get("an", True))
    except (OSError, ValueError):
        return True


def convert(text: str, name: str, slug: str, dhcp: bool = False, quelle: str = "templates/remote.yaml") -> str:
    header = (f"# GENERIERT aus {quelle} durch tools/remotes.py - "
              f"NICHT von Hand bearbeiten (Aenderungen in der Vorlage machen).\n")
    dev = entwickler()
    out = []
    for line in text.splitlines(keepends=True):
        s = line.lstrip()
        if s.startswith("#") or s.startswith("//"):
            out.append(line)          # Kommentare bleiben wie sie sind
            continue
        if not dev and DEV_PAKET in line:   # Entwicklermodus aus: Testwerkzeuge nicht mitbauen
            out.append(line[:len(line) - len(s)] + "# " + s.rstrip("\n") + "   # Entwicklermodus aus\n")
            continue
        out.append(line.replace("REMOTENAME", slug.upper()).replace("Remotename", name).replace("remotename", slug))
    return header + (ohne_feste_ip("".join(out)) if dhcp else "".join(out))


def liste() -> list:
    if LISTE.exists():
        return json.loads(LISTE.read_text(encoding="utf-8"))
    return [{"name": "Remote 1", "slug": "couch"}, {"name": "Remote 2", "slug": "wohnzimmer"}]


def ziel(slug: str) -> Path:
    return HERE / f"remote-wz-{slug}.yaml"


def ziel_halink(slug: str) -> Path:
    return HERE / f"remote-wz-{slug}-halink.yaml"


def paare(r: dict) -> list:
    """(Vorlage, Ziel, dhcp) fuer eine Remote: Grunddatei und Bluetooth-Schicht."""
    p = [(SRC, ziel(r["slug"]), r.get("dhcp", False))]
    if SRC_HALINK.exists():
        p.append((SRC_HALINK, ziel_halink(r["slug"]), False))
    return p


def erzeugen(check: bool = False) -> int:
    rc = 0
    for r, (quelle, dst, dhcp) in ((r, p) for r in liste() for p in paare(r)):
        new = convert(quelle.read_text(encoding="utf-8"), r["name"], r["slug"], dhcp, f"templates/{quelle.name}")
        old = dst.read_text(encoding="utf-8") if dst.exists() else ""
        if new == old:
            print(f"OK: {dst.name} ist aktuell.")
            continue
        diff = list(difflib.unified_diff(old.splitlines(), new.splitlines(), "aktuell", "neu", lineterm="", n=0))
        n = sum(1 for l in diff if l[:1] in "+-" and l[:3] not in ("+++", "---"))
        if check:
            print(f"{dst.name} weicht ab: {n} Zeilen")
            rc = 1
            continue
        dst.write_text(new, encoding="utf-8")
        print(f"{dst.name} neu erzeugt ({n} Zeilen geaendert).")
    return rc


def neu(name: str, ip: str) -> int:
    name = name.strip()
    slug = kennung(name)
    if not slug or len(name) > MAX_NAME:
        print(f"FEHLER: Name leer oder laenger als {MAX_NAME} Zeichen"); return 2
    dhcp = ip.lower() in ("", "dhcp", "auto")
    if not dhcp and not re.fullmatch(r"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}", ip):
        print("FEHLER: IP-Adresse ungueltig"); return 2
    rs = liste()
    if slug == "remotename" or any(r["slug"] == slug for r in rs):
        print(f"FEHLER: Eine Remote '{slug}' gibt es schon"); return 2
    if dhcp:
        rs.append({"name": name, "slug": slug, "dhcp": True})
        LISTE.write_text(json.dumps(rs, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        erzeugen()
        print(f"remote-wz-{slug}")
        return 0
    return _ip_eintragen(name, slug, ip, rs, neu=True)


def _ip_eintragen(name: str, slug: str, ip: str, rs: list, neu: bool) -> int:
    sec = SECRETS.read_text(encoding="utf-8")
    if re.search(rf"^\S+:\s*{re.escape(ip)}\s*$", sec, re.M):
        print(f"FEHLER: IP {ip} ist schon vergeben"); return 2
    key = f"ip_remote_wz_{slug}"
    if re.search(rf"^{key}:", sec, re.M):
        sec = re.sub(rf"^{key}:.*$", f"{key}: {ip}", sec, flags=re.M)
        SECRETS.write_text(sec, encoding="utf-8")
    else:
        SECRETS.write_text(sec.rstrip("\n") + f"\n{key}: {ip}\n", encoding="utf-8")
    if neu:
        rs.append({"name": name, "slug": slug})
    else:
        for r in rs:
            if r["slug"] == slug:
                r.pop("dhcp", None)
    LISTE.write_text(json.dumps(rs, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    erzeugen()
    print(f"remote-wz-{slug}")
    return 0


def fest(slug: str, ip: str) -> int:
    """IP, die die Remote vom Router bekommen hat, als feste IP eintragen."""
    rs = liste()
    r = next((x for x in rs if x["slug"] == slug), None)
    if r is None:
        print(f"FEHLER: Remote '{slug}' unbekannt"); return 2
    if not re.fullmatch(r"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}", ip):
        print("FEHLER: IP-Adresse ungueltig"); return 2
    return _ip_eintragen(r["name"], slug, ip, rs, neu=False)


def entfernen(slug: str) -> int:
    """Erzeugte Remote austragen: Liste, YAML-Datei und IP-Secret (Remote 1 nie)."""
    rs = liste()
    rs2 = [r for r in rs if r["slug"] != slug]
    LISTE.write_text(json.dumps(rs2, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    for z in (ziel(slug), ziel_halink(slug)):
        if z.exists():
            z.unlink()
    sec = SECRETS.read_text(encoding="utf-8")
    SECRETS.write_text(re.sub(rf"^ip_remote_wz_{slug}:.*\n?", "", sec, flags=re.M), encoding="utf-8")
    print(f"remote-wz-{slug} entfernt")
    return 0


def entwickler_setzen(an: bool) -> int:
    ENTWICKLER.write_text(json.dumps({"an": an}) + "\n", encoding="utf-8")
    print(f"Entwicklermodus {'an' if an else 'aus'}")
    return erzeugen()


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["--entwickler"] and len(a) >= 2 and a[1] in ("an", "aus"):
        raise SystemExit(entwickler_setzen(a[1] == "an"))
    if a[:1] == ["--entfernen"] and len(a) >= 2:
        raise SystemExit(entfernen(a[1]))
    if a[:1] == ["--fest"] and len(a) >= 3:
        raise SystemExit(fest(a[1], a[2]))
    if a[:1] == ["--neu"] and len(a) >= 3:
        raise SystemExit(neu(a[1], a[2]))
    raise SystemExit(erzeugen(check="--check" in a))
