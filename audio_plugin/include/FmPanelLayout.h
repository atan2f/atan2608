#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

#include "OpnaLookAndFeel.h"

namespace audio_plugin {

// Shared geometry for the FM part page and the CH3 special page so the two stay
// pixel-aligned: a top band of four columns (matrix | LFO | ALGORITHM | globals)
// above a 2x2 operator-card grid. Both panels reference these constants so a
// column-width tweak lands in one place.
namespace FmLayout {

constexpr int kTopBandH = 186;
constexpr int kColGap = 8;
constexpr int kMatrixW = 212;  // col1: part tabs + routing matrix
constexpr int kLfoW = 150;     // col2: global LFO box
constexpr int kAlgoW = 340;    // col3: algorithm selector + diagram
// col4 (part/CH3-special globals) takes the remaining width.

// Content rect of a titled panel (mirrors OpnaLookAndFeel::drawPanel).
inline juce::Rectangle<int> panelContent(juce::Rectangle<int> panel) {
  return panel.withTrimmedTop(OpnaLookAndFeel::kTitleH).reduced(4);
}

// The 2x2 operator-card rectangles filling `area` (OP1 OP2 / OP3 OP4).
inline std::array<juce::Rectangle<int>, 4> operatorGrid(juce::Rectangle<int> area,
                                                        int gap = kColGap) {
  const int cw = (area.getWidth() - gap) / 2;
  const int ch = (area.getHeight() - gap) / 2;
  const int x = area.getX(), y = area.getY();
  return {juce::Rectangle<int>{x, y, cw, ch},
          juce::Rectangle<int>{x + cw + gap, y, cw, ch},
          juce::Rectangle<int>{x, y + ch + gap, cw, ch},
          juce::Rectangle<int>{x + cw + gap, y + ch + gap, cw, ch}};
}

}  // namespace FmLayout
}  // namespace audio_plugin
