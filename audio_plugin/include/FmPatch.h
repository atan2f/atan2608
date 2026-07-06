#pragma once

namespace opna {

// One of the four FM operators. Field ranges match the YM2608 register fields.
struct FmOperator {
  int detune = 0;       // DT  0..7 (bit2 = sign: 0..3 = +, 4..7 = -)
  int multiple = 1;     // MUL 0..15
  int totalLevel = 0;   // TL  0..127 (attenuation; 0 = loudest)
  int keyScale = 0;     // KS  0..3
  int attackRate = 31;  // AR  0..31
  int decayRate = 0;    // DR  0..31
  int sustainRate = 0;  // SR  0..31
  int releaseRate = 7;  // RR  0..15
  int sustainLevel = 0; // SL  0..15
  int ssgEg = 0;        // SSG-EG 0..15 (bit3 = enable)
  bool amEnable = false;
};

// A complete FM voice patch for one channel.
//
// Note: the YM2608 has a single global LFO (rate + enable, register 0x22) shared
// by every FM channel, so it is NOT part of the per-group patch; see FmGlobal in
// FmConfig.h. AMS/PMS remain here because they are per-channel (register 0xB4).
struct FmPatch {
  FmOperator op[4];
  int algorithm = 7;  // 0..7
  int feedback = 0;   // 0..7 (operator 1 self-feedback)
  int ams = 0;        // amplitude-modulation sensitivity 0..3
  int pms = 0;        // pitch-modulation sensitivity 0..7

  bool operator==(const FmPatch& o) const {
    if (algorithm != o.algorithm || feedback != o.feedback || ams != o.ams ||
        pms != o.pms)
      return false;
    for (int i = 0; i < 4; ++i) {
      const FmOperator& a = op[i];
      const FmOperator& b = o.op[i];
      if (a.detune != b.detune || a.multiple != b.multiple ||
          a.totalLevel != b.totalLevel || a.keyScale != b.keyScale ||
          a.attackRate != b.attackRate || a.decayRate != b.decayRate ||
          a.sustainRate != b.sustainRate || a.releaseRate != b.releaseRate ||
          a.sustainLevel != b.sustainLevel || a.ssgEg != b.ssgEg ||
          a.amEnable != b.amEnable)
        return false;
    }
    return true;
  }
  bool operator!=(const FmPatch& o) const { return !(*this == o); }
};

}  // namespace opna
