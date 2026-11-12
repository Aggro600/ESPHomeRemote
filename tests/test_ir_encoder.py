"""IR-Kodierer (Integration -> Blaster/Dock): Rahmenlaengen und Kopf je Protokoll."""
from __future__ import annotations

import pytest

from custom_components.esphomeremote.ir_encoder import kodieren


def test_nec():
    raw, f = kodieren({"proto": "NEC", "addr": 0x04, "cmd": 0x08})
    assert f == 38000
    assert raw[0] == 9000 and raw[1] == -4500, "NEC-Kopf 9 ms / 4,5 ms"
    # Kopf (2) + 32 Bit (64) + Schlusspuls = 67 Werte, mit Pause 68
    assert len(raw) in (67, 68)
    assert all(v != 0 for v in raw)
    assert all((v > 0) != (raw[i - 1] > 0) for i, v in enumerate(raw) if i), "Pulse und Pausen wechseln sich ab"


def test_nec_adresse_und_befehl_unterscheiden_sich():
    a, _ = kodieren({"proto": "NEC", "addr": 1, "cmd": 2})
    b, _ = kodieren({"proto": "NEC", "addr": 1, "cmd": 3})
    assert a != b


def test_sony():
    raw, f = kodieren({"proto": "SIRC", "addr": 1, "cmd": 21})   # Sony-Fernseher: Power
    assert f == 40000
    assert raw[0] == 2400 and raw[1] == -600, "Sony-Kopf 2,4 ms / 0,6 ms"
    # Sony wird 3x gesendet (ein Rahmen wird von Fernsehern ignoriert)
    assert raw.count(2400) == 3


def test_samsung():
    raw, f = kodieren({"proto": "Samsung32", "addr": 0x07, "cmd": 0x02})
    assert f == 38000
    assert raw[0] == 4500 and raw[1] == -4500


def test_raw_durchgereicht():
    raw, f = kodieren({"raw": [100, -200, 0, 300], "f": 36000})
    assert raw == [100, -200, 300] and f == 36000


def test_unbekannt():
    with pytest.raises(ValueError):
        kodieren({"proto": "GibtEsNicht", "addr": 1, "cmd": 1})
