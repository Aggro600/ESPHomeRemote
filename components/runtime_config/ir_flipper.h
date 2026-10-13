#pragma once

// Parser fuer das Flipper-Zero-".ir"-Dateiformat, das OpenRemote Studio in
// /devices/<Geraet>.ir auf die SD-Karte schreibt. Reine Textverarbeitung,
// keine ESPHome-Abhaengigkeit - damit host-seitig testbar.

#include "ir_encoders.h"
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace esphome {
namespace runtime_config {

struct FlipperCmd {
  std::string name;
  IrTimings timings;         // leer => nicht aufloesbar
  uint32_t freq_hz{38000};
  std::string protocol;      // gesetzt, wenn Protokoll nicht unterstuetzt
};

// Flipper-Hex: "04" -> 4  /  "04 00 00 00" -> 4 (Little-Endian-Bytefolge)
inline uint32_t flipper_hex(const std::string &in) {
  std::string s = in;
  while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\t'))
    s.pop_back();
  if (s.find(' ') == std::string::npos)
    return (uint32_t) strtoul(s.c_str(), nullptr, 16);
  uint32_t v = 0;
  int shift = 0;
  const char *p = s.c_str();
  while (*p && shift < 32) {
    while (*p == ' ')
      p++;
    if (!*p)
      break;
    char *end = nullptr;
    unsigned long b = strtoul(p, &end, 16);
    if (end == p)
      break;
    v |= (uint32_t) (b & 0xFF) << shift;
    shift += 8;
    p = end;
  }
  return v;
}

inline std::string flipper_line_value(const std::string &line, size_t prefix_len) {
  if (line.size() <= prefix_len)
    return "";
  std::string v = line.substr(prefix_len);
  size_t a = v.find_first_not_of(" \t");
  size_t b = v.find_last_not_of(" \t\r\n");
  if (a == std::string::npos)
    return "";
  return v.substr(a, b - a + 1);
}

// Zerlegt den Dateiinhalt. Gibt false zurueck, wenn der Header fehlt.
inline bool parse_flipper_ir(const std::string &raw, std::vector<FlipperCmd> &out, size_t max_cmds = 64) {
  bool header_ok = false;
  bool have_cur = false;
  std::string c_name, c_type, c_proto, c_addr, c_cmd, c_data;
  uint32_t c_freq = 38000;

  auto flush = [&]() {
    if (!have_cur)
      return;
    FlipperCmd fc;
    fc.name = c_name;
    fc.freq_hz = c_freq;
    if (c_type == "raw") {
      IrTimings t;
      bool mark = true;
      const char *p = c_data.c_str();
      while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',')
          p++;
        if (!*p)
          break;
        long v = strtol(p, (char **) &p, 10);
        if (v <= 0)
          continue;
        t.push_back(mark ? (int32_t) v : -(int32_t) v);
        mark = !mark;
      }
      fc.timings = std::move(t);
    } else if (c_type == "pronto") {
      IrTimings t;
      uint32_t fhz = c_freq;
      if (ir_decode_pronto(c_data, t, fhz)) {
        fc.timings = std::move(t);
        fc.freq_hz = fhz;
      }
    } else {  // "parsed"
      IrTimings t;
      uint32_t fhz = c_freq;
      if (ir_encode_parsed(c_proto, flipper_hex(c_addr), flipper_hex(c_cmd), t, fhz)) {
        fc.timings = std::move(t);
        fc.freq_hz = fhz;
      } else {
        fc.protocol = c_proto;
      }
    }
    if (!fc.name.empty())
      out.push_back(std::move(fc));
    have_cur = false;
    c_name.clear();
    c_type.clear();
    c_proto.clear();
    c_addr.clear();
    c_cmd.clear();
    c_data.clear();
    c_freq = 38000;
  };

  size_t pos = 0;
  while (pos < raw.size()) {
    size_t nl = raw.find('\n', pos);
    std::string line = raw.substr(pos, (nl == std::string::npos ? raw.size() : nl) - pos);
    pos = (nl == std::string::npos) ? raw.size() : nl + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.pop_back();

    if (!header_ok) {
      if (line.rfind("Filetype:", 0) == 0 && line.find("IR signals file") != std::string::npos)
        header_ok = true;
      continue;
    }
    if (line.empty() || line[0] == '#')
      continue;
    if (line.rfind("name:", 0) == 0) {
      flush();
      if (out.size() >= max_cmds)
        break;
      c_name = flipper_line_value(line, 5);
      have_cur = true;
    } else if (have_cur && line.rfind("type:", 0) == 0) {
      c_type = flipper_line_value(line, 5);
    } else if (have_cur && line.rfind("protocol:", 0) == 0) {
      c_proto = flipper_line_value(line, 9);
    } else if (have_cur && line.rfind("address:", 0) == 0) {
      c_addr = flipper_line_value(line, 8);
    } else if (have_cur && line.rfind("command:", 0) == 0) {
      c_cmd = flipper_line_value(line, 8);
    } else if (have_cur && line.rfind("frequency:", 0) == 0) {
      c_freq = (uint32_t) strtoul(flipper_line_value(line, 10).c_str(), nullptr, 10);
    } else if (have_cur && line.rfind("data:", 0) == 0) {
      c_data = flipper_line_value(line, 5);
    }
  }
  flush();
  return header_ok;
}

}  // namespace runtime_config
}  // namespace esphome
