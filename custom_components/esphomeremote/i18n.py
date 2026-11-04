"""Sprache fuer Texte der Integration (2026-11-01, Nutzer: "alles lokalisieren, nach der HA-Sprache, Fallback Englisch").

Der Code bleibt deutsch. Ist Home Assistant nicht auf Deutsch, uebersetzt t() ueber dasselbe Woerterbuch wie der
Konfigurator (frontend/i18n/<sprache>.json: deutsches Stueck -> Uebersetzung): erst der ganze Text, sonst die
bekannten Stuecke darin (laengste zuerst, ein Durchgang). Fuer Entitaetsnamen, Benachrichtigungen und Texte,
die HA an die Remotes schickt.
"""
from __future__ import annotations

import json
from pathlib import Path
import re

from homeassistant.core import HomeAssistant

_PFAD = Path(__file__).parent / "frontend" / "i18n"
_cache: dict[str, tuple[dict[str, str], re.Pattern | None, list]] = {}
_HASS: HomeAssistant | None = None


def setzen(hass: HomeAssistant) -> None:
    """Beim Start: HA merken, damit Entitaeten ohne eigenes hass (Konstruktor) uebersetzen koennen."""
    global _HASS
    _HASS = hass


async def laden(hass: HomeAssistant) -> None:
    """Woerterbuch einmal im Executor laden (2026-11-04, HACS-Pruefung: read_text() lief beim ersten t() in der
    Ereignisschleife). Muss beim Einrichten vor allen Plattformen laufen; t() liest danach nie mehr selbst."""
    setzen(hass)
    sp = sprache(hass)
    if sp != "de" and sp not in _cache:
        await hass.async_add_executor_job(_laden, sp)


def sprache(hass: HomeAssistant | None) -> str:
    l = str(getattr(getattr(hass, "config", None), "language", "") or "en").lower()
    return "de" if l.startswith("de") else "en"   # weitere Sprachen: Datei i18n/<code>.json, hier aufnehmen


def _laden(sp: str):
    if sp not in _cache:
        try:
            wb = json.loads((_PFAD / f"{sp}.json").read_text(encoding="utf-8"))
        except (OSError, ValueError):
            wb = {}
        regeln = [(re.compile(m), re.sub(r"\$(\d)", r"\\\1", e)) for m, e in wb.pop("__regeln", [])]
        keys = sorted((k for k, v in wb.items() if v and v != k), key=len, reverse=True)

        def teil(k: str) -> str:
            a = r"(?<![^\W_])" if re.match(r"\w", k) else ""
            b = r"(?![^\W_])" if re.search(r"\w$", k) else ""
            return a + re.escape(k) + b
        _cache[sp] = (wb, re.compile("|".join(teil(k) for k in keys)) if keys else None, regeln)
    return _cache[sp]


def t(hass: HomeAssistant | None, text: str) -> str:
    """Deutschen Text in die Sprache von HA (Deutsch: unveraendert)."""
    sp = sprache(hass or _HASS)
    if sp == "de" or not text:
        return text
    if sp not in _cache:   # noch nicht geladen (laden() laeuft beim Einrichten): nie in der Ereignisschleife lesen
        return text
    wb, muster, regeln = _cache[sp]
    s = re.sub(r"\s+", " ", str(text))
    for m, e in regeln:
        s = m.sub(e, s)
    if s.strip() in wb:
        return s.replace(s.strip(), wb[s.strip()])
    return muster.sub(lambda m: wb.get(m.group(0), m.group(0)), s) if muster else s
