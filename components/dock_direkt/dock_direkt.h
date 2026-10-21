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
// Inhalt; je Absender eine laufende Nummer gegen Wiederholung mitgeschnittener Pakete. Die Nummer zaehlt ueber
// Neustarts weiter (NVS, in 1000er-Schritten vorausgespeichert) - das Dock nimmt nur steigende Nummern an
// (2026-10-19; vorher je Sitzung neu ab 1, ein Paket aus einer alten Sitzung liess sich wiederholen).
//
// Paket: 'O' 'R' ver(1) typ(1) sitzung(4) nr(4) param(4) anzahl(2) inhalt hmac(8)
//   typ 1 = Infrarot (param = Traeger Hz), 2 = 433 MHz (param = Wiederholungen): inhalt = anzahl Zeiten (varint, zigzag)
//   typ 3 = Home-Assistant-Aktion (2026-10-19): inhalt = anzahl Bytes "dienst\0schluessel\0wert\0..." - das Dock fuehrt
//           sie ueber seine (immer bestehende) HA-Verbindung aus, wie in der Original-Firmware ("Home Assistant via
//           dock"). Hat das Dock gerade keine HA-Verbindung, haelt es die Aktion bis zu 30 s.
//   typ 4 = Zustands-Anfrage Remote -> Dock (inhalt = Geraetename), typ 5 = Zustand Dock -> Remote (inhalt = JSON,
//           den HA dem Dock bei jeder Aenderung schickt: Startseite, Fernseher, Timer). So zeigt die Remote nach dem
//           Tiefschlaf sofort den aktuellen Stand, bevor ihre eigene HA-Verbindung steht (2026-10-19).
//   typ 6-9 = HTTP ueber das Dock (2026-10-20): die Remote holt Bilder/Konfiguration von HA, ohne eigene Verbindung.
//           Ein Abruf = zufaellige Abruf-Nr. (param). 6 = oeffnen (Dock verbindet sich mit HAs HTTP), 7 = Daten
//           (beide Richtungen; inhalt = Versatz u32 + Bytes), 8 = Ende (beide), 9 = Quittung (inhalt = empfangen u32).
//           Die Daten sind mit AES-128-CTR verschluesselt (Schluessel aus direkt_schluessel) - ESP-NOW selbst ist
//           offen, Cover-Adressen und Konfiguration sollen nicht mitlesbar sein. Laeuft je Seite in einem eigenen
//           Task (Remote: HTTP-Vermittler von ha_tunnel, Dock: eigener Task), nicht in loop().

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/preferences.h"
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#ifdef DD_EMPFANG
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#endif
#ifdef DD_HA
#include "esphome/components/api/custom_api_device.h"
#endif
#include <map>
#include <atomic>

namespace esphome {
namespace dock_direkt {

enum Typ : uint8_t { IR = 1, RF = 2, HA = 3, ZUSTAND_FRAGE = 4, ZUSTAND = 5, HTTP_AUF = 6, HTTP_DATEN = 7, HTTP_ZU = 8, HTTP_QUITT = 9 };

class DockDirekt : public Component
#ifdef DD_HA
    , public api::CustomAPIDevice
#endif
{
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
  // Remote: Home-Assistant-Aktion ueber das Dock (typ 3). Rueckgabe/Fehlschlag wie senden().
  bool senden_ha(const uint8_t mac[6], const std::string &dienst, const std::vector<std::pair<std::string, std::string>> &daten,
                 std::function<void()> &&fehlschlag);
  // Remote: Stand beim Dock anfragen; die Antwort kommt ueber on_zustand
  bool zustand_anfragen(const uint8_t mac[6], const std::string &name, std::function<void()> &&fehlschlag);
  void add_on_zustand(std::function<void(std::string)> &&f) { zustand_cbs_.push_back(std::move(f)); }
  // Dock: Stand einer Remote ablegen (HA-Aktion esphome.<dock>_zustand)
  void zustand_setzen(const std::string &remote, const std::string &text);
  // "AC:27:6E:76:BA:B0" -> 6 Byte
  static bool mac_lesen(const char *s, uint8_t out[6]);

  // Remote: Dock, das zuletzt bestaetigt hat (zuerst probieren)
  bool letztes_ok(uint8_t out[6]) const { if (!ok_gueltig_) return false; memcpy(out, ok_mac_, 6); return true; }
  uint32_t ok_ms() const { return ok_ms_; }

  // ---- HTTP ueber das Dock (2026-10-20), s. oben typ 6-9
  // Remote: Docks in dieser Reihenfolge probieren (aus loop() setzen, z. B. tasten_konfig::dock_macs()).
  void set_http_ziele(const std::vector<std::string> &macs);
  // Remote, aus dem HTTP-Task von ha_tunnel: Abruf vom lokalen Socket c ueber ein Dock weiterleiten. "vorab" bekommt
  // die schon gelesene Anfrage - false = kein Dock hat geantwortet, nichts an c geschrieben: der Aufrufer nimmt
  // einen anderen Weg und schickt "vorab" zuerst.
  bool http_relay(int c, std::vector<uint8_t> &vorab);
  // Dock: HAs HTTP-Adresse ("IP:Port")
  void set_ha_http(const std::string &hp) { ha_http_ = hp; }
  void http_dock_task_();   // oeffentlich nur fuer den Task-Einstieg
  uint32_t empfangen() const { return n_empfangen_; }
  uint32_t gesendet() const { return n_gesendet_; }

 protected:
  bool bereit_();
  void hmac_(const uint8_t mac[6], const uint8_t *d, size_t n, uint8_t out[8]) const;
  void naechster_versuch_();
  void funk_zurueck_();
  void verarbeiten_(const uint8_t mac[6], const uint8_t *d, size_t n);
  bool einreihen_(const uint8_t mac[6], Typ typ, uint32_t param, uint16_t anzahl, const std::vector<uint8_t> &inhalt,
                  std::function<void()> &&fehlschlag);

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
  ESPPreferenceObject kanal_pref_, nr_pref_;
  uint32_t nr_bis_{0};   // so weit ist die Nummer im NVS vorausgespeichert
  uint32_t kanal_pruef_ms_{0};
  uint8_t ok_mac_[6]{};
  bool ok_gueltig_{false};
  uint32_t ok_ms_{0};   // Zeitpunkt der letzten Bestaetigung (Statusleiste der Remote)

  // ---- HTTP ueber das Dock
  std::string ha_http_;
  uint8_t http_key_[16]{};   // AES-Schluessel (aus schluessel_ abgeleitet, setup)
  struct HttpPeer { uint8_t mac[6]; uint32_t sitzung, nr; };
  std::vector<HttpPeer> http_peers_;   // Dock: zuletzt gesehene Oeffnen-Pakete je Remote (Wiederholschutz)
  std::vector<std::string> http_ziele_;   // geschuetzt durch http_mtx_ (HTTP-Task liest)
  std::atomic<bool> http_funk_wunsch_{false}, http_funk_bereit_{false};   // Remote: HTTP-Task braucht den Funk
  std::atomic<uint32_t> http_nr_{0};
  bool http_paket_(const uint8_t mac[6], Typ typ, uint32_t id, const uint8_t *d, size_t n, std::vector<uint8_t> &out);
  bool http_lesen_(const uint8_t mac[6], const std::vector<uint8_t> &p, uint8_t &typ, uint32_t &id, std::vector<uint8_t> &inhalt);
  bool roh_senden_(const uint8_t mac[6], const std::vector<uint8_t> &p, int versuche);
  void aes_ctr_(uint8_t typ, uint32_t sitzung, uint32_t nr, uint32_t id, uint8_t *d, size_t n) const;

  // ---- Empfangen (Dock)
#ifdef DD_EMPFANG
  remote_transmitter::RemoteTransmitterComponent *ir_{nullptr}, *rf_{nullptr};
#endif
  struct Absender { uint8_t mac[6]; uint32_t sitzung, nr; };
  std::vector<Absender> absender_;
  std::map<std::string, std::string> zustaende_;   // Dock: Geraetename -> Stand (JSON)
  std::vector<std::function<void(std::string)>> zustand_cbs_;
  uint32_t antwort_bis_ms_{0};   // Remote: Funk bis dahin offen halten (Antwort auf die Zustands-Anfrage)
#ifdef DD_HA
  struct HaAuftrag { std::string dienst; std::map<std::string, std::string> daten; uint32_t t0; };
  std::vector<HaAuftrag> ha_warte_;   // HA gerade nicht verbunden: kurz halten
  void ha_ausfuehren_(HaAuftrag &a);
#endif
};

class ZustandTrigger : public Trigger<std::string> {
 public:
  explicit ZustandTrigger(DockDirekt *p) { p->add_on_zustand([this](std::string t) { this->trigger(t); }); }
};

class SendenTrigger : public Trigger<int> {
 public:
  explicit SendenTrigger(DockDirekt *p) { p->add_on_senden([this](int typ) { this->trigger(typ); }); }
};

}  // namespace dock_direkt
}  // namespace esphome
