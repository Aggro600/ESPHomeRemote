#!/usr/bin/env python3
"""Dateien auf die SD-Karte einer Remote hochladen, ueber den Konfig-Modus-Endpunkt (sd_http.cpp).

Ersetzt Ad-hoc-curl-Aufrufe: setzt den Content-Type korrekt (application/octet-stream - sonst
versucht der Geraete-HTTP-Server, den Koerper als Formular zu zerlegen, s. Kommentar in
sd_http.cpp), schaltet den Konfig-Modus selbst an/aus und meldet jede Datei einzeln.

    python3 tools/sd_upload.py <ip> logos              # alle Logos (covers/ + icons/Custom/) hochladen
    python3 tools/sd_upload.py <ip> <lokale-datei> <sd-pfad>   # eine einzelne Datei
    python3 tools/sd_upload.py <ip> --list [pfad]      # Inhalt eines Ordners auflisten
    python3 tools/sd_upload.py <ip> --delete <sd-pfad> # Datei loeschen (ueber POST /delete -
                                                        #  DELETE als HTTP-Methode kommt durch
                                                        #  ESPHomes web_server_base fuer ESP-IDF
                                                        #  nicht durch, 405 vor jedem Handler)

Braucht keinen HA-Umweg: schaltet den Schalter "Konfig-Modus (HTTP)" direkt ueber die ESPHome-
API des Geraets, dieselbe, die auch Home Assistant benutzt - dafuer reicht die IP, ein
Verschluesselungs-Key ist fuer reine HTTP-Zugriffe auf /sd/api/ nicht noetig (die API-Verbindung
selbst brauchen wir dafuer nicht, wir schalten den Schalter stattdessen ueber die HA-REST-API).
"""
import json
import os
import sys
import time
import urllib.request
import urllib.error

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD_KARTE = os.path.join(HERE, "sd-karte")


def ha_service(service_path: str, entity_id: str | None, daten: dict | None = None) -> None:
    token = os.environ.get("SUPERVISOR_TOKEN")
    if not token:
        print("Kein SUPERVISOR_TOKEN - Konfig-Modus muss von Hand in Home Assistant angeschaltet werden.")
        return
    daten = dict(daten or {})
    if entity_id:
        daten["entity_id"] = entity_id
    req = urllib.request.Request(
        f"http://supervisor/core/api/services/{service_path}",
        data=json.dumps(daten).encode(),
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        method="POST",
    )
    urllib.request.urlopen(req, timeout=10).read()


def konfig_modus(ip: str, remote_name: str, an: bool) -> None:
    # Dienst statt Schalter (seit 2026-10-18 gibt es den Schalter in HA nicht mehr)
    ha_service(f"esphome/remote_wz_{remote_name}_konfig_http", None, {"an": an})
    if an:
        time.sleep(3)  # SD mounten + HTTP-Listener starten braucht einen Moment


def remote_name_from_ip(ip: str) -> str:
    return {"192.168.1.60": "couch", "192.168.1.69": "wohnzimmer"}.get(ip, "")


def http(method: str, path: str, ip: str, body: bytes = None, content_type: str = None):
    url = f"http://{ip}{path}"
    headers = {"Content-Type": content_type} if content_type else {}
    req = urllib.request.Request(url, data=body, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return r.status, r.read().decode(errors="replace")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode(errors="replace")


def upload_file(ip: str, local_path: str, sd_path: str) -> bool:
    with open(local_path, "rb") as f:
        data = f.read()
    # application/octet-stream statt des curl-/urllib-Standards: sonst versucht der Geraete-
    # HTTP-Server, den Koerper als application/x-www-form-urlencoded zu zerlegen (s. sd_http.cpp)
    # - bei mehr als ~1 KB scheitert das mit "Bad request syntax", darunter mit einer Fehlantwort.
    status, body = http("POST", f"/sd/api/file?path={sd_path}", ip, data, "application/octet-stream")
    ok = status == 200
    print(f"  {'OK  ' if ok else 'FEHL'} {sd_path} ({len(data)} B) -> {status} {body if not ok else ''}".rstrip())
    return ok


def delete_file(ip: str, sd_path: str) -> bool:
    # Echtes HTTP DELETE kommt durch ESPHomes web_server_base fuer ESP-IDF nicht durch (405,
    # noch vor jedem eigenen Handler) - deshalb der eigene Weg ueber POST /delete (sd_http.cpp).
    status, body = http("POST", f"/sd/api/delete?path={sd_path}", ip, b"", "application/octet-stream")
    ok = status == 200
    print(f"  {'OK  ' if ok else 'FEHL'} loeschen {sd_path} -> {status} {body if not ok else ''}".rstrip())
    return ok


def upload_logos(ip: str) -> None:
    files = []
    for sub in ("covers", os.path.join("icons", "Custom")):
        d = os.path.join(SD_KARTE, sub)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith("_preview.png"):
                continue  # nur zur lokalen Ansicht, kein Geraete-Asset (s. make_sd_assets.py)
            local = os.path.join(d, name)
            if os.path.isfile(local):
                files.append((local, "/" + sub.replace(os.sep, "/") + "/" + name))
    if not files:
        print(f"Keine Dateien unter {SD_KARTE}/covers oder .../icons/Custom gefunden - "
              f"erst tools/make_sd_assets.py laufen lassen.")
        return
    print(f"{len(files)} Dateien -> {ip}")
    ok = sum(upload_file(ip, local, sd_path) for local, sd_path in files)
    print(f"fertig: {ok}/{len(files)} ok")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    ip = sys.argv[1]
    name = remote_name_from_ip(ip)
    if not name:
        print(f"Unbekannte IP {ip} - erwartet 192.168.1.60 (Remote 1) oder 192.168.1.69 (Remote 2).")
        return 2

    konfig_modus(ip, name, True)
    try:
        if len(sys.argv) >= 3 and sys.argv[2] == "logos":
            upload_logos(ip)
        elif len(sys.argv) >= 3 and sys.argv[2] == "--list":
            path = sys.argv[3] if len(sys.argv) > 3 else "/"
            status, body = http("GET", f"/sd/api/list?path={path}", ip)
            print(body if status == 200 else f"Fehler {status}: {body}")
        elif len(sys.argv) >= 4 and sys.argv[2] == "--delete":
            delete_file(ip, sys.argv[3])
        elif len(sys.argv) >= 4:
            upload_file(ip, sys.argv[2], sys.argv[3])
        else:
            print(__doc__)
            return 2
    finally:
        konfig_modus(ip, name, False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
