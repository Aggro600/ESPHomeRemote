#pragma once
#include <vector>
// Logitech Z906: ESP32 between console and amplifier. See __init__.py.
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include <atomic>
#include <deque>
#include <string>
#include <freertos/semphr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace esphome::z906 {

class Z906;

// Main volume / subwoofer / centre / rear (level 0..43)
class Z906Level : public number::Number {
 public:
  void set_parent(Z906 *p, uint8_t kanal) { parent_ = p; kanal_ = kanal; }
 protected:
  void control(float value) override;
  Z906 *parent_{nullptr};
  uint8_t kanal_{0};
};

class Z906Input : public select::Select {
 public:
  void set_parent(Z906 *p) { parent_ = p; }
 protected:
  void control(size_t index) override;
  Z906 *parent_{nullptr};
};

class Z906Effect : public select::Select {
 public:
  void set_parent(Z906 *p) { parent_ = p; }
 protected:
  void control(size_t index) override;
  Z906 *parent_{nullptr};
};

class Z906StatusButton : public button::Button {
 public:
  void set_parent(Z906 *p) { parent_ = p; }
 protected:
  void press_action() override;
  Z906 *parent_{nullptr};
};

class Z906MitschnittButton : public button::Button {
 public:
  void set_parent(Z906 *p) { parent_ = p; }
 protected:
  void press_action() override;
  Z906 *parent_{nullptr};
};

class Z906 : public Component {
 public:
  // Capture of the first minutes after start (bytes + notes like pin voltages), output via button
  void notiz(const std::string &text);
  void verlauf_ausgeben();
  void set_pins(int krx, int ktx, int vrx, int vtx) { krx_ = krx; ktx_ = ktx; vrx_ = vrx; vtx_ = vtx; }
  void set_pegel(uint8_t kanal, Z906Level *n) { if (kanal < 4) pegel_n_[kanal] = n; }
  void set_eingang_select(Z906Input *s) { eingang_s_ = s; }
  void set_effekt_select(Z906Effect *s) { effekt_s_ = s; }
  void set_power_sensor(binary_sensor::BinarySensor *b) { power_b_ = b; }
  void set_mitschnitt(bool m) { mitschnitt_ = m; }

  // Calls from Home Assistant (main loop) -> queue -> bridge task
  void pegel_setzen(uint8_t kanal, int ziel);
  void eingang_setzen(int index);
  void effekt_setzen(int index);
  void status_lesen();

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::HARDWARE; }   // early: forwarding from the start

 protected:
  struct Befehl {
    uint8_t n{0};
    bool eigene_antwort{false};   // amp reply (AA ...) belongs to us - do not forward it to the console
    uint8_t b[100];
  };
  static void task_(void *arg);
  void task_schleife_();
  void vom_verstaerker_(uint8_t b);   // read along: amp -> console
  void von_konsole_(uint8_t b);
  // Interference filter (2026-10-07): while console/amp boot, both lines carry garbage, in which valid commands
  // occur by chance (capture: 22 speaker test, 10 headphones, 02/03/06 input...). The ESP turned that into clean
  // bytes - with a direct cable it would only be noise. Only protocol bytes pass; after an invalid byte the
  // direction is muted for 300 ms.
  uint32_t stoer_bis_[2]{0, 0};
  uint32_t gefiltert_[2]{0, 0};
  int k_pos_{-1};          // console sends an AA message itself: position in it (-1 = none)
  int k_len_{0};
  uint32_t k_frame_t0_{0};
  bool gueltig_(int richtung, uint8_t b) const;
  // Wake guard (2026-11-01): between the console's wake byte (30) and its power-on (11), a level command reaching
  // the amp makes the console abort the start and fall back to standby (reproduced: knob turned right after
  // switching on, or a level command from HA). Level steps from the console are dropped in this window (lost on
  // purpose - user decision); commands from HA wait. Only this change on top of the 2026-10-31 state: the echo pass
  // and the ring capture of the same day broke input 4 (amp restarted) and were taken out again.
  bool k_standby_{false};    // console in standby: after its 34 (sent on boot and when going to standby) or 36
  uint32_t weck_bis_{0};     // window open until then (0 = closed)
  uint32_t weck_an_{0};      // time of the power-on byte (11) inside the window
  bool weckfenster_(uint32_t jetzt) const { return weck_bis_ != 0 && (int32_t) (jetzt - weck_bis_) < 0; }
  void weckfenster_pruefen_(uint32_t jetzt);
  size_t ausgabe_pos_{0};
  bool ausgabe_laeuft_{false};
  void einzelbyte_(uint8_t b);
  void antwort_fertig_();
  void senden_(const uint8_t *b, size_t n, bool eigene_antwort = false);
  void schritt_publizieren_();

  int krx_{-1}, ktx_{-1}, vrx_{-1}, vtx_{-1};
  QueueHandle_t befehle_{nullptr};

  // State: written by the task, published by loop()
  std::atomic<int8_t> pegel_[4]{{-1}, {-1}, {-1}, {-1}};
  std::atomic<int8_t> eingang_{-1};   // 0..5
  std::atomic<int8_t> effekt_[6]{{-1}, {-1}, {-1}, {-1}, {-1}, {-1}};   // per input: 0 3D, 1 2.1, 2 4.1, 3 None
  std::atomic<int8_t> power_{-1};
  int8_t gemeldet_pegel_[4]{-2, -2, -2, -2};
  int8_t gemeldet_eingang_{-2}, gemeldet_effekt_{-2}, gemeldet_power_{-2};

  // Task state (task only)
  uint8_t mb_[64];
  int mb_pos_{-1};   // -1 = no AA message in progress
  uint32_t mb_t0_{0};
  bool mb_eigene_{false};
  bool eigene_antwort_offen_{false};
  uint32_t antwort_bis_{0};   // a request expecting an AA reply is pending until then
  uint32_t letzter_verkehr_{0};
  uint32_t weitergeleitet_k_{0}, weitergeleitet_v_{0}, verworfen_{0};
  uint32_t letzte_abfrage_{0}, letzter_bericht_{0};
  // Traffic capture (debug_traffic): bytes per direction, written as one line after 30 ms of silence
  bool mitschnitt_{false};
  char zeile_[2][200];
  int zeile_n_[2]{0, 0};
  uint32_t zeile_t_[2]{0, 0};
  void mitschnitt_byte_(int richtung, uint8_t b);
  void mitschnitt_flush_(int richtung, bool erzwingen);
  std::deque<std::string> verlauf_;
  SemaphoreHandle_t verlauf_lock_{nullptr};
  void verlauf_merken_(const char *text);

  Z906Level *pegel_n_[4]{nullptr, nullptr, nullptr, nullptr};
  Z906Input *eingang_s_{nullptr};
  Z906Effect *effekt_s_{nullptr};
  binary_sensor::BinarySensor *power_b_{nullptr};
};

}  // namespace esphome::z906
