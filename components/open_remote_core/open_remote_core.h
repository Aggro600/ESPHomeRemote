#pragma once
// open_remote_core - Sammelheader, den ESPHome vor die erzeugten Lambdas
// einbindet (per cg.add_global in __init__.py). Enthaelt, was in einer Lambda
// nicht per #include erreichbar waere: die esp-idf-Header fuer Power-Management
// und Deep-Sleep, den RTC-Merker g_deep_active und die Multiroom-Lautsprecher-
// Tabelle fuer die Musik-Seite.

// ===== Power-Management / Deep-Sleep =========================================
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

// Ueberlebt den Deep-Sleep (RTC-Speicher). 0x5EEE = "Tiefschlaf laeuft" -> nach
// jedem Wake ps_deep wieder true setzen. Bei Power-On/Reset automatisch 0.
RTC_DATA_ATTR uint32_t g_deep_active;

// ===== Multiroom-Lautsprecher (Seite "Lautsprecher") ========================
#include <cctype>
#include <string>

// >>> HIER deine Multiroom-Lautsprecher eintragen <<<
// Reihenfolge = Reihenfolge der Schaltflaechen. Bei geaenderter Anzahl auch
// MUS_GRP_N und die Statuszeilen im Skript mus_grp_refresh (YAML) mitziehen.
static const int MUS_GRP_N = 7;
static const char *const MUS_GRP_ID[MUS_GRP_N] = {
    "media_player.lautsprecher_1",
    "media_player.lautsprecher_2",
    "media_player.lautsprecher_3",
    "media_player.lautsprecher_4",
    "media_player.lautsprecher_5",
    "media_player.lautsprecher_6",
    "media_player.lautsprecher_7",
};

// Sucht die nackte Kennung in der (als String durchgereichten) group_members-
// Liste; Treffer zaehlt nur, wenn dahinter kein weiteres Kennungszeichen steht.
inline bool mus_grp_ist_dabei(const std::string &liste, int i) {
  if (i < 0 || i >= MUS_GRP_N)
    return false;
  const std::string kennung = MUS_GRP_ID[i];
  size_t pos = liste.find(kennung);
  while (pos != std::string::npos) {
    const size_t ende = pos + kennung.size();
    const char c = ende < liste.size() ? liste[ende] : '\0';
    if (!(isalnum((unsigned char) c) || c == '_' || c == '.'))
      return true;
    pos = liste.find(kennung, pos + 1);
  }
  return false;
}
