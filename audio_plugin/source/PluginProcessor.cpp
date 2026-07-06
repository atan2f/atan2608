#include <cmath>

#include "BinaryData.h"
#include "RegisterEncoder.h"

namespace audio_plugin {
PluginProcessor::PluginProcessor()
    : AudioProcessor(
          BusesProperties()
#if !JUCE_IS_MIDI_EFFECT
#if !JUCE_IS_SYNTH
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
#endif
              ),
      apvts_(*this, nullptr, "PARAMETERS", createPatchParameterLayout()) {
  patchParams_.connect(apvts_);
  ssgParams_.connect(apvts_);
  rhythmParams_.connect(apvts_);
  samplerParams_.connect(apvts_);
  routing_.connect(apvts_);
  masterGain_ = apvts_.getRawParameterValue("master");
  limiterOn_ = apvts_.getRawParameterValue("limiter");
  // The fresh defaults are the initial revert point / clean baseline (a host
  // state restore overwrites this in setStateInformation).
  presetBaseline_ = apvts_.copyState();
  loadDefaultRhythmRom();  // auto-load the rhythm ROM from %APPDATA%/atan2608
}

const juce::String PluginProcessor::getName() const {
  return JUCE_PLUGIN_NAME;
}

bool PluginProcessor::acceptsMidi() const {
#if JUCE_NEEDS_MIDI_INPUT
  return true;
#else
  return false;
#endif
}

bool PluginProcessor::producesMidi() const {
#if JUCE_NEEDS_MIDI_OUTPUT
  return true;
#else
  return false;
#endif
}

bool PluginProcessor::isMidiEffect() const {
#if JUCE_IS_MIDI_EFFECT
  return true;
#else
  return false;
#endif
}

double PluginProcessor::getTailLengthSeconds() const {
  return 0.0;
}

int PluginProcessor::getNumPrograms() {
  return 1;  // some hosts choke on 0 programs
}

int PluginProcessor::getCurrentProgram() {
  return 0;
}

void PluginProcessor::setCurrentProgram(int index) {
  juce::ignoreUnused(index);
}

const juce::String PluginProcessor::getProgramName(int index) {
  juce::ignoreUnused(index);
  return {};
}

void PluginProcessor::changeProgramName(int index,
                                        const juce::String& newName) {
  juce::ignoreUnused(index, newName);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
  opna_.prepare(sampleRate);
  opna_.setFidelity(fidelity_);  // apply the saved fidelity preference
  // Rhythm ROM: whatever is currently loaded (default-location or override);
  // null when no ROM was found, in which case the ADPCM-A rhythm part is silent.
  if (!rhythmRom_.empty())
    opna_.setRhythmRom(rhythmRom_.data(), static_cast<int>(rhythmRom_.size()));
  else
    opna_.setRhythmRom(nullptr, 0);
  scratch_.assign(static_cast<size_t>(juce::jmax(1, samplesPerBlock)), 0.0f);
  limiter_.prepare(sampleRate);
  // Reserve enough that no register-emit path reallocates on the audio thread.
  // Worst case is Ch3SpecialPart::emitRebuild (keyMask + mode + a 30-write FM
  // encodePatch = 32); a small margin guards against future drift.
  regScratch_.reserve(static_cast<size_t>(40));
  ssgAllocator_.reset();
  ssgIndepNote_.fill(-1);
  for (auto& a : fmActive_)
    a.note = -1;
  for (auto& a : ssgActive_)
    a.note = -1;
  channelBend_.fill(0.0);
  fmInitialised_ = false;
  ssgInitialised_ = false;
  rhythmInitialised_ = false;
  chipRatesInitialised_ = false;
  samplerNote_ = -1;
  lastSamplerLevel_ = -1;
  lastSamplerPan_ = -1;
  updateChipRatesIfChanged();  // latch clock/prescale before encoding any notes
  updateFmIfChanged();
  updateSsgPatchIfChanged();
  updateRhythmIfChanged();

  applyPendingSample();  // a sample loaded before playback started
  if (sampleLoaded_) {
    // The chip reset cleared the ADPCM-B registers; the RAM contents persist,
    // so re-push the start/end/level configuration.
    regScratch_.clear();
    opna::AdpcmBRegisterEncoder::configure(sampleBytes_, samplerParams_.level(),
                                           samplerParams_.pan(), regScratch_);
    for (const auto& w : regScratch_)
      applyWrite(opna_, w);
    lastSamplerLevel_ = samplerParams_.level();
    lastSamplerPan_ = samplerParams_.pan();
  }
}

void PluginProcessor::updateFmIfChanged() {
  const bool first = !fmInitialised_;

  // The chip has one global LFO shared by every FM channel.
  const opna::FmGlobal g = patchParams_.global();
  if (first || g != fmGlobal_) {
    fmGlobal_ = g;
    applyWrite(opna_, opna::RegisterEncoder::encodeLfo(g));
  }

  // Per-channel group membership / pan.
  bool configChanged = false;
  for (int c = 0; c < opna::kNumFmChannels; ++c) {
    const opna::FmChannelConfig cfg = patchParams_.channelConfig(c + 1);
    if (first || cfg != fmChannels_[(size_t)c]) {
      fmChannels_[(size_t)c] = cfg;
      configChanged = true;
    }
  }

  // CH3 special-mode pseudo-part. Adopt its state first so ownsChannel3() is
  // current for the group rebuild below; a change in ownership forces a rebuild
  // so CH3 moves in/out of the normal grouping.
  bool spOwnershipChanged = false;
  const bool spNeedsRebuild =
      ch3Special_.setStateIfChanged(patchParams_.buildCh3Special(), spOwnershipChanged);
  if (spOwnershipChanged)
    configChanged = true;

  // Per-group patch + routing. Routing only affects note dispatch, so cache it
  // unconditionally; a changed patch must be re-pushed to that group's channels.
  std::array<bool, opna::kNumFmGroups> patchChanged{};
  for (int gi = 0; gi < opna::kNumFmGroups; ++gi) {
    fmGroups_[(size_t)gi].routing = patchParams_.routing(gi + 1);
    fmGroups_[(size_t)gi].solo = patchParams_.solo(gi + 1);
    const opna::FmPatch p = patchParams_.build(gi + 1);
    if (first || p != fmGroups_[(size_t)gi].patch) {
      fmGroups_[(size_t)gi].patch = p;
      patchChanged[(size_t)gi] = true;
    }
  }

  if (configChanged) {
    // Membership changed: rebuild pools (keys everything off) and re-push every
    // channel, since a channel may now belong to a different group.
    rebuildFmGroups();
    for (int c = 0; c < opna::kNumFmChannels; ++c)
      encodeFmChannel(c);
  } else {
    for (int gi = 0; gi < opna::kNumFmGroups; ++gi)
      if (patchChanged[(size_t)gi])
        for (int m = 0; m < fmGroups_[(size_t)gi].memberCount; ++m)
          encodeFmChannel(fmGroups_[(size_t)gi].members[(size_t)m]);
  }

  // Push the CH3 special-mode registers last so they own channel 3 (when
  // enabled) without the normal channel encode clobbering them. On disable this
  // clears the mode register; the group rebuild above re-pushed CH3's normal
  // patch since ownsChannel3() is now false.
  if (spNeedsRebuild || (configChanged && (spOwnershipChanged || ch3Special_.ownsChannel3()))) {
    regScratch_.clear();
    ch3Special_.emitRebuild(regScratch_);
    for (const auto& w : regScratch_)
      applyWrite(opna_, w);
  }

  fmInitialised_ = true;
}

void PluginProcessor::rebuildFmGroups() {
  // Key everything off first so a channel changing groups can't leave a stuck or
  // misrouted note sounding.
  for (int ch = 0; ch < opna::kNumFmChannels; ++ch) {
    applyWrite(opna_, opna::RegisterEncoder::keyOff(ch));
    fmActive_[(size_t)ch].note = -1;
  }

  for (auto& gs : fmGroups_)
    gs.memberCount = 0;

  for (int ch = 0; ch < opna::kNumFmChannels; ++ch) {
    // While the CH3 special pseudo-part owns physical channel 3, keep it out of
    // the normal grouping so no part drives it.
    if (ch == 2 && ch3Special_.ownsChannel3())
      continue;
    const int grp = fmChannels_[(size_t)ch].group;  // 0 = off
    if (grp >= 1 && grp <= opna::kNumFmGroups) {
      auto& gs = fmGroups_[(size_t)(grp - 1)];
      gs.members[(size_t)gs.memberCount++] = ch;
    }
  }

  for (auto& gs : fmGroups_)
    gs.allocator = opna::VoiceAllocator(gs.memberCount > 0 ? gs.memberCount : 1);
}

void PluginProcessor::encodeFmChannel(int hwChannel) {
  // CH3 special mode owns channel 3's registers; don't overwrite them with a
  // normal patch.
  if (hwChannel == 2 && ch3Special_.ownsChannel3())
    return;
  const int grp = fmChannels_[(size_t)hwChannel].group;
  if (grp < 1 || grp > opna::kNumFmGroups)
    return;  // channel off: left keyed off, no patch pushed
  regScratch_.clear();
  opna::RegisterEncoder::encodePatch(fmGroups_[(size_t)(grp - 1)].patch, hwChannel,
                                     regScratch_, fmChannels_[(size_t)hwChannel].pan);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
}

void PluginProcessor::updateSsgPatchIfChanged() {
  const opna::SsgState state = ssgParams_.buildState();
  if (ssgInitialised_ && state == currentSsg_)
    return;

  const bool modeChanged = !ssgInitialised_ || state.mode != currentSsg_.mode;
  currentSsg_ = state;
  ssgInitialised_ = true;

  // A mode change moves channels in/out of the pooled allocator; rebuild it (and
  // silence everything first so a channel changing role can't stick on).
  if (modeChanged)
    rebuildSsgPool();

  regScratch_.clear();
  opna::SsgRegisterEncoder::encodeGlobal(state.pooled, regScratch_);
  opna::SsgRegisterEncoder::encodeMixer(state, regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
}

void PluginProcessor::rebuildSsgPool() {
  // Key every SSG channel off and clear note tracking so a channel changing
  // pooled/independent role can't leave a stuck note sounding.
  for (int ch = 0; ch < opna::SsgState::kNumChannels; ++ch) {
    applyWrite(opna_, opna::SsgRegisterEncoder::noteOffAmplitude(ch));
    ssgIndepNote_[(size_t)ch] = -1;
  }

  ssgPooledCount_ = 0;
  for (int ch = 0; ch < opna::SsgState::kNumChannels; ++ch)
    if (currentSsg_.mode[(size_t)ch] == opna::SsgChannelMode::Pooled)
      ssgPooled_[(size_t)ssgPooledCount_++] = ch;

  ssgAllocator_ =
      opna::VoiceAllocator(ssgPooledCount_ > 0 ? ssgPooledCount_ : 1);
}

bool PluginProcessor::ssgAnyIndepSolo() const {
  for (int ch = 0; ch < opna::SsgState::kNumChannels; ++ch)
    if (ssgParams_.independent(ch) && ssgParams_.indepSolo(ch))
      return true;
  return false;
}

void PluginProcessor::updateChipRatesIfChanged() {
  const opna::ChipRates r = patchParams_.chipRates();
  if (chipRatesInitialised_ && r == chipRates_)
    return;
  const bool first = !chipRatesInitialised_;
  chipRates_ = r;
  chipRatesInitialised_ = true;
  // RT-safe: writes the prescale-select registers and updates the resample ratio
  // in place (no allocation). The master clock is fixed, so the native rate and
  // FIR cutoff stay valid without a kernel rebuild.
  opna_.setChipConfig(r.clockHz, r.prescale);
  if (!first && sampleLoaded_)
    // The sampler's pitch reference depends on the ADPCM-B clock; recompute it so
    // the next note-on plays in tune. (Patch/timbre registers are clock-
    // independent; held notes re-tune on their next note-on.)
    sampleRootDeltaN_ = opna::AdpcmBRegisterEncoder::rootDeltaN(
        sampleSourceRate_, chipRates_.adpcmClockHz());
}

void PluginProcessor::fmNoteOn(int midiNote, int msgChannel) {
  // A note plays on every group whose routing accepts it; within a group it is
  // allocated to one of the group's member channels.
  for (int gi = 0; gi < opna::kNumFmGroups; ++gi) {
    auto& gs = fmGroups_[(size_t)gi];
    if (gs.memberCount == 0)
      continue;
    if (!opna::acceptsSolo(gs.routing, msgChannel, midiNote, gs.solo, anySolo_))
      continue;

    const int slot = gs.allocator.noteOn(midiNote);
    const int channel = gs.members[(size_t)slot];

    // Chip-accurate retrigger: if this hardware channel is currently keyed (a
    // held note, a stolen sounding voice, or a same-tick NoteOff whose key-off
    // is still un-clocked), key it off and clock the retrigger gap so the
    // following key-on is a clean 0->1 edge that re-attacks the envelope.
    if (fmActive_[(size_t)channel].note >= 0 || fmDirtySinceRender_[(size_t)channel]) {
      applyWrite(opna_, opna::RegisterEncoder::keyOff(channel));
      opna_.advanceNativeFrames();
    }

    // Octave transpose: the voice tracker keys on the incoming note (above), but
    // the chip is programmed at the transposed pitch.
    const int played = opna::transposed(gs.routing, midiNote);
    const double bend = channelBend_[(size_t)juce::jlimit(1, 16, msgChannel)];
    regScratch_.clear();
    opna::RegisterEncoder::encodeFrequencyBend(played + bend, channel,
                                               chipRates_.fmClockHz(), regScratch_);
    for (const auto& w : regScratch_)
      applyWrite(opna_, w);

    applyWrite(opna_, opna::RegisterEncoder::keyOn(channel));
    fmActive_[(size_t)channel] = {played, juce::jlimit(1, 16, msgChannel)};
    fmDirtySinceRender_[(size_t)channel] = true;
  }

  // CH3 special pseudo-part (self-gates on enable + routing + solo). Same
  // retrigger gap as the normal channels: key CH3 off and clock the gap before
  // the part re-emits its freqs + key-on, so every note-on re-attacks. This is a
  // reg-0x28 key-edge concern and does not apply to CSM, where Timer A keys the
  // operators and the part restarts the timer itself -- skip the gap there.
  if (ch3Special_.mode() != 2 &&
      (ch3Special_.isKeyed() || ch3SpDirtySinceRender_)) {
    applyWrite(opna_, opna::Ch3SpecialEncoder::keyMask(0));
    opna_.advanceNativeFrames();
  }
  const double spBend = channelBend_[(size_t)juce::jlimit(1, 16, msgChannel)];
  regScratch_.clear();
  ch3Special_.noteOn(midiNote, msgChannel, chipRates_.fmClockHz(), spBend,
                     anySolo_, regScratch_);
  if (!regScratch_.empty())
    ch3SpDirtySinceRender_ = true;
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
}

void PluginProcessor::fmNoteOff(int midiNote) {
  for (int gi = 0; gi < opna::kNumFmGroups; ++gi) {
    auto& gs = fmGroups_[(size_t)gi];
    if (gs.memberCount == 0)
      continue;
    const int slot = gs.allocator.noteOff(midiNote);
    if (slot >= 0) {
      const int channel = gs.members[(size_t)slot];
      applyWrite(opna_, opna::RegisterEncoder::keyOff(channel));
      fmActive_[(size_t)channel].note = -1;
      // The key-off is queued but not yet clocked; keep the channel "dirty" so a
      // same-tick NoteOn re-keys with a gap and re-attacks.
      fmDirtySinceRender_[(size_t)channel] = true;
    }
  }

  regScratch_.clear();
  ch3Special_.noteOff(midiNote, regScratch_);
  if (!regScratch_.empty())
    ch3SpDirtySinceRender_ = true;
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
}

void PluginProcessor::pitchWheel(int msgChannel, int wheelValue) {
  const int ch = juce::jlimit(1, 16, msgChannel);
  // Wheel 0-16383, centre 8192. Map to +/- the fixed bend range in semitones.
  const double bend = (wheelValue - 8192) / 8192.0 * kBendRangeSemitones;
  channelBend_[(size_t)ch] = bend;
  fmPitchBend(ch, bend);
  ssgPitchBend(ch, bend);
  samplerPitchBend(ch, bend);
}

void PluginProcessor::fmPitchBend(int ch, double bend) {
  // Re-pitch every sounding FM note that was triggered by this MIDI channel
  // (no key-on, so the envelope keeps running).
  for (int c = 0; c < opna::kNumFmChannels; ++c) {
    const FmActiveNote& a = fmActive_[(size_t)c];
    if (a.note < 0 || a.msgChannel != ch)
      continue;
    regScratch_.clear();
    opna::RegisterEncoder::encodeFrequencyBend(a.note + bend, c,
                                               chipRates_.fmClockHz(), regScratch_);
    for (const auto& w : regScratch_)
      applyWrite(opna_, w);
  }

  regScratch_.clear();
  ch3Special_.pitchBend(ch, bend, chipRates_.fmClockHz(), regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
}

void PluginProcessor::ssgPitchBend(int ch, double bend) {
  // Re-pitch every sounding SSG channel triggered by this MIDI channel by
  // rewriting only its tone period (no amplitude / envelope re-arm), so the
  // square keeps sounding while the pitch slides.
  for (int c = 0; c < opna::SsgState::kNumChannels; ++c) {
    const FmActiveNote& a = ssgActive_[(size_t)c];
    if (a.note < 0 || a.msgChannel != ch)
      continue;
    regScratch_.clear();
    opna::SsgRegisterEncoder::encodeToneBend(a.note + bend, c,
                                             chipRates_.ssgClockHz(), regScratch_);
    for (const auto& w : regScratch_)
      applyWrite(opna_, w);
  }
}

void PluginProcessor::emitSsgVoice(int midiNote, int channel, int msgChannel,
                                  const opna::SsgVoice& voice) {
  // SSG needs no retrigger gap: the square tone has no attack transient, and the
  // hardware envelope is restarted by re-writing the shape register (0x0D) below
  // - a write-effect, not a key-state edge - so every note-on already re-attacks.
  const int mc = juce::jlimit(1, 16, msgChannel);
  const double bend = channelBend_[(size_t)mc];
  regScratch_.clear();
  opna::SsgRegisterEncoder::encodeToneBend(
      midiNote + bend, channel, chipRates_.ssgClockHz(), regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);

  applyWrite(opna_, opna::SsgRegisterEncoder::noteOnAmplitude(voice, channel));
  if (voice.envEnable)  // re-trigger the shared hardware envelope (re-arms it
                        // for every channel currently using it -- authentic).
    applyWrite(opna_, opna::SsgRegisterEncoder::envelopeShape(currentSsg_.pooled));
  ssgActive_[(size_t)channel] = {midiNote, mc};
}

void PluginProcessor::ssgNoteOn(int midiNote, int msgChannel) {
  // Pooled sub-voice: shared timbre, polyphonic across the pooled channels.
  const opna::PartRouting pooledRouting = routing_.ssg();
  if (ssgPooledCount_ > 0 &&
      opna::acceptsSolo(pooledRouting, msgChannel, midiNote, routing_.ssgSolo(),
                        anySolo_)) {
    const int slot = ssgAllocator_.noteOn(midiNote);  // tracker keys on incoming
    const int channel = ssgPooled_[(size_t)slot];
    emitSsgVoice(opna::transposed(pooledRouting, midiNote), channel, msgChannel,
                 currentSsg_.effectiveVoice(channel));
  }

  // Independent channels: each its own monophonic voice with its own routing.
  for (int ch = 0; ch < opna::SsgState::kNumChannels; ++ch) {
    if (currentSsg_.mode[(size_t)ch] != opna::SsgChannelMode::Independent)
      continue;
    const opna::PartRouting r = ssgParams_.indepRouting(ch);
    if (!opna::acceptsSolo(r, msgChannel, midiNote, ssgParams_.indepSolo(ch),
                           anySolo_))
      continue;
    ssgIndepNote_[(size_t)ch] = midiNote;  // incoming note, for note-off match
    emitSsgVoice(opna::transposed(r, midiNote), ch, msgChannel,
                 currentSsg_.voice[(size_t)ch]);
  }
}

void PluginProcessor::ssgNoteOff(int midiNote) {
  const int slot = ssgAllocator_.noteOff(midiNote);
  if (slot >= 0 && ssgPooledCount_ > 0) {
    const int channel = ssgPooled_[(size_t)slot];
    applyWrite(opna_, opna::SsgRegisterEncoder::noteOffAmplitude(channel));
    ssgActive_[(size_t)channel].note = -1;
  }

  for (int ch = 0; ch < opna::SsgState::kNumChannels; ++ch) {
    if (currentSsg_.mode[(size_t)ch] == opna::SsgChannelMode::Independent &&
        ssgIndepNote_[(size_t)ch] == midiNote) {
      applyWrite(opna_, opna::SsgRegisterEncoder::noteOffAmplitude(ch));
      ssgIndepNote_[(size_t)ch] = -1;
      ssgActive_[(size_t)ch].note = -1;
    }
  }
}

void PluginProcessor::updateRhythmIfChanged() {
  const opna::RhythmPatch patch = rhythmParams_.build();
  if (rhythmInitialised_ && patch == currentRhythm_)
    return;

  currentRhythm_ = patch;
  rhythmInitialised_ = true;

  regScratch_.clear();
  opna::RhythmRegisterEncoder::encodePatch(patch, regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
}

void PluginProcessor::rhythmNoteOn(int midiNote) {
  // Rhythm instruments are one-shot percussion: trigger on note-on, ignore
  // note-off. No retrigger gap is needed - writing the reg-0x10 key bit restarts
  // the sample each time (a write-effect, not a key-state edge).
  const int instrument = opna::RhythmRegisterEncoder::noteToInstrument(midiNote);
  if (instrument >= 0)
    applyWrite(opna_, opna::RhythmRegisterEncoder::trigger(instrument));
}

void PluginProcessor::loadSample(const float* mono,
                                 int numSamples,
                                 double sampleRate) {
  // Two ADPCM samples per RAM byte: cap so the encoded data fits the chip's RAM.
  numSamples = juce::jlimit(0, 2 * opna::OpnaChip::kAdpcmBRamBytes, numSamples);
  std::vector<int16_t> pcm(static_cast<size_t>(juce::jmax(0, numSamples)));
  for (int i = 0; i < numSamples; ++i) {
    const float s = juce::jlimit(-1.0f, 1.0f, mono[i]);
    pcm[(size_t)i] = static_cast<int16_t>(s * 32767.0f);
  }
  auto encoded = opna::AdpcmBEncoder::encode(pcm.data(), numSamples);

  // Retain the canonical copy (message thread) for state save, then stage the
  // DRAM image (respecting the re-encode-start choice) for the audio thread.
  loadedSampleData_ = std::move(encoded);
  loadedSampleRate_ = sampleRate;
  stageSamplerDram();
}

void PluginProcessor::loadEncodedSample(const unsigned char* data,
                                        int size,
                                        double sampleRate) {
  if (data == nullptr || size <= 0) {
    unloadSample();
    return;
  }
  size = juce::jmin(size, opna::OpnaChip::kAdpcmBRamBytes);  // never exceed RAM
  loadedSampleData_.assign(data, data + size);
  loadedSampleRate_ = sampleRate;
  stageSamplerDram();
}

void PluginProcessor::stageSamplerDram() {
  if (loadedSampleData_.empty())
    return;

  std::vector<unsigned char> blob;
  const int startPermille = samplerParams_.startPermille();
  if (samplerParams_.reencodeStart() && startPermille > 0) {
    // Re-encode with a fresh predictor at the start byte so the chip -- which
    // resets its predictor/step when it begins reading at the start address (and
    // at every loop point) -- reconstructs full level instead of ramping up from
    // silence. Decode the canonical data to recover the exact PCM the chip plays,
    // then re-encode it. The start unit is 32 bytes = 64 samples.
    const int dataBytes = static_cast<int>(loadedSampleData_.size());
    const int startUnit =
        opna::AdpcmBRegisterEncoder::unitForPermille(dataBytes, startPermille);
    const int resetSample = startUnit * opna::AdpcmBRegisterEncoder::kAddressUnit * 2;
    auto pcm = opna::AdpcmBEncoder::decode(loadedSampleData_.data(), dataBytes);
    auto enc = opna::AdpcmBEncoder::encode(pcm.data(),
                                           static_cast<int>(pcm.size()), resetSample);
    blob.assign(enc.begin(), enc.end());
  } else {
    blob = loadedSampleData_;
  }

  const juce::SpinLock::ScopedLockType lock(samplerLock_);
  encodedPending_ = std::move(blob);
  pendingSampleRate_ = loadedSampleRate_;
  pendingSample_ = true;
  pendingUnload_ = false;  // a fresh image supersedes a pending unload
}

void PluginProcessor::handleAsyncUpdate() {
  stageSamplerDram();  // message-thread rebuild requested from the audio thread
}

void PluginProcessor::updateSamplerDramIfChanged() {
  // Desired DRAM signature: the start unit it should be re-encoded for, or -1
  // when the canonical data is wanted (re-encode off or start at 0).
  int desired = -1;
  const int startPermille = samplerParams_.startPermille();
  if (samplerParams_.reencodeStart() && startPermille > 0)
    desired = opna::AdpcmBRegisterEncoder::unitForPermille(sampleBytes_, startPermille);

  if (desired != lastDramStartUnit_) {
    lastDramStartUnit_ = desired;
    if (sampleLoaded_)
      triggerAsyncUpdate();  // rebuild on the message thread
  }
}

void PluginProcessor::unloadSample() {
  loadedSampleData_.clear();
  loadedSampleRate_ = 0.0;

  const juce::SpinLock::ScopedLockType lock(samplerLock_);
  pendingUnload_ = true;
  pendingSample_ = false;  // cancel any not-yet-applied load
}

void PluginProcessor::applyPendingSample() {
  if (!pendingSample_ && !pendingUnload_)
    return;
  const juce::SpinLock::ScopedTryLockType lock(samplerLock_);
  if (!lock.isLocked())
    return;

  if (pendingUnload_) {
    applyWrite(opna_, opna::AdpcmBRegisterEncoder::keyOff());
    sampleLoaded_ = false;
    samplerNote_ = -1;
    sampleBytes_ = 0;
    lastSamplerLevel_ = -1;
    pendingUnload_ = false;
    return;
  }

  opna_.loadAdpcmB(encodedPending_.data(), static_cast<int>(encodedPending_.size()));
  sampleBytes_ = static_cast<int>(encodedPending_.size());
  sampleSourceRate_ = pendingSampleRate_;
  sampleRootDeltaN_ = opna::AdpcmBRegisterEncoder::rootDeltaN(
      pendingSampleRate_, chipRates_.adpcmClockHz());

  regScratch_.clear();
  opna::AdpcmBRegisterEncoder::configure(sampleBytes_, samplerParams_.level(),
                                         samplerParams_.pan(), regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);

  lastSamplerLevel_ = samplerParams_.level();
  lastSamplerPan_ = samplerParams_.pan();
  sampleLoaded_ = true;
  pendingSample_ = false;
}

void PluginProcessor::updateSamplerLevel() {
  if (!sampleLoaded_)
    return;
  const int level = samplerParams_.level();
  if (level != lastSamplerLevel_) {
    applyWrite(opna_, {1, 0x0B, level & 0xFF});
    lastSamplerLevel_ = level;
  }
  const int pan = samplerParams_.pan();
  if (pan != lastSamplerPan_) {
    applyWrite(opna_, {1, 0x01, opna::AdpcmBRegisterEncoder::control2(pan)});
    lastSamplerPan_ = pan;
  }
}

void PluginProcessor::samplerNoteOn(int midiNote, int msgChannel) {
  if (!sampleLoaded_)
    return;
  // Octave transpose: pitch from the transposed note, but note-off still matches
  // the incoming note (stored in samplerNote_ below).
  const int played = opna::transposed(routing_.sampler(), midiNote);
  const int mc = juce::jlimit(1, 16, msgChannel);
  const double bend = channelBend_[(size_t)mc];
  const int deltaN = opna::AdpcmBRegisterEncoder::noteDeltaNBend(
      played + bend, samplerParams_.rootNote(), sampleRootDeltaN_);

  // Playback window (latched at key-on). Clamp so end is always past start,
  // giving at least one address unit of audio.
  const int maxUnit = opna::AdpcmBRegisterEncoder::endUnitForBytes(sampleBytes_);
  int startUnit = opna::AdpcmBRegisterEncoder::unitForPermille(
      sampleBytes_, samplerParams_.startPermille());
  int endUnit = opna::AdpcmBRegisterEncoder::unitForPermille(
      sampleBytes_, samplerParams_.endPermille());
  if (endUnit <= startUnit)
    endUnit = juce::jmin(maxUnit, startUnit + 1);

  // Chip-accurate retrigger: if a note is still playing (or a same-tick key-off
  // is still un-clocked), key off and clock the retrigger gap so the start
  // address re-latches and playback restarts from the new key-on edge.
  if (samplerNote_ >= 0 || samplerDirtySinceRender_) {
    applyWrite(opna_, opna::AdpcmBRegisterEncoder::keyOff());
    opna_.advanceNativeFrames();
  }

  regScratch_.clear();
  opna::AdpcmBRegisterEncoder::setRegion(startUnit, endUnit, regScratch_);
  opna::AdpcmBRegisterEncoder::setDeltaN(deltaN, regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);

  applyWrite(opna_, opna::AdpcmBRegisterEncoder::keyOn(samplerParams_.loop()));
  samplerNote_ = midiNote;
  samplerPlayedNote_ = played;
  samplerMsgChannel_ = mc;
  samplerDirtySinceRender_ = true;
}

void PluginProcessor::samplerPitchBend(int ch, double bend) {
  // Re-pitch the sounding sampler note by rewriting delta-N only (no key-on, so
  // the sample keeps playing). Varispeed: this also changes playback speed.
  if (samplerNote_ < 0 || samplerMsgChannel_ != ch)
    return;
  const int deltaN = opna::AdpcmBRegisterEncoder::noteDeltaNBend(
      samplerPlayedNote_ + bend, samplerParams_.rootNote(), sampleRootDeltaN_);
  regScratch_.clear();
  opna::AdpcmBRegisterEncoder::setDeltaN(deltaN, regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
}

void PluginProcessor::samplerNoteOff(int midiNote) {
  if (midiNote == samplerNote_) {
    applyWrite(opna_, opna::AdpcmBRegisterEncoder::keyOff());
    samplerNote_ = -1;
    samplerDirtySinceRender_ = true;  // key-off queued but not yet clocked
  }
}

void PluginProcessor::allNotesOff() {
  for (int ch = 0; ch < opna::kNumFmChannels; ++ch)
    applyWrite(opna_, opna::RegisterEncoder::keyOff(ch));
  regScratch_.clear();
  ch3Special_.allOff(regScratch_);
  for (const auto& w : regScratch_)
    applyWrite(opna_, w);
  for (int ch = 0; ch < opna::SsgRegisterEncoder::kNumChannels; ++ch)
    applyWrite(opna_, opna::SsgRegisterEncoder::noteOffAmplitude(ch));
  applyWrite(opna_, opna::AdpcmBRegisterEncoder::keyOff());
  samplerNote_ = -1;
  for (auto& gs : fmGroups_)
    gs.allocator.reset();
  for (auto& a : fmActive_)
    a.note = -1;
  ssgAllocator_.reset();
  ssgIndepNote_.fill(-1);
  for (auto& a : ssgActive_)
    a.note = -1;
  // All key-offs above are queued but not yet clocked; mark everything dirty so a
  // same-tick re-note still gets its retrigger gap.
  fmDirtySinceRender_.fill(true);
  ch3SpDirtySinceRender_ = true;
  samplerDirtySinceRender_ = true;
}

void PluginProcessor::clearRetriggerDirty() {
  fmDirtySinceRender_.fill(false);
  ch3SpDirtySinceRender_ = false;
  samplerDirtySinceRender_ = false;
}

void PluginProcessor::releaseResources() {
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
#if JUCE_IS_MIDI_EFFECT
  juce::ignoreUnused(layouts);
  return true;
#else
  // Only mono or stereo -- some hosts (certain GarageBand versions) require
  // stereo bus layouts.
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
      layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;

#if !JUCE_IS_SYNTH
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
#endif

  return true;
#endif
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                   juce::MidiBuffer& midiMessages) {
  juce::ScopedNoDenormals noDenormals;

  const int numSamples = buffer.getNumSamples();
  const int numOut = getTotalNumOutputChannels();

  float* left = numOut > 0 ? buffer.getWritePointer(0) : nullptr;
  if (left == nullptr)
    return;

  // Mono output: the right channel is rendered into scratch_ and discarded.
  // scratch_ is sized to samplesPerBlock in prepareToPlay, but that is only a
  // hint - a host may hand us a larger block - so grow it on the (rare) miss to
  // avoid an out-of-bounds write. Stereo writes straight to the host buffer.
  float* right;
  if (numOut > 1) {
    right = buffer.getWritePointer(1);
  } else {
    if (scratch_.size() < static_cast<size_t>(numSamples))
      scratch_.assign(static_cast<size_t>(numSamples), 0.0f);
    right = scratch_.data();
  }

  updateChipRatesIfChanged();
  updateFmIfChanged();
  updateSsgPatchIfChanged();
  updateRhythmIfChanged();
  applyPendingSample();
  updateSamplerLevel();
  updateSamplerDramIfChanged();

  const opna::PartRouting rhythmRouting = routing_.rhythm();
  const opna::PartRouting samplerRouting = routing_.sampler();
  const bool ssgSolo = routing_.ssgSolo();
  const bool rhythmSolo = routing_.rhythmSolo();
  const bool samplerSolo = routing_.samplerSolo();

  // Any solo across the whole instrument (FM group solos were cached above).
  anySolo_ = ssgSolo || rhythmSolo || samplerSolo || ch3Special_.soloed() ||
             ssgAnyIndepSolo();
  for (const auto& gs : fmGroups_)
    anySolo_ = anySolo_ || gs.solo;

  // Render the chip in segments split at MIDI event boundaries so note timing
  // is sample-accurate within the block.
  int pos = 0;
  for (const auto metadata : midiMessages) {
    const auto msg = metadata.getMessage();
    const int when = juce::jlimit(0, numSamples, metadata.samplePosition);

    if (when > pos) {
      opna_.render(left + pos, right + pos, when - pos);
      pos = when;
      clearRetriggerDirty();  // writes are now clocked; clear the gap guards
    }

    if (msg.isNoteOn()) {
      const int note = msg.getNoteNumber();
      const int ch = msg.getChannel();
      fmNoteOn(note, ch);  // per-group routing + solo handled inside
      ssgNoteOn(note, ch);  // pooled + per-channel independent routing inside
      if (opna::acceptsSolo(rhythmRouting, ch, note, rhythmSolo, anySolo_))
        rhythmNoteOn(note);
      if (opna::acceptsSolo(samplerRouting, ch, note, samplerSolo, anySolo_))
        samplerNoteOn(note, ch);
    } else if (msg.isNoteOff()) {
      // Release on all parts; each is a no-op if it wasn't holding the note.
      fmNoteOff(msg.getNoteNumber());
      ssgNoteOff(msg.getNoteNumber());
      samplerNoteOff(msg.getNoteNumber());
    } else if (msg.isPitchWheel()) {
      pitchWheel(msg.getChannel(), msg.getPitchWheelValue());
    } else if (msg.isAllNotesOff() || msg.isAllSoundOff()) {
      allNotesOff();
    }
  }

  if (numSamples > pos) {
    opna_.render(left + pos, right + pos, numSamples - pos);
    clearRetriggerDirty();
  }

  const float gain = masterGain_ != nullptr ? masterGain_->load() : 1.0f;
  buffer.applyGain(gain);

  // Global output limiter (opt-in). Stereo-linked, so when stereo it sees both
  // real output channels; when mono, only the left one (right is scratch_).
  if (limiterOn_ != nullptr && limiterOn_->load() > 0.5f)
    limiter_.process(left, numOut > 1 ? right : nullptr, numSamples);

  // Mono output: the right channel was rendered into scratch_; nothing to copy.
}

bool PluginProcessor::hasEditor() const {
  return true;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
  return new PluginEditor(*this);
}

void PluginProcessor::setFidelity(opna::OpnaChip::Fidelity f) {
  if (f == fidelity_)
    return;
  fidelity_ = f;
  // Rebuilds the resampler (allocates + changes the native rate), so suspend
  // audio processing around it. No-op-safe before prepareToPlay.
  suspendProcessing(true);
  opna_.setFidelity(f);
  suspendProcessing(false);
}

juce::File PluginProcessor::defaultRhythmRomFile() {
  return juce::File::getSpecialLocation(
             juce::File::userApplicationDataDirectory)
      .getChildFile("atan2608")
      .getChildFile("ym2608_adpcm_rom.bin");
}

void PluginProcessor::loadDefaultRhythmRom() {
  // The YM2608 rhythm ROM is exactly 8 KB; ignore anything else.
  rhythmRom_.clear();
  usingUserRom_ = false;
  const juce::File f = defaultRhythmRomFile();
  juce::MemoryBlock mb;
  if (f.existsAsFile() && f.loadFileAsData(mb) && mb.getSize() == 8192) {
    const auto* p = static_cast<const unsigned char*>(mb.getData());
    rhythmRom_.assign(p, p + mb.getSize());
  }
}

void PluginProcessor::setUserRhythmRom(const void* data, int size) {
  suspendProcessing(true);
  if (data != nullptr && size > 0) {
    const auto* p = static_cast<const unsigned char*>(data);
    rhythmRom_.assign(p, p + size);
    usingUserRom_ = true;
  } else {
    // Revert to the auto-loaded ROM from the default location.
    loadDefaultRhythmRom();
  }
  if (!rhythmRom_.empty())
    opna_.setRhythmRom(rhythmRom_.data(), static_cast<int>(rhythmRom_.size()));
  else
    opna_.setRhythmRom(nullptr, 0);
  suspendProcessing(false);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData) {
  // Build from copyState (params only); the name and sample live outside the
  // APVTS tree and are merged in here so the live tree never carries the sample.
  if (auto xml = apvts_.copyState().createXml()) {
    if (loadedPresetName_.isNotEmpty())
      xml->createNewChildElement("LoadedPresetName")->addTextElement(loadedPresetName_);

    // The revert point (clean preset state) so the editor's Revert/dirty
    // indicator survives a reopen. Wrapped so it nests one APVTS tree inside
    // another without colliding; stripped before replaceState on restore.
    if (presetBaseline_.isValid())
      if (auto baseXml = presetBaseline_.createXml())
        xml->createNewChildElement("PresetBaseline")->addChildElement(baseXml.release());

    // (Window size is a machine-local pref in the editor's prefs file, never the
    // plugin state - so it doesn't travel with a preset.)

    // The loaded ADPCM-B sample (Base64). Bounded by the editor's load-time
    // length cap; absent when no sample is loaded.
    if (!loadedSampleData_.empty()) {
      auto* s = xml->createNewChildElement("Sample");
      s->setAttribute("rate", loadedSampleRate_);
      s->addTextElement(juce::Base64::toBase64(loadedSampleData_.data(),
                                               loadedSampleData_.size()));
    }
    copyXmlToBinary(*xml, destData);
  }
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
  if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
    if (xml->hasTagName(apvts_.state.getType())) {
      if (auto* nameEl = xml->getChildByName("LoadedPresetName"))
        loadedPresetName_ = nameEl->getAllSubText();
      else
        loadedPresetName_.clear();

      // Restore the revert point; a blob without one falls back to the restored
      // state itself (treated as clean) further below.
      juce::ValueTree restoredBaseline;
      if (auto* baseEl = xml->getChildByName("PresetBaseline"))
        if (auto* treeEl = baseEl->getFirstChildElement())
          restoredBaseline = juce::ValueTree::fromXml(*treeEl);

      if (auto* s = xml->getChildByName("Sample")) {
        juce::MemoryOutputStream mos;
        if (juce::Base64::convertFromBase64(mos, s->getAllSubText()) &&
            mos.getDataSize() > 0)
          loadEncodedSample(static_cast<const unsigned char*>(mos.getData()),
                            static_cast<int>(mos.getDataSize()),
                            s->getDoubleAttribute("rate", 44100.0));
        else
          unloadSample();
      } else {
        unloadSample();
      }

      // Strip the non-APVTS children so they don't pollute the live tree.
      xml->deleteAllChildElementsWithTagName("LoadedPresetName");
      xml->deleteAllChildElementsWithTagName("Sample");
      xml->deleteAllChildElementsWithTagName("PresetBaseline");
      apvts_.replaceState(juce::ValueTree::fromXml(*xml));

      // A blob without a stored baseline is treated as clean: the restored state
      // becomes its own revert point.
      presetBaseline_ =
          restoredBaseline.isValid() ? restoredBaseline : apvts_.copyState();
    }
  }
}
}  // namespace audio_plugin

// Must be in the global namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
  return new audio_plugin::PluginProcessor();
}
