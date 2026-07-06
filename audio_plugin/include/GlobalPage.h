#pragma once

#include <array>

#include "FmConfig.h"
#include "ParamWidgets.h"

namespace audio_plugin {

// The central "GLOBAL" page: the per-part routing table plus the chip-wide
// per-patch controls (master output gain, prescale, limiter). One row per
// instrument part (the six FM timbre groups plus SSG, Rhythm and the ADPCM-B
// sampler), each with a Solo button, a MIDI-channel selector and a low/high key
// range - the single place to set per-part MIDI routing. Solo temporarily mutes
// every non-soloed part and makes the soloed part(s) respond on all channels
// (Omni) while keeping their key range. The master/prescale/limiter controls are
// per-patch APVTS params, so they live here rather than in the machine-local gear.
class GlobalPage : public juce::Component, private juce::Timer {
public:
  explicit GlobalPage(juce::AudioProcessorValueTreeState& apvts);

  // One row per part: the six FM timbre groups, a gated FM CH3 SP row (shown
  // only while CH3 special mode is on), the SSG pooled part, SSG A/B/C, Rhythm,
  // and Sampler.
  static constexpr int kNumRows = opna::kNumFmGroups + 7;

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  // One part's controls. Aggregate so the std::array default-constructs each.
  struct Row {
    juce::Label name;
    ParamToggle solo;
    ParamCombo chan;
    ParamBar low, high, oct;
    bool hasOct = false;  // false for Rhythm (per-note drum map; transpose N/A)
    juce::Colour accent{OpnaColours::cyan};
    juce::RangedAudioParameter* soloParam = nullptr;
  };

  void timerCallback() override;
  // A row is shown when it has no gate parameter, or its gate parameter is on
  // (>= 0.5). Gated rows: the three SSG A/B/C rows (gated on that channel's
  // Independent mode) and the FM CH3 SP row (gated on special-mode enable).
  bool rowVisible(int i) const;
  void setRowVisible(int i, bool v);
  void soloClicked(int rowIndex);

  // Split a row/header rect into the six aligned columns (name, solo, MIDI CH,
  // LOW KEY, HI KEY, OCT). Shared by paint (headers) and resized (controls).
  std::array<juce::Rectangle<int>, 6> columns(juce::Rectangle<int> r) const;

  std::array<Row, kNumRows> rows_;
  // The chip-wide per-patch controls beneath the routing matrix.
  ParamBar master_;
  ParamCombo prescale_;
  ParamToggle limiter_;
  juce::Rectangle<int> footerBounds_;  // the global-controls strip (for the rule)
  juce::Rectangle<int> headerBounds_;
  // Per-row gate parameter (nullptr = always visible); the last-seen visibility
  // so the timer only re-lays-out on a change.
  std::array<std::atomic<float>*, kNumRows> gate_{};
  std::array<bool, kNumRows> visible_{};
};

}  // namespace audio_plugin
