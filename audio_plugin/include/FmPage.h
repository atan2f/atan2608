#pragma once

#include "FmPartPanel.h"

namespace audio_plugin {

// FM editing page: the part timbre editor (which itself hosts the interactive
// routing matrix, the global LFO, the algorithm selector and the operators).
class FmPage : public juce::Component {
public:
  explicit FmPage(juce::AudioProcessorValueTreeState& apvts);
  void resized() override;

private:
  FmPartPanel partPanel_;
};

}  // namespace audio_plugin
