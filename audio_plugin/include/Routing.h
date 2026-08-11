#pragma once

namespace opna {

// MIDI routing for one instrument part: a channel filter, a key range, and an
// octave transpose applied to accepted notes. Shared by every part.
struct PartRouting {
  int midiChannel = 0;  // 0 = Omni, 1..16 = a specific channel
  int lowKey = 0;
  int highKey = 127;
  int octave = 0;       // play-time transpose in octaves; NOT used by accepts()
};

// True if a note on the given MIDI channel (1..16) should be played by the part.
// The key-range test uses the *incoming* note (octave is a play-time transform,
// so the split point stays at the physical key).
inline bool accepts(const PartRouting& r, int msgChannel, int note) {
  const bool channelOk = r.midiChannel == 0 || r.midiChannel == msgChannel;
  return channelOk && note >= r.lowKey && note <= r.highKey;
}

// The pitch an accepted note sounds at after this part's octave transpose,
// clamped to the MIDI range. Note-off/voice tracking still key on the original
// (incoming) note; only the programmed pitch is shifted.
inline int transposed(const PartRouting& r, int note) {
  const int t = note + 12 * r.octave;
  return t < 0 ? 0 : (t > 127 ? 127 : t);
}

// Solo-aware acceptance. While any part is soloed, only soloed parts accept
// notes; a soloed part responds as if on Omni (its MIDI channel filter is
// ignored) but still respects its key range, so multiple soloed parts can be
// split-tested. With nothing soloed this is identical to accepts().
inline bool acceptsSolo(const PartRouting& r, int msgChannel, int note,
                        bool thisSoloed, bool anySolo) {
  if (anySolo) {
    if (!thisSoloed)
      return false;
    return note >= r.lowKey && note <= r.highKey;  // Omni: ignore channel
  }
  return accepts(r, msgChannel, note);
}

}  // namespace opna
