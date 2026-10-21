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
    lade_ende = [t for (t, v), (_, v0) in zip(reihen.get("lade", [])[1:], reihen.get("lade", [])) if v == "off" and v0 == "on"]
    def kurz_nach_laden(t):
        i = bisect_right(lade_ende, t) - 1
        return i >= 0 and t - lade_ende[i] < 900
    zustaende: dict[str, list[float]] = {}   # name -> [prozent, stunden]
    for (t0, a0), (t1, a1) in zip(werte, werte[1:]):
        h = (t1 - t0) / 3600
        if h <= 0 or zu("lade", t0) == "on" or zu("lade", t1) == "on" or a1 > a0 + 0.5 or kurz_nach_laden(t0):
            continue
        weg = any(t0 < t < t1 and v in AUS for t, v in ak)
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
            if zu("lade", t0) == "on":
                anteile["laden"] += h
            elif zu("akku", t0) in AUS:
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
        "laufzeit_tage": round(100 / schnitt / 24, 1) if schnitt else None,
        "schlafquote": round(100 * anteile["tiefschlaf"] / (anteile["wach"] + anteile["tiefschlaf"])) if anteile["wach"] + anteile["tiefschlaf"] > 0 else None,
        "akku_jetzt": werte[-1][1] if werte else None,
    }


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
