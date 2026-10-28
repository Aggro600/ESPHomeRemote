"""Akku-Auswertung je Remote aus dem Recorder (2026-10-19, Konfigurator-Seite "Akku").

Verbrauch je Zustand (Tiefschlaf, wach nur Bluetooth, wach mit WLAN), ohne Ladezeiten, Anteil Tiefschlaf, die Gruende
aus dem Sensor "Tiefschlaf-Status" (warum die Remote wach blieb) und die geschaetzte Laufzeit bei diesem Gebrauch.
Rechnung wie die Handauswertung vom 2026-10-19: zwischen zwei Akkuwerten ohne Ladekabel; liegt dazwischen
"nicht erreichbar" (Tiefschlaf), zaehlt der Abschnitt als Tiefschlaf.
"""

from __future__ import annotations

from bisect import bisect_right
from datetime import timedelta

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.util import dt as dt_util

AUS = ("unavailable", "unknown", None, "")


def _reihe(states) -> list[tuple[float, str]]:
    out = []
    for st in states or []:
        if isinstance(st, dict):
            t, v = st.get("last_changed"), st.get("state")
            t = dt_util.parse_datetime(t) if isinstance(t, str) else t
        else:
            t, v = st.last_changed, st.state
        if t is not None:
            out.append((t.timestamp(), v))
    return out


def auswerten(reihen: dict[str, list[tuple[float, str]]], ende: float) -> dict:
    ak = reihen.get("akku", [])
    def zu(k, t):
        s = reihen.get(k, [])
        i = bisect_right([a for a, _ in s], t) - 1
        return s[i][1] if i >= 0 else None

    werte = [(t, float(v)) for t, v in ak if v not in AUS and _zahl(v)]
    # Nach dem Laden korrigiert der Akkuchip seinen Prozentwert noch eine Weile (auch bei Teilladungen) - diese
    # Minuten nicht als Verbrauch zaehlen
    ladungen = _ladungen(reihen.get("lade", []), ende)
    lade_beginn = [a for a, _ in ladungen]
    def in_ladung(t):
        i = bisect_right(lade_beginn, t) - 1
        return i >= 0 and t < ladungen[i][1]
    def kurz_nach_laden(t):
        i = bisect_right(lade_beginn, t) - 1
        return i >= 0 and 0 <= t - ladungen[i][1] < 1800   # 2026-10-25: 15 -> 30 min (Chip korrigiert laenger)
    def geladen_zwischen(t0, t1):
        # Zwischenladen (2026-10-25): auch ein kurzes Laden MITTEN im Abschnitt (z. B. waehrend die Remote schlief)
        # schliesst ihn aus - vorher wurde nur Anfang und Ende geprueft, der Abschnitt zaehlte mit zu wenig Verbrauch
        i = bisect_right(lade_beginn, t1) - 1
        return i >= 0 and ladungen[i][1] > t0
    zustaende: dict[str, list[float]] = {}   # name -> [prozent, stunden]
    ausgelassen = 0.0   # Stunden, die wegen Laden nicht zaehlen
    for (t0, a0), (t1, a1) in zip(werte, werte[1:]):
        h = (t1 - t0) / 3600
        if (h <= 0 or in_ladung(t0) or in_ladung(t1) or geladen_zwischen(t0, t1)
                or a1 > a0 + 0.5 or kurz_nach_laden(t0)):
            ausgelassen += h
            continue
        # Tiefschlaf: nicht erreichbar dazwischen, oder (Dock-Betrieb, Wert bleibt stehen) Status "Tiefschlaf"
        weg = any(t0 < t < t1 and v in AUS for t, v in ak) or zu("grund", t0) == "Tiefschlaf"
        if weg and h > 0.2:
            k = "tiefschlaf"
        elif h > 3:
            continue
        else:
            k = "wach_wlan" if zu("wlan", t0) == "on" else "wach_ble"
        z = zustaende.setdefault(k, [0.0, 0.0])
        z[0] += a0 - a1
        z[1] += h

    # Zeitanteile (gesamter Zeitraum): laden / Tiefschlaf (nicht erreichbar) / wach
    anteile = {"laden": 0.0, "tiefschlaf": 0.0, "wach": 0.0}
    if ak:
        punkte = sorted({t for t, _ in ak} | {t for t, _ in reihen.get("lade", [])})
        punkte.append(ende)
        for t0, t1 in zip(punkte, punkte[1:]):
            h = (t1 - t0) / 3600
            if in_ladung(t0):
                anteile["laden"] += h
            elif zu("akku", t0) in AUS or zu("grund", t0) == "Tiefschlaf":
                anteile["tiefschlaf"] += h
            else:
                anteile["wach"] += h

    # Gruende aus "Tiefschlaf-Status" (nur Zeiten, in denen die Remote erreichbar war)
    gruende: dict[str, float] = {}
    gs = reihen.get("grund", [])
    for (t0, v), (t1, _) in zip(gs, gs[1:] + [(ende, None)]):
        if v in AUS:
            continue
        k = "wartet (Zeit läuft)" if str(v).startswith("wartet") else str(v)
        gruende[k] = gruende.get(k, 0.0) + (t1 - t0) / 3600

    # Durchschnitt: Verbrauch wach bzw. im Tiefschlaf, gewichtet mit der echten Zeit in diesen Zustaenden (die
    # Messabschnitte allein unterschaetzen lange Schlafphasen)
    wp = sum(zustaende[k][0] for k in ("wach_wlan", "wach_ble") if k in zustaende)
    wh = sum(zustaende[k][1] for k in ("wach_wlan", "wach_ble") if k in zustaende)
    rate_wach = wp / wh if wh > 0 else None
    rate_tief = zustaende["tiefschlaf"][0] / zustaende["tiefschlaf"][1] if zustaende.get("tiefschlaf", [0, 0])[1] > 0 else 0.15
    schnitt = None
    if rate_wach is not None and anteile["wach"] + anteile["tiefschlaf"] > 0:
        schnitt = (rate_wach * anteile["wach"] + rate_tief * anteile["tiefschlaf"]) / (anteile["wach"] + anteile["tiefschlaf"])
    return {
        "zustaende": {k: {"prozent": round(p, 1), "stunden": round(h, 1), "pro_stunde": round(p / h, 2) if h > 0 else None}
                      for k, (p, h) in zustaende.items()},
        "anteile": {k: round(v, 1) for k, v in anteile.items()},
        "gruende": sorted(({"grund": k, "stunden": round(v, 1)} for k, v in gruende.items() if v >= 0.05),
                          key=lambda x: -x["stunden"])[:8],
        "schnitt_pro_stunde": round(schnitt, 2) if schnitt else None,
        "pro_tag": round(schnitt * 24, 1) if schnitt else None,
        "ladevorgaenge": len(ladungen),
        "ausgelassen_stunden": round(ausgelassen, 1),
        "laufzeit_tage": round(100 / schnitt / 24, 1) if schnitt else None,
        "schlafquote": round(100 * anteile["tiefschlaf"] / (anteile["wach"] + anteile["tiefschlaf"])) if anteile["wach"] + anteile["tiefschlaf"] > 0 else None,
        "akku_jetzt": werte[-1][1] if werte else None,
    }


def _ladungen(lade: list[tuple[float, str]], ende: float) -> list[tuple[float, float]]:
    """Ladezeiten [(von, bis)] aus dem Ladekabel-Sensor, entprellt (2026-10-25): das Signal flattert in der Ladeschale
    (Remote 1: 1600 Ein-Phasen in 3 Tagen, meist unter 10 s). Luecken unter 2 min gehoeren zum selben Ladevorgang,
    einzelne Ausschlaege unter 20 s zaehlen nicht. "Nicht erreichbar" beendet eine Ladung nicht (Remote schlaeft am
    Kabel nicht, das Signal fehlt nur kurz)."""
    roh: list[list[float]] = []
    an = None
    for t, v in lade:
        if v == "on" and an is None:
            an = t
        elif v == "off" and an is not None:
            roh.append([an, t])
            an = None
    if an is not None:
        roh.append([an, ende])
    zusammen: list[list[float]] = []
    for a, b in roh:
        if zusammen and a - zusammen[-1][1] < 120:
            zusammen[-1][1] = max(zusammen[-1][1], b)
        else:
            zusammen.append([a, b])
    return [(a, b) for a, b in zusammen if b - a >= 20]


def _zahl(v) -> bool:
    try:
        float(v)
        return True
    except (TypeError, ValueError):
        return False


@websocket_api.websocket_command({vol.Required("type"): "esphomeremote_konfig/akku_auswertung",
                                  vol.Required("device"): str, vol.Optional("tage", default=3): vol.All(int, vol.Range(1, 20))})
@websocket_api.async_response
async def ws_akku_auswertung(hass, connection, msg) -> None:
    from homeassistant.components.recorder import get_instance, history
    slug = msg["device"].replace("-", "_")
    ids = {"akku": f"sensor.{slug}_akkustand", "lade": f"binary_sensor.{slug}_ladekabel",
           "wlan": f"binary_sensor.{slug}_wlan_aktiv", "grund": f"sensor.{slug}_tiefschlaf_status"}
    ende = dt_util.utcnow()
    start = ende - timedelta(days=msg["tage"])
    roh = await get_instance(hass).async_add_executor_job(
        lambda: history.get_significant_states(hass, start, ende, list(ids.values()), None, True, False, False, True))
    reihen = {k: _reihe(roh.get(e)) for k, e in ids.items()}
    erg = auswerten(reihen, ende.timestamp())
    erg["tage"] = msg["tage"]
    erg["seit"] = start.isoformat()
    connection.send_result(msg["id"], erg)


def registrieren(hass) -> None:
    websocket_api.async_register_command(hass, ws_akku_auswertung)
