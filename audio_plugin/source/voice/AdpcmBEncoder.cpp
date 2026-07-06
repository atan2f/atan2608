#include "AdpcmBEncoder.h"

namespace opna {

namespace {
constexpr int kStepMin = 127;
constexpr int kStepMax = 24576;
constexpr int kStepScale[8] = {57, 57, 57, 57, 77, 102, 128, 153};

int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

std::vector<uint8_t> AdpcmBEncoder::encode(const int16_t* pcm, int numSamples,
                                           int resetSample) {
  std::vector<uint8_t> out;
  out.reserve(static_cast<size_t>((numSamples + 1) / 2));

  int accumulator = 0;
  int step = kStepMin;
  uint8_t high = 0;
  bool haveHigh = false;

  for (int i = 0; i < numSamples; ++i) {
    // Re-arm the predictor at the requested seek/loop point so the bytes from
    // here on decode correctly when the chip restarts with a fresh reset there.
    if (i == resetSample) {
      accumulator = 0;
      step = kStepMin;
    }
    const int target = pcm[i];
    const int diff = target - accumulator;
    const int sign = diff < 0 ? 8 : 0;
    const int mag = diff < 0 ? -diff : diff;

    // Pick the 3-bit magnitude whose reconstructed delta is closest, using the
    // same integer math as the decoder so encoder and decoder stay in lockstep.
    int bestMag = 0;
    int bestErr = -1;
    for (int m = 0; m < 8; ++m) {
      const int recon = (2 * m + 1) * step / 8;
      int err = mag - recon;
      if (err < 0)
        err = -err;
      if (bestErr < 0 || err < bestErr) {
        bestErr = err;
        bestMag = m;
      }
    }

    int delta = (2 * bestMag + 1) * step / 8;
    if (sign)
      delta = -delta;
    accumulator = clampi(accumulator + delta, -32768, 32767);
    step = clampi(step * kStepScale[bestMag] / 64, kStepMin, kStepMax);

    const uint8_t nibble = static_cast<uint8_t>(sign | bestMag);
    if (!haveHigh) {
      high = static_cast<uint8_t>(nibble << 4);
      haveHigh = true;
    } else {
      out.push_back(static_cast<uint8_t>(high | nibble));
      haveHigh = false;
    }
  }
  if (haveHigh)
    out.push_back(high);  // pad final lone nibble (low nibble = 0)

  return out;
}

std::vector<int16_t> AdpcmBEncoder::decode(const uint8_t* data, int numBytes) {
  std::vector<int16_t> out;
  out.reserve(static_cast<size_t>(numBytes) * 2);

  int accumulator = 0;
  int step = kStepMin;

  for (int i = 0; i < numBytes; ++i) {
    const uint8_t nibbles[2] = {static_cast<uint8_t>((data[i] >> 4) & 0x0F),
                                static_cast<uint8_t>(data[i] & 0x0F)};
    for (const uint8_t nibble : nibbles) {
      const int mag = nibble & 0x07;
      int delta = (2 * mag + 1) * step / 8;
      if (nibble & 0x08)
        delta = -delta;
      accumulator = clampi(accumulator + delta, -32768, 32767);
      step = clampi(step * kStepScale[mag] / 64, kStepMin, kStepMax);
      out.push_back(static_cast<int16_t>(accumulator));
    }
  }
  return out;
}

}  // namespace opna
