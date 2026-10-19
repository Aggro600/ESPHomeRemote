#pragma once
// Direkter Funkweg Remote -> Dock (2026-10-18), ohne Home Assistant dazwischen.
//
// Warum ESP-NOW (WLAN-Funk ohne Router) und nicht Bluetooth:
//  - Das Dock haengt am Netzteil, sein WLAN ist immer an (power_save none) -> es hoert ESP-NOW jederzeit mit,
//    ohne dass Scan-Fenster oder Verbindungsplaetze des Bluetooth-Proxys gebraucht werden.
//  - Unicast mit Empfangsbestaetigung der Gegenstelle (MAC-ACK): die Remote weiss sofort, ob das Dock es hat,
//    und kann sonst ueber HA oder selbst senden. Bluetooth-Werbung (31 Byte) haette weder ACK noch Platz fuer
//    angelernte Rohsignale; eine eigene GATT-Verbindung kostet Plaetze und braucht Hunderte ms zum Aufbau.
//  - Bis 1470 Byte je Paket (ESP-NOW v2): auch lange Rohsignale (Klimaanlage) passen in ein Paket.
// Die Remote sendet auf dem Kanal ihres WLANs (= Kanal des Docks, gleicher Router). Ist ihr WLAN aus
// ("Nur Bluetooth", Ruhezustand), schaltet sie den Funk fuer ein paar ms ohne Anmeldung ein (Kanal gemerkt,
// notfalls alle Kanaele) und danach wieder aus.
//
// Schutz: HMAC-SHA256 (gekuerzt auf 8 Byte) mit gemeinsamem Schluessel aus secrets.yaml ueber Absender-MAC und
// Inhalt; je Absender Sitzung + laufende Nummer gegen Wiederholung mitgeschnittener Pakete.
//
// Paket: 'O' 'R' ver(1) typ(1) sitzung(4) nr(4) param(4) anzahl(2) zeiten(varint, zigzag) hmac(8)
//   typ 1 = Infrarot (param = Traeger Hz), 2 = 433 MHz (param = Wiederholungen)

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include <functional>
#include <string>
#include <vector>

#ifdef DD_EMPFANG
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#endif

namespace esphome {
namespace dock_direkt {

enum Typ : uint8_t { IR = 1, RF = 2 };

class DockDirekt : public Component {
 public:
  void set_schluessel(const std::string &k) { schluessel_ = k; }
#ifdef DD_EMPFANG
  void set_ir(remote_transmitter::RemoteTransmitterComponent *t) { ir_ = t; }
  void set_rf(remote_transmitter::RemoteTransmitterComponent *t) { rf_ = t; }
#endif
  void add_on_senden(std::function<void(int typ)> &&f) { senden_cbs_.push_back(std::move(f)); }

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Remote: an das Dock mit dieser WLAN-MAC senden. false = geht gerade nicht (WLAN verbindet sich, Signal zu
  // lang, ESP-NOW nicht bereit) - dann sofort selbst ausweichen. true = unterwegs; kommt keine Bestaetigung,
  // ruft die Komponente spaeter (aus loop) `fehlschlag` auf.
  bool senden(const uint8_t mac[6], Typ typ, uint32_t param, const std::vector<int32_t> &zeiten,
              std::function<void()> &&fehlschlag);
  // "AC:27:6E:76:BA:B0" -> 6 Byte
  static bool mac_lesen(const char *s, uint8_t out[6]);

  uint32_t empfangen() const { return n_empfangen_; }
  uint32_t gesendet() const { return n_gesendet_; }

 protected:
  bool bereit_();
  void hmac_(const uint8_t mac[6], const uint8_t *d, size_t n, uint8_t out[8]) const;
  void naechster_versuch_();
  void funk_zurueck_();
  void verarbeiten_(const uint8_t mac[6], const uint8_t *d, size_t n);

  std::string schluessel_;
  bool init_{false};
  uint32_t init_versuch_ms_{0};
  std::vector<std::function<void(int)>> senden_cbs_;
  uint32_t n_empfangen_{0}, n_gesendet_{0};

  // ---- Senden (Remote)
  struct Auftrag {
    uint8_t mac[6];
    std::vector<uint8_t> paket;
    std::function<void()> fehlschlag;
  };
  std::vector<Auftrag> warte_;     // noch nicht gesendet
  bool unterwegs_{false};
  Auftrag aktuell_;
  bool funk_geliehen_{false};      // WLAN war aus, Funk nur fuer ESP-NOW eingeschaltet
  std::vector<uint8_t> kanaele_;   // noch zu probierende Kanaele (nur bei geliehenem Funk)
  uint32_t gesendet_ms_{0};
  uint32_t funk_ab_ms_{0};         // geliehener Funk: erst ab dann senden
  uint32_t sitzung_{0}, nr_{0};
  uint8_t kanal_{0};               // zuletzt bekannter WLAN-Kanal (gespeichert)
  ESPPreferenceObject kanal_pref_;
  uint32_t kanal_pruef_ms_{0};

  // ---- Empfangen (Dock)
#ifdef DD_EMPFANG
  remote_transmitter::RemoteTransmitterComponent *ir_{nullptr}, *rf_{nullptr};
#endif
  struct Absender { uint8_t mac[6]; uint32_t sitzung, nr; };
  std::vector<Absender> absender_;
};

class SendenTrigger : public Trigger<int> {
 public:
  explicit SendenTrigger(DockDirekt *p) { p->add_on_senden([this](int typ) { this->trigger(typ); }); }
};

}  // namespace dock_direkt
}  // namespace esphome
