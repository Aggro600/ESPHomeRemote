// Host-side check of the ADPCM path: encode a signal with the component's
// encoder exactly as AtvVoice::loop() does (frame header + nibble packing),
// decode it with an independent reference IMA decoder, and measure the error.
#include "../ima_adpcm.h"
#include <cmath>
#include <cstdio>
#include <vector>

using esphome::atv_voice::ImaAdpcmEncoder;

static const int STEP[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

// Reference decoder, written from the IMA ADPCM definition, not from the encoder.
struct Decoder {
  int predictor = 0, index = 0;
  int decode(int code) {
    int step = STEP[index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    predictor += (code & 8) ? -diff : diff;
    if (predictor > 32767) predictor = 32767;
    if (predictor < -32768) predictor = -32768;
    index += IDX[code];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    return predictor;
  }
};

int main() {
  const size_t FRAME_SAMPLES = 256, HEADER = 6;
  const size_t TOTAL = FRAME_SAMPLES * 40;  // ~0.6 s at 16 kHz

  std::vector<short> pcm(TOTAL);
  for (size_t i = 0; i < TOTAL; i++) {
    double t = (double) i / 16000.0;
    pcm[i] = (short) (9000 * sin(2 * M_PI * 300 * t) + 4000 * sin(2 * M_PI * 1200 * t));
  }

  ImaAdpcmEncoder enc;
  enc.reset();
  double err2 = 0, sig2 = 0;
  size_t frames = 0;

  for (size_t off = 0; off + FRAME_SAMPLES <= TOTAL; off += FRAME_SAMPLES) {
    unsigned char frame[HEADER + FRAME_SAMPLES / 2];
    frame[0] = (unsigned char) (frames >> 8);
    frame[1] = (unsigned char) (frames & 0xFF);
    short predictor = enc.predictor();
    frame[2] = (unsigned char) (predictor & 0xFF);
    frame[3] = (unsigned char) ((predictor >> 8) & 0xFF);
    frame[4] = enc.index();
    frame[5] = 0;
    for (size_t i = 0; i < FRAME_SAMPLES; i += 2) {
      unsigned char lo = enc.encode(pcm[off + i]);
      unsigned char hi = enc.encode(pcm[off + i + 1]);
      frame[HEADER + i / 2] = (unsigned char) (lo | (hi << 4));
    }

    // Decode this frame standing alone, the way a receiver would after a lost
    // frame: state comes from the header only.
    Decoder dec;
    dec.predictor = (short) ((unsigned short) frame[2] | ((unsigned short) frame[3] << 8));
    dec.index = frame[4];
    for (size_t i = 0; i < FRAME_SAMPLES; i++) {
      unsigned char byte = frame[HEADER + i / 2];
      int code = (i % 2 == 0) ? (byte & 0x0F) : (byte >> 4);
      int out = dec.decode(code);
      double d = (double) out - (double) pcm[off + i];
      err2 += d * d;
      sig2 += (double) pcm[off + i] * (double) pcm[off + i];
    }
    frames++;
  }

  double snr = 10.0 * log10(sig2 / err2);
  printf("frames=%zu  SNR=%.1f dB\n", frames, snr);
  if (snr < 20.0) {
    printf("FAIL: ADPCM output does not track the input\n");
    return 1;
  }
  printf("PASS: encoder output decodes back to the input, frames are self-contained\n");
  return 0;
}
