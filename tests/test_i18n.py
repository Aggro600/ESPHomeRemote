"""Sprachdateien: gueltig, vollstaendig, zueinander passend."""
from __future__ import annotations

import json
import re


def _json(p):
    return json.loads(p.read_text(encoding="utf-8"))


def _schluessel(d, pfad=""):
    out = set()
    for k, v in d.items():
        if isinstance(v, dict):
            out |= _schluessel(v, f"{pfad}{k}.")
        else:
            out.add(f"{pfad}{k}")
    return out


def test_ha_uebersetzungen_vollstaendig(integration_pfad):
    """strings.json (Quelle), translations/en.json und translations/de.json haben dieselben Schluessel."""
    s = _schluessel(_json(integration_pfad / "strings.json"))
    en = _schluessel(_json(integration_pfad / "translations" / "en.json"))
    de = _schluessel(_json(integration_pfad / "translations" / "de.json"))
    assert s == en, f"strings.json vs en.json: {sorted(s ^ en)[:10]}"
    assert s == de, f"strings.json vs de.json: {sorted(s ^ de)[:10]}"


def test_konfigurator_woerterbuch(integration_pfad):
    """Konfigurator-Woerterbuch: jede Regel ein gueltiger regulaerer Ausdruck, keine leeren Uebersetzungen,
    Platzhalter ($1, $2) der Regeln stimmen mit den Gruppen ueberein."""
    wb = _json(integration_pfad / "frontend" / "i18n" / "en.json")
    regeln = wb.pop("__regeln")
    assert len(wb) > 1000
    for k, v in wb.items():
        assert isinstance(v, str) and v.strip(), f"leere Uebersetzung fuer {k!r}"
    for muster, ersatz in regeln:
        # JavaScript-Besonderheiten auf Python abbilden (\p{L} usw. kennt re nicht - nur grob pruefen)
        py = re.sub(r"\\p\{\w+\}", "x", muster)
        r = re.compile(py)
        for g in re.findall(r"\$(\d)", ersatz):
            assert int(g) <= r.groups, f"Regel {muster!r}: $"+g+" ohne Gruppe"


def test_kein_deutsch_in_englischen_ha_texten(integration_pfad):
    en = json.dumps(_json(integration_pfad / "translations" / "en.json"), ensure_ascii=False)
    assert not re.search(r"[äöüß]", en), "Umlaute in translations/en.json"
    verdaechtig = re.findall(r"\b(?:nicht|und|oder|Fernbedienung|Einstellungen|Taste|Akku)\b", en)
    assert not verdaechtig, f"deutsche Woerter in translations/en.json: {verdaechtig[:5]}"


def test_keine_html_in_ha_texten(integration_pfad):
    """hassfest lehnt spitze Klammern in strings.json ab (2026-11-12: `<file>.yaml` fiel durch)."""
    for name in ("strings.json", "translations/en.json", "translations/de.json"):
        text = json.dumps(_json(integration_pfad / name), ensure_ascii=False)
        treffer = re.findall(r"<[a-zA-Z/][^>]{0,40}>", text)
        assert not treffer, f"{name}: {treffer[:5]}"


def test_behebbare_hinweise_ohne_description(integration_pfad):
    """hassfest: ein behebbarer Hinweis hat fix_flow ODER description, nie beides (2026-11-12 durchgefallen)."""
    for name in ("strings.json", "translations/en.json", "translations/de.json"):
        for key, issue in _json(integration_pfad / name)["issues"].items():
            if "fix_flow" in issue:
                assert "description" not in issue, f"{name}: issues.{key} hat fix_flow und description"
            else:
                assert "description" in issue, f"{name}: issues.{key} ohne description"
