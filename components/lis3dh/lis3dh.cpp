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

void LIS3DHComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "LIS3DHTR Motion-Wake:");
  LOG_I2C_DEVICE(this);
  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup fehlgeschlagen - Chip nicht gefunden");
  }
}

}  // namespace lis3dh
}  // namespace esphome
