#pragma once

namespace opna {

// The six ADPCM-A rhythm instruments share one total-level control and have a
// per-instrument level. Instrument order matches ymfm's built-in rhythm ROM:
// 0=bass drum, 1=snare, 2=top cymbal, 3=hi-hat, 4=tom, 5=rim shot.
struct RhythmPatch {
  int totalLevel = 63;                       // 0..63 (higher = louder)
  int level[6] = {31, 31, 31, 31, 31, 31};   // per-instrument 0..31
  int pan[6] = {1, 1, 1, 1, 1, 1};           // per-instrument 0=L 1=C 2=R

  bool operator==(const RhythmPatch& o) const {
    if (totalLevel != o.totalLevel)
      return false;
    for (int i = 0; i < 6; ++i)
      if (level[i] != o.level[i] || pan[i] != o.pan[i])
        return false;
    return true;
  }
  bool operator!=(const RhythmPatch& o) const { return !(*this == o); }
};

}  // namespace opna
