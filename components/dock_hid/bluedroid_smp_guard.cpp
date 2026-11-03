// Schluessel-Schutz fuer gekoppelte Geraete (2026-11-03, eigene Umsetzung, MIT wie das Projekt).
#include "esphome/core/defines.h"
#ifdef USE_DOCK_HID_SMP_KEY_GUARD

// Fehler in ESP-IDF ab 5.5.6 (gemeldet als https://github.com/espressif/esp-idf/issues/19286, gefunden von markusg1234):
// Bluedroid loescht seit 5.5.6 die alten Schluessel eines Geraets erst in smp_check_auth_req(), also nachdem die
// Verbindung verschluesselt ist. Dieselbe Stelle laeuft aber auch, wenn ein schon gekoppeltes Geraet (Fernseher,
// Streamer ...) beim Wiederverbinden einfach mit seinem gespeicherten Schluessel verschluesselt - ganz ohne Kopplung.
// Dann sind die Schluessel im Speicher weg; bei der naechsten Trennung raeumt der Stack den leeren Eintrag ab
// ("Deleting unpaired device"), beim naechsten Verbinden antwortet das Dock auf die Schluesselanfrage mit "kenne ich
// nicht", und Android (btif_hh_virtual_unplug) loescht die Kopplung. Beim Sony-TV am Couchtisch-Dock: Kopplung nach
// jedem zweiten Funkabbruch weg (2026-11-02).
//
// Abhilfe: btm_sec_clear_ble_keys() wird umgeleitet (-Wl,--wrap aus __init__.py). Ruft smp_check_auth_req() sie auf,
// ohne dass in dieser Verbindung eine Kopplung angefragt wurde (smp_proc_pair_cmd), bleibt alles stehen. Bei einer
// echten neuen Kopplung und bei allen anderen Aufrufern (Entkoppeln, Platz vergessen) loescht sie wie immer.
// "Kopplung angefragt" endet mit ESP_GAP_BLE_AUTH_CMPL_EVT oder der Trennung (dock_hid.cpp: dock_hid_kopplung_vorbei).
// Die Strukturen von Bluedroid sind nicht oeffentlich - die Zeiger gehen nur durch.

#include <atomic>
#include "esphome/core/log.h"

namespace {
const char *const TAG_SCHUTZ = "dock_hid.smp";
std::atomic<bool> kopplung_angefragt{false};   // Pairing Request gesehen, Kopplung noch nicht abgeschlossen
bool in_auth_pruefung = false;                 // nur im BTU-Task benutzt
}  // namespace

// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)
extern "C" {
void __real_smp_proc_pair_cmd(void *cb, void *daten);
void __real_smp_check_auth_req(void *cb, void *daten);
void __real_btm_sec_clear_ble_keys(void *geraet);

void dock_hid_kopplung_vorbei() { kopplung_angefragt.store(false); }

void __wrap_smp_proc_pair_cmd(void *cb, void *daten) {
  kopplung_angefragt.store(true);
  __real_smp_proc_pair_cmd(cb, daten);
}

void __wrap_smp_check_auth_req(void *cb, void *daten) {
  in_auth_pruefung = true;
  __real_smp_check_auth_req(cb, daten);
  in_auth_pruefung = false;
}

void __wrap_btm_sec_clear_ble_keys(void *geraet) {
  if (in_auth_pruefung) {
    if (!kopplung_angefragt.load()) {
      ESP_LOGI(TAG_SCHUTZ, "Verschluesselt mit gespeichertem Schluessel - Schluessel bleibt");
      return;
    }
    ESP_LOGI(TAG_SCHUTZ, "Neue Kopplung - alte Schluessel werden ersetzt");
  }
  __real_btm_sec_clear_ble_keys(geraet);
}
}  // extern "C"
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,readability-identifier-naming)

#endif  // USE_DOCK_HID_SMP_KEY_GUARD
