#pragma once

#include "OpnaLookAndFeel.h"

namespace audio_plugin {

// A small square toolbar button that paints a gear glyph. The embedded PC-9800
// face is ASCII-only (no gear codepoint), so the settings affordance is drawn
// rather than typed; the bevelled chrome matches the other toolbar buttons.
class GearButton : public juce::Button {
public:
  GearButton() : juce::Button("SETTINGS") {}

  void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
    auto r = getLocalBounds();
    g.setColour(OpnaColours::panel);
    g.fillRect(r);
    if (highlighted) {
      g.setColour(juce::Colours::white.withAlpha(0.07f));
      g.fillRect(r);
    }
    OpnaLookAndFeel::drawBezel(g, r, !down, 2);

    // Gear silhouette: a toothed ring with a punched hub hole, centred.
    const auto c = r.toFloat().getCentre();
    const float ring = juce::jmin(r.getWidth(), r.getHeight()) * 0.30f;
    const float hub = ring * 0.42f;
    constexpr int teeth = 8;
    juce::Path gear;
    gear.addEllipse(c.x - ring, c.y - ring, ring * 2.0f, ring * 2.0f);
    for (int i = 0; i < teeth; ++i) {
      const float a =
          (float)i / teeth * juce::MathConstants<float>::twoPi;
      const float tw = ring * 0.55f, th = ring * 0.55f;
      juce::Path tooth;
      tooth.addRectangle(-tw * 0.5f, -ring - th * 0.5f, tw, th);
      tooth.applyTransform(
          juce::AffineTransform::rotation(a).translated(c.x, c.y));
      gear.addPath(tooth);
    }
    g.setColour(highlighted ? OpnaColours::cyan : OpnaColours::dim);
    g.fillPath(gear);

    g.setColour(OpnaColours::panel);
    g.fillEllipse(c.x - hub, c.y - hub, hub * 2.0f, hub * 2.0f);
  }
};

}  // namespace audio_plugin
