#pragma once

// Datengetriebenes Menue fuer die Open Remote. Baut die LVGL-Oberflaeche zur
// Laufzeit aus /sd/menu.json - der Original-OMOTE-Konfigurator kann Button-/
// Aktivitaets-/Theme-Teile davon bearbeiten, Licht/Rollo-Items kommen per
// Hand-JSON oder Konfigurator-Fork dazu (Design-Entscheidung: Weg 3).
//
// Widgets werden dynamisch als Kinder eines Root-Containers (lv_obj_t*, per
// YAML-id uebergeben) erzeugt und bei jedem Seitenwechsel neu aufgebaut.
//
// menu.json:
// {
//   "schemaVersion": 1,
//   "theme": {"bg":"#0b0f14","card":"#1a1f27","accent":"#22c55e","text":"#e5e7eb"},
//   "start": "home",
//   "pages": [
//     {"id":"home","name":"Start","cols":3,"items":[
//        {"type":"nav","slot":0,"label":"Raeume","target":"wz"},
//        {"type":"nav","slot":1,"label":"Geraete","target":"dev"}
//     ]},
//     {"id":"wz","name":"Wohnzimmer","cols":2,"items":[
//        {"type":"light","slot":0,"span":2,"label":"Licht","entity":"light.wohnzimmer"},
//        {"type":"cover","slot":2,"span":2,"label":"Rollo","entity":"cover.wohnzimmer_rollo"},
//        {"type":"button","slot":4,"label":"TV an","device":"Fetch TV","command":"Power"},
//        {"type":"button","slot":5,"label":"Kino","activity":"Fetch TV"},
//        {"type":"toggle","slot":6,"label":"Sternenhimmel","entity":"light.sternenhimmel"},
//        {"type":"button","slot":7,"label":"< Start","press":"home"}
//     ]}
//   ]
// }
//
// item.type: nav | button | light | cover | toggle | label | spacer
// item.press (button): "back" | "home"  (sonst device+command ODER activity ODER
//                       ha_service+ha_entity)
// item.slot: Rasterposition (row-major, cols pro Seite), item.span: Zellen breit

#ifdef USE_ESP32

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/defines.h"
#include "esphome/components/lvgl/lvgl_esphome.h"
#ifdef USE_LVGL_FONT
#include "esphome/components/font/font.h"
#endif
#include <string>
#include <vector>
#include <map>

namespace esphome {

namespace sd_card { class SdCard; }
namespace runtime_config { class RuntimeConfig; }
namespace font { class Font; }

namespace menu_ui {

class HaServiceTrigger : public Trigger<std::string, std::string> {};
class HaNumberTrigger : public Trigger<std::string, std::string, int> {};

enum ItemType : uint8_t {
  IT_SPACER, IT_NAV, IT_BUTTON, IT_LIGHT, IT_COVER, IT_TOGGLE, IT_LABEL
};

struct MenuItem {
  ItemType type{IT_SPACER};
  uint8_t slot{0};
  uint8_t span{1};
  std::string label;
  std::string target;      // nav
  std::string press;       // "back"/"home"
  std::string device, command, activity;   // button
  std::string macro;       // button -> Makro
  std::string ha_service;  // button -> HA
  std::string entity;      // light/cover/toggle/label/button-ha
  std::string icon;        // mdi-Name (Katalog in mdi_icons.h), optional
  bool by_id{false};       // device/command sind IDs, keine Namen
  // Laufzeit
  lv_obj_t *widget{nullptr};
  lv_obj_t *sub{nullptr};   // z.B. Slider im Licht-Card, Label im Button
  lv_obj_t *sub2{nullptr};
};

struct MenuTheme {
  bool set{false};
  lv_color_t bg, card, accent, text;
  std::string wallpaper_path;   // /themes/.../xxx.rgb565 (240x320), optional
};

struct MenuPage {
  std::string id;
  std::string name;
  uint8_t cols{3};
  MenuTheme theme;
  std::vector<MenuItem> items;
};

class MenuUi : public Component {
 public:
  void set_sd(sd_card::SdCard *s) { sd_ = s; }
  void set_runtime_config(runtime_config::RuntimeConfig *r) { rc_ = r; }
  void set_root(lv_obj_t *root) { root_ = root; }
  void set_title_label(lv_obj_t *l) { title_ = l; }
  void set_icon_font(font::Font *f) { icon_font_ = f; }
  void set_path(const std::string &p) { path_ = p; }
  void set_runtime_path(const std::string &p) { runtime_path_ = p; }
  void set_wallpaper_swap(bool b) { wallpaper_swap_ = b; }
  void set_load_on_boot(bool b) { load_on_boot_ = b; }

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE - 5.0f; }

  bool load();
  bool loaded() const { return loaded_; }
  size_t page_count() const { return pages_.size(); }
  std::string status() const { return status_; }

  void show_page(const std::string &id);
  void show_page_index(int idx);
  void go_back();
  void go_home();
  void reopen();   // Menue neu von der Startseite oeffnen (nav_stack leeren)
  bool at_root() const { return this->nav_stack_.size() <= 1; }

  // D-Pad-Navigation (aus dem bestehenden Tasten-Handler aufrufen)
  void focus_move(int dx, int dy);
  void focus_activate();   // "OK"

  // HA-Aktionen -> YAML (robuster als C++-Serviceaufruf)
  void register_ha_service_trigger(HaServiceTrigger *t) { ha_service_trigs_.push_back(t); }
  void register_ha_number_trigger(HaNumberTrigger *t) { ha_number_trigs_.push_back(t); }
  void fire_ha_service_(const std::string &service, const std::string &entity) {
    for (auto *t : ha_service_trigs_) t->trigger(service, entity);
  }
  void fire_ha_number_(const std::string &entity, const std::string &service, int v) {
    for (auto *t : ha_number_trigs_) t->trigger(entity, service, v);
  }

 protected:
  // Menue aus dem OpenRemote-Studio-Export bauen: runtime.json (devicePages,
  // pages, themes, macros) hat Vorrang; fehlt sie, direkt aus runtime_config.
  bool build_from_runtime_json_();
  bool build_auto_menu_();
  void apply_page_theme_(const MenuTheme &t);
  lv_obj_t *load_wallpaper_(const std::string &path);
  void build_current_();
  void clear_widgets_();
  void place_(MenuItem &it, int col_w, int row_h, int cols, int pad);
  void make_button_(MenuItem &it);
  void make_navlike_(MenuItem &it, bool nav);
  void make_light_(MenuItem &it);
  void make_cover_(MenuItem &it);
  void make_toggle_(MenuItem &it);
  void make_label_(MenuItem &it);
  void bind_state_(MenuItem &it);
  void item_action_(MenuItem &it);
  void apply_focus_style_();
  int find_page_(const std::string &id) const;
  static void event_cb_(lv_event_t *e);
  // Icon-Label (mdi) links neben/ueber dem Text erzeugen, wenn it.icon gesetzt
  // ist und in mdi_icons.h steht. Gibt nullptr zurueck, sonst.
  lv_obj_t *make_icon_(lv_obj_t *parent, const std::string &name);

  sd_card::SdCard *sd_{nullptr};
  runtime_config::RuntimeConfig *rc_{nullptr};
  lv_obj_t *root_{nullptr};
  lv_obj_t *title_{nullptr};
  font::Font *icon_font_{nullptr};
  std::string path_{"/menu.json"};
  std::string runtime_path_{"/runtime.json"};
  std::string start_id_;
  bool load_on_boot_{true};
  bool wallpaper_swap_{false};
  lv_obj_t *wallpaper_img_{nullptr};   // aktuell gesetztes Hintergrundbild
  void *wallpaper_buf_{nullptr};       // PSRAM-Puffer des zuletzt geladenen .rgb565
  std::string wallpaper_loaded_path_;
  lv_image_dsc_t wallpaper_dsc_{};     // zeigt in wallpaper_buf_
  uint32_t cover_adjust_until_{0};     // bis dahin HA-current_position-Updates ignorieren

  bool loaded_{false};
  // Hochgezaehlt bei jedem load(). HA-State-Callbacks koennen nicht abgemeldet
  // werden - sie halten einen MenuItem*, der nach einem Reload dangelt. Die
  // Callbacks pruefen die Generation und werden nach einem Reload zu No-ops.
  uint32_t load_gen_{0};
  std::string status_;
  std::vector<MenuPage> pages_;
  int cur_page_{-1};
  std::vector<std::string> nav_stack_;
  int focus_idx_{-1};   // Index in pages_[cur_page_].items der fokussierten Zelle

  // Theme
  lv_color_t th_bg_{lv_color_hex(0x0b0f14)};
  lv_color_t th_card_{lv_color_hex(0x1a1f27)};
  lv_color_t th_accent_{lv_color_hex(0x22c55e)};
  lv_color_t th_text_{lv_color_hex(0xe5e7eb)};

  std::vector<HaServiceTrigger *> ha_service_trigs_;
  std::vector<HaNumberTrigger *> ha_number_trigs_;
};

}  // namespace menu_ui
}  // namespace esphome

#endif  // USE_ESP32
