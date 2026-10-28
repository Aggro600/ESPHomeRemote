#pragma once
// Bewegungsgesten (2026-10-17): Schuetteln, Kippen links/rechts und eigene, an der Remote angelernte Gesten ("Zauberstab").
// Belegung im Konfigurator (Fernbedienung > Gesten), Daten in cfg.gesten:
//   schuetteln / kipp_l / kipp_r: {an, s: [Schritte], k: Taste}
//   eigen: [{id, n, an, f: 2, v: [Vorlage als Hex, 96 Werte = 32 Punkte x rechts/vorn/oben], s: [Schritte], k: Taste}]
//   f = Format: 2 = Lage-Rahmen (seit 2026-10-17 abends). Aeltere Vorlagen (Geraeteachsen) werden ignoriert -> neu anlernen.
//   k (optional) = wirkt wie ein kurzer Druck dieser Taste in der aktuellen Aktivitaet (an der Remote waehlbar)
// Messung: LIS3DH-FIFO (200 Hz), je 4 Messungen gemittelt -> 50 Hz. Schwerkraft per Tiefpass abgezogen.
// Eine Bewegung beginnt nach Ruhe (>= 300 ms) mit einem deutlichen Ausschlag und endet mit erneuter Ruhe;
// danach Vergleich mit den Vorlagen. Erkannt wird die FORM, nicht eine 1:1-Kopie:
//   - Lage-Rahmen: Beschleunigung in rechts/vorn/oben (aus der Schwerkraft beim Start) - egal wie die Remote
//     in der Hand gedreht ist; dazu Vergleich mit +-30 Grad Drehung um die Hochachse (anders zum TV gewandt)
//   - Groesse egal (auf gleiche Staerke normiert), Tempo egal (DTW), ruhige Enden abgeschnitten
//   - Kreise: sind alle Proben ein Kreis (Drehung >= 270 Grad in einer Ebene, rund, gleichmaessig), zaehlt jeder
//     Kreis in derselben Ebene und Drehrichtung - gross, klein, schnell, langsam, 1-2 Runden, anderer Startpunkt
//   - Grenze je Geste aus der Streuung der eigenen Proben (wer ungenau anlernt, bekommt mehr Spielraum)
// Sicherheitslogik gegen Fehlausloesungen:
//   - nur bei Ruhe vorher (beim Laufen, Tragen, Hochheben aus der Hand passiert nichts)
//   - nicht im Dock und nicht am Ladekabel, nicht in den 800 ms nach einer Taste/Beruehrung
//   - Abstand zur zweitbesten Geste (eindeutig), danach 1,2 s Pause
//   - Display aus: nur mit "Gesten auch bei dunklem Display"
// Rueckmeldung: kurzer Hinweis oben auf dem Display; an HA das Ereignis esphome.remote_geste_erkannt (Panel: Test).

namespace gestenseite { inline void probe(const int8_t *w, uint32_t dauer); }

namespace gesten {

static const int PUNKTE = 32;
static const float HZ = 50.0f;

struct Vorlage { int8_t w[PUNKTE * 3]; };
struct Form { bool kreis{false}; float n[3]{0, 0, 0}; float dreh{0}; };
struct Eigene {
  std::string id, name;
  std::vector<Vorlage> vorlagen;
  float grenze{15};          // Abstand, bis zu dem die Geste zaehlt
  bool kreis{false};         // alle Proben Kreise -> Formvergleich
  float n[3]{0, 0, 0};       // Ebene + Drehrichtung des Kreises (Lage-Rahmen)
};

struct Zustand {
  bool fifo_an{false};
  // Tiefpass Schwerkraft (mg) und Rohwert
  float gx{0}, gy{0}, gz{0};   // Schwerkraft (in Ruhe nachgefuehrt)
  float fx{0}, fy{0}, fz{0};   // schneller Tiefpass (~80 ms): Abweichung davon = Unruhe
  bool g_ok{false};
  uint32_t ruhe_ms{0};         // zuletzt mindestens 300 ms ruhig (Voraussetzung fuer den Start einer Geste)
  // Segment
  bool in_bewegung{false};
  int ruhig{0};                 // ruhige Messungen in Folge
  std::vector<float> seg;       // x,y,z dynamisch (mg)
  float sx{0}, sy{0}, sz{0};    // Schwerkraft beim Start
  uint32_t sperre_bis{0};
  // Kippen
  float roll0{0};
  bool roll_ok{false};
  int kipp_cnt{0};
  int kipp_richtung{0};
  bool kipp_gesperrt{false};
  // Anlernen
  bool lernen{false};          // Anlernen (esphomeremote-gestenseite.h): Bewegungen gehen als Probe dorthin
  // Vorlagen-Cache
  uint32_t gen{0xFFFFFFFF};
  std::vector<Eigene> eigene;
  // Hinweis
  lv_obj_t *toast{nullptr};
  uint32_t toast_bis{0};
  uint32_t debug_ms{0};
};
inline Zustand &z() { static Zustand s; return s; }

inline bool hex_lesen(const char *h, int8_t *out, int n) {
  auto nib = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  for (int i = 0; i < n; i++) {
    int a = nib(h[2 * i]), b = nib(h[2 * i + 1]);
    if (a < 0 || b < 0) return false;
    out[i] = (int8_t) (uint8_t) (a * 16 + b);
  }
  return true;
}

// DTW mit Band (+-6 Punkte), mittlerer Abstand je Schritt
inline float dtw(const float *a, const int8_t *b) {
  static float D[PUNKTE + 1][PUNKTE + 1];
  const int BAND = 6;
  for (int i = 0; i <= PUNKTE; i++)
    for (int j = 0; j <= PUNKTE; j++) D[i][j] = 1e9f;
  D[0][0] = 0;
  for (int i = 1; i <= PUNKTE; i++) {
    for (int j = std::max(1, i - BAND); j <= std::min(PUNKTE, i + BAND); j++) {
      float d = 0;
      for (int k = 0; k < 3; k++) { const float x = a[(i - 1) * 3 + k] - b[(j - 1) * 3 + k]; d += x * x; }
      d = sqrtf(d);
      D[i][j] = d + std::min(D[i - 1][j - 1], std::min(D[i - 1][j], D[i][j - 1]));
    }
  }
  return D[PUNKTE][PUNKTE] / PUNKTE;
}

// Abstand mit Drehung um die Hochachse (+-30 Grad: anders zum Fernseher gewandt). Index 2 = oben.
static const int WINKEL = 5;
struct Gedreht { float w[WINKEL][PUNKTE * 3]; };
inline void drehen(const int8_t *w, Gedreht &g) {
  static const float GRAD[WINKEL] = {-30, -15, 0, 15, 30};
  for (int a = 0; a < WINKEL; a++) {
    const float c = cosf(GRAD[a] * 0.0174533f), s = sinf(GRAD[a] * 0.0174533f);
    for (int i = 0; i < PUNKTE; i++) {
      const float x = w[i * 3], y = w[i * 3 + 1];
      g.w[a][i * 3] = c * x - s * y;
      g.w[a][i * 3 + 1] = s * x + c * y;
      g.w[a][i * 3 + 2] = w[i * 3 + 2];
    }
  }
}
inline float abstand(const Gedreht &g, const int8_t *v) {
  float d = 1e9f;
  for (int a = 0; a < WINKEL; a++) d = std::min(d, dtw(g.w[a], v));
  return d;
}
inline float abstand(const int8_t *w, const int8_t *v) {
  static Gedreht g;
  drehen(w, g);
  return abstand(g, v);
}

// Form einer Vorlage: Kreis? Ebene/Drehrichtung = Summe der Kreuzprodukte aufeinanderfolgender Punkte.
// Kreis = >= 270 Grad Drehung, flach (< 30 % ausserhalb der Ebene), rund (schmale Achse >= 30 % der breiten),
// gleichmaessig (>= 85 % der Drehung in eine Richtung). Simulation 2026-10-17: echte Kreise 100 %, Zufall < 1 %.
inline Form form(const int8_t *w) {
  Form f;
  float m[PUNKTE], mm = 0;
  for (int i = 0; i < PUNKTE; i++) {
    m[i] = sqrtf((float) w[i * 3] * w[i * 3] + (float) w[i * 3 + 1] * w[i * 3 + 1] + (float) w[i * 3 + 2] * w[i * 3 + 2]);
    mm = std::max(mm, m[i]);
  }
  if (mm < 4) return f;
  auto gilt = [&](int i) { return m[i] > 0.25f * mm && m[i + 1] > 0.25f * mm; };
  float L[3] = {0, 0, 0};
  for (int i = 0; i < PUNKTE - 1; i++) {
    if (!gilt(i)) continue;
    const int8_t *a = w + i * 3, *b = w + i * 3 + 3;
    L[0] += (float) a[1] * b[2] - (float) a[2] * b[1];
    L[1] += (float) a[2] * b[0] - (float) a[0] * b[2];
    L[2] += (float) a[0] * b[1] - (float) a[1] * b[0];
  }
  const float ll = sqrtf(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
  if (ll < 1) return f;
  const float n[3] = {L[0] / ll, L[1] / ll, L[2] / ll};
  float pr[PUNKTE][3], aus = 0, ges = 0;
  for (int i = 0; i < PUNKTE; i++) {
    const float d = w[i * 3] * n[0] + w[i * 3 + 1] * n[1] + w[i * 3 + 2] * n[2];
    for (int k = 0; k < 3; k++) pr[i][k] = w[i * 3 + k] - d * n[k];
    aus += d * d;
    ges += m[i] * m[i];
  }
  float th = 0, ab = 0;
  for (int i = 0; i < PUNKTE - 1; i++) {
    if (!gilt(i)) continue;
    const float *a = pr[i], *b = pr[i + 1];
    const float c[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    const float d = atan2f(c[0] * n[0] + c[1] * n[1] + c[2] * n[2], a[0] * b[0] + a[1] * b[1] + a[2] * b[2]) * 57.2958f;
    if (fabsf(d) < 100) { th += d; ab += fabsf(d); }
  }
  // Rundheit: Kovarianz in der Ebene (Basis e1/e2)
  float e1[3];
  if (fabsf(n[0]) < 0.9f) { e1[0] = 0; e1[1] = n[2]; e1[2] = -n[1]; }       // n x (1,0,0)
  else { e1[0] = -n[2]; e1[1] = 0; e1[2] = n[0]; }                           // n x (0,1,0)
  const float el = sqrtf(e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2]);
  for (float &x : e1) x /= el;
  const float e2[3] = {n[1] * e1[2] - n[2] * e1[1], n[2] * e1[0] - n[0] * e1[2], n[0] * e1[1] - n[1] * e1[0]};
  float sxx = 0, syy = 0, sxy = 0;
  for (int i = 0; i < PUNKTE; i++) {
    const float x = pr[i][0] * e1[0] + pr[i][1] * e1[1] + pr[i][2] * e1[2];
    const float y = pr[i][0] * e2[0] + pr[i][1] * e2[1] + pr[i][2] * e2[2];
    sxx += x * x; syy += y * y; sxy += x * y;
  }
  const float tr = sxx + syy, de = sqrtf(std::max(0.0f, (sxx - syy) * (sxx - syy) / 4 + sxy * sxy));
  const float l1 = tr / 2 + de, l2 = tr / 2 - de;
  const float rund = l1 > 0 ? l2 / l1 : 0, flach = ges > 0 ? aus / ges : 1, gleich = ab > 0 ? th / ab : 0;
  f.dreh = th;
  for (int k = 0; k < 3; k++) f.n[k] = n[k];
  f.kreis = th >= 270 && flach < 0.3f && rund >= 0.3f && gleich >= 0.85f;
  return f;
}

inline float n_dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// Segment (dynamische Beschleunigung, Geraeteachsen, mg) -> 32 Punkte im Lage-Rahmen rechts/vorn/oben:
// oben = Schwerkraft beim Start (gx/gy/gz), vorn = Laengsachse (Y) waagerecht gelegt (fast senkrecht gehalten: X),
// rechts = vorn x oben. Ruhige Enden (< 20 % der Spitze) weg, leicht geglaettet, auf gleiche Staerke normiert
// (Mittel 400 mg): ein kleiner, langsamer Kreis passt dann genauso wie ein grosser. Einheit danach 8 mg.
inline void abtasten(const std::vector<float> &seg, float gx, float gy, float gz, int8_t *out) {
  const int n0 = seg.size() / 3;
  float mm = 0;
  std::vector<float> m(n0);
  for (int i = 0; i < n0; i++) {
    m[i] = sqrtf(seg[i * 3] * seg[i * 3] + seg[i * 3 + 1] * seg[i * 3 + 1] + seg[i * 3 + 2] * seg[i * 3 + 2]);
    mm = std::max(mm, m[i]);
  }
  int a = 0, b = n0 - 1;
  while (a < b && m[a] < 0.2f * mm) a++;
  while (b > a && m[b] < 0.2f * mm) b--;
  a = std::max(0, a - 1); b = std::min(n0 - 1, b + 1);
  if (b - a < 8) { a = 0; b = n0 - 1; }
  const int n = std::max(1, b - a + 1);
  // Rahmen
  float u[3] = {gx, gy, gz};
  float ul = sqrtf(n_dot(u, u));
  if (ul < 100) { u[0] = 0; u[1] = 0; u[2] = 1; ul = 1; }
  for (float &x : u) x /= ul;
  float f[3] = {-u[1] * u[0], 1 - u[1] * u[1], -u[1] * u[2]};   // Y waagerecht
  if (sqrtf(n_dot(f, f)) < 0.35f) { f[0] = 1 - u[0] * u[0]; f[1] = -u[0] * u[1]; f[2] = -u[0] * u[2]; }
  const float fl = sqrtf(n_dot(f, f));
  for (float &x : f) x /= fl;
  const float r[3] = {f[1] * u[2] - f[2] * u[1], f[2] * u[0] - f[0] * u[2], f[0] * u[1] - f[1] * u[0]};
  std::vector<float> t(n * 3);
  float q = 0;
  for (int i = 0; i < n; i++) {
    float p[3];
    for (int k = 0; k < 3; k++) {
      const int i0 = a + std::max(0, i - 1), i1 = a + i, i2 = a + std::min(n - 1, i + 1);
      p[k] = (seg[i0 * 3 + k] + seg[i1 * 3 + k] + seg[i2 * 3 + k]) / 3;
    }
    t[i * 3] = n_dot(p, r); t[i * 3 + 1] = n_dot(p, f); t[i * 3 + 2] = n_dot(p, u);
    q += n_dot(p, p);
  }
  const float rms = sqrtf(q / n);
  const float k = rms > 1.0f ? 400.0f / rms : 1.0f;
  for (int p = 0; p < PUNKTE; p++) {
    const float tt = n > 1 ? (float) p * (n - 1) / (PUNKTE - 1) : 0;
    const int i = (int) tt;
    const float fr = tt - i;
    for (int ax = 0; ax < 3; ax++) {
      const float v0 = t[i * 3 + ax], v1 = t[std::min(i + 1, n - 1) * 3 + ax];
      out[p * 3 + ax] = (int8_t) std::max(-127.0f, std::min(127.0f, (v0 + (v1 - v0) * fr) * k / 8.0f));
    }
  }
}

// Grundgrenze je Empfindlichkeit; die Grenze einer Geste waechst mit der Streuung ihrer Proben (bis +10)
inline float grundgrenze() {
  static const float G[3] = {12.0f, 15.0f, 18.0f};
  return G[std::max(0, std::min(2, (int) gesten_sens->value()))];
}

// Gemeinsame Merkmale einer Gruppe von Proben (auch beim Anlernen)
inline void gruppe(Eigene &g) {
  const int n = g.vorlagen.size();
  float intra = 0;
  for (int i = 0; i < n; i++)
    for (int j = i + 1; j < n; j++) intra = std::max(intra, abstand(g.vorlagen[i].w, g.vorlagen[j].w));
  const float b = grundgrenze();
  g.grenze = std::max(b, std::min(b + 10, intra * 1.5f + 3));
  g.kreis = n > 0;
  float s[3] = {0, 0, 0};
  std::vector<Form> fo;
  for (const auto &v : g.vorlagen) {
    fo.push_back(form(v.w));
    if (!fo.back().kreis) g.kreis = false;
    for (int k = 0; k < 3; k++) s[k] += fo.back().n[k];
  }
  for (int i = 0; i < n && g.kreis; i++)
    for (int j = i + 1; j < n; j++) if (n_dot(fo[i].n, fo[j].n) < 0.5f) g.kreis = false;
  const float l = sqrtf(n_dot(s, s));
  for (int k = 0; k < 3; k++) g.n[k] = l > 0 ? s[k] / l : 0;
}

inline void vorlagen_laden() {
  auto &s = z();
  if (s.gen == tk_cfg->generation()) return;
  s.gen = tk_cfg->generation();
  s.eigene.clear();
  JsonArrayConst a = tk_cfg->konfig("gesten")["eigen"];
  for (JsonObjectConst e : a) {
    if (!(e["an"] | true) || (e["f"] | 1) != 2) continue;
    Eigene g;
    g.id = e["id"] | "";
    g.name = e["n"] | "";
    for (JsonVariantConst v : e["v"].as<JsonArrayConst>()) {
      const char *h = v.as<const char *>();
      Vorlage vl;
      if (h && strlen(h) == 192 && hex_lesen(h, vl.w, PUNKTE * 3)) g.vorlagen.push_back(vl);
    }
    if (g.vorlagen.empty()) continue;
    gruppe(g);
    ESP_LOGI("geste", "Geste %s: %u Proben, Grenze %.1f%s", g.name.c_str(), (unsigned) g.vorlagen.size(), g.grenze,
             g.kreis ? ", Kreis" : "");
    s.eigene.push_back(std::move(g));
  }
}

// Bewertung einer Bewegung gegen die eigenen Gesten: Wert = Abstand / Grenze (<= 1 zaehlt).
// Kreis-Gesten: ein Kreis in derselben Ebene/Drehrichtung zaehlt immer (Wert 0,5), solange er nicht ganz anders verlaeuft.
struct Treffer { int beste{-1}; float w1{1e9f}, w2{1e9f}, d1{1e9f}; };
inline Treffer vergleichen(const int8_t *w, const std::string &ohne = "") {
  auto &s = z();
  Treffer t;
  static Gedreht gd;
  drehen(w, gd);
  const Form fw = form(w);
  for (size_t gi = 0; gi < s.eigene.size(); gi++) {
    const auto &g = s.eigene[gi];
    if (!ohne.empty() && g.id == ohne) continue;
    float d = 1e9f;
    for (const auto &v : g.vorlagen) d = std::min(d, abstand(gd, v.w));
    float wert = d / g.grenze;
    if (g.kreis && fw.kreis && n_dot(fw.n, g.n) >= 0.6f && d < g.grenze * 2.5f) wert = std::min(wert, 0.5f);
    if (wert < t.w1) { t.w2 = t.w1; t.w1 = wert; t.d1 = d; t.beste = gi; }
    else if (wert < t.w2) t.w2 = wert;
  }
  return t;
}

inline void hinweis(const std::string &text) {
  auto &s = z();
  if (disp_off->value()) return;
  if (s.toast == nullptr) {
    s.toast = lv_label_create(lv_layer_top());
    lv_obj_set_style_bg_color(s.toast, lv_color_hex(0x1A1F27), 0);
    lv_obj_set_style_bg_opa(s.toast, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s.toast, lv_color_hex(0xF1F3F5), 0);
    lv_obj_set_style_text_font(s.toast, a15->get_lv_font(), 0);
    lv_obj_set_style_radius(s.toast, 14, 0);
    lv_obj_set_style_pad_hor(s.toast, 14, 0);
    lv_obj_set_style_pad_ver(s.toast, 7, 0);
    lv_obj_set_style_border_color(s.toast, lv_color_hex(0x3B82F6), 0);
    lv_obj_set_style_border_width(s.toast, 2, 0);
    lv_obj_align(s.toast, LV_ALIGN_TOP_MID, 0, 30);
  }
  lv_label_set_text(s.toast, text.c_str());
  lv_obj_clear_flag(s.toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s.toast);
  s.toast_bis = millis() + 1400;
}

inline void melden_ha(const std::string &name, float abstand) {
  if (!api::global_api_server || !api::global_api_server->is_connected()) return;
  char b[16];
  snprintf(b, sizeof(b), "%.1f", abstand);
  geste_erkannt_an_ha->execute(name, std::string(b));
}

inline void ausloesen(const char *art, const std::string &id, const std::string &name, float abstand) {
  auto &s = z();
  s.sperre_bis = millis() + 1200;
  ESP_LOGI("geste", "Erkannt: %s (Abstand %.1f)", name.c_str(), abstand);
  melden_ha(name, abstand);
  JsonObjectConst g = tk_cfg->konfig("gesten");
  JsonObjectConst o;
  if (!id.empty()) {
    for (JsonObjectConst e : g["eigen"].as<JsonArrayConst>())
      if (id == (e["id"] | "")) o = e;
  } else {
    o = g[art];
  }
  JsonArrayConst steps = o["s"];
  const int taste = o["k"] | 0;
  const bool hat_steps = !steps.isNull() && steps.size() > 0;
  if (!hat_steps && taste <= 0) { hinweis("Geste: " + name + " (frei)"); return; }
  hinweis("Geste: " + name);
  if (hat_steps) tk_cfg->schritte(steps, disp_off->value());
  if (taste > 0) tk_cfg->taste_kurz(taste, activity->value(), disp_off->value());
}

inline bool fest_an(const char *art) {
  JsonVariantConst g = tk_cfg->konfig("gesten")[art];
  return !g.isNull() && (g["an"] | true) && (g["s"].size() > 0 || (g["k"] | 0) > 0);
}

// Erlaubt? (Sicherheitslogik ausser Ruhe/Abstand)
inline bool erlaubt() {
  if (!gesten_an->value() || !tk_cfg->aktiv()) return false;
  if (charger_present->state || dockanz_im_dock()) return false;
  if (disp_off->value() && !gesten_dunkel->value()) return false;
  if (millis() - last_input_ms->value() < 800) return false;
  return (int32_t) (millis() - z().sperre_bis) >= 0;
}

inline void schuetteln_pruefen_und_vergleichen() {
  auto &s = z();
  const int n = s.seg.size() / 3;
  const uint32_t dauer = (uint32_t) (n * 1000 / HZ);
  // Anlernen an der Remote: Bewegung als Probe (nicht direkt nach einer Taste - das ist die Bestaetigung)
  if (s.lernen) {
    if (n >= 12 && millis() - last_input_ms->value() > 700) {
      int8_t w[PUNKTE * 3];
      abtasten(s.seg, s.sx, s.sy, s.sz, w);
      gestenseite::probe(w, dauer);
    }
    return;
  }
  if (!erlaubt() || n < 12 || n > 150) return;
  int8_t w[PUNKTE * 3];
  abtasten(s.seg, s.sx, s.sy, s.sz, w);
  // Eigene Gesten zuerst (ein Kreis hat auch Richtungswechsel - frueher wurde er manchmal "Schuetteln")
  vorlagen_laden();
  if (!s.eigene.empty()) {
    const Treffer t = vergleichen(w);
    if (t.beste >= 0) {
      const auto &g = s.eigene[t.beste];
      ESP_LOGD("geste", "Bewegung %u ms: %s Wert %.2f (Abstand %.1f, Grenze %.1f), zweite %.2f", (unsigned) dauer,
               g.name.c_str(), t.w1, t.d1, g.grenze, t.w2);
      if (t.w1 <= 1.0f) {
        if (t.w2 < 1e8f && t.w2 < t.w1 * 1.25f) {   // nicht eindeutig
          ESP_LOGI("geste", "Nicht eindeutig: %s %.2f / %.2f", g.name.c_str(), t.w1, t.w2);
          melden_ha("(unklar) " + g.name, t.d1);
          return;
        }
        ausloesen("eigen", g.id, g.name, t.d1);
        return;
      }
      // Knapp daneben: fuer das Einstellen im Konfigurator melden (Liste "Zuletzt erkannt"), aber nichts ausloesen
      if (t.w1 < 1.5f) melden_ha("(knapp) " + g.name, t.d1);
    }
  }
  // Schuetteln: hin und her auf EINER Achse (deutlich staerker als die anderen), kein Kreis
  {
    float sum[3] = {0, 0, 0};
    for (int i = 0; i < n; i++) for (int a = 0; a < 3; a++) sum[a] += fabsf(s.seg[i * 3 + a]);
    const int ax = sum[0] >= sum[1] && sum[0] >= sum[2] ? 0 : sum[1] >= sum[2] ? 1 : 2;
    const float zweite = std::max(sum[(ax + 1) % 3], sum[(ax + 2) % 3]);
    static const float TH[3] = {700, 550, 420};
    const float th = TH[std::max(0, std::min(2, (int) gesten_sens->value()))];
    int wechsel = 0, letzt = 0;
    for (int i = 0; i < n; i++) {
      const float v = s.seg[i * 3 + ax];
      const int sg = v > th ? 1 : v < -th ? -1 : 0;
      if (sg != 0 && sg != letzt) { if (letzt != 0) wechsel++; letzt = sg; }
    }
    const bool linie = sum[ax] >= zweite * 1.5f && !form(w).kreis;
    if (wechsel >= 3 && dauer <= 1500 && linie && fest_an("schuetteln"))
      ausloesen("schuetteln", "", "Zweimal schütteln", (float) wechsel);
  }
}

inline void kippen(float x, float y, float zz) {
  auto &s = z();
  // Rollwinkel um die Laengsachse (Y): Schwerkraft wandert zwischen Z (flach) und X (seitlich)
  // Fast senkrecht gehalten: Rollwinkel unbestimmt (springt) - dann gar nicht werten
  if (sqrtf(x * x + zz * zz) < 500.0f) { s.kipp_cnt = 0; s.roll_ok = false; return; }
  const float roll = atan2f(x, zz) * 57.2958f;
  if (s.in_bewegung || s.lernen || !erlaubt()) { s.kipp_cnt = 0; return; }
  if (!s.roll_ok) { s.roll0 = roll; s.roll_ok = true; return; }
  const float d = roll - s.roll0;
  if (s.kipp_gesperrt) {   // erst wieder scharf, wenn die Remote zurueck ist
    if (fabsf(d) < 20) s.kipp_gesperrt = false;
    return;
  }
  if (fabsf(d) < 50) {
    s.kipp_cnt = 0;
    s.roll0 += (roll - s.roll0) * 0.02f;   // Ausgangslage folgt langsam (anders Halten)
    return;
  }
  const int r = d > 0 ? 1 : -1;
  if (r != s.kipp_richtung) { s.kipp_richtung = r; s.kipp_cnt = 0; }
  if (++s.kipp_cnt < (int) (0.5f * HZ)) return;   // 0,5 s gehalten
  s.kipp_cnt = 0;
  s.kipp_gesperrt = true;
  const char *art = r > 0 ? "kipp_r" : "kipp_l";
  if (fest_an(art)) ausloesen(art, "", r > 0 ? "Kippen rechts" : "Kippen links", fabsf(d));
}

// Eine Messung (50 Hz, mg)
inline void messung(float x, float y, float zz) {
  auto &s = z();
  auto &l = letzte_lage();
  l.x = x; l.y = y; l.z = zz; l.ms = millis();
  if (!s.g_ok) { s.gx = s.fx = x; s.gy = s.fy = y; s.gz = s.fz = zz; s.g_ok = true; }
  // Unruhe: Abweichung vom kurzen Mittel - klein, sobald die Remote stillhaelt, egal in welcher Lage
  s.fx += (x - s.fx) * 0.25f; s.fy += (y - s.fy) * 0.25f; s.fz += (zz - s.fz) * 0.25f;
  const float ux = x - s.fx, uy = y - s.fy, uz = zz - s.fz;
  const bool still = sqrtf(ux * ux + uy * uy + uz * uz) < 45.0f;
  if (still) { s.gx += (x - s.gx) * 0.1f; s.gy += (y - s.gy) * 0.1f; s.gz += (zz - s.gz) * 0.1f; }
  const float dx = x - s.gx, dy = y - s.gy, dz = zz - s.gz;
  const float m = sqrtf(dx * dx + dy * dy + dz * dz);
  static const float START[3] = {320, 250, 190};
  const float start = START[std::max(0, std::min(2, (int) gesten_sens->value()))];
  if (still) { if (s.ruhig < 1000) s.ruhig++; } else s.ruhig = 0;
  if (!s.in_bewegung) {
    if (s.ruhig >= (int) (0.3f * HZ)) s.ruhe_ms = millis();
    if (m > start && millis() - s.ruhe_ms < 400) {   // deutlicher Ausschlag kurz nach Ruhe
      s.in_bewegung = true;
      s.seg.clear();
      s.sx = s.gx; s.sy = s.gy; s.sz = s.gz;
    } else {
      kippen(s.gx, s.gy, s.gz);
      return;
    }
  }
  s.seg.push_back(x - s.sx); s.seg.push_back(y - s.sy); s.seg.push_back(zz - s.sz);
  const bool ende = s.ruhig >= (int) (0.24f * HZ);
  if (ende || s.seg.size() / 3 >= 160) {
    const int weg = std::min<int>(s.ruhig, s.seg.size() / 3);   // Ruhe am Ende abschneiden
    s.seg.resize(s.seg.size() - weg * 3);
    s.in_bewegung = false;
    if (ende) schuetteln_pruefen_und_vergleichen();
    s.seg.clear();
  }
}

// Alle 100 ms aus dem YAML-Intervall
inline void takt() {
  auto &s = z();
  const bool soll = s.lernen || (gesten_an->value() && tk_cfg->aktiv() && (!disp_off->value() || gesten_dunkel->value()));
  if (soll != s.fifo_an) {
    motion_chip->set_fifo(soll);
    s.fifo_an = soll;
    s.g_ok = false; s.in_bewegung = false; s.ruhig = 0; s.roll_ok = false; s.ruhe_ms = 0;
    ESP_LOGI("geste", "Bewegungsmessung %s", soll ? "an (FIFO)" : "aus");
  }
  if (s.toast && s.toast_bis && (int32_t) (millis() - s.toast_bis) >= 0) {
    lv_obj_add_flag(s.toast, LV_OBJ_FLAG_HIDDEN);
    s.toast_bis = 0;
  }
  if (!soll) return;
  int16_t b[32 * 3];
  const int n = motion_chip->fifo_lesen(b, 32);
  static int acc = 0;
  static float ax = 0, ay = 0, az = 0;
  for (int i = 0; i < n; i++) {
    ax += b[i * 3]; ay += b[i * 3 + 1]; az += b[i * 3 + 2];
    if (++acc == 4) { messung(ax / 4, ay / 4, az / 4); acc = 0; ax = ay = az = 0; }
  }
}

}  // namespace gesten
