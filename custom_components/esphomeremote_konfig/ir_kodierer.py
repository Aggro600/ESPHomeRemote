"""IR-Schritte (wie in der Belegung: proto/addr/cmd im Flipper-Format oder raw/f) -> Rohzeiten.

1:1 aus der Firmware (esphome/components/tasten_konfig/ir_encoders.h) uebernommen, damit HA ueber
einen IR-Sender (infrared-Entitaet) genau dasselbe sendet wie die Remote.
Ausgabe: (zeiten, traeger_hz), + = Mark (us), - = Space (us).
"""

from __future__ import annotations


def _pd(out: list[int], data: int, bits: int, mark: int, zero: int, one: int, msb: bool = False) -> None:
    for i in range(bits):
        b = (data >> ((bits - 1 - i) if msb else i)) & 1
        out += [mark, -(one if b else zero)]


def _zusammen(raw: list[int]) -> list[int]:
    out: list[int] = []
    for v in raw:
        if out and (out[-1] < 0) == (v < 0):
            out[-1] += v
        else:
            out.append(v)
    return out


def _nec(a: int, c: int, ext: bool) -> tuple[list[int], int]:
    if ext:
        f = (a & 0xFFFF) | ((c & 0xFFFF) << 16)
    else:
        f = (a & 0xFF) | ((~a & 0xFF) << 8) | ((c & 0xFF) << 16) | ((~c & 0xFF) << 24)
    out = [9000, -4500]
    _pd(out, f, 32, 560, 560, 1690)
    return out + [560], 38000


def _nec42(a: int, c: int, ext: bool) -> tuple[list[int], int]:
    if ext:
        f = (a & 0x3FFFFFF) | ((c & 0xFFFF) << 26)
    else:
        a, c = a & 0x1FFF, c & 0xFF
        f = a | ((~a & 0x1FFF) << 13) | (c << 26) | ((~c & 0xFF) << 34)
    out = [9000, -4500]
    _pd(out, f, 42, 560, 560, 1690)
    return out + [560], 38000


def _sony(a: int, c: int, bits: int) -> tuple[list[int], int]:
    f = c & 0x7F
    f |= (a & {12: 0x1F, 15: 0xFF, 20: 0x1FFF}[bits]) << 7
    out: list[int] = []
    for rep in range(3):   # Sony erwartet Wiederholung, Start-zu-Start ~45 ms
        dauer = 3000
        out += [2400, -600]
        for i in range(bits):
            m = 1200 if (f >> i) & 1 else 600
            out += [m, -600]
            dauer += m + 600
        if rep < 2:
            out[-1] = -max(8000, 45000 - dauer)
    return out, 40000


def _rc5(a: int, c: int, rc5x: bool) -> tuple[list[int], int]:
    cmd = c & 0x7F
    if cmd >= 64 or rc5x:
        f, cmd = 0b10 << 12, cmd & 0x3F
    else:
        f = 0b11 << 12
    f |= ((a & 0x1F) << 6) | cmd
    raw: list[int] = []
    for i in range(13, -1, -1):
        raw += [-889, 889] if (f >> i) & 1 else [889, -889]
    out = _zusammen(raw)
    if out and out[0] < 0:
        out.pop(0)
    if out and out[-1] < 0:
        out.pop()
    return out, 36000


def _rc6(a: int, c: int) -> tuple[list[int], int]:
    u = 444
    raw = [6 * u, -2 * u]

    def bit(one: bool, h: int) -> None:
        raw.extend([h, -h] if one else [-h, h])

    bit(True, u)
    for _ in range(3):
        bit(False, u)
    bit(False, 2 * u)
    ac = ((a & 0xFF) << 8) | (c & 0xFF)
    for i in range(15, -1, -1):
        bit(bool((ac >> i) & 1), u)
    out = _zusammen(raw)
    if out and out[-1] < 0:
        out.pop()
    return out, 36000


def _samsung32(a: int, c: int) -> tuple[list[int], int]:
    f = (a & 0xFF) | ((a & 0xFF) << 8) | ((c & 0xFF) << 16) | ((~c & 0xFF) << 24)
    out = [4500, -4500]
    _pd(out, f, 32, 550, 550, 1650)
    return out + [550], 38000


def _kaseikyo(a: int, c: int) -> tuple[list[int], int]:
    vendor, genre, ident, data = (a >> 8) & 0xFFFF, a & 0xFF, (a >> 24) & 0x03, c & 0x03FF
    vp = (vendor ^ (vendor >> 8)) & 0xFF
    vp = (vp ^ (vp >> 4)) & 0x0F
    f = vendor | (vp << 16) | (genre << 20) | (data << 28) | (ident << 38)
    f |= (((f >> 16) & 0xFF) ^ ((f >> 24) & 0xFF) ^ ((f >> 32) & 0xFF)) << 40
    out = [3456, -1728]
    _pd(out, f, 48, 432, 432, 1296)
    return out + [432], 37000


def _pioneer(a: int, c: int) -> tuple[list[int], int]:
    a, c = a & 0xFF, c & 0xFF
    f = a | ((~a & 0xFF) << 8) | (c << 16) | ((~c & 0xFF) << 24)
    out = [8500, -4225]
    _pd(out, f, 32, 500, 500, 1500)
    return out + [500], 40000


def _rca(a: int, c: int) -> tuple[list[int], int]:
    a, c = a & 0x0F, c & 0xFF
    f = a | (c << 4) | ((~a & 0x0F) << 12) | ((~c & 0xFF) << 16)
    out = [4000, -4000]
    _pd(out, f, 24, 500, 1000, 2000)
    return out + [500], 38000


def _jvc(a: int, c: int) -> tuple[list[int], int]:
    out = [8400, -4200]
    _pd(out, (a & 0xFF) | ((c & 0xFF) << 8), 16, 525, 525, 1575)
    return out + [525], 38000


PROTOKOLLE = {
    "NEC": lambda a, c: _nec(a, c, False),
    "NECext": lambda a, c: _nec(a, c, True), "NEC1": lambda a, c: _nec(a, c, True),
    "NEC5": lambda a, c: _nec(a, c, True), "Onkyo": lambda a, c: _nec(a, c, True),
    "NEC42": lambda a, c: _nec42(a, c, False), "NEC42ext": lambda a, c: _nec42(a, c, True),
    "SIRC": lambda a, c: _sony(a, c, 12), "SIRC15": lambda a, c: _sony(a, c, 15),
    "SIRC20": lambda a, c: _sony(a, c, 20),
    "RC5": lambda a, c: _rc5(a, c, False), "RC5X": lambda a, c: _rc5(a, c, True),
    "RC6": _rc6, "Samsung32": _samsung32,
    "Kaseikyo": _kaseikyo, "Panasonic": _kaseikyo, "Denon": _kaseikyo,
    "Pioneer": _pioneer, "RCA": _rca, "JVC": _jvc,
}


def kodieren(schritt: dict) -> tuple[list[int], int]:
    """IR-Schritt -> (Rohzeiten, Traeger). ValueError bei unbekanntem Protokoll."""
    if schritt.get("raw") is not None:
        return [int(v) for v in schritt["raw"] if int(v)], int(schritt.get("f") or 38000)
    proto = str(schritt.get("proto") or "NEC")
    fn = PROTOKOLLE.get(proto)
    if fn is None:
        raise ValueError(f"IR-Protokoll {proto} unbekannt")
    return fn(int(schritt.get("addr") or 0), int(schritt.get("cmd") or 0))
