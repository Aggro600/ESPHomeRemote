// Einstellungsseite "Smart Display" (2026-10-17, page_extras) und die Maus-/Eingabe-Teile der Bluetooth-Seite
// (Zeigertempo, Eingabe je Bluetooth-Platz) - dieselben Werte wie im Konfigurator (Einstellungen > Dock-Anzeige und
// Bluetooth & Maus, Seite Bluetooth). Abgleich mit HA ueber open-remote-einst-sync.h.
#pragma once
#include "open-remote-stufen.h"

namespace extras {

// Stufen wie im Konfigurator (EINST_GRUPPEN)
static const int VON_V[] = {0, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18};
static const stufen::Tabelle VON = {VON_V, 11};
static const int BIS_V[] = {12, 16, 18, 19, 20, 21, 22, 23, 24};
static const stufen::Tabelle BIS = {BIS_V, 9};
static const int TEMPO_V[] = {50, 75, 100, 125, 150, 200, 250};
static const stufen::Tabelle TEMPO = {TEMPO_V, 7};

inline void wert(lv_obj_t *l, const char *t, bool aktiv) {
  lv_label_set_text(l, t);
  lv_obj_set_style_text_color(l, lv_color_hex(aktiv ? 0x22C55E : 0x8B94A3), LV_PART_MAIN);
}

inline void an_aus(lv_obj_t *l, bool an) { wert(l, an ? "AN" : "AUS", an); }

// Platz i: 0 = nichts, 1 = Maus, 2 = volle Tastatur, 3 = beides
inline int platz_modus(int i) {
  return ((maus_slots->value() >> i) & 1) | (((voll_slots->value() >> i) & 1) << 1);
}

inline const char *platz_text(int m) {
  static const char *T[] = {"Nur Tasten", "Maus", "Tastatur", "Maus + Tastatur"};
  return T[m & 3];
}

inline void touchpad_auffrischen();

// Seite "Smart Display" auffrischen (auch fuers Konfigurator-Menue, Skript einst_extras)
inline void auffrischen() {
  an_aus(dock_an_lbl, dock_an->value());
  static const char *DISP[] = {"Wie sonst", "Bleibt an", "Zeitfenster"};
  const int d = std::max(0, std::min(2, (int) dock_disp->value()));
  wert(dock_disp_lbl, DISP[d], d != 0);
  char b[24];
  snprintf(b, sizeof(b), "%d Uhr", (int) dock_von->value());
  wert(dock_von_lbl, b, d == 2);
  snprintf(b, sizeof(b), "%d Uhr", (int) dock_bis->value());
  wert(dock_bis_lbl, b, d == 2);
  lv_slider_set_value(dock_von_sld, stufen::index(VON, dock_von->value()), LV_ANIM_OFF);
  lv_slider_set_value(dock_bis_sld, stufen::index(BIS, dock_bis->value()), LV_ANIM_OFF);
  lv_label_set_text(h_dock_an, dock_an->value()
      ? "Steht die Remote im Dock, zeigt sie Uhrzeit, Wetter, Raumklima, Termine und den Ladestand. Herausnehmen = sofort wieder Fernbedienung. Was angezeigt wird, stellst du im Konfigurator unter Dockingstation ein."
      : "AUS: Im Dock bleibt die normale Fernbedienung.");
  lv_label_set_text(h_dock_disp, d == 0 ? "Wie sonst: Das Display geht im Dock nach der normalen Zeit aus. Antippen oder eine Taste zeigt die Anzeige wieder."
                               : d == 1 ? "Bleibt an: Im Dock geht das Display nie aus (Helligkeit wie immer über den Lichtsensor)."
                                        : "Zeitfenster: Im Dock bleibt das Display in der Zeit unten an, sonst geht es wie gewohnt aus.");
  static const char *HAD[] = {"Aus", "Ohne HA", "Immer"};
  const int hd = std::max(0, std::min(2, (int) ha_dock->value()));
  wert(ha_dock_lbl, HAD[hd], hd != 0);
  lv_label_set_text(h_ha_dock, hd == 2 ? "Immer: HA-Befehle der Tasten und des Menüs gehen per Funk ans Dock - sofort, auch direkt nach dem Aufwachen. Antwortet kein Dock, wie bisher direkt an HA."
                             : hd == 1 ? "Ohne HA: Nur solange die Remote noch nicht mit HA verbunden ist (z. B. gleich nach dem Aufwachen), übers Dock."
                                       : "Aus: HA-Befehle gehen nur über die eigene Verbindung (Bluetooth/WLAN) und warten nach dem Aufwachen auf sie.");
  an_aus(musik_dock_lbl, musik_dock->value());
  an_aus(musik_player_lbl, musik_player->value());
  lv_label_set_text(h_musik_dock, musik_dock->value()
      ? "Spielt der Player, zeigt die Remote im Dock statt des Smart Displays die Musik als Spektrum (über das Mikrofon). Ist die Musik aus, kommt nach 8 s das Smart Display zurück."
      : "AUS: Im Dock bleibt das Smart Display. Die Musik-Anzeige gibt es weiter über das Menü oder eine Taste.");
  lv_label_set_text(h_musik_player, musik_player->value()
      ? "Das Mikrofon hört nur zu, solange der Jetzt-läuft-Player spielt - sonst bleibt es aus."
      : "AUS: Die Anzeige hört immer zu, auch bei Musik, die HA nicht kennt (Radio, Plattenspieler).");

  touchpad_auffrischen();
}

// Bluetooth-Seite, Abschnitt "Maus & Tastatur je Geraet" (Skript einst_ble), dazu das Steuerkreuz im Touchpad
inline void touchpad_auffrischen() {
  char b[48];
  snprintf(b, sizeof(b), "%d %%", (int) maus_tempo->value());
  wert(maus_tempo_lbl, b, true);
  lv_slider_set_value(maus_tempo_sld, stufen::index(TEMPO, maus_tempo->value()), LV_ANIM_OFF);
  // Eingabeart des gewaehlten Platzes (Knopf "Eingabe" unter den Aktionen)
  {
    const int m = platz_modus(sel_slot->value() & 3);
    wert(ble_eingabe_lbl, platz_text(m), m != 0);
  }
  snprintf(b, sizeof(b), "%d %%", (int) maus_dpad_tempo->value());
  wert(maus_dpad_lbl2, b, maus_dpad->value());
  lv_slider_set_value(maus_dpad_sld, stufen::index(TEMPO, maus_dpad_tempo->value()), LV_ANIM_OFF);
  an_aus(maus_dpad_lbl, maus_dpad->value());
  static const char *ZU[] = {"Schließt Touchpad", "Zurück am Gerät", "Rechtsklick"};
  wert(maus_zurueck_lbl, ZU[std::max(0, std::min(2, (int) maus_zurueck->value()))], true);
  lv_label_set_text(h_maus_dpad, maus_dpad->value()
      ? "Im Touchpad bewegen die Pfeiltasten den Zeiger (halten = schneller), OK = Klick, OK lang = Rechtsklick, OK halten + Pfeile = Ziehen, Kanal +/- = Scrollen. Zurück lang = Knöpfe wählen (X, Umschalter, Klicktasten, Makros). Die Menütaste legt das Touchpad weg, nochmal Menütaste holt es zurück."
      : "AUS: Im Touchpad steuern die Tasten wie auf der Startseite das Gerät der Aktivität - den Zeiger bewegt nur der Bildschirm.");
  lv_label_set_text(h_plaetze, "Platz antippen, dann Verbinden / Koppeln / Löschen. Eingabe: Nur Tasten (Fernseher), Maus (Touchpad), Tastatur (Buchstaben, Makros) oder beides - danach das Gerät einmal neu koppeln.");
}

// Platz i weiterschalten: Nur Tasten -> Maus -> Tastatur -> Maus + Tastatur. Kommt die Maus erstmals dazu
// oder faellt ganz weg, startet die Remote 3 s spaeter neu (Bluetooth-Tabelle aendert sich).
inline void platz_weiter(int i) {
  const int m = (platz_modus(i) + 1) & 3;
  const int bit = 1 << i;
  maus_slots->value() = (maus_slots->value() & ~bit) | ((m & 1) ? bit : 0);
  voll_slots->value() = (voll_slots->value() & ~bit) | ((m & 2) ? bit : 0);
  if (kb->set_slot_flags(maus_slots->value(), voll_slots->value())) neustart_gleich->execute();
  auffrischen();
  refresh_ble_slots->execute();
}

// Firmware-Update (Wartung & Tests): Stand anzeigen. stufe 0 = Info, 1 = laeuft, 2 = fertig, 3 = Fehler.
// Leerer Text = Erklaerung. Der Stand bleibt bis zum Neustart (nach einem Update startet die Remote ohnehin neu).
inline void fw_status(const std::string &text, int stufe) {
  stufe = std::max(0, std::min(3, stufe));
  fw_upd_text->value() = text;
  fw_upd_stufe->value() = stufe;
  static const uint32_t FARBE[] = {0x8B94A3, 0x3B82F6, 0x22C55E, 0xEF4444};
  static const char *ZEICHEN[] = {">", "läuft", "fertig", "Fehler"};
  lv_label_set_text(fw_upd_lbl, ZEICHEN[stufe]);
  lv_obj_set_style_text_color(fw_upd_lbl, lv_color_hex(FARBE[stufe]), LV_PART_MAIN);
  lv_label_set_text(h_update, text.empty()
      ? "Holt die neueste Firmware aus Home Assistant und spielt sie auf. Die Remote bleibt dafür wach, das dauert einige Minuten. Danach startet sie neu. Im Konfigurator kannst du ein Update auch vormerken: Es startet, sobald die Remote das nächste Mal online ist."
      : text.c_str());
}

}  // namespace extras
