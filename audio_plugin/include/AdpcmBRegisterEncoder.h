#pragma once

#include <vector>

#include "OpnaChip.h"
#include "RegisterEncoder.h"  // for RegWrite

namespace opna {

// Drives the single ADPCM-B (Delta-T) channel, which plays back ADPCM data from
// external RAM. All registers are on the high port (0x100-0x10F). Playback rate
// (pitch) is set by delta-N: Fs = (chipClock/144) * deltaN / 65536.
class AdpcmBRegisterEncoder {
public:
  // ADPCM-B is clocked at the FM rate, chipClock/144.
  static constexpr double kAdpcmClockHz = OpnaChip::kClockHz / 144.0;

  // Address granularity when using 8-bit DRAM mode (address shift of 5).
  static constexpr int kAddressUnit = 32;

  // delta-N for a sample whose data was encoded from PCM at sampleRateHz, so it
  // plays at original pitch. adpcmClockHz is the effective ADPCM-B clock (varies
  // with the chip clock/prescale); it defaults to the prescale-6 value so the
  // existing call sites and golden tests are unchanged.
  static int rootDeltaN(double sampleRateHz, double adpcmClockHz = kAdpcmClockHz);

  // delta-N for a MIDI note relative to the sample's root note.
  static int noteDeltaN(int midiNote, int rootNote, int rootDeltaN);

  // As noteDeltaN, but takes a fractional MIDI note so pitch bend can shift the
  // playback rate continuously. (deltaN is the only pitch control; bending it is
  // varispeed, so pitch and playback speed move together -- inherent to the chip.)
  static int noteDeltaNBend(double midiNote, int rootNote, int rootDeltaN);

  // Configure start/end/limit/level/control for a sample occupying
  // [0, dataBytes) of RAM. pan is 0=L 1=C 2=R. Does not key on.
  static void configure(int dataBytes, int level, int pan,
                        std::vector<RegWrite>& out);

  // The end address (in kAddressUnit-byte units) for a sample of dataBytes.
  static int endUnitForBytes(int dataBytes);

  // Convert a playback fraction (0..1000 per-mille) of a dataBytes-long sample
  // into a start/end address in kAddressUnit-byte units, clamped to the sample.
  static int unitForPermille(int dataBytes, int permille);

  // Write the playback start/end window (regs 0x02-0x05) in address units. These
  // bound playback only -- they do not alter the sample bytes resident in RAM.
  // The chip latches them at key-on, so set them before keyOn().
  static void setRegion(int startUnit, int endUnit, std::vector<RegWrite>& out);

  // Control-2 byte (reg 0x01): pan bits + 8-bit external DRAM mode.
  static int control2(int pan);

  // Set the playback delta-N (pitch).
  static void setDeltaN(int deltaN, std::vector<RegWrite>& out);

  // Key-on. When loop is true the chip repeats start..end until key-off
  // (ctrl bit 4); otherwise it plays once.
  static RegWrite keyOn(bool loop = false);
  static RegWrite keyOff();
};

}  // namespace opna
