"""Komplett-Sicherung: Geraetedateien, Secrets und angelegte Remotes kommen zurueck - ohne etwas zu ueberschreiben."""
from __future__ import annotations

REPO = "https://github.com/Aggro600/ESPHomeRemote"


async def _ws(client, **msg):
    _ws.n = getattr(_ws, "n", 0) + 100
    await client.send_json({"id": _ws.n, **msg})
    r = await client.receive_json()
    assert r["success"], r
    return r["result"]


async def test_komplett_rundlauf(hass, eingerichtet, hass_ws_client, tmp_path):
    from custom_components.esphomeremote import firmware

    firmware.ESPHOME_DIR = tmp_path
    (tmp_path / "remote-wz-test.yaml").write_text(f"packages:\n  firmware:\n    url: {REPO}\n    ref: stabil\n")
    (tmp_path / "dock-wz.yaml").write_text(f"packages:\n  basis:\n    url: {REPO}\n    ref: stabil\n")
    (tmp_path / "eigenes.yaml").write_text("esphome:\n  name: eigenes\n")   # nicht von uns
    (tmp_path / "secrets.yaml").write_text("wifi_ssid: Zuhause\nwifi_password: geheim\nesphomeremote_key: k1\n")
    kd = hass.data["esphomeremote"]
    kd.data["extra"] = ["remote-wz-zwei"]

    ws = await hass_ws_client(hass)
    ohne = await _ws(ws, type="esphomeremote/sicherung_komplett")
    assert set(ohne["dateien"]) == {"remote-wz-test.yaml", "dock-wz.yaml"}
    assert "secrets" not in ohne, "Secrets nur auf Wunsch"
    assert ohne["extra"] == ["remote-wz-zwei"]
    mit = await _ws(ws, type="esphomeremote/sicherung_komplett", secrets=True)
    assert "wifi_password: geheim" in mit["secrets"]

    # Neues System: leerer Ordner, andere secrets, Remote fehlt
    neu = tmp_path / "neu"
    neu.mkdir()
    firmware.ESPHOME_DIR = neu
    (neu / "secrets.yaml").write_text("wifi_ssid: Anderswo\n")
    (neu / "dock-wz.yaml").write_text("# schon da\n")
    kd.devices.remove("remote-wz-zwei")

    r = await _ws(ws, type="esphomeremote/einspielen_dateien", dateien=mit["dateien"], secrets=mit["secrets"], extra=mit["extra"])
    assert r["dateien_neu"] == ["remote-wz-test.yaml"] and r["dateien_da"] == ["dock-wz.yaml"]
    assert (neu / "dock-wz.yaml").read_text() == "# schon da\n", "vorhandene Dateien bleiben"
    assert (neu / "remote-wz-test.yaml").read_text().startswith("packages:")
    sec = (neu / "secrets.yaml").read_text()
    assert "wifi_ssid: Anderswo" in sec and "wifi_ssid: Zuhause" not in sec, "vorhandene Secrets bleiben"
    assert "wifi_password: geheim" in sec and "esphomeremote_key: k1" in sec
    assert r["secrets"] == ["wifi_password", "esphomeremote_key"]
    assert r["remotes"] == ["remote-wz-zwei"] and "remote-wz-zwei" in kd.devices

    # Fremde oder unsinnige Dateien werden nicht geschrieben
    r = await _ws(ws, type="esphomeremote/einspielen_dateien", dateien={"../boese.yaml": f"url: {REPO}", "remote-x.yaml": "kein paket"})
    assert r["dateien_neu"] == [] and not (tmp_path / "boese.yaml").exists()
