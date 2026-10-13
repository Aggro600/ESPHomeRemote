// IR-Sender/-Empfaenger nur bei Bedarf aktiv (2026-10-07, Strom).
// ESPHome legt beide RMT-Kanaele beim Start an und laesst sie dauerhaft "enabled". ESP-IDF haelt dafuer eine
// Stromspar-Sperre (pm_lock "rmt_0_0", CPU_FREQ_MAX) - der Prozessor kam dadurch NIE in den Leichtschlaf
// (pm_bericht: light_sleep_counts 0, CPU_MAX 99 %). Hier werden die Kanaele nur fuer die Dauer eines Sendevorgangs
// (remote_transmitter on_transmit/on_complete) bzw. waehrend des IR-Lernens (Empfaenger-Strom an) eingeschaltet.
//
// Die Kanal-Handles sind in ESPHome "protected". Zugriff ueber die explizite Template-Instanziierung (dort gilt die
// Zugriffspruefung nicht) - ohne die Komponenten zu kopieren.
#pragma once
#include "esphome/components/remote_receiver/remote_receiver.h"
#include "esphome/components/remote_transmitter/remote_transmitter.h"
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>

namespace rmt_bedarf {

template<typename Tag, typename Tag::type M> struct Zugriff {
  friend typename Tag::type hole(Tag) { return M; }
};
struct RxKanal {
  using type = rmt_channel_handle_t esphome::remote_receiver::RemoteReceiverComponent::*;
  friend type hole(RxKanal);
};
struct RxSpeicher {
  using type = esphome::remote_receiver::RemoteReceiverComponentStore esphome::remote_receiver::RemoteReceiverComponent::*;
  friend type hole(RxSpeicher);
};
struct TxKanal {
  using type = rmt_channel_handle_t esphome::remote_transmitter::RemoteTransmitterComponent::*;
  friend type hole(TxKanal);
};
template struct Zugriff<RxKanal, &esphome::remote_receiver::RemoteReceiverComponent::channel_>;
template struct Zugriff<RxSpeicher, &esphome::remote_receiver::RemoteReceiverComponent::store_>;
template struct Zugriff<TxKanal, &esphome::remote_transmitter::RemoteTransmitterComponent::channel_>;

inline bool &tx_an() { static bool an = true; return an; }   // nach setup() aktiv
inline bool &rx_an() { static bool an = true; return an; }

// Sender: vor dem Senden an (on_transmit), danach aus (on_complete)
inline void sender(esphome::remote_transmitter::RemoteTransmitterComponent *t, bool an) {
  if (t == nullptr || an == tx_an()) return;
  rmt_channel_handle_t h = t->*hole(TxKanal());
  if (h == nullptr) return;
  if ((an ? rmt_enable(h) : rmt_disable(h)) == ESP_OK) tx_an() = an;
}

// Empfaenger: nur waehrend des IR-Lernens. Beim Einschalten den Empfang neu anstossen (wie in setup()).
inline void empfaenger(esphome::remote_receiver::RemoteReceiverComponent *r, bool an) {
  if (r == nullptr || an == rx_an()) return;
  rmt_channel_handle_t h = r->*hole(RxKanal());
  if (h == nullptr) return;
  if (!an) {
    if (rmt_disable(h) == ESP_OK) rx_an() = false;
    return;
  }
  if (rmt_enable(h) != ESP_OK) return;
  auto &st = r->*hole(RxSpeicher());
  st.buffer_write = 0;
  st.buffer_read = 0;
  st.overflow = false;
  rmt_receive(h, (uint8_t *) st.buffer + sizeof(rmt_rx_done_event_data_t), st.receive_size, &st.config);
  rx_an() = true;
}

}  // namespace rmt_bedarf
