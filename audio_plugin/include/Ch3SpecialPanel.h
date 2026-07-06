#pragma once

#include <array>
#include <memory>

#include "FmAlgoPanel.h"
#include "FmLfoBox.h"
#include "ParamWidgets.h"

namespace audio_plugin {

// One operator's controls for the CH3 special pseudo-part: the standard timbre
// bars plus the per-operator pitch strip (coarse/cents/key-follow/key-enable)
// that only special mode exposes. Self-contained (attaches to ch3sp_ parameter
// ids) so it doesn't entangle the normal OperatorCard.
class Ch3OperatorCard : public juce::Component {
public:
  Ch3OperatorCard(juce::AudioProcessorValueTreeState& apvts, int op);
  void setAccent(juce::Colour c);
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  int op_ = 0;
  juce::Colour accent_ = OpnaColours::ch3sp;
  // Envelope row + timbre row (mirrors the normal operator card).
  ParamBar attack_, decay1_, sustain_, decay2_, release_;
  ParamBar multiple_, detune_, level_, rateScale_, ssgEg_;
  ParamToggle am_;
  // Special-mode per-operator pitch.
  ParamBar coarse_, cents_;
  ParamToggle follow_, keyEnable_;
};

// The CH3 special-mode editor. When special mode is enabled this pseudo-part
// takes over all of physical channel 3 and gives each operator an independent
// pitch. It owns the full CH3 patch (algorithm/feedback/AMS/PMS/pan) plus the
// per-operator pitch controls. Deliberately terse: this is an advanced, rarely
// used hardware mode and the labels assume the user knows why they're here.
class Ch3SpecialPanel : public juce::Component {
public:
  explicit Ch3SpecialPanel(juce::AudioProcessorValueTreeState& apvts);
  void paint(juce::Graphics&) override;
  void resized() override;

private:
  juce::Colour accent() const { return OpnaColours::ch3sp; }

  // Same shared furniture as a normal part page -- the global LFO box and the
  // algorithm selector + diagram -- here bound to the ch3sp_ parameters. Pan
  // lives in the routing matrix's CH3 PAN cell and MIDI routing/solo on the
  // GLOBAL page, so the only globals left are FB/AMS/PMS.
  FmLfoBox lfo_;
  FmAlgoPanel algo_;
  ParamBar feedback_, ams_, pms_;
  // CSM (Composite Sine Mode): Timer A auto-retriggers the four operators so the
  // trigger rate is the pitch and the operators act as formants.
  ParamToggle csm_;

  std::array<std::unique_ptr<Ch3OperatorCard>, 4> ops_;

  juce::Rectangle<int> globalsBounds_;
};

}  // namespace audio_plugin
