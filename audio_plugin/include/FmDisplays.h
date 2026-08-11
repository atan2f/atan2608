#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>

#include "OpnaLookAndFeel.h"

namespace audio_plugin {

// Carrier bitmask (bit op = carrier) for one of the 8 OPN algorithms.
int algoCarrierMask(int algo);

// Draws the FM algorithm routing diagram for the currently-edited group, polling
// the group's algorithm/feedback parameters so it stays in sync with the knobs.
class AlgorithmDisplay : public juce::Component, private juce::Timer {
public:
  explicit AlgorithmDisplay(juce::AudioProcessorValueTreeState& apvts);
  ~AlgorithmDisplay() override;

  void setGroup(int group);  // 1-based; binds to fmGroupParamId(group, ...)
  // Bind to an arbitrary parameter source (e.g. the CH3 special part, which
  // doesn't live in the normal FM group namespace). idFor(field) -> param id.
  void setParamIds(std::function<juce::String(const char*)> idFor);
  void setAccent(juce::Colour c);
  void paint(juce::Graphics&) override;

private:
  void timerCallback() override;
  float param(const char* name) const;

  juce::AudioProcessorValueTreeState& apvts_;
  int group_ = 1;
  std::function<juce::String(const char*)> idFor_;  // null -> use group_
  int lastAlgo_ = -1;
  int lastFeedback_ = -1;
  juce::Colour accent_ = OpnaColours::cyan;
};

// Draws an approximate envelope shape (Attack / Decay1 / Sustain / Decay2 /
// Release) for one operator of the edited group.
class EnvelopeDisplay : public juce::Component, private juce::Timer {
public:
  explicit EnvelopeDisplay(juce::AudioProcessorValueTreeState& apvts);
  ~EnvelopeDisplay() override;

  void setTarget(int group, int op);  // group 1-based, op 0-based
  void setAccent(juce::Colour c);
  void paint(juce::Graphics&) override;

private:
  void timerCallback() override;
  int op(const char* field) const;

  juce::AudioProcessorValueTreeState& apvts_;
  int group_ = 1;
  int opIndex_ = 0;
  long long signature_ = -1;  // packed param values to detect change
  juce::Colour accent_ = OpnaColours::cyan;
};

}  // namespace audio_plugin
