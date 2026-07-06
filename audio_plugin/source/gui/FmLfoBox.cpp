#include "FmLfoBox.h"

#include "FmPanelLayout.h"

namespace audio_plugin {

FmLfoBox::FmLfoBox(juce::AudioProcessorValueTreeState& apvts) {
  addAndMakeVisible(rate_);
  addAndMakeVisible(on_);
  rate_.attach(apvts, "fm_lfo_rate", "RATE");
  rate_.setAccent(OpnaColours::cyan);
  on_.attach(apvts, "fm_lfo_on", "ENABLE");
}

void FmLfoBox::paint(juce::Graphics& g) {
  OpnaLookAndFeel::drawPanel(g, getLocalBounds(), OpnaColours::cyan,
                             "LFO  (FM GLOBAL)");
}

void FmLfoBox::resized() {
  auto c = FmLayout::panelContent(getLocalBounds());
  rate_.setBounds(c.removeFromTop(52));
  c.removeFromTop(10);
  on_.setBounds(c.removeFromTop(24));
}

}  // namespace audio_plugin
