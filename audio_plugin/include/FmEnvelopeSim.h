#pragma once

// YM2608 FM envelope generator model for the operator EnvelopeDisplay.
//
// Rather than plotting the envelope on an absolute time axis -- which fights the
// chip's enormous dynamic range (sub-millisecond attacks beside multi-second
// releases) and the fact that TL/SL are attenuations, not screen levels -- this
// produces a *stage schematic* (after the nonagon EnvelopeSettingsPanel model):
// attack / decay / sustain / release laid out left to right, each stage's width
// proportional to its real duration but min-clamped so a fast stage never
// vanishes, and a fixed sustain window.
//
// The per-stage *durations* and *curve shapes* come from a faithful, JUCE-free
// port of ymfm's per-operator EG (deps/ymfm-mame/src/ymfm_fm.ipp:
// clock_envelope / start_attack) -- exact rate tables and the signed-wraparound
// attack approximation -- so a slow attack really is wider and a real concave
// attack / convex decay shape is preserved. The display maps those shapes onto
// schematic, predictable levels (peak = a linear function of TL, sustain = a
// linear function of SL), so every parameter moves exactly one thing on screen.
// Key-scaling (KSR) is taken as 0 (drawn "as written" for a low reference note).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace opna {

// Chip-domain operator envelope parameters (NOT the inverted APVTS "sl" value --
// pass slChip = the hardware sustain-level register, 0 = loud .. 15 = silent).
struct EnvInput {
  int ar = 31;     // attack rate, 0..31
  int dr = 0;      // decay rate (D1R), 0..31
  int sr = 0;      // sustain rate (D2R), 0..31
  int rr = 15;     // release rate, 0..15
  int slChip = 0;  // sustain level, hardware sense (0 = loud .. 15 = silent)
  int tl = 0;      // total level, 0 = loud .. 127 = silent (display only)
  int ssgeg = 0;   // SSG-EG register (bit 3 = enable, bits 0-2 = mode)
};

// One ADSR stage. `seconds` is the real duration (drives the relative width);
// `shape` is the authentic envelope amplitude (0..1) sampled across the stage,
// front = stage start, back = stage end. The display renormalises `shape`'s value
// range onto the stage's schematic start/end levels, so curvature is real but the
// levels are predictable.
struct EnvStage {
  double seconds = 0.0;
  std::vector<float> shape;
};

struct EnvSchematic {
  EnvStage attack;       // amplitude 0 -> 1
  EnvStage decay;        // amplitude 1 -> sustain level
  EnvStage sustain;      // D2R curve over a fixed reference window (knee shows SR)
  EnvStage release;      // amplitude sustain -> 0
  float sustainDrop = 0.0f;  // 0..1: fraction SR bleeds off over the sustain window
  // SSG-EG enabled. The decay/sustain/release stages above are the plain ADSR
  // build; when this is set the display draws the SSG-EG loop/hold pattern over
  // the decay+sustain region instead (its looping doesn't fit a stage box).
  bool ssgOn = false;
};

namespace envsim_detail {

// ymfm s_increment_table (ymfm_fm.ipp). attenuation increment per 6-bit rate and
// the 3-bit cycle index.
inline uint32_t attenIncrement(uint32_t rate, uint32_t index) {
  static const uint32_t t[64] = {
      0x00000000, 0x00000000, 0x10101010, 0x10101010, 0x10101010, 0x10101010,
      0x11101110, 0x11101110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
      0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010,
      0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
      0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010,
      0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
      0x10101010, 0x10111010, 0x11101110, 0x11111110, 0x10101010, 0x10111010,
      0x11101110, 0x11111110, 0x10101010, 0x10111010, 0x11101110, 0x11111110,
      0x11111111, 0x21112111, 0x21212121, 0x22212221, 0x22222222, 0x42224222,
      0x42424242, 0x44424442, 0x44444444, 0x84448444, 0x84848484, 0x88848884,
      0x88888888, 0x88888888, 0x88888888, 0x88888888};
  return (t[rate] >> (4 * index)) & 0xF;
}

inline uint32_t effRate(uint32_t rawrate, uint32_t ksr) {
  return rawrate == 0 ? 0u : std::min<uint32_t>(rawrate + ksr, 63u);
}

// Linear amplitude (0..1) for a 10-bit combined attenuation. Each attenuation
// unit is 1/64 octave of attenuation (ymfm's 4.8 attenuation_to_volume: 6.02 dB
// per 256 of the 4.8 value, and the EG value is shifted up by 2).
inline float ampFromAtten(uint32_t atten) {
  if (atten >= 0x3ff)
    return 0.0f;
  return (float)std::pow(2.0, -(double)atten / 64.0);
}

}  // namespace envsim_detail

// Build the stage schematic: per-stage real durations + authentic amplitude
// shapes, for the attack / decay / sustain-window / release. fmSampleRate is the
// chip's FM operating rate (clock/144 at prescale 6) -- the EG advances one tick
// every 3 FM samples. SSG-EG is not unrolled here (its looping doesn't fit a
// stage diagram); the plain ADSR shape is built and `ssgOn` flags the badge.
inline EnvSchematic buildEnvSchematic(const EnvInput& in, double fmSampleRate) {
  using namespace envsim_detail;
  const uint32_t ksr = 0;
  const uint32_t rA = effRate((uint32_t)in.ar * 2, ksr);
  const uint32_t rD = effRate((uint32_t)in.dr * 2, ksr);
  const uint32_t rS = effRate((uint32_t)in.sr * 2, ksr);
  const uint32_t rR = effRate((uint32_t)in.rr * 4 + 2, ksr);
  // ymfm: eg_sustain = sl | ((sl+1)&0x10), then << 5  (sl==15 -> 31<<5 = 0x3e0).
  const uint32_t egSustain = (uint32_t)((in.slChip == 15 ? 31 : in.slChip)) << 5;
  const double dt = 3.0 / fmSampleRate;

  uint32_t egStep = 0;  // continuous EG counter across stages (chip-accurate phase)
  uint16_t att = 0x3ff;

  // One additive EG tick (decay/sustain/release): attenuation climbs to silence.
  auto tickAdd = [&](uint32_t rate) {
    const uint32_t rs = rate >> 2;
    const uint32_t ec = egStep << rs;
    if ((ec & 0x7ff) == 0) {
      const uint32_t rel = (ec >> (rs <= 11 ? 11 : rs)) & 0x7;
      att = (uint16_t)std::min<uint32_t>(att + attenIncrement(rate, rel), 0x3ff);
    }
    ++egStep;
  };
  // One attack EG tick: attenuation falls toward 0 (signed-wraparound, see above).
  auto tickAtk = [&](uint32_t rate) {
    const uint32_t rs = rate >> 2;
    const uint32_t ec = egStep << rs;
    if ((ec & 0x7ff) == 0 && rate < 62) {
      const uint32_t rel = (ec >> (rs <= 11 ? 11 : rs)) & 0x7;
      att = (uint16_t)(att + ((~att * attenIncrement(rate, rel)) >> 4));
    }
    ++egStep;
  };

  EnvSchematic s;
  s.ssgOn = (in.ssgeg & 0x08) != 0;
  const int kMaxTicks = 600000;  // ~32 s guard (e.g. AR=0 never completes attack)

  // Run one stage: count its real duration, then re-run sampling ~48 points at
  // even time spacing (so mapping sample index -> X is time-proportional and the
  // authentic curvature -- concave attack, convex decay -- is preserved). `tick`
  // advances the EG one step; `running` is true while the stage hasn't finished.
  auto runStage = [&](EnvStage& st, auto tick, auto running) {
    const uint16_t a0 = att;
    const uint32_t step0 = egStep;
    long ticks = 0;
    while (running() && ticks < kMaxTicks) {
      tick();
      ++ticks;
    }
    st.seconds = (double)ticks * dt;
    att = a0;  // re-run from the stage start to sample the curve
    egStep = step0;
    const long every = std::max(1L, ticks / 48);
    st.shape.push_back(ampFromAtten(att));
    for (long i = 1; i <= ticks; ++i) {
      tick();
      if (i % every == 0)
        st.shape.push_back(ampFromAtten(att));
    }
    st.shape.push_back(ampFromAtten(att));  // explicit endpoint
  };

  // --- Attack: attenuation 0x3ff -> 0 (amplitude 0 -> 1). ---
  if (rA >= 62)
    att = 0;  // AR>=62 snaps to peak
  runStage(s.attack, [&] { tickAtk(rA); }, [&] { return att > 0; });

  // --- Decay (D1R): attenuation 0 -> sustain level. ---
  runStage(s.decay, [&] { tickAdd(rD); }, [&] { return att < egSustain; });

  // --- Sustain (D2R): the curve the level follows over a fixed reference window.
  // The window width is fixed (it is not a real-time stage), but sampling the
  // actual D2R curve makes the knee position reflect SR -- so a fast SR drops
  // early then flattens while a slow SR sags gently. SR=0 is flat. ---
  {
    const float startAmp = ampFromAtten(att);
    const uint16_t savedAtt = att;
    const uint32_t savedStep = egStep;
    const double kSustainRef = 0.25;
    const long ticks = (long)(kSustainRef / dt);
    const long every = std::max(1L, ticks / 48);
    s.sustain.shape.push_back(startAmp);
    for (long i = 1; i <= ticks; ++i) {
      tickAdd(rS);
      if (i % every == 0)
        s.sustain.shape.push_back(ampFromAtten(att));
    }
    const float endAmp = ampFromAtten(att);
    s.sustain.shape.push_back(endAmp);
    s.sustainDrop = startAmp > 1.0e-6f
                        ? std::clamp(1.0f - endAmp / startAmp, 0.0f, 1.0f)
                        : 0.0f;
    att = savedAtt;  // release sims from the sustain level, not post-D2R
    egStep = savedStep;
  }

  // --- Release (RR): attenuation sustain -> silence. ---
  runStage(s.release, [&] { tickAdd(rR); },
           [&] { return att < 0x3ff && ampFromAtten(att) > 0.0008f; });

  return s;
}

}  // namespace opna
