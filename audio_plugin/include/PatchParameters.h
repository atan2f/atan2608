#pragma once

#include <array>
#include <atomic>

#include "Ch3SpecialPart.h"
#include "FmConfig.h"
#include "FmPatch.h"

namespace audio_plugin {

// Parameter-id helpers (shared between layout creation and pointer binding so
// the strings can never drift apart). Groups and channels are 1-based in the id
// strings; the C++ indices passed in are 1-based too (group 1..kNumFmGroups,
// channel 1..kNumFmChannels) to match how they read in a DAW's parameter list.
juce::String fmOpParamId(int group, int op, const char* field);
juce::String fmGroupParamId(int group, const char* name);
juce::String fmChanParamId(int channel, const char* name);
juce::String ch3SpOpParamId(int op, const char* field);
juce::String ch3SpParamId(const char* name);

// Builds the full APVTS layout for the FM section: kNumFmGroups timbre groups
// (11 params per operator + algorithm/feedback/AMS/PMS + MIDI routing each), the
// single global LFO, and per-channel group/pan assignment.
juce::AudioProcessorValueTreeState::ParameterLayout createPatchParameterLayout();

// Cached atomic pointers into the APVTS so the audio thread can assemble the FM
// state without per-block string lookups.
class PatchParameters {
public:
  void connect(juce::AudioProcessorValueTreeState& apvts);

  // group / channel are 1-based.
  opna::FmPatch build(int group) const;
  opna::PartRouting routing(int group) const;
  bool solo(int group) const;
  opna::FmChannelConfig channelConfig(int channel) const;
  opna::FmGlobal global() const;

  // Prescale built from the chip_prescale choice (master crystal is fixed).
  opna::ChipRates chipRates() const;

  // Assemble the CH3 special-mode pseudo-part state.
  opna::Ch3SpecialState buildCh3Special() const;

private:
  struct OpPtrs {
    std::atomic<float>* detune = nullptr;
    std::atomic<float>* multiple = nullptr;
    std::atomic<float>* totalLevel = nullptr;
    std::atomic<float>* keyScale = nullptr;
    std::atomic<float>* attackRate = nullptr;
    std::atomic<float>* decayRate = nullptr;
    std::atomic<float>* sustainRate = nullptr;
    std::atomic<float>* releaseRate = nullptr;
    std::atomic<float>* sustainLevel = nullptr;
    std::atomic<float>* ssgEg = nullptr;
    std::atomic<float>* amEnable = nullptr;
  };
  struct GroupPtrs {
    std::array<OpPtrs, 4> op;
    std::atomic<float>* algorithm = nullptr;
    std::atomic<float>* feedback = nullptr;
    std::atomic<float>* ams = nullptr;
    std::atomic<float>* pms = nullptr;
    std::atomic<float>* midiChannel = nullptr;
    std::atomic<float>* lowKey = nullptr;
    std::atomic<float>* highKey = nullptr;
    std::atomic<float>* octave = nullptr;
    std::atomic<float>* solo = nullptr;
  };
  struct ChanPtrs {
    std::atomic<float>* group = nullptr;
    std::atomic<float>* pan = nullptr;
  };

  // CH3 special-mode pseudo-part: full timbre (reusing OpPtrs/GroupPtrs-style
  // fields) plus per-operator pitch controls and an enable flag.
  struct Ch3OpPitchPtrs {
    std::atomic<float>* coarse = nullptr;
    std::atomic<float>* cents = nullptr;
    std::atomic<float>* keyFollow = nullptr;
    std::atomic<float>* keyEnable = nullptr;
  };
  struct Ch3SpPtrs {
    std::array<OpPtrs, 4> op;
    std::array<Ch3OpPitchPtrs, 4> pitch;
    std::atomic<float>* algorithm = nullptr;
    std::atomic<float>* feedback = nullptr;
    std::atomic<float>* ams = nullptr;
    std::atomic<float>* pms = nullptr;
    std::atomic<float>* pan = nullptr;
    std::atomic<float>* enable = nullptr;
    std::atomic<float>* csm = nullptr;
    std::atomic<float>* midiChannel = nullptr;
    std::atomic<float>* lowKey = nullptr;
    std::atomic<float>* highKey = nullptr;
    std::atomic<float>* octave = nullptr;
    std::atomic<float>* solo = nullptr;
  };

  std::array<GroupPtrs, opna::kNumFmGroups> groups_;
  std::array<ChanPtrs, opna::kNumFmChannels> channels_;
  Ch3SpPtrs ch3sp_;
  std::atomic<float>* lfoRate_ = nullptr;
  std::atomic<float>* lfoEnable_ = nullptr;
  std::atomic<float>* chipPrescale_ = nullptr;
};

}  // namespace audio_plugin
