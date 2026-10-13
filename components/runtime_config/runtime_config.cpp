#include "runtime_config.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"

#include "esphome/components/sd_card/sd_card.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include "esphome/components/remote_base/remote_base.h"
#include "esphome/components/espidf_ble_keyboard/espidf_ble_keyboard.h"

#include "ir_encoders.h"
#include "ir_flipper.h"

#include <ArduinoJson.h>
#include <cctype>

namespace esphome {
namespace runtime_config {

static const char *const TAG = "runtime_config";

void RuntimeConfig::setup() {
  if (this->load_on_boot_) {
    // erst wenn die SD gemountet ist (SdCard::mount kann auf Boot laufen)
    this->set_timeout(2500, [this]() {
      if (this->sd_ != nullptr && !this->sd_->is_mounted())
        this->sd_->mount();
      this->load();
    });
  }
}

void RuntimeConfig::dump_config() {
  ESP_LOGCONFIG(TAG, "Runtime-Config (OMOTE runtime.json):");
  ESP_LOGCONFIG(TAG, "  Pfad: %s", this->path_.c_str());
  ESP_LOGCONFIG(TAG, "  IR-Sender: %s   BLE-Keyboard: %s", this->tx_ ? "ja" : "-",
                this->kb_ ? "ja" : "-");
  if (this->loaded_) {
    ESP_LOGCONFIG(TAG, "  %s", this->schema_info_.c_str());
    ESP_LOGCONFIG(TAG, "  %d Geraete, %d Befehle, %d Aktivitaeten", (int) this->device_count(),
                  (int) this->command_count(), (int) this->activity_count());
  } else {
    ESP_LOGCONFIG(TAG, "  (noch nicht geladen)");
  }
}

void RuntimeConfig::parse_raw_timings_(const std::string &s, std::vector<int32_t> &out) {
  out.clear();
  const char *p = s.c_str();
  bool mark = true;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == ',')
      p++;
    if (!*p)
      break;
    long v = strtol(p, (char **) &p, 10);
    if (v <= 0)
      continue;
    out.push_back(mark ? (int32_t) v : -(int32_t) v);
    mark = !mark;
  }
}

// Eine Flipper-".ir"-Datei -> ein Geraet mit Befehlen (alles zu Raw-Timings).
bool RuntimeConfig::parse_ir_file_(const std::string &path) {
  if (this->sd_ == nullptr)
    return false;
  std::string raw;
  if (!this->sd_->read_text(path, raw, 128 * 1024) || raw.empty())
    return false;

  std::vector<FlipperCmd> cmds;
  if (!parse_flipper_ir(raw, cmds) || cmds.empty())
    return false;

  // Geraetename aus dem Dateinamen (letztes '/' bis '.ir')
  std::string base = path;
  size_t slash = base.find_last_of('/');
  if (slash != std::string::npos)
    base = base.substr(slash + 1);
  size_t dot = base.rfind(".ir");
  if (dot != std::string::npos)
    base = base.substr(0, dot);
  for (auto &ch : base)
    if (ch == '_')
      ch = ' ';

  Device dev;
  dev.id = "irf_" + base;
  dev.name = base;
  dev.type = "IRDB device";
  dev.transport = "ir";

  for (auto &fc : cmds) {
    Command c;
    c.name = fc.name;
    c.id = "irc_" + dev.name + "#" + fc.name;
    if (!fc.timings.empty()) {
      c.ir_timings = std::move(fc.timings);
      c.ir_freq = fc.freq_hz;
      c.kind = CMD_IR_RAW;
    } else {
      c.protocol = fc.protocol;
      c.kind = CMD_IR_DB;
      this->irdb_unresolved_++;
    }
    dev.commands.push_back(std::move(c));
  }
  this->devices_.push_back(std::move(dev));
  return true;
}

size_t RuntimeConfig::load_ir_device_files_() {
  this->ir_file_device_count_ = 0;
  if (this->sd_ == nullptr || !this->sd_->is_mounted())
    return 0;

  // 1. /devices/index.txt (eine Datei je Zeile)
  std::string index;
  if (this->sd_->read_text("/devices/index.txt", index, 16 * 1024) && !index.empty()) {
    size_t pos = 0;
    while (pos < index.size()) {
      size_t nl = index.find('\n', pos);
      std::string p = index.substr(pos, (nl == std::string::npos ? index.size() : nl) - pos);
      pos = (nl == std::string::npos) ? index.size() : nl + 1;
      while (!p.empty() && (p.back() == '\r' || p.back() == ' '))
        p.pop_back();
      if (p.empty())
        continue;
      if (p[0] != '/')
        p = "/devices/" + p;
      if (this->parse_ir_file_(p))
        this->ir_file_device_count_++;
    }
  }

  // 2. sonst /devices scannen
  if (this->ir_file_device_count_ == 0) {
    for (auto &e : this->sd_->list_dir("/devices")) {
      if (e.is_dir)
        continue;
      std::string n = e.name;
      if (n.size() < 4)
        continue;
      std::string ext = n.substr(n.size() - 3);
      for (auto &ch : ext)
        ch = (char) tolower(ch);
      if (ext != ".ir")
        continue;
      if (this->parse_ir_file_("/devices/" + n))
        this->ir_file_device_count_++;
    }
  }
  if (this->ir_file_device_count_)
    ESP_LOGI(TAG, "%d Geraete aus /devices/*.ir geladen", (int) this->ir_file_device_count_);
  return this->ir_file_device_count_;
}

bool RuntimeConfig::load() {
  this->loaded_ = false;
  this->devices_.clear();
  this->activities_.clear();
  this->irdb_unresolved_ = 0;

  if (this->sd_ == nullptr || !this->sd_->is_mounted()) {
    this->last_result_ = "SD nicht gemountet";
    ESP_LOGW(TAG, "%s", this->last_result_.c_str());
    return false;
  }

  // Geraete aus /devices/*.ir (Flipper-Format, von OpenRemote Studio) - auch
  // dann, wenn es gar keine runtime.json gibt.
  this->load_ir_device_files_();

  std::string raw;
  if (!this->sd_->read_text(this->path_, raw, 512 * 1024) || raw.empty()) {
    if (!this->devices_.empty()) {
      this->loaded_ = true;
      this->merge_learned_();
      this->last_result_ = "geladen (nur .ir-Dateien): " + std::to_string(this->device_count()) +
                           " Geraete, " + std::to_string(this->command_count()) + " Befehle";
      ESP_LOGI(TAG, "%s", this->last_result_.c_str());
      this->load_cbs_.call();
      for (auto *t : this->load_trigs_) t->trigger();
      return true;
    }
    this->last_result_ = std::string("kann ") + this->path_ + " nicht lesen";
    ESP_LOGW(TAG, "%s", this->last_result_.c_str());
    return false;
  }
  ESP_LOGI(TAG, "%s: %d Bytes gelesen, parse ...", this->path_.c_str(), (int) raw.size());

  // Nur die Zweige behalten, die wir brauchen - spart Heap bei grosser Datei.
  JsonDocument filter;
  filter["schemaVersion"] = true;
  filter["webConfigVersion"] = true;
  filter["devices"][0]["id"] = true;
  filter["devices"][0]["name"] = true;
  filter["devices"][0]["type"] = true;
  filter["devices"][0]["transport"] = true;
  filter["devices"][0]["protocol"] = true;
  filter["devices"][0]["fileBacked"] = true;
  filter["devices"][0]["commands"][0] = true;   // ganzes command-Objekt
  filter["activities"][0]["id"] = true;
  filter["activities"][0]["name"] = true;
  filter["activities"][0]["steps"][0] = true;
  filter["macros"][0]["id"] = true;
  filter["macros"][0]["name"] = true;
  filter["macros"][0]["steps"][0] = true;
  filter["settings"] = true;

  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, raw, DeserializationOption::Filter(filter));
  if (err) {
    this->last_result_ = std::string("JSON-Fehler: ") + err.c_str();
    ESP_LOGE(TAG, "%s", this->last_result_.c_str());
    return false;
  }

  int schema = doc["schemaVersion"] | 0;
  const char *wc = doc["webConfigVersion"] | "?";
  this->schema_info_ = std::string("schema ") + std::to_string(schema) + ", webConfig " + wc;

  JsonObject st = doc["settings"];
  if (st) {
    RcSettings s;
    s.brightness = st["brightness"] | -1;
    s.sleep_seconds = st["sleepSeconds"] | -1;
    s.wake_sensitivity = st["wakeSensitivity"] | -1;
    s.clock_enabled = st["clockEnabled"].is<bool>() ? (int) (bool) st["clockEnabled"] : -1;
    s.bluetooth_enabled = st["bluetoothEnabled"].is<bool>() ? (int) (bool) st["bluetoothEnabled"] : -1;
    s.wifi_enabled = st["wifiEnabled"].is<bool>() ? (int) (bool) st["wifiEnabled"] : -1;
    s.remote_name = st["remoteName"] | "";
    s.timezone = st["timeZone"] | "";
    s.city = st["city"] | "";
    this->settings_ = s;
  }

  JsonArray devs = doc["devices"].as<JsonArray>();
  for (JsonObject dj : devs) {
    // Studio/IRDB-Geraete stehen als /devices/*.ir auf der Karte und wurden
    // oben schon geladen - hier nur die Zusammenfassung ueberspringen.
    if (dj["fileBacked"].as<bool>())
      continue;
    std::string dname = dj["name"] | "";
    bool dup = false;
    for (auto &ex : this->devices_)
      if (strcasecmp(ex.name.c_str(), dname.c_str()) == 0) {
        dup = true;
        break;
      }
    if (dup)
      continue;

    Device d;
    d.id = dj["id"] | "";
    d.name = dname;
    d.type = dj["type"] | "";
    d.transport = dj["transport"] | "";
    d.protocol = dj["protocol"] | "";
    JsonArray cmds = dj["commands"].as<JsonArray>();
    for (JsonObject cj : cmds) {
      Command c;
      c.id = cj["id"] | "";
      c.name = cj["name"] | "";
      if (cj["ir"].is<JsonObject>()) {
        JsonObject ir = cj["ir"];
        std::string irtype = ir["type"] | "";
        c.ir_freq = ir["frequency"] | 38000;
        if (irtype == "raw" && ir["data"].is<const char *>()) {
          this->parse_raw_timings_(ir["data"].as<const char *>(), c.ir_timings);
          if (!c.ir_timings.empty())
            c.kind = CMD_IR_RAW;
        } else if (irtype == "pronto" && ir["data"].is<const char *>()) {
          IrTimings t;
          uint32_t fhz = c.ir_freq;
          if (ir_decode_pronto(ir["data"].as<const char *>(), t, fhz)) {
            c.ir_timings.assign(t.begin(), t.end());
            c.ir_freq = fhz;
            c.kind = CMD_IR_RAW;
          }
        } else if (irtype == "parsed") {
          IrTimings t;
          uint32_t fhz = c.ir_freq;
          uint32_t a = ir["address"].is<const char *>() ? flipper_hex(ir["address"].as<const char *>())
                                                        : (uint32_t) (ir["address"] | 0);
          uint32_t cc = ir["command"].is<const char *>() ? flipper_hex(ir["command"].as<const char *>())
                                                         : (uint32_t) (ir["command"] | 0);
          if (ir_encode_parsed(ir["protocol"] | "", a, cc, t, fhz)) {
            c.ir_timings.assign(t.begin(), t.end());
            c.ir_freq = fhz;
            c.kind = CMD_IR_RAW;
          } else {
            c.protocol = ir["protocol"] | "";
            c.kind = CMD_IR_DB;
            this->irdb_unresolved_++;
          }
        }
      } else if (cj["hid"].is<JsonObject>()) {
        JsonObject h = cj["hid"];
        const char *rep = h["report"] | "keyboard";
        c.hid_usage = h["usage"] | 0;
        c.hid_modifier = h["modifier"] | 0;
        c.kind = (std::string(rep) == "consumer") ? CMD_HID_CONSUMER : CMD_HID_KEY;
      } else if (cj["protocol"].is<const char *>()) {
        // IRDB-Verweis ohne eingebettete Timings - die eigentlichen Codes
        // stehen als /devices/<Geraet>.ir auf der Karte (oben geladen).
        c.protocol = cj["protocol"].as<const char *>();
        c.kind = CMD_IR_DB;
        this->irdb_unresolved_++;
      }
      d.commands.push_back(std::move(c));
    }
    this->devices_.push_back(std::move(d));
  }

  // Aktivitaeten UND Makros - gleiche Struktur, gemeinsame Registry.
  for (const char *branch : {"activities", "macros"}) {
    for (JsonObject aj : doc[branch].as<JsonArray>()) {
      Activity a;
      a.id = aj["id"] | "";
      a.name = aj["name"] | "";
      JsonArray steps = aj["steps"].as<JsonArray>();
      for (JsonObject sj : steps) {
        Step s;
        std::string st = sj["type"] | "";
        if (st == "delay") {
          s.is_delay = true;
          s.delay_ms = sj["ms"] | (uint32_t) (sj["delay"] | 0);
        } else {
          s.device_id = sj["deviceId"] | "";
          s.command_id = sj["commandId"] | "";
        }
        a.steps.push_back(std::move(s));
      }
      if (!a.steps.empty())
        this->activities_.push_back(std::move(a));
    }
  }

  this->loaded_ = true;
  this->merge_learned_();
  this->last_result_ = "geladen: " + std::to_string(this->device_count()) + " Geraete, " +
                       std::to_string(this->command_count()) + " Befehle, " +
                       std::to_string(this->activity_count()) + " Aktivitaeten";
  if (this->ir_file_device_count_)
    this->last_result_ += " (" + std::to_string(this->ir_file_device_count_) + " aus .ir-Dateien)";
  if (this->irdb_unresolved_)
    this->last_result_ += " (" + std::to_string(this->irdb_unresolved_) +
                          " Befehle mit unbekanntem Protokoll - anlernen)";
  ESP_LOGI(TAG, "%s", this->last_result_.c_str());
  this->load_cbs_.call();
  for (auto *t : this->load_trigs_) t->trigger();
  return true;
}

Command *RuntimeConfig::mutable_command_(const std::string &device,
                                                       const std::string &command) {
  for (auto &d : this->devices_) {
    if (d.id != device && strcasecmp(d.name.c_str(), device.c_str()) != 0)
      continue;
    for (auto &c : d.commands)
      if (c.id == command || strcasecmp(c.name.c_str(), command.c_str()) == 0)
        return &c;
  }
  return nullptr;
}

void RuntimeConfig::merge_learned_() {
  this->learned_count_ = 0;
  if (this->sd_ == nullptr || !this->sd_->is_mounted())
    return;
  std::string raw;
  if (!this->sd_->read_text(this->learned_path_, raw, 256 * 1024) || raw.empty())
    return;
  JsonDocument doc;
  if (deserializeJson(doc, raw)) {
    ESP_LOGW(TAG, "%s unlesbar", this->learned_path_.c_str());
    return;
  }
  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject e : arr) {
    const char *dev = e["deviceId"] | (e["device"] | "");
    const char *cmd = e["commandId"] | (e["command"] | "");
    JsonObject ir = e["ir"];
    if (!ir || !ir["data"].is<const char *>())
      continue;
    Command *c = this->mutable_command_(dev, cmd);
    std::vector<int32_t> t;
    this->parse_raw_timings_(ir["data"].as<const char *>(), t);
    if (t.empty())
      continue;
    if (c != nullptr) {
      c->kind = CMD_IR_RAW;
      c->ir_freq = ir["frequency"] | 38000;
      c->ir_timings = std::move(t);
    } else {
      // unbekannt -> in ein Sammel-Geraet "Gelernt" legen
      Device *gd = nullptr;
      for (auto &d : this->devices_)
        if (d.name == "Gelernt")
          gd = &d;
      if (gd == nullptr) {
        Device nd;
        nd.id = "learned";
        nd.name = "Gelernt";
        nd.type = "IR device";
        nd.transport = "ir";
        this->devices_.push_back(std::move(nd));
        gd = &this->devices_.back();
      }
      Command nc;
      nc.id = std::string(cmd);
      nc.name = e["name"] | (std::string(cmd).empty() ? "?" : std::string(cmd));
      nc.kind = CMD_IR_RAW;
      nc.ir_freq = ir["frequency"] | 38000;
      nc.ir_timings = std::move(t);
      gd->commands.push_back(std::move(nc));
    }
    this->learned_count_++;
  }
  if (this->learned_count_)
    ESP_LOGI(TAG, "%d gelernte IR-Befehle aus %s gemergt", (int) this->learned_count_,
             this->learned_path_.c_str());
}

bool RuntimeConfig::learn_capture(const std::string &device, const std::string &command,
                                  uint32_t freq, const std::vector<int32_t> &timings) {
  if (timings.size() < 4) {
    this->last_result_ = "IR-Learn: zu wenig Daten";
    return false;
  }
  // 1. sofort in die Registry
  Command *c = this->mutable_command_(device, command);
  if (c != nullptr) {
    c->kind = CMD_IR_RAW;
    c->ir_freq = freq;
    c->ir_timings = timings;
  }
  // 2. learned.json lesen / anlegen / anhaengen
  std::string raw;
  this->sd_->read_text(this->learned_path_, raw, 256 * 1024);
  JsonDocument doc;
  if (raw.empty() || deserializeJson(doc, raw))
    doc.to<JsonArray>();
  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();
  JsonObject e = arr.add<JsonObject>();
  e["deviceId"] = device;
  e["commandId"] = command;
  JsonObject ir = e["ir"].to<JsonObject>();
  ir["type"] = "raw";
  ir["frequency"] = freq;
  std::string data;
  data.reserve(timings.size() * 5);
  for (size_t i = 0; i < timings.size(); i++) {
    if (i)
      data += ' ';
    data += std::to_string(timings[i] < 0 ? -timings[i] : timings[i]);
  }
  ir["data"] = data;
  std::string out;
  serializeJson(doc, out);
  bool ok = this->sd_->write_text(this->learned_path_, out, false);
  this->last_result_ = ok ? ("IR gelernt: " + device + "/" + command + " (" +
                             std::to_string(timings.size()) + " Werte)")
                          : "IR-Learn: Schreiben fehlgeschlagen";
  ESP_LOGI(TAG, "%s", this->last_result_.c_str());
  if (ok)
    this->learned_count_++;
  return ok;
}

size_t RuntimeConfig::command_count() const {
  size_t n = 0;
  for (auto &d : this->devices_)
    n += d.commands.size();
  return n;
}

const Device *RuntimeConfig::find_device_(const std::string &q) const {
  for (auto &d : this->devices_)
    if (d.id == q)
      return &d;
  for (auto &d : this->devices_)
    if (strcasecmp(d.name.c_str(), q.c_str()) == 0)
      return &d;
  return nullptr;
}

const Command *RuntimeConfig::find_command_(const Device *d, const std::string &q) const {
  if (d == nullptr)
    return nullptr;
  for (auto &c : d->commands)
    if (c.id == q)
      return &c;
  for (auto &c : d->commands)
    if (strcasecmp(c.name.c_str(), q.c_str()) == 0)
      return &c;
  return nullptr;
}

const Activity *RuntimeConfig::find_activity_(const std::string &q) const {
  for (auto &a : this->activities_)
    if (a.id == q)
      return &a;
  for (auto &a : this->activities_)
    if (strcasecmp(a.name.c_str(), q.c_str()) == 0)
      return &a;
  return nullptr;
}

bool RuntimeConfig::exec_command_(const Command &c) {
  switch (c.kind) {
    case CMD_IR_RAW: {
      if (this->tx_ == nullptr) {
        ESP_LOGW(TAG, "kein IR-Sender konfiguriert");
        return false;
      }
      auto call = this->tx_->transmit();
      auto *data = call.get_data();
      data->set_carrier_frequency(c.ir_freq);
      data->reserve(c.ir_timings.size());
      for (int32_t t : c.ir_timings) {
        if (t >= 0)
          data->mark((uint32_t) t);
        else
          data->space((uint32_t) (-t));
      }
      call.set_send_times(this->ir_send_times_);
      call.perform();
      ESP_LOGD(TAG, "IR raw '%s' (%d Werte, %lu Hz) gesendet", c.name.c_str(),
               (int) c.ir_timings.size(), (unsigned long) c.ir_freq);
      return true;
    }
    case CMD_HID_CONSUMER:
      if (this->kb_ == nullptr)
        return false;
      this->kb_->send_consumer(c.hid_usage);
      ESP_LOGD(TAG, "HID consumer 0x%02X ('%s')", c.hid_usage, c.name.c_str());
      return true;
    case CMD_HID_KEY:
      if (this->kb_ == nullptr)
        return false;
      this->kb_->send_key_combo(c.hid_modifier, (uint8_t) c.hid_usage);
      ESP_LOGD(TAG, "HID key mod=0x%02X usage=0x%02X ('%s')", c.hid_modifier, c.hid_usage,
               c.name.c_str());
      return true;
    case CMD_IR_DB:
      ESP_LOGW(TAG, "'%s': IR-Protokoll '%s' wird nicht unterstuetzt - bitte anlernen",
               c.name.c_str(), c.protocol.c_str());
      return false;
    default:
      return false;
  }
}

bool RuntimeConfig::send_command(const std::string &device_id, const std::string &command_id) {
  const Device *d = this->find_device_(device_id);
  const Command *c = this->find_command_(d, command_id);
  if (c == nullptr) {
    this->last_result_ = "Befehl nicht gefunden: " + device_id + " / " + command_id;
    ESP_LOGW(TAG, "%s", this->last_result_.c_str());
    return false;
  }
  bool ok = this->exec_command_(*c);
  this->last_result_ = (ok ? "OK: " : "FEHLER: ") + d->name + " / " + c->name;
  return ok;
}

bool RuntimeConfig::send_command_by_name(const std::string &dn, const std::string &cn) {
  return this->send_command(dn, cn);
}

void RuntimeConfig::run_step_(std::shared_ptr<std::vector<Step>> steps, size_t idx) {
  if (idx >= steps->size())
    return;
  const Step &s = (*steps)[idx];
  uint32_t wait_after = 120;   // Standard-Abstand zwischen Befehlen
  if (s.is_delay) {
    wait_after = s.delay_ms;
  } else {
    this->send_command(s.device_id, s.command_id);
  }
  this->set_timeout(wait_after, [this, steps, idx]() { this->run_step_(steps, idx + 1); });
}

bool RuntimeConfig::run_activity(const std::string &activity_id) {
  const Activity *a = this->find_activity_(activity_id);
  if (a == nullptr) {
    this->last_result_ = "Aktivitaet nicht gefunden: " + activity_id;
    ESP_LOGW(TAG, "%s", this->last_result_.c_str());
    return false;
  }
  ESP_LOGI(TAG, "Aktivitaet '%s': %d Schritte", a->name.c_str(), (int) a->steps.size());
  auto steps = std::make_shared<std::vector<Step>>(a->steps);
  this->run_step_(steps, 0);
  this->last_result_ = "Aktivitaet laeuft: " + a->name;
  return true;
}

bool RuntimeConfig::run_activity_by_name(const std::string &n) { return this->run_activity(n); }

std::vector<std::string> RuntimeConfig::device_names() const {
  std::vector<std::string> v;
  for (auto &d : this->devices_)
    v.push_back(d.name);
  return v;
}

std::vector<std::string> RuntimeConfig::command_names(const std::string &q) const {
  std::vector<std::string> v;
  const Device *d = this->find_device_(q);
  if (d)
    for (auto &c : d->commands)
      v.push_back(c.name);
  return v;
}

std::vector<std::string> RuntimeConfig::activity_names() const {
  std::vector<std::string> v;
  for (auto &a : this->activities_)
    v.push_back(a.name);
  return v;
}

std::string RuntimeConfig::devices_json() const {
  std::string s = "[";
  bool first = true;
  for (auto &d : this->devices_) {
    if (!first)
      s += ",";
    first = false;
    s += "{\"name\":\"" + d.name + "\",\"transport\":\"" + d.transport + "\",\"cmds\":" +
         std::to_string(d.commands.size()) + "}";
  }
  s += "]";
  return s;
}

}  // namespace runtime_config
}  // namespace esphome

#endif  // USE_ESP32
