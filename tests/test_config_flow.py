"""Einrichten ueber die Oberflaeche: ohne Supervisor kommt zuerst der Hinweis, dann geht es normal weiter."""
from __future__ import annotations

from homeassistant import config_entries
from homeassistant.data_entry_flow import FlowResultType

DOMAIN = "esphomeremote"


async def test_flow_ohne_supervisor(hass, mock_bluetooth):
    from unittest.mock import AsyncMock, patch

    r = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    assert r["type"] == FlowResultType.FORM and r["step_id"] == "hinweis"
    assert "esphome" in r["description_placeholders"]["ordner"]

    r = await hass.config_entries.flow.async_configure(r["flow_id"], {})
    assert r["type"] == FlowResultType.FORM and r["step_id"] == "user"

    r = await hass.config_entries.flow.async_configure(r["flow_id"], {"devices": "Wohnzimmer, Schlafzimmer", "key": "abc123"})
    assert r["type"] == FlowResultType.FORM and r["step_id"] == "fertig"
    assert "abc123" in r["description_placeholders"]["block"]

    with patch("custom_components.esphomeremote.bridge.Bruecke.abgleichen", AsyncMock(return_value=[])):
        r = await hass.config_entries.flow.async_configure(r["flow_id"], {})
        await hass.async_block_till_done()
    assert r["type"] == FlowResultType.CREATE_ENTRY
    assert r["data"] == {"key": "abc123", "devices": ["wohnzimmer", "schlafzimmer"]}
    assert hass.data[DOMAIN].devices == ["wohnzimmer", "schlafzimmer"]


async def test_zweite_instanz_abgelehnt(hass, eingerichtet):
    r = await hass.config_entries.flow.async_init(DOMAIN, context={"source": config_entries.SOURCE_USER})
    assert r["type"] == FlowResultType.ABORT and r["reason"] == "single_instance_allowed"
