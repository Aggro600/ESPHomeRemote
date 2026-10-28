#pragma once
// Dock als Bluetooth-Tastatur/Fernbedienung/Maus fuer mehrere Geraete (2026-10-22).
//
// Das Dock haengt am Netzteil und bleibt dauerhaft mit bis zu 4 Geraeten (Fernseher, Streamer, Tablet, Laptop ...)
// gekoppelt und verbunden. Die Remote schickt Tasten und Touchpad per ESP-NOW (dock_direkt, typ 10) - so kommt ein
// Tastendruck sofort an, auch wenn die Remote gerade schlaeft oder mit einem anderen Geraet verbunden ist.
//
// Teilt sich den Bluetooth-Stack mit ESPHome (esp32_ble + bluetooth_proxy laufen weiter):
//  - GATT-Server: eigener Rueckruf (ESPHome registriert keinen, solange esp32_ble_server fehlt) - laeuft im
//    Bluetooth-Task, daher ist der Verbindungs-Stand mit einer Sperre geschuetzt.
//  - GAP (Werbung, Kopplung): ueber die Rueckrufe von esp32_ble im Hauptprogramm.
//  - Verbindungen, bei denen das Dock Zentrale ist (Proxy), werden ignoriert (link_role).
// Eine Kennung fuer alle Geraete; jedes Geraet ("Platz") bekommt beim Lesen der Geraetebeschreibung seine eigene
// (mit/ohne Maus, volle Tastatur oder nur Ziffern) - Android TV baut sonst bei jedem Verbinden das Bild neu auf.
// Plaetze (Adresse, IRK, Name, Optionen) liegen im NVS; die Schluessel selbst verwaltet Bluedroid.

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <esp_gap_ble_api.h>
#include <esp_gatts_api.h>
#include <atomic>
#include <cstring>
#include <array>
#include <deque>
#include <vector>
#include <esp_gattc_api.h>
#include <functional>
#include <mutex>
#include <string>

namespace esphome {
namespace dock_hid {

static const uint8_t MAX_PLAETZE = 4;
static const uint8_t MAX_VERB = 6;

// Optionen je Platz
static const uint8_t OPT_MAUS = 1;   // Maus/Touchpad melden
static const uint8_t OPT_VOLL = 2;   // volle Tastatur (sonst nur Ziffern/Enter/Esc...)

// Ereignis (auch das Funkformat von dock_direkt typ 10: 6 Byte je Ereignis)
enum Art : uint8_t { CONSUMER = 0, TASTE = 1, KNOPF = 2, SYSTEM = 3, MAUS = 4 };
enum Aktion : uint8_t { TIPP = 0, DRUECKEN = 1, LOSLASSEN = 2, HALTEN = 3 };   // HALTEN = noch gedrueckt (Lebenszeichen)
struct Ereignis {
  uint8_t art, aktion;
  uint8_t d[4];   // CONSUMER: code lo/hi | TASTE: mod, code | KNOPF: n (1..8) | SYSTEM: code | MAUS: knoepfe, dx, dy, rad
};

struct Platz {
  uint8_t belegt;
  uint8_t optionen;
  uint8_t addr[6];   // Identitaetsadresse
  uint8_t irk[16];
  uint8_t hat_irk;
  char name[21];
};

class DockHid : public Component {
 public:
  void set_name(const std::string &n) { name_ = n; }
  void set_plaetze(uint8_t n) { anzahl_ = n > MAX_PLAETZE ? MAX_PLAETZE : n; }
  void set_status(text_sensor::TextSensor *t) { status_ = t; }
  void set_halten_ms(uint32_t ms) { halten_ms_ = ms; }
  void set_sprache(bool an) { sprachdienst_ = an; }   // Sprachdienst anbieten (YAML sprache:, 2026-10-24)
  void set_scan_status(text_sensor::TextSensor *t) { scan_status_ = t; }
  void set_scan_fenster_verbunden(uint32_t ms) { fe_verbunden_ = fe_verbunden_yaml_ = ms; }
  void set_scan_standard(uint32_t intervall_ms, uint32_t fenster_ms) {
    std_iv_ = ist_iv_ = intervall_ms;   // so laeuft der Tracker schon (gleiche Zeiten in der YAML)
    std_fe_ = ist_fe_ = fenster_ms;
  }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_BLUETOOTH; }

  // ---- Bedienung (HA-Aktionen, Konfigurator)
  // Platz p koppeln: 2 Minuten sichtbar, das naechste neue Geraet bekommt den Platz (ein altes auf p wird vergessen)
  void koppeln(int p, const std::string &name, int optionen);
  void koppeln_abbrechen();
  void vergessen(int p);
  void optionen(int p, const std::string &name, int optionen);
  // Platz p trennen und s Sekunden nicht werben (kein Wiederverbinden) - Test/Diagnose (2026-10-23)
  void trennen(int p, uint32_t s);

  // ---- Senden: false = Platz nicht verbunden
  bool senden(uint8_t p, const Ereignis &e);
  bool verbunden(uint8_t p) const;
  uint8_t maske() const;   // Bit je verbundenem Platz
  uint32_t empfangen() const { return n_ereignisse_; }

  // ---- Scan-Stufen des Proxys (2026-10-22, Konfigurator Dockingstation > Suchtempo)
  // HA (dockscan.py) gibt die Stufe vor: schnell, solange eine Remote fehlt (hoechstens X min), langsam, wenn alle da
  // sind. Dazu der Weckruf: fragt eine Remote nach dem Aufwachen ihren Stand ab (dock_direkt typ 4), sucht das Dock
  // weck_s Sekunden schnell - so findet es sie auch nach Stunden "langsam" sofort. Ohne Nachricht von HA (3 min) gilt
  // wieder der Standard aus der YAML. Waehrend einer Kopplung bleibt der Scan pausiert.
  void scan_stufe(uint32_t intervall_ms, uint32_t fenster_ms, const std::string &grund, uint32_t weck_s,
                  uint32_t weck_intervall_ms, uint32_t weck_fenster_ms, const std::string &remotes,
                  uint32_t fenster_verbunden_ms = 0);
  void weckruf(const std::string &remote);

  // ---- Sprache fuer den Fernseher (2026-10-24, Android-TV-Voice / ATVV wie atv_voice auf der Remote)
  // Das Dock bietet den Sprachdienst an, handelt mit dem Fernseher die Faehigkeiten aus (ADPCM 16 kHz) und reicht durch:
  //  Remote -> Fernseher: sprache(p, 0, Steuer-Nachricht) / sprache(p, 1, ADPCM) - Daten von dock_direkt typ 12.
  //  Fernseher -> Remote: MIC_OPEN / MIC_CLOSE (z. B. Mikrofon-Symbol in SmartTube) gehen ueber an_remote(p, art, daten)
  //  an die Remote, die zuletzt an Platz p bedient hat (art 2 = oeffnen, 3 = schliessen; Daten = Nachricht des TV).
  // Waehrend gesprochen wird, pausiert der Proxy-Scan und Bluetooth hat Vorrang (ein Funk fuer alles).
  bool sprache(uint8_t p, uint8_t art, const uint8_t *d, uint16_t n);
  std::function<void(uint8_t p, uint8_t art, const std::vector<uint8_t> &daten)> an_remote;

  // ---- Bluetooth (oeffentlich fuer die Rueckrufe)
  void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
  void gatts_(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
  // Eigene GATT-Client-Rolle nur fuer die MTU-Aushandlung (2026-10-24): der Fernseher fragt keine groessere MTU an -
  // ohne gehen ans Sprachdienst nur 20-Byte-Pakete raus (400/s bei 16 kHz) und der Stream staut sich. Wie die Remote
  // (espidf_ble_keyboard ble_mtu_client_anfordern): aux_open auf die bestehende Verbindung, dann send_mtu_req.
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param);

 protected:
  struct Verb {
    bool aktiv{false}, sicher{false};
    uint16_t conn_id{0};
    uint8_t bda[6]{};
    int8_t platz{-1};
    uint16_t mtu{23};
    uint32_t seit_ms{0};
    uint8_t addr_type{0};
    bool mtu_gefragt{false};
  };
  struct Spaeter { uint32_t ms; uint8_t p; Ereignis e; };
  // Sprache an den Fernseher: fester Ringpuffer (einmal beim Start geholt) statt vieler kleiner
  // Vektoren - die haben den Speicher bei langer Sprache zerstueckelt und das Dock zum Absturz gebracht.
  // Eintrag: [Platz | 0x80 = Steuerung][Laenge][Daten]
  uint8_t *ring_{nullptr};
  size_t ring_kopf_{0}, ring_fuss_{0}, sprache_bytes_{0};
  bool ring_rein_(uint8_t p, bool ctl, const uint8_t *d, uint8_t n);
  bool ring_vorne_(uint8_t &p, bool &ctl, uint8_t *d, uint8_t &n) const;
  void ring_weg_(uint8_t n);
  void ring_leeren_() { ring_kopf_ = ring_fuss_ = sprache_bytes_ = 0; }
  struct SprachZahl { uint32_t rein{0}, raus{0}, weg{0}, start_ms{0}; uint16_t mtu{0}; size_t max_belegt{0}; } sz_;
  uint32_t sprache_ms_{0};                 // letztes Paket der Remote
  int8_t sprache_platz_{-1};               // laeuft gerade (Platz), sonst -1
  std::mutex tv_mtx_;                      // Wuensche des Fernsehers aus dem Bluetooth-Task
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>> tv_wunsch_;   // (Platz, MIC_OPEN/MIC_CLOSE)
  void atvv_write_(uint16_t conn_id, const uint8_t *v, uint16_t n);
  void sprache_loop_(uint32_t jetzt);
  bool sprach_senden_(uint8_t p, bool ctl, const uint8_t *d, uint16_t n);

  struct Gehalten { bool an{false}; uint8_t art; uint8_t p; uint32_t bis_ms; };

  int platz_von_(const uint8_t bda[6]) const;
  bool aufloesen_(const uint8_t bda[6], const uint8_t irk[16]) const;
  void speichern_();
  void werbung_();
  void melden_();
  bool report_(uint8_t p, uint16_t handle, const uint8_t *d, uint16_t n);
  bool wirkt_(uint8_t p, const Ereignis &e, bool unten);
  void neu_gekoppelt_(Verb &v, const uint8_t bda[6]);
  size_t karte_(uint8_t optionen, uint8_t *out) const;

  std::string name_{"OpenRemote Dock"};
  uint8_t anzahl_{4};
  text_sensor::TextSensor *status_{nullptr};
  uint32_t halten_ms_{1200};
  bool sprachdienst_{false};
  esp_gatt_if_t gattc_if_{ESP_GATT_IF_NONE};
  uint8_t mtu_bda_[6]{};
  void mtu_anfragen_(uint16_t conn_id);   // Bluetooth-Task oder Hauptprogramm
  std::atomic<uint32_t> caps_n_{0};   // wie oft ein Fernseher die Faehigkeiten abgefragt hat

  Platz plaetze_[MAX_PLAETZE]{};
  ESPPreferenceObject pref_;

  mutable std::mutex mtx_;   // verb_ (Bluetooth-Task schreibt)
  Verb verb_[MAX_VERB];
  std::atomic<bool> geaendert_{true};

  bool gestartet_{false};
  esp_gatt_if_t gatts_if_{ESP_GATT_IF_NONE};

  int koppel_platz_{-1};
  uint32_t koppel_bis_{0};
  std::string koppel_name_;
  uint8_t koppel_opt_{0};

  // Werbung: 0 aus, 1 fuer bekannte (nicht auffindbar), 2 Kopplung (auffindbar)
  uint8_t werbung_soll_{0}, werbung_ist_{0};
  uint32_t pause_bis_{0};   // trennen(): bis dahin keine Werbung
  bool werbung_busy_{false};
  uint32_t werbung_ms_{0};

  std::deque<Spaeter> spaeter_;
  Gehalten gehalten_[MAX_PLAETZE * 3]{};
  uint32_t melden_ms_{0};
  uint32_t n_ereignisse_{0};

  // Proxy-Scan waehrend der Kopplung pausieren (2026-10-22): der C3 hat einen Funk fuer WLAN, Scan und Verbindungen -
  // mit laufendem Scan (1/3 der Zeit) verpasste er die ersten Pakete des Streamers ("hcif disc ... rsn 0x3e").
  bool scan_pause_{false};
  uint32_t scan_wieder_ms_{0};
  // Geraet (TV/Streamer) verbindet sich wieder: Proxy-Suche bis dahin aus (2026-10-27, s. scan_ in loop)
  uint32_t wieder_bis_{0};
  void scan_(bool an);
  void koppeln_ende_(uint32_t scan_nach_ms);
  uint32_t rtc_ms_{0};

  // Scan-Stufen (Zeiten in ms)
  void scan_stufe_pruefen_(uint32_t jetzt);
  text_sensor::TextSensor *scan_status_{nullptr};
  uint32_t std_iv_{150}, std_fe_{50}, fe_verbunden_{30}, fe_verbunden_yaml_{30};
  uint32_t ha_iv_{0}, ha_fe_{0}, ha_bis_{0};
  std::string ha_grund_;
  uint32_t weck_s_{30}, weck_iv_{150}, weck_fe_{50}, weck_bis_{0};
  std::string weck_remotes_, weck_von_;
  uint32_t ist_iv_{0}, ist_fe_{0};
  std::string ist_text_;
  uint32_t stufe_ms_{0};
  bool start_ruhe_{false};
};

extern DockHid *global_dock_hid;

}  // namespace dock_hid
}  // namespace esphome
