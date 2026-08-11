#pragma once

#include <array>

#include "Routing.h"

namespace opna {

// Number of FM channels on the chip and the number of timbre groups the plugin
// exposes (one per channel at most).
constexpr int kNumFmChannels = 6;
constexpr int kNumFmGroups = 6;

// Chip-native panning: the YM2608 only has per-channel Left/Right enable bits
// (register 0xB4 bits 7/6). There is no continuous pan.
enum class FmPan { Left, Center, Right };

// Maps a pan position to the 0xB4 L/R enable bits (bit7 = Left, bit6 = Right).
inline int panBits(FmPan p) {
  switch (p) {
    case FmPan::Left:   return 0x80;
    case FmPan::Right:  return 0x40;
    case FmPan::Center: return 0xC0;
  }
  return 0xC0;
}

// Per-hardware-channel assignment. group 0 = channel off (silent); 1..kNumFmGroups
// = the timbre group this channel belongs to. Channels sharing a group share that
// group's patch + routing and together form the group's polyphony pool.
struct FmChannelConfig {
  int group = 1;
  FmPan pan = FmPan::Center;

  bool operator==(const FmChannelConfig& o) const {
    return group == o.group && pan == o.pan;
  }
  bool operator!=(const FmChannelConfig& o) const { return !(*this == o); }
};

// Chip master clock + prescale, which scale every section's effective rate.
// Centralised here so no encoder hardcodes the prescale-6 / fixed-crystal
// constants; the processor builds one of these from the chip parameters and
// threads the derived per-engine clocks into the note encoders.
//
// The prescale (reg 0x2D/0x2E/0x2F) is a real, software-writable divider that
// retunes the chip, so the encoders compensate using the derived clocks below to
// keep notes in tune. The clock is the soldered crystal, fixed at the PC-9801-86
// value, but kept configurable here so the compensation math stays clock-agnostic
// and unit-testable.
struct ChipRates {
  double clockHz = 7987200.0;  // master crystal (PC-9801-86)
  int prescale = 6;            // 6, 3 or 2 (reg 0x2D/0x2E/0x2F)

  // FM fnum / Timer-A effective clock. The fnum and Timer-A formulas hardcode
  // the prescale-6 divider 144 (= OPERATORS*6); substituting this value for the
  // raw clock reproduces divider = OPERATORS*prescale at the real crystal, so a
  // single substitution covers both clock and prescale with no formula change.
  // At prescale 6 this equals clockHz exactly.
  double fmClockHz() const {
    return clockHz * 6.0 / static_cast<double>(prescale);
  }

  // SSG generator clock. The hardware SSG divider is 4 / 2 / 1 for prescale
  // 6 / 3 / 2 (verified against ymfm ym2608::update_prescale's rate table; note
  // it is NOT clock/(prescale*2/3), which only happens to be right at 6 and 3).
  double ssgClockHz() const {
    const double div = prescale == 2 ? 1.0 : (prescale == 3 ? 2.0 : 4.0);
    return clockHz / div;
  }

  // ADPCM-B runs at the FM rate: clock / (OPERATORS * prescale) (= clock/144 at
  // prescale 6).
  double adpcmClockHz() const {
    return clockHz / (24.0 * static_cast<double>(prescale));
  }

  bool operator==(const ChipRates& o) const {
    return clockHz == o.clockHz && prescale == o.prescale;
  }
  bool operator!=(const ChipRates& o) const { return !(*this == o); }
};

// The chip-global FM LFO (one for all channels).
struct FmGlobal {
  int lfoRate = 0;       // 0..7
  bool lfoEnable = false;

  bool operator==(const FmGlobal& o) const {
    return lfoRate == o.lfoRate && lfoEnable == o.lfoEnable;
  }
  bool operator!=(const FmGlobal& o) const { return !(*this == o); }
};

}  // namespace opna
