#include "RhythmParameters.h"

namespace audio_plugin {

namespace {
const char* kInstrumentIds[6] = {"rhy_bd", "rhy_sd", "rhy_top",
                                 "rhy_hh", "rhy_tom", "rhy_rim"};
const char* kInstrumentNames[6] = {"RHY BASS", "RHY SNARE", "RHY CYMBAL",
                                   "RHY HI-HAT", "RHY TOM", "RHY RIM"};
const char* kPanNames[6] = {"RHY BASS PAN", "RHY SNARE PAN", "RHY CYMBAL PAN",
                            "RHY HI-HAT PAN", "RHY TOM PAN", "RHY RIM PAN"};

juce::String rhyPanId(int i) {
  return juce::String(kInstrumentIds[i]) + "_pan";
}

int rhyAsInt(const std::atomic<float>* p) {
  return p != nullptr ? static_cast<int>(p->load() + 0.5f) : 0;
}
}  // namespace

void addRhythmParameters(
    juce::AudioProcessorValueTreeState::ParameterLayout& layout) {
  using Int = juce::AudioParameterInt;
  using Choice = juce::AudioParameterChoice;
  const opna::RhythmPatch d;

  layout.add(std::make_unique<Int>(juce::ParameterID{"rhy_tl", 1},
                                   "RHY TOTAL", 0, 63, d.totalLevel));
  for (int i = 0; i < 6; ++i) {
    layout.add(std::make_unique<Int>(juce::ParameterID{kInstrumentIds[i], 1},
                                     kInstrumentNames[i], 0, 31, d.level[i]));
    layout.add(std::make_unique<Choice>(juce::ParameterID{rhyPanId(i), 1},
                                        kPanNames[i],
                                        juce::StringArray{"L", "C", "R"}, d.pan[i]));
  }
}

void RhythmParameters::connect(juce::AudioProcessorValueTreeState& apvts) {
  totalLevel_ = apvts.getRawParameterValue("rhy_tl");
  for (int i = 0; i < 6; ++i) {
    level_[(size_t)i] = apvts.getRawParameterValue(kInstrumentIds[i]);
    pan_[(size_t)i] = apvts.getRawParameterValue(rhyPanId(i));
  }
}

opna::RhythmPatch RhythmParameters::build() const {
  opna::RhythmPatch p;
  p.totalLevel = rhyAsInt(totalLevel_);
  for (int i = 0; i < 6; ++i) {
    p.level[i] = rhyAsInt(level_[(size_t)i]);
    p.pan[i] = rhyAsInt(pan_[(size_t)i]);
  }
  return p;
}

}  // namespace audio_plugin
