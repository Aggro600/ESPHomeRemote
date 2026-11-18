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


async def test_ha_adresse(hass, eingerichtet):
    """Adresse fuer die Remotes (2026-11-18): leer, Name oder Docker-IP -> Hinweis; LAN-IP -> keiner."""
    from custom_components.esphomeremote.reparaturen import adresse_pruefen

    reg = ir.async_get(hass)
    for schlecht in ("", "homeassistant.local:8123", "172.17.0.2:8123", "127.0.0.1:8123"):
        adresse_pruefen(hass, schlecht)
        assert reg.async_get_issue(DOMAIN, "ha_address") is not None, schlecht
    adresse_pruefen(hass, "192.168.1.20:8123")
    assert reg.async_get_issue(DOMAIN, "ha_address") is None


async def test_basis_name_wird_ip(hass, eingerichtet):
    """Interne URL mit Hostname: die Remote bekommt die eigene IP von HA statt des Namens."""
    from types import SimpleNamespace

    kd = next(v for v in hass.data.values() if hasattr(v, "_basis_ermitteln"))
    await hass.config.async_update(internal_url="http://homeassistant.local:8123")
    hass.config.api = SimpleNamespace(local_ip="192.168.1.20", port=8123, use_ssl=False)
    assert kd.basis() == "192.168.1.20:8123"
    await hass.config.async_update(internal_url="http://192.168.1.30:8123")
    assert kd.basis() == "192.168.1.30:8123"
    hass.config.api = None
    await hass.config.async_update(internal_url="https://ha.example.org")
    assert kd.basis() == ""
    await hass.async_block_till_done()
    assert ir.async_get(hass).async_get_issue(DOMAIN, "ha_address") is not None
