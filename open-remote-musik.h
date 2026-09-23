// Die Lautsprecher der Multiroom-Gruppe "media_player.multiroom_audio".
//
// Warum eine eigene Datei: die Tabelle wird an drei Stellen gebraucht (Anzeige
// auffrischen, Umschalten, alle rein/raus). In einem Lambda liesse sie sich nur
// durch Abschreiben teilen, und beim naechsten Umbenennen eines Geraets waere
// eine der Kopien vergessen worden.
//
// Die Reihenfolge bestimmt zugleich die Reihenfolge der Schaltflaechen auf der
// Seite "Lautsprecher". Wer hier etwas einfuegt, muss MUS_GRP_N und die Liste
// der Statuszeilen im Skript mus_grp_refresh mitziehen.

#pragma once

#include <cctype>
#include <string>

static const int MUS_GRP_N = 7;

static const char *const MUS_GRP_ID[MUS_GRP_N] = {
    "media_player.kuchen_lautsprecher_l",
    "media_player.kuchen_lautsprecher_r",
    "media_player.badezimmer_lautsprecher",
    "media_player.fernseher_im_raum_wohnzimmer_2",
    "media_player.schlafzimmer_lautsprecher",
    "media_player.tablet_wohnzimmer_3",
    "media_player.tablet_kuche_music_assi",
};

// Tablet Flur steht bewusst NICHT in der Liste. Music Assistant fasst nur
// Geraete derselben Art zusammen und lehnt es ab:
//   "Player I01RUkMtED8... can not be grouped with Multiroom Audio"
// Ein Knopf dafuer haette also nur einen Fehlschlag ausgeloest.

// Home Assistant reicht Attribute als Zeichenkette durch. Ob dabei die
// Python-Schreibweise mit einfachen oder JSON mit doppelten Anfuehrungszeichen
// herauskommt, laesst sich von der Fernbedienung aus nicht nachlesen - also
// wird auf keins von beidem gewettet. Gesucht wird die nackte Kennung, und ein
// Treffer zaehlt nur, wenn direkt dahinter kein weiteres Kennungszeichen
// steht. Sonst wuerde "...lautsprecher_l" auch in "...lautsprecher_links"
// anschlagen.
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
