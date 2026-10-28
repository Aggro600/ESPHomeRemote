#pragma once
// Musik-Anzeige (2026-10-18, Seite page_musik, cur_page 52): Spektrum-Analyser wie bei den Stereoanlagen der 70er -
// gruene Balken, oben gelb/rot, darueber eine Spitzenmarke, die vom Balken hochgeschubst wird und langsamer
// wieder herunterfaellt. Hoert ueber das Mikrofon der Remote mit (16 kHz), 512-Punkte-FFT, 16 Baender von 50 Hz bis
// 8 kHz (logarithmisch), automatische Verstaerkung (leise und laute Musik fuellen die Anzeige gleich).
//
// Zu sehen: im Dock statt des Smart Displays, solange Musik laeuft (Einstellung musik_dock), oder jederzeit ueber das
// Menue / eine Taste ("musik"). Mit musik_player hoert die Remote nur zu, wenn der "Jetzt laeuft"-Player
// (${ent_jetzt}, text_sensor mp_state) gerade spielt - sonst bleibt das Mikrofon aus.
// Das Mikrofon teilt sie sich mit der Sprache: Sprache hat Vorrang, die Anzeige startet es danach selbst wieder.

#include <cmath>
#include <freertos/FreeRTOS.h>

namespace musik {

static const int N = 512;          // FFT-Laenge (32 ms bei 16 kHz)
static const int BAENDER = 16;
static const int BX = 8, BW = 224, BY = 78, BH = 186;   // Analyser-Feld (BH = 31 LED-Reihen)
static const int SEG = 6;          // LED-Raster: 4 px Licht, 2 px Fuge
static const int SPALTE = BW / BAENDER, BREITE = SPALTE - 3;

struct Zustand {
  // Audio (Mikrofon-Task schreibt, loop liest)
  float ring[N]{};
  volatile int pos{0};
  volatile uint32_t proben{0};
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  volatile bool hoert{false};
  bool registriert{false};
  bool ich_gestartet{false};
  uint32_t strom_ms{0};            // Mikrofonversorgung eingeschaltet (Einschwingen abwarten)
  // Auswertung
  float fenster[N]{};
  float re[N]{}, im[N]{};
  int band_von[BAENDER + 1]{};
  float pegel[BAENDER]{}, spitze[BAENDER]{}, fall[BAENDER]{};
  uint32_t halten_bis[BAENDER]{};
  float referenz{-30.0f};          // automatische Verstaerkung: lautestes Band (dB), faellt langsam
  // Anzeige
  bool gebaut{false};
  lv_obj_t *saeule[BAENDER]{}, *licht[BAENDER]{}, *kappe[BAENDER]{}, *spiegel[BAENDER]{};
  lv_obj_t *titel{nullptr}, *kuenstler{nullptr}, *hinweis{nullptr}, *uhr{nullptr};
  int hoehe[BAENDER]{}, kappe_y[BAENDER]{}, spiegel_h[BAENDER]{};
  lv_timer_t *timer{nullptr};
  uint32_t letzt_ms{0};
  uint32_t still_seit{0};          // Dock: seit wann laeuft keine Musik mehr
  bool vom_dock{false};
  bool demo{false};                // Test: kuenstliche Musik statt Mikrofon (Dienst musik_demo)
  float demo_t{0};
  std::string titel_alt, kuenstler_alt;
  int uhr_min{-1};
};
inline Zustand &z() { static Zustand s; return s; }

// ---------------------------------------------------------------- Bedingungen
inline bool player_spielt() { return mp_state->state == "playing"; }
inline bool sprache_aktiv() { return voice->is_streaming() || ha_voice->is_running(); }
// Darf und soll gerade zugehoert werden? grund = Text fuer die Anzeige, wenn nicht
inline bool soll_hoeren(const char *&grund) {
  grund = nullptr;
  if (!mic_allowed->value()) { grund = "Mikrofon ist aus (Einstellungen › Sprache)"; return false; }
  if (voice->mic_blocked()) { grund = "Mikrofon gesperrt (SD-Karte)"; return false; }
  if (musik_player->value() && !player_spielt()) { grund = "Wartet auf Musik"; return false; }
  if (disp_off->value()) return false;
  return true;
}

// ---------------------------------------------------------------- Mikrofon
inline void registrieren() {
  auto &s = z();
  if (s.registriert) return;
  s.registriert = true;
  remote_mic->add_data_callback([](const std::vector<uint8_t> &d) {
    auto &s = z();
    if (!s.hoert) return;
    portENTER_CRITICAL(&s.mux);
    int p = s.pos;
    for (size_t i = 0; i + 3 < d.size(); i += 4) {
      int32_t v;
      memcpy(&v, &d[i], 4);
      s.ring[p] = (float) (v >> 8) * (1.0f / 8388608.0f);
      p = (p + 1) & (N - 1);
    }
    s.pos = p;
    s.proben += d.size() / 4;
    portEXIT_CRITICAL(&s.mux);
  });
}

inline void hoeren_an() {
  auto &s = z();
  registrieren();
  if (!s.hoert) {
    s.hoert = true;
    s.strom_ms = millis();
    mic_power->turn_on();
    ESP_LOGI("musik", "Mikrofon an");
  }
  // Sprache hat das Mikrofon beendet, oder es lief noch nicht: (wieder) starten, sobald es eingeschwungen ist
  if (remote_mic->is_stopped() && !sprache_aktiv() && millis() - s.strom_ms > 300) {
    remote_mic->start();
    s.ich_gestartet = true;
  }
}

inline void hoeren_aus() {
  auto &s = z();
  if (!s.hoert) return;
  s.hoert = false;
  if (s.ich_gestartet && !sprache_aktiv()) remote_mic->stop();
  s.ich_gestartet = false;
  if (!voice->mic_blocked()) apply_mic_power->execute();   // Versorgung wie eingestellt (die SD-Skripte steuern sie selbst)
  ESP_LOGI("musik", "Mikrofon aus");
}

// Die SD-Karte kommt (sd_strom_an): Mikrofon sofort anhalten. set_mic_blocked() stoppt nur ein von der Sprache
// gestartetes Mikrofon - lief es fuer die Anzeige weiter, startete die Karte auf denselben Pins, und der SPI-Treiber
// stuerzte ab (2026-10-18, LoadProhibited in spi_device_polling_start).
inline void sd_kommt() {
  auto &s = z();
  if (!s.hoert) return;
  s.hoert = false;
  if (s.ich_gestartet) remote_mic->stop();
  s.ich_gestartet = false;
  ESP_LOGI("musik", "Mikrofon aus (SD-Karte)");
}

// Alle 250 ms aus dockanz::takt (ESPHome-Zeitplan, laeuft auch, wenn LVGL bei dunklem Display pausiert):
// hoert die Anzeige, obwohl sie nicht zu sehen ist oder nicht darf, Mikrofon aus.
inline void waechter() {
  const char *grund = nullptr;
  if (z().hoert && (cur_page->value() != 52 || !soll_hoeren(grund))) hoeren_aus();
}

// ---------------------------------------------------------------- FFT
inline void vorbereiten() {
  auto &s = z();
  for (int i = 0; i < N; i++) s.fenster[i] = 0.5f - 0.5f * cosf(2.0f * (float) M_PI * i / (N - 1));
  // Baender logarithmisch 50 Hz .. 8 kHz, jedes mindestens ein eigenes FFT-Feld (31,25 Hz)
  const float f0 = 50.0f, f1 = 8000.0f, df = 16000.0f / N;
  int vorher = 1;
  for (int b = 0; b <= BAENDER; b++) {
    const float f = f0 * powf(f1 / f0, (float) b / BAENDER);
    int k = (int) lroundf(f / df);
    if (k <= vorher && b > 0) k = vorher + 1;
    if (k > N / 2) k = N / 2;
    s.band_von[b] = k;
    vorher = k;
  }
}

inline void fft(float *re, float *im) {
  for (int i = 1, j = 0; i < N; i++) {
    int bit = N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
  }
  for (int len = 2; len <= N; len <<= 1) {
    const float w = -2.0f * (float) M_PI / len;
    const float wr = cosf(w), wi = sinf(w);
    for (int i = 0; i < N; i += len) {
      float cr = 1.0f, ci = 0.0f;
      for (int k = 0; k < len / 2; k++) {
        const int a = i + k, b = a + len / 2;
        const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
        re[b] = re[a] - tr; im[b] = im[a] - ti;
        re[a] += tr; im[a] += ti;
        const float n = cr * wr - ci * wi;
        ci = cr * wi + ci * wr;
        cr = n;
      }
    }
  }
}

// Neue Pegel 0..1 je Band (aus dem Ringpuffer); false = noch keine Daten
inline bool auswerten(float dt) {
  auto &s = z();
  if (!s.hoert || s.proben < (uint32_t) N || millis() - s.strom_ms < 500) return false;
  portENTER_CRITICAL(&s.mux);
  const int p = s.pos;
  for (int i = 0; i < N; i++) s.re[i] = s.ring[(p + i) & (N - 1)];
  portEXIT_CRITICAL(&s.mux);
  float mittel = 0;
  for (int i = 0; i < N; i++) mittel += s.re[i];
  mittel /= N;
  for (int i = 0; i < N; i++) { s.re[i] = (s.re[i] - mittel) * s.fenster[i]; s.im[i] = 0; }
  fft(s.re, s.im);
  float db[BAENDER], lautest = -120.0f;
  for (int b = 0; b < BAENDER; b++) {
    float e = 0;
    for (int k = s.band_von[b]; k < s.band_von[b + 1]; k++) e += s.re[k] * s.re[k] + s.im[k] * s.im[k];
    e /= (float) (s.band_von[b + 1] - s.band_von[b]);
    // Hoehen etwas anheben (Musik hat oben weniger Energie, sonst bleibt die rechte Haelfte flach)
    db[b] = 10.0f * log10f(e + 1e-12f) + 4.0f * (float) b / BAENDER * 3.0f;
    lautest = std::max(lautest, db[b]);
  }
  // Automatische Verstaerkung: Referenz folgt dem lautesten Band sofort nach oben, faellt mit 4 dB/s
  s.referenz = std::max(lautest, s.referenz - 4.0f * dt);
  s.referenz = std::max(s.referenz, -62.0f);   // Rauschsperre: Stille bleibt dunkel
  const float spanne = 42.0f;
  for (int b = 0; b < BAENDER; b++) {
    float v = (db[b] - (s.referenz - spanne)) / spanne;
    v = std::max(0.0f, std::min(1.0f, v));
    v = v * v * (1.4f - 0.4f * v);   // unten ruhiger, oben knackiger
    // Balken: sofort hoch, weich herunter
    s.pegel[b] = v > s.pegel[b] ? v : s.pegel[b] + (v - s.pegel[b]) * std::min(1.0f, dt * 9.0f);
  }
  return true;
}

// Demo (nur zum Testen der Optik, Dienst musik_demo): Bass-Schlag im Takt, wandernde Mitten, flackernde Hoehen
inline void demo_pegel(float dt) {
  auto &s = z();
  s.demo_t += dt;
  const float t = s.demo_t;
  const float schlag = powf(std::max(0.0f, cosf(t * 2.0f * (float) M_PI * 2.0f)), 6.0f);   // 120 BPM
  for (int b = 0; b < BAENDER; b++) {
    const float x = (float) b / (BAENDER - 1);
    float v = 0.85f * schlag * expf(-x * 5.0f) + 0.55f * (0.5f + 0.5f * sinf(t * 1.7f + x * 7.0f)) * expf(-powf((x - 0.45f) * 2.2f, 2))
            + 0.25f * (float) (esp_random() % 100) / 100.0f * x;
    v = std::max(0.0f, std::min(1.0f, v));
    s.pegel[b] = v > s.pegel[b] ? v : s.pegel[b] + (v - s.pegel[b]) * std::min(1.0f, dt * 9.0f);
  }
}
inline void demo(bool an) { z().demo = an; ESP_LOGI("musik", "Demo %s", an ? "an" : "aus"); }

// Ohne Daten: alles sinkt langsam ab
inline void abklingen(float dt) {
  auto &s = z();
  for (int b = 0; b < BAENDER; b++) s.pegel[b] = std::max(0.0f, s.pegel[b] - dt * 1.2f);
}

// Spitzenmarken: werden hochgeschubst, halten kurz, fallen dann beschleunigt (langsamer als der Balken)
inline void spitzen(float dt) {
  auto &s = z();
  const uint32_t jetzt = millis();
  for (int b = 0; b < BAENDER; b++) {
    if (s.pegel[b] >= s.spitze[b]) {
      s.spitze[b] = s.pegel[b];
      s.fall[b] = 0;
      s.halten_bis[b] = jetzt + 420;
    } else if ((int32_t) (jetzt - s.halten_bis[b]) > 0) {
      s.fall[b] += 1.5f * dt;
      s.spitze[b] = std::max(s.pegel[b], s.spitze[b] - s.fall[b] * dt);
    }
  }
}

// ---------------------------------------------------------------- Aufbau
inline lv_obj_t *kasten(lv_obj_t *p) {
  lv_obj_t *o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);
  return o;
}

// Immer genau eine Zeile: lange Titel laufen durch (Laufschrift), statt umzubrechen
inline lv_obj_t *text(lv_obj_t *p, const lv_font_t *f, uint32_t farbe, bool laufen = false) {
  lv_obj_t *l = lv_label_create(p);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(farbe), 0);
  lv_obj_set_size(l, 220, lv_font_get_line_height(f));
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(l, laufen ? LV_LABEL_LONG_SCROLL_CIRCULAR : LV_LABEL_LONG_DOT);
  lv_obj_set_style_anim_duration(l, 9000, 0);
  lv_label_set_text(l, "");
  lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
  return l;
}

inline void aufbauen() {
  auto &s = z();
  if (s.gebaut) return;
  s.gebaut = true;
  vorbereiten();
  lv_obj_t *r = musik_root;
  lv_obj_clean(r);
  lv_obj_set_style_bg_color(r, lv_color_hex(0x05070A), 0);
  lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);

  s.uhr = text(r, a13->get_lv_font(), 0x5B6472);
  lv_obj_align(s.uhr, LV_ALIGN_TOP_MID, 0, 6);
  s.titel = text(r, a15->get_lv_font(), 0xF1F3F5, true);
  lv_obj_align(s.titel, LV_ALIGN_TOP_MID, 0, 26);
  s.kuenstler = text(r, a13->get_lv_font(), 0x22D3EE);
  lv_obj_align(s.kuenstler, LV_ALIGN_TOP_MID, 0, 48);

  // Rahmen des Feldes: feines Raster wie bei einem Messgeraet (Hilfslinien bei 25/50/75 %)
  for (int i = 1; i < 4; i++) {
    lv_obj_t *l = kasten(r);
    lv_obj_set_size(l, BW, 1);
    lv_obj_set_pos(l, BX, BY + BH * i / 4);
    lv_obj_set_style_bg_color(l, lv_color_hex(0x10161E), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  }

  for (int b = 0; b < BAENDER; b++) {
    const int x = BX + b * SPALTE + 1;
    // Saeule = Ausschnitt (waechst von unten); darin das volle Farbband gruen -> gelb -> rot, bleibt stehen
    s.saeule[b] = kasten(r);
    lv_obj_set_size(s.saeule[b], BREITE, 0);
    lv_obj_set_pos(s.saeule[b], x, BY + BH);
    // (Kinder werden auf die Saeule zugeschnitten - LVGL-Standard ohne OVERFLOW_VISIBLE)
    s.licht[b] = kasten(s.saeule[b]);
    lv_obj_set_size(s.licht[b], BREITE, BH);
    lv_obj_align(s.licht[b], LV_ALIGN_BOTTOM_MID, 0, 0);
    // Zonen wie bei der Stereoanlage: oben rot, darunter orange/gelb, unten gruen
    struct Zone { int von, bis; uint32_t oben, unten; };
    static const Zone ZONEN[] = {{0, 14, 0xFF1F3A, 0xFF4A2A}, {14, 32, 0xFF7A1A, 0xFFD21F},
                                 {32, 55, 0xF5E62A, 0x3BEA6B}, {55, 100, 0x2BEA6B, 0x0FB85A}};
    for (const auto &zo : ZONEN) {
      lv_obj_t *o = kasten(s.licht[b]);
      lv_obj_set_size(o, BREITE, BH * (zo.bis - zo.von) / 100 + 1);
      lv_obj_set_pos(o, 0, BH * zo.von / 100);
      lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(o, lv_color_hex(zo.oben), 0);
      lv_obj_set_style_bg_grad_color(o, lv_color_hex(zo.unten), 0);
      lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_VER, 0);
    }
    // Spiegelung unter der Grundlinie (blass, nach unten auslaufend)
    s.spiegel[b] = kasten(r);
    lv_obj_set_size(s.spiegel[b], BREITE, 0);
    lv_obj_set_pos(s.spiegel[b], x, BY + BH + 3);
    lv_obj_set_style_bg_opa(s.spiegel[b], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s.spiegel[b], lv_color_hex(0x18E06A), 0);
    lv_obj_set_style_bg_grad_color(s.spiegel[b], lv_color_hex(0x18E06A), 0);
    lv_obj_set_style_bg_grad_dir(s.spiegel[b], LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(s.spiegel[b], 70, 0);
    lv_obj_set_style_bg_grad_opa(s.spiegel[b], 0, 0);
  }
  // LED-Raster: dunkle Fugen quer ueber alle Saeulen (ueber dem Licht, unter den Spitzen)
  for (int y = BY + SEG - 2; y < BY + BH; y += SEG) {
    lv_obj_t *f = kasten(r);
    lv_obj_set_size(f, BW, 2);
    lv_obj_set_pos(f, BX, y);
    lv_obj_set_style_bg_color(f, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
  }
  // Senkrechte Fugen zwischen den Saeulen bleiben Hintergrund (Abstand 3 px)
  // Grundlinie
  lv_obj_t *g = kasten(r);
  lv_obj_set_size(g, BW, 1);
  lv_obj_set_pos(g, BX, BY + BH + 1);
  lv_obj_set_style_bg_color(g, lv_color_hex(0x22D3EE), 0);
  lv_obj_set_style_bg_opa(g, 90, 0);
  // Spitzenmarken: helles Cyan mit Leuchten
  for (int b = 0; b < BAENDER; b++) {
    s.kappe[b] = kasten(r);
    lv_obj_set_size(s.kappe[b], BREITE, 3);
    lv_obj_set_pos(s.kappe[b], BX + b * SPALTE + 1, BY + BH - 3);
    lv_obj_set_style_bg_color(s.kappe[b], lv_color_hex(0xBFF6FF), 0);
    lv_obj_set_style_bg_opa(s.kappe[b], LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s.kappe[b], lv_color_hex(0x22D3EE), 0);
    lv_obj_set_style_border_width(s.kappe[b], 1, 0);
    lv_obj_set_style_border_side(s.kappe[b], LV_BORDER_SIDE_TOP, 0);
    lv_obj_add_flag(s.kappe[b], LV_OBJ_FLAG_HIDDEN);
    s.kappe_y[b] = -1;
  }
  // Frequenzskala unter der Spiegelung (Messgeraet-Optik)
  static const struct { int band; const char *t; } SKALA[] = {{0, "50"}, {4, "200"}, {8, "800"}, {12, "3k"}, {15, "8k"}};
  for (const auto &e : SKALA) {
    lv_obj_t *l = lv_label_create(r);
    lv_obj_set_style_text_font(l, a13->get_lv_font(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x3A4250), 0);
    lv_label_set_text(l, e.t);
    lv_obj_update_layout(l);
    const int mitte = BX + e.band * SPALTE + SPALTE / 2;
    const int lw = (int) lv_obj_get_width(l);
    lv_obj_set_pos(l, std::max(BX, std::min(BX + BW - lw, mitte - lw / 2)), BY + BH + 40);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
  }
  s.hinweis = text(r, a13->get_lv_font(), 0x5B6472);
  lv_obj_align(s.hinweis, LV_ALIGN_TOP_MID, 0, BY + BH / 2 - 8);
}

inline void kopf_zeigen() {
  auto &s = z();
  const std::string t = mp_title->state, k = mp_artist->state;
  const bool spielt = player_spielt();
  const std::string tt = spielt ? t : std::string(), kk = spielt ? k : std::string();
  if (tt != s.titel_alt) { s.titel_alt = tt; lv_label_set_text(s.titel, tt.c_str()); }
  if (kk != s.kuenstler_alt) { s.kuenstler_alt = kk; lv_label_set_text(s.kuenstler, kk.c_str()); }
  auto u = sntp_time->now();
  if (u.is_valid() && u.minute != s.uhr_min) {
    s.uhr_min = u.minute;
    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", u.hour, u.minute);
    lv_label_set_text(s.uhr, b);
  }
}

inline void zeichnen() {
  auto &s = z();
  for (int b = 0; b < BAENDER; b++) {
    // auf das LED-Raster einrasten: ganze Segmente
    int h = (int) (s.pegel[b] * BH);
    h = h / SEG * SEG;
    if (h != s.hoehe[b]) {
      s.hoehe[b] = h;
      lv_obj_set_height(s.saeule[b], h);
      lv_obj_set_y(s.saeule[b], BY + BH - h);
      const int sh = std::min(h / 4, 34);
      if (sh != s.spiegel_h[b]) { s.spiegel_h[b] = sh; lv_obj_set_height(s.spiegel[b], sh); }
    }
    // ganz unten: Marke verschwindet
    int ky = s.spitze[b] < 0.03f ? -1 : std::max(BY - 4, BY + BH - (int) (s.spitze[b] * BH) - 4);
    if (ky != s.kappe_y[b]) {
      if (ky < 0) lv_obj_add_flag(s.kappe[b], LV_OBJ_FLAG_HIDDEN);
      else {
        if (s.kappe_y[b] < 0) lv_obj_clear_flag(s.kappe[b], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s.kappe[b], ky);
      }
      s.kappe_y[b] = ky;
    }
  }
}

// ---------------------------------------------------------------- Ablauf
inline void bild(lv_timer_t *) {
  auto &s = z();
  if (cur_page->value() != 52) {
    hoeren_aus();
    if (s.timer) { lv_timer_delete(s.timer); s.timer = nullptr; }
    return;
  }
  const uint32_t jetzt = millis();
  const float dt = std::min(0.1f, (jetzt - s.letzt_ms) / 1000.0f);
  s.letzt_ms = jetzt;
  const char *grund = nullptr;
  if (soll_hoeren(grund)) hoeren_an();
  else hoeren_aus();
  if (s.demo) demo_pegel(dt);
  else if (!auswerten(dt)) abklingen(dt);
  spitzen(dt);
  zeichnen();
  static const char *alt = nullptr;
  if (s.demo) grund = nullptr;
  if (grund != alt) { alt = grund; lv_label_set_text(s.hinweis, grund ? grund : ""); }
  static uint32_t kopf_ms = 0;
  if (jetzt - kopf_ms > 500) { kopf_ms = jetzt; kopf_zeigen(); }
}

// Seite geladen (on_load)
inline void geladen() {
  auto &s = z();
  aufbauen();
  s.titel_alt = s.kuenstler_alt = "\x01";
  s.uhr_min = -1;
  s.letzt_ms = millis();
  s.still_seit = 0;
  kopf_zeigen();
  if (!s.timer) s.timer = lv_timer_create(bild, 33, nullptr);   // ~30 Bilder/s
}

inline void zeigen(bool vom_dock) {
  z().vom_dock = vom_dock;
  popup_seite->value() = -1;
  tasten::seite_zeigen(page_musik, LV_SCR_LOAD_ANIM_FADE_IN);
}

// Tipp auf die Anzeige: zur Fernbedienung (wie beim Smart Display)
inline void beruehrt() {
  last_input_ms->value() = millis();
  go_home->execute();
}

// Fuer die Dock-Anzeige: soll statt des Smart Displays die Musik-Anzeige laufen?
inline bool im_dock_zeigen() {
  if (!musik_dock->value() || !mic_allowed->value()) return false;
  return player_spielt();
}

// Alle 250 ms aus dockanz::takt: im Dock zwischen Smart Display und Musik wechseln
inline void dock_takt(bool im_dock) {
  auto &s = z();
  waechter();
  const int p = cur_page->value();
  if (!im_dock) {
    if (p == 52 && s.vom_dock) { s.vom_dock = false; go_home->execute(); }
    return;
  }
  if (p == 50 && im_dock_zeigen()) { zeigen(true); return; }
  if (p == 52 && s.vom_dock) {
    // Musik vorbei: nach 8 s zurueck zum Smart Display
    if (im_dock_zeigen()) s.still_seit = 0;
    else if (s.still_seit == 0) s.still_seit = millis();
    else if (millis() - s.still_seit > 8000) { s.still_seit = 0; s.vom_dock = false; dockanz::zeigen(); }
    // Taste im Dock: Fernbedienung
    if (millis() - last_input_ms->value() < 400) { s.vom_dock = false; go_home->execute(); }
  }
}

}  // namespace musik
