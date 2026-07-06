#pragma once

#include <array>
#include <atomic>

#include "RhythmPatch.h"

namespace audio_plugin {

void addRhythmParameters(
    juce::AudioProcessorValueTreeState::ParameterLayout& layout);

// Cached pointers for assembling a RhythmPatch on the audio thread.
class RhythmParameters {
public:
  void connect(juce::AudioProcessorValueTreeState& apvts);
  opna::RhythmPatch build() const;

private:
  std::atomic<float>* totalLevel_ = nullptr;
  std::array<std::atomic<float>*, 6> level_{};
  std::array<std::atomic<float>*, 6> pan_{};
};

}  // namespace audio_plugin
