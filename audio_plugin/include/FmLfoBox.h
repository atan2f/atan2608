#pragma once

#include "ParamWidgets.h"

namespace audio_plugin {

// The chip's single global LFO (RATE + ENABLE), drawn as a titled
// "LFO (FM GLOBAL)" panel. Bound to the shared fm_lfo_* parameters, so it shows
// the same state on every FM page (the LFO is global, not per-part).
class FmLfoBox : public juce::Component {
public:
  explicit FmLfoBox(juce::AudioProcessorValueTreeState& apvts);
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  ParamBar rate_;
  ParamToggle on_;
};

}  // namespace audio_plugin
