#pragma once

#include <array>
#include <functional>

#include "FmDisplays.h"
#include "ParamWidgets.h"

namespace audio_plugin {

// The algorithm editor shared by every FM page: a 1-8 selector grid plus the
// routing diagram, drawn as a titled "ALGORITHM" panel. Re-targetable to any
// algorithm parameter -- a normal FM group (setGroup) or the CH3 special part
// (setParamIds) -- and self-refreshing (polls the bound algorithm so the
// highlight + diagram track host/preset changes).
class FmAlgoPanel : public juce::Component, private juce::Timer {
public:
  explicit FmAlgoPanel(juce::AudioProcessorValueTreeState& apvts);

  void setGroup(int group);  // 1-based; binds to fmGroupParamId(group, ...)
  void setParamIds(std::function<juce::String(const char*)> idFor);
  void setAccent(juce::Colour c);

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  void timerCallback() override;
  void refreshButtons(int algo);
  void retarget();  // re-read the current algorithm and refresh
  juce::String algoId() const;
  int currentAlgo() const;

  juce::AudioProcessorValueTreeState& apvts_;
  std::array<juce::TextButton, 8> buttons_;
  AlgorithmDisplay diagram_;
  std::function<juce::String(const char*)> idFor_;  // null -> use group_
  int group_ = 1;
  juce::Colour accent_ = OpnaColours::cyan;
  int lastAlgo_ = -1;
};

}  // namespace audio_plugin
