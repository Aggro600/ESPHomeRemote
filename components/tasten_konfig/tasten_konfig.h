#pragma once
// Tasten-Konfigurator (2026-10-03): Belegung der 24 Tasten je Aktivitaet und Geste
// (kurz / doppelt / lang), gepflegt in Home Assistant (Panel "Fernbedienung").
//
// - Speicher: NVS-Namensraum "tasten" (Blob "cfg" = JSON wie von HA geliefert, "h" = Hash).
// - Abgleich: HA kuendigt den Hash an (text_sensor homeassistant -> angekuendigt()),
//   weicht er ab, holt ein eigener Task die Datei per HTTP; Hash des Inhalts wird geprueft.
// - Gesten: eigene Zeitsteuerung je Taste (keine gemeinsamen gest_*-Variablen).
// - Ausfuehrung: Hooks (siehe open-remote-tasten.h), weil sie an YAML-ids haengt.
// Ohne gueltige oder ausgeschaltete Belegung meldet ereignis() false: dann bedienen die Tasten nur die
// Seiten der Remote (Navigation), nichts geht an ein Geraet.

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
namespace dock_direkt { class DockDirekt; }
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
  void set_direkt(dock_direkt::DockDirekt *d) { direkt_ = d; }

  // ---- Hooks (gesetzt in open-remote-tasten.h, bewusst OHNE JSON-Typen: alles mit
  // ArduinoJson bleibt in dieser Uebersetzungseinheit, main.cpp ist an der l32r-Grenze) ----
  // Funktion der Remote ("menu_smart", ..., "voice_ptt_ende"); geste 0/1/2.
  std::function<void(const char *fn, int arg, int geste, bool dunkel)> intern;
  // Fortschritt der Uebertragung fuers Display: phase 1 = beginnt, 2 = laeuft (pct, -1 = unbekannt),
  // 3 = uebernommen, 4 = Fehler (text = Grund).
  std::function<void(int phase, int pct, const char *text)> anzeige;
  // Darf jetzt geladen werden (keine Sprache aktiv)? Sperrt dabei Schlaf/WLAN-Aus.
  std::function<bool()> darf_laden;
  // Folgemodus fuers Display: name + Restzeit in ms; ms = 0 = Modus vorbei.
  std::function<void(const char *name, uint32_t ms)> modus_anzeige;

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
  // Aktivitaeten aus der Konfiguration (2026-10-04): Anzahl (0 = keine Konfig), Name, Bluetooth-Platz.
  int aktivitaeten() const;
  std::string aktivitaet_name(int i) const;
  int aktivitaet_slot(int i) const;
  bool aktivitaet_im_dock(int i) const;   // "dock" (fehlt: die ersten vier)
  // Startseite (config.home, 2026-10-04): true = eigener Aufbau aktiv. Bereich in Rasterzellen (6 x 8, 40 px).
  bool home_eigen() const;
  bool home_bereich(const char *name, int &x, int &y, int &w, int &h, bool &an) const;
  // Folgemodus (2026-10-15, cfg.modi): belegt einzelne Tasten fuer s Sekunden um, z. B. Kanal +/-
  // dimmt die Haengelampe, nachdem sie per Rot doppelt geschaltet wurde. Jeder Druck einer
  // Modus-Taste verlaengert die Zeit. Start per Schritt {"t":"modus","id":..} oder Menue-Eintrag "modus".
  void modus_starten(const char *id);
  void modus_beenden();
  bool modus_taste(int taste) const;   // gehoert die Taste gerade dem Folgemodus?
  // Aktivitaet startet/endet (2026-10-16): Schritte der Pseudo-Tasten 900 ("Beim Starten") / 901 ("Beim Beenden").
  void aktivitaet_ereignis(int aktivitaet, bool start) { if (aktiv()) ausfuehren_(aktivitaet, start ? 900 : 901, KURZ, false); }
  // Schrittfolge ausfuehren (Menue-Kacheln): wie eine Tastengeste KURZ.
  // Teil der Konfiguration (z. B. "gesten", "makros"), leer ohne Konfiguration. Nur bis zur naechsten Uebernahme gueltig.
  JsonVariantConst konfig(const char *k) const {
    if (doc_ == nullptr) return JsonVariantConst();
    return doc_->as<JsonObjectConst>()[k];
  }
  void schritte(JsonArrayConst steps, bool dunkel = false) { folge_(steps, Ctx{0, KURZ, dunkel, 0}, 0, gen_); }
  // Taste kurz "druecken" wie ihre Belegung in der Aktivitaet (Bewegungsgesten "wirkt wie Taste", 2026-10-17)
  void taste_kurz(int taste, int aktivitaet, bool dunkel = false) { if (aktiv()) ausfuehren_(aktivitaet, taste, KURZ, dunkel); }

  // Einen IR-Schritt (JSON wie in der Belegung) sofort senden - Test aus dem Konfigurator, Sendeweg wie eingestellt.
  void ir_test(const std::string &json);
  // Name des Bluetooth-Platzes i (0..3) aus dem Konfigurator, leer = nicht vergeben.
  std::string slot_name(int i) const;
  // Kameras fuer die Seiten "Kamerabild"/"Live-Bild" (Konfigurator: cfg.kameras): Anzahl, Name, camera.*-Entitaet
  int kameras() const;
  std::string kamera_name(int i) const;
  std::string kamera_entitaet(int i) const;
  // Verknuepfungen aus dem Konfigurator (cfg.ents, 2026-10-19): HA-Entitaet einer Rolle ("tv", "tts", "adb",
  // "multiroom" ...), leer = nicht gewaehlt
  std::string ent(const char *rolle) const;

  // HA-Aufruf mit kurzer Warteschlange (WLAN/API kann nach dem Aufwachen noch fehlen).
  void ha_senden(const std::string &svc, std::map<std::string, std::string> d);
  // HA-Aktion eines Tastenschritts: je nach Einstellung "HA-Befehle ueber das Dock" (ha_dock_modus: 0 aus, 1 nur ohne
  // HA-Verbindung, 2 immer zuerst) per Funk ans Dock, sonst/als Ausweg wie ha_senden (2026-10-19)
  void ha_aufruf(const std::string &svc, std::map<std::string, std::string> d);
  std::function<int()> ha_dock_modus;
  std::function<void()> ha_wecken;   // HA-Aufruf wartet: pausierten HA-Link wieder einschalten

  // ---- Geraete-Einstellungen (2026-10-16): gepflegt im Konfigurator (cfg.einst = {"name": [wert, rev]}) ----
  // Die YAML meldet jede Einstellung an (Name = id des globals). Neue Konfiguration aus HA: Werte mit geaenderter
  // Revision uebernehmen (Aenderung im Konfigurator); gleiche Revision = Wert der Remote bleibt (am Geraet geaendert).
  // Die Remote meldet ihre Werte nach dem Start und bei jeder Aenderung an HA (einst_melden, JSON {"name": wert}).
  void einst_feld(const char *k, bool bol, std::function<int()> get, std::function<void(int)> set) {
    einst_.push_back(EinstFeld{k, bol, std::move(get), std::move(set), 0});
  }
  std::function<void()> einst_angewendet;                   // nach Uebernahme: Seiten auffrischen, Wirkung
  std::function<void(const std::string &)> einst_melden;    // Werte an HA (Ereignis)

 protected:
  JsonObjectConst taste_cfg_(int aktivitaet, int taste);
  JsonArrayConst steps_(JsonObjectConst kc, Geste g) const;   // mit Ersatz "<geste>_offline" ohne HA
  bool hat_(JsonObjectConst kc, Geste g) const;
  JsonObjectConst modus_keys_() const;
  void modus_verlaengern_(int taste = -1);   // taste: eigene Zeit der Modus-Taste ("s"), sonst die des Modus
  void schritt_(JsonObjectConst s, const Ctx &c, bool halten);
  void ble_(JsonObjectConst s, bool halten);
  void ha_(JsonObjectConst s);
  void ir_(JsonObjectConst s);
  void ir_selbst_(const std::vector<int32_t> &t, uint32_t f);
  // An ein Dock: zuerst direkt per Funk (dock_direkt), sonst `sonst` (ueber HA bzw. selbst) - auch spaeter,
  // wenn das Dock den direkten Weg nicht bestaetigt.
  void ha_ueber_dock_(const std::string &svc, std::map<std::string, std::string> d, std::vector<std::string> macs, size_t i);
  void an_dock_(const std::string &dock, int typ, uint32_t param, const std::vector<int32_t> &t, std::function<void()> &&sonst);
  void rf_(JsonObjectConst s);
  std::vector<std::string> docks_(JsonObjectConst s) const;
  bool api_ok_() const;
  bool haelt_irgendwas_() const;
  espidf_ble_keyboard::EspidfBleKeyboard *kb_{nullptr};
  remote_transmitter::RemoteTransmitterComponent *tx_{nullptr};
  dock_direkt::DockDirekt *direkt_{nullptr};
  void ausfuehren_(int aktivitaet, int taste, Geste g, bool dunkel);
  void folge_(JsonArrayConst steps, Ctx c, size_t ab, uint32_t gen);
  void wiederholen_(int taste, uint32_t ms);
  void lang_wiederholen_(int taste, uint32_t ms);
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

  // Folgemodus: Index in cfg.modi (-1 = keiner), gueltig fuer Generation modus_gen_
  int modus_{-1};
  uint32_t modus_gen_{0}, modus_bis_{0}, modus_ms_{0};

  struct EinstFeld { std::string k; bool bol; std::function<int()> get; std::function<void(int)> set; int gemeldet; };
  std::vector<EinstFeld> einst_;
  bool einst_voll_{true};   // alle Werte melden (nach dem Start)
  uint32_t einst_ms_{0}, einst_log_ms_{0};
  void einst_uebernehmen_(JsonDocument *alt, JsonDocument *neu);
  void einst_pruefen_();

  struct HaJob { std::string svc; std::map<std::string, std::string> d; uint32_t t0; };
  std::vector<HaJob> ha_q_;
  uint32_t api_da_ms_{0};   // zuletzt mit HA verbunden (fuer "Ohne HA"-Belegungen)
};

}  // namespace tasten_konfig
}  // namespace esphome
