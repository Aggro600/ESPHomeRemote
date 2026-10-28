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
//   typ 10 = Bluetooth-Eingabe ueber das Dock (2026-10-22): param = Platz am Dock (0..3) | 0x100 "Antwort erwuenscht",
//           inhalt = anzahl Ereignisse a 6 Byte (dock_hid::Ereignis: art, aktion, 4 Byte). Das Dock ist dauerhaft mit
//           Fernseher/Tablet/Laptop gekoppelt (components/dock_hid) und gibt sie sofort weiter.
//   typ 11 = Antwort Dock -> Remote: param = verbundene Plaetze (Bitmaske), inhalt = Platz, ok (1 = angekommen).
//           Kommt immer, wenn der Platz nicht verbunden ist (die Remote sendet dann selbst), sonst nur auf Wunsch.

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
#ifdef DD_HID
#include "esphome/components/dock_hid/dock_hid.h"
#endif
#include <map>
#include <atomic>

namespace esphome {
namespace dock_direkt {

enum Typ : uint8_t { IR = 1, RF = 2, HA = 3, ZUSTAND_FRAGE = 4, ZUSTAND = 5, HTTP_AUF = 6, HTTP_DATEN = 7, HTTP_ZU = 8, HTTP_QUITT = 9, HID = 10, HID_ANTWORT = 11,
                     SPRACHE = 12 };   // 12: Sprache fuer den Fernseher ueber das Dock (2026-10-24), s. senden_sprache

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
#ifdef DD_HID
  void set_hid(dock_hid::DockHid *h) { hid_ = h; }
#endif

  void setup() override;
  void loop() override;
  void dump_config() override;
  // Direkt nach dem WLAN (250), noch vor Display (245) und LVGL: so kann die Remote die Weck-Taste schon waehrend
  // des Starts ans Dock schicken (2026-10-23, Einstellung weck_dock). Vorher AFTER_WIFI (200).
  float get_setup_priority() const override { return 249.0f; }

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
  // Remote: Bluetooth-Eingabe ueber das Dock (typ 10). ereignisse = n * 6 Byte. Rueckgabe/Fehlschlag wie senden();
  // die Antwort des Docks (typ 11) kommt ueber on_hid_antwort(dock-mac, maske, platz, ok).
  bool senden_hid(const uint8_t mac[6], uint8_t platz, bool antwort, const std::vector<uint8_t> &ereignisse,
                  std::function<void()> &&fehlschlag);
  void add_on_hid_antwort(std::function<void(const uint8_t *mac, uint8_t maske, uint8_t platz, bool ok)> &&f) {
    hid_cbs_.push_back(std::move(f));
  }
  // Remote: Sprache fuer den Fernseher ueber das Dock (typ 12, 2026-10-24). Das Dock reicht den Sprachdienst des
  // Fernsehers (Android-TV-Voice, ATVV) an Platz `platz` durch: inhalt = Abschnitte [art][laenge lo][laenge hi][Daten],
  // art 0 = Steuer-Nachricht (AUDIO_START/AUDIO_STOP, unveraendert), 1 = Audio (ADPCM-Bytes am Stueck).
  bool senden_sprache(const uint8_t mac[6], uint8_t platz, const std::vector<uint8_t> &inhalt, std::function<void()> &&fehlschlag);
  // Remote: Wunsch des Fernsehers vom Dock (art 2 Mikrofon oeffnen, 3 schliessen; Daten = MIC_OPEN/MIC_CLOSE)
  void add_on_sprache(std::function<void(uint8_t platz, uint8_t art, const uint8_t *d, uint16_t n)> &&f) {
    sprache_cbs_.push_back(std::move(f));
  }
  size_t warteschlange() const { return warte_.size() + (unterwegs_ ? 1 : 0); }
  // Remote, Start aus dem Tiefschlaf (2026-10-23): auch senden, waehrend das WLAN sich noch verbindet - auf dem Kanal,
  // auf dem es gerade verbindet (fast_connect = Kanal des Routers = Kanal des Docks). Nur waehrend pumpen().
  void set_frueh(bool f) { frueh_ = f; }
  // Remote: loop() selbst drehen, bis alles gesendet ist (hoechstens max_ms) - fuer den Versand waehrend setup()
  void pumpen(uint32_t max_ms);
  // Funk nach dem Senden noch so lange offen halten (Antwort des Docks abwarten, WLAN der Remote aus)
  void antwort_abwarten(uint32_t ms) { if ((int32_t) (millis() + ms - antwort_bis_ms_) > 0) antwort_bis_ms_ = millis() + ms; }
  // Dock: Stand einer Remote ablegen (HA-Aktion esphome.<dock>_zustand)
  void zustand_setzen(const std::string &remote, const std::string &text);
  // "AA:BB:CC:DD:EE:05" -> 6 Byte
  static bool mac_lesen(const char *s, uint8_t out[6]);

  // ---- Heimnetz als zweiter Weg (2026-10-23)
  // ESP-NOW klappt nur auf demselben Kanal. Haengen Remote und Dock an verschiedenen Zugangspunkten (FRITZ!Box und
  // Repeater auf verschiedenen Kanaelen), kommt nichts an. Ist das WLAN der Remote verbunden und bestaetigt das Dock
  // nicht, geht dasselbe Paket (gleiche Pruefsumme, gleiche Nummer - kein doppeltes Ausfuehren) per UDP an die IP des
  // Docks; danach 5 min gleich so. Das Dock antwortet auf dem Weg, auf dem die Remote zuletzt kam.
  // Paket: 'O','N', Absender-MAC (6), ESP-NOW-Paket. Port NETZ_PORT.
  void ip_setzen(const uint8_t mac[6], const std::string &ip);   // Remote: IP eines Docks (aus der Konfiguration "dip")
  // 0 automatisch (Funk, sonst Heimnetz), 1 nur Funk, 2 Heimnetz zuerst (Remote-Einstellung dock_netz)
  std::function<int()> netz_modus;
  uint32_t netz_gesendet() const { return n_netz_; }

  // Remote: Dock, das zuletzt bestaetigt hat (zuerst probieren)
  bool letztes_ok(uint8_t out[6]) const { if (!ok_gueltig_) return false; memcpy(out, ok_mac_, 6); return true; }
  uint32_t ok_ms() const { return ok_ms_; }
  // Remote: WLAN verbindet sich gerade - Funk zum Dock kurz gesperrt (kein Zeichen, dass das Dock fehlt)
  bool funk_belegt() const;

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
    bool gesendet{false};   // auf dem aktuellen Kanal schon abgeschickt
    uint8_t nochmal{2};     // so oft nachlegen, bevor es als "nicht erreicht" gilt
  };
  std::vector<Auftrag> warte_;     // noch nicht gesendet
  bool unterwegs_{false};
  Auftrag aktuell_;
  bool funk_geliehen_{false};      // WLAN war aus, Funk nur fuer ESP-NOW eingeschaltet
  bool frueh_{false};              // s. set_frueh
  bool wlan_bereit_frueh_();       // WLAN verbindet gerade, Funk laeuft schon (frueher Versand)
  std::vector<uint8_t> kanaele_;   // noch zu probierende Kanaele (nur bei geliehenem Funk)
  uint32_t gesendet_ms_{0};
  uint32_t funk_ab_ms_{0};         // geliehener Funk: erst ab dann senden
  uint32_t sitzung_{0}, nr_{0};
  uint8_t kanal_{0};               // zuletzt bekannter WLAN-Kanal (gespeichert)
  // Kanal, auf dem das Dock zuletzt bei ausgeschaltetem WLAN bestaetigt hat (2026-10-23): haengt das Dock an einem
  // anderen Zugangspunkt (Repeater) als die Remote, ist das nicht der WLAN-Kanal - dann zuerst diesen probieren
  uint8_t dock_kanal_{0};
  ESPPreferenceObject kanal_pref_, nr_pref_, dock_kanal_pref_, ok_pref_;   // ok_pref_: zuletzt erreichtes Dock
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

  // ---- Heimnetz (UDP)
  struct NetzZiel { uint8_t mac[6]; uint32_t ip; uint32_t bevorzugt_bis; };   // ip in Netz-Reihenfolge
  std::vector<NetzZiel> netz_;
  int udp_{-1};
  uint32_t n_netz_{0};
  bool netz_gemeldet_{false};
  NetzZiel *netz_ziel_(const uint8_t mac[6]);
  bool udp_bereit_();
  bool udp_senden_(const NetzZiel &z, const std::vector<uint8_t> &p);
  bool netz_statt_funk_();   // aktuell_ uebers Heimnetz schicken (Erfolg wie eine Funk-Bestaetigung)
  void udp_lesen_();
  bool echt_(const uint8_t mac[6], const uint8_t *d, size_t n) const;

  // ---- Empfangen (Dock)
#ifdef DD_EMPFANG
  remote_transmitter::RemoteTransmitterComponent *ir_{nullptr}, *rf_{nullptr};
#endif
  struct Absender { uint8_t mac[6]; uint32_t sitzung, nr; };
  std::vector<Absender> absender_;
  std::map<std::string, std::string> zustaende_;   // Dock: Geraetename -> Stand (JSON)
  std::vector<std::function<void(std::string)>> zustand_cbs_;
  std::vector<std::function<void(uint8_t, uint8_t, const uint8_t *, uint16_t)>> sprache_cbs_;
  // Dock: wer zuletzt an welchem Bluetooth-Platz bedient hat (Ziel fuer Wuensche des Fernsehers) + Wiederholungen
  struct PlatzRemote { uint8_t mac[6]; uint32_t ms; bool da; };
  PlatzRemote platz_remote_[4]{};
  struct AnRemote { uint8_t mac[6]; std::vector<uint8_t> inhalt; uint8_t platz; uint8_t rest; uint32_t ab_ms; uint32_t nr; bool unterwegs; };
  uint32_t an_remote_nr_{0};
  std::vector<AnRemote> an_remote_;
  void an_remote_senden_(uint8_t platz, uint8_t art, const std::vector<uint8_t> &daten);
  std::vector<std::function<void(const uint8_t *, uint8_t, uint8_t, bool)>> hid_cbs_;
#ifdef DD_HID
  dock_hid::DockHid *hid_{nullptr};
#endif
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
