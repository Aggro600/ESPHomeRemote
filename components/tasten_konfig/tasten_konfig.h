#pragma once
// Tasten-Konfigurator (2026-10-03): Belegung der 24 Tasten je Aktivitaet und Geste
// (kurz / doppelt / lang), gepflegt in Home Assistant (Panel "Fernbedienung").
//
// - Speicher: NVS-Namensraum "tasten" (Blob "cfg" = JSON wie von HA geliefert, "h" = Hash).
// - Abgleich: HA kuendigt den Hash an (text_sensor homeassistant -> angekuendigt()),
//   weicht er ab, holt ein eigener Task die Datei per HTTP; Hash des Inhalts wird geprueft.
// - Gesten: eigene Zeitsteuerung je Taste (keine gemeinsamen gest_*-Variablen).
// - Ausfuehrung: Hooks (siehe open-remote-tasten.h), weil sie an YAML-ids haengt.
// Ohne gueltige oder ausgeschaltete Belegung meldet ereignis() false -> die eingebaute
// Logik der YAML laeuft wie bisher.

#include "esphome/core/component.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include <functional>
#include <map>
#include <vector>
#include <string>

namespace esphome {
namespace espidf_ble_keyboard { class EspidfBleKeyboard; }
namespace remote_transmitter { class RemoteTransmitterComponent; }
namespace tasten_konfig {

enum Geste : uint8_t { KURZ = 0, DOPPELT = 1, LANG = 2 };

struct Ctx {
  int taste;
  Geste geste;
  bool dunkel;   // war das Display beim (ersten) Druck aus?
  int aktivitaet;
};

class TastenKonfig : public Component {
 public:
  void set_host(const std::string &h) { host_ = h; }
  void set_key(const std::string &k) { key_ = k; }
  void set_times(uint32_t long_ms, uint32_t dbl_ms) { long_ms_ = long_ms; dbl_ms_ = dbl_ms; }
  void set_status(text_sensor::TextSensor *t) { status_ = t; }
  void set_transfer(text_sensor::TextSensor *t) { transfer_ = t; }

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void dump_config() override;

  void set_keyboard(espidf_ble_keyboard::EspidfBleKeyboard *kb) { kb_ = kb; }
  void set_transmitter(remote_transmitter::RemoteTransmitterComponent *tx) { tx_ = tx; }

  // ---- Hooks (gesetzt in open-remote-tasten.h, bewusst OHNE JSON-Typen: alles mit
  // ArduinoJson bleibt in dieser Uebersetzungseinheit, main.cpp ist an der l32r-Grenze) ----
  // Funktion der Remote ("menu_smart", ..., "voice_ptt_ende"); geste 0/1/2.
  std::function<void(const char *fn, int arg, int geste, bool dunkel)> intern;
  // Fortschritt der Uebertragung fuers Display: phase 1 = beginnt, 2 = laeuft (pct, -1 = unbekannt),
  // 3 = uebernommen, 4 = Fehler (text = Grund).
  std::function<void(int phase, int pct, const char *text)> anzeige;
  // Darf jetzt geladen werden (keine Sprache aktiv)? Sperrt dabei Schlaf/WLAN-Aus.
  std::function<bool()> darf_laden;

  // ---- Laufzeit ----
  bool aktiv() const { return doc_ != nullptr && an_; }
  // true = die Belegung hat das Ereignis verarbeitet. darf_neu=false: neue Drucke nicht
  // annehmen (z. B. Navigation im Menue), Loslassen eigener Tasten aber schon.
  bool ereignis(int taste, bool gedrueckt, int aktivitaet, bool dunkel, bool darf_neu);
  // Traegt die Taste in dieser Aktivitaet eine Funktion, die das Display wecken soll?
  bool weckt(int taste, int aktivitaet);
  void angekuendigt(const std::string &hash);
  void neu_laden() { if (soll_hash_.empty()) soll_hash_ = hash_.empty() ? std::string("00000000") : hash_; versuch_ = 0; laden_ab_ = millis(); erzwingen_ = true; }
  const std::string &hash() const { return hash_; }
  // Fuer das Menue (components/menue): ganze Konfiguration, Generation, Benachrichtigung.
  JsonObjectConst wurzel() const { return doc_ != nullptr ? doc_->as<JsonObjectConst>() : JsonObjectConst(); }
  uint32_t generation() const { return gen_; }
  const std::string &host() const { return host_; }
  const std::string &schluessel() const { return key_; }
  void bei_neuer_konfig(std::function<void()> &&f) { neu_cbs_.push_back(std::move(f)); }
  // Eingebaute Popups (config.popups.<name>): an? und ggf. eigene Menueseite statt der eingebauten.
  bool popup_an(const char *name) const;
  // Aktivitaeten aus der Konfiguration (2026-10-04): Anzahl (0 = keine Konfig), Name, Bluetooth-Platz.
  int aktivitaeten() const;
  std::string aktivitaet_name(int i) const;
  int aktivitaet_slot(int i) const;
  bool aktivitaet_im_dock(int i) const;   // "dock" (fehlt: die ersten vier)
  // Startseite (config.home, 2026-10-04): true = eigener Aufbau aktiv. Bereich in Rasterzellen (6 x 8, 40 px).
  bool home_eigen() const;
  bool home_bereich(const char *name, int &x, int &y, int &w, int &h, bool &an) const;
  std::string popup_ersatz(const char *name) const;
  // Schrittfolge ausfuehren (Menue-Kacheln): wie eine Tastengeste KURZ.
  void schritte(JsonArrayConst steps, bool dunkel = false) { folge_(steps, Ctx{0, KURZ, dunkel, 0}, 0, gen_); }

  // HA-Aufruf mit kurzer Warteschlange (WLAN/API kann nach dem Aufwachen noch fehlen).
  void ha_senden(const std::string &svc, std::map<std::string, std::string> d);

 protected:
  JsonObjectConst taste_cfg_(int aktivitaet, int taste);
  void schritt_(JsonObjectConst s, const Ctx &c, bool halten);
  void ble_(JsonObjectConst s, bool halten);
  void ha_(JsonObjectConst s);
  void ir_(JsonObjectConst s);
  bool api_ok_() const;
  bool haelt_irgendwas_() const;
  espidf_ble_keyboard::EspidfBleKeyboard *kb_{nullptr};
  remote_transmitter::RemoteTransmitterComponent *tx_{nullptr};
  void ausfuehren_(int aktivitaet, int taste, Geste g, bool dunkel);
  void folge_(JsonArrayConst steps, Ctx c, size_t ab, uint32_t gen);
  void wiederholen_(int taste, uint32_t ms);
  std::vector<std::function<void()>> neu_cbs_;
  bool anwenden_(const char *daten, size_t len, const std::string &h, bool speichern);
  void status_melden_();
  void melden_(int phase, int pct, const std::string &text);
  static void download_task_(void *arg);   // dauerhafter Arbeits-Task (Stapel im PSRAM)
  static void download_einmal_(TastenKonfig *self);
  void *worker_{nullptr};
  void download_starten_();

  std::string host_, key_;
  uint32_t long_ms_{400}, dbl_ms_{160};
  text_sensor::TextSensor *status_{nullptr};
  text_sensor::TextSensor *transfer_{nullptr};
  volatile size_t dl_gelesen_{0};
  volatile int dl_gesamt_{-1};
  int letzt_pct_{-2};
  uint32_t letzt_melden_{0};

  json::SpiRamAllocator alloc_;
  JsonDocument *doc_{nullptr};
  bool an_{false};
  std::string hash_;
  uint32_t gen_{0};

  // Abgleich
  std::string soll_hash_;
  bool erzwingen_{false};
  uint8_t versuch_{0};
  uint32_t laden_ab_{0};
  volatile bool dl_laeuft_{false};
  uint32_t dl_start_{0}, grund_log_{0};
  volatile bool dl_fertig_{false};
  char *dl_buf_{nullptr};
  size_t dl_len_{0};
  int dl_status_{0};
  std::string dl_url_, dl_soll_;

  // Gesten je Taste (Keycodes bis 102)
  struct Zustand {
    bool unten{false}, eigen{false}, lang_fertig{false}, doppel{false}, haelt{false}, wartet{false};
    bool wdh{false};   // "Kurz wiederholen" laeuft
    bool dunkel{false};
    int8_t aktivitaet{0};
    uint32_t los_ms{0};
  };
  Zustand z_[110];

  struct HaJob { std::string svc; std::map<std::string, std::string> d; uint32_t t0; };
  std::vector<HaJob> ha_q_;
};

}  // namespace tasten_konfig
}  // namespace esphome
