#pragma once

#include <vector>

#include "RegisterEncoder.h"  // for RegWrite
#include "RhythmPatch.h"

namespace opna {

// Translates rhythm notes/levels to YM2608 ADPCM-A registers (0x10-0x1D on
// port 0). Start/end addresses for the six drums are configured internally by
// the chip reset, so we only drive the key-on, total level and per-instrument
// pan/level registers.
class RhythmRegisterEncoder {
public:
  static constexpr int kNumInstruments = 6;

  // MIDI note -> rhythm instrument index (0..5), or -1 for unmapped notes.
  // Uses a General-MIDI-style drum map.
  static int noteToInstrument(int midiNote);

  // Total level (0x11) and per-instrument pan+level (0x18..0x1D).
  static void encodePatch(const RhythmPatch& patch, std::vector<RegWrite>& out);

  // Key-on / key-off (dump) for one instrument via the 0x10 control register.
  static RegWrite trigger(int instrument);
  static RegWrite stop(int instrument);
};

}  // namespace opna
