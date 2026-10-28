#include "dock_hid.h"
#include "esphome/components/bluetooth_connection/bluetooth_connection_bluedroid.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/components/esp32_ble/ble.h"
#ifdef USE_ESP32_BLE_TRACKER
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#endif
#include <esp_attr.h>
#include <esp_heap_caps.h>

#include <esp_bt_defs.h>
#include <esp_bt_main.h>
#include <esp_gatt_common_api.h>
#include <esp_coexist.h>
#include <mbedtls/aes.h>
#include <cinttypes>

namespace esphome {
namespace dock_hid {

static const char *const TAG = "dock_hid";
static const uint16_t APP_ID = 0x4F52;
static const uint16_t APP_ID_C = 0x4F53;   // GATT-Client nur fuer die MTU (Sprachdienst)
static const uint32_t KOPPEL_MS = 120000;
static const uint32_t TIPP_MS = 30;          // Druck bis Loslassen bei einem Tipp
static const uint32_t FREMD_MS = 15000;      // unbekannte Verbindung ohne Kopplung: dann trennen

DockHid *global_dock_hid = nullptr;

// Kopplung ueber einen Neustart retten (RTC-Speicher bleibt bei Absturz/Watchdog erhalten): das Dock startete beim
// Koppeln mehrfach neu (Watchdog im Bluetooth-Controller) - danach war der Kopplungsmodus weg.
struct KoppelRtc { uint32_t magic; int8_t platz; uint8_t opt; uint32_t rest_ms; char name[21]; };
static RTC_NOINIT_ATTR KoppelRtc s_koppel_rtc;
static const uint32_t KOPPEL_MAGIC = 0x4B4F5050u;

// ------------------------------------------------------------------ Geraetebeschreibung
// Wie die Remote (espidf_ble_keyboard): 1 Tastatur, 2 Medien (Consumer), 3 System, 6 Tasten (Button-Seite,
// Sony-Zahnrad), 4 Maus relativ, 5 Maus absolut. Je Platz ohne Maus wird ab "Maus" abgeschnitten, ohne "voll" ist
// das Tasten-Array der Tastatur auf 0x1E..0x2C (Ziffern, Enter, Esc, Backspace, Tab, Leertaste) begrenzt.
static const uint8_t KARTE[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x73, 0x05, 0x07, 0x19, 0x00, 0x29, 0x73, 0x81, 0x00,
    0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x03, 0x19, 0x00, 0x2A, 0xFF, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0,
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x19, 0x00, 0x29, 0xFF,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x00, 0xC0,
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06, 0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0xC0,
    // ab hier Maus (MAUS_AB)
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x04, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x75, 0x05, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03, 0x81, 0x06,
    0xC0, 0xC0,
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x05, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02,
    0x75, 0x05, 0x95, 0x01, 0x81, 0x01,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x00, 0x00, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02, 0x81, 0x02,
    0xC0, 0xC0,
};

static size_t maus_ab() {
  static const uint8_t muster[] = {0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x04};
  for (size_t i = 0; i + sizeof(muster) <= sizeof(KARTE); i++)
    if (memcmp(KARTE + i, muster, sizeof(muster)) == 0) return i;
  return sizeof(KARTE);
}

size_t DockHid::karte_(uint8_t optionen, uint8_t *out) const {
  const size_t n = (optionen & OPT_MAUS) ? sizeof(KARTE) : maus_ab();
  memcpy(out, KARTE, n);
  if (!(optionen & OPT_VOLL)) {
    for (size_t i = 0; i + 15 < n; i++) {
      if (out[i] == 0x95 && out[i + 1] == 0x06 && out[i + 2] == 0x75 && out[i + 3] == 0x08 && out[i + 4] == 0x15 &&
          out[i + 8] == 0x05 && out[i + 9] == 0x07) {
        out[i + 5] = 0x1E; out[i + 7] = 0x2C; out[i + 11] = 0x1E; out[i + 13] = 0x2C;
        break;
      }
    }
  }
  return n;
}

// ------------------------------------------------------------------ GATT-Tabellen
#define PERM_R ESP_GATT_PERM_READ
#define PERM_RW (ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE)
#define PERM_R_ENC ESP_GATT_PERM_READ_ENCRYPTED
#define PERM_W_ENC ESP_GATT_PERM_WRITE_ENCRYPTED
#define PERM_RW_ENC (ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED)

static const uint16_t U_PRI = ESP_GATT_UUID_PRI_SERVICE, U_CHAR = ESP_GATT_UUID_CHAR_DECLARE,
                      U_CCC = ESP_GATT_UUID_CHAR_CLIENT_CONFIG, U_REF = ESP_GATT_UUID_RPT_REF_DESCR;
static const uint16_t U_DIS = 0x180A, U_PNP = 0x2A50, U_MFR = 0x2A29, U_BAS = 0x180F, U_BAT = 0x2A19;
static const uint16_t U_HID = ESP_GATT_UUID_HID_SVC, U_INFO = ESP_GATT_UUID_HID_INFORMATION,
                      U_MAP = ESP_GATT_UUID_HID_REPORT_MAP, U_CTRL = ESP_GATT_UUID_HID_CONTROL_POINT,
                      U_PROTO = ESP_GATT_UUID_HID_PROTO_MODE, U_REPORT = ESP_GATT_UUID_HID_REPORT;
static const uint8_t P_R = ESP_GATT_CHAR_PROP_BIT_READ, P_WNR = ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                     P_RWNR = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                     P_RW = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR,
                     P_RN = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

// PnP-ID wie die Remote (Google-Vendor-ID - Android TV behandelt das Geraet dann wie eine Fernbedienung)
static uint8_t pnp_val[7] = {0x01, 0xE0, 0x00, 0xB2, 0xA1, 0x00, 0x01};
static const char mfr_val[] = "OpenRemote";
enum { D_SVC, D_PNP_C, D_PNP, D_MFR_C, D_MFR, D_NB };
static const esp_gatts_attr_db_t dis_db[D_NB] = {
    [D_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_PRI, PERM_R, 2, 2, (uint8_t *) &U_DIS}},
    [D_PNP_C] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CHAR, PERM_R, 1, 1, (uint8_t *) &P_R}},
    [D_PNP] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_PNP, PERM_R, 7, 7, pnp_val}},
    [D_MFR_C] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CHAR, PERM_R, 1, 1, (uint8_t *) &P_R}},
    [D_MFR] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_MFR, PERM_R, sizeof(mfr_val) - 1, sizeof(mfr_val) - 1, (uint8_t *) mfr_val}},
};

static uint8_t bat_val = 100;
static uint16_t bat_ccc = 0;
enum { B_SVC, B_BAT_C, B_BAT, B_CCC, B_NB };
static const esp_gatts_attr_db_t bas_db[B_NB] = {
    [B_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_PRI, PERM_R, 2, 2, (uint8_t *) &U_BAS}},
    [B_BAT_C] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CHAR, PERM_R, 1, 1, (uint8_t *) &P_RN}},
    [B_BAT] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_BAT, PERM_R, 1, 1, &bat_val}},
    [B_CCC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CCC, PERM_RW, 2, 2, (uint8_t *) &bat_ccc}},
};

static uint8_t info_val[4] = {0x11, 0x01, 0x00, 0x03};
static uint8_t ctrl_val = 0, proto_val = 0x01;
static uint8_t kb_val[8] = {0}, kb_out_val[1] = {0}, cons_val[2] = {0}, sys_val = 0, knopf_val = 0;
static uint8_t maus_val[4] = {0}, abs_val[5] = {0};
static uint16_t kb_ccc = 0, cons_ccc = 0, sys_ccc = 0, knopf_ccc = 0, maus_ccc = 0, abs_ccc = 0;
static uint8_t kb_ref[2] = {0x01, 0x01}, kb_out_ref[2] = {0x01, 0x02}, cons_ref[2] = {0x02, 0x01},
               sys_ref[2] = {0x03, 0x01}, knopf_ref[2] = {0x06, 0x01}, maus_ref[2] = {0x04, 0x01}, abs_ref[2] = {0x05, 0x01};
static uint8_t map_platzhalter[1] = {0};

enum {
  H_SVC,
  H_INFO_C, H_INFO,
  H_MAP_C, H_MAP,
  H_CTRL_C, H_CTRL,
  H_PROTO_C, H_PROTO,
  H_KB_C, H_KB, H_KB_CCC, H_KB_REF,
  H_KBO_C, H_KBO, H_KBO_REF,
  H_CONS_C, H_CONS, H_CONS_CCC, H_CONS_REF,
  H_SYS_C, H_SYS, H_SYS_CCC, H_SYS_REF,
  H_KNOPF_C, H_KNOPF, H_KNOPF_CCC, H_KNOPF_REF,
  H_MAUS_C, H_MAUS, H_MAUS_CCC, H_MAUS_REF,
  H_ABS_C, H_ABS, H_ABS_CCC, H_ABS_REF,
  H_NB,
};
#define CHR(p) {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CHAR, PERM_R, 1, 1, (uint8_t *) &(p)}}
#define VAL(u, perm, v) {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &(u), perm, sizeof(v), sizeof(v), (uint8_t *) &(v)}}
static const esp_gatts_attr_db_t hid_db[H_NB] = {
    [H_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_PRI, PERM_R, 2, 2, (uint8_t *) &U_HID}},
    [H_INFO_C] = CHR(P_R), [H_INFO] = VAL(U_INFO, PERM_R_ENC, info_val),
    // Geraetebeschreibung: Antwort je Verbindung (ESP_GATT_RSP_BY_APP), s. gatts_ READ
    [H_MAP_C] = CHR(P_R),
    [H_MAP] = {{ESP_GATT_RSP_BY_APP}, {ESP_UUID_LEN_16, (uint8_t *) &U_MAP, PERM_R_ENC, sizeof(KARTE), 1, map_platzhalter}},
    [H_CTRL_C] = CHR(P_WNR), [H_CTRL] = VAL(U_CTRL, PERM_W_ENC, ctrl_val),
    [H_PROTO_C] = CHR(P_RWNR), [H_PROTO] = VAL(U_PROTO, PERM_RW_ENC, proto_val),
    [H_KB_C] = CHR(P_RN), [H_KB] = VAL(U_REPORT, PERM_R_ENC, kb_val), [H_KB_CCC] = VAL(U_CCC, PERM_RW_ENC, kb_ccc),
    [H_KB_REF] = VAL(U_REF, PERM_R_ENC, kb_ref),
    [H_KBO_C] = CHR(P_RW), [H_KBO] = VAL(U_REPORT, PERM_RW_ENC, kb_out_val), [H_KBO_REF] = VAL(U_REF, PERM_R_ENC, kb_out_ref),
    [H_CONS_C] = CHR(P_RN), [H_CONS] = VAL(U_REPORT, PERM_R_ENC, cons_val), [H_CONS_CCC] = VAL(U_CCC, PERM_RW_ENC, cons_ccc),
    [H_CONS_REF] = VAL(U_REF, PERM_R_ENC, cons_ref),
    [H_SYS_C] = CHR(P_RN), [H_SYS] = VAL(U_REPORT, PERM_R_ENC, sys_val), [H_SYS_CCC] = VAL(U_CCC, PERM_RW_ENC, sys_ccc),
    [H_SYS_REF] = VAL(U_REF, PERM_R_ENC, sys_ref),
    [H_KNOPF_C] = CHR(P_RN), [H_KNOPF] = VAL(U_REPORT, PERM_R_ENC, knopf_val),
    [H_KNOPF_CCC] = VAL(U_CCC, PERM_RW_ENC, knopf_ccc), [H_KNOPF_REF] = VAL(U_REF, PERM_R_ENC, knopf_ref),
    [H_MAUS_C] = CHR(P_RN), [H_MAUS] = VAL(U_REPORT, PERM_R_ENC, maus_val), [H_MAUS_CCC] = VAL(U_CCC, PERM_RW_ENC, maus_ccc),
    [H_MAUS_REF] = VAL(U_REF, PERM_R_ENC, maus_ref),
    [H_ABS_C] = CHR(P_RN), [H_ABS] = VAL(U_REPORT, PERM_R_ENC, abs_val), [H_ABS_CCC] = VAL(U_CCC, PERM_RW_ENC, abs_ccc),
    [H_ABS_REF] = VAL(U_REF, PERM_R_ENC, abs_ref),
};
static uint16_t h_tab[H_NB];
enum { SVC_DIS = 0, SVC_BAS = 1, SVC_HID = 2, SVC_ATVV = 3 };

// Sprachdienst (Android TV Voice over BLE, 2026-10-24) - gleiche Tabelle wie components/atv_voice auf der Remote
static const uint8_t ATVV_SVC_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC, 0x05, 0x4F, 0x21, 0x5A, 0x01, 0x00, 0x5E, 0xAB};
static const uint8_t ATVV_TX_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC, 0x05, 0x4F, 0x21, 0x5A, 0x02, 0x00, 0x5E, 0xAB};
static const uint8_t ATVV_RX_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC, 0x05, 0x4F, 0x21, 0x5A, 0x03, 0x00, 0x5E, 0xAB};
static const uint8_t ATVV_CTL_UUID[16] = {0x64, 0xB6, 0x17, 0xF6, 0x01, 0xAF, 0x7D, 0xBC, 0x05, 0x4F, 0x21, 0x5A, 0x04, 0x00, 0x5E, 0xAB};
enum { A_SVC, A_TX_C, A_TX, A_RX_C, A_RX, A_RX_CCC, A_CTL_C, A_CTL, A_CTL_CCC, A_NB };
static const uint8_t P_ATVV_W = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t P_ATVV_N = ESP_GATT_CHAR_PROP_BIT_NOTIFY | ESP_GATT_CHAR_PROP_BIT_READ;
static uint8_t atvv_tx[32] = {0}, atvv_rx[1] = {0}, atvv_ctl[1] = {0}, atvv_rx_ccc[2] = {0}, atvv_ctl_ccc[2] = {0};
static const esp_gatts_attr_db_t atvv_db[A_NB] = {
    [A_SVC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_PRI, ESP_GATT_PERM_READ, 16, 16, (uint8_t *) ATVV_SVC_UUID}},
    [A_TX_C] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CHAR, ESP_GATT_PERM_READ, 1, 1, (uint8_t *) &P_ATVV_W}},
    [A_TX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) ATVV_TX_UUID, ESP_GATT_PERM_WRITE, sizeof(atvv_tx), 0, atvv_tx}},
    [A_RX_C] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CHAR, ESP_GATT_PERM_READ, 1, 1, (uint8_t *) &P_ATVV_N}},
    [A_RX] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) ATVV_RX_UUID, ESP_GATT_PERM_READ, 1, 1, atvv_rx}},
    [A_RX_CCC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CCC, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, atvv_rx_ccc}},
    [A_CTL_C] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CHAR, ESP_GATT_PERM_READ, 1, 1, (uint8_t *) &P_ATVV_N}},
    [A_CTL] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_128, (uint8_t *) ATVV_CTL_UUID, ESP_GATT_PERM_READ, 1, 1, atvv_ctl}},
    [A_CTL_CCC] = {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t *) &U_CCC, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, 2, 2, atvv_ctl_ccc}},
};
static uint16_t a_tab[A_NB];
static int a_status = -1;   // Tabelle des Sprachdienstes: -1 nicht angelegt, 0 ok, sonst Fehler (Diagnose, dump_config)
static const uint8_t ATVV_GET_CAPS = 0x0A, ATVV_MIC_OPEN = 0x0C, ATVV_MIC_CLOSE = 0x0D, ATVV_CAPS_RESP = 0x0B,
                     ATVV_AUDIO_STOP = 0x00;
static const size_t SPRACHE_RING = 6144;   // Ringpuffer zum Fernseher (~0,7 s Audio)

// Gescheiterte Speicheranforderungen zaehlen (2026-10-25) - kommt aus jedem Task, deshalb nur zaehlen, loop() meldet
static std::atomic<uint32_t> s_alloc_fehl{0}, s_alloc_groesse{0};
static void alloc_fehlgeschlagen(size_t groesse, uint32_t caps, const char *funktion) {
  s_alloc_fehl++;
  s_alloc_groesse = (uint32_t) groesse;
}

static void gatts_cb(esp_gatts_cb_event_t e, esp_gatt_if_t i, esp_ble_gatts_cb_param_t *p) {
  if (global_dock_hid != nullptr) global_dock_hid->gatts_(e, i, p);
}

static void mac_text(const uint8_t *a, char *out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X", a[0], a[1], a[2], a[3], a[4], a[5]);
}

// ------------------------------------------------------------------ Start
void DockHid::setup() {
  global_dock_hid = this;
  heap_caps_register_failed_alloc_callback(alloc_fehlgeschlagen);
  if (sprachdienst_) {
    ring_ = (uint8_t *) malloc(SPRACHE_RING);   // gleich beim Start, solange der Speicher am Stueck ist
    if (ring_ == nullptr) { ESP_LOGE(TAG, "Kein Speicher fuer den Sprachpuffer - Sprachdienst aus"); sprachdienst_ = false; }
  }
  pref_ = global_preferences->make_preference<Platz[MAX_PLAETZE]>(0x44484944u);   // "DHID"
  if (!pref_.load(&plaetze_)) memset(plaetze_, 0, sizeof(plaetze_));
  for (auto &p : plaetze_) p.name[sizeof(p.name) - 1] = 0;
  if (s_koppel_rtc.magic == KOPPEL_MAGIC && s_koppel_rtc.platz >= 0 && s_koppel_rtc.platz < anzahl_ &&
      s_koppel_rtc.rest_ms > 5000 && s_koppel_rtc.rest_ms <= KOPPEL_MS) {
    s_koppel_rtc.name[20] = 0;
    const uint32_t rest = s_koppel_rtc.rest_ms;
    koppeln(s_koppel_rtc.platz, s_koppel_rtc.name, s_koppel_rtc.opt);
    koppel_bis_ = millis() + rest;
    ESP_LOGW(TAG, "Nach Neustart: Kopplung Platz %d laeuft weiter (%u s)", koppel_platz_ + 1, (unsigned) (rest / 1000));
  } else {
    s_koppel_rtc.magic = 0;
  }
  // Gekoppelte Geraete verbinden sich gleich nach dem Start wieder: von Anfang an mit dem kurzen Fenster scannen
  // (der Tracker startet erst nach uns) - der Start war der Moment der Abstuerze (2026-10-22)
  for (auto &p : plaetze_) start_ruhe_ |= p.belegt;
  // Proxy-Scan erst nach der Startphase (YAML: continuous: false) - loop() startet ihn ueber scan_(true)
  scan_pause_ = true;
  scan_wieder_ms_ = 20000;
#ifdef USE_ESP32_BLE_TRACKER
  if (start_ruhe_ && std_fe_ > fe_verbunden_ && esp32_ble_tracker::global_esp32_ble_tracker != nullptr) {
    esp32_ble_tracker::global_esp32_ble_tracker->set_scan_window((uint32_t) (fe_verbunden_ / 0.625f));
    ist_fe_ = fe_verbunden_;
  }
#endif
}

void DockHid::scan_(bool an) {
#ifdef USE_ESP32_BLE_TRACKER
  auto *t = esp32_ble_tracker::global_esp32_ble_tracker;
  if (t == nullptr || an == !scan_pause_) return;
  scan_pause_ = !an;
  if (an) { t->set_scan_continuous(true); t->start_scan(); ESP_LOGI(TAG, "Proxy-Scan wieder an"); }
  else { t->stop_scan(); ESP_LOGI(TAG, "Proxy-Scan pausiert (Kopplung/Sprache/Wiederverbinden)"); }
#endif
}

// ------------------------------------------------------------------ Scan-Stufen
static uint32_t grenzen_(uint32_t ms, uint32_t lo, uint32_t hi) { return ms < lo ? lo : (ms > hi ? hi : ms); }

void DockHid::scan_stufe(uint32_t intervall_ms, uint32_t fenster_ms, const std::string &grund, uint32_t weck_s,
                         uint32_t weck_intervall_ms, uint32_t weck_fenster_ms, const std::string &remotes,
                         uint32_t fenster_verbunden_ms) {
  ha_iv_ = intervall_ms;
  // Konfigurator (2026-10-23): 10..50 ms, 0 = Wert aus der YAML
  fe_verbunden_ = fenster_verbunden_ms == 0 ? fe_verbunden_yaml_ : grenzen_(fenster_verbunden_ms, 10, 50);
  ha_fe_ = fenster_ms;
  ha_grund_ = grund.substr(0, 60);
  for (char &z : ha_grund_) if (z == '"' || z == '\\') z = '\'';   // landet im JSON des Status
  ha_bis_ = millis() + 180000;   // HA wiederholt jede Minute; bleibt es aus, gilt wieder der Standard
  weck_s_ = weck_s > 600 ? 600 : weck_s;
  weck_iv_ = weck_intervall_ms;
  weck_fe_ = weck_fenster_ms;
  weck_remotes_ = remotes;
  if (weck_s_ == 0) weck_bis_ = 0;
  stufe_ms_ = 0;   // gleich anwenden
}

void DockHid::weckruf(const std::string &remote) {
  if (weck_s_ == 0) return;
  if (!weck_remotes_.empty()) {   // nur Remotes, die HA mitzaehlt ("a,b,c")
    std::string l = "," + weck_remotes_ + ",";
    if (l.find("," + remote + ",") == std::string::npos) return;
  }
  weck_bis_ = (millis() + weck_s_ * 1000) | 1;
  weck_von_ = remote.substr(0, 40);
  for (char &z : weck_von_) if (z == '"' || z == '\\') z = '\'';
  stufe_ms_ = 0;
}

void DockHid::scan_stufe_pruefen_(uint32_t jetzt) {
  if (stufe_ms_ != 0 && jetzt - stufe_ms_ < 1000) return;
  stufe_ms_ = jetzt | 1;
  uint32_t iv = std_iv_, fe = std_fe_;
  std::string grund = "Standard";
  int32_t rest = weck_bis_ ? (int32_t) (weck_bis_ - jetzt) : 0;
  if (weck_bis_ && rest <= 0) weck_bis_ = 0;
  if (weck_bis_) {
    iv = weck_iv_, fe = weck_fe_;
    grund = "Weckruf " + weck_von_;
  } else if (ha_bis_ && (int32_t) (ha_bis_ - jetzt) > 0) {
    iv = ha_iv_, fe = ha_fe_;
    grund = ha_grund_;
  } else {
    ha_bis_ = 0;
  }
  // Grenzen von Bluetooth: 2,5 ms .. 10,24 s, Fenster <= Intervall
  iv = grenzen_(iv, 3, 10240);
  fe = grenzen_(fe, 3, iv);
  // Mit verbundenem Geraet hoechstens fenster_verbunden (wie ESPHome connection_scan_window): laengere Fenster
  // brachten den Controller beim Start mit dem Streamer in den Interrupt-Watchdog (2026-10-22)
  bool verbunden_jetzt = false;
  {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto &v : verb_) verbunden_jetzt |= v.aktiv;
  }
  if (start_ruhe_ && jetzt > 30000) start_ruhe_ = false;   // bis dahin sind die Geraete wieder verbunden
  if ((verbunden_jetzt || start_ruhe_) && fe > fe_verbunden_) {
    fe = fe_verbunden_;
    grund += " (Geraet verbunden)";
  }
  if (iv != ist_iv_ || fe != ist_fe_) {
#ifdef USE_ESP32_BLE_TRACKER
    auto *t = esp32_ble_tracker::global_esp32_ble_tracker;
    if (t != nullptr) {
      t->set_scan_interval((uint32_t) (iv / 0.625f));
      t->set_scan_window((uint32_t) (fe / 0.625f));
      // Sofort neu starten nur, wenn das Fenster kleiner wird (Geraet verbunden - Schutz vor dem Interrupt-Watchdog).
      // Sonst gelten die Zeiten beim naechsten Scan-Durchlauf (2026-10-27: jeder Stopp/Start ist Arbeit fuer den
      // Controller, der Watchdog-Absturz lag in seiner Scan-Planung). Pausiert (Kopplung): beim Wiederanlaufen.
      if (!scan_pause_ && fe < ist_fe_) { t->stop_scan(); t->set_scan_continuous(true); }
    }
#endif
    ESP_LOGI(TAG, "Suchtempo: %u von %u ms (%s)", (unsigned) fe, (unsigned) iv, grund.c_str());
    ist_iv_ = iv;
    ist_fe_ = fe;
  }
  if (scan_status_ != nullptr) {
    // Restzeit nur beim Wechsel melden (der Konfigurator zaehlt selbst runter) - sonst jede Sekunde ein Wert an HA
    char b[160];
    snprintf(b, sizeof(b), "{\"i\":%u,\"f\":%u,\"g\":\"%s\",\"r\":%d,\"p\":%d}", (unsigned) iv, (unsigned) fe,
             grund.c_str(), weck_bis_ ? (int) ((rest + 999) / 1000) : 0, scan_pause_ ? 1 : 0);
    std::string s(b);
    std::string schluessel = s.substr(0, s.find("\"r\"")) + (scan_pause_ ? "p" : "") + std::to_string(weck_bis_);
    if (ist_text_ != schluessel) {
      ist_text_ = schluessel;
      scan_status_->publish_state(s);
    }
  }
}

void DockHid::koppeln_ende_(uint32_t scan_nach_ms) {
  koppel_platz_ = -1;
  s_koppel_rtc.magic = 0;
  scan_wieder_ms_ = millis() + scan_nach_ms;
  geaendert_ = true;
}

void DockHid::dump_config() {
  ESP_LOGCONFIG(TAG, "Dock-Tastatur \"%s\": %u Plaetze", name_.c_str(), (unsigned) anzahl_);
  for (uint8_t i = 0; i < anzahl_; i++) {
    if (!plaetze_[i].belegt) continue;
    char a[18];
    mac_text(plaetze_[i].addr, a);
    ESP_LOGCONFIG(TAG, "  Platz %u: %s (%s%s, Maus %s, Tastatur %s)", (unsigned) i + 1, plaetze_[i].name, a,
                  plaetze_[i].hat_irk ? ", IRK" : "", (plaetze_[i].optionen & OPT_MAUS) ? "ja" : "nein",
                  (plaetze_[i].optionen & OPT_VOLL) ? "voll" : "Ziffern");
  }
  if (sprachdienst_)
    ESP_LOGCONFIG(TAG, "  Sprachdienst: %s (Handles TX %u, RX %u, CTL %u)",
                  a_status == 0 ? "angelegt" : a_status < 0 ? "NICHT angelegt" : "FEHLER", a_tab[A_TX], a_tab[A_RX], a_tab[A_CTL]);
}

void DockHid::loop() {
  const uint32_t jetzt = millis();
  // Speicher-Tiefstand mitschreiben (2026-10-25): wann und wobei er faellt (WLAN war bei 0,6 kB minutenlang weg)
  {
    static size_t tief = SIZE_MAX;
    const size_t m = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    if (m + 1024 < tief) {
      tief = m;
      ESP_LOGW(TAG, "Speicher-Tiefstand %u B (jetzt frei %u, groesster Block %u, Sprache %s, Geraete 0x%X)", (unsigned) m,
               (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), sprache_platz_ >= 0 ? "an" : "aus",
               (unsigned) maske());
    }
    // hoechstens alle 10 s: jede Logzeile kostet selbst Speicher (Sendepuffer je API-Zuhoerer wuchs in 143-B-Schritten)
    static uint32_t fehl_gemeldet = 0, fehl_ms = 0;
    const uint32_t f = s_alloc_fehl;
    if (f != fehl_gemeldet && (fehl_ms == 0 || jetzt - fehl_ms >= 10000)) {
      fehl_ms = jetzt;
      ESP_LOGE(TAG, "%u Speicheranforderung(en) gescheitert, zuletzt %u B (frei %u, groesster Block %u)",
               (unsigned) (f - fehl_gemeldet), (unsigned) s_alloc_groesse.load(),
               (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned) heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
      fehl_gemeldet = f;
    }
  }
  // Proxy-Verbindungen mit Dock-Takt aufbauen, solange ein Geraet verbunden ist (bluetooth_connection, 2026-10-23)
  bluetooth_connection::g_peripherie_verbunden = maske() != 0;
  if (!gestartet_) {
    if (esp32_ble::global_ble == nullptr || !esp32_ble::global_ble->is_active()) return;
    gestartet_ = true;
    // Kopplung ohne Eingabe ("Just Works"), mit Bindung, Secure Connections; Schluessel fuer beide Seiten.
    esp_ble_auth_req_t auth = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t io = ESP_IO_CAP_NONE;
    uint8_t ks = 16, ik = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK, rk = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &ks, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &ik, 1);
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rk, 1);
    esp_err_t e = esp_ble_gatts_register_callback(gatts_cb);
    if (e == ESP_OK) e = esp_ble_gatts_app_register(APP_ID);
    if (e == ESP_OK && sprachdienst_) {
      esp_ble_gatt_set_local_mtu(247);   // wie die Remote (dort 517) - sonst bleibt jede Aushandlung bei 23
      esp_ble_gattc_app_register(APP_ID_C);
    }
    if (e != ESP_OK) {
      ESP_LOGE(TAG, "GATT-Server nicht registriert: %s", esp_err_to_name(e));
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "Bluetooth bereit - Tastatur fuer %u Geraete", (unsigned) anzahl_);
  }

  // Verzoegerte Ereignisse (Loslassen nach Tipp, Tipps in Folge)
  while (!spaeter_.empty() && (int32_t) (jetzt - spaeter_.front().ms) >= 0) {
    const Spaeter s = spaeter_.front();
    spaeter_.pop_front();
    wirkt_(s.p, s.e, s.e.aktion != LOSLASSEN);
  }
  // Gehaltene Tasten ohne Lebenszeichen der Remote loslassen (Paket verloren, Remote eingeschlafen)
  for (auto &g : gehalten_) {
    if (g.an && (int32_t) (jetzt - g.bis_ms) >= 0) {
      g.an = false;
      Ereignis e{g.art, LOSLASSEN, {0, 0, 0, 0}};
      wirkt_(g.p, e, false);
      ESP_LOGW(TAG, "Platz %u: gehaltene Taste ohne Lebenszeichen - losgelassen", (unsigned) g.p + 1);
    }
  }

  // Unbekannte Verbindungen (nicht gekoppelt, keine Kopplung offen) trennen
  {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto &v : verb_) {
      if (v.aktiv && v.platz < 0 && koppel_platz_ < 0 && jetzt - v.seit_ms > FREMD_MS) {
        char a[18];
        mac_text(v.bda, a);
        ESP_LOGI(TAG, "%s: nicht gekoppelt - getrennt", a);
        esp_ble_gap_disconnect(v.bda);
        v.seit_ms = jetzt;   // nicht jede Runde
      }
    }
  }

  if (koppel_platz_ >= 0 && (int32_t) (jetzt - koppel_bis_) >= 0) {
    ESP_LOGI(TAG, "Kopplung Platz %d: Zeit abgelaufen", koppel_platz_ + 1);
    koppeln_ende_(1000);
  }
  if (koppel_platz_ >= 0) {
    scan_(false);
    if (jetzt - rtc_ms_ > 1000) { rtc_ms_ = jetzt; s_koppel_rtc.rest_ms = koppel_bis_ - jetzt; }
  } else if (wieder_bis_ != 0 && (int32_t) (jetzt - wieder_bis_) < 0) {
    scan_(false);
  } else if (scan_pause_ && (int32_t) (jetzt - scan_wieder_ms_) >= 0) {
    wieder_bis_ = 0;
    scan_(true);
  }
  if (sprachdienst_) sprache_loop_(jetzt);
  scan_stufe_pruefen_(jetzt);
  werbung_();
  if ((geaendert_ && jetzt - melden_ms_ > 200) || (koppel_platz_ >= 0 && jetzt - melden_ms_ > 5000)) melden_();
}

// ------------------------------------------------------------------ Plaetze
bool DockHid::aufloesen_(const uint8_t bda[6], const uint8_t irk[16]) const {
  if ((bda[0] & 0xC0) != 0x40) return false;   // keine aufloesbare Zufallsadresse
  // ah(k, r) = e(k, 0^104 || prand) mod 2^24 - Bluedroid legt die IRK je nach Version in Funk-Reihenfolge ab: beide pruefen
  for (int dreh = 0; dreh < 2; dreh++) {
    uint8_t k[16], in[16] = {0}, out[16];
    for (int i = 0; i < 16; i++) k[i] = dreh ? irk[15 - i] : irk[i];
    in[13] = bda[0]; in[14] = bda[1]; in[15] = bda[2];
    mbedtls_aes_context c;
    mbedtls_aes_init(&c);
    mbedtls_aes_setkey_enc(&c, k, 128);
    mbedtls_aes_crypt_ecb(&c, MBEDTLS_AES_ENCRYPT, in, out);
    mbedtls_aes_free(&c);
    if (out[13] == bda[3] && out[14] == bda[4] && out[15] == bda[5]) return true;
  }
  return false;
}

int DockHid::platz_von_(const uint8_t bda[6]) const {
  for (int i = 0; i < anzahl_; i++) {
    const Platz &p = plaetze_[i];
    if (!p.belegt) continue;
    if (memcmp(p.addr, bda, 6) == 0) return i;
    if (p.hat_irk && aufloesen_(bda, p.irk)) return i;
  }
  return -1;
}

void DockHid::speichern_() {
  pref_.save(&plaetze_);
  global_preferences->sync();
  geaendert_ = true;
}

void DockHid::neu_gekoppelt_(Verb &v, const uint8_t bda[6]) {
  const int p = koppel_platz_;
  Platz neu{};
  neu.belegt = 1;
  neu.optionen = koppel_opt_;
  memcpy(neu.addr, bda, 6);
  strncpy(neu.name, koppel_name_.c_str(), sizeof(neu.name) - 1);
  // Identitaetsadresse und IRK aus der Bindung (Tablets/Laptops verbinden sich mit wechselnden Adressen)
  int n = esp_ble_get_bond_device_num();
  if (n > 0) {
    std::vector<esp_ble_bond_dev_t> liste(n);
    esp_ble_get_bond_device_list(&n, liste.data());
    for (int i = 0; i < n; i++) {
      const auto &b = liste[i];
      const bool id = b.bond_key.key_mask & ESP_LE_KEY_PID;
      if (memcmp(b.bd_addr, bda, 6) == 0 || (id && aufloesen_(bda, b.bond_key.pid_key.irk)) ||
          (id && memcmp(b.bond_key.pid_key.static_addr, bda, 6) == 0)) {
        memcpy(neu.addr, b.bd_addr, 6);
        if (id) { memcpy(neu.irk, b.bond_key.pid_key.irk, 16); neu.hat_irk = 1; }
        break;
      }
    }
  }
  // Gleiches Geraet auf einem anderen Platz? Dort austragen. Altes Geraet auf diesem Platz vergessen.
  for (int i = 0; i < anzahl_; i++) {
    if (i != p && plaetze_[i].belegt && memcmp(plaetze_[i].addr, neu.addr, 6) == 0) plaetze_[i].belegt = 0;
  }
  if (plaetze_[p].belegt && memcmp(plaetze_[p].addr, neu.addr, 6) != 0) esp_ble_remove_bond_device(plaetze_[p].addr);
  plaetze_[p] = neu;
  v.platz = p;
  koppeln_ende_(20000);   // Host liest danach Dienste und Beschreibung - Funk noch frei halten
  speichern_();
  char a[18];
  mac_text(neu.addr, a);
  ESP_LOGI(TAG, "Platz %d gekoppelt: %s (%s%s)", p + 1, neu.name, a, neu.hat_irk ? ", wechselnde Adresse" : "");
}

void DockHid::koppeln(int p, const std::string &name, int optionen) {
  if (p < 0 || p >= anzahl_) { ESP_LOGW(TAG, "Platz %d gibt es nicht (1..%u)", p + 1, (unsigned) anzahl_); return; }
  koppel_platz_ = p;
  koppel_bis_ = millis() + KOPPEL_MS;
  koppel_name_ = name.empty() ? std::string("Geraet ") + std::to_string(p + 1) : name.substr(0, 20);
  koppel_opt_ = optionen & (OPT_MAUS | OPT_VOLL);
  geaendert_ = true;
  s_koppel_rtc.magic = KOPPEL_MAGIC;
  s_koppel_rtc.platz = p;
  s_koppel_rtc.opt = koppel_opt_;
  s_koppel_rtc.rest_ms = KOPPEL_MS;
  memset(s_koppel_rtc.name, 0, sizeof(s_koppel_rtc.name));
  strncpy(s_koppel_rtc.name, koppel_name_.c_str(), 20);
  if (gestartet_) scan_(false);
  ESP_LOGI(TAG, "Kopplung Platz %d (%s) - Dock ist 2 Minuten als \"%s\" sichtbar", p + 1, koppel_name_.c_str(), name_.c_str());
}

void DockHid::koppeln_abbrechen() { koppeln_ende_(1000); }

void DockHid::vergessen(int p) {
  if (p < 0 || p >= anzahl_ || !plaetze_[p].belegt) return;
  uint8_t a[6];
  memcpy(a, plaetze_[p].addr, 6);
  {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto &v : verb_) if (v.aktiv && v.platz == p) { esp_ble_gap_disconnect(v.bda); v.platz = -1; }
  }
  esp_ble_remove_bond_device(a);
  plaetze_[p] = Platz{};
  speichern_();
  ESP_LOGI(TAG, "Platz %d vergessen", p + 1);
}

void DockHid::trennen(int p, uint32_t s) {
  if (p < 0 || p >= anzahl_) return;
  pause_bis_ = (millis() + s * 1000) | 1;
  {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto &v : verb_) if (v.aktiv && v.platz == p) esp_ble_gap_disconnect(v.bda);
  }
  geaendert_ = true;
  ESP_LOGI(TAG, "Platz %d getrennt, %u s keine Werbung", p + 1, (unsigned) s);
}

void DockHid::optionen(int p, const std::string &name, int optionen) {
  if (p < 0 || p >= anzahl_ || !plaetze_[p].belegt) return;
  if (!name.empty()) {
    memset(plaetze_[p].name, 0, sizeof(plaetze_[p].name));
    strncpy(plaetze_[p].name, name.c_str(), sizeof(plaetze_[p].name) - 1);
  }
  if (optionen >= 0 && (optionen & 3) != plaetze_[p].optionen) {
    plaetze_[p].optionen = optionen & 3;
    // Der Host merkt sich die Geraetebeschreibung pro Kopplung - wirkt erst nach neuem Koppeln
    ESP_LOGW(TAG, "Platz %d: Maus/Tastatur geaendert - am Geraet neu koppeln, damit es wirkt", p + 1);
  }
  speichern_();
}

// ------------------------------------------------------------------ Werbung
void DockHid::werbung_() {
  uint8_t soll = 0;
  if (koppel_platz_ >= 0) {
    soll = 2;
  } else {
    uint8_t frei = 0;
    for (int i = 0; i < anzahl_; i++) if (plaetze_[i].belegt && !verbunden(i)) frei++;
    if (frei) soll = 1;
    if (pause_bis_ != 0 && (int32_t) (millis() - pause_bis_) < 0) soll = 0;
  }
  werbung_soll_ = soll;
  if (werbung_busy_) {
    if (millis() - werbung_ms_ < 2000) return;
    werbung_busy_ = false;   // Rueckmeldung verloren
  }
  if (werbung_ist_ == werbung_soll_) return;
  werbung_busy_ = true;
  werbung_ms_ = millis();
  if (werbung_ist_ != 0) {
    esp_ble_gap_stop_advertising();
    return;
  }
  // Werbedaten: Flags, Erscheinung "Fernbedienung", HID-Dienst, Name
  uint8_t d[31];
  size_t n = 0;
  d[n++] = 2; d[n++] = 0x01; d[n++] = soll == 2 ? 0x06 : 0x04;   // bei Kopplung auffindbar
  d[n++] = 3; d[n++] = 0x19; d[n++] = 0x80; d[n++] = 0x01;
  d[n++] = 3; d[n++] = 0x03; d[n++] = 0x12; d[n++] = 0x18;
  size_t l = name_.size();
  if (l > sizeof(d) - n - 2) l = sizeof(d) - n - 2;
  d[n++] = l + 1; d[n++] = 0x09;
  memcpy(d + n, name_.data(), l);
  n += l;
  esp_ble_gap_config_adv_data_raw(d, n);
}

void DockHid::gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT: {
      const auto &u = param->update_conn_params;
      if (platz_von_(u.bda) >= 0)
        ESP_LOGI(TAG, "Takt mit Platz %d: %.2f ms, Latenz %u (Status %d)", platz_von_(u.bda) + 1, u.conn_int * 1.25f,
                 (unsigned) u.latency, (int) u.status);
      break;
    }
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT: {
      if (!werbung_busy_ || werbung_soll_ == 0) { werbung_busy_ = false; break; }
      esp_ble_adv_params_t a = {};
      a.adv_int_min = werbung_soll_ == 2 ? 0x30 : 0xA0;   // 30 ms beim Koppeln, sonst 100 ms
      a.adv_int_max = werbung_soll_ == 2 ? 0x50 : 0xF0;
      a.adv_type = ADV_TYPE_IND;
      a.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
      a.channel_map = ADV_CHNL_ALL;
      a.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
      werbung_ist_ = werbung_soll_;
      esp_ble_gap_start_advertising(&a);
      break;
    }
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
      werbung_busy_ = false;
      if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
        ESP_LOGW(TAG, "Werbung startet nicht (%d)", param->adv_start_cmpl.status);
        werbung_ist_ = 0;
      }
      break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
      werbung_busy_ = false;
      werbung_ist_ = 0;
      break;
    case ESP_GAP_BLE_SEC_REQ_EVT: {
      const uint8_t *bda = param->ble_security.ble_req.bd_addr;
      bool unsere = false;
      {
        std::lock_guard<std::mutex> l(mtx_);
        for (auto &v : verb_) if (v.aktiv && memcmp(v.bda, bda, 6) == 0) unsere = true;
      }
      if (!unsere) break;   // Proxy-Verbindung: macht esp32_ble_client
      const bool ok = koppel_platz_ >= 0 || platz_von_(bda) >= 0;
      char a[18];
      mac_text(bda, a);
      ESP_LOGI(TAG, "%s will koppeln - %s", a, ok ? "ja" : "abgelehnt (keine Kopplung offen)");
      esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, ok);
      break;
    }
    case ESP_GAP_BLE_AUTH_CMPL_EVT: {
      const auto &ac = param->ble_security.auth_cmpl;
      std::lock_guard<std::mutex> l(mtx_);
      Verb *v = nullptr;
      for (auto &x : verb_) if (x.aktiv && memcmp(x.bda, ac.bd_addr, 6) == 0) v = &x;
      if (v == nullptr) break;   // Proxy
      char a[18];
      mac_text(ac.bd_addr, a);
      if (!ac.success) {
        ESP_LOGW(TAG, "%s: Kopplung/Verschluesselung fehlgeschlagen (0x%x)", a, ac.fail_reason);
        break;
      }
      v->sicher = true;
      int p = platz_von_(ac.bd_addr);
      if (p < 0 && koppel_platz_ >= 0) {
        neu_gekoppelt_(*v, ac.bd_addr);
      } else if (p >= 0) {
        v->platz = p;
        ESP_LOGI(TAG, "Platz %d verbunden: %s", p + 1, plaetze_[p].name);
      } else {
        ESP_LOGI(TAG, "%s: alte Bindung ohne Platz - vergessen und getrennt", a);
        esp_ble_remove_bond_device(v->bda);
        esp_ble_gap_disconnect(v->bda);
      }
      geaendert_ = true;
      break;
    }
    default:
      break;
  }
}

// ------------------------------------------------------------------ GATT-Server (Bluetooth-Task)
void DockHid::gatts_(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param) {
  if (event == ESP_GATTS_REG_EVT) {
    if (param->reg.app_id != APP_ID) return;
    if (param->reg.status != ESP_GATT_OK) { ESP_LOGE(TAG, "GATT-App: Fehler %d", param->reg.status); return; }
    gatts_if_ = gatts_if;
    esp_ble_gap_set_device_name(name_.c_str());
    esp_ble_gatts_create_attr_tab(dis_db, gatts_if, D_NB, SVC_DIS);
    esp_ble_gatts_create_attr_tab(bas_db, gatts_if, B_NB, SVC_BAS);
    esp_ble_gatts_create_attr_tab(hid_db, gatts_if, H_NB, SVC_HID);
    if (sprachdienst_) esp_ble_gatts_create_attr_tab(atvv_db, gatts_if, A_NB, SVC_ATVV);
    return;
  }
  if (gatts_if != gatts_if_ && gatts_if != ESP_GATT_IF_NONE) return;
  switch (event) {
    case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
      const auto &t = param->add_attr_tab;
      if (t.status != ESP_GATT_OK) {
        ESP_LOGE(TAG, "GATT-Tabelle %u: Fehler %d", t.svc_inst_id, t.status);
        if (t.svc_inst_id == SVC_ATVV) a_status = t.status;
        break;
      }
      if (t.svc_inst_id == SVC_HID && t.num_handle == H_NB) memcpy(h_tab, t.handles, sizeof(h_tab));
      if (t.svc_inst_id == SVC_ATVV) {
        a_status = t.num_handle == A_NB ? 0 : 1000 + t.num_handle;
        if (t.num_handle == A_NB) memcpy(a_tab, t.handles, sizeof(a_tab));
      }
      esp_ble_gatts_start_service(t.handles[0]);
      break;
    }
    case ESP_GATTS_CONNECT_EVT: {
      const auto &c = param->connect;
      if (c.link_role != 1) break;   // Dock ist Zentrale = Proxy-Verbindung
      {
        std::lock_guard<std::mutex> l(mtx_);
        Verb *v = nullptr;
        for (auto &x : verb_) if (!x.aktiv) { v = &x; break; }
        if (v == nullptr) { esp_ble_gap_disconnect(const_cast<uint8_t *>(c.remote_bda)); break; }
        *v = Verb{};
        v->aktiv = true;
        v->conn_id = c.conn_id;
        memcpy(v->bda, c.remote_bda, 6);
        v->seit_ms = millis();
        v->addr_type = c.ble_addr_type;
        v->platz = platz_von_(c.remote_bda);   // gesichert erst nach der Verschluesselung (AUTH_CMPL)
      }
      werbung_ist_ = 0;   // verbindbare Werbung endet mit der Verbindung
      werbung_busy_ = false;
      if (wieder_bis_ != 0) wieder_bis_ = (millis() + 5000) | 1;   // nach dem Wiederverbinden noch kurz ohne Suche
      // Takt 15-30 ms (Touchpad flott genug), der Host entscheidet. Vorher ab 11,25 ms: zusammen mit dem Proxy-Scan
      // (50 ms Fenster) lief der Controller beim Start in den Interrupt-Watchdog (r_lld_scan_try_sched, 2026-10-22).
      esp_ble_conn_update_params_t cp = {};
      memcpy(cp.bda, c.remote_bda, 6);
      // (2026-10-23 mit 30-50 ms getestet: "Rejected: Resources" fuer Proxy-Verbindungen blieb - zurueck auf 15-30 ms)
      cp.min_int = 0x0C;
      cp.max_int = 0x18;
      cp.latency = 0;
      cp.timeout = 400;
      esp_ble_gap_update_conn_params(&cp);
      char a[18];
      mac_text(c.remote_bda, a);
      ESP_LOGI(TAG, "Verbunden: %s (conn %u)", a, c.conn_id);
      geaendert_ = true;
      break;
    }
    case ESP_GATTS_DISCONNECT_EVT: {
      std::lock_guard<std::mutex> l(mtx_);
      for (auto &v : verb_) {
        if (v.aktiv && v.conn_id == param->disconnect.conn_id) {
          ESP_LOGI(TAG, "Getrennt: Platz %d (Grund 0x%x)", v.platz + 1, param->disconnect.reason);
          v.aktiv = false;
          // Wiederverbinden ohne Proxy-Suche (2026-10-27): alle 9 Abstuerze (Interrupt-Watchdog in der Scan-Planung
          // des Controllers, r_lld_scan_try_sched) kamen 33-86 s nach einer Trennung des Fernsehers, im Moment seines
          // Wiederverbindens (Flugschreiber). Suche aus, bis er wieder da ist (+5 s), hoechstens 2 min.
          if (v.platz >= 0) wieder_bis_ = (millis() + 120000) | 1;
          geaendert_ = true;
        }
      }
      break;
    }
    case ESP_GATTS_MTU_EVT: {
      std::lock_guard<std::mutex> l(mtx_);
      for (auto &v : verb_) if (v.aktiv && v.conn_id == param->mtu.conn_id) v.mtu = param->mtu.mtu;
      break;
    }
    case ESP_GATTS_WRITE_EVT: {
      const auto &w = param->write;
      if (sprachdienst_ && w.handle == a_tab[A_TX] && w.len > 0 && !w.is_prep) atvv_write_(w.conn_id, w.value, w.len);
      break;
    }
    case ESP_GATTS_READ_EVT: {
      const auto &r = param->read;
      if (!r.need_rsp || r.handle != h_tab[H_MAP]) break;
      uint8_t opt = OPT_MAUS | OPT_VOLL;
      uint16_t mtu = 23;
      {
        std::lock_guard<std::mutex> l(mtx_);
        for (auto &v : verb_) {
          if (!v.aktiv || v.conn_id != r.conn_id) continue;
          mtu = v.mtu;
          int p = v.platz >= 0 ? v.platz : platz_von_(v.bda);
          if (p >= 0) opt = plaetze_[p].optionen;
          else if (koppel_platz_ >= 0) opt = koppel_opt_;
        }
      }
      static uint8_t karte[sizeof(KARTE)];
      const size_t n = karte_(opt, karte);
      esp_gatt_rsp_t rsp = {};
      rsp.attr_value.handle = r.handle;
      rsp.attr_value.offset = r.offset;
      size_t rest = r.offset < n ? n - r.offset : 0;
      if (rest > (size_t) (mtu - 1)) rest = mtu - 1;
      if (rest > ESP_GATT_MAX_ATTR_LEN) rest = ESP_GATT_MAX_ATTR_LEN;
      rsp.attr_value.len = rest;
      memcpy(rsp.attr_value.value, karte + r.offset, rest);
      esp_ble_gatts_send_response(gatts_if, r.conn_id, r.trans_id, ESP_GATT_OK, &rsp);
      if (r.offset == 0)
        ESP_LOGI(TAG, "Geraetebeschreibung an conn %u: %u Byte (Maus %s, Tastatur %s)", r.conn_id, (unsigned) n,
                 (opt & OPT_MAUS) ? "ja" : "nein", (opt & OPT_VOLL) ? "voll" : "Ziffern");
      break;
    }
    default:
      break;
  }
}

// ------------------------------------------------------------------ MTU fuer den Sprachdienst (2026-10-24)
void DockHid::mtu_anfragen_(uint16_t conn_id) {
  if (gattc_if_ == ESP_GATT_IF_NONE) return;
  uint8_t bda[6];
  uint8_t typ = 0;
  {
    std::lock_guard<std::mutex> l(mtx_);
    Verb *v = nullptr;
    for (auto &x : verb_) if (x.aktiv && x.conn_id == conn_id) v = &x;
    if (v == nullptr || v->mtu > 23 || v->mtu_gefragt) return;
    v->mtu_gefragt = true;
    memcpy(bda, v->bda, 6);
    typ = v->addr_type;
  }
  memcpy(mtu_bda_, bda, 6);
  const esp_err_t e = esp_ble_gattc_open(gattc_if_, bda, (esp_ble_addr_type_t) typ, true);
  ESP_LOGI(TAG, "conn %u: groessere MTU anfragen (%s)", conn_id, esp_err_to_name(e));
}

void DockHid::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if, esp_ble_gattc_cb_param_t *param) {
  if (event == ESP_GATTC_REG_EVT) {
    if (param->reg.app_id == APP_ID_C && param->reg.status == ESP_GATT_OK) gattc_if_ = gattc_if;
    return;
  }
  if (gattc_if_ == ESP_GATT_IF_NONE || gattc_if != gattc_if_) return;
  switch (event) {
    case ESP_GATTC_OPEN_EVT:
      if (param->open.status == ESP_GATT_OK) esp_ble_gattc_send_mtu_req(gattc_if, param->open.conn_id);
      else ESP_LOGW(TAG, "MTU: Client-Verbindung ging nicht (%d)", param->open.status);
      break;
    case ESP_GATTC_CFG_MTU_EVT: {
      ESP_LOGI(TAG, "MTU ausgehandelt: %u (Status %d)", param->cfg_mtu.mtu, param->cfg_mtu.status);
      if (param->cfg_mtu.status != ESP_GATT_OK) break;
      std::lock_guard<std::mutex> l(mtx_);
      for (auto &v : verb_) if (v.aktiv && memcmp(v.bda, mtu_bda_, 6) == 0) v.mtu = param->cfg_mtu.mtu;
      break;
    }
    default:
      break;
  }
}

// ------------------------------------------------------------------ Sprache (2026-10-24)
// Bluetooth-Task: Befehl des Fernsehers auf dem Sprachdienst
void DockHid::atvv_write_(uint16_t conn_id, const uint8_t *v, uint16_t n) {
  int p = -1;
  {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto &x : verb_) if (x.aktiv && x.conn_id == conn_id) p = x.platz;
  }
  if (v[0] == ATVV_GET_CAPS) {
    // Antwort wie die echten Fernbedienungen (s. atv_voice handle_get_caps_): 20 Byte, Codec an [3], [4] = 3, Chunk 20
    const uint16_t codecs = n > 4 ? (uint16_t) ((v[3] << 8) | v[4]) : 0;
    uint8_t r[20] = {0};
    r[0] = ATVV_CAPS_RESP;
    r[1] = n > 1 ? v[1] : 1;
    r[2] = n > 2 ? v[2] : 0;
    r[3] = 0x02;   // ADPCM 16 kHz - so kodiert die Remote ueber das Dock
    r[4] = 0x03;
    r[6] = 20;
    if (codecs != 0 && (codecs & 0x02) == 0) ESP_LOGW(TAG, "Fernseher kann kein ADPCM 16 kHz (0x%04X) - Sprache wird nicht gehen", codecs);
    esp_ble_gatts_send_indicate(gatts_if_, conn_id, a_tab[A_CTL], sizeof(r), r, false);
    ESP_LOGI(TAG, "Platz %d: Sprachdienst - Faehigkeiten gemeldet (Fernseher-Codecs 0x%04X)", p + 1, codecs);
    mtu_anfragen_(conn_id);
    caps_n_++;
    geaendert_ = true;
    return;
  }
  if ((v[0] == ATVV_MIC_OPEN || v[0] == ATVV_MIC_CLOSE) && p >= 0) {
    std::lock_guard<std::mutex> l(tv_mtx_);
    if (tv_wunsch_.size() < 8) tv_wunsch_.push_back({(uint8_t) p, std::vector<uint8_t>(v, v + n)});
    return;
  }
  ESP_LOGD(TAG, "Platz %d: Sprachbefehl 0x%02X (%u Byte) ignoriert", p + 1, v[0], n);
}

// Hauptprogramm (dock_direkt typ 12): Steuer-Nachricht oder Audio der Remote fuer Platz p
bool DockHid::sprache(uint8_t p, uint8_t art, const uint8_t *d, uint16_t n) {
  if (!sprachdienst_ || p >= anzahl_ || n == 0 || !verbunden(p)) return false;
  sprache_ms_ = millis();
  const bool ctl = art == 0;
  if (ctl && d[0] == 0x04) {   // AUDIO_START: noch kleine MTU -> jetzt anfragen (kommt fuer die naechsten Pakete)
    uint16_t cid = 0xFFFF;
    {
      std::lock_guard<std::mutex> l(mtx_);
      for (auto &v : verb_) if (v.aktiv && v.sicher && v.platz == p && v.mtu <= 23) cid = v.conn_id;
    }
    if (cid != 0xFFFF) mtu_anfragen_(cid);
  }
  if (ctl && d[0] == 0x04 && sprache_platz_ != (int8_t) p) {   // AUDIO_START
    sprache_platz_ = (int8_t) p;
    scan_wieder_ms_ = millis() + 1000;
    scan_(false);
    esp_coex_preference_set(ESP_COEX_PREFER_BT);
    sz_ = SprachZahl{};
    sz_.start_ms = millis();
    {
      std::lock_guard<std::mutex> l(mtx_);
      for (auto &v : verb_) if (v.aktiv && v.sicher && v.platz == p) sz_.mtu = v.mtu;
    }
    ESP_LOGI(TAG, "Platz %u: Sprache von der Remote (MTU %u)", (unsigned) p + 1, sz_.mtu);
  }
  if (!ctl) sz_.rein += n;
  // Audio in Stuecke passend zur Verbindung (MTU - 3, hoechstens 128 Byte wie atv_voice)
  uint16_t mtu = 23;
  {
    std::lock_guard<std::mutex> l(mtx_);
    for (auto &v : verb_) if (v.aktiv && v.sicher && v.platz == p) mtu = v.mtu;
  }
  const uint16_t stueck = ctl ? n : std::max<uint16_t>(20, std::min<uint16_t>(128, mtu - 3));
  for (uint16_t off = 0; off < n; off += stueck) {
    const uint16_t l = std::min<uint16_t>(stueck, n - off);
    if (l > 255 || !ring_rein_(p, ctl, d + off, (uint8_t) l)) {
      if (sz_.weg == 0) ESP_LOGW(TAG, "Platz %u: Sprache staut sich - Puffer voll, verwerfe", (unsigned) p + 1);
      sz_.weg += n - off;
      break;
    }
  }
  if (sprache_bytes_ > sz_.max_belegt) sz_.max_belegt = sprache_bytes_;
  return true;
}

bool DockHid::ring_rein_(uint8_t p, bool ctl, const uint8_t *d, uint8_t n) {
  if (ring_ == nullptr || sprache_bytes_ + 2 + n > SPRACHE_RING) return false;
  auto b = [&](uint8_t x) { ring_[ring_kopf_] = x; ring_kopf_ = (ring_kopf_ + 1) % SPRACHE_RING; };
  b(p | (ctl ? 0x80 : 0));
  b(n);
  for (uint8_t i = 0; i < n; i++) b(d[i]);
  sprache_bytes_ += 2 + n;
  return true;
}

bool DockHid::ring_vorne_(uint8_t &p, bool &ctl, uint8_t *d, uint8_t &n) const {
  if (sprache_bytes_ == 0) return false;
  const uint8_t k = ring_[ring_fuss_];
  p = k & 0x7F;
  ctl = (k & 0x80) != 0;
  n = ring_[(ring_fuss_ + 1) % SPRACHE_RING];
  for (uint8_t i = 0; i < n; i++) d[i] = ring_[(ring_fuss_ + 2 + i) % SPRACHE_RING];
  return true;
}

void DockHid::ring_weg_(uint8_t n) {
  ring_fuss_ = (ring_fuss_ + 2 + n) % SPRACHE_RING;
  sprache_bytes_ -= 2 + n;
}

bool DockHid::sprach_senden_(uint8_t p, bool ctl, const uint8_t *d, uint16_t n) {
  const uint16_t h = a_tab[ctl ? A_CTL : A_RX];
  if (gatts_if_ == ESP_GATT_IF_NONE) return false;
  if (h == 0) {   // Sprachdienst nicht angelegt: verwerfen statt stauen
    static bool gemeldet = false;
    if (!gemeldet) { gemeldet = true; ESP_LOGE(TAG, "Sprachdienst nicht angelegt (Status %d) - Sprache wird verworfen", a_status); }
    return true;
  }
  std::lock_guard<std::mutex> l(mtx_);
  for (auto &v : verb_)
    if (v.aktiv && v.sicher && v.platz == p)
      return esp_ble_gatts_send_indicate(gatts_if_, v.conn_id, h, n, const_cast<uint8_t *>(d), false) == ESP_OK;
  return true;   // Platz weg: verwerfen
}

void DockHid::sprache_loop_(uint32_t jetzt) {
  // Wuensche des Fernsehers an die Remote weitergeben
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>> w;
  {
    std::lock_guard<std::mutex> l(tv_mtx_);
    w.swap(tv_wunsch_);
  }
  for (auto &x : w) {
    const bool auf = x.second[0] == ATVV_MIC_OPEN;
    ESP_LOGI(TAG, "Platz %u: Fernseher %s das Mikrofon", (unsigned) x.first + 1, auf ? "will" : "schliesst");
    if (!auf && sprache_platz_ == (int8_t) x.first) {   // wie atv_voice: Schliessen mit AUDIO_STOP (Grund 0) bestaetigen
      uint8_t stop[20] = {ATVV_AUDIO_STOP, 0x00};
      if (!ring_rein_(x.first, true, stop, sizeof(stop))) { ring_leeren_(); ring_rein_(x.first, true, stop, sizeof(stop)); }
    }
    if (an_remote) an_remote(x.first, auf ? 2 : 3, x.second);
  }
  // An den Fernseher schieben, solange der Funk es nimmt
  static uint32_t stau_seit = 0;
  uint8_t sp, sn, sd[255];
  bool sctl;
  for (int k = 0; k < 24 && ring_vorne_(sp, sctl, sd, sn); k++) {
    if (!sprach_senden_(sp, sctl, sd, sn)) {   // Stau: naechste Runde
      if (stau_seit == 0) stau_seit = jetzt;
      if (jetzt - stau_seit > 800) {
        ESP_LOGW(TAG, "Platz %u: Fernseher nimmt seit 800 ms nichts an - %u Byte Sprache verworfen", (unsigned) sp + 1, (unsigned) sprache_bytes_);
        sz_.weg += sprache_bytes_;
        ring_leeren_();
        stau_seit = 0;
      }
      break;
    }
    stau_seit = 0;
    if (!sctl) sz_.raus += sn;
    ring_weg_(sn);
    if (sctl && sn > 0 && sd[0] == ATVV_AUDIO_STOP) sprache_ms_ = 0;
  }
  // Ende: AUDIO_STOP gesendet oder 3 s nichts mehr von der Remote
  if (sprache_platz_ >= 0 && sprache_bytes_ == 0 && (sprache_ms_ == 0 || jetzt - sprache_ms_ > 3000)) {
    if (sprache_ms_ != 0) {
      uint8_t stop[20] = {ATVV_AUDIO_STOP, 0x02};
      sprach_senden_((uint8_t) sprache_platz_, true, stop, sizeof(stop));
      ESP_LOGW(TAG, "Platz %d: Sprache ohne Ende von der Remote - beendet", sprache_platz_ + 1);
    }
    ESP_LOGI(TAG, "Platz %d: Sprache fertig nach %.1f s - %u Byte von der Remote, %u an den Fernseher, %u verworfen, Puffer max %u, MTU %u",
             sprache_platz_ + 1, (jetzt - sz_.start_ms) / 1000.0f, (unsigned) sz_.rein, (unsigned) sz_.raus, (unsigned) sz_.weg,
             (unsigned) sz_.max_belegt, sz_.mtu);
    sprache_platz_ = -1;
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
    scan_wieder_ms_ = jetzt + 500;
  }
  if (sprache_platz_ >= 0) scan_wieder_ms_ = jetzt + 1000;   // waehrenddessen kein Proxy-Scan
}

// ------------------------------------------------------------------ Senden
bool DockHid::verbunden(uint8_t p) const {
  std::lock_guard<std::mutex> l(mtx_);
  for (auto &v : verb_) if (v.aktiv && v.sicher && v.platz == p) return true;
  return false;
}

uint8_t DockHid::maske() const {
  uint8_t m = 0;
  std::lock_guard<std::mutex> l(mtx_);
  for (auto &v : verb_) if (v.aktiv && v.sicher && v.platz >= 0) m |= 1u << v.platz;
  return m;
}

bool DockHid::report_(uint8_t p, uint16_t handle, const uint8_t *d, uint16_t n) {
  if (gatts_if_ == ESP_GATT_IF_NONE || handle == 0) return false;
  bool ok = false;
  std::lock_guard<std::mutex> l(mtx_);
  for (auto &v : verb_) {
    if (!v.aktiv || !v.sicher || v.platz != p) continue;
    ok = esp_ble_gatts_send_indicate(gatts_if_, v.conn_id, handle, n, const_cast<uint8_t *>(d), false) == ESP_OK || ok;
  }
  return ok;
}

bool DockHid::wirkt_(uint8_t p, const Ereignis &e, bool unten) {
  switch (e.art) {
    case CONSUMER: {
      uint8_t r[2] = {unten ? e.d[0] : (uint8_t) 0, unten ? e.d[1] : (uint8_t) 0};
      return report_(p, h_tab[H_CONS], r, 2);
    }
    case TASTE: {
      uint8_t r[8] = {0};
      if (unten) { r[0] = e.d[0]; r[2] = e.d[1]; }
      return report_(p, h_tab[H_KB], r, 8);
    }
    case KNOPF: {
      uint8_t r = (unten && e.d[0] >= 1 && e.d[0] <= 8) ? (uint8_t) (1u << (e.d[0] - 1)) : 0;
      return report_(p, h_tab[H_KNOPF], &r, 1);
    }
    case SYSTEM: {
      uint8_t r = unten ? e.d[0] : 0;
      return report_(p, h_tab[H_SYS], &r, 1);
    }
    case MAUS: {
      if (!(plaetze_[p].optionen & OPT_MAUS)) return false;
      return report_(p, h_tab[H_MAUS], e.d, 4);
    }
    default:
      return false;
  }
}

bool DockHid::senden(uint8_t p, const Ereignis &e) {
  if (p >= anzahl_ || !verbunden(p)) return false;
  n_ereignisse_++;
  const uint32_t jetzt = millis();
  if (e.art == MAUS) return wirkt_(p, e, true);
  // Reihenfolge je Platz einhalten: liegt noch etwas an, hinten anstellen
  uint32_t ab = jetzt;
  for (auto &s : spaeter_) if (s.p == p && (int32_t) (s.ms - ab) >= 0) ab = s.ms + 5;
  auto jetzt_oder_spaeter = [&](const Ereignis &x, uint32_t t) {
    if (t == jetzt) wirkt_(p, x, x.aktion != LOSLASSEN);
    else spaeter_.push_back(Spaeter{t, p, x});
  };
  Gehalten *g = nullptr;
  const uint8_t gi = p * 3 + (e.art == CONSUMER ? 0 : e.art == TASTE ? 1 : 2);
  if (gi < MAX_PLAETZE * 3) g = &gehalten_[gi];
  switch (e.aktion) {
    case TIPP: {
      Ereignis runter = e, hoch = e;
      runter.aktion = DRUECKEN;
      hoch.aktion = LOSLASSEN;
      jetzt_oder_spaeter(runter, ab);
      spaeter_.push_back(Spaeter{ab + TIPP_MS, p, hoch});
      break;
    }
    case DRUECKEN:
      jetzt_oder_spaeter(e, ab);
      if (g) *g = Gehalten{true, e.art, p, ab + halten_ms_};
      break;
    case HALTEN:
      if (g && g->an) g->bis_ms = jetzt + halten_ms_;
      break;
    case LOSLASSEN:
      jetzt_oder_spaeter(e, ab);
      if (g) g->an = false;
      break;
  }
  return true;
}

// ------------------------------------------------------------------ Stand fuer HA (Konfigurator)
void DockHid::melden_() {
  melden_ms_ = millis();
  geaendert_ = false;
  if (status_ == nullptr) return;
  const uint8_t m = maske();
  std::string s = "{\"k\":";
  s += std::to_string(koppel_platz_);
  if (koppel_platz_ >= 0) {
    s += ",\"ks\":";
    s += std::to_string((int32_t) (koppel_bis_ - millis()) / 1000);
  }
  if (sprachdienst_) {   // Sprachdienst: Status der GATT-Tabelle (0 ok, -1 fehlt) und ob ein Fernseher ihn nutzt
    s += ",\"sp\":";
    s += std::to_string(a_status);
    s += ",\"spc\":";
    s += std::to_string(caps_n_);
  }
  s += ",\"p\":[";
  for (int i = 0; i < anzahl_; i++) {
    if (i) s += ',';
    const Platz &p = plaetze_[i];
    s += "{\"b\":";
    s += p.belegt ? '1' : '0';
    if (p.belegt) {
      s += ",\"n\":\"";
      for (const char *c = p.name; *c; c++) if (*c != '"' && *c != '\\') s += *c;
      s += "\",\"v\":";
      s += (m >> i) & 1 ? '1' : '0';
      s += ",\"o\":";
      s += std::to_string(p.optionen);
    }
    s += '}';
  }
  s += "]}";
  if (s.size() > 255) s = s.substr(0, 255);
  if (status_->get_state() != s) status_->publish_state(s);
}

}  // namespace dock_hid
}  // namespace esphome
