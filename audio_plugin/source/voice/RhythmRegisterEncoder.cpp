#include "RhythmRegisterEncoder.h"

namespace opna {

int RhythmRegisterEncoder::noteToInstrument(int midiNote) {
  switch (midiNote) {
    case 35:  // Acoustic Bass Drum
    case 36:  // Bass Drum 1
      return 0;
    case 38:  // Acoustic Snare
    case 40:  // Electric Snare
      return 1;
    case 49:  // Crash Cymbal 1
    case 51:  // Ride Cymbal 1
    case 52:  // Chinese Cymbal
    case 55:  // Splash Cymbal
    case 57:  // Crash Cymbal 2
    case 59:  // Ride Cymbal 2
      return 2;  // top cymbal
    case 42:  // Closed Hi-Hat
    case 44:  // Pedal Hi-Hat
    case 46:  // Open Hi-Hat
      return 3;
    case 41:
    case 43:
    case 45:
    case 47:
    case 48:
    case 50:  // toms
      return 4;
    case 37:  // Side Stick
    case 39:  // Hand Clap
      return 5;  // rim shot
    default:
      return -1;
  }
}

void RhythmRegisterEncoder::encodePatch(const RhythmPatch& patch,
                                        std::vector<RegWrite>& out) {
  out.push_back({0, 0x11, patch.totalLevel & 0x3F});
  for (int i = 0; i < kNumInstruments; ++i) {
    // 0x18+i: bit7 = L, bit6 = R (both = centre), bits0-4 = instrument level.
    const int panBits = patch.pan[i] == 0 ? 0x80 : patch.pan[i] == 2 ? 0x40 : 0xC0;
    out.push_back({0, 0x18 + i, panBits | (patch.level[i] & 0x1F)});
  }
}

RegWrite RhythmRegisterEncoder::trigger(int instrument) {
  return {0, 0x10, 1 << instrument};
}

RegWrite RhythmRegisterEncoder::stop(int instrument) {
  return {0, 0x10, 0x80 | (1 << instrument)};
}

}  // namespace opna
