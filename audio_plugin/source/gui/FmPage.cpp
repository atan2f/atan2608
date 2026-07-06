#include "FmPage.h"

namespace audio_plugin {

FmPage::FmPage(juce::AudioProcessorValueTreeState& apvts) : partPanel_(apvts) {
  addAndMakeVisible(partPanel_);
}

void FmPage::resized() {
  partPanel_.setBounds(getLocalBounds());
}

}  // namespace audio_plugin
