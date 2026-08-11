#pragma once

#include <array>
#include <vector>

#include "Ch3SpecialEncoder.h"
#include "FmConfig.h"
#include "FmPatch.h"
#include "RegisterEncoder.h"
#include "Routing.h"

namespace opna {

// Per-operator pitch control for channel-3 special mode. Each of CH3's four
// operators can sound at an independent pitch.
//
//   keyFollow ON : pitch = playedNote + coarse(semitones) + cents/100
//   keyFollow OFF: pitch = C4 (60) + coarse(semitones) + cents/100   (fixed)
//
// keyEnable gates whether the operator participates in the key-on at all (so an
// operator can be left silent without changing the algorithm).
struct Ch3OpPitch {
  int coarse = 0;          // semitone offset, -48..48
  int cents = 0;           // fine offset, -100..100
  bool keyFollow = true;   // track the played note vs. fixed reference pitch
  bool keyEnable = true;   // include this operator in the key-on mask

  bool operator==(const Ch3OpPitch& o) const {
    return coarse == o.coarse && cents == o.cents &&
           keyFollow == o.keyFollow && keyEnable == o.keyEnable;
  }
  bool operator!=(const Ch3OpPitch& o) const { return !(*this == o); }
};

// Complete state of the CH3 special pseudo-part. When enabled this part takes
// over all of physical channel 3 (it owns the shared CH3 registers too:
// algorithm/feedback/AMS/PMS/pan), so there is no separate "owner" part.
struct Ch3SpecialState {
  bool enabled = false;
  bool solo = false;       // part-solo (audition); gates dispatch via acceptsSolo
  int mode = 1;            // 0 = off, 1 = multi-frequency, 2 = CSM (reserved)
  FmPatch patch;           // timbre + algorithm/feedback/AMS/PMS for CH3
  FmPan pan = FmPan::Center;
  PartRouting routing;
  std::array<Ch3OpPitch, 4> op;

  bool operator==(const Ch3SpecialState& o) const {
    return enabled == o.enabled && solo == o.solo && mode == o.mode &&
           patch == o.patch && pan == o.pan &&
           routing.midiChannel == o.routing.midiChannel &&
           routing.lowKey == o.routing.lowKey && routing.highKey == o.routing.highKey &&
           routing.octave == o.routing.octave && op == o.op;
  }
  bool operator!=(const Ch3SpecialState& o) const { return !(*this == o); }
};

// Self-contained driver for CH3 special mode. The processor delegates to it with
// a handful of one-line calls; all special-mode register knowledge lives here.
// Register writes are appended to a caller-provided scratch vector (matching the
// existing regScratch_ pattern), so the whole class is testable without a chip.
class Ch3SpecialPart {
public:
  // True while this part owns physical channel 3 (i.e. enabled). The processor
  // uses this to keep CH3 out of the normal part grouping.
  bool ownsChannel3() const { return state_.enabled; }

  // True when this part is contributing a solo (only meaningful while enabled);
  // folded into the processor's global anySolo.
  bool soloed() const { return state_.enabled && state_.solo; }

  int mode() const { return state_.mode; }

  // True while a note is currently sounding (the CH3 ops are keyed). The
  // processor uses this to force a chip-accurate retrigger gap before re-keying.
  bool isKeyed() const { return activeNote_ >= 0; }

  // Adopt new state. Updates the stored state and reports whether the chip needs
  // a rebuild (call emitRebuild) and whether CH3 ownership changed (enable
  // toggled), which the processor uses to force a group rebuild. Pitch/routing-
  // only edits don't need a rebuild (they take effect at the next note-on).
  bool setStateIfChanged(const Ch3SpecialState& s, bool& ownershipChanged);

  // Emit the persistent CH3 register state for the current stored state: the
  // mode register, and (when enabled) the composite CH3 patch. When disabled this
  // just clears the mode register and keys CH3 off. A timbre/algorithm edit
  // re-pushes the patch without keying off, so a held note isn't cut.
  void emitRebuild(std::vector<RegWrite>& out);

  // Note dispatch. Each self-gates on enable + routing, so callers stay
  // branch-free. bendSemis is the current pitch-bend for the source MIDI channel
  // (applied only to key-following operators).
  void noteOn(int midiNote, int msgChannel, double clockHz, double bendSemis,
              bool anySolo, std::vector<RegWrite>& out);
  void noteOff(int midiNote, std::vector<RegWrite>& out);
  void pitchBend(int msgChannel, double bendSemis, double clockHz,
                 std::vector<RegWrite>& out);
  void allOff(std::vector<RegWrite>& out);

private:
  // Effective (fractional) MIDI note for one operator given the played note and
  // current bend. Key-following operators track the note + bend; fixed operators
  // ignore them and sit at a C4-relative pitch. coarse/cents always detune.
  double effectiveNote(int op, int playedNote, double bendSemis) const;

  // Key bitmask of operators with keyEnable set.
  int enabledOps() const;
  // enabledOps() while a note is sounding, else 0.
  int activeOpMask() const;
  // Append the frequency writes for every keyEnabled operator (no key-on). In
  // CSM mode the hardware keys all four operators regardless of the mask, so
  // pass allOps=true to write all four formant frequencies.
  void emitFreqs(int playedNote, double bendSemis, double clockHz,
                 std::vector<RegWrite>& out, bool allOps = false);

  // True in CSM mode (Timer A drives the operators instead of reg 0x28).
  bool csm() const { return state_.mode == 2; }

  Ch3SpecialState state_;
  bool initialised_ = false;

  // CH3 special is strictly monophonic: one key drives all four operators, each
  // at its own pitch (the hardware's multi-frequency mode). Last-note priority,
  // and -- like the other parts and a tracker driving the chip -- no held-note
  // recall: releasing the sounding note keys off (a still-held earlier note does
  // not resume); releasing a non-sounding note does nothing.
  int activeNote_ = -1;
  // The transposed pitch the sounding note plays at (activeNote_ + routing
  // octave). Tracked separately so note-off/bend gating still match the incoming
  // note while the operators sound at the shifted pitch.
  int activePlayedNote_ = -1;
  int activeMsgChannel_ = 1;

  // Set when the mode (multi-freq <-> CSM) changes, so the next emitRebuild can
  // reconcile keystate. The two modes key the operators through different
  // mechanisms (reg 0x28 vs Timer A), and a key-on latched by the old mode would
  // otherwise stay stuck under the new one -- sustaining the voice (masking the
  // CSM grain pulse) or never releasing.
  bool modeChanged_ = false;
};

}  // namespace opna
