#pragma once
#include <cstdint>

namespace esphome {
namespace atv_voice {

// Standard IMA/DVI ADPCM encoder — the codec Google's "Voice over BLE Remote
// Control" spec uses for the Android TV Voice service. 4 bits per sample, so a
// 128-byte payload carries 256 samples (16 ms at 16 kHz, 32 ms at 8 kHz).
class ImaAdpcmEncoder {
 public:
  void reset() {
    this->predictor_ = 0;
    this->index_ = 0;
  }

  /// Encode one 16-bit sample into a 4-bit ADPCM code.
  uint8_t encode(int16_t sample) {
    const int16_t step = STEP_TABLE[this->index_];
    int32_t diff = (int32_t) sample - this->predictor_;
    uint8_t code = 0;
    if (diff < 0) {
      code = 8;
      diff = -diff;
    }
    int32_t diffq = step >> 3;
    if (diff >= step) {
      code |= 4;
      diff -= step;
      diffq += step;
    }
    if (diff >= (step >> 1)) {
      code |= 2;
      diff -= step >> 1;
      diffq += step >> 1;
    }
    if (diff >= (step >> 2)) {
      code |= 1;
      diffq += step >> 2;
    }

    if (code & 8)
      this->predictor_ -= diffq;
    else
      this->predictor_ += diffq;
    if (this->predictor_ > 32767)
      this->predictor_ = 32767;
    if (this->predictor_ < -32768)
      this->predictor_ = -32768;

    this->index_ += INDEX_TABLE[code];
    if (this->index_ < 0)
      this->index_ = 0;
    if (this->index_ > 88)
      this->index_ = 88;
    return code;
  }

  // The decoder needs the state the frame started from; both go into the frame
  // header, so a lost frame costs one frame instead of the rest of the stream.
  int16_t predictor() const { return (int16_t) this->predictor_; }
  uint8_t index() const { return (uint8_t) this->index_; }

 protected:
  int32_t predictor_{0};
  int8_t index_{0};

  static constexpr int8_t INDEX_TABLE[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
  static constexpr int16_t STEP_TABLE[89] = {
      7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
      31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
      130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
      544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
      2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
      9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
};

}  // namespace atv_voice
}  // namespace esphome
