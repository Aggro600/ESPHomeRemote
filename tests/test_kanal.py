"""Firmware-Kanal stabil/beta: Option, Geraetedateien, neue Dateien."""
from __future__ import annotations


async def test_kanal_umstellen(hass, eingerichtet, tmp_path):
    from custom_components.esphomeremote import firmware

    firmware.ESPHOME_DIR = tmp_path
    (tmp_path / "remote-wz-test.yaml").write_text(
        "substitutions:\n  device_name: remote-wz-test\npackages:\n  firmware:\n    url: https://github.com/Aggro600/ESPHomeRemote\n"
        "    ref: stabil\n    files: [esphomeremote-firmware.yaml]\n    refresh: 0s\n")
    (tmp_path / "dock-wz.yaml").write_text(
        "packages:\n  basis:\n    url: https://github.com/Aggro600/ESPHomeRemote\n    ref: stabil\n    files: [esphomeremote-dock-firmware.yaml]\n")
    (tmp_path / "fremd.yaml").write_text("packages:\n  x:\n    url: https://github.com/jemand/anderes\n    ref: stabil\n")
    (tmp_path / "secrets.yaml").write_text("wifi_ssid: x\n")

    assert firmware.zweig() == "stabil"
    assert firmware.kanal_umstellen("beta") == ["dock-wz.yaml", "remote-wz-test.yaml"]
    assert "ref: beta" in (tmp_path / "remote-wz-test.yaml").read_text()
    assert "ref: stabil" in (tmp_path / "fremd.yaml").read_text(), "fremde Pakete bleiben"
    assert firmware.kanal_umstellen("beta") == [], "zweites Mal: nichts mehr zu tun"
    assert firmware.kanal_umstellen("stabil") == ["dock-wz.yaml", "remote-wz-test.yaml"]


async def test_kanal_option(hass, eingerichtet, tmp_path):
    """Option umstellen -> Dateien folgen, neue Dateien nehmen den Kanal."""
    from custom_components.esphomeremote import firmware
    from custom_components.esphomeremote.docks import _dock_text

    firmware.ESPHOME_DIR = tmp_path
    (tmp_path / "remote-wz-test.yaml").write_text(
        "packages:\n  firmware:\n    url: https://github.com/Aggro600/ESPHomeRemote\n    ref: stabil\n    files: [esphomeremote-firmware.yaml]\n")
    hass.config_entries.async_update_entry(eingerichtet, options={"devices": ["remote-wz-test", "remote-wz-zwei"], "kanal": "beta"})
    await hass.async_block_till_done()
    assert firmware.zweig() == "beta"
    assert "ref: beta" in (tmp_path / "remote-wz-test.yaml").read_text()
    assert "ref: beta" in _dock_text("dock-neu", "Dock neu", "original", "Test")

    hass.config_entries.async_update_entry(eingerichtet, options={"devices": ["remote-wz-test", "remote-wz-zwei"], "kanal": "stabil"})
    await hass.async_block_till_done()
    assert firmware.zweig() == "stabil"
    assert "ref: stabil" in (tmp_path / "remote-wz-test.yaml").read_text()


async def test_optionen_dialog(hass, eingerichtet):
    from homeassistant.data_entry_flow import FlowResultType

    r = await hass.config_entries.options.async_init(eingerichtet.entry_id)
    assert r["type"] == FlowResultType.FORM and r["step_id"] == "init"
    r = await hass.config_entries.options.async_configure(r["flow_id"], {"devices": "remote-wz-test, remote-wz-zwei", "kanal": "beta"})
    assert r["type"] == FlowResultType.CREATE_ENTRY
    assert eingerichtet.options["kanal"] == "beta"
