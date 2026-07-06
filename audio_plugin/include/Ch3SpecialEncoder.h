#pragma once

#include <vector>

#include "RegisterEncoder.h"

namespace opna {

// Register encoding for the YM2608 channel-3 "special" (multi-frequency) mode.
//
// In special mode physical FM channel 3 runs its four operators at independent
// frequencies. The mode is selected by reg 0x27 bits 6-7 (value 1 =
// multi-frequency, value 2 = CSM which additionally retriggers from Timer A).
// Three of the operators read dedicated frequency registers (0xA8-0xAA /
// 0xAC-0xAE); the fourth keeps using the channel's normal CH3 registers
// (0xA2/0xA6).
//
// The operator -> frequency-register mapping is "scrambled" by the OPN hardware.
// Verified against ymfm (deps/ymfm-mame/src/ymfm_opn.cpp cache_operator_data and
// operator_map) combined with our logical-op interleave RegisterEncoder::
// kSlotOffset = {0,8,4,12}:
//
//   OP1 -> multi_block_freq(1) -> 0xA9 (fnum low) / 0xAD (block+high)
//   OP2 -> multi_block_freq(2) -> 0xAA / 0xAE
//   OP3 -> multi_block_freq(0) -> 0xA8 / 0xAC
//   OP4 -> normal CH3 frequency -> 0xA2 / 0xA6
//
// The golden tests pin this table.
//
// JUCE-free and chip-free so it is fully unit-testable with golden values.
class Ch3SpecialEncoder {
public:
  // Channel-3 select code used by the key-on register (0x28 low bits).
  static constexpr int kCh3KeyCode = 2;

  // Reg 0x27 mode select. mode: 0 = normal, 1 = multi-frequency, 2 = CSM.
  // The low bits (timer load/enable/reset) are written 0; this leaves Timer A
  // halted (CSM armed but silent). Use encodeCsmRun to start/stop the timer.
  static RegWrite encodeMode(int mode);

  // CSM run state (reg 0x27 = CSM mode bits + Timer A load bit). When running,
  // Timer A free-runs and auto-retriggers CH3's four operators at its overflow
  // rate; that rate is the pitch. run=false halts the timer (mode stays armed).
  //   running = 0x80 (CSM) | 0x01 (load Timer A) = 0x81
  //   halted  = 0x80
  // Writing the load bit only (re)starts a stopped timer; to force a fresh
  // restart, write halted then running (the processor's 0x28 retrigger gap does
  // not apply to CSM -- Timer A keys the operators, not reg 0x28).
  static RegWrite encodeCsmRun(bool run);

  // Timer A 10-bit value for a CSM fundamental pitch. The overflow (retrigger)
  // frequency is clock / ((1024 - NA) * 144) on the OPNA, so this inverts that:
  // NA = clamp(1024 - round(clockHz / (144 * f_note)), 0, 1023). NA=0 is the
  // floor (~54 Hz, near A1); notes below clamp there. Resolution coarsens with
  // pitch (few discrete NA steps high up), so top-octave tuning drifts slightly.
  static int noteToTimerA(double midiNote, double clockHz);

  // Append the Timer A value writes: reg 0x24 = NA upper 8 bits, 0x25 = lower 2.
  static void encodeTimerA(int na, std::vector<RegWrite>& out);

  // Append the two frequency-register writes (block/high THEN fnum-low, matching
  // the hardware latch order) for one logical operator of channel 3.
  static void encodeOperatorFrequency(double midiNote,
                                      int logicalOp,
                                      double clockHz,
                                      std::vector<RegWrite>& out);

  // Key register write for channel 3. opsOn is a 4-bit mask in OP1..OP4 order
  // (bit0 = OP1): the operators that should be ON. Pass 0 to key all four off.
  static RegWrite keyMask(int opsOn);
};

}  // namespace opna
