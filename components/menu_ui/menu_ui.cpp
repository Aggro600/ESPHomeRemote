#include "menu_ui.h"

#ifdef USE_ESP32

#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/string_ref.h"
#include "mdi_icons.h"
#include "studio_icons.h"
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <utility>
#include <esp_heap_caps.h>
#include "esphome/components/sd_card/sd_card.h"
#include "esphome/components/runtime_config/runtime_config.h"

#ifdef USE_API
#include "esphome/components/api/api_server.h"
#endif

#include <ArduinoJson.h>

namespace esphome {
namespace menu_ui {

static const char *const TAG = "menu_ui";

static lv_color_t hex_(const char *s, lv_color_t def) {
  if (s == nullptr || s[0] != '#')
    return def;
  long v = strtol(s + 1, nullptr, 16);
  return lv_color_hex((uint32_t) v);
}

// mdi-Name -> Unicode-Codepoint (0 = unbekannt). MDI_ICONS ist nach Name sortiert.
static uint32_t mdi_cp_(const std::string &name) {
  if (name.empty())
    return 0;
  int lo = 0, hi = MDI_ICONS_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = std::strcmp(name.c_str(), MDI_ICONS[mid].name);
    if (c == 0)
      return MDI_ICONS[mid].cp;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return 0;
}

// Icon-Name -> Codepoint. Nimmt mdi-Namen direkt, sonst OpenRemote-Studio-Namen
// ("Volume Up" / "volume_up" / "streaming_app") ueber die Uebersetzungstabelle.
static uint32_t icon_cp_(const std::string &raw) {
  if (raw.empty())
    return 0;
  uint32_t cp = mdi_cp_(raw);
  if (cp)
    return cp;
  std::string norm;
  for (char c : raw)
    norm += (c == ' ' || c == '-') ? '_' : (char) tolower((unsigned char) c);
  cp = mdi_cp_(norm);
  if (cp)
    return cp;
  for (int i = 0; i < STUDIO_ICONS_COUNT; i++)
    if (norm == STUDIO_ICONS[i].name)
      return mdi_cp_(STUDIO_ICONS[i].mdi);
  return 0;
}

static std::string utf8_(uint32_t cp) {
  std::string s;
  if (cp < 0x80) {
    s += (char) cp;
  } else if (cp < 0x800) {
    s += (char) (0xC0 | (cp >> 6));
    s += (char) (0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    s += (char) (0xE0 | (cp >> 12));
    s += (char) (0x80 | ((cp >> 6) & 0x3F));
    s += (char) (0x80 | (cp & 0x3F));
  } else {
    s += (char) (0xF0 | (cp >> 18));
    s += (char) (0x80 | ((cp >> 12) & 0x3F));
    s += (char) (0x80 | ((cp >> 6) & 0x3F));
    s += (char) (0x80 | (cp & 0x3F));
  }
  return s;
}

lv_obj_t *MenuUi::make_icon_(lv_obj_t *parent, const std::string &name) {
#ifdef USE_LVGL_FONT
  if (this->icon_font_ == nullptr)
    return nullptr;
  uint32_t cp = icon_cp_(name);
  if (cp == 0)
    return nullptr;
  lv_obj_t *ic = lv_label_create(parent);
  lv_obj_set_style_text_font(ic, this->icon_font_->get_lv_font(), 0);
  lv_obj_set_style_text_color(ic, this->th_text_, 0);
  std::string g = utf8_(cp);
  lv_label_set_text(ic, g.c_str());
  return ic;
#else
  (void) parent;
  (void) name;
  return nullptr;
#endif
}

void MenuUi::setup() {
  // Wenn runtime_config nachlaedt (z.B. Studio-Konfig frisch auf der SD),
  // das Auto-Menue neu aufbauen.
  if (this->rc_ != nullptr) {
    this->rc_->add_on_load_callback([this]() {
      if (this->load_on_boot_)
        this->load();
    });
  }
  if (this->load_on_boot_) {
    this->set_timeout(3000, [this]() {
      if (this->sd_ != nullptr && !this->sd_->is_mounted())
        this->sd_->mount();
      this->load();
    });
  }
}

void MenuUi::dump_config() {
  ESP_LOGCONFIG(TAG, "Menu-UI (datengetrieben):");
  ESP_LOGCONFIG(TAG, "  Pfad: %s   Root: %s   %d Seiten", this->path_.c_str(),
                this->root_ ? "ok" : "FEHLT", (int) this->pages_.size());
}

int MenuUi::find_page_(const std::string &id) const {
  for (size_t i = 0; i < this->pages_.size(); i++)
    if (this->pages_[i].id == id)
      return (int) i;
  return -1;
}

bool MenuUi::load() {
  this->loaded_ = false;
  this->load_gen_++;   // alte HA-State-Callbacks entwerten (siehe Header)
  this->pages_.clear();
  this->cur_page_ = -1;
  this->nav_stack_.clear();

  if (this->root_ == nullptr) {
    this->status_ = "kein Root-Widget";
    ESP_LOGE(TAG, "%s", this->status_.c_str());
    return false;
  }
  if (this->sd_ == nullptr || !this->sd_->is_mounted()) {
    this->status_ = "SD nicht gemountet";
    ESP_LOGW(TAG, "%s", this->status_.c_str());
    return false;
  }
  std::string raw;
  if (!this->sd_->read_text(this->path_, raw, 256 * 1024) || raw.empty()) {
    // Keine menu.json -> aus dem OpenRemote-Studio-Export bauen:
    // 1. runtime.json (devicePages/pages/themes) - exaktes Studio-Layout
    // 2. sonst direkt aus runtime_config (Geraete/Befehle/Aktivitaeten)
    bool ok = this->build_from_runtime_json_();
    if (!ok)
      ok = this->build_auto_menu_();
    if (ok) {
      this->loaded_ = true;
      this->status_ = "Menue (Studio-Export): " + std::to_string(this->pages_.size()) + " Seiten";
      ESP_LOGI(TAG, "%s", this->status_.c_str());
      this->start_id_ = this->pages_.empty() ? "" : this->pages_[0].id;
      this->nav_stack_.clear();
      this->show_page(this->start_id_);
      return true;
    }
    this->status_ = std::string("kann ") + this->path_ + " nicht lesen";
    ESP_LOGW(TAG, "%s", this->status_.c_str());
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    this->status_ = std::string("JSON-Fehler: ") + err.c_str();
    ESP_LOGE(TAG, "%s", this->status_.c_str());
    return false;
  }

  JsonObject th = doc["theme"];
  if (th) {
    this->th_bg_ = hex_(th["bg"] | "", this->th_bg_);
    this->th_card_ = hex_(th["card"] | "", this->th_card_);
    this->th_accent_ = hex_(th["accent"] | "", this->th_accent_);
    this->th_text_ = hex_(th["text"] | "", this->th_text_);
  }

  JsonArray pgs = doc["pages"].as<JsonArray>();
  for (JsonObject pj : pgs) {
    MenuPage p;
    p.id = pj["id"] | "";
    p.name = pj["name"] | p.id;
    p.cols = pj["cols"] | 3;
    if (p.cols < 1)
      p.cols = 1;
    if (p.cols > 6)
      p.cols = 6;
    JsonArray its = pj["items"].as<JsonArray>();
    for (JsonObject ij : its) {
      MenuItem it;
      std::string t = ij["type"] | "spacer";
      if (t == "nav") it.type = IT_NAV;
      else if (t == "button") it.type = IT_BUTTON;
      else if (t == "light") it.type = IT_LIGHT;
      else if (t == "cover") it.type = IT_COVER;
      else if (t == "toggle") it.type = IT_TOGGLE;
      else if (t == "label") it.type = IT_LABEL;
      else it.type = IT_SPACER;
      it.slot = ij["slot"] | 0;
      it.span = ij["span"] | 1;
      if (it.span < 1) it.span = 1;
      it.label = ij["label"] | "";
      it.target = ij["target"] | "";
      it.press = ij["press"] | "";
      it.device = ij["device"] | (ij["deviceId"] | "");
      it.command = ij["command"] | (ij["commandId"] | "");
      it.activity = ij["activity"] | "";
      it.ha_service = ij["ha_service"] | (ij["service"] | "");
      it.entity = ij["entity"] | "";
      it.icon = ij["icon"] | (ij["iconName"] | "");
      p.items.push_back(std::move(it));
    }
    this->pages_.push_back(std::move(p));
  }

  this->loaded_ = true;
  this->status_ = "geladen: " + std::to_string(this->pages_.size()) + " Seiten";
  ESP_LOGI(TAG, "%s", this->status_.c_str());

  std::string start = doc["start"] | "";
  if (start.empty() && !this->pages_.empty())
    start = this->pages_[0].id;
  this->start_id_ = start;
  this->nav_stack_.clear();
  this->show_page(start);
  return true;
}

// Menue direkt aus runtime_config: Startseite -> Geraete / Aktivitaeten,
// je Geraet eine Seite mit allen Befehlen als Buttons. Deckt den Fall ab,
// dass jemand nur die OpenRemote-Studio-Konfig (runtime.json + /devices/*.ir)
// auf die SD legt und gar keine menu.json anlegt.
bool MenuUi::build_auto_menu_() {
  if (this->rc_ == nullptr || !this->rc_->loaded())
    return false;
  auto devs = this->rc_->device_names();
  auto acts = this->rc_->activity_names();
  if (devs.empty() && acts.empty())
    return false;

  auto slug = [](const std::string &s) {
    std::string o;
    for (char c : s) {
      if (c == ' ' || c == '/' || c == ':')
        o += '_';
      else
        o += c;
    }
    return o;
  };

  // Startseite
  {
    MenuPage home;
    home.id = "home";
    home.name = "Menue";
    home.cols = 1;
    uint8_t slot = 0;
    if (!devs.empty()) {
      MenuItem it;
      it.type = IT_NAV;
      it.label = "Geraete";
      it.target = "geraete";
      it.slot = slot++;
      it.icon = "remote-tv";
      home.items.push_back(std::move(it));
    }
    if (!acts.empty()) {
      MenuItem it;
      it.type = IT_NAV;
      it.label = "Aktivitaeten";
      it.target = "aktivitaeten";
      it.slot = slot++;
      it.icon = "movie-open";
      home.items.push_back(std::move(it));
    }
    this->pages_.push_back(std::move(home));
  }

  // Geraeteliste
  if (!devs.empty()) {
    MenuPage list;
    list.id = "geraete";
    list.name = "Geraete";
    list.cols = 1;
    uint8_t slot = 0;
    for (auto &d : devs) {
      MenuItem it;
      it.type = IT_NAV;
      it.label = d;
      it.target = "dev_" + slug(d);
      it.slot = slot++;
      list.items.push_back(std::move(it));
    }
    {
      MenuItem b;
      b.type = IT_BUTTON;
      b.label = "< Zurueck";
      b.press = "home";
      b.slot = slot++;
      list.items.push_back(std::move(b));
    }
    this->pages_.push_back(std::move(list));

    // je Geraet eine Seite
    for (auto &d : devs) {
      MenuPage dp;
      dp.id = "dev_" + slug(d);
      dp.name = d;
      dp.cols = 3;
      uint8_t slot = 0;
      for (auto &cmd : this->rc_->command_names(d)) {
        MenuItem it;
        it.type = IT_BUTTON;
        it.label = cmd;
        it.device = d;
        it.command = cmd;
        it.slot = slot++;
        dp.items.push_back(std::move(it));
      }
      MenuItem b;
      b.type = IT_BUTTON;
      b.label = "< Zurueck";
      b.press = "back";
      b.slot = slot++;
      dp.items.push_back(std::move(b));
      this->pages_.push_back(std::move(dp));
    }
  }

  // Aktivitaeten
  if (!acts.empty()) {
    MenuPage ap;
    ap.id = "aktivitaeten";
    ap.name = "Aktivitaeten";
    ap.cols = 1;
    uint8_t slot = 0;
    for (auto &a : acts) {
      MenuItem it;
      it.type = IT_BUTTON;
      it.label = a;
      it.activity = a;
      it.slot = slot++;
      ap.items.push_back(std::move(it));
    }
    MenuItem b;
    b.type = IT_BUTTON;
    b.label = "< Zurueck";
    b.press = "home";
    b.slot = slot++;
    ap.items.push_back(std::move(b));
    this->pages_.push_back(std::move(ap));
  }

  ESP_LOGI(TAG, "Auto-Menue: %d Geraete, %d Aktivitaeten -> %d Seiten", (int) devs.size(),
           (int) acts.size(), (int) this->pages_.size());
  return true;
}

// ---- OpenRemote-Studio-Export: runtime.json -> Menue ---------------------
// devicePages / pages / themes / macros werden gerendert. Button-IDs (deviceId
// + commandId) statt Namen, damit auch gleichnamige Befehle eindeutig bleiben.

static uint8_t clampu8(long v, long lo, long hi) { return (uint8_t) (v < lo ? lo : (v > hi ? hi : v)); }

bool MenuUi::build_from_runtime_json_() {
  if (this->sd_ == nullptr || !this->sd_->is_mounted())
    return false;
  std::string raw;
  if (!this->sd_->read_text(this->runtime_path_, raw, 512 * 1024) || raw.empty())
    return false;

  JsonDocument filter;
  for (const char *k : {"devicePages", "pages", "themes", "macros", "activities", "devices"})
    filter[k] = true;
  JsonDocument doc;
  if (deserializeJson(doc, raw, DeserializationOption::Filter(filter))) {
    ESP_LOGW(TAG, "runtime.json (Menue) unlesbar");
    return false;
  }

  // Themes: id -> Farben + Wallpaper
  std::map<std::string, MenuTheme> themes;
  JsonArray themes_arr = doc["themes"].as<JsonArray>();
  for (JsonObject tj : themes_arr) {
    std::string id = tj["id"] | "";
    if (id.empty())
      continue;
    MenuTheme mt;
    mt.set = true;
    mt.accent = hex_((tj["colour1"] | ""), lv_color_hex(0x22c55e));
    mt.bg = hex_((tj["colour2"] | ""), lv_color_hex(0x0b0f14));
    mt.card = hex_((tj["colour3"] | ""), lv_color_hex(0x1a1f27));
    mt.text = lv_color_hex(0xe5e7eb);
    mt.wallpaper_path = tj["runtimePath"] | "";
    themes[id] = mt;
  }
  auto theme_for = [&](const std::string &name) -> MenuTheme {
    auto it = themes.find(name);
    return it != themes.end() ? it->second : MenuTheme{};
  };

  // Aktivitaeten-Namen (fuer Fallback, wenn keine "pages"-Aktivitaetsseite da ist)
  std::vector<std::pair<std::string, std::string>> acts;  // {name, id}
  JsonArray acts_arr = doc["activities"].as<JsonArray>();
  for (JsonObject aj : acts_arr)
    acts.emplace_back(aj["name"] | "", aj["id"] | "");

  auto slug = [](const std::string &s) {
    std::string o;
    for (char c : s)
      o += (c == ' ' || c == '/' || c == ':' || c == '#') ? '_' : c;
    return o;
  };

  auto item_from_json = [&](JsonObject ij, MenuItem &mi) {
    std::string t = ij["type"] | "button";
    mi.slot = clampu8(ij["slot"] | 0, 0, 200);
    mi.label = ij["name"] | "";
    mi.icon = ij["iconName"] | "";
    if (t == "activity") {
      mi.type = IT_BUTTON;
      mi.activity = ij["refId"] | (ij["name"] | "");
      mi.by_id = ij["refId"].is<const char *>();
    } else if (t == "macro") {
      mi.type = IT_BUTTON;
      mi.macro = ij["refId"] | (ij["name"] | "");
    } else if (t == "page" || t == "nav") {
      mi.type = IT_NAV;
      mi.target = std::string("p_") + slug(ij["refId"] | (ij["name"] | ""));
    } else {  // button - Namen statt IDs, damit .ir-Geraete + runtime.json-Geraete
              // (unterschiedliche ID-Schemata) beide getroffen werden.
      mi.type = IT_BUTTON;
      mi.device = ij["deviceName"] | "";   // vom Aufrufer gesetzt, s.u.
      mi.command = ij["name"] | "";
    }
  };

  size_t before = this->pages_.size();

  // Startseite
  MenuPage home;
  home.id = "home";
  home.name = "Menue";
  home.cols = 1;
  uint8_t hslot = 0;

  // devicePages
  JsonArray dpages_arr = doc["devicePages"].as<JsonArray>();
  for (JsonObject dp : dpages_arr) {
    std::string dname = dp["name"] | "";
    JsonArray items = dp["items"].as<JsonArray>();
    if (dname.empty() || items.size() == 0)
      continue;
    MenuPage page;
    page.id = "dev_" + slug(dname);
    page.name = dname;
    page.cols = 4;  // OpenRemote-Geraeteseiten sind 4 Spalten breit
    page.theme = theme_for(dp["pageTheme"] | "");
    for (JsonObject ij : items) {
      MenuItem mi;
      item_from_json(ij, mi);
      if (mi.type == IT_BUTTON && mi.macro.empty() && mi.activity.empty())
        mi.device = dname;   // Geraeteseite -> Geraetename fuer alle Buttons
      page.items.push_back(std::move(mi));
    }
    // Zurueck unten anhaengen
    MenuItem back;
    back.type = IT_BUTTON;
    back.label = "<";
    back.press = "back";
    uint8_t maxslot = 0;
    for (auto &x : page.items)
      if (x.slot > maxslot)
        maxslot = x.slot;
    back.slot = maxslot + 1;
    page.items.push_back(std::move(back));
    this->pages_.push_back(std::move(page));

    MenuItem nav;
    nav.type = IT_NAV;
    nav.label = dname;
    nav.target = "dev_" + slug(dname);
    nav.slot = hslot++;
    home.items.push_back(std::move(nav));
  }

  // pages (Aktivitaeten, eigene Seiten; "settings" ueberspringen wir)
  JsonArray pages_arr = doc["pages"].as<JsonArray>();
  for (JsonObject pj : pages_arr) {
    std::string ptype = pj["pageType"] | "";
    std::string pname = pj["name"] | "";
    if (ptype == "settings")
      continue;
    MenuPage page;
    page.id = "p_" + slug(pname);
    page.name = pname.empty() ? "Seite" : pname;
    page.cols = 3;
    page.theme = theme_for(pj["theme"] | "");
    JsonArray pitems = pj["items"].as<JsonArray>();
    for (JsonObject ij : pitems) {
      MenuItem mi;
      item_from_json(ij, mi);
      page.items.push_back(std::move(mi));
    }
    MenuItem back;
    back.type = IT_BUTTON;
    back.label = "<";
    back.press = "home";
    uint8_t maxslot = 0;
    for (auto &x : page.items)
      if (x.slot > maxslot)
        maxslot = x.slot;
    back.slot = maxslot + 1;
    page.items.push_back(std::move(back));
    this->pages_.push_back(std::move(page));

    MenuItem nav;
    nav.type = IT_NAV;
    nav.label = page.name;
    nav.target = page.id;
    nav.slot = hslot++;
    home.items.push_back(std::move(nav));
  }

  // Keine Aktivitaetsseite in "pages"? Dann eine bauen.
  bool has_act_page = false;
  for (auto &p : this->pages_)
    if (p.id == "p_Activities" || p.name == "Aktivitaeten" || p.name == "Activities")
      has_act_page = true;
  if (!has_act_page && !acts.empty()) {
    MenuPage ap;
    ap.id = "p_Activities";
    ap.name = "Aktivitaeten";
    ap.cols = 1;
    uint8_t s = 0;
    for (auto &a : acts) {
      MenuItem it;
      it.type = IT_BUTTON;
      it.label = a.first;
      it.activity = a.second.empty() ? a.first : a.second;
      it.by_id = !a.second.empty();
      it.slot = s++;
      ap.items.push_back(std::move(it));
    }
    MenuItem back;
    back.type = IT_BUTTON;
    back.label = "<";
    back.press = "home";
    back.slot = s++;
    ap.items.push_back(std::move(back));
    this->pages_.push_back(std::move(ap));

    MenuItem nav;
    nav.type = IT_NAV;
    nav.label = "Aktivitaeten";
    nav.target = "p_Activities";
    nav.slot = hslot++;
    home.items.push_back(std::move(nav));
  }

  if (this->pages_.size() == before)
    return false;
  // Startseite an den Anfang
  this->pages_.insert(this->pages_.begin(), std::move(home));
  ESP_LOGI(TAG, "Menue aus runtime.json: %d Seiten (%d Themes)",
           (int) this->pages_.size(), (int) themes.size());
  return true;
}

// ---- Theme --------------------------------------------------------------
// Farben aus dem Studio-Theme (colour1/2/3). Ist im Theme ein gerendertes
// .rgb565-Wallpaper (240x320) hinterlegt, wird es als LVGL-Hintergrundbild
// geladen (setzt LV_USE_IMAGE=1 als Build-Flag voraus, siehe YAML).
void MenuUi::apply_page_theme_(const MenuTheme &t) {
  if (!t.set)
    return;
  this->th_bg_ = t.bg;
  this->th_card_ = t.card;
  this->th_accent_ = t.accent;
  this->th_text_ = t.text;
  lv_obj_t *scr = lv_obj_get_screen(this->root_);
  if (scr != nullptr)
    lv_obj_set_style_bg_color(scr, this->th_bg_, 0);

  lv_obj_t *wp = this->load_wallpaper_(t.wallpaper_path);
  // root_ durchsichtig, damit das Bild dahinter sichtbar ist - sonst deckt
  // der Container-Hintergrund alles ab. Ohne Wallpaper wieder deckend.
  if (this->root_ != nullptr)
    lv_obj_set_style_bg_opa(this->root_, wp != nullptr ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

// Laedt <path> (roh 240x320 RGB565, 153600 Byte) von der SD in einen
// PSRAM-Puffer und haengt es als Hintergrundbild hinter alle Menue-Widgets.
// Gibt das Bild-Objekt zurueck oder nullptr (kein Pfad / Fehler).
lv_obj_t *MenuUi::load_wallpaper_(const std::string &path) {
  static constexpr int WP_W = 240, WP_H = 320;
  static constexpr size_t WP_BYTES = (size_t) WP_W * WP_H * 2;

  if (path.empty()) {
    if (this->wallpaper_img_ != nullptr) {
      lv_obj_add_flag(this->wallpaper_img_, LV_OBJ_FLAG_HIDDEN);
    }
    return nullptr;
  }
  // schon geladen?
  if (path == this->wallpaper_loaded_path_ && this->wallpaper_img_ != nullptr) {
    lv_obj_clear_flag(this->wallpaper_img_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_background(this->wallpaper_img_);
    return this->wallpaper_img_;
  }
  if (this->sd_ == nullptr || !this->sd_->is_mounted())
    return nullptr;

  std::vector<uint8_t> raw;
  if (!this->sd_->read_binary(path, raw, WP_BYTES) || raw.size() < WP_BYTES) {
    ESP_LOGW(TAG, "Wallpaper '%s': %d/%d Byte gelesen", path.c_str(), (int) raw.size(),
             (int) WP_BYTES);
    return nullptr;
  }
  // Puffer in PSRAM (SRAM reicht fuer 150 KB oft nicht neben LVGL/BT).
  void *buf = heap_caps_malloc(WP_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (buf == nullptr)
    buf = heap_caps_malloc(WP_BYTES, MALLOC_CAP_8BIT);
  if (buf == nullptr) {
    ESP_LOGW(TAG, "Wallpaper: kein Speicher (%d Byte)", (int) WP_BYTES);
    return nullptr;
  }
  memcpy(buf, raw.data(), WP_BYTES);
  if (this->wallpaper_swap_) {
    uint8_t *b = (uint8_t *) buf;
    for (size_t i = 0; i + 1 < WP_BYTES; i += 2)
      std::swap(b[i], b[i + 1]);
  }

  if (this->wallpaper_buf_ != nullptr)
    heap_caps_free(this->wallpaper_buf_);
  this->wallpaper_buf_ = buf;

  memset(&this->wallpaper_dsc_, 0, sizeof(this->wallpaper_dsc_));
  this->wallpaper_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
  this->wallpaper_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
  this->wallpaper_dsc_.header.w = WP_W;
  this->wallpaper_dsc_.header.h = WP_H;
  this->wallpaper_dsc_.header.stride = WP_W * 2;
  this->wallpaper_dsc_.data_size = WP_BYTES;
  this->wallpaper_dsc_.data = (const uint8_t *) buf;

  lv_obj_t *scr = lv_obj_get_screen(this->root_);
  if (scr == nullptr)
    return nullptr;
  if (this->wallpaper_img_ == nullptr) {
    this->wallpaper_img_ = lv_image_create(scr);
    lv_obj_add_flag(this->wallpaper_img_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(this->wallpaper_img_, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_image_set_src(this->wallpaper_img_, &this->wallpaper_dsc_);
  lv_obj_set_pos(this->wallpaper_img_, 0, 0);
  lv_obj_clear_flag(this->wallpaper_img_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_background(this->wallpaper_img_);
  this->wallpaper_loaded_path_ = path;
  ESP_LOGI(TAG, "Wallpaper geladen: %s", path.c_str());
  return this->wallpaper_img_;
}

void MenuUi::clear_widgets_() {
  if (this->root_ != nullptr)
    lv_obj_clean(this->root_);   // loescht alle Kinder
  if (this->cur_page_ >= 0)
    for (auto &it : this->pages_[this->cur_page_].items) {
      it.widget = it.sub = it.sub2 = nullptr;
    }
}

void MenuUi::show_page(const std::string &id) {
  int idx = this->find_page_(id);
  if (idx < 0) {
    ESP_LOGW(TAG, "Seite '%s' unbekannt", id.c_str());
    return;
  }
  if (this->nav_stack_.empty() || this->nav_stack_.back() != id)
    this->nav_stack_.push_back(id);
  this->show_page_index(idx);
}

void MenuUi::show_page_index(int idx) {
  if (idx < 0 || idx >= (int) this->pages_.size())
    return;
  this->clear_widgets_();
  this->cur_page_ = idx;
  this->focus_idx_ = -1;
  this->apply_page_theme_(this->pages_[idx].theme);
  this->build_current_();
  if (this->title_ != nullptr)
    lv_label_set_text(this->title_, this->pages_[idx].name.c_str());
  // ersten fokussierbaren Slot fokussieren
  this->focus_move(0, 0);
}

void MenuUi::go_back() {
  if (this->nav_stack_.size() > 1) {
    this->nav_stack_.pop_back();
    this->show_page_index(this->find_page_(this->nav_stack_.back()));
  }
}

void MenuUi::reopen() {
  if (this->pages_.empty())
    return;
  std::string start = this->start_id_.empty() ? this->pages_[0].id : this->start_id_;
  this->nav_stack_.clear();
  this->show_page(start);
}

void MenuUi::go_home() {
  if (this->pages_.empty())
    return;
  std::string home = this->nav_stack_.empty() ? this->pages_[0].id : this->nav_stack_.front();
  this->nav_stack_.clear();
  this->show_page(home);
}

void MenuUi::build_current_() {
  MenuPage &p = this->pages_[this->cur_page_];
  int W = lv_obj_get_width(this->root_);
  int H = lv_obj_get_height(this->root_);
  if (W <= 0) W = 240;
  if (H <= 0) H = 280;
  const int pad = 6;
  int col_w = (W - pad) / p.cols;
  // Zeilenhoehe: quadratisch-ish, aber min 54
  int row_h = col_w;
  if (row_h < 54) row_h = 54;
  if (row_h > 96) row_h = 96;

  for (auto &it : p.items) {
    switch (it.type) {
      case IT_NAV: this->make_navlike_(it, true); break;
      case IT_BUTTON: this->make_button_(it); break;
      case IT_LIGHT: this->make_light_(it); break;
      case IT_COVER: this->make_cover_(it); break;
      case IT_TOGGLE: this->make_toggle_(it); break;
      case IT_LABEL: this->make_label_(it); break;
      default: continue;
    }
    if (it.widget == nullptr)
      continue;
    this->place_(it, col_w, row_h, p.cols, pad);
    lv_obj_set_user_data(it.widget, &it);
    lv_obj_add_event_cb(it.widget, MenuUi::event_cb_, LV_EVENT_CLICKED, this);
    if (it.type != IT_LABEL && it.type != IT_SPACER)
      lv_obj_add_flag(it.widget, LV_OBJ_FLAG_CLICKABLE);
    this->bind_state_(it);
  }
}

void MenuUi::place_(MenuItem &it, int col_w, int row_h, int cols, int pad) {
  int r = it.slot / cols;
  int c = it.slot % cols;
  int span = it.span;
  if (c + span > cols) span = cols - c;
  lv_obj_set_pos(it.widget, pad + c * col_w, pad + r * row_h);
  lv_obj_set_size(it.widget, col_w * span - pad, row_h - pad);
}

void MenuUi::make_navlike_(MenuItem &it, bool nav) {
  lv_obj_t *b = lv_obj_create(this->root_);
  lv_obj_remove_style_all(b);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, this->th_card_, 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_border_width(b, 2, 0);
  lv_obj_set_style_border_color(b, this->th_card_, 0);
  lv_obj_set_style_border_color(b, this->th_accent_, LV_STATE_FOCUSED);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ic = this->make_icon_(b, it.icon);
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, it.label.c_str());
  lv_obj_set_style_text_color(l, this->th_text_, 0);
  lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l, LV_PCT(92));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  if (ic != nullptr) {
    lv_obj_align(ic, LV_ALIGN_CENTER, 0, -12);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 16);
  } else {
    lv_obj_center(l);
  }
  it.widget = b;
  it.sub = l;
}

void MenuUi::make_button_(MenuItem &it) { this->make_navlike_(it, false); }

void MenuUi::make_toggle_(MenuItem &it) {
  lv_obj_t *card = lv_obj_create(this->root_);
  lv_obj_remove_style_all(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, this->th_card_, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, this->th_accent_, LV_STATE_FOCUSED);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ic = this->make_icon_(card, it.icon);
  lv_obj_t *l = lv_label_create(card);
  lv_label_set_text(l, it.label.c_str());
  lv_obj_set_style_text_color(l, this->th_text_, 0);
  if (ic != nullptr) {
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 32, 0);
  } else {
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
  }
  lv_obj_t *dot = lv_obj_create(card);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 14, 14);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(0x555555), 0);
  lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -8, 0);
  it.widget = card;
  it.sub = l;
  it.sub2 = dot;
}

void MenuUi::make_light_(MenuItem &it) {
  lv_obj_t *card = lv_obj_create(this->root_);
  lv_obj_remove_style_all(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, this->th_card_, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, this->th_accent_, LV_STATE_FOCUSED);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ic = this->make_icon_(card, it.icon);
  lv_obj_t *l = lv_label_create(card);
  lv_label_set_text(l, it.label.c_str());
  lv_obj_set_style_text_color(l, this->th_text_, 0);
  if (ic != nullptr) {
    lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 0, -2);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 28, 2);
  } else {
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
  }
  lv_obj_t *sl = lv_slider_create(card);
  lv_slider_set_range(sl, 0, 100);
  lv_obj_set_width(sl, LV_PCT(94));
  lv_obj_align(sl, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_set_style_bg_color(sl, this->th_accent_, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, this->th_accent_, LV_PART_KNOB);
  lv_obj_add_event_cb(
      sl,
      [](lv_event_t *e) {
        auto *self = static_cast<MenuUi *>(lv_event_get_user_data(e));
        auto *it = static_cast<MenuItem *>(lv_obj_get_user_data(lv_obj_get_parent(
            (lv_obj_t *) lv_event_get_target(e))));
        if (self == nullptr || it == nullptr)
          return;
        int v = lv_slider_get_value((lv_obj_t *) lv_event_get_target(e));
        self->fire_ha_number_(it->entity, std::string("light.turn_on"), v);
      },
      LV_EVENT_RELEASED, this);
  it.widget = card;
  it.sub = sl;
  it.sub2 = l;
}

void MenuUi::make_cover_(MenuItem &it) {
  lv_obj_t *card = lv_obj_create(this->root_);
  lv_obj_remove_style_all(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, this->th_card_, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_border_width(card, 2, 0);
  lv_obj_set_style_border_color(card, this->th_accent_, LV_STATE_FOCUSED);
  lv_obj_set_style_pad_all(card, 6, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *ic = this->make_icon_(card, it.icon);
  lv_obj_t *l = lv_label_create(card);
  lv_label_set_text(l, it.label.c_str());
  lv_obj_set_style_text_color(l, this->th_text_, 0);
  if (ic != nullptr) {
    lv_obj_align(ic, LV_ALIGN_TOP_LEFT, 0, -2);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 28, 2);
  } else {
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
  }
  // Bedien-Hinweis: zu / stop / auf (D-Pad Links / Klick / Rechts)
  lv_obj_t *hint = lv_label_create(card);
  lv_label_set_text(hint, LV_SYMBOL_DOWN "  " LV_SYMBOL_STOP "  " LV_SYMBOL_UP);
  lv_obj_set_style_text_color(hint, this->th_text_, 0);
  lv_obj_set_style_text_opa(hint, LV_OPA_60, 0);
  lv_obj_align(hint, LV_ALIGN_RIGHT_MID, 0, 2);
  // Fortschrittsbalken = Position
  lv_obj_t *bar = lv_bar_create(card);
  lv_bar_set_range(bar, 0, 100);
  lv_obj_set_size(bar, LV_PCT(94), 8);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_set_style_bg_color(bar, this->th_accent_, LV_PART_INDICATOR);
  it.widget = card;
  it.sub = bar;
  it.sub2 = l;
}

void MenuUi::make_label_(MenuItem &it) {
  lv_obj_t *l = lv_label_create(this->root_);
  lv_label_set_text(l, it.label.c_str());
  lv_obj_set_style_text_color(l, this->th_text_, 0);
  it.widget = l;
}

void MenuUi::bind_state_(MenuItem &it) {
#ifdef USE_API
  if (it.entity.empty() || api::global_api_server == nullptr)
    return;
  MenuItem *ip = &it;
  const uint32_t gen = this->load_gen_;
  MenuUi *self = this;
  if (it.type == IT_TOGGLE) {
    api::global_api_server->subscribe_home_assistant_state(
        it.entity, optional<std::string>(), [self, ip, gen](StringRef s) {
          if (gen != self->load_gen_ || ip->sub2 == nullptr)
            return;
          bool on = (s == "on");
          lv_obj_set_style_bg_color(ip->sub2, on ? self->th_accent_ : lv_color_hex(0x555555), 0);
        });
  } else if (it.type == IT_LIGHT) {
    api::global_api_server->subscribe_home_assistant_state(
        it.entity, optional<std::string>("brightness"), [self, ip, gen](StringRef s) {
          if (gen != self->load_gen_ || ip->sub == nullptr)
            return;
          int b = atoi(s.c_str());   // 0..255
          lv_slider_set_value(ip->sub, (b * 100) / 255, LV_ANIM_OFF);
        });
  } else if (it.type == IT_COVER) {
    api::global_api_server->subscribe_home_assistant_state(
        it.entity, optional<std::string>("current_position"), [self, ip, gen](StringRef s) {
          if (gen != self->load_gen_ || ip->sub == nullptr)
            return;
          if ((int32_t) (millis() - self->cover_adjust_until_) < 0)
            return;   // Nutzer stellt gerade die Zielposition ein
          lv_bar_set_value(ip->sub, atoi(s.c_str()), LV_ANIM_OFF);
        });
  } else if (it.type == IT_LABEL) {
    api::global_api_server->subscribe_home_assistant_state(
        it.entity, optional<std::string>(), [self, ip, gen](StringRef s) {
          if (gen != self->load_gen_ || ip->widget == nullptr)
            return;
          std::string tmp = s.str();
          lv_label_set_text(ip->widget, tmp.c_str());
        });
  }
#endif
}

void MenuUi::item_action_(MenuItem &it) {
  switch (it.type) {
    case IT_NAV:
      if (!it.target.empty())
        this->show_page(it.target);
      break;
    case IT_BUTTON:
      if (it.press == "back") { this->go_back(); }
      else if (it.press == "home") { this->go_home(); }
      else if (!it.macro.empty() && this->rc_ != nullptr) {
        this->rc_->run_activity(it.macro);   // Makros teilen die Aktivitaets-Registry
      } else if (!it.activity.empty() && this->rc_ != nullptr) {
        this->rc_->run_activity(it.activity);   // nimmt id ODER name
      } else if (!it.device.empty() && this->rc_ != nullptr) {
        this->rc_->send_command(it.device, it.command);   // nimmt id ODER name
      } else if (!it.ha_service.empty()) {
        this->fire_ha_service_(it.ha_service, it.entity);
      }
      break;
    case IT_TOGGLE:
      this->fire_ha_service_("homeassistant.toggle", it.entity);
      break;
    case IT_COVER:
      // Klick = stop. Zielposition per D-Pad Links/Rechts (siehe focus_move).
      this->cancel_timeout("cover_pos");
      this->fire_ha_service_("cover.stop_cover", it.entity);
      break;
    default:
      break;
  }
}

void MenuUi::event_cb_(lv_event_t *e) {
  auto *self = static_cast<MenuUi *>(lv_event_get_user_data(e));
  auto *obj = (lv_obj_t *) lv_event_get_target(e);
  auto *it = static_cast<MenuItem *>(lv_obj_get_user_data(obj));
  if (self != nullptr && it != nullptr)
    self->item_action_(*it);
}

// ---- D-Pad-Fokus ----
void MenuUi::apply_focus_style_() {
  if (this->cur_page_ < 0)
    return;
  auto &items = this->pages_[this->cur_page_].items;
  for (int i = 0; i < (int) items.size(); i++) {
    if (items[i].widget == nullptr || items[i].type == IT_LABEL || items[i].type == IT_SPACER)
      continue;
    if (i == this->focus_idx_)
      lv_obj_add_state(items[i].widget, LV_STATE_FOCUSED);
    else
      lv_obj_clear_state(items[i].widget, LV_STATE_FOCUSED);
  }
}

void MenuUi::focus_move(int dx, int dy) {
  if (this->cur_page_ < 0)
    return;
  auto &items = this->pages_[this->cur_page_].items;
  auto focusable = [&](int i) {
    return i >= 0 && i < (int) items.size() && items[i].widget != nullptr &&
           items[i].type != IT_LABEL && items[i].type != IT_SPACER;
  };
  if (!focusable(this->focus_idx_)) {
    for (int i = 0; i < (int) items.size(); i++)
      if (focusable(i)) { this->focus_idx_ = i; this->apply_focus_style_(); return; }
    return;
  }
  if (dx == 0 && dy == 0) { this->apply_focus_style_(); return; }

  // Links/Rechts auf einem Regler/Balken aendert den Wert, statt den Fokus zu
  // bewegen - genau wie in der fest verdrahteten Menue-Bedienung. Voll breite
  // Regler verlaesst man ueber Hoch/Runter.
  {
    MenuItem &f = items[this->focus_idx_];
    // Licht: Links/Rechts = Helligkeit (Slider -> light.turn_on brightness_pct).
    if (dx != 0 && f.type == IT_LIGHT && f.sub != nullptr &&
        lv_obj_check_type(f.sub, &lv_slider_class)) {
      lv_obj_t *sl = f.sub;
      int32_t lo = lv_slider_get_min_value(sl), hi = lv_slider_get_max_value(sl);
      int32_t step = (hi - lo) / 20;
      if (step < 1) step = 1;
      int32_t nv = lv_slider_get_value(sl) + (dx > 0 ? step : -step);
      if (nv < lo) nv = lo;
      if (nv > hi) nv = hi;
      lv_slider_set_value(sl, nv, LV_ANIM_OFF);
      lv_obj_send_event(sl, LV_EVENT_RELEASED, nullptr);
      return;
    }
    // Rollo: Links/Rechts = Zielposition in 20-%-Schritten (Balken), nach 600 ms
    // Ruhe -> cover.set_cover_position. An den Enden (0 / 100) wirkt das wie
    // ganz zu / ganz auf. Klick = stop (item_action_).
    if (dx != 0 && f.type == IT_COVER && f.sub != nullptr && !f.entity.empty()) {
      int32_t nv = lv_bar_get_value(f.sub) + (dx > 0 ? 20 : -20);
      if (nv < 0) nv = 0;
      if (nv > 100) nv = 100;
      lv_bar_set_value(f.sub, nv, LV_ANIM_OFF);
      this->cover_adjust_until_ = millis() + 2500;   // HA-State-Callback kurz ignorieren
      std::string ent = f.entity;
      this->cancel_timeout("cover_pos");
      this->set_timeout("cover_pos", 600, [this, ent, nv]() {
        this->fire_ha_number_(ent, std::string("cover.set_cover_position"), (int) nv);
      });
      return;
    }
  }

  // naechste fokussierbare Zelle nach Bildschirm-Naehe in Richtung (dx,dy)
  lv_obj_t *cur = items[this->focus_idx_].widget;
  int cx = lv_obj_get_x(cur) + lv_obj_get_width(cur) / 2;
  int cy = lv_obj_get_y(cur) + lv_obj_get_height(cur) / 2;
  int best = -1;
  long best_score = 1 << 30;
  for (int i = 0; i < (int) items.size(); i++) {
    if (i == this->focus_idx_ || !focusable(i))
      continue;
    lv_obj_t *o = items[i].widget;
    int ox = lv_obj_get_x(o) + lv_obj_get_width(o) / 2;
    int oy = lv_obj_get_y(o) + lv_obj_get_height(o) / 2;
    int rx = ox - cx, ry = oy - cy;
    if (dx > 0 && rx <= 4) continue;
    if (dx < 0 && rx >= -4) continue;
    if (dy > 0 && ry <= 4) continue;
    if (dy < 0 && ry >= -4) continue;
    long score = (long) rx * rx + (long) ry * ry;
    // in Bewegungsrichtung gewichten
    if (dx != 0) score += (long) ry * ry * 3;
    if (dy != 0) score += (long) rx * rx * 3;
    if (score < best_score) { best_score = score; best = i; }
  }
  if (best >= 0)
    this->focus_idx_ = best;
  this->apply_focus_style_();
}

void MenuUi::focus_activate() {
  if (this->cur_page_ < 0)
    return;
  auto &items = this->pages_[this->cur_page_].items;
  if (this->focus_idx_ >= 0 && this->focus_idx_ < (int) items.size())
    this->item_action_(items[this->focus_idx_]);
}

}  // namespace menu_ui
}  // namespace esphome

#endif  // USE_ESP32
