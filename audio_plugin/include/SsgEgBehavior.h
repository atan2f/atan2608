#pragma once

namespace audio_plugin {

// Decoded SSG-EG operator behaviour, mirroring ymfm's clock_ssg_eg_state
// (deps/ymfm-mame/src/ymfm_fm.ipp). The register value is the 4-bit SSG-EG field:
// bit 3 is the enable, bits 0-2 are the mode. JUCE-free so it can be unit-tested
// and shared by the envelope viz and the control-gating rules.
//
//   mode = ssgeg & 7
//   bit0 (mode & 1): 0 = continuous (loops by restarting the attack)
//                    1 = hold (runs once, then holds)
//   bit1 (mode & 2): continuous -> alternate invert each cycle (triangle)
//   bit2 (mode & 4): start inverted
//
//   Hold end-state: ymfm sets the operator's inverted flag to (bit2 ^ bit1) and
//   forces the attenuation. When that flag is set the attenuation is forced to
//   0x200 which, inverted, outputs at FULL volume -- a sustained tone (holdsHigh,
//   modes 3/5). When clear the attenuation is forced to max -> silence (holdsLow,
//   modes 1/7).
struct SsgEgBehavior {
  bool enabled = false;        // ssgeg >= 8
  bool loops = false;          // continuous modes 0/2/4/6
  bool alternates = false;     // modes 2/6 (triangle: invert toggles each cycle)
  bool startInverted = false;  // bit2 set: modes 4/6
  bool holdsHigh = false;      // hold modes settling at full volume: 3/5
  bool holdsLow = false;       // hold modes settling at silence: 1/7
  // Note: every ADSR rate (AR/DR/SL/SR/RR) still applies under SSG-EG -- the
  // looping only adds behaviour between the attack and key-off.
};

inline SsgEgBehavior decodeSsgEg(int ssgeg) {
  SsgEgBehavior b;
  b.enabled = ssgeg >= 8;
  if (!b.enabled)
    return b;

  const int mode = ssgeg & 7;
  const bool hold = (mode & 1) != 0;
  const bool bit1 = (mode & 2) != 0;
  const bool bit2 = (mode & 4) != 0;

  b.startInverted = bit2;
  if (hold) {
    const bool endInverted = bit2 ^ bit1;  // ymfm: m_ssg_inverted end-state
    b.holdsHigh = endInverted;             // forced to 0x200, inverted -> full
    b.holdsLow = !endInverted;             // forced to max -> silence
  } else {
    b.loops = true;
    b.alternates = bit1;
  }
  return b;
}

}  // namespace audio_plugin
