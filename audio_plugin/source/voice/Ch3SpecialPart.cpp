#include "Ch3SpecialPart.h"

namespace opna {

bool Ch3SpecialPart::setStateIfChanged(const Ch3SpecialState& s,
                                       bool& ownershipChanged) {
  const bool first = !initialised_;
  ownershipChanged = first || (s.enabled != state_.enabled);

  // A mode switch (multi-freq <-> CSM) needs keystate reconciliation at the next
  // rebuild (see modeChanged_). A fresh enable counts too: the old mode's key may
  // still be latched on physical CH3 from a previous session of the part.
  if ((!first && s.mode != state_.mode) || ownershipChanged)
    modeChanged_ = true;

  // A chip rebuild is needed for state that maps to persistent CH3 registers.
  // Per-op pitch and routing take effect at the next note-on, so editing them
  // must not re-push.
  const bool rebuild = first || s.enabled != state_.enabled ||
                       s.mode != state_.mode || s.pan != state_.pan ||
                       s.patch != state_.patch;

  state_ = s;
  initialised_ = true;
  return rebuild;
}

void Ch3SpecialPart::emitRebuild(std::vector<RegWrite>& out) {
  if (!state_.enabled) {
    // Hand CH3 back to normal operation: clear multi-frequency mode and silence
    // any operators still keyed on. The processor re-pushes the normal CH3
    // patch as part of its group rebuild.
    out.push_back(Ch3SpecialEncoder::encodeMode(0));
    out.push_back(Ch3SpecialEncoder::keyMask(0));
    activeNote_ = -1;
    modeChanged_ = false;
    return;
  }

  if (modeChanged_) {
    // Reconcile keystate across a mode switch (or fresh enable): clear any manual
    // 0x28 key-on that the previous mode latched, and drop the held note so it
    // does not carry across modes. Without this a multi-freq key-on stays latched
    // under CSM -- sustaining the operators (so CSM sounds identical) and never
    // releasing (stuck note). The voice re-arms cleanly on the next note-on.
    out.push_back(Ch3SpecialEncoder::keyMask(0));
    activeNote_ = -1;
    modeChanged_ = false;
  }

  if (csm())
    // CSM: keep Timer A running if a note is sounding, else leave it armed but
    // halted. Re-emitting the load bit while the timer runs is a no-op, so a
    // timbre/algorithm edit does not cut a sounding grain train.
    out.push_back(Ch3SpecialEncoder::encodeCsmRun(activeNote_ >= 0));
  else
    out.push_back(Ch3SpecialEncoder::encodeMode(state_.mode));
  // The composite CH3 patch: per-op timbre + shared algorithm/feedback/AMS/PMS
  // and pan, written to channel index 2. No key-off -- a timbre/algorithm edit
  // shouldn't cut a held note (all four operators sound in every algorithm).
  RegisterEncoder::encodePatch(state_.patch, 2, out, state_.pan);
}

double Ch3SpecialPart::effectiveNote(int op, int playedNote,
                                     double bendSemis) const {
  const Ch3OpPitch& p = state_.op[(size_t)op];
  // Key-following operators track the note + bend; fixed operators ignore them
  // and sit at a C4-relative pitch. coarse/cents always detune.
  const double base = p.keyFollow ? (playedNote + bendSemis) : 60.0;
  return base + p.coarse + p.cents / 100.0;
}

int Ch3SpecialPart::enabledOps() const {
  int m = 0;
  for (int op = 0; op < 4; ++op)
    if (state_.op[(size_t)op].keyEnable)
      m |= (1 << op);
  return m;
}

int Ch3SpecialPart::activeOpMask() const {
  return activeNote_ >= 0 ? enabledOps() : 0;
}

void Ch3SpecialPart::emitFreqs(int playedNote, double bendSemis, double clockHz,
                               std::vector<RegWrite>& out, bool allOps) {
  for (int op = 0; op < 4; ++op) {
    if (!allOps && !state_.op[(size_t)op].keyEnable)
      continue;
    Ch3SpecialEncoder::encodeOperatorFrequency(
        effectiveNote(op, playedNote, bendSemis), op, clockHz, out);
  }
}

void Ch3SpecialPart::noteOn(int midiNote, int msgChannel, double clockHz,
                            double bendSemis, bool anySolo,
                            std::vector<RegWrite>& out) {
  if (!state_.enabled)
    return;
  if (!acceptsSolo(state_.routing, msgChannel, midiNote, state_.solo, anySolo))
    return;

  // Last-note priority: the new note takes over the single voice. Gating matches
  // the incoming note (above); the operators sound at the octave-transposed
  // pitch.
  activeNote_ = midiNote;
  activePlayedNote_ = transposed(state_.routing, midiNote);
  activeMsgChannel_ = msgChannel;

  if (csm()) {
    // CSM: the played note sets Timer A (the retrigger rate = the pitch); the
    // operator frequencies are formants. The hardware keys all four operators on
    // every overflow regardless of the per-op key-enable mask, so write all four
    // formant frequencies. Stop-then-start Timer A so every note-on is a clean
    // restart (the processor's 0x28 retrigger gap does not apply to CSM).
    emitFreqs(activePlayedNote_, bendSemis, clockHz, out, /*allOps=*/true);
    out.push_back(Ch3SpecialEncoder::encodeCsmRun(false));
    Ch3SpecialEncoder::encodeTimerA(
        Ch3SpecialEncoder::noteToTimerA(activePlayedNote_ + bendSemis, clockHz), out);
    out.push_back(Ch3SpecialEncoder::encodeCsmRun(true));
    return;
  }

  // Multi-frequency: the processor gaps key-off -> key-on around this so every
  // note-on re-attacks.
  emitFreqs(activePlayedNote_, bendSemis, clockHz, out);
  out.push_back(Ch3SpecialEncoder::keyMask(activeOpMask()));
}

void Ch3SpecialPart::noteOff(int midiNote, std::vector<RegWrite>& out) {
  if (!state_.enabled)
    return;
  // No held-note recall (matches the other parts and a tracker driving the
  // chip): releasing the sounding note keys off; releasing any other note that
  // is merely still physically held makes no sound change.
  if (midiNote != activeNote_)
    return;
  activeNote_ = -1;
  activePlayedNote_ = -1;
  // CSM: halt Timer A (the grain train stops and the last grain decays out).
  // Multi-frequency: key the operators off via reg 0x28.
  out.push_back(csm() ? Ch3SpecialEncoder::encodeCsmRun(false)
                      : Ch3SpecialEncoder::keyMask(0));
}

void Ch3SpecialPart::pitchBend(int msgChannel, double bendSemis, double clockHz,
                               std::vector<RegWrite>& out) {
  if (!state_.enabled)
    return;
  if (activeNote_ < 0 || activeMsgChannel_ != msgChannel)
    return;
  if (csm()) {
    // CSM: bend the fundamental by rewriting Timer A without restarting -- the
    // new period takes effect at the next overflow (a glide). Formants stay put.
    Ch3SpecialEncoder::encodeTimerA(
        Ch3SpecialEncoder::noteToTimerA(activePlayedNote_ + bendSemis, clockHz), out);
    return;
  }
  // Re-pitch the sounding note without keying on (envelopes keep running).
  // Fixed operators stay put.
  emitFreqs(activePlayedNote_, bendSemis, clockHz, out);
}

void Ch3SpecialPart::allOff(std::vector<RegWrite>& out) {
  if (state_.enabled)
    out.push_back(csm() ? Ch3SpecialEncoder::encodeCsmRun(false)
                        : Ch3SpecialEncoder::keyMask(0));
  activeNote_ = -1;
  activePlayedNote_ = -1;
}

}  // namespace opna
