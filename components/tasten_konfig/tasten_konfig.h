#pragma once
// Tasten-Konfigurator (2026-10-03): Belegung der 24 Tasten je Aktivitaet und Geste
// (kurz / doppelt / lang), gepflegt in Home Assistant (Panel "Fernbedienung").
//
// - Speicher: NVS-Namensraum "tasten" (Blob "cfg" = JSON wie von HA geliefert, "h" = Hash).
// - Abgleich: HA kuendigt den Hash an (text_sensor homeassistant -> angekuendigt()),
//   weicht er ab, holt ein eigener Task die Datei per HTTP; Hash des Inhalts wird geprueft.
// - Gesten: eigene Zeitsteuerung je Taste (keine gemeinsamen gest_*-Variablen).
// - Ausfuehrung: Hooks (siehe esphomeremote-tasten.h), weil sie an YAML-ids haengt.
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

  // ---- Hooks (gesetzt in esphomeremote-tasten.h, bewusst OHNE JSON-Typen: alles mit
  // ArduinoJson bleibt in dieser Uebersetzungseinheit, main.cpp ist an der l32r-Grenze) ----
  // Funktion der Remote ("menu_smart", ..., "voice_ptt_ende"); geste 0/1/2.
  std::function<void(const char *fn, int arg, int geste, bool dunkel)> intern;
  // Menueseite als Popup oeffnen (Schritt int/popup_open mit "page", 2026-10-26: z. B. Lautstaerke lang -> Medien-Steuerung)
  std::function<void(const char *seite)> popup_oeffnen;
  // Fortschritt der Uebertragung fuers Display: phase 1 = beginnt, 2 = laeuft (pct, -1 = unbekannt),
  // 3 = uebernommen, 4 = Fehler (text = Grund).
  std::function<void(int phase, int pct, const char *text)> anzeige;
  // Darf jetzt geladen werden (keine Sprache aktiv)? Sperrt dabei Schlaf/WLAN-Aus.
  std::function<bool()> darf_laden;
  // Ohne eigene HA-Verbindung laden, weil der Abruf ueber das Dock geht (2026-10-25, -halink: HTTP-Vermittler mit
  // Dock-Weg). Leer = nur mit HA-Verbindung (wie bisher).
  std::function<bool()> laden_ueber_dock;
  // Neue Belegung, aber keine HA-Verbindung (2026-10-25): WLAN anfordern und kurz darauf warten - ueber WLAN sind die
  // ~65 kB in 1-2 s da, ueber das Dock ~1 min. true = noch warten, false = jetzt laden (WLAN da oder kommt nicht).
  std::function<bool()> wlan_holen;
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
  // Neue Belegung angekuendigt (auch ueber das Dock, "kh"), aber noch nicht geladen (2026-10-24)
  bool belegung_offen() const { return !soll_hash_.empty() && (soll_hash_ != hash_ || erzwingen_) && versuch_ < 8; }
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
  // Wert eines Startseiten-Bereichs (z. B. "ziel"/"label" der Knoepfe Menue/Apps, 2026-10-26) - auch ohne eigene Anordnung
  const char *home_wert(const char *bereich, const char *k) const;
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

  // Weck-Taste schon waehrend des Starts senden (2026-10-23, Einstellung weck_dock): aus on_boot direkt nach dock_direkt
  // (vor Display/LVGL). Nur wenn die Taste lediglich "kurz" belegt ist und alle Schritte Bluetooth/Infrarot/433 MHz
  // sind und mindestens einer ueber ein Dock geht - dann per Funk ans Dock (Ausweichwege wie sonst, laufen ggf. erst
  // nach dem Start weiter). true = gesendet, die Tastenmatrix soll die Weck-Taste nicht noch einmal melden.
  bool weck_frueh(int taste, int aktivitaet, bool dunkel);
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
  // Bluetooth ueber das Dock (2026-10-22, components/dock_hid): Weg der Bluetooth-Tasten, Einstellung bt_weg
  //   0 = automatisch (Remote, wenn sie selbst mit dem Geraet verbunden ist, sonst das Dock - z. B. direkt nach dem
  //       Aufwachen oder fuer ein Geraet einer anderen Aktivitaet), 1 = nur die Remote (wie frueher), 2 = Dock zuerst.
  // Ein Schritt kann den Weg selbst festlegen ("w"); Geraete nur am Dock: Schritt mit "dock" + "dp" (Platz am Dock).
  // Zuordnung Platz der Remote -> Dock-Platz: cfg.bt.dock = {"0": {"d": "dock_wz_couchtisch", "p": 0}, ...}
  std::function<int()> bt_weg_modus;
  // Touchpad (esphomeremote-maus.h): true = geht ueber das Dock (die Remote sendet dann nicht selbst)
  bool maus_ueber_dock(uint8_t knoepfe, int8_t dx, int8_t dy, int8_t rad);
  // Tastatur, Text, Makros, Knoepfe der Remote-Seiten ueber das Dock (2026-10-23): gleiches Ziel wie das Touchpad (Platz
  // der Aktivitaet am Dock, Einstellung bt_weg). Das Geraet muss nur am Dock gekoppelt sein. false = kein Dock-Ziel ->
  // der Aufrufer sendet selbst. `selbst` laeuft, wenn das Dock nicht antwortet oder das Geraet dort nicht verbunden ist.
  bool eingabe_ueber_dock(uint8_t art, uint16_t code, uint8_t mod, std::function<void()> &&selbst);
  bool text_ueber_dock(const std::string &text, std::function<void()> &&selbst);
  bool eingabe_dock_ziel(std::string &dock, uint8_t &platz, bool maus = false) const;
  // Dock-Platz (2026-10-23): bedient das Dock das Geraet dieses Platzes ("Dock zuerst" + Zuordnung "Am Dock")? Dann
  // verbindet sich die Remote nicht selbst (kb->set_hid_ruhe). Erreicht sie das Dock nicht, Notweg: 10 min selbst.
  bool platz_ueber_dock(int platz) const;
  // Sprache ueber das Dock (2026-10-24): sprache_ziel() legt fest, ob und an welches Dock/welchen Platz (gleiche Regel
  // wie Tasten: eingabe_dock_ziel), sprache_senden() schickt ein Paket (atv_voice dock_weg). sprache_lauschen_ms: nach
  // jeder Eingabe ueber das Dock den Funk so lange offen halten, damit "Fernseher will das Mikrofon" ankommt.
  bool sprache_ziel();
  bool sprache_senden(const std::vector<uint8_t> &paket);
  uint32_t sprache_lauschen_ms{0};
  // Werte der Remote ueber das Dock an HA (2026-10-23): nur ohne eigene HA-Verbindung, ohne Ausweg (verfaellt, wenn
  // kein Dock antwortet). HA: esphomeremote_konfig.remote_status (dockstatus.py). pumpen = sofort raus (vor Tiefschlaf)
  void status_ueber_dock(std::map<std::string, std::string> d, bool pumpen = false);
  // Allgemein: HA-Aktion svc ueber das Dock, nur ohne eigene HA-Verbindung, ohne Ausweg (verfaellt ohne Dock)
  // false = nicht abgeschickt (HA-Verbindung steht, kein Dock); fehlschlag: Dock hat nicht bestaetigt
  bool dock_aufruf(const std::string &svc, std::map<std::string, std::string> d, bool pumpen = false,
                   std::function<void()> fehlschlag = {});
  std::function<void()> ha_wecken;
  // Bluetooth-Notweg (2026-10-25, Konfigurator "bt_notweg"): 0 = aus (die Remote verbindet sich nie von selbst, wenn
  // das Dock nicht antwortet), 1 = bis das Dock wieder antwortet (hoechstens 10 min). notweg_vorbei: Platz neu anwenden.
  std::function<int()> notweg_modus;
  std::function<void()> notweg_vorbei;
  // Docks, die laut HA gerade online sind (Dock-Stand "do", 2026-10-25) - werden vor den anderen probiert
  void set_docks_online(std::vector<std::string> namen) { docks_online_ = std::move(namen); }
  // Stand (Startseite) beim Dock anfragen - Antwort ueber dock_direkt on_zustand (2026-10-19)
  bool zustand_anfragen();   // HA-Aufruf wartet: pausierten HA-Link wieder einschalten
  // Statusleiste (2026-10-20): Weg der HA-Befehle. 0 = eigene HA-Verbindung steht (oder Dock-Weg aus),
  // 1 = ueber das Dock (noch nichts bestaetigt),
  // 2 = ueber das Dock und das Dock hat zuletzt geantwortet, 3 = ueber das Dock, aber zuletzt kein Dock erreicht.
  int dock_weg() const;
  // Dock hat nach dem letzten Fehlschlag wieder geantwortet (auch mit eigener HA-Verbindung, anders als dock_weg)
  bool dock_erreichbar() const;
  void dock_pruefen();
  // Bluetooth-Tasten vor der Verbindung merken und nachsenden (sonst verwerfen), Einstellung weck_tasten
  void set_vor_verbindung_merken(bool m) { vor_verbindung_merken_ = m; }
  std::vector<std::string> dock_macs() const;   // Docks: das zuletzt bestaetigte zuerst (HTTP ueber das Dock)   // Stand beim Dock abfragen (nichts ausfuehren) - fuer das Statusleisten-Symbol

  // ---- Geraete-Einstellungen (2026-10-16): gepflegt im Konfigurator (cfg.einst = {"name": [wert, rev]}) ----
  // Die YAML meldet jede Einstellung an (Name = id des globals). Neue Konfiguration aus HA: Werte mit geaenderter
  // Revision uebernehmen (Aenderung im Konfigurator); gleiche Revision = Wert der Remote bleibt (am Geraet geaendert).
  // Die Remote meldet ihre Werte nach dem Start und bei jeder Aenderung an HA (einst_melden, JSON {"name": wert}).
  void einst_feld(const char *k, bool bol, std::function<int()> get, std::function<void(int)> set) {
    // gemeldet = aktueller Wert (2026-10-25): sonst galt nach jedem Aufwachen fast alles als geaendert. Volle
    // Meldung kommt ohnehin mit jeder HA-Verbindung (einst_voll_).
    const int jetzt = get ? get() : 0;
    einst_.push_back(EinstFeld{k, bol, std::move(get), std::move(set), jetzt});
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
  void ble_selbst_(uint8_t art, uint16_t code, uint8_t mod, int dev, bool halten);
  // Ziel am Dock fuer einen Schritt bzw. Platz der Remote; false = keins eingetragen
  bool bt_ziel_(JsonObjectConst s, int slot, std::string &dock, uint8_t &platz) const;
  // Ein Ereignis per Funk ans Dock; false = geht gerade nicht (sofort selbst ausweichen).
  // kein_funk: Dock hat nicht bestaetigt; nicht_da: Dock hat das Geraet gerade nicht verbunden.
  bool ble_dock_(const std::string &dock, uint8_t platz, uint8_t art, uint8_t aktion, uint16_t code, uint8_t mod,
                 std::function<void()> &&kein_funk, std::function<void()> &&nicht_da);
  void ble_ha_(const std::string &dock, uint8_t platz, uint8_t art, uint16_t code, uint8_t mod);
  void hid_antwort_(uint8_t maske, uint8_t platz, bool ok);
  struct DockHalt { bool an{false}; std::string dock; uint8_t platz{0}, art{0}, mod{0}; uint16_t code{0}; uint32_t naechst{0}; };
  DockHalt dock_halt_;
  int frueh_slot_{-1};
  uint32_t notweg_bis_{0};
  uint32_t notweg_ab_{0};
  uint32_t einst_dock_ms_{0};   // zuletzt Einstellungen ueber das Dock gemeldet
  uint32_t stand_nach_ms_{0};   // Stand kurz nach einem Befehl ueber das Dock abfragen (Popups sofort)
  std::vector<std::string> docks_online_;
  uint8_t sprache_mac_[6]{};
  uint8_t sprache_platz_{0};
  bool sprache_da_{false};
  void notweg_();   // Bluetooth-Platz der Aktivitaet beim fruehen Versand (die Tastatur kennt ihn noch nicht)
  bool frueh_geeignet_(JsonArrayConst steps, int slot) const;
  struct DockOffen { bool an{false}; uint8_t platz{0}; uint32_t ms{0}; std::function<void()> nicht_da; };
  DockOffen dock_offen_;
  struct Maus { int16_t dx{0}, dy{0}, rad{0}; uint8_t knoepfe{0}, gesendet{0}; bool offen{false}; std::string dock; uint8_t platz{0}; uint32_t ms{0}, ziel_ms{0}; bool ziel{false}; };
  Maus maus_;
  void maus_pumpen_(bool sofort = false);
  void ble_puffer_();   // vor der Bluetooth-Verbindung gedrueckte Tasten nachsenden
  void ha_(JsonObjectConst s);
  void ir_(JsonObjectConst s);
  void ir_selbst_(const std::vector<int32_t> &t, uint32_t f);
  // An ein Dock: zuerst direkt per Funk (dock_direkt), sonst `sonst` (ueber HA bzw. selbst) - auch spaeter,
  // wenn das Dock den direkten Weg nicht bestaetigt.
  void ha_ueber_dock_(const std::string &svc, std::map<std::string, std::string> d, std::vector<std::string> macs, size_t i);
  void an_dock_(const std::string &dock, int typ, uint32_t param, const std::vector<int32_t> &t, std::function<void()> &&sonst);
  void rf_(JsonObjectConst s);
  std::vector<std::string> docks_(JsonObjectConst s) const;
  static bool ist_blaster(const std::string &d);
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
  // Fortsetzen nach Abbruch (2026-10-25, Weg ueber das Dock brach mitten drin ab und fing wieder bei 0 an): der
  // angefangene Puffer bleibt fuer denselben Stand liegen, HA schickt ab dl_ab_ den Rest (HTTP 206).
  char *teil_buf_{nullptr};
  size_t teil_len_{0};
  std::string teil_hash_;
  size_t dl_ab_{0};
  volatile int dl_voll_{-1};   // erwartete Gesamtlaenge (ab + Content-Length), -1 = unbekannt

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
  bool einst_api_vorher_{false};   // HA-Verbindung beim letzten Pruefen (Flanke -> alles neu melden)
  uint32_t einst_ms_{0}, einst_log_ms_{0};
  void einst_uebernehmen_(JsonDocument *alt, JsonDocument *neu);
  void einst_pruefen_();

  struct HaJob { std::string svc; std::map<std::string, std::string> d; uint32_t t0; };
  std::vector<HaJob> ha_q_;
  uint32_t dock_fehl_ms_{0};   // zuletzt kein Dock erreicht (s. dock_weg)
  void dock_pruefen_(std::vector<std::string> macs, size_t i);
  // Tasten direkt nach dem Start/Aufwachen, bevor der Bluetooth-Host verbunden ist (2026-10-20): statt sie zu
  // verwerfen, kurz merken und nachsenden, sobald die Verbindung steht.
  struct BleJob { uint8_t art; uint16_t code; uint8_t mod; };   // art: 0 consumer, 1 key, 2 button
  std::vector<BleJob> ble_q_;
  bool vor_verbindung_merken_{true};
  uint32_t ble_da_ms_{0};
  uint32_t api_da_ms_{0};   // zuletzt mit HA verbunden (fuer "Ohne HA"-Belegungen)
};

}  // namespace tasten_konfig
}  // namespace esphome
