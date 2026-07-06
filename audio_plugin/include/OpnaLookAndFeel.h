#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace audio_plugin {

// PC-98 inspired palette (navy field, cyan/amber accents).
namespace OpnaColours {
inline const juce::Colour bg{0xff070b16};
inline const juce::Colour panel{0xff0f1628};
inline const juce::Colour panel2{0xff16203a};
inline const juce::Colour raised{0xff1b2746};
inline const juce::Colour ink{0xffeaf2ff};
inline const juce::Colour dim{0xffa6b6dc};  // label text; kept WCAG-AA on panel
inline const juce::Colour line{0xff3a4a78};
inline const juce::Colour cyan{0xff3fd2ff};  // reserved for GLOBAL elements
inline const juce::Colour amber{0xffffce4d};
inline const juce::Colour green{0xff6dffb0};
inline const juce::Colour ch3sp{0xff5a8cff};  // FM CH3 special-mode accent (azure)

// Hard-bevel highlight / shadow tones (PC-98 chunky panel edges).
inline const juce::Colour bevelLight{0xff3a4c7e};
inline const juce::Colour bevelDark{0xff05080f};

// Per-part accent colour (part is 1-based; 0/out-of-range -> dim). Warm,
// non-teal hues so the global cyan stays distinct.
juce::Colour part(int p);
}  // namespace OpnaColours

class OpnaLookAndFeel : public juce::LookAndFeel_V4 {
public:
  OpnaLookAndFeel();

  juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

  // Font getters so every component's text uses the PC-9800 typeface.
  juce::Font getLabelFont(juce::Label&) override;
  juce::Font getComboBoxFont(juce::ComboBox&) override;
  juce::Font getPopupMenuFont() override;
  juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

  void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                        bool shouldDrawButtonAsHighlighted,
                        bool shouldDrawButtonAsDown) override;

  // Hard-edged (no rounded corners) bevelled button background.
  void drawButtonBackground(juce::Graphics&, juce::Button&,
                            const juce::Colour& backgroundColour,
                            bool shouldDrawButtonAsHighlighted,
                            bool shouldDrawButtonAsDown) override;

  void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                        float sliderPos, float rotaryStartAngle,
                        float rotaryEndAngle, juce::Slider&) override;

  void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                    int buttonX, int buttonY, int buttonW, int buttonH,
                    juce::ComboBox&) override;

  // Chunky horizontal value bar (LinearBar style) -> drawValueBar.
  void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                        float sliderPos, float minSliderPos, float maxSliderPos,
                        juce::Slider::SliderStyle, juce::Slider&) override;

  // PC-98 styled tooltips: bevelled navy panel, amber title line, dim footer.
  void drawTooltip(juce::Graphics&, const juce::String& text, int width,
                   int height) override;
  juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
                                        juce::Point<int> screenPos,
                                        juce::Rectangle<int> parentArea) override;

  // Height of a panel's title strip (shared so content insets stay in sync).
  static constexpr int kTitleH = 22;

  // The shared PC-9800 typeface (lazily created from embedded binary data).
  static juce::Typeface::Ptr pc98Typeface();
  // A font in the PC-9800 typeface at the given pixel height.
  static juce::Font font(float height, bool bold = false);

  // ---- Reusable PC-98 chunky drawing primitives (pure Graphics helpers) ----

  // Fill an area with `steps` discrete colour bands from `top` to `bottom`
  // (the banded "16-colour" gradient look). vertical=true ramps top->bottom.
  static void drawSteppedGradient(juce::Graphics&, juce::Rectangle<int> area,
                                  juce::Colour top, juce::Colour bottom,
                                  int steps, bool vertical);

  // Hard two-tone bevel: light top/left + dark bottom/right when `raised`,
  // inverted (sunken trough) otherwise. Draws inside `bounds`.
  static void drawBezel(juce::Graphics&, juce::Rectangle<int> bounds,
                        bool raised, int thickness = 2);

  // Panel: body fill + a banded-gradient title strip tinted with `accent` +
  // raised outer bevel + title text. Returns the inner content rect (below the
  // title strip, inset from the bevel).
  static juce::Rectangle<int> drawPanel(juce::Graphics&,
                                        juce::Rectangle<int> bounds,
                                        juce::Colour accent,
                                        const juce::String& title);

  // Sunken trough filled with chunky segmented blocks up to `proportion`
  // (0..1), coloured `accent`. The knob replacement's core visual.
  static void drawValueBar(juce::Graphics&, juce::Rectangle<int> trough,
                           float proportion, juce::Colour accent);
};

}  // namespace audio_plugin
