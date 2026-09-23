#pragma once

// IR-Encoder fuer das Flipper-".ir"-Dateiformat, das OpenRemote Studio auf die
// SD-Karte schreibt (/devices/<Name>.ir). Jedes "type: parsed" wird hier zu
// Roh-Timings gerechnet und dann ueber remote_transmitter gesendet - genau wie
// die Original-Firmware es macht (die haelt fuer die vier "Extra"-Protokolle
// ebenfalls einen Raw-Encoder vor).
//
// Ausgabe: std::vector<int32_t>, + = Mark (LED an, us), - = Space (us).
// freq_hz wird auf die Traegerfrequenz des Protokolls gesetzt.
//
// Unterstuetzt: NEC, NECext/NEC1, NEC42, Sony SIRC 12/15/20, RC5/RC5X, RC6,
// Samsung32, Kaseikyo (Panasonic/Denon), Pioneer, RCA, JVC, Pronto-Hex.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace esphome {
namespace runtime_config {

using IrTimings = std::vector<int32_t>;

// --- kleine Bausteine ------------------------------------------------------

// Pulse-Distance-Frame: fester Mark, danach Space je nach Bit. LSB oder MSB.
inline void ir_pulse_distance(IrTimings &out, uint64_t data, uint8_t bits, uint32_t mark, uint32_t zero_space,
                              uint32_t one_space, bool msb_first) {
  for (uint8_t i = 0; i < bits; i++) {
    uint8_t bit_index = msb_first ? (bits - 1 - i) : i;
    bool one = (data >> bit_index) & 1ULL;
    out.push_back((int32_t) mark);
    out.push_back(-(int32_t) (one ? one_space : zero_space));
  }
}

// --- Protokolle ----------------------------------------------------------

// Feldaufteilung + Timings 1:1 aus der Flipper-Zero-Firmware
// (lib/infrared/encoder_decoder/*), weil OpenRemote Studio Flipper-".ir"-
// Dateien schreibt und die IRDB-Codes mit diesen Konventionen geparst wurden.

inline bool ir_encode_nec(uint32_t addr, uint32_t cmd, bool ext, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 38000;
  uint64_t frame;
  if (ext) {
    // NECext (Flipper): 16-Bit-Adresse literal, 16-Bit-Kommando literal
    frame = (addr & 0xFFFFULL) | ((cmd & 0xFFFFULL) << 16);
  } else {
    frame = (addr & 0xFFULL) | (((~addr) & 0xFFULL) << 8) | ((cmd & 0xFFULL) << 16) | (((~cmd) & 0xFFULL) << 24);
  }
  out.push_back(9000);
  out.push_back(-4500);
  ir_pulse_distance(out, frame, 32, 560, 560, 1690, false);
  out.push_back(560);
  return true;
}

// NEC42 (Flipper): 13 addr + 13 ~addr + 8 cmd + 8 ~cmd.
// NEC42ext: 26 addr + 16 cmd (kein Komplement).
inline bool ir_encode_nec42(uint32_t addr, uint32_t cmd, bool ext, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 38000;
  uint64_t frame;
  if (ext) {
    frame = (addr & 0x3FFFFFFULL) | ((cmd & 0xFFFFULL) << 26);
  } else {
    uint64_t a = addr & 0x1FFFULL, c = cmd & 0xFFULL;
    frame = a | (((~a) & 0x1FFFULL) << 13) | (c << 26) | (((~c) & 0xFFULL) << 34);
  }
  out.push_back(9000);
  out.push_back(-4500);
  ir_pulse_distance(out, frame, 42, 560, 560, 1690, false);
  out.push_back(560);
  return true;
}

inline bool ir_encode_sony(uint32_t addr, uint32_t cmd, uint8_t nbits, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 40000;
  if (nbits != 12 && nbits != 15 && nbits != 20)
    nbits = 12;
  // Flipper SIRC: data = command(7) | address << 7   (Adresse 5/8/13 Bit)
  uint64_t frame = cmd & 0x7FULL;
  if (nbits == 12)
    frame |= (addr & 0x1FULL) << 7;
  else if (nbits == 15)
    frame |= (addr & 0xFFULL) << 7;
  else  // 20: 13-Bit-Adresse
    frame |= (addr & 0x1FFFULL) << 7;
  // 3x senden (Sony erwartet Wiederholung), Start-zu-Start ~45 ms.
  for (int rep = 0; rep < 3; rep++) {
    int32_t frame_us = 2400 + 600;
    out.push_back(2400);
    out.push_back(-600);
    for (uint8_t i = 0; i < nbits; i++) {
      bool one = (frame >> i) & 1ULL;
      int32_t m = one ? 1200 : 600;
      out.push_back(m);
      out.push_back(-600);
      frame_us += m + 600;
    }
    if (rep < 2) {
      int32_t gap = 45000 - frame_us;
      if (gap < 8000)
        gap = 8000;
      out.back() = -gap;
    }
  }
  return true;
}

// RC5 (14 bit, MSB first, Manchester 889 us). Standard-IEC-Kodierung wie in
// ESPHomes remote_base: Bit 1 = Space,Mark  /  Bit 0 = Mark,Space.
inline bool ir_encode_rc5(uint32_t addr, uint32_t cmd, bool rc5x, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 36000;
  const uint32_t bit_us = 889;
  uint32_t frame = 0;
  uint32_t command = cmd & 0x7FU;
  if (command >= 64 || rc5x) {
    frame |= 0b10U << 12;   // S1=1, S2 = ~cmd6 (fuer 7-Bit-Kommandos)
    command &= 0x3FU;
  } else {
    frame |= 0b11U << 12;
  }
  frame |= 0U << 11;                 // Toggle
  frame |= (addr & 0x1FU) << 6;
  frame |= command;
  for (int i = 13; i >= 0; i--) {
    bool one = (frame >> i) & 1U;
    if (one) {
      out.push_back(-(int32_t) bit_us);
      out.push_back((int32_t) bit_us);
    } else {
      out.push_back((int32_t) bit_us);
      out.push_back(-(int32_t) bit_us);
    }
  }
  // aufeinanderfolgende gleiche Pegel zusammenfassen
  IrTimings merged;
  for (int32_t v : out) {
    if (!merged.empty() && ((merged.back() < 0) == (v < 0)))
      merged.back() += v;
    else
      merged.push_back(v);
  }
  out = std::move(merged);
  if (!out.empty() && out.front() < 0)
    out.erase(out.begin());   // fuehrender Space (idle) entfaellt
  if (!out.empty() && out.back() < 0)
    out.pop_back();
  return true;
}

// RC6 Mode 0 (Manchester 444 us, Toggle-Bit doppelt breit).
// Bit 1 = Mark,Space  /  Bit 0 = Space,Mark  (invers zu RC5).
inline bool ir_encode_rc6(uint32_t addr, uint32_t cmd, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 36000;
  const int32_t u = 444;
  IrTimings raw;
  raw.push_back(6 * u);   // Leader Mark
  raw.push_back(-2 * u);  // Leader Space
  auto emit_bit = [&](bool one, int32_t half) {
    if (one) {
      raw.push_back(half);
      raw.push_back(-half);
    } else {
      raw.push_back(-half);
      raw.push_back(half);
    }
  };
  // Startbit (1) + 3 Modusbits (0)
  emit_bit(true, u);
  emit_bit(false, u);
  emit_bit(false, u);
  emit_bit(false, u);
  // Toggle-Bit (0), doppelte Breite
  emit_bit(false, 2 * u);
  // 8 Adresse + 8 Kommando, MSB first
  uint32_t ac = ((addr & 0xFFU) << 8) | (cmd & 0xFFU);
  for (int i = 15; i >= 0; i--)
    emit_bit((ac >> i) & 1U, u);
  IrTimings merged;
  for (int32_t v : raw) {
    if (!merged.empty() && ((merged.back() < 0) == (v < 0)))
      merged.back() += v;
    else
      merged.push_back(v);
  }
  out = std::move(merged);
  if (!out.empty() && out.back() < 0)
    out.pop_back();
  return true;
}

// Samsung32 (Flipper): addr | addr<<8 | cmd<<16 | ~cmd<<24, LSB first.
inline bool ir_encode_samsung32(uint32_t addr, uint32_t cmd, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 38000;
  uint64_t frame = (addr & 0xFFULL) | ((addr & 0xFFULL) << 8) | ((cmd & 0xFFULL) << 16) | (((~cmd) & 0xFFULL) << 24);
  out.push_back(4500);
  out.push_back(-4500);
  ir_pulse_distance(out, frame, 32, 550, 550, 1650, false);
  out.push_back(550);
  return true;
}

// Kaseikyo / Panasonic / Denon (48 Bit, LSB). Feldaufteilung + Timings aus der
// Analyse der Flipper-IRDB (siehe Original-Firmware-Kommentar).
inline bool ir_encode_kaseikyo(uint32_t addr, uint32_t cmd, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 37000;
  uint16_t vendor = (uint16_t) ((addr >> 8) & 0xFFFFU);
  uint8_t genre = (uint8_t) (addr & 0xFFU);
  uint8_t id = (uint8_t) ((addr >> 24) & 0x03U);
  uint16_t data = (uint16_t) (cmd & 0x03FFU);
  uint8_t vparity = (uint8_t) (vendor ^ (vendor >> 8));
  vparity = (uint8_t) ((vparity ^ (vparity >> 4)) & 0x0FU);
  uint64_t frame = (uint64_t) vendor | ((uint64_t) vparity << 16) | ((uint64_t) genre << 20) |
                   ((uint64_t) data << 28) | ((uint64_t) id << 38);
  uint8_t b2 = (uint8_t) ((frame >> 16) & 0xFF);
  uint8_t b3 = (uint8_t) ((frame >> 24) & 0xFF);
  uint8_t b4 = (uint8_t) ((frame >> 32) & 0xFF);
  frame |= (uint64_t) (uint8_t) (b2 ^ b3 ^ b4) << 40;
  out.push_back(3456);
  out.push_back(-1728);
  ir_pulse_distance(out, frame, 48, 432, 432, 1296, false);
  out.push_back(432);
  return true;
}

// Pioneer (Flipper): NEC-foermig, 8500/4225, 500/500/1500, 40 kHz.
inline bool ir_encode_pioneer(uint32_t addr, uint32_t cmd, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 40000;
  uint64_t a = addr & 0xFFULL, c = cmd & 0xFFULL;
  uint64_t frame = a | (((~a) & 0xFFULL) << 8) | (c << 16) | (((~c) & 0xFFULL) << 24);
  out.push_back(8500);
  out.push_back(-4225);
  ir_pulse_distance(out, frame, 32, 500, 500, 1500, false);
  out.push_back(500);
  return true;
}

// RCA (Flipper): addr4 | cmd8<<4 | ~addr4<<12 | ~cmd8<<16, LSB first,
// 4000/4000, Bit0 500/1000, Bit1 500/2000, 38 kHz.
inline bool ir_encode_rca(uint32_t addr, uint32_t cmd, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 38000;
  uint32_t a = addr & 0x0FU, c = cmd & 0xFFU;
  uint32_t frame = a | (c << 4) | (((~a) & 0x0FU) << 12) | (((~c) & 0xFFU) << 16);
  out.push_back(4000);
  out.push_back(-4000);
  ir_pulse_distance(out, frame, 24, 500, 1000, 2000, false);
  out.push_back(500);
  return true;
}

inline bool ir_encode_jvc(uint32_t addr, uint32_t cmd, IrTimings &out, uint32_t &freq_hz) {
  freq_hz = 38000;
  uint64_t frame = (addr & 0xFFULL) | ((cmd & 0xFFULL) << 8);
  out.push_back(8400);
  out.push_back(-4200);
  ir_pulse_distance(out, frame, 16, 525, 525, 1575, false);
  out.push_back(525);
  return true;
}

// Pronto-Hex ("0000 xxxx once1 once2 <pairs...>") -> Roh-Timings der ersten
// Sequenz. Identisch zur Original-Firmware (loadProntoTimings).
inline bool ir_decode_pronto(const std::string &text, IrTimings &out, uint32_t &freq_hz) {
  const char *cur = text.c_str();
  uint32_t word[4];
  for (int i = 0; i < 4; i++) {
    while (*cur == ' ')
      cur++;
    if (!*cur)
      return false;
    char *end = nullptr;
    unsigned long v = strtoul(cur, &end, 16);
    if (end == cur || v > 0xFFFF)
      return false;
    word[i] = (uint32_t) v;
    cur = end;
  }
  if (word[0] != 0 || word[1] == 0)
    return false;
  uint32_t carrier = 4145146UL / word[1];
  if (carrier < 20000 || carrier > 60000)
    return false;
  freq_hz = carrier;
  uint32_t once = word[2] * 2U;
  if (!once || once > 1024)
    return false;
  for (uint32_t i = 0; i < once; i++) {
    while (*cur == ' ')
      cur++;
    if (!*cur)
      return false;
    char *end = nullptr;
    unsigned long cycles = strtoul(cur, &end, 16);
    if (end == cur)
      return false;
    cur = end;
    uint32_t us = (uint32_t) (((uint64_t) cycles * 1000000ULL + carrier / 2) / carrier);
    out.push_back((i & 1) ? -(int32_t) us : (int32_t) us);
  }
  return true;
}

// Dispatcher: protocol-Name + address/command -> Timings. Gibt false zurueck,
// wenn das Protokoll (noch) nicht unterstuetzt wird.
inline bool ir_encode_parsed(const std::string &proto, uint32_t addr, uint32_t cmd, IrTimings &out, uint32_t &freq_hz) {
  auto is = [&](const char *n) { return proto == n; };
  if (is("NEC"))
    return ir_encode_nec(addr, cmd, false, out, freq_hz);
  if (is("NECext") || is("NEC1") || is("NEC5") || is("Onkyo"))
    return ir_encode_nec(addr, cmd, true, out, freq_hz);
  if (is("NEC42"))
    return ir_encode_nec42(addr, cmd, false, out, freq_hz);
  if (is("NEC42ext"))
    return ir_encode_nec42(addr, cmd, true, out, freq_hz);
  if (is("SIRC"))
    return ir_encode_sony(addr, cmd, 12, out, freq_hz);
  if (is("SIRC15"))
    return ir_encode_sony(addr, cmd, 15, out, freq_hz);
  if (is("SIRC20"))
    return ir_encode_sony(addr, cmd, 20, out, freq_hz);
  if (is("RC5"))
    return ir_encode_rc5(addr, cmd, false, out, freq_hz);
  if (is("RC5X"))
    return ir_encode_rc5(addr, cmd, true, out, freq_hz);
  if (is("RC6"))
    return ir_encode_rc6(addr, cmd, out, freq_hz);
  if (is("Samsung32"))
    return ir_encode_samsung32(addr, cmd, out, freq_hz);
  if (is("Kaseikyo") || is("Panasonic") || is("Denon"))
    return ir_encode_kaseikyo(addr, cmd, out, freq_hz);
  if (is("Pioneer"))
    return ir_encode_pioneer(addr, cmd, out, freq_hz);
  if (is("RCA"))
    return ir_encode_rca(addr, cmd, out, freq_hz);
  if (is("JVC"))
    return ir_encode_jvc(addr, cmd, out, freq_hz);
  return false;
}

}  // namespace runtime_config
}  // namespace esphome
