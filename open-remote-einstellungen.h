// GENERIERT (tools/einst_parse.py + einst_gen.py): Einstellungs-Eintraege des Konfigurator-Menues -> Widgets der
// eingebauten Einstellungsseiten. Das Menue steuert diese Widgets fern (Klick / Regler), die Logik bleibt in der YAML.
#pragma once
#include "esphome/components/menue/menue.h"

namespace einstellungen {

inline esphome::menue::EinstRef finden(const std::string &k) {
  struct Z { const char *k; char t; lv_obj_t *o; lv_obj_t *w; uint8_t g; };
  static const Z tab[] = {   // static: erst beim ersten Aufruf (nach dem LVGL-Aufbau) gefuellt
    {"disp_bri_sld", 's', disp_bri_sld, disp_bri_lbl, 0},   // bright: Display
    {"h_bri", 'i', nullptr, h_bri, 0},   // bright: Hinweis
    {"kbd_light_btn", 'b', kbd_light_btn, kbd_light_lbl, 0},   // bright: Tastenbeleuchtung
    {"h_kbdon", 'i', nullptr, h_kbdon, 0},   // bright: Hinweis
    {"kbd_bri_sld", 's', kbd_bri_sld, kbd_bri_lbl, 0},   // bright: Tastenbeleuchtung
    {"h_kbdbri", 'i', nullptr, h_kbdbri, 0},   // bright: Hinweis
    {"to_sld", 's', to_sld, to_val_lbl, 0},   // bright: Display aus nach
    {"h_to", 'i', nullptr, h_to, 0},   // bright: Hinweis
    {"ps_dim_sld", 's', ps_dim_sld, ps_dim_lbl, 0},   // bright: Abdunkeln nach
    {"ps_h_dim", 'i', nullptr, ps_h_dim, 0},   // bright: Hinweis
    {"ps_dimp_sld", 's', ps_dimp_sld, ps_dimp_lbl, 0},   // bright: Abdunkeln auf
    {"ps_h_dimp", 'i', nullptr, ps_h_dimp, 0},   // bright: Hinweis
    {"show_ble_btn", 'b', show_ble_btn, sh_ble_lbl, 1},   // show: Bluetooth-Symbol
    {"h_sh_ble", 'i', nullptr, h_sh_ble, 1},   // show: Hinweis
    {"show_wifi_btn", 'b', show_wifi_btn, sh_wifi_lbl, 1},   // show: WLAN-Symbol
    {"h_sh_wifi", 'i', nullptr, h_sh_wifi, 1},   // show: Hinweis
    {"show_batt_btn", 'b', show_batt_btn, sh_batt_lbl, 1},   // show: Akkustand
    {"h_sh_batt", 'i', nullptr, h_sh_batt, 1},   // show: Hinweis
    {"show_clock_btn", 'b', show_clock_btn, sh_clock_lbl, 1},   // show: Uhrzeit
    {"h_sh_clock", 'i', nullptr, h_sh_clock, 1},   // show: Hinweis
    {"show_assi_btn", 'b', show_assi_btn, sh_assi_lbl, 1},   // show: Assistenten-Symbol
    {"h_sh_assi", 'i', nullptr, h_sh_assi, 1},   // show: Hinweis
    {"show_motion_btn", 'b', show_motion_btn, sh_motion_lbl, 1},   // show: Bewegungs-Symbol
    {"show_ha_btn", 'b', show_ha_btn, sh_ha_lbl, 1},   // show: Home-Assistant-Symbol
    {"h_sh_ha", 'i', nullptr, h_sh_ha, 1},   // show: Hinweis
    {"cover_player_btn", 'b', cover_player_btn, cover_player_lbl, 1},   // show: Cover Player
    {"h_cov_p", 'i', nullptr, h_cov_p, 1},   // show: Hinweis
    {"cover_home_btn", 'b', cover_home_btn, cover_home_lbl, 1},   // show: Cover Startseite
    {"h_cov_h", 'i', nullptr, h_cov_h, 1},   // show: Hinweis
    {"mem_sld", 's', mem_sld, mem_lbl, 1},   // show: Menü merken für
    {"ps_h_mem", 'i', nullptr, ps_h_mem, 1},   // show: Hinweis
    {"menu_close_sld", 's', menu_close_sld, menu_close_lbl, 1},   // show: Menü schließen nach
    {"h_menu_close", 'i', nullptr, h_menu_close, 1},   // show: Hinweis
    {"sd_logos_btn", 'b', sd_logos_btn, sd_logos_lbl, 1},   // show: Logos von SD-Karte
    {"h_sd_logos", 'i', nullptr, h_sd_logos, 1},   // show: Hinweis
    {"ps_wifi_btn", 'b', ps_wifi_btn, ps_wifi_lbl, 2},   // power: WLAN-Sparmodus
    {"ps_h_wifi", 'i', nullptr, ps_h_wifi, 2},   // power: Hinweis
    {"ps_wifimax_btn", 'b', ps_wifimax_btn, ps_wifimax_lbl, 2},   // power: Sparmodus stark (Ruhe)
    {"ps_h_wifimax", 'i', nullptr, ps_h_wifimax, 2},   // power: Hinweis
    {"ps_wifi_off_btn", 'b', ps_wifi_off_btn, ps_off_lbl, 2},   // power: WLAN aus im Ruhezustand
    {"ps_h_off", 'i', nullptr, ps_h_off, 2},   // power: Hinweis
    {"ps_wifis_sld", 's', ps_wifis_sld, ps_wifis_lbl, 2},   // power: WLAN aus nach
    {"ps_h_offs", 'i', nullptr, ps_h_offs, 2},   // power: Hinweis
    {"ps_wifitv_sld", 's', ps_wifitv_sld, ps_wifitv_lbl, 2},   // power: WLAN aus bei TV an
    {"ps_h_wifitv", 'i', nullptr, ps_h_wifitv, 2},   // power: Hinweis
    {"ps_cpu_sld", 's', ps_cpu_sld, ps_cpu_lbl, 2},   // power: Prozessor drosseln
    {"ps_h_cpu", 'i', nullptr, ps_h_cpu, 2},   // power: Hinweis
    {"ps_light_btn", 'b', ps_light_btn, ps_light_lbl, 2},   // power: Leichtschlaf
    {"ps_wfest_btn", 'b', ps_wfest_btn, ps_wfest_lbl, 2},   // power: WLAN-Schnellstart
    {"ps_wfrueh_btn", 'b', ps_wfrueh_btn, ps_wfrueh_lbl, 2},   // power: WLAN früh einschalten
    {"ps_nurble_btn", 'b', ps_nurble_btn, ps_nurble_lbl, 2},   // power: Nur Bluetooth
    {"ps_nurble_h", 'i', nullptr, ps_nurble_h, 2},   // power: WLAN bleibt aus, Home Assistant, Bilder und Sprache laufen über Bluetooth. Für Updates schaltet das Wartungsfenster das WLAN kurz ein.
    {"ps_bleidle_btn", 'b', ps_bleidle_btn, ps_bleidle_lbl, 2},   // power: BLE-Verbindung langsam
    {"ps_h_ble", 'i', nullptr, ps_h_ble, 2},   // power: Hinweis
    {"ps_bles_sld", 's', ps_bles_sld, ps_bles_lbl, 2},   // power: BLE langsam nach
    {"ps_h_bles", 'i', nullptr, ps_h_bles, 2},   // power: Hinweis
    {"motion_mode_lbl", 'i', nullptr, motion_mode_lbl, 2},   // power: Aufwecken bei
    {"mm_0", 'b', mm_0, mm_0_mk, 2},   // power: Aus
    {"mm_1", 'b', mm_1, mm_1_mk, 2},   // power: Bewegung
    {"mm_2", 'b', mm_2, mm_2_mk, 2},   // power: Hochheben
    {"h_motion", 'i', nullptr, h_motion, 2},   // power: Hinweis
    {"ms_sld", 's', ms_sld, motion_sens_lbl, 2},   // power: Empfindlichkeit
    {"h_sens", 'i', nullptr, h_sens, 2},   // power: Hinweis
    {"keys_wake_btn", 'b', keys_wake_btn, keys_wake_lbl, 2},   // power: Tasten wecken
    {"h_keys", 'i', nullptr, h_keys, 2},   // power: Hinweis
    {"menue_weckt_btn", 'b', menue_weckt_btn, menue_weckt_lbl, 2},   // power: Menütaste bei dunklem Display
    {"h_menue_weckt", 'i', nullptr, h_menue_weckt, 2},   // power: Hinweis
    {"dim_wake_btn", 'b', dim_wake_btn, dim_wake_lbl, 2},   // power: Aufhellen bei Bewegung
    {"h_dim_wake", 'i', nullptr, h_dim_wake, 2},   // power: Hinweis
    {"dim_hold_btn", 'b', dim_hold_btn, dim_hold_lbl, 2},   // power: Nicht abdunkeln bei Bewegung
    {"h_dim_hold", 'i', nullptr, h_dim_hold, 2},   // power: Hinweis
    {"ds_sld", 's', ds_sld, dim_sens_lbl, 2},   // power: Empfindlichkeit (beide oben)
    {"h_dim_sens", 'i', nullptr, h_dim_sens, 2},   // power: Hinweis
    {"klopf_btn", 'b', klopf_btn, klopf_lbl, 2},   // power: Doppelt klopfen weckt
    {"kl_sld", 's', kl_sld, klopf_sens_lbl, 2},   // power: Klopf-Empfindlichkeit
    {"h_klopf", 'i', nullptr, h_klopf, 2},   // power: Hinweis
    {"ps_touch_btn", 'b', ps_touch_btn, ps_touch_lbl, 2},   // power: Touch weckt
    {"ps_h_touch", 'i', nullptr, ps_h_touch, 2},   // power: Hinweis
    {"ps_touchms_sld", 's', ps_touchms_sld, ps_touchms_lbl, 2},   // power: Touch-Abfrage
    {"ps_h_touchms", 'i', nullptr, ps_h_touchms, 2},   // power: Hinweis
    {"ps_deep_btn", 'b', ps_deep_btn, ps_deep_lbl, 2},   // power: Tiefschlaf im Ruhezustand
    {"ps_h_deep", 'i', nullptr, ps_h_deep, 2},   // power: Hinweis
    {"ps_deepble_btn", 'b', ps_deepble_btn, ps_deepble_lbl, 2},   // power: Nur ohne Bluetooth
    {"ps_deeps_sld", 's', ps_deeps_sld, ps_deeps_lbl, 2},   // power: Tiefschlaf nach
    {"ps_h_deeps", 'i', nullptr, ps_h_deeps, 2},   // power: Hinweis
    {"ps_deeptv_sld", 's', ps_deeptv_sld, ps_deeptv_lbl, 2},   // power: Tiefschlaf bei TV an
    {"ps_h_deeptv", 'i', nullptr, ps_h_deeptv, 2},   // power: Hinweis
    {"ps_low_sld", 's', ps_low_sld, ps_low_lbl, 2},   // power: Sparmodus bei Akku unter
    {"ps_h_low", 'i', nullptr, ps_h_low, 2},   // power: Hinweis
    {"tile_assi", 'b', tile_assi, assi_lbl, 3},   // voice_set: Assistent
    {"h_assi", 'i', nullptr, h_assi, 3},   // voice_set: Hinweis
    {"tile_google_mode", 'b', tile_google_mode, google_mode_lbl, 3},   // voice_set: Google-Modus
    {"h_google_mode", 'i', nullptr, h_google_mode, 3},   // voice_set: Hinweis
    {"tile_tts", 'b', tile_tts, tts_lbl, 3},   // voice_set: Antwort hörbar
    {"h_tts", 'i', nullptr, h_tts, 3},   // voice_set: Hinweis
    {"tile_mic", 'b', tile_mic, mic_lbl, 3},   // voice_set: Mikrofon
    {"tile_mic_warm", 'b', tile_mic_warm, mic_warm_lbl, 3},   // voice_set: Mikrofon vorwärmen
    {"h_mic_warm", 'i', nullptr, h_mic_warm, 3},   // voice_set: Hinweis
    {"ble_state_lbl", 'i', nullptr, ble_state_lbl, 4},   // ble: Nicht verbunden
    {"ble_slot0", 'b', ble_slot0, ble_slot0_name, 4},   // ble: Platz 1
    {"ble_slot1", 'b', ble_slot1, ble_slot1_name, 4},   // ble: Platz 2
    {"ble_slot2", 'b', ble_slot2, ble_slot2_name, 4},   // ble: Platz 3
    {"ble_slot3", 'b', ble_slot3, ble_slot3_name, 4},   // ble: Platz 4
    {"ble_target_lbl", 'i', nullptr, ble_target_lbl, 4},   // ble: Platz 1
    {"ble_connect", 'b', ble_connect, nullptr, 4},   // ble: Verbinden
    {"ble_pair", 'b', ble_pair, nullptr, 4},   // ble: Koppeln
    {"ble_forget", 'b', ble_forget, nullptr, 4},   // ble: Löschen
    {"ble_eingabe_btn", 'b', ble_eingabe_btn, ble_eingabe_lbl, 4},   // ble: Eingabe
    {"h_plaetze", 'i', nullptr, h_plaetze, 4},   // ble: Hinweis
    {"ble_par_btn", 'b', ble_par_btn, ble_par_lbl, 4},   // ble: Parallel verbunden
    {"maus_tempo_sld", 's', maus_tempo_sld, maus_tempo_lbl, 4},   // ble: Zeigertempo
    {"maus_dpad_btn", 'b', maus_dpad_btn, maus_dpad_lbl, 4},   // ble: Steuerkreuz als Maus
    {"maus_dpad_sld", 's', maus_dpad_sld, maus_dpad_lbl2, 4},   // ble: Tempo Steuerkreuz
    {"maus_zurueck_btn", 'b', maus_zurueck_btn, maus_zurueck_lbl, 4},   // ble: Zurück-Taste
    {"h_maus_dpad", 'i', nullptr, h_maus_dpad, 4},   // ble: Hinweis
    {"ak_status", 'i', nullptr, ak_status, 5},   // akku: Status
    {"ak_pct", 'i', nullptr, ak_pct, 5},   // akku: Akkustand
    {"ak_volt", 'i', nullptr, ak_volt, 5},   // akku: Spannung
    {"ak_rate", 'i', nullptr, ak_rate, 5},   // akku: Rate jetzt (Chip)
    {"ak_r1", 'i', nullptr, ak_r1, 5},   // akku: Letzte Stunde
    {"ak_r24", 'i', nullptr, ak_r24, 5},   // akku: Letzte 24 h
    {"ak_rest", 'i', nullptr, ak_rest, 5},   // akku: Restlaufzeit
    {"ak_geladen", 'i', nullptr, ak_geladen, 5},   // akku: Zuletzt geladen
    {"ak_geladen2", 'i', nullptr, ak_geladen2, 5},   // akku: ak_geladen2
    {"ak_schlaf", 'i', nullptr, ak_schlaf, 5},   // akku: Tiefschlaf seit Ladung
    {"dock_an_btn", 'b', dock_an_btn, dock_an_lbl, 6},   // extras: Smart Display im Dock
    {"h_dock_an", 'i', nullptr, h_dock_an, 6},   // extras: Hinweis
    {"dock_disp_btn", 'b', dock_disp_btn, dock_disp_lbl, 6},   // extras: Display im Dock
    {"h_dock_disp", 'i', nullptr, h_dock_disp, 6},   // extras: Hinweis
    {"dock_von_sld", 's', dock_von_sld, dock_von_lbl, 6},   // extras: an ab
    {"dock_bis_sld", 's', dock_bis_sld, dock_bis_lbl, 6},   // extras: an bis
    {"musik_dock_btn", 'b', musik_dock_btn, musik_dock_lbl, 6},   // extras: Im Dock bei Musik
    {"h_musik_dock", 'i', nullptr, h_musik_dock, 6},   // extras: Hinweis
    {"musik_player_btn", 'b', musik_player_btn, musik_player_lbl, 6},   // extras: Nur wenn Player spielt
    {"h_musik_player", 'i', nullptr, h_musik_player, 6},   // extras: Hinweis
    {"tile_touchtest", 'b', tile_touchtest, nullptr, 7},   // service: Touch-Test
    {"h_touch", 'i', nullptr, h_touch, 7},   // service: Hinweis
    {"tile_button_test", 'b', tile_button_test, nullptr, 7},   // service: Tasten-Test
    {"h_btn", 'i', nullptr, h_btn, 7},   // service: Hinweis
    {"clean_dur_sld", 's', clean_dur_sld, clean_dur_lbl, 7},   // service: Reinigungsdauer
    {"h_cleandur", 'i', nullptr, h_cleandur, 7},   // service: Hinweis
    {"tile_clean", 'b', tile_clean, nullptr, 7},   // service: Reinigungsmodus
    {"h_clean", 'i', nullptr, h_clean, 7},   // service: Hinweis
    {"tile_sd", 'b', tile_sd, nullptr, 7},   // service: SD-Karte
    {"h_sd_tile", 'i', nullptr, h_sd_tile, 7},   // service: Hinweis
    {"ps_log_btn", 'b', ps_log_btn, ps_log_lbl, 7},   // service: Debug-Modus
    {"ps_h_log", 'i', nullptr, ps_h_log, 7},   // service: Hinweis
    {"testmod_btn", 'b', testmod_btn, testmod_lbl, 7},   // service: Testmodus (immer online)
    {"ps_h_test", 'i', nullptr, ps_h_test, 7},   // service: Hinweis
    {"tile_update", 'b', tile_update, fw_upd_lbl, 7},   // service: Firmware aktualisieren
    {"h_update", 'i', nullptr, h_update, 7},   // service: Hinweis
    {"tile_shutdown", 'b', tile_shutdown, nullptr, 7},   // service: Gerät herunterfahren
    {"tile_restart", 'b', tile_restart, restart_lbl, 7},   // service: Gerät neu starten
    {"h_restart", 'i', nullptr, h_restart, 7},   // service: Hinweis
    {"ak_graph", 'g', ak_graph->obj, nullptr, 5},   // akku: Verlauf 24 h
  };
  for (const auto &z : tab) if (k == z.k) return {z.t, z.o, z.w, z.g};
  return {};
}

// Werte der eingebauten Seiten auffrischen (ohne Seitenwechsel); maske = Bits der Gruppen (EinstRef::gruppe)
inline void auffrischen(uint32_t maske = 0xFFFFFFFFu) {
  if (maske & (1u << 0)) einst_bright->execute();
  if (maske & (1u << 1)) einst_show->execute();
  if (maske & (1u << 2)) einst_power->execute();
  if (maske & (1u << 3)) einst_voice_set->execute();
  if (maske & (1u << 4)) einst_ble->execute();
  if (maske & (1u << 5)) einst_akku->execute();
  if (maske & (1u << 6)) einst_extras->execute();
  if (maske & (1u << 7)) einst_service->execute();
}

}  // namespace einstellungen
