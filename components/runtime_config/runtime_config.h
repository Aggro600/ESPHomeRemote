#pragma once

// Liest die OMOTE-Konfigurator-Datei `runtime.json` von der SD-Karte und baut
// daraus eine Befehls- und Aktivitaeten-Registry. Damit ist die ESPHome-
// Firmware kompatibel zum Dateiformat des Original-Konfigurators
// (github: LORDSn1per/OpenRemote-Firmware) - OHNE dessen dynamische GUI.
// Die vorhandene LVGL-Menue-Bedienung bleibt; sie ruft nur send_command()/
// run_activity() auf.
//
// Format (schemaVersion 1):
//   devices[]   : {id, name, type, transport: "ir"|"bluetooth", protocol, commands[]}
//     commands[]: {id, name, ir:{type:"raw", frequency, data:"m s m s ..."}}   ODER
//                 {id, name, hid:{report:"keyboard"|"consumer", usage:<int>}}   ODER
//                 {id, name, protocol}   <- IRDB-Befehl, Code liegt in der IRDB
//                                            (Phase 2, hier noch nicht dekodiert)
//   activities[]: {id, name, steps:[{type:"command", deviceId, commandId} |
//                                    {type:"delay", ms}]}
//
// Aufruf: id(rc)->send_command("deviceId","commandId")
//         id(rc)->send_command_by_name("Chromecast","Home")
//         id(rc)->run_activity("act_...")   /  run_activity_by_name("Fetch TV")

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include <string>
#include <vector>
#include <map>

namespace esphome {

namespace sd_card { class SdCard; }
namespace remote_transmitter { class RemoteTransmitterComponent; }
namespace espidf_ble_keyboard { class EspidfBleKeyboard; }

namespace runtime_config {

class LoadTrigger : public Trigger<> {};

enum CmdKind : uint8_t { CMD_NONE, CMD_IR_RAW, CMD_HID_KEY, CMD_HID_CONSUMER, CMD_IR_DB };

struct Command {
  std::string id;
  std::string name;
  CmdKind kind{CMD_NONE};
  // IR raw
  uint32_t ir_freq{38000};
  std::vector<int32_t> ir_timings;   // + = mark, - = space (us)
  // HID
  uint16_t hid_usage{0};
  uint8_t hid_modifier{0};
  std::string protocol;              // fuer IRDB-Befehle (Info)
};

struct Device {
  std::string id;
  std::string name;
  std::string type;         // "IR device" / "Streamer" / "IRDB device"
  std::string transport;    // "ir" / "bluetooth"
  std::string protocol;
  std::vector<Command> commands;
};

struct Step {
  bool is_delay{false};
  uint32_t delay_ms{0};
  std::string device_id;
  std::string command_id;
};

struct Activity {
  std::string id;
  std::string name;
  std::vector<Step> steps;
};

// Auszug aus runtime.json "settings" (OpenRemote Studio). -1 / leer = nicht gesetzt.
struct RcSettings {
  int brightness{-1};        // 0..255
  int sleep_seconds{-1};     // Display-Timeout
  int wake_sensitivity{-1};  // 0..2 (Aufwachen durch Hochheben)
  int clock_enabled{-1};
  int bluetooth_enabled{-1};
  int wifi_enabled{-1};
  std::string remote_name;
  std::string timezone;
  std::string city;
};

class RuntimeConfig : public Component {
 public:
  void set_sd(sd_card::SdCard *sd) { sd_ = sd; }
  void set_path(const std::string &p) { path_ = p; }
  void set_learned_path(const std::string &p) { learned_path_ = p; }
  void set_transmitter(remote_transmitter::RemoteTransmitterComponent *t) { tx_ = t; }
  void set_keyboard(espidf_ble_keyboard::EspidfBleKeyboard *k) { kb_ = k; }
  void set_load_on_boot(bool b) { load_on_boot_ = b; }
  void set_ir_send_times(uint8_t n) { ir_send_times_ = n; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  // --- API ---
  bool load();                       // (neu) von SD laden - Karte muss gemountet sein
  bool loaded() const { return loaded_; }
  size_t device_count() const { return devices_.size(); }
  size_t command_count() const;
  size_t activity_count() const { return activities_.size(); }
  std::string schema_info() const { return schema_info_; }

  bool send_command(const std::string &device_id, const std::string &command_id);
  bool send_command_by_name(const std::string &device_name, const std::string &command_name);
  bool run_activity(const std::string &activity_id);
  bool run_activity_by_name(const std::string &activity_name);

  // Introspektion (fuer text_sensors / Menue)
  std::vector<std::string> device_names() const;
  std::vector<std::string> command_names(const std::string &device_id_or_name) const;
  std::vector<std::string> activity_names() const;
  std::string devices_json() const;   // kompakte Uebersicht

  std::string last_result() const { return last_result_; }
  void add_on_load_callback(std::function<void()> &&cb) { load_cbs_.add(std::move(cb)); }
  void register_load_trigger(LoadTrigger *t) { load_trigs_.push_back(t); }

  const RcSettings &settings() const { return settings_; }

  // --- IR-Learn: IRDB-Befehle durch Anlernen der echten FB in raw umwandeln ---
  // timings: + = mark, - = space (us). device/command per Name ODER id.
  // Schreibt nach learned.json und merged sofort in die Registry.
  bool learn_capture(const std::string &device, const std::string &command, uint32_t freq,
                     const std::vector<int32_t> &timings);
  size_t learned_count() const { return learned_count_; }

 protected:
  const Device *find_device_(const std::string &id_or_name) const;
  const Command *find_command_(const Device *d, const std::string &id_or_name) const;
  const Activity *find_activity_(const std::string &id_or_name) const;
  bool exec_command_(const Command &c);
  void run_step_(std::shared_ptr<std::vector<Step>> steps, size_t idx);
  void parse_raw_timings_(const std::string &s, std::vector<int32_t> &out);

  sd_card::SdCard *sd_{nullptr};
  remote_transmitter::RemoteTransmitterComponent *tx_{nullptr};
  espidf_ble_keyboard::EspidfBleKeyboard *kb_{nullptr};
  std::string path_{"/runtime.json"};
  std::string learned_path_{"/learned.json"};
  bool load_on_boot_{true};
  uint8_t ir_send_times_{1};

  void merge_learned_();
  Command *mutable_command_(const std::string &device, const std::string &command);

  // Flipper-".ir"-Dateien aus /devices (schreibt OpenRemote Studio dorthin,
  // wenn man Geraete aus der IRDB waehlt). index.txt wird bevorzugt, sonst der
  // Ordner gescannt.
  size_t load_ir_device_files_();
  bool parse_ir_file_(const std::string &path);
  size_t ir_file_device_count_{0};

  bool loaded_{false};
  size_t learned_count_{0};
  size_t irdb_unresolved_{0};   // IRDB-Befehle, deren Protokoll wir nicht kennen
  std::string schema_info_, last_result_;
  std::vector<Device> devices_;
  std::vector<Activity> activities_;
  RcSettings settings_;
  CallbackManager<void()> load_cbs_;
  std::vector<LoadTrigger *> load_trigs_;
};

}  // namespace runtime_config
}  // namespace esphome

#endif  // USE_ESP32
