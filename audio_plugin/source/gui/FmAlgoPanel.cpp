#include "FmAlgoPanel.h"

#include "FmPanelLayout.h"
#include "PatchParameters.h"  // fmGroupParamId

namespace audio_plugin {

FmAlgoPanel::FmAlgoPanel(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts), diagram_(apvts) {
  for (int i = 0; i < 8; ++i) {
    auto& b = buttons_[(size_t)i];
    b.setButtonText(juce::String(i + 1));
    b.setClickingTogglesState(false);
    b.onClick = [this, i] {
      if (auto* p = apvts_.getParameter(algoId()))
        p->setValueNotifyingHost(p->convertTo0to1((float)i));
    };
    addAndMakeVisible(b);
  }
  addAndMakeVisible(diagram_);
  startTimerHz(30);
}

juce::String FmAlgoPanel::algoId() const {
  return idFor_ ? idFor_("algorithm") : fmGroupParamId(group_, "algorithm");
}

int FmAlgoPanel::currentAlgo() const {
  if (auto* p = apvts_.getRawParameterValue(algoId()))
    return (int)(p->load() + 0.5f);
  return 0;
}

void FmAlgoPanel::setGroup(int group) {
  group_ = group;
  idFor_ = nullptr;
  diagram_.setGroup(group);
  retarget();
}

void FmAlgoPanel::setParamIds(std::function<juce::String(const char*)> idFor) {
  idFor_ = idFor;
  diagram_.setParamIds(idFor);
  retarget();
}

void FmAlgoPanel::setAccent(juce::Colour c) {
  accent_ = c;
  diagram_.setAccent(c);
  refreshButtons(lastAlgo_);
  repaint();
}

void FmAlgoPanel::retarget() {
  lastAlgo_ = currentAlgo();
  refreshButtons(lastAlgo_);
  repaint();
}

void FmAlgoPanel::refreshButtons(int algo) {
  for (int i = 0; i < 8; ++i) {
    auto& b = buttons_[(size_t)i];
    const bool active = i == algo;
    b.setColour(juce::TextButton::buttonColourId,
                active ? accent_ : OpnaColours::panel);
    b.setColour(juce::TextButton::textColourOffId,
                active ? OpnaColours::bg : OpnaColours::dim);
  }
}

void FmAlgoPanel::timerCallback() {
  const int algo = currentAlgo();
  if (algo != lastAlgo_) {
    lastAlgo_ = algo;
    refreshButtons(algo);  // the diagram polls itself
  }
}

void FmAlgoPanel::paint(juce::Graphics& g) {
  OpnaLookAndFeel::drawPanel(g, getLocalBounds(), accent_, "ALGORITHM");
}

void FmAlgoPanel::resized() {
  auto c = FmLayout::panelContent(getLocalBounds());
  auto sel = c.removeFromLeft(168);
  const int bh = (sel.getHeight() - 4) / 2;
  auto selTop = sel.removeFromTop(bh);
  sel.removeFromTop(4);
  auto selBot = sel;
  const int bw = selTop.getWidth() / 4;
  for (int i = 0; i < 4; ++i)
    buttons_[(size_t)i].setBounds(selTop.removeFromLeft(bw).reduced(2));
  for (int i = 4; i < 8; ++i)
    buttons_[(size_t)i].setBounds(selBot.removeFromLeft(bw).reduced(2));
  c.removeFromLeft(10);
  diagram_.setBounds(
      c.withSizeKeepingCentre(c.getWidth(), juce::jmin(c.getHeight(), 140)));
}

}  // namespace audio_plugin
