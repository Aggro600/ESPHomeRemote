// Geraete-Einstellungen im Konfigurator (2026-10-16): alle Einstellungen der Remote (globals mit restore_value)
// werden mit Home Assistant abgeglichen - Konfigurator -> Remote (cfg.einst) und Remote -> Konfigurator
// (Ereignis esphome.remote_einstellungen). Logik in components/tasten_konfig (einst_feld / einst_uebernehmen_).
// Laufzeit-Zustaende (Aktivitaet, Testmodus, Firmware-Hash) gehoeren nicht dazu.
#pragma once

namespace einst_sync {

inline void einrichten() {
  auto *t = tk_cfg;
#define EINST_B(n) t->einst_feld(#n, true, []() { return (int) n->value(); }, [](int v) { n->value() = v != 0; })
#define EINST_I(n) t->einst_feld(#n, false, []() { return (int) n->value(); }, [](int v) { n->value() = v; })
  // Bildschirm
  EINST_I(disp_bri); EINST_B(kbd_light_on); EINST_I(kbd_bri); EINST_I(disp_timeout_s); EINST_I(ps_dim_s); EINST_I(ps_dim_pct);
  // Anzeige
  EINST_B(show_ble); EINST_B(show_wifi); EINST_B(show_batt); EINST_B(show_clock); EINST_B(show_assi_icon);
  EINST_B(show_motion); EINST_B(show_ha); EINST_B(cover_ganz_player); EINST_B(cover_ganz_home);
  EINST_I(mem_s); EINST_I(menu_close_s); EINST_B(sd_logos);
  // Energie & Wecken
  EINST_B(ps_wifi); EINST_B(ps_wifi_max); EINST_B(ps_wifi_off); EINST_I(ps_wifi_off_s); EINST_I(ps_wifi_off_tv_s);
  EINST_B(ps_cpu); EINST_I(ps_cpu_s); EINST_B(ps_light); EINST_B(wifi_fest); EINST_B(wlan_frueh);
  EINST_B(ps_bleidle); EINST_I(ps_ble_s);
  EINST_I(motion_mode); EINST_I(motion_sens); EINST_B(keys_wake_all); EINST_B(menue_weckt_nur);
  EINST_B(dim_wake); EINST_B(dim_hold_motion); EINST_I(dim_sens); EINST_B(klopf_wake); EINST_I(klopf_sens);
  EINST_B(ps_touch_wake); EINST_I(ps_touch_ms);
  EINST_B(ps_deep); EINST_B(deep_ohne_ble); EINST_I(ps_deep_s); EINST_I(ps_deep_tv_s); EINST_I(ps_low_pct);
  // Sprache
  EINST_I(assistant); EINST_I(google_mode); EINST_B(tts_speaker); EINST_B(mic_allowed); EINST_B(mic_vorwaermen);
  // Bluetooth
  EINST_B(ble_parallel);   // kb_ziffern: seit 2026-10-17 kein Schalter mehr (Tastatur je Platz)
  // Wartung
  EINST_I(clean_s); EINST_B(ps_log_debug);
  // Dock-Anzeige, Touchpad, Gesten (2026-10-17)
  EINST_B(dock_an); EINST_I(dock_disp); EINST_I(dock_von); EINST_I(dock_bis);
  EINST_I(maus_slots); EINST_I(voll_slots); EINST_I(maus_tempo);
  EINST_B(maus_dpad); EINST_I(maus_dpad_tempo); EINST_I(maus_zurueck);
  EINST_B(gesten_an); EINST_B(gesten_dunkel); EINST_I(gesten_sens);
#undef EINST_B
#undef EINST_I
  // Nur Bluetooth (WLAN aus): liegt im HA-Link (ha_tunnel), nicht in einem global
  t->einst_feld("nur_ble", true, []() { return (int) wlan_zugriff::g_nur_ble; },
                [](int v) { if (wlan_zugriff::g_set_nur_ble) { wlan_zugriff::g_set_nur_ble(v != 0); wlan_zugriff::g_nur_ble = v != 0; } });

  // Wirkung wie beim Umschalten auf der Remote: Seiten auffrischen (apply_*), dazu was die Seiten nicht erledigen
  t->einst_angewendet = []() {
    einstellungen::auffrischen();
    // Tastenlicht nur bei Display an einschalten - der Abgleich kommt oft im Hintergrund (Display aus, Tasten leuchteten)
    if (kbd_light_on->value() && !disp_off->value()) button_backlight->turn_on().perform();
    else if (!kbd_light_on->value()) button_backlight->turn_off().perform();
    esphome::logger::global_logger->set_log_level(ps_log_debug->value() ? ESPHOME_LOG_LEVEL_DEBUG : ESPHOME_LOG_LEVEL_INFO);
    g_deep_active = ps_deep->value() ? 0x5EEEu : 0;
    kb->set_parallel(ble_parallel->value());
    apply_mic_power->execute();
    sd_logo_pruefen->execute();
    // Bluetooth je Platz: Maus/volle Tastatur. Maus kommt dazu oder faellt ganz weg -> erst nach Neustart
    if (kb->set_slot_flags(maus_slots->value(), voll_slots->value())) neustart_gleich->execute();
  };
  t->einst_melden = [](const std::string &werte) { einst_an_ha->execute(werte); };
  ESP_LOGI("einst", "Abgleich mit dem Konfigurator eingerichtet");
}

}  // namespace einst_sync
