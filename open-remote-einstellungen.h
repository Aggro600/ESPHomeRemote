// GENERIERT (einst_gen.py, 2026-10-04): Einstellungs-Eintraege des Konfigurator-Menues -> Widgets der
// eingebauten Einstellungsseiten. Das Menue steuert diese Widgets fern (Klick / Regler), die Logik bleibt in der YAML.
#pragma once
#include "esphome/components/menue/menue.h"

namespace einstellungen {

inline esphome::menue::EinstRef finden(const std::string &k) {
  struct Z { const char *k; char t; lv_obj_t *o; lv_obj_t *w; };
  static const Z tab[] = {   // static: erst beim ersten Aufruf (nach dem LVGL-Aufbau) gefuellt
    {"disp_bri_sld", 's', disp_bri_sld, disp_bri_lbl},   // bright: Display
    {"h_bri", 'i', nullptr, h_bri},   // bright: Hinweis
    {"kbd_light_btn", 'b', kbd_light_btn, kbd_light_lbl},   // bright: Tastenbeleuchtung
    {"h_kbdon", 'i', nullptr, h_kbdon},   // bright: Hinweis
    {"kbd_bri_sld", 's', kbd_bri_sld, kbd_bri_lbl},   // bright: Tastenbeleuchtung
    {"h_kbdbri", 'i', nullptr, h_kbdbri},   // bright: Hinweis
    {"to_sld", 's', to_sld, to_val_lbl},   // bright: Display aus nach
    {"h_to", 'i', nullptr, h_to},   // bright: Hinweis
    {"ps_dim_sld", 's', ps_dim_sld, ps_dim_lbl},   // bright: Abdunkeln nach
    {"ps_h_dim", 'i', nullptr, ps_h_dim},   // bright: Hinweis
    {"ps_dimp_sld", 's', ps_dimp_sld, ps_dimp_lbl},   // bright: Abdunkeln auf
    {"ps_h_dimp", 'i', nullptr, ps_h_dimp},   // bright: Hinweis
    {"show_ble_btn", 'b', show_ble_btn, sh_ble_lbl},   // show: Bluetooth-Symbol
    {"h_sh_ble", 'i', nullptr, h_sh_ble},   // show: Hinweis
    {"show_wifi_btn", 'b', show_wifi_btn, sh_wifi_lbl},   // show: WLAN-Symbol
    {"h_sh_wifi", 'i', nullptr, h_sh_wifi},   // show: Hinweis
    {"show_batt_btn", 'b', show_batt_btn, sh_batt_lbl},   // show: Akkustand
    {"h_sh_batt", 'i', nullptr, h_sh_batt},   // show: Hinweis
    {"show_clock_btn", 'b', show_clock_btn, sh_clock_lbl},   // show: Uhrzeit
    {"h_sh_clock", 'i', nullptr, h_sh_clock},   // show: Hinweis
    {"show_assi_btn", 'b', show_assi_btn, sh_assi_lbl},   // show: Assistenten-Symbol
    {"h_sh_assi", 'i', nullptr, h_sh_assi},   // show: Hinweis
    {"show_motion_btn", 'b', show_motion_btn, sh_motion_lbl},   // show: Bewegungs-Symbol
    {"cover_player_btn", 'b', cover_player_btn, cover_player_lbl},   // show: Cover Player
    {"h_cov_p", 'i', nullptr, h_cov_p},   // show: Hinweis
    {"cover_home_btn", 'b', cover_home_btn, cover_home_lbl},   // show: Cover Startseite
    {"h_cov_h", 'i', nullptr, h_cov_h},   // show: Hinweis
    {"mem_sld", 's', mem_sld, mem_lbl},   // show: Menü merken für
    {"ps_h_mem", 'i', nullptr, ps_h_mem},   // show: Hinweis
    {"menu_close_sld", 's', menu_close_sld, menu_close_lbl},   // show: Menü schließen nach
    {"h_menu_close", 'i', nullptr, h_menu_close},   // show: Hinweis
    {"sd_logos_btn", 'b', sd_logos_btn, sd_logos_lbl},   // show: Logos von SD-Karte
    {"h_sd_logos", 'i', nullptr, h_sd_logos},   // show: Hinweis
    {"ps_wifi_btn", 'b', ps_wifi_btn, ps_wifi_lbl},   // power: WLAN-Sparmodus
    {"ps_h_wifi", 'i', nullptr, ps_h_wifi},   // power: Hinweis
    {"ps_wifimax_btn", 'b', ps_wifimax_btn, ps_wifimax_lbl},   // power: Sparmodus stark (Ruhe)
    {"ps_h_wifimax", 'i', nullptr, ps_h_wifimax},   // power: Hinweis
    {"ps_wifi_off_btn", 'b', ps_wifi_off_btn, ps_off_lbl},   // power: WLAN aus im Ruhezustand
    {"ps_h_off", 'i', nullptr, ps_h_off},   // power: Hinweis
    {"ps_wifis_sld", 's', ps_wifis_sld, ps_wifis_lbl},   // power: WLAN aus nach
    {"ps_h_offs", 'i', nullptr, ps_h_offs},   // power: Hinweis
    {"ps_wifitv_sld", 's', ps_wifitv_sld, ps_wifitv_lbl},   // power: WLAN aus bei TV an
    {"ps_h_wifitv", 'i', nullptr, ps_h_wifitv},   // power: Hinweis
    {"ps_cpu_sld", 's', ps_cpu_sld, ps_cpu_lbl},   // power: Prozessor drosseln
    {"ps_h_cpu", 'i', nullptr, ps_h_cpu},   // power: Hinweis
    {"ps_light_btn", 'b', ps_light_btn, ps_light_lbl},   // power: Leichtschlaf
    {"ps_wfest_btn", 'b', ps_wfest_btn, ps_wfest_lbl},   // power: WLAN-Schnellstart
    {"ps_wfrueh_btn", 'b', ps_wfrueh_btn, ps_wfrueh_lbl},   // power: WLAN früh einschalten
    {"ps_bleidle_btn", 'b', ps_bleidle_btn, ps_bleidle_lbl},   // power: BLE-Verbindung langsam
    {"ps_h_ble", 'i', nullptr, ps_h_ble},   // power: Hinweis
    {"ps_bles_sld", 's', ps_bles_sld, ps_bles_lbl},   // power: BLE langsam nach
    {"ps_h_bles", 'i', nullptr, ps_h_bles},   // power: Hinweis
    {"motion_mode_lbl", 'i', nullptr, motion_mode_lbl},   // power: Aufwecken bei
    {"mm_0", 'b', mm_0, mm_0_mk},   // power: Aus
    {"mm_1", 'b', mm_1, mm_1_mk},   // power: Bewegung
    {"mm_2", 'b', mm_2, mm_2_mk},   // power: Hochheben
    {"h_motion", 'i', nullptr, h_motion},   // power: Hinweis
    {"ms_sld", 's', ms_sld, motion_sens_lbl},   // power: Empfindlichkeit
    {"h_sens", 'i', nullptr, h_sens},   // power: Hinweis
    {"keys_wake_btn", 'b', keys_wake_btn, keys_wake_lbl},   // power: Tasten wecken
    {"h_keys", 'i', nullptr, h_keys},   // power: Hinweis
    {"dim_wake_btn", 'b', dim_wake_btn, dim_wake_lbl},   // power: Aufhellen bei Bewegung
    {"h_dim_wake", 'i', nullptr, h_dim_wake},   // power: Hinweis
    {"dim_hold_btn", 'b', dim_hold_btn, dim_hold_lbl},   // power: Nicht abdunkeln bei Bewegung
    {"h_dim_hold", 'i', nullptr, h_dim_hold},   // power: Hinweis
    {"ds_sld", 's', ds_sld, dim_sens_lbl},   // power: Empfindlichkeit (beide oben)
    {"h_dim_sens", 'i', nullptr, h_dim_sens},   // power: Hinweis
    {"klopf_btn", 'b', klopf_btn, klopf_lbl},   // power: Doppelt klopfen weckt
    {"kl_sld", 's', kl_sld, klopf_sens_lbl},   // power: Klopf-Empfindlichkeit
    {"h_klopf", 'i', nullptr, h_klopf},   // power: Hinweis
    {"ps_touch_btn", 'b', ps_touch_btn, ps_touch_lbl},   // power: Touch weckt
    {"ps_h_touch", 'i', nullptr, ps_h_touch},   // power: Hinweis
    {"ps_touchms_sld", 's', ps_touchms_sld, ps_touchms_lbl},   // power: Touch-Abfrage
    {"ps_h_touchms", 'i', nullptr, ps_h_touchms},   // power: Hinweis
    {"ps_deep_btn", 'b', ps_deep_btn, ps_deep_lbl},   // power: Tiefschlaf im Ruhezustand
    {"ps_h_deep", 'i', nullptr, ps_h_deep},   // power: Hinweis
    {"ps_deepble_btn", 'b', ps_deepble_btn, ps_deepble_lbl},   // power: Nur ohne Bluetooth
    {"ps_deeps_sld", 's', ps_deeps_sld, ps_deeps_lbl},   // power: Tiefschlaf nach
    {"ps_h_deeps", 'i', nullptr, ps_h_deeps},   // power: Hinweis
    {"ps_deeptv_sld", 's', ps_deeptv_sld, ps_deeptv_lbl},   // power: Tiefschlaf bei TV an
    {"ps_h_deeptv", 'i', nullptr, ps_h_deeptv},   // power: Hinweis
    {"ps_low_sld", 's', ps_low_sld, ps_low_lbl},   // power: Sparmodus bei Akku unter
    {"ps_h_low", 'i', nullptr, ps_h_low},   // power: Hinweis
    {"tile_assi", 'b', tile_assi, assi_lbl},   // voice_set: Assistent
    {"h_assi", 'i', nullptr, h_assi},   // voice_set: Hinweis
    {"tile_google_mode", 'b', tile_google_mode, google_mode_lbl},   // voice_set: Google-Modus
    {"h_google_mode", 'i', nullptr, h_google_mode},   // voice_set: Hinweis
    {"tile_tts", 'b', tile_tts, tts_lbl},   // voice_set: Antwort hörbar
    {"h_tts", 'i', nullptr, h_tts},   // voice_set: Hinweis
    {"tile_mic", 'b', tile_mic, mic_lbl},   // voice_set: Mikrofon
    {"ble_state_lbl", 'i', nullptr, ble_state_lbl},   // ble: Nicht verbunden
    {"ble_slot0", 'b', ble_slot0, ble_slot0_lbl},   // ble: Google Streamer
    {"ble_slot1", 'b', ble_slot1, ble_slot1_lbl},   // ble: TV
    {"ble_slot2", 'b', ble_slot2, ble_slot2_lbl},   // ble: Tablet Wohnzimmer
    {"ble_slot3", 'b', ble_slot3, ble_slot3_lbl},   // ble: Slot 4
    {"ble_target_lbl", 'i', nullptr, ble_target_lbl},   // ble: Aktion für: 1
    {"ble_connect", 'b', ble_connect, nullptr},   // ble: Verbinden
    {"ble_pair", 'b', ble_pair, nullptr},   // ble: Neu koppeln
    {"ble_forget", 'b', ble_forget, nullptr},   // ble: Kopplung löschen
    {"ble_kbd_btn", 'b', ble_kbd_btn, ble_kbd_lbl},   // ble: Tastatur
    {"ble_kbd_hint", 'i', nullptr, ble_kbd_hint},   // ble: Hinweis
    {"ble_par_btn", 'b', ble_par_btn, ble_par_lbl},   // ble: Parallel verbunden
    {"ak_status", 'i', nullptr, ak_status},   // akku: Status
    {"ak_pct", 'i', nullptr, ak_pct},   // akku: Akkustand
    {"ak_volt", 'i', nullptr, ak_volt},   // akku: Spannung
    {"ak_rate", 'i', nullptr, ak_rate},   // akku: Rate jetzt (Chip)
    {"ak_r1", 'i', nullptr, ak_r1},   // akku: Letzte Stunde
    {"ak_r24", 'i', nullptr, ak_r24},   // akku: Letzte 24 h
    {"ak_rest", 'i', nullptr, ak_rest},   // akku: Restlaufzeit
    {"ak_geladen", 'i', nullptr, ak_geladen},   // akku: Zuletzt geladen
    {"ak_geladen2", 'i', nullptr, ak_geladen2},   // akku: ak_geladen2
    {"ak_schlaf", 'i', nullptr, ak_schlaf},   // akku: Tiefschlaf seit Ladung
    {"tile_touchtest", 'b', tile_touchtest, nullptr},   // service: Touch-Test
    {"h_touch", 'i', nullptr, h_touch},   // service: Hinweis
    {"tile_button_test", 'b', tile_button_test, nullptr},   // service: Tasten-Test
    {"h_btn", 'i', nullptr, h_btn},   // service: Hinweis
    {"clean_dur_sld", 's', clean_dur_sld, clean_dur_lbl},   // service: Reinigungsdauer
    {"h_cleandur", 'i', nullptr, h_cleandur},   // service: Hinweis
    {"tile_clean", 'b', tile_clean, nullptr},   // service: Reinigungsmodus
    {"h_clean", 'i', nullptr, h_clean},   // service: Hinweis
    {"tile_sd", 'b', tile_sd, nullptr},   // service: SD-Karte
    {"h_sd_tile", 'i', nullptr, h_sd_tile},   // service: Hinweis
    {"ps_log_btn", 'b', ps_log_btn, ps_log_lbl},   // service: Debug-Log
    {"ps_h_log", 'i', nullptr, ps_h_log},   // service: Hinweis
    {"testmod_btn", 'b', testmod_btn, testmod_lbl},   // service: Testmodus (immer online)
    {"ps_h_test", 'i', nullptr, ps_h_test},   // service: Hinweis
    {"tile_shutdown", 'b', tile_shutdown, nullptr},   // service: Gerät herunterfahren
    {"tile_restart", 'b', tile_restart, restart_lbl},   // service: Gerät neu starten
    {"h_restart", 'i', nullptr, h_restart},   // service: Hinweis
  };
  for (const auto &z : tab) if (k == z.k) return {z.t, z.o, z.w};
  return {};
}

// Werte der eingebauten Seiten auffrischen (ohne Seitenwechsel)
inline void auffrischen() {
  einst_bright->execute();
  einst_show->execute();
  einst_power->execute();
  einst_voice_set->execute();
  einst_ble->execute();
  einst_akku->execute();
  einst_service->execute();
}

}  // namespace einstellungen
