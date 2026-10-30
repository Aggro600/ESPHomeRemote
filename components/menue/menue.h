#pragma once
// Konfigurierbares Menue (2026-10-03). Daten: Konfiguration["menu"] aus tasten_konfig.
//
// "menu": {
//   "on": true, "start": "main",
//   "pages": [ { "id": "main", "title": "Menü", "remember": true, "flow": true,
//                "items": [ { "t": "special", "page": "media", "label": "Medienplayer", "w": 6, "h": 1 }, ... ] } ]
// }
// Item-Typen (t): page (Unterseite, target), special (eingebaute Seite, page), action (steps wie
// bei den Tasten), toggle / light / cover / sensor / number / select (entity, optional steps statt Standard),
// camera (Klingelbild), setting (Einstellung der Remote), text.
// Raster: 6 Spalten, 40-px-Zeilen. flow=true: Items fliessen in ihrer Reihenfolge (w/h), sonst x/y.

#include "esphome/core/component.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace tasten_konfig { class TastenKonfig; }
namespace menue {

// Allocator fuer PSRAM (2026-10-04, Speichertest): kleine Bloecke < 512 B landen sonst automatisch
// im knappen internen RAM (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL) - die Zustands-/Seitenpuffer des
// Menues kosteten so ~4,8 KB, die WLAN, HA-API und BT-Kopplung fehlten.
template<typename T> struct PsramAlloc {
  using value_type = T;
  PsramAlloc() = default;
  template<typename U> PsramAlloc(const PsramAlloc<U> &) {}
  T *allocate(size_t n) {
    void *p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr) p = malloc(n * sizeof(T));
    return (T *) p;
  }
  void deallocate(T *p, size_t) { free(p); }
  template<typename U> bool operator==(const PsramAlloc<U> &) const { return true; }
  template<typename U> bool operator!=(const PsramAlloc<U> &) const { return false; }
};
using PStr = std::basic_string<char, std::char_traits<char>, PsramAlloc<char>>;
template<typename K, typename V> using PMap = std::map<K, V, std::less<K>, PsramAlloc<std::pair<const K, V>>>;
template<typename T> using PVec = std::vector<T, PsramAlloc<T>>;

enum Typ : uint8_t { T_TEXT, T_PAGE, T_SPECIAL, T_ACTION, T_TOGGLE, T_LIGHT, T_COVER, T_SENSOR, T_KAMERA, T_EINST, T_ZAHL, T_AUSWAHL,
                    T_PLAYER, T_WIDGET };
// T_WIDGET (2026-10-27, widgets.cpp): t "widget", art uhr | wetter | akku | medien | timer | kamera | kombi. Gross ab h >= 2,
// sonst schmal (eine Zeile). Kombi: "teile" (bis zu 3 aus uhr/wetter/akku/medien), Wetter "we", Medien "me";
// "mz" immer | nur (Medien nur waehrend der Wiedergabe) | ueber (Medien uebernimmt waehrend der Wiedergabe).
// Medien "leer": "weg" = ohne Wiedergabe keine Zeilen (die Seite rueckt zusammen). Kamera "takt" (s), Uhr "datum" vor|nach|aus.
// Licht (2026-10-29): "lichter" [{e, n}], "gruppe" (fuer "Alle"), "szenen" [{e, n}], "ziel" (Seite beim Antippen des Kopfs),
// "lteile" (lampen, hell, temp, farbe, palette, szenen; fehlt = alle, soweit Platz). Bedienung ueber esphomeremote_konfig.licht.
enum WidgetArt : uint8_t { W_KEIN, W_UHR, W_WETTER, W_AKKU, W_MEDIEN, W_TIMER, W_KAMERA, W_KOMBI, W_LICHT };
// Rollen der Unter-Widgets (Item::wo)
enum WRolle : uint8_t { WR_ZEIT, WR_DATUM, WR_W_ICON, WR_W_TEMP, WR_W_TEXT, WR_W_FC, WR_A_WERT, WR_A_ICON, WR_A_BAR, WR_M_TITEL,
                        WR_M_SUB, WR_M_BAR, WR_M_PLAY, WR_M_BILD, WR_T_ZEIT, WR_T_BAR, WR_T_NAME, WR_K_BILD, WR_K_PH,
                        WR_L_POWER, WR_L_ICON, WR_L_NAME, WR_L_INFO, WR_L_PCT, WR_L_CHIPS, WR_L_HELL, WR_L_TEMP, WR_L_FARBE,
                        WR_L_PAL, WR_L_SZENEN, WR_ANZAHL };
// T_PLAYER (2026-10-26): Medien-Elemente, jedes frei platzierbar mit eigener Entitaet (Item::teil):
// t "mtitel" = Titel/Interpret/Zustand, "mtaste" (fn) = Taste, "vpreset" (wert %) = Lautstaerke setzen.
// Lautstaerke-Regler ist ein T_ZAHL mit media_player (ok: "mute", st: Schritt in %). Seiten-Option "vol": Lauter/
// Leiser/Stumm der Remote steuern diesen Player, solange die Seite offen ist.
enum PlayerTeil : uint8_t { P_KEIN, P_INFO, P_MUTE, P_PREV, P_PLAY, P_NEXT, P_PRESET, P_PLAYNUR, P_PAUSE, P_STOP, P_VOLUP,
                            P_VOLDN, P_SHUFFLE, P_REPEAT, P_POWER };
// T_ZAHL (t: "number", 2026-10-07): number / input_number (value with slider) or media_player (volume 0..100, OK = on/off)
// T_AUSWAHL (t: "select", 2026-10-07): select / input_select - OK = next option, D-pad left/right = previous/next

// Einstellung der Remote (t: "setting", src: Widget-ID einer eingebauten Einstellungsseite, 2026-10-04):
// typ 'b' = Knopf (Klick weiterreichen), 's' = Regler, 'i' = nur Anzeige, 'g' = Linien-Grafik (obj = lv_line).
// wert = Label mit dem aktuellen Wert.
struct EinstRef {
  char typ{0};
  lv_obj_t *obj{nullptr};
  lv_obj_t *wert{nullptr};
  uint8_t gruppe{31};   // Auffrisch-Gruppe (Bit fuer einst_auffrischen), 31 = keine
};

struct Item {
  Typ typ{T_TEXT};
  int16_t x{0}, y{0}, w{6}, h{1};   // int16: lange Seiten haben > 127 Rasterzeilen (int8 lief ueber -> Zeile -120)
  JsonObjectConst j;          // Rohdaten (label, icon, entity, target, page, steps ...)
  lv_obj_t *obj{nullptr};     // Karte
  lv_obj_t *wert{nullptr};    // Zustandstext
  lv_obj_t *regler{nullptr};  // Licht-Regler
  bool name_wert{false};      // kleine Kachel: Zustand = Farbe des Namens
  EinstRef einst;             // T_EINST: ferngesteuertes Widget
  lv_obj_t *name{nullptr};    // Beschriftung (Gestaltung: Farbe, Groesse, Ausrichtung)
  lv_obj_t *ico{nullptr};     // Symbol (Gestaltung: Farbe "ic", Ausrichtung "ia")
  uint32_t bg{0x1A1F27};      // Kartenfarbe (eigene Farbe aus "bg", sonst Standard)
  // Anzeige-Zeile mit Namen (z. B. Akku: Spannung) bzw. Grafik: der Cursor faehrt auch darueber, damit man die
  // ganze Seite durchblaettern kann (2026-10-07). OK macht dort nichts.
  bool info{false};
  bool durchsichtig{false};   // Karte ohne Hintergrund (Anzeige-Zeile): nur mit Fokus hinterlegt
  bool gestapelt{false};      // Name und Wert passen nicht in eine Zeile: Name oben (umbrechend), Wert darunter
  uint8_t teil{P_KEIN};       // Medien-Element (T_PLAYER)
  uint8_t preset{0};          // P_PRESET: Lautstaerke in Prozent
  lv_obj_t *zusatz{nullptr};  // P_INFO: Interpret; sonst zweite Zeile ("sub")
  lv_obj_t *badge{nullptr};   // Abzeichen oben rechts ("badge")
  int32_t rand{-1};           // eigene Rahmenfarbe ("rc"), -1 = wie Hintergrund
  bool gesperrt{false};       // Bedingung ("wenn") nicht erfuellt: grau bzw. versteckt, keine Aktion
  uint8_t wart{W_KEIN};       // T_WIDGET: Art
  uint8_t wbau{0};            // T_WIDGET: gebaute Teile (Bits 1 << WidgetArt), Medien-Umschaltung bei Kombi
  int8_t wslot{-1};           // T_WIDGET: Bildplatz (wbild) fuer Kamera/Cover, -1 = keiner
  uint32_t wtakt_ms{0};       // T_WIDGET: naechstes Nachladen des Bilds
  lv_obj_t *wo[WR_ANZAHL]{};  // T_WIDGET: Unter-Widgets nach Rolle
  int8_t lwahl{-1};           // W_LICHT: gewaehlte Lampe (-1 = alle)
  uint32_t lsend_ms{0};       // W_LICHT: letzter Aufruf beim Ziehen (hoechstens alle 350 ms)
  bool fokusierbar() const {
    if (typ == T_WIDGET) return !gesperrt && (wart == W_MEDIEN || wart == W_TIMER || wart == W_KOMBI || wart == W_LICHT);
    return typ != T_TEXT && teil != P_INFO && !gesperrt && !(typ == T_EINST && einst.typ != 'b' && einst.typ != 's' && !info);
  }
};

struct Seite {
  PStr id;
  JsonObjectConst j;
};

struct EntZustand {
  PStr state;
  int bri{-1};
  int pos{-1};
  PStr unit;
  float mn{0}, mx{100}, schritt{1};   // number/input_number: range (T_ZAHL)
  PMap<PStr, PStr> attr;   // weitere Attribute (Popup-Ausloeser, Kachel-Quellen "e:a")
  uint32_t pos_ms{0};       // media_player: millis(), zu denen media_position galt (Fortschrittsbalken)
};

class Menue : public Component {
 public:
  void set_tk(tasten_konfig::TastenKonfig *tk) { tk_ = tk; }
  // Aus der YAML (on_boot), nach dem LVGL-Aufbau:
  void set_anzeige(lv_obj_t *root, lv_obj_t *titel, lv_obj_t *zurueck_lbl, lv_obj_t *zurueck_btn) {
    root_ = root; titel_ = titel; zurueck_lbl_ = zurueck_lbl; zurueck_btn_ = zurueck_btn;
  }
  // X/Zurueck per OK ausgeloest (wie Antippen des Knopfes).
  std::function<void()> zurueck_taste;
  void set_fonts(const lv_font_t *text, const lv_font_t *klein, const lv_font_t *icons, const lv_font_t *gross = nullptr,
                 const lv_font_t *sehr_klein = nullptr, const lv_font_t *sehr_gross = nullptr) {
    f_text_ = text; f_klein_ = klein; f_icon_ = icons; f_gross_ = gross; f_xs_ = sehr_klein; f_xl_ = sehr_gross;
  }
  // Eingebaute Seite zeigen (esphomeremote-tasten.h). Rueckgabe: deren cur_page-Nummer, -1 = unbekannt.
  std::function<int(const std::string &name)> spezial;
  // Menue verlassen (Startseite).
  std::function<void()> nach_hause;
  // Popup-Ausloeser (page.popup.open/close): Seite als Popup zeigen bzw. schliessen.
  std::function<void(const std::string &seite, bool wecken)> popup_auf;
  std::function<void(const std::string &seite)> popup_zu;
  // Popup mit eingebautem Inhalt (Seite "inhalt", z. B. "tastatur", 2026-10-13): statt der Menueseite zeigt die
  // YAML ihre eigene Seite. wert = Wert des ausloesenden Wechsels (Tastatur: "zahlen" -> Ziffernblock).
  std::function<void(const std::string &inhalt, const std::string &wert, bool wecken)> inhalt_auf;
  std::function<void(const std::string &inhalt)> inhalt_zu;
  // Derselbe Oeffnen-Wert kommt erneut (Dock-Stand, 2026-10-26): weggelegte Tastatur nach Ablauf wieder zeigen
  std::function<void(const std::string &inhalt, const std::string &wert)> inhalt_noch;
  // Display war aus: Inhalts-Popups (Tastatur) beim naechsten Wert neu bewerten
  void pegel_vergessen() { pegel_.clear(); }
  // Ereignis der Remote als Popup-Ausloeser (z. B. art "aktivitaet", wert = Name der gewaehlten Aktivitaet)
  void ereignis(const std::string &art, const std::string &wert);
  // Popup-Ausloeser-Zustaende aus dem Dock-Stand (2026-10-24), s. menue.cpp
  void zustaende_von_dock(JsonArrayConst werte);
  // Kamerabild-Kachel (t: "camera", src: "klingel_haus" | "klingel_wohnung"): Bildquelle aus der YAML.
  std::function<const void *(const std::string &quelle)> bild;
  void bild_neu();   // Bild wurde neu geladen -> sichtbare Kamera-Kacheln auffrischen
  // Einstellungen (t: "setting"): Widget zur ID finden und die eingebauten Seiten auffrischen
  std::function<EinstRef(const std::string &key)> einstellung;
  std::function<void(uint32_t maske)> einst_auffrischen;   // nur die Gruppen dieser Seite (Bits aus EinstRef::gruppe)

  // Widgets (2026-10-27, widgets.cpp): Uhrzeit (false = noch nicht gestellt; wtag 0 = Montag), Akku der Remote,
  // Bildplaetze fuer Kamera/Cover (online_image in der YAML: wbild liefert das Bild, wbild_laden holt eine URL,
  // wbild_fertig meldet die YAML nach dem Download)
  std::function<bool(int &h, int &m, int &wtag, int &tag, int &monat)> zeit;
  std::function<void(int &pct, bool &laedt)> akku;
  std::function<const void *(int slot)> wbild;
  std::function<void(int slot, const std::string &url)> wbild_laden;
  void wbild_fertig(int slot);
  void set_widget_fonts(const lv_font_t *uhr, const lv_font_t *wetter) { f_uhr_ = uhr; f_wetter_ = wetter; }
  // Startseite je Aktivitaet (2026-10-27): diese Instanz zeigt nur eine Seite (keine Popups/Ausloeser, kein Fokus);
  // Unterseiten-Kacheln oeffnen das richtige Menue (seite_oeffnen)
  void set_nur_anzeige(bool n) { nur_anzeige_ = n; }
  std::function<void(const std::string &seite)> seite_oeffnen;
  void startseite(const std::string &id);   // Seite id zeigen (leer = nichts)
  const std::string &startseite_id() const { return start_zeigt_; }
  // Menue (nicht Popup) direkt auf Seite id oeffnen (von der Startseite aus): Startseite + Seite im Stapel
  void oeffnen_auf(const std::string &id);

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  bool aktiv() const { return an_ && !seiten_.empty(); }
  bool hat_seite(const std::string &id) const { return seite_index_(id) >= 0; }
  // Eingebauter Inhalt einer Seite ("inhalt", z. B. "tastatur"), leer = normale Menueseite
  std::string seiten_inhalt(const std::string &id) const {
    const int i = seite_index_(id);
    return i < 0 ? std::string() : std::string(seiten_[i].j["inhalt"] | "");
  }
  // Knopf oben links eines Popups (2026-10-27, Konfigurator "pknopf"): "min" = minimieren (wie die Menuetaste, die
  // Menuetaste holt es zurueck), sonst X = schliessen. inhalt_knopf: der des zuletzt ausgeloesten eingebauten Inhalts.
  bool seiten_knopf_min(const std::string &id) const {
    const int i = seite_index_(id);
    return i >= 0 && !strcmp(seiten_[i].j["pknopf"] | "", "min");
  }
  bool inhalt_knopf_min() const { return inhalt_knopf_min_; }
  void set_inhalt_knopf_min(bool m) { inhalt_knopf_min_ = m; }
  // Seite als Popup zeigen (HA-Dienst menue_popup, z. B. Kamera-Popup). Auch bei ausgeschaltetem Menue.
  void popup(const std::string &id);
  bool ist_popup() const { return popup_; }
  const std::string &popup_seite() const { return popup_id_; }

  // Menue oeffnen: merken=true -> zuletzt gezeigte Seite, wenn sie (bzw. ein Vorgaenger) "merken" hat.
  void oeffnen(bool merken);
  void anzeigen();               // aktuelle Seite neu aufbauen (Rueckkehr aus einer Spezialseite)
  void einblenden(uint32_t ab);   // Karten von oben nach unten einblenden (Menue oeffnen)
  // Gemerkte eingebaute Seite wieder oeffnen (Menue-Taste im Merkfenster), wie per Eintrag mit "merken"
  void spezial_oeffnen(const std::string &name);
  bool zurueck();                // false = war schon oben
  void fokus(int dx, int dy);
  void ok();
  // Hardware-Taste auf einer Menueseite (vor der Navigation). true = verbraucht.
  //  - Halte-Knopf (item.hold) mit Fokus: OK druecken = Aktion, loslassen = item.release
  //  - Tastenmodus (Knopf mit Funktion "keymode"): Eintraege mit item.key folgen der Taste
  //    (druecken/loslassen), Zurueck beendet nur den Tastenmodus
  bool taste(int k, bool gedrueckt);
  void tastenmodus(int an);   // 1 an, 0 aus, -1 umschalten
  bool tastenmodus() const { return tmodus_; }
  void verlassen();              // beim Schliessen: Merken vorbereiten, Widgets loeschen
  // Spezialseite, die zuletzt aus dem Menue geoeffnet wurde (cur_page-Nummer), -1 = keine
  int einstieg() const { return einstieg_; }
  bool einstieg_merken() const { return einstieg_merken_; }

 protected:
  void laden_();
  int seite_index_(const std::string &id) const;
  void bauen_(bool alles = false);   // alles=false: nur der sichtbare Teil sofort, der Rest in loop() (bau_weiter_)
  void bau_eins_(size_t i);           // Karte items_[i] anlegen
  void bau_weiter_();
  bool flow_{true};   // Seite im Fluss-Layout; sonst feste x/y - dort waechst keine Karte von selbst (2026-10-11)
  std::vector<Item> wartend_;         // noch nicht angelegte Eintraege (Reihenfolge = Index in items_)
  size_t wartend_pos_{0};
  uint32_t rest_ms_{0};
  uint32_t zurueck_fc_{0xF1F3F5};   // Rahmenfarbe des markierten Zurueck/X (Seite "bfc")
  uint32_t zurueck_bg_{0x3B82F6};   // Farbe des Zurueck/X-Knopfs (Seite "bc")
  bool zurueck_x_{false};           // Seite "bsym": x = Knopf zeigt X und schliesst das Menue
  uint32_t einbl_t0_{0}, einbl_bis_{0}, einbl_ab_{0};   // Karten-Einblendung (einblenden)
  void einbl_item_(Item &it);
  // Seitenwechsel im Menue (2026-10-13): alte Seite rutscht weg, neue kommt nach. r = +1 vorwaerts
  // (alte nach links, neue von rechts), -1 zurueck (umgekehrt)
  int rutsch_{0};
  lv_obj_t *alt_{nullptr};          // Karten der verlassenen Seite waehrend des Rutschens
  lv_obj_t *alt_abheben_();
  void alt_weg_();
  void rutschen_(int r);
  // Titel rutscht mit der Seite (2026-10-13): alter Titel fliegt hinter das blaue Zurueck-Feld bzw. rechts aus dem
  // Display, der neue kommt von der anderen Seite. r = +1 vorwaerts, -1 zurueck; ohne alten Titel nur der neue.
  void titel_anim_(int r, uint32_t verz);
  void titel_weg_();
  void titel_einbl_();
  int32_t titel_ty_{0};
  static inline Menue *titel_menue_{nullptr};
  std::string titel_alt_;
  lv_obj_t *titel_fenster_{nullptr};   // Ausschnitt rechts vom blauen Feld mit beiden Titeln (nur waehrend der Animation)
  int titel_r_{0};
  lv_obj_t *titel_ende_{nullptr};
  int32_t titel_x0_{0}, titel_weit_{0};
  void leeren_();
  void karte_(Item &it);
  void zustand_(Item &it);
  void aktualisieren_(const std::string &entity);
  bool bedingungen_ok_(JsonObjectConst pp);
  void popup_ausloesen_(const Seite &s, bool auf, const std::string &wert);
  // Popup "exklusiv" (2026-10-13): solange es offen ist, warten andere Popups und gehen danach auf
  bool exklusiv_offen_(const std::string &ausser) const;
  std::vector<std::pair<std::string, std::string>> warten_;   // Seite, Ausloeser-Wert
  uint32_t warten_ab_{0};
  void abonnieren_(const std::string &entity, const char *attr);
  void zustand_neu_(const std::string &entity, const std::string &a, const std::string &s);
  void zahl_setzen_(Item &it, float wert);
  // Medien-Steuerung: Lautstaerke um d Prozent (sofort angezeigt), Stumm umschalten, Lauter/Leiser/Stumm der Remote
  void player_lautst_(const std::string &e, int d);
  void lautst_setzen_(const std::string &e, int v);
  void player_stumm_(const std::string &e);
  void player_karte_(Item &it);
  void player_zustand_(Item &it);
  void farbe_(Item &it, bool an, uint32_t farbe);
  // Kachel-Optionen (2026-10-26): Anordnung "lay", zweite Zeile "sub", Abzeichen "badge", Wert-Texte "map"/"nk"/"eh",
  // Stil je Zustand "zst", Bedingung "wenn", Regler "vmin"/"vmax"/"live"/"rf", Rahmen "rad"/"rc"/"transp",
  // lang druecken "lang", Sicherheitsabfrage "sicher", Preset-Leiste (vpreset "werte"), Medien "z2"/"lauf"/"bar"
  void layout_eigen_(Item &it, lv_obj_t *k, const char *ic, const char *label, bool mit_wert, char lay);
  void regler_einzeilig_(Item &it, lv_obj_t *k, lv_obj_t *sl);
  void extras_(Item &it);
  void extras_zustand_(Item &it, std::string &wert);
  std::string quelle_(JsonObjectConst q, const Item &it) const;
  const EntZustand *ez_(const char *e) const;
  bool sicher_ok_(Item &it);
  int lautst_grenze_(const std::string &e, int v) const;
  void fortschritt_(Item &it);
  static void lang_cb_(lv_event_t *e);
  static void live_cb_(lv_event_t *e);
  static void seg_cb_(lv_event_t *e);
  uint32_t live_ms_{0}, sicher_bis_{0}, fortschritt_ms_{0};
  int sicher_i_{-1}, ok_lang_i_{-1};
  bool lang_war_{false}, ok_lang_fertig_{false};
  JsonObjectConst seite_j_() const;
  // Popups mit eingebautem Inhalt (Tastatur, 2026-10-26): nach dem Zustand statt nach dem Wechsel - zuletzt
  // gesehener Wert je Seite. Leer nach Neustart/Aufwachen: dann zaehlt schon der erste Wert.
  std::map<std::string, std::string> pegel_;
  void pegel_pruefen_(const std::string &entity, const std::string &attr, const std::string &neu);
  void ausloeser_(const std::string &entity, const std::string &attr, const std::string &alt, const std::string &neu);
  void ausloesen_(Item &it, int richtung = 0);
  void fokus_setzen_(int idx);
  static void klick_cb_(lv_event_t *e);
  static void regler_cb_(lv_event_t *e);
  static void halt_cb_(lv_event_t *e);
  void halten_(int idx, bool an);     // Halte-Knopf druecken (an) / loslassen
  void modus_anzeigen_();
  void gestalten_(Item &it);
  void einst_zeigen_(Item &it);       // Wert/Regler aus dem Original-Widget uebernehmen
  void einst_schritt_(Item &it, int d);
  int auto_zeilen_(const Item &it, float zelle) const;
  // Zeilen, die ein Eintrag braucht (2026-10-07): h aus dem Konfigurator ist das Minimum, lange Namen/Werte
  // brechen um statt "..." - stapeln: Name und Wert untereinander
  int zeilen_(const Item &it, float zelle, bool *stapeln) const;
  const char *wert_text_(const Item &it) const;
  PVec<lv_point_precise_t> grafik_pkt_;   // Kopie der Punkte der Grafik ('g'), skaliert auf die Karte
  void grafik_zeigen_(Item &it);
  void hoehen_pruefen_();
  void hoehen_jetzt_();
  bool hoehen_neu_{false};
  float zelle_{36.0f};
  // Scrollleiste rechts (2026-10-07): bei jedem Scrollen (Wischen UND D-Pad) einblenden, danach ausblenden
  void leiste_einrichten_();
  void leiste_zeigen_();
  bool leiste_ok_{false};
  uint32_t leiste_bis_{0};
  void platz_(const Item &it, lv_obj_t *o) const;
  uint32_t einst_ab_{0};
  const char *icon_(const char *name);

  tasten_konfig::TastenKonfig *tk_{nullptr};
  lv_obj_t *root_{nullptr}, *titel_{nullptr}, *zurueck_lbl_{nullptr}, *zurueck_btn_{nullptr};
  static const int FOKUS_ZURUECK = -2;   // X/Zurueck-Knopf in der Kopfzeile
 public:
  uint32_t zeitlupe{1};   // alle Animationen (Menue, Kopfzeile, Leiste) x-mal langsamer - nur fuer Bildschirmfotos
 private:
  const lv_font_t *f_text_{nullptr}, *f_klein_{nullptr}, *f_icon_{nullptr}, *f_gross_{nullptr}, *f_xs_{nullptr}, *f_xl_{nullptr};
  uint32_t hl_{0x3B82F6};   // Hervorhebung (menu.hl): Rahmen der Markierung, Regler, Zurueck-Knopf

  bool an_{false};
  std::string start_;
  PVec<Seite> seiten_;
  std::vector<std::string> stapel_;     // Seiten-IDs, oben = aktuell
  PVec<Item> items_;                    // Items der angezeigten Seite
  PMap<PStr, int> fokus_mem_;
  int fokus_{-1};
  int einstieg_{-1};
  bool einstieg_merken_{false};
  bool popup_{false};
  bool tmodus_{false};
  int halt_{-1};          // gerade gehaltener Eintrag (Halte-Knopf), -1 = keiner
  std::string popup_id_;
  bool inhalt_knopf_min_{false};
  std::vector<std::string> gemerkt_;    // Stapel beim Schliessen, falls die Seite "merken" hat

  PMap<PStr, EntZustand> zustand_cache_;
  PVec<PStr> abos_;                     // "entity|attr", schon bei der API angemeldet
  // Zustaende der sichtbaren Kacheln per HTTP von HA (statt Dauer-Abos, die internes RAM kosten)
  static void abfrage_task_(void *arg);   // dauerhafter Arbeits-Task (Stapel im PSRAM)
  static void abfrage_einmal_(Menue *self);
  void *worker_{nullptr};
  void abfrage_starten_();
  void abfrage_auswerten_();
  volatile bool abf_laeuft_{false}, abf_fertig_{false};
  char *abf_buf_{nullptr};
  size_t abf_len_{0};
  int abf_status_{0};
  std::string abf_url_;
  uint32_t abf_ab_{0};
  StaticTask_t abf_tcb_;   // je Instanz (Startseite hat ihre eigene)
  // Widgets
  bool nur_anzeige_{false};
  std::string start_zeigt_;
  const lv_font_t *f_uhr_{nullptr}, *f_wetter_{nullptr};
  uint32_t wtick_ms_{0};
  PVec<PStr> weg_e_;   // Medien-Widgets mit "leer: weg", die gerade nicht gezeigt werden (Entitaet weiter abfragen)
  int8_t wslots_frei_{0};
  void widget_karte_(Item &it);
  void widget_teile_(Item &it);      // Inhalt (neu) aufbauen
  void widget_zustand_(Item &it);
  void widget_tick_();
  void widget_ausloesen_(Item &it, int richtung);
  void licht_bauen_(Item &it);
  void licht_zustand_(Item &it);
  void licht_senden_(Item &it, const char *aktion, const std::string &wert);
  std::string licht_ziele_(const Item &it, bool fuer_aufruf) const;
  static void licht_cb_(lv_event_t *e);
 public:
  // Medien-Widget auf dem "Jetzt laeuft"-Player (2026-10-29): Tasten wie die Wiedergabe-Karte (Bluetooth/Multiroom).
  // was: 0 Play/Pause, 1 vor, 2 zurueck, 3 lauter, 4 leiser. true = erledigt (sonst HA-Aufruf an die Entitaet)
  std::function<bool(const std::string &, int)> medien_taste;
  // Symbol (MDI-Name -> Zeichen der Menue-Symbolschrift) fuer andere, z. B. die Aktivitaeten-Leiste (2026-10-30)
  const char *symbol(const char *name) { return name != nullptr && *name ? icon_(name) : nullptr; }
  // Alle Schriftgroessen der Remote (2026-10-30): Groesse in px frei waehlbar ("px"), genommen wird die naechste vorhandene
  void set_schriften(std::vector<std::pair<int, const lv_font_t *>> l) { schriften_ = std::move(l); }
  const lv_font_t *schrift(int px) const {
    const lv_font_t *f = nullptr;
    int best = 1000;
    for (auto &s : schriften_) if (std::abs(s.first - px) < best) { best = std::abs(s.first - px); f = s.second; }
    return f;
  }
  std::vector<std::pair<int, const lv_font_t *>> schriften_;
 protected:
  void widget_bild_(Item &it, bool sofort);
  bool widget_weg_(JsonObjectConst j) const;   // Medien-Widget ohne Wiedergabe ausblenden?
  bool medien_laeuft_(const char *e) const;
  uint8_t widget_maske_(const Item &it) const;
  void widget_teil_(Item &it, lv_obj_t *p, uint8_t art, int breite, int hoehe, bool gross);
  static void wknopf_cb_(lv_event_t *e);
};

}  // namespace menue
}  // namespace esphome
