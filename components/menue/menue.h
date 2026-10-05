#pragma once
// Konfigurierbares Menue (2026-10-03). Daten: Konfiguration["menu"] aus tasten_konfig.
//
// "menu": {
//   "on": true, "start": "main",
//   "pages": [ { "id": "main", "title": "Menü", "remember": true, "flow": true,
//                "items": [ { "t": "special", "page": "rooms", "label": "Räume", "w": 6, "h": 1 }, ... ] } ]
// }
// Item-Typen (t): page (Unterseite, target), special (eingebaute Seite, page), action (steps wie
// bei den Tasten), toggle / light / cover / sensor (entity, optional steps statt Standard), text.
// Raster: 6 Spalten, 40-px-Zeilen. flow=true: Items fliessen in ihrer Reihenfolge (w/h), sonst x/y.

#include "esphome/core/component.h"
#include "esphome/components/json/json_util.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#include <esp_heap_caps.h>
#include <algorithm>
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

enum Typ : uint8_t { T_TEXT, T_PAGE, T_SPECIAL, T_ACTION, T_TOGGLE, T_LIGHT, T_COVER, T_SENSOR, T_KAMERA, T_EINST };

// Einstellung der Remote (t: "setting", src: Widget-ID einer eingebauten Einstellungsseite, 2026-10-04):
// typ 'b' = Knopf (Klick weiterreichen), 's' = Regler, 'i' = nur Anzeige. wert = Label mit dem aktuellen Wert.
struct EinstRef {
  char typ{0};
  lv_obj_t *obj{nullptr};
  lv_obj_t *wert{nullptr};
};

struct Item {
  Typ typ{T_TEXT};
  int8_t x{0}, y{0}, w{6}, h{1};
  JsonObjectConst j;          // Rohdaten (label, icon, entity, target, page, steps ...)
  lv_obj_t *obj{nullptr};     // Karte
  lv_obj_t *wert{nullptr};    // Zustandstext
  lv_obj_t *regler{nullptr};  // Licht-Regler
  bool name_wert{false};      // kleine Kachel: Zustand = Farbe des Namens
  EinstRef einst;             // T_EINST: ferngesteuertes Widget
  bool fokusierbar() const { return typ != T_TEXT && !(typ == T_EINST && einst.typ != 'b' && einst.typ != 's'); }
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
  PMap<PStr, PStr> attr;   // weitere Attribute (Popup-Ausloeser)
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
  void set_fonts(const lv_font_t *text, const lv_font_t *klein, const lv_font_t *icons) { f_text_ = text; f_klein_ = klein; f_icon_ = icons; }
  // Eingebaute Seite zeigen (open-remote-tasten.h). Rueckgabe: deren cur_page-Nummer, -1 = unbekannt.
  std::function<int(const std::string &name)> spezial;
  // Menue verlassen (Startseite).
  std::function<void()> nach_hause;
  // Popup-Ausloeser (page.popup.open/close): Seite als Popup zeigen bzw. schliessen.
  std::function<void(const std::string &seite, bool wecken)> popup_auf;
  std::function<void(const std::string &seite)> popup_zu;
  // Kamerabild-Kachel (t: "camera", src: "klingel_haus" | "klingel_wohnung"): Bildquelle aus der YAML.
  std::function<const void *(const std::string &quelle)> bild;
  void bild_neu();   // Bild wurde neu geladen -> sichtbare Kamera-Kacheln auffrischen
  // Einstellungen (t: "setting"): Widget zur ID finden und die eingebauten Seiten auffrischen
  std::function<EinstRef(const std::string &key)> einstellung;
  std::function<void()> einst_auffrischen;

  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  bool aktiv() const { return an_ && !seiten_.empty(); }
  bool hat_seite(const std::string &id) const { return seite_index_(id) >= 0; }
  // Seite als Popup zeigen (HA-Dienst menue_popup, z. B. Kamera-Popup). Auch bei ausgeschaltetem Menue.
  void popup(const std::string &id);
  bool ist_popup() const { return popup_; }
  const std::string &popup_seite() const { return popup_id_; }

  // Menue oeffnen: merken=true -> zuletzt gezeigte Seite, wenn sie (bzw. ein Vorgaenger) "merken" hat.
  void oeffnen(bool merken);
  void anzeigen();               // aktuelle Seite neu aufbauen (Rueckkehr aus einer Spezialseite)
  bool zurueck();                // false = war schon oben
  bool oben() const { return stapel_.size() <= 1; }
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
  void einstieg_weg() { einstieg_ = -1; }
  bool einstieg_merken() const { return einstieg_merken_; }

 protected:
  void laden_();
  int seite_index_(const std::string &id) const;
  void bauen_();
  void leeren_();
  void karte_(Item &it);
  void zustand_(Item &it);
  void aktualisieren_(const std::string &entity);
  void abonnieren_(const std::string &entity, const char *attr);
  void ausloeser_(const std::string &entity, const std::string &attr, const std::string &alt, const std::string &neu);
  void ausloesen_(Item &it, int richtung = 0);
  void fokus_setzen_(int idx);
  static void klick_cb_(lv_event_t *e);
  static void regler_cb_(lv_event_t *e);
  static void halt_cb_(lv_event_t *e);
  void halten_(int idx, bool an);     // Halte-Knopf druecken (an) / loslassen
  void modus_anzeigen_();
  void einst_zeigen_(Item &it);       // Wert/Regler aus dem Original-Widget uebernehmen
  void einst_schritt_(Item &it, int d);
  int auto_zeilen_(const Item &it, float zelle) const;
  void hoehen_pruefen_();
  void hoehen_jetzt_();
  bool hoehen_neu_{false};
  uint32_t einst_ab_{0};
  const char *icon_(const char *name);

  tasten_konfig::TastenKonfig *tk_{nullptr};
  lv_obj_t *root_{nullptr}, *titel_{nullptr}, *zurueck_lbl_{nullptr}, *zurueck_btn_{nullptr};
  static const int FOKUS_ZURUECK = -2;   // X/Zurueck-Knopf in der Kopfzeile
  const lv_font_t *f_text_{nullptr}, *f_klein_{nullptr}, *f_icon_{nullptr};

  bool an_{false};
  uint32_t gen_{0};
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
  std::vector<std::string> gemerkt_;    // Stapel beim Schliessen, falls die Seite "merken" hat
  std::string popup_id_merk_;

  PMap<PStr, EntZustand> zustand_cache_;
  PVec<PStr> abos_;                     // "entity|attr", schon bei der API angemeldet
  bool abo_neu_{false};
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
};

}  // namespace menue
}  // namespace esphome
