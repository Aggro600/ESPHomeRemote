"""Tests fuer die Integration (2026-11-12). Laufen mit pytest-homeassistant-custom-component:

    pip install pytest-homeassistant-custom-component
    pytest tests/                                  # im Repository (custom_components/ liegt daneben)

Die Fixture "hass" ist ein Home Assistant ohne Netz; "enable_custom_integrations" laesst es custom_components/ laden.
Der Supervisor und das ESPHome-Add-on fehlen hier absichtlich - die Integration muss trotzdem starten (Nutzer mit
HA im Container), und was sie dann nicht kann, meldet sie sauber statt abzustuerzen.
"""
from __future__ import annotations

import sys
from pathlib import Path

import pytest

pytest_plugins = "pytest_homeassistant_custom_component"

# Wurzel mit custom_components/: im Repository der Ordner ueber tests/, auf dem Entwickler-System /config
for kandidat in (Path(__file__).resolve().parent.parent, Path(__file__).resolve().parent.parent.parent):
    if (kandidat / "custom_components" / "esphomeremote").is_dir():
        if str(kandidat) not in sys.path:
            sys.path.insert(0, str(kandidat))
        WURZEL = kandidat
        break
else:
    raise RuntimeError("custom_components/esphomeremote nicht gefunden")

INTEGRATION = WURZEL / "custom_components" / "esphomeremote"


def _paket_sichtbar() -> None:
    """custom_components ist ein Namensraum-Paket; Home Assistant haengt beim Start sein (leeres) Konfigurationsverzeichnis
    davor. Unser Ordner muss im Suchpfad des Pakets bleiben - sonst findet eine einzeln gestartete Testdatei die
    Integration nicht ("No module named custom_components.esphomeremote")."""
    import importlib
    cc = importlib.import_module("custom_components")
    pfad = str(WURZEL / "custom_components")
    if pfad not in list(cc.__path__):
        cc.__path__.append(pfad)


_paket_sichtbar()


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations, monkeypatch):
    monkeypatch.delenv("SUPERVISOR_TOKEN", raising=False)   # Tests laufen immer "ohne Supervisor"
    yield


@pytest.fixture
def expected_lingering_tasks() -> bool:
    return True   # Hintergrund-Aufgaben der Integration (Dock-Stand, Warnungen) laufen bis zum Ende von HA


@pytest.fixture
def expected_lingering_timers() -> bool:
    return True


@pytest.fixture
def integration_pfad() -> Path:
    return INTEGRATION


@pytest.fixture
async def eingerichtet(hass, mock_bluetooth):
    """Integration mit zwei Test-Remotes, wie nach dem Einrichten ueber die Oberflaeche."""
    from unittest.mock import AsyncMock, patch
    from pytest_homeassistant_custom_component.common import MockConfigEntry
    _paket_sichtbar()
    import custom_components.esphomeremote.bridge  # noqa: F401 - Ziel des patch() muss geladen sein (auch bei einer einzelnen Testdatei)

    entry = MockConfigEntry(domain="esphomeremote", data={"key": "testschluessel", "devices": ["remote-wz-test", "remote-wz-zwei"]},
                            title="ESPHomeRemote")
    entry.add_to_hass(hass)
    # Die Bluetooth-Bruecke oeffnet TCP-Ports (ESPHome-API je Remote) - im Test verboten, Verhalten wird gesondert getestet
    with patch("custom_components.esphomeremote.bridge.Bruecke.abgleichen", AsyncMock(return_value=[])):
        assert await hass.config_entries.async_setup(entry.entry_id)
        await hass.async_block_till_done()
    return entry
