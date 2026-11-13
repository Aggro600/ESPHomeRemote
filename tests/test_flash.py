"""Flash-Schreibzugriffe: Rate je Tag aus dem Zaehler der Remote, Hinweis erst bei einem Fehler, Neustart setzt zurueck."""
from __future__ import annotations

from datetime import timedelta

from homeassistant.helpers import issue_registry as ir
from homeassistant.util import dt as dt_util

DOMAIN = "esphomeremote"
EID = "sensor.remote_wz_test_flash_schreibzugriffe"


async def test_flash_rate(hass, eingerichtet):
    w = hass.data[DOMAIN + "_warnungen"]
    reg = ir.async_get(hass)
    t0 = dt_util.utcnow()

    def messen(wert, stunden):
        hass.states.async_set(EID, str(wert))
        w._flash("remote_wz_test", "remote-wz-test", t0 + timedelta(hours=stunden), hass.states.get(EID))

    messen(100, 0)
    messen(250, 6)   # 150 in 6 h = 600 am Tag: normal
    assert round(w.flash_rate["remote_wz_test"]) == 600
    assert reg.async_get_issue(DOMAIN, "flash_writes_remote_wz_test") is None

    messen(3000, 12)   # 2900 in 12 h = 5800 am Tag: Fehler
    assert reg.async_get_issue(DOMAIN, "flash_writes_remote_wz_test") is not None

    messen(5, 13)   # Strom weg / Neustart: Zaehler wieder klein -> neues Fenster, kein Fehlalarm
    assert w.flash["remote_wz_test"][1] == 5
    messen(105, 19)   # wieder normal
    assert reg.async_get_issue(DOMAIN, "flash_writes_remote_wz_test") is None


async def test_flash_ueber_dock_und_diagnose(hass, eingerichtet):
    """Der Wert kommt auch ueber das Dock an (Schluessel f) und steht in der Diagnose."""
    from custom_components.esphomeremote.dockstatus import WERTE, ART
    from custom_components.esphomeremote.diagnostics import async_get_config_entry_diagnostics

    assert WERTE["f"][1] == "flash_schreibzugriffe" and "f" in ART
    hass.states.async_set(EID, "1234")
    hass.data[DOMAIN + "_warnungen"].flash_rate["remote_wz_test"] = 420.4
    d = await async_get_config_entry_diagnostics(hass, eingerichtet)
    r = d["remotes"]["remote_1"]
    assert r["flash_schreibzugriffe_seit_start"] == "1234" and r["flash_schreibzugriffe_je_tag"] == 420
