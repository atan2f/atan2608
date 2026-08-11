#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

#include "FmConfig.h"
#include "OpnaLookAndFeel.h"

namespace audio_plugin {

// Interactive channel->part routing grid: rows = parts (P1..P6), columns = the
// six hardware FM channels, plus a PAN row beneath. Clicking a cell assigns that
// channel to the part (or turns it off if it was already that part); clicking a
// PAN cell cycles L/C/R. It polls the params so host/preset changes show.
//
// CH3's column doubles as the CH3 special-mode control: clicking its header
// enables special mode (and clicking the then-locked column disables it). While
// special mode owns CH3 the column is locked out of normal part assignment and
// shows "SP", enforcing the one-channel-one-part rule.
class RoutingMatrix : public juce::Component, private juce::Timer {
public:
  explicit RoutingMatrix(juce::AudioProcessorValueTreeState& apvts);

  void setActivePart(int part);  // 1-based; highlights that row
  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent&) override;

private:
  // Cell geometry shared by paint and hit-testing.
  struct Geom {
    int gridX, gridY, cellW, rowH;
    juce::Rectangle<int> content;
  };
  Geom geom() const;

  void timerCallback() override;
  int channelGroup(int ch) const;  // ch 1-based; 0 = off
  int channelPan(int ch) const;    // 0=L 1=C 2=R
  int ch3SpecialPan() const;       // CH3 special-mode pan (separate parameter)
  bool ch3Special() const;         // CH3 occupied by special mode
  void setParamInt(const juce::String& id, int v);

  juce::AudioProcessorValueTreeState& apvts_;
  int activePart_ = 1;
  std::array<int, opna::kNumFmChannels> chGroup_{};
  std::array<int, opna::kNumFmChannels> chPan_{};
  int ch3SpPan_ = 1;
  bool ch3Special_ = false;
};

}  // namespace audio_plugin
