#pragma once

#include <cstdint>
#include <vector>

namespace opna {

// Encodes 16-bit mono PCM into YM2608 ADPCM-B (Delta-T) nibbles, two per byte
// with the high nibble first (the order ymfm's decoder expects). The encoder
// mirrors ymfm's decoder step math exactly so playback tracks the input.
class AdpcmBEncoder {
public:
  // Encode numSamples of PCM. resetSample, when >= 0, forces the encoder's
  // predictor/step back to their key-on initial state (accumulator 0, step
  // minimum) at that sample index -- so a decoder that begins reading there with
  // a fresh reset (as the chip does at a non-zero start address, and at every
  // loop point) reconstructs the signal correctly instead of ramping up from
  // silence. -1 (the default) encodes one continuous stream from sample 0.
  static std::vector<uint8_t> encode(const int16_t* pcm, int numSamples,
                                     int resetSample = -1);

  // Inverse of encode(): decode ADPCM-B nibbles (two per byte, high first) back
  // to 16-bit PCM. Used to reconstruct a waveform for display when a saved
  // sample is restored from a preset. Yields numBytes*2 samples.
  static std::vector<int16_t> decode(const uint8_t* data, int numBytes);
};

}  // namespace opna
