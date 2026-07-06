#pragma once

#include <array>
#include <memory>

#include "Ch3SpecialPanel.h"
#include "FmAlgoPanel.h"
#include "FmConfig.h"
#include "FmDisplays.h"
#include "FmLfoBox.h"
#include "ParamWidgets.h"
#include "RoutingMatrix.h"

namespace audio_plugin {

// One operator's controls: an envelope graph plus value bars grouped by
// function. Tinted with the owning part's accent colour.
class OperatorCard : public juce::Component {
public:
  explicit OperatorCard(juce::AudioProcessorValueTreeState& apvts);

  void setTarget(int part, int op);  // part 1-based, op 0-based
  void setAccent(juce::Colour c);
  void setRole(bool carrier);  // updates the "OP# - CARRIER/MODULATOR" header
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  juce::AudioProcessorValueTreeState& apvts_;
  int opIndex_ = 0;
  bool carrier_ = false;
  juce::Colour accent_ = OpnaColours::cyan;
  EnvelopeDisplay env_;
  // Envelope row.
  ParamBar attack_, decay1_, sustain_, decay2_, release_;
  // Pitch / level / mod row.
  ParamBar multiple_, detune_, level_, rateScale_, ssgEg_;
  ParamToggle am_;
};

// The timbre editor for one FM part: part tabs, the interactive routing matrix
// (channel->part assignment + pan), the chip-global LFO box, the algorithm
// selector + diagram + part globals, and 4 operator cards. Per-part MIDI channel
// and key range live on the central GLOBAL page. Everything part-specific is
// tinted with the part's accent; global elements stay teal.
class FmPartPanel : public juce::Component, private juce::Timer {
public:
  explicit FmPartPanel(juce::AudioProcessorValueTreeState& apvts);

  void setPart(int part);  // 1-based; kCh3SpecialPart selects the special tab
  void paint(juce::Graphics&) override;
  void resized() override;

  // Sentinel "part" for the CH3 special pseudo-part tab (one past P1..P6).
  static constexpr int kCh3SpecialPart = opna::kNumFmGroups + 1;

private:
  void timerCallback() override;
  void refreshTabs();
  void applyAccent();
  void setNormalWidgetsVisible(bool v);
  bool onSpecialTab() const { return part_ == kCh3SpecialPart; }
  juce::Colour accent() const {
    return onSpecialTab() ? OpnaColours::ch3sp : OpnaColours::part(part_);
  }

  juce::AudioProcessorValueTreeState& apvts_;
  int part_ = 1;
  int lastAlgo_ = -1;

  // CH3 special mode is enabled from the routing grid; this panel only reveals
  // its editor tab while it is on.
  std::atomic<float>* ch3spEnableParam_ = nullptr;
  bool lastCh3SpEnabled_ = false;

  std::array<juce::TextButton, opna::kNumFmGroups> tabs_;  // P1..P6
  juce::TextButton ch3spTab_;                              // CH3 special (7th)
  RoutingMatrix matrix_;

  // Shared FM-page furniture: the global LFO box (teal) and the algorithm
  // selector + diagram (reused on the CH3 special tab too).
  FmLfoBox lfo_;
  FmAlgoPanel algo_;
  ParamBar feedback_, ams_, pms_;

  std::array<std::unique_ptr<OperatorCard>, 4> ops_;

  // CH3 special-mode editor, shown only while the 7th tab is selected.
  Ch3SpecialPanel ch3spPanel_;

  // Part-globals panel rect (filled in resized(), drawn in paint()).
  juce::Rectangle<int> partBounds_;
};

}  // namespace audio_plugin
