#include "lis3dh.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace lis3dh {

static const char *const TAG = "lis3dh";

// Standard-LIS3DH-Register (ST-Datenblatt). I2C-Adresse 0x19 ist gegen die
// Rev6-Netzliste verifiziert ("LIS3DH accelerometer at 0x19").
static const uint8_t REG_CTRL_REG1 = 0x20;
static const uint8_t REG_CTRL_REG2 = 0x21;
static const uint8_t REG_CTRL_REG3 = 0x22;
static const uint8_t REG_CTRL_REG4 = 0x23;
static const uint8_t REG_CTRL_REG5 = 0x24;
static const uint8_t REG_REFERENCE = 0x26;
static const uint8_t REG_INT1_CFG = 0x30;
static const uint8_t REG_INT1_SRC = 0x31;
static const uint8_t REG_INT1_THS = 0x32;
static const uint8_t REG_INT1_DURATION = 0x33;

// Motion-Wake-Sequenz, 1:1 aus der OpenRemote-Firmware (configureLis3dhMotionWake,
// OpenRemote_1.0.ino ~6708). Der entscheidende Punkt: OHNE Hochpassfilter sieht
// der Interrupt-Generator dauerhaft den 1g-Schwerkraftvektor auf der vertikalen
// Achse und meldet permanent "Bewegung". CTRL_REG2 = 0x09 (HPIS1 + FDS) leitet
// die hochpassgefilterten Daten in den INT1-Generator; ein Dummy-Read von
// REFERENCE nach ein paar ruhigen Samples legt die Nulllage (= aktuelle
// Schwerkraftrichtung) fest.
void LIS3DHComponent::setup() {
  // Routing und INT1-Config erst leeren, dann Filter aufsetzen, dann scharf
  // schalten - sonst kann der Einschwing-Transient sofort auslösen.
  if (!this->write_byte(REG_CTRL_REG3, 0x00)) {
    ESP_LOGE(TAG, "LIS3DHTR nicht gefunden");
    this->mark_failed();
    return;
  }
  this->write_byte(REG_INT1_CFG, 0x00);

  // CTRL_REG1 = 0x3F: 25 Hz Low-Power-Modus (LPen=1), Z/Y/X aktiv.
  this->write_byte(REG_CTRL_REG1, 0x3F);
  // CTRL_REG2 = 0x09: HPIS1 (Hochpass in den INT1-Generator) + FDS.
  this->write_byte(REG_CTRL_REG2, 0x09);
  // CTRL_REG4 = 0x80: BDU=1, +-2g.
  this->write_byte(REG_CTRL_REG4, 0x80);
  // CTRL_REG5 = 0x00: NICHT gelatcht - die INT1-Leitung folgt dem Bewegungs-
  // zustand in Echtzeit (HIGH bei Bewegung, LOW bei Stillstand). Für einen
  // laufenden binary_sensor besser als der gelatchte Modus der Firmware, der
  // ein Lesen von INT1_SRC pro Ereignis bräuchte.
  this->write_byte(REG_CTRL_REG5, 0x00);
  // CTRL_REG6 = 0x00: INT1 aktiv-HIGH. Muss ausdruecklich gesetzt werden: vor dem
  // Tiefschlaf stellt deep_sleep_powerdown auf aktiv-LOW (ext1 weckt nur bei LOW), und
  // der LIS3DH haengt am Dauerstrom - ohne diese Zeile bliebe die Leitung nach dem
  // Aufwachen invertiert (Ruhe = "Bewegung"), bis der Akku einmal ab war.
  this->write_byte(0x25, 0x00);
  // Schwelle (~16 mg/LSB bei +-2g) und Mindestdauer (in ODR-Takten).
  this->write_byte(REG_INT1_THS, this->threshold_);
  this->write_byte(REG_INT1_DURATION, this->duration_);

  // Hochpassfilter einschwingen lassen, dann Nulllage aus den ruhigen Samples
  // festlegen. Ohne das wacht das Gerät sofort nach dem Boot "auf".
  delay(400);
  uint8_t ignored = 0;
  this->read_byte(REG_REFERENCE, &ignored);  // HPF-Referenz = aktuelle Lage
  delay(80);
  this->read_byte(REG_INT1_SRC, &ignored);   // evtl. anstehende Flanke löschen

  // Jetzt scharf: High-Event auf X, Y ODER Z; IA1 auf INT1-Pin routen.
  this->write_byte(REG_INT1_CFG, 0x2A);
  this->write_byte(REG_CTRL_REG3, 0x40);

  uint8_t src = 0;
  this->read_byte(REG_INT1_SRC, &src);
  ESP_LOGCONFIG(TAG, "Motion-Wake scharf (Schwelle %u = ~%u mg, INT1_SRC=0x%02X)",
                this->threshold_, this->threshold_ * 16u, src);
}

// ---- Betrieb: Ruck + Doppelklopfen -----------------------------------------------------------
// 200 Hz: schnell genug fuer ein Klopfen (Spitze nur ~10 ms). Im Betrieb Normalmodus (10 Bit,
// weniger Rauschen als Low-Power), vor dem Tiefschlaf Low-Power (weniger Strom).
static const uint8_t REG_CTRL_REG6 = 0x25;
static const uint8_t REG_CLICK_CFG = 0x38;
static const uint8_t REG_CLICK_SRC = 0x39;
static const uint8_t REG_CLICK_THS = 0x3A;
static const uint8_t REG_TIME_LIMIT = 0x3B;
static const uint8_t REG_TIME_LATENCY = 0x3C;
static const uint8_t REG_TIME_WINDOW = 0x3D;

void LIS3DHComponent::klopf_register_(uint8_t klopf_ths) {
  // Doppelklopfen auf allen Achsen (XD/YD/ZD). Zeiten in Takten zu 5 ms (200 Hz):
  // ein Klopfer max. 60 ms, danach 100 ms Ruhe, der zweite muss binnen 400 ms kommen.
  this->write_byte(REG_CLICK_CFG, 0x2A);
  this->write_byte(REG_CLICK_THS, 0x80 | (klopf_ths & 0x7F));  // Bit 7: festhalten bis CLICK_SRC gelesen
  this->write_byte(REG_TIME_LIMIT, 12);
  this->write_byte(REG_TIME_LATENCY, 20);
  this->write_byte(REG_TIME_WINDOW, 80);
}

void LIS3DHComponent::configure_runtime(uint8_t ruck_ths, uint8_t klopf_ths) {
  if (this->is_failed()) return;
  this->write_byte(REG_CTRL_REG3, 0x00);    // nichts auf den Pin - im Betrieb wird gepollt
  this->write_byte(REG_INT1_CFG, 0x00);
  this->write_byte(REG_CTRL_REG1, 0x67);    // 200 Hz Normalmodus, X/Y/Z
  this->write_byte(REG_CTRL_REG2, 0x05);    // Hochpass nur fuer INT1 + Klopfen (FDS=0: Rohdaten bleiben)
  this->write_byte(REG_CTRL_REG4, 0x80);    // BDU, +-2 g
  this->write_byte(REG_CTRL_REG5, this->fifo_ ? 0x48 : 0x08);   // LIR_INT1 (+ FIFO_EN fuer Gesten)
  this->write_byte(0x2E, this->fifo_ ? 0x80 : 0x00);             // FIFO: Stream bzw. Bypass
  this->write_byte(REG_CTRL_REG6, 0x00);    // aktiv-HIGH (vor dem Tiefschlaf wird umgestellt)
  this->write_byte(REG_INT1_THS, ruck_ths & 0x7F);
  // 2 Takte (10 ms): ein Ruck dauert 50-100 ms und wird sicher erkannt, eine einzelne
  // Rausch-Spitze nicht.
  this->write_byte(REG_INT1_DURATION, 2);
  this->ruck_ths_ = ruck_ths;
  this->klopf_register_(klopf_ths);
  delay(20);                                // Hochpass einschwingen lassen
  uint8_t d = 0;
  this->read_byte(REG_REFERENCE, &d);       // Nulllage = aktuelle Lage
  this->write_byte(REG_INT1_CFG, 0x2A);     // X/Y/Z hoch, ODER
  this->read_byte(REG_INT1_SRC, &d);        // Einschwing-Ereignisse verwerfen
  this->read_byte(REG_CLICK_SRC, &d);
}

void LIS3DHComponent::set_ruck_threshold(uint8_t ths) {
  if (this->is_failed() || ths == this->ruck_ths_) return;
  this->write_byte(REG_INT1_THS, ths & 0x7F);
  this->ruck_ths_ = ths;
}

bool LIS3DHComponent::poll_events(bool &ruck, bool &doppel) {
  ruck = doppel = false;
  if (this->is_failed()) return false;
  uint8_t src = 0, csrc = 0;
  if (!this->read_byte(REG_INT1_SRC, &src) || !this->read_byte(REG_CLICK_SRC, &csrc)) return false;
  ruck = (src & 0x40) != 0;                 // IA
  doppel = (csrc & 0x60) == 0x60;           // IA + DClick
  return true;
}

void LIS3DHComponent::arm_for_sleep(bool ruck, uint8_t ruck_ths, bool klopf, uint8_t klopf_ths, bool hoch, uint8_t hoch_ths) {
  if (hoch) ruck = false;   // Hochheben ersetzt den Ruck (gleicher Interrupt-Ausgang INT1)
  if (this->is_failed()) return;
  this->write_byte(REG_CTRL_REG3, 0x00);
  this->write_byte(REG_INT1_CFG, 0x00);
  this->write_byte(REG_CLICK_CFG, 0x00);
  // Ohne Klopfen 50 Hz Low-Power (0x4F, wie die Original-Firmware im Tiefschlaf; vorher 10 Hz = 0x2F):
  // ein Tippen auf das Display ist nur ein kurzer Stoss und fiel bei 10 Hz oft zwischen zwei Messungen.
  // Mehrverbrauch laut Datenblatt ~3 uA. Mit Klopfen braucht es die 200 Hz.
  this->write_byte(REG_CTRL_REG1, klopf ? 0x6F : 0x4F);
  // Hochpass: Ruck und Klopfen (0x05); bei Hochheben NICHT fuer INT1 - die Lage braucht die Schwerkraft (0x04)
  this->write_byte(REG_CTRL_REG2, hoch ? 0x04 : 0x05);
  this->write_byte(REG_CTRL_REG4, 0x80);
  this->write_byte(REG_CTRL_REG5, 0x00);    // Ruck NICHT festhalten: die Leitung folgt der Bewegung (FIFO aus)
  this->write_byte(0x2E, 0x00);             // FIFO: Bypass
  this->write_byte(REG_CTRL_REG6, 0x02);    // aktiv-LOW - ext1 weckt bei LOW
  this->write_byte(REG_INT1_THS, (hoch ? hoch_ths : ruck_ths) & 0x7F);
  // Mindestdauer: bei 200 Hz 4 Takte (20 ms) gegen Einzel-Spitzen, bei 50 Hz reicht eine Messung. Hochheben: die neue
  // Lage muss ~0,3 s anliegen (gegen Wackeln und Anstossen).
  this->write_byte(REG_INT1_DURATION, hoch ? (klopf ? 60 : 15) : (klopf ? 4 : 0));
  if (klopf) this->klopf_register_(klopf_ths);        // Klopfen bleibt festgehalten -> Pin bleibt LOW
  // Einschwingen wie die Original-Firmware (configureLis3dhDeepSleepWake): nach dem Umstellen
  // erst ruhige Messungen sammeln, dann die Hochpass-Referenz setzen und alte Ereignisse loeschen,
  // ERST DANN den Interrupt freigeben. Ohne diese Pause sah der Einschwingvorgang wie Bewegung aus
  // und weckte die Remote sofort wieder (Weckprotokoll 2026-10-03: Tiefschlaf, 4 s spaeter wach).
  uint8_t d = 0;
  delay(400);
  this->read_byte(REG_REFERENCE, &d);
  delay(80);
  this->read_byte(REG_INT1_SRC, &d);
  this->read_byte(REG_CLICK_SRC, &d);
  if (ruck) this->write_byte(REG_INT1_CFG, 0x2A);
  if (hoch) this->write_byte(REG_INT1_CFG, 0x7F);   // 6D-Bewegung: Lagewechsel auf einer der sechs Achsenrichtungen
  this->write_byte(REG_CTRL_REG3, ((ruck || hoch) ? 0x40 : 0x00) | (klopf ? 0x80 : 0x00));
}

void LIS3DHComponent::set_fifo(bool an) {
  if (this->is_failed() || an == this->fifo_) return;
  this->fifo_ = an;
  this->write_byte(REG_CTRL_REG5, an ? 0x48 : 0x08);
  this->write_byte(0x2E, 0x00);             // erst Bypass: FIFO leeren
  if (an) this->write_byte(0x2E, 0x80);     // dann Stream
}

int LIS3DHComponent::fifo_lesen(int16_t *xyz, int max) {
  if (this->is_failed() || !this->fifo_) return -1;
  uint8_t src = 0;
  if (!this->read_byte(0x2F, &src)) return -1;
  int n = src & 0x1F;
  if (src & 0x40) n = 32;                   // uebergelaufen: voll
  if (n > max) n = max;
  if (n <= 0) return 0;
  uint8_t d[6 * 32];
  if (!this->read_bytes(0x28 | 0x80, d, 6 * n)) return -1;
  for (int i = 0; i < n; i++)
    for (int a = 0; a < 3; a++)
      xyz[i * 3 + a] = ((int16_t) ((uint16_t) d[i * 6 + a * 2 + 1] << 8 | d[i * 6 + a * 2])) >> 4;
  return n;
}

void LIS3DHComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LIS3DHTR Motion-Wake:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup fehlgeschlagen - Chip nicht gefunden");
  }
}

}  // namespace lis3dh
}  // namespace esphome
