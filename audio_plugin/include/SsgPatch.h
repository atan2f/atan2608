#pragma once

#include <array>

namespace opna {

// The SSG (AY-3-8910-style PSG) has three tone channels but only **one** noise
// generator and **one** hardware envelope shared by all of them. SsgPatch
// carries the part-global generator settings (noise/envelope) plus the timbre of
// the "pooled" voice that the pooled channels share. Truly per-channel settings
// (mix, fixed volume, HW-envelope enable) for an independent channel live in
// SsgVoice; tone period is already per-channel in the register map.
struct SsgPatch {
  // Mix selects which of the (per-channel) tone/noise sources feed the channel.
  // kOff disables both so an independent channel can be silenced without a
  // separate enable.
  enum Mix { kTone = 0, kNoise = 1, kToneNoise = 2, kOff = 3 };

  int mix = kTone;        // tone / noise / both / off (pooled voice)
  int volume = 12;        // 0..15 (used when envEnable is false); 0 = silent
  bool envEnable = false; // use the hardware envelope instead of fixed volume
  int envPeriod = 2000;   // 0..65535 (16-bit) -- shared generator
  int envShape = 8;       // 0..15 (8..15 repeat) -- shared generator
  int noisePeriod = 16;   // 0..31 (5-bit) -- shared generator

  bool operator==(const SsgPatch& o) const {
    return mix == o.mix && volume == o.volume && envEnable == o.envEnable &&
           envPeriod == o.envPeriod && envShape == o.envShape &&
           noisePeriod == o.noisePeriod;
  }
  bool operator!=(const SsgPatch& o) const { return !(*this == o); }
};

// Per-channel voice timbre for a channel running Independent (its own mix, fixed
// volume and HW-envelope enable). The noise colour and envelope period/shape it
// uses still come from the shared generators in SsgPatch.
struct SsgVoice {
  int mix = SsgPatch::kTone;  // tone / noise / both / off
  int volume = 12;            // 0..15
  bool envEnable = false;     // use the shared HW envelope

  bool operator==(const SsgVoice& o) const {
    return mix == o.mix && volume == o.volume && envEnable == o.envEnable;
  }
  bool operator!=(const SsgVoice& o) const { return !(*this == o); }
};

// Each SSG channel is either Pooled (joins the shared poly allocator using the
// pooled voice) or Independent (its own mono voice + MIDI routing).
enum class SsgChannelMode { Pooled = 0, Independent = 1 };

// Complete chip-relevant SSG state: the shared generators + pooled voice
// (SsgPatch), per-channel mode, and the per-channel independent voices. MIDI
// routing for independent channels is held separately (like the other parts),
// since it does not affect chip registers.
struct SsgState {
  static constexpr int kNumChannels = 3;

  SsgPatch pooled;                            // shared generators + pooled voice
  std::array<SsgChannelMode, kNumChannels> mode{
      {SsgChannelMode::Pooled, SsgChannelMode::Pooled, SsgChannelMode::Pooled}};
  std::array<SsgVoice, kNumChannels> voice{};  // used when channel is Independent

  // Effective per-channel voice (pooled voice for Pooled channels, own voice for
  // Independent ones). Drives the per-channel mixer/amplitude writes.
  SsgVoice effectiveVoice(int channel) const {
    if (mode[(size_t)channel] == SsgChannelMode::Independent)
      return voice[(size_t)channel];
    return {pooled.mix, pooled.volume, pooled.envEnable};
  }

  bool operator==(const SsgState& o) const {
    return pooled == o.pooled && mode == o.mode && voice == o.voice;
  }
  bool operator!=(const SsgState& o) const { return !(*this == o); }
};

}  // namespace opna
