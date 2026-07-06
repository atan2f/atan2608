#pragma once

#include <vector>

#include "FmConfig.h"
#include "FmPatch.h"
#include "OpnaChip.h"

namespace opna {

// A single pending register write. port 0 = FM ch 1-3 / SSG / common;
// port 1 = FM ch 4-6. The key-on/off register (0x28) always uses port 0.
struct RegWrite {
  int port;
  int reg;
  int value;
};

// Pure translation from FmPatch + MIDI note to YM2608 register writes.
// No JUCE, no chip state - fully unit-testable with golden values.
class RegisterEncoder {
public:
  static constexpr int kNumFmChannels = 6;

  // Per-channel tone registers (0x30-0x90, 0xB0, 0xB4). Does NOT include the
  // global LFO register, frequency, or key-on. The pan controls the 0xB4 L/R
  // enable bits (chip-native Left/Center/Right).
  static void encodePatch(const FmPatch& patch,
                          int channel,
                          std::vector<RegWrite>& out,
                          FmPan pan = FmPan::Center);

  // Global LFO register (0x22), always on port 0.
  static RegWrite encodeLfo(const FmGlobal& global);

  // Frequency: appends 0xA4 (block/fnum-high) THEN 0xA0 (fnum-low); the high
  // byte must be latched before the low byte is committed.
  static void encodeFrequency(int midiNote,
                              int channel,
                              double clockHz,
                              std::vector<RegWrite>& out);

  // As encodeFrequency, but takes a fractional MIDI note so pitch bend can shift
  // the pitch continuously between semitones.
  static void encodeFrequencyBend(double midiNote,
                                  int channel,
                                  double clockHz,
                                  std::vector<RegWrite>& out);

  static RegWrite keyOn(int channel);
  static RegWrite keyOff(int channel);

  // Compute the YM2608 block/F-number pair for a (fractional) MIDI note.
  static void noteToFnum(double midiNote, double clockHz, int& block, int& fnum);

private:
  // Logical operator [op1..op4] -> register slot offset. The OPN hardware
  // interleaves the middle two operators (op1,op3,op2,op4 in register space).
  static constexpr int kSlotOffset[4] = {0, 8, 4, 12};
};

// Apply a register write to a live chip.
inline void applyWrite(OpnaChip& chip, const RegWrite& w) {
  if (w.port == 0)
    chip.writeReg0(w.reg, w.value);
  else
    chip.writeReg1(w.reg, w.value);
}

}  // namespace opna
