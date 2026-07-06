#pragma once

#include <array>
#include <vector>
#include "AdpcmBEncoder.h"
#include "AdpcmBRegisterEncoder.h"
#include "Ch3SpecialPart.h"
#include "Limiter.h"
#include "OpnaChip.h"
#include "PatchParameters.h"
#include "RegisterEncoder.h"
#include "SamplerParameters.h"
#include "RhythmParameters.h"
#include "RhythmPatch.h"
#include "RhythmRegisterEncoder.h"
#include "SsgParameters.h"
#include "SsgPatch.h"
#include "SsgRegisterEncoder.h"
#include "VoiceAllocator.h"

namespace audio_plugin {
class PluginProcessor : public juce::AudioProcessor, private juce::AsyncUpdater {
public:
  PluginProcessor();

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
  using AudioProcessor::processBlock;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String& newName) override;

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  juce::AudioProcessorValueTreeState& getApvts() { return apvts_; }

  // Name of the currently-loaded user preset (persisted in the plugin state so
  // the editor can reselect it). Empty when the state isn't a saved preset.
  const juce::String& getLoadedPresetName() const { return loadedPresetName_; }
  void setLoadedPresetName(const juce::String& name) { loadedPresetName_ = name; }

  // The "clean" APVTS state of the loaded preset (the revert point). Persisted in
  // the plugin state so the editor's dirty/Revert indicator survives a reopen:
  // the editor compares the live state to this to decide whether the patch was
  // modified. Captured by the editor on every load / Init / save.
  const juce::ValueTree& getPresetBaseline() const { return presetBaseline_; }
  void setPresetBaseline(juce::ValueTree baseline) {
    presetBaseline_ = std::move(baseline);
  }

  // Load a mono PCM sample for the ADPCM-B sampler part (called from the
  // message thread; applied to the chip on the audio thread).
  void loadSample(const float* mono, int numSamples, double sampleRate);

  // Unload the current sampler sample (applied on the audio thread).
  void unloadSample();

  // Load pre-encoded ADPCM-B bytes directly (used when restoring a sample from a
  // saved preset; skips the PCM->ADPCM encode step).
  void loadEncodedSample(const unsigned char* data, int size, double sampleRate);

  // --- App-level settings (driven by the editor's Settings menu) ---

  // Oversampling fidelity. Reconfigures the chip with audio processing suspended
  // (rebuilds the resampler), so it is safe to call from the message thread.
  void setFidelity(opna::OpnaChip::Fidelity f);
  opna::OpnaChip::Fidelity fidelity() const { return fidelity_; }

  // Supply a user rhythm ROM (a manual override) or, with data==nullptr, revert
  // to the auto-loaded ROM from the default location. Applied with processing
  // suspended.
  void setUserRhythmRom(const void* data, int size);
  bool usingUserRhythmRom() const { return usingUserRom_; }
  // Whether any rhythm ROM is currently loaded (default-location or override).
  // When false the ADPCM-A rhythm part is silent.
  bool hasRhythmRom() const { return !rhythmRom_.empty(); }

  // The default rhythm-ROM path the plugin auto-loads at startup:
  // %APPDATA%/atan2608/ym2608_adpcm_rom.bin. The copyrighted ROM is not embedded,
  // so the user drops it here (or loads one manually in Settings).
  static juce::File defaultRhythmRomFile();

  // The currently-loaded sample as encoded ADPCM-B bytes (message-thread state,
  // serialised into presets) and its source sample rate. Empty when no sample.
  const std::vector<unsigned char>& loadedSampleData() const { return loadedSampleData_; }
  double loadedSampleRate() const { return loadedSampleRate_; }

private:
  // Push changed FM state (per-group patches, per-channel group/pan, global LFO)
  // to the chip. Cheap no-op when nothing changed since the last call.
  void updateFmIfChanged();
  void rebuildFmGroups();        // recompute group membership + allocators
  void encodeFmChannel(int hwChannel);  // re-push one channel's patch + pan
  void updateSsgPatchIfChanged();
  // Re-read the chip clock/prescale parameters and, when they change, apply them
  // to the chip (RT-safe: register writes + in-place resample-ratio update).
  void updateChipRatesIfChanged();
  void rebuildSsgPool();  // recompute the pooled-channel set + allocator
  // Emit one SSG voice on a hardware channel (tone + amplitude + envelope
  // re-trigger). Shared by the pooled and independent dispatch paths.
  void emitSsgVoice(int midiNote, int channel, int msgChannel,
                    const opna::SsgVoice& voice);
  bool ssgAnyIndepSolo() const;
  void updateRhythmIfChanged();
  void fmNoteOn(int midiNote, int msgChannel);
  void fmNoteOff(int midiNote);
  // Pitch wheel: compute the per-channel bend, store it, and re-pitch every
  // sounding note (no key-on, so envelopes keep running) on each affected part.
  void pitchWheel(int msgChannel, int wheelValue);
  void fmPitchBend(int msgChannel, double bend);   // FM groups + CH3 special
  void ssgPitchBend(int msgChannel, double bend);  // pooled + independent SSG
  void samplerPitchBend(int msgChannel, double bend);  // ADPCM-B delta-N
  void ssgNoteOn(int midiNote, int msgChannel);
  void ssgNoteOff(int midiNote);
  void rhythmNoteOn(int midiNote);
  void applyPendingSample();
  void updateSamplerLevel();
  // Build the ADPCM-B DRAM image from the canonical sample and stage it for the
  // audio thread (message thread only). When re-encode-start is on and START is
  // non-zero, the data is re-encoded with a fresh predictor at the start point so
  // a mid-sample start plays at full level; otherwise the canonical data is sent
  // as-is. Reused by sample load and by re-encode/start parameter changes.
  void stageSamplerDram();
  // Audio-thread check: if the re-encode/start choice changed the desired DRAM
  // image, request a rebuild on the message thread (via the AsyncUpdater).
  void updateSamplerDramIfChanged();
  void handleAsyncUpdate() override;  // runs stageSamplerDram() on the msg thread
  void samplerNoteOn(int midiNote, int msgChannel);
  void samplerNoteOff(int midiNote);
  void allNotesOff();
  // Clear the retrigger "dirty since render" guards. Called after each chip
  // render in processBlock, once queued key writes have been clocked.
  void clearRetriggerDirty();

  juce::AudioProcessorValueTreeState apvts_;
  juce::String loadedPresetName_;
  juce::ValueTree presetBaseline_;  // revert point; see get/setPresetBaseline
  PatchParameters patchParams_;
  SsgParameters ssgParams_;
  RhythmParameters rhythmParams_;
  SamplerParameters samplerParams_;
  RoutingParameters routing_;
  std::atomic<float>* masterGain_ = nullptr;
  std::atomic<float>* limiterOn_ = nullptr;
  opna::Limiter limiter_;  // global output peak limiter (opt-in via "limiter")

  // FM is organised into timbre groups. Each group owns one patch + routing and
  // a polyphony pool made of its member hardware channels.
  struct FmGroupState {
    opna::FmPatch patch;
    opna::PartRouting routing;
    bool solo = false;
    std::array<int, opna::kNumFmChannels> members{};  // hardware channel indices
    int memberCount = 0;
    opna::VoiceAllocator allocator{1};
  };
  std::array<FmGroupState, opna::kNumFmGroups> fmGroups_;
  std::array<opna::FmChannelConfig, opna::kNumFmChannels> fmChannels_;
  opna::FmGlobal fmGlobal_;
  bool fmInitialised_ = false;

  // Chip prescale (master clock fixed), built from the chip_prescale param.
  // The derived effective clocks are threaded into every note encoder so the
  // chip stays in tune across prescale changes.
  opna::ChipRates chipRates_;
  bool chipRatesInitialised_ = false;

  // CH3 special (multi-frequency) pseudo-part. Self-contained: when enabled it
  // takes over physical channel 3 and the FM path delegates to it.
  opna::Ch3SpecialPart ch3Special_;

  // True while any part (FM group, SSG, rhythm or sampler) is soloed. Refreshed
  // each block; gates note dispatch so only soloed parts sound.
  bool anySolo_ = false;

  // Pitch bend (FM, SSG and ADPCM-B). Bend is a per-MIDI-channel message; track
  // the current bend in semitones for each of the 16 channels, and which note +
  // source channel is sounding on each hardware voice so held notes can be
  // re-pitched live when the wheel moves. `note` holds the *played* (transposed)
  // pitch so the bend math matches what the chip was programmed at. Fixed +/-2
  // semitone range. SSG independent + pooled channels share ssgActive_; the
  // single sampler voice uses samplerPlayedNote_/samplerMsgChannel_ below.
  static constexpr double kBendRangeSemitones = 2.0;
  struct FmActiveNote {
    int note = -1;        // -1 = idle
    int msgChannel = 1;   // MIDI channel that triggered it (1-16)
  };
  std::array<FmActiveNote, opna::kNumFmChannels> fmActive_{};
  std::array<FmActiveNote, opna::SsgState::kNumChannels> ssgActive_{};
  std::array<double, 17> channelBend_{};  // index 1-16, semitones

  // Chip-accurate retrigger bookkeeping. The chip only edge-detects key-on at
  // clock time, so a key-off and key-on issued within one render segment (e.g. a
  // tracker's NoteOff+NoteOn on one tick) collapse to "still on" and never
  // re-attack. A note-on therefore forces a one-native-frame gap whenever the
  // target voice is currently keyed (tracked by fmActive_/samplerNote_/isKeyed)
  // OR a key write to it is still un-clocked from earlier in this block. These
  // flags carry that "touched since the last render" state; they are cleared
  // after every opna_.render(...) in processBlock once the writes are clocked.
  std::array<bool, opna::kNumFmChannels> fmDirtySinceRender_{};
  bool ch3SpDirtySinceRender_ = false;
  bool samplerDirtySinceRender_ = false;

  opna::SsgState currentSsg_;
  bool ssgInitialised_ = false;
  // Pooled-channel set: ssgPooled_[0..ssgPooledCount_) are the hardware channel
  // indices currently in Pooled mode; the ssgAllocator_ hands out slots into
  // this list. Independent channels are driven directly (mono, own routing) and
  // their currently-sounding note is tracked in ssgIndepNote_ (-1 = idle).
  std::array<int, opna::SsgState::kNumChannels> ssgPooled_{};
  int ssgPooledCount_ = 0;
  std::array<int, opna::SsgState::kNumChannels> ssgIndepNote_{};
  opna::RhythmPatch currentRhythm_;
  bool rhythmInitialised_ = false;

  // ADPCM-B sampler. Encoded data is staged on the message thread and applied
  // to the chip on the audio thread under a non-blocking spin lock.
  juce::SpinLock samplerLock_;
  std::vector<unsigned char> encodedPending_;
  // Canonical loaded-sample state, owned and mutated only on the message thread
  // (so getStateInformation can serialise it without locking). The audio thread
  // receives a copy via encodedPending_ under samplerLock_.
  std::vector<unsigned char> loadedSampleData_;
  double loadedSampleRate_ = 0.0;
  double pendingSampleRate_ = 0.0;
  bool pendingSample_ = false;
  bool pendingUnload_ = false;
  bool sampleLoaded_ = false;
  int sampleBytes_ = 0;
  int sampleRootDeltaN_ = 0;
  int lastSamplerLevel_ = -1;
  int lastSamplerPan_ = -1;
  int samplerNote_ = -1;
  // Pitch-bend bookkeeping for the single sampler voice: the played (transposed)
  // note its delta-N was computed from and the MIDI channel that triggered it.
  // Only read while samplerNote_ >= 0.
  int samplerPlayedNote_ = -1;
  int samplerMsgChannel_ = 1;
  // Signature of the DRAM image currently staged/resident: -1 = canonical data
  // (re-encode off or start 0), otherwise the start unit it was re-encoded for.
  // Audio thread compares the desired signature to this to detect a needed
  // rebuild; -2 forces the first check to evaluate.
  int lastDramStartUnit_ = -2;
  // Source sample rate of the loaded sample (audio-thread copy), used to
  // recompute the root delta-N when the chip clock/prescale changes.
  double sampleSourceRate_ = 0.0;

  // App-level settings state.
  opna::OpnaChip::Fidelity fidelity_ = opna::OpnaChip::Fidelity::Med;
  // Active rhythm ROM bytes (empty -> no ROM -> rhythm silent). Loaded from the
  // default location at startup; a manual override replaces it.
  std::vector<unsigned char> rhythmRom_;
  bool usingUserRom_ = false;  // true once a manual override is loaded
  // Load the default-location ROM into rhythmRom_ (clears it if absent/invalid).
  void loadDefaultRhythmRom();

  opna::OpnaChip opna_;
  opna::VoiceAllocator ssgAllocator_{opna::SsgRegisterEncoder::kNumChannels};
  std::vector<float> scratch_;             // discard buffer for mono output
  std::vector<opna::RegWrite> regScratch_;  // reused so events don't allocate

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}  // namespace audio_plugin
