"""Reparatur-Hinweise: ohne Supervisor, alte Dateien (behebbar), USB-Flash noetig."""
from __future__ import annotations

from homeassistant.helpers import issue_registry as ir

DOMAIN = "esphomeremote"


async def test_hinweis_ohne_supervisor(hass, eingerichtet):
    reg = ir.async_get(hass)
    assert reg.async_get_issue(DOMAIN, "no_supervisor") is not None


async def test_alte_dateien_und_usb(hass, eingerichtet, tmp_path):
    from custom_components.esphomeremote import firmware
    from custom_components.esphomeremote.reparaturen import nach_liste

    geraete = [{"art": "remote", "name": "remote-wz-test", "friendly": "Remote Test", "geflasht": True, "fw": "2026.11.1.1"},
               {"art": "remote", "name": "remote-wz-zwei", "friendly": "Remote Zwei", "geflasht": True, "fw": "2026.11.10.7"},
               {"art": "dock", "name": "dock-wz", "geflasht": True, "fw": "2026.10.1.1"}]
    nach_liste(hass, geraete, ["components/menu", "esphomeremote-keys.h"])
    reg = ir.async_get(hass)
    alt = reg.async_get_issue(DOMAIN, "old_files")
    assert alt is not None and alt.is_fixable and alt.translation_placeholders["anzahl"] == "2"
    assert reg.async_get_issue(DOMAIN, "usb_flash_remote-wz-test") is not None, "alte Firmware -> USB-Hinweis"
    assert reg.async_get_issue(DOMAIN, "usb_flash_remote-wz-zwei") is None, "neue Firmware -> kein Hinweis"
    assert reg.async_get_issue(DOMAIN, "usb_flash_dock-wz") is None, "Docks brauchen keinen Bootloader"

    # Remote aktualisiert, Dateien weg: Hinweise verschwinden
    geraete[0]["fw"] = "2026.11.5.4"
    nach_liste(hass, geraete, [])
    assert reg.async_get_issue(DOMAIN, "old_files") is None
    assert reg.async_get_issue(DOMAIN, "usb_flash_remote-wz-test") is None


async def test_alte_dateien_beheben(hass, eingerichtet, hass_client, tmp_path):
    """Der Beheben-Knopf verschiebt die Dateien ins Archiv - wie der Knopf im Konfigurator."""
    from homeassistant.setup import async_setup_component
    from custom_components.esphomeremote import firmware
    from custom_components.esphomeremote.reparaturen import nach_liste

    assert await async_setup_component(hass, "repairs", {})
    firmware.ESPHOME_DIR = tmp_path   # der ESPHome-Ordner des Tests
    komp = firmware.KOMPONENTEN[0]
    (tmp_path / "components" / komp).mkdir(parents=True)
    (tmp_path / "components" / komp / "__init__.py").write_text("")
    (tmp_path / "esphomeremote-keys.h").write_text("// alt")
    (tmp_path / "remote-wz-test.yaml").write_text("esphome:\n  name: remote-wz-test\n")
    alt = firmware.altlasten()
    assert f"components/{komp}" in alt and "esphomeremote-keys.h" in alt
    nach_liste(hass, [], alt)

    client = await hass_client()
    r = await client.post("/api/repairs/issues/fix", json={"handler": DOMAIN, "issue_id": "old_files"})
    assert r.status == 200, await r.text()
    flow = await r.json()
    assert flow["type"] == "form" and flow["step_id"] == "confirm"
    r = await client.post(f"/api/repairs/issues/fix/{flow['flow_id']}", json={})
    assert r.status == 200, await r.text()
    assert (await r.json())["type"] == "create_entry"

    assert not (tmp_path / "esphomeremote-keys.h").exists() and not (tmp_path / "components").exists()
    assert list((tmp_path / "archive").glob("esphomeremote-alt-*/esphomeremote-keys.h"))
    assert (tmp_path / "remote-wz-test.yaml").exists(), "Geraetedateien bleiben"
    assert ir.async_get(hass).async_get_issue(DOMAIN, "old_files") is None
