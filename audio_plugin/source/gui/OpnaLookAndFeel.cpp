#include "OpnaLookAndFeel.h"

#include "BinaryData.h"

namespace audio_plugin {

juce::Colour OpnaColours::part(int p) {
  switch (p) {
    case 1: return juce::Colour{0xffffc24d};  // amber
    case 2: return juce::Colour{0xffff8a3d};  // orange
    case 3: return juce::Colour{0xffff5a52};  // red
    case 4: return juce::Colour{0xffff6ec7};  // magenta
    case 5: return juce::Colour{0xffb98cff};  // violet
    case 6: return juce::Colour{0xffd6ff5a};  // lime
    default: return OpnaColours::dim;
  }
}

juce::Typeface::Ptr OpnaLookAndFeel::pc98Typeface() {
  static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
      BinaryData::pc9800_ttf, (size_t)BinaryData::pc9800_ttfSize);
  return tf;
}

OpnaLookAndFeel::OpnaLookAndFeel() {
  setColour(juce::ResizableWindow::backgroundColourId, OpnaColours::bg);
  setColour(juce::Label::textColourId, OpnaColours::ink);

  setColour(juce::ComboBox::backgroundColourId, OpnaColours::panel2);
  setColour(juce::ComboBox::textColourId, OpnaColours::ink);
  setColour(juce::ComboBox::outlineColourId, OpnaColours::line);
  setColour(juce::ComboBox::arrowColourId, OpnaColours::cyan);
  setColour(juce::PopupMenu::backgroundColourId, OpnaColours::panel2);
  setColour(juce::PopupMenu::textColourId, OpnaColours::ink);
  setColour(juce::PopupMenu::highlightedBackgroundColourId, OpnaColours::cyan);
  setColour(juce::PopupMenu::highlightedTextColourId, OpnaColours::bg);

  setColour(juce::TextButton::buttonColourId, OpnaColours::panel2);
  setColour(juce::TextButton::textColourOffId, OpnaColours::ink);
  setColour(juce::TextButton::textColourOnId, OpnaColours::bg);

  setColour(juce::ToggleButton::textColourId, OpnaColours::dim);
  setColour(juce::ToggleButton::tickColourId, OpnaColours::cyan);
  setColour(juce::ToggleButton::tickDisabledColourId, OpnaColours::line);

  setColour(juce::Slider::textBoxTextColourId, OpnaColours::amber);
  setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

juce::Typeface::Ptr OpnaLookAndFeel::getTypefaceForFont(const juce::Font&) {
  return pc98Typeface();
}

juce::Font OpnaLookAndFeel::font(float height, bool bold) {
  auto tf = pc98Typeface();
  if (tf != nullptr) {
    // The embedded PC-9800 face is single-weight. Do NOT call setBold(): on the
    // macOS CoreText backend that re-resolves the font by family + style flags,
    // fails to find a bold variant, and silently falls back to the system sans
    // (this is why the panel headers lost the retro font on Mac but not Win).
    return juce::Font(juce::FontOptions(tf).withHeight(height));
  }
  juce::Font f{juce::FontOptions(height)};
  f.setBold(bold);
  return f;
}

juce::Font OpnaLookAndFeel::getLabelFont(juce::Label& l) {
  return font(l.getFont().getHeight(), l.getFont().isBold());
}
juce::Font OpnaLookAndFeel::getComboBoxFont(juce::ComboBox&) {
  return font(14.0f);
}
juce::Font OpnaLookAndFeel::getPopupMenuFont() {
  return font(15.0f);
}
juce::Font OpnaLookAndFeel::getTextButtonFont(juce::TextButton&, int h) {
  return font(juce::jmin(15.0f, (float)h * 0.6f));
}

void OpnaLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                       bool, bool) {
  const float fontSize = juce::jmin(15.0f, (float)button.getHeight() * 0.75f);
  const int boxSize = juce::roundToInt(fontSize * 1.15f);
  auto box = juce::Rectangle<int>(2, (button.getHeight() - boxSize) / 2, boxSize,
                                  boxSize);
  // Sunken square well (no rounded corners).
  g.setColour(OpnaColours::bg);
  g.fillRect(box);
  drawBezel(g, box, false, 1);
  if (button.getToggleState()) {
    g.setColour(button.findColour(juce::ToggleButton::tickColourId));
    g.fillRect(box.reduced(3));
  }

  g.setColour(button.findColour(juce::ToggleButton::textColourId));
  g.setFont(font(fontSize));
  if (!button.isEnabled())
    g.setOpacity(0.5f);
  g.drawFittedText(button.getButtonText(),
                   button.getLocalBounds()
                       .withTrimmedLeft(boxSize + 8)
                       .withTrimmedRight(2),
                   juce::Justification::centredLeft, 10);
}

void OpnaLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                           const juce::Colour& backgroundColour,
                                           bool highlighted, bool down) {
  auto r = b.getLocalBounds();
  g.setColour(backgroundColour);
  g.fillRect(r);
  if (highlighted) {
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.fillRect(r);
  }
  drawBezel(g, r, !down, 2);
}

void OpnaLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                       int height, float sliderPos,
                                       float startAngle, float endAngle,
                                       juce::Slider& slider) {
  const auto bounds =
      juce::Rectangle<int>(x, y, width, height).toFloat().reduced(3.0f);
  const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
  const auto centre = bounds.getCentre();
  const float angle = startAngle + sliderPos * (endAngle - startAngle);

  // Body.
  g.setColour(OpnaColours::raised);
  g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
  g.setColour(OpnaColours::line);
  g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f,
                1.0f);

  // Value arc.
  const float arcR = radius - 2.0f;
  juce::Path track;
  track.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle,
                      true);
  g.setColour(OpnaColours::line);
  g.strokePath(track, juce::PathStrokeType(2.0f));

  juce::Path value;
  value.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
  g.setColour(OpnaColours::cyan);
  g.strokePath(value, juce::PathStrokeType(2.0f));
  juce::ignoreUnused(slider);

  // Pointer.
  juce::Path pointer;
  const float pw = 2.0f;
  pointer.addRoundedRectangle(-pw * 0.5f, -radius + 2.0f, pw, radius * 0.6f, 1.0f);
  pointer.applyTransform(
      juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
  g.setColour(OpnaColours::cyan);
  g.fillPath(pointer);
}

void OpnaLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                   int, int, int, int, juce::ComboBox& box) {
  const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
  g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
  g.fillRect(bounds);
  g.setColour(box.findColour(juce::ComboBox::outlineColourId));
  g.drawRect(bounds, 1.0f);

  juce::Path arrow;
  const float ah = 3.0f;
  const float ax = (float)width - 12.0f;
  const float ay = (float)height * 0.5f;
  arrow.addTriangle(ax, ay - ah, ax + 8.0f, ay - ah, ax + 4.0f, ay + ah);
  g.setColour(box.findColour(juce::ComboBox::arrowColourId));
  g.fillPath(arrow);
}

void OpnaLookAndFeel::positionComboBoxText(juce::ComboBox& box,
                                           juce::Label& label) {
  // Leave room on the right for the drop arrow (drawn at width-12).
  label.setBounds(4, 1, box.getWidth() - 18, box.getHeight() - 2);
  label.setFont(getComboBoxFont(box));
  // Truncate with an ellipsis rather than squishing the glyphs horizontally
  // (JUCE's default 0.7 minimum scale) when the preset name overruns the box.
  label.setMinimumHorizontalScale(1.0f);
}

void OpnaLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width,
                                       int height, float sliderPos, float,
                                       float, juce::Slider::SliderStyle,
                                       juce::Slider& slider) {
  const auto trough = juce::Rectangle<int>(x, y, width, height);
  const float span = (float)width;
  const float prop = span > 0.0f ? (sliderPos - (float)x) / span : 0.0f;
  const auto accent = slider.findColour(juce::Slider::trackColourId);
  drawValueBar(g, trough, juce::jlimit(0.0f, 1.0f, prop), accent);
}

// --------------------------------------------------------- drawing primitives
void OpnaLookAndFeel::drawSteppedGradient(juce::Graphics& g,
                                          juce::Rectangle<int> area,
                                          juce::Colour top, juce::Colour bottom,
                                          int steps, bool vertical) {
  // Ordered (Bayer 4x4) dither between `steps` quantized levels, drawn in chunky
  // 2px "pixels" - a PC-98 dithered ramp rather than a smooth/banded one.
  static const int bayer[4][4] = {
      {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const int levels = juce::jmax(2, steps);
  const int px = 2;
  const float denom = (float)(levels - 1);
  for (int y = area.getY(); y < area.getBottom(); y += px) {
    for (int x = area.getX(); x < area.getRight(); x += px) {
      const float t = vertical
          ? (float)(y - area.getY()) / juce::jmax(1, area.getHeight() - 1)
          : (float)(x - area.getX()) / juce::jmax(1, area.getWidth() - 1);
      const float scaled = t * denom;
      const int lo = (int)scaled;
      const float frac = scaled - (float)lo;
      const float thr = ((float)bayer[(y / px) & 3][(x / px) & 3] + 0.5f) / 16.0f;
      const int level = juce::jmin(levels - 1, frac > thr ? lo + 1 : lo);
      g.setColour(top.interpolatedWith(bottom, (float)level / denom));
      g.fillRect(x, y, juce::jmin(px, area.getRight() - x),
                 juce::jmin(px, area.getBottom() - y));
    }
  }
}

void OpnaLookAndFeel::drawBezel(juce::Graphics& g, juce::Rectangle<int> b,
                                bool raised, int thickness) {
  const juce::Colour light = raised ? OpnaColours::bevelLight : OpnaColours::bevelDark;
  const juce::Colour dark = raised ? OpnaColours::bevelDark : OpnaColours::bevelLight;
  for (int i = 0; i < thickness; ++i) {
    const int yTop = b.getY() + i;
    const int yBot = b.getBottom() - 1 - i;
    const int xLeft = b.getX() + i;
    const int xRight = b.getRight() - 1 - i;
    g.setColour(light);
    g.fillRect(b.getX() + i, yTop, b.getWidth() - i, 1);          // top
    g.fillRect(xLeft, b.getY() + i, 1, b.getHeight() - i);        // left
    g.setColour(dark);
    g.fillRect(b.getX() + i, yBot, b.getWidth() - i, 1);          // bottom
    g.fillRect(xRight, b.getY() + i, 1, b.getHeight() - i);       // right
  }
}

juce::Rectangle<int> OpnaLookAndFeel::drawPanel(juce::Graphics& g,
                                                juce::Rectangle<int> bounds,
                                                juce::Colour accent,
                                                const juce::String& title) {
  const auto full = bounds;

  // Body.
  g.setColour(OpnaColours::panel);
  g.fillRect(full);

  // Title strip: a banded gradient tinted toward the accent.
  const int titleH = kTitleH;
  auto strip = bounds.removeFromTop(titleH);
  drawSteppedGradient(g, strip, accent, accent.withMultipliedBrightness(0.72f), 5,
                      true);
  if (title.isNotEmpty()) {
    g.setColour(OpnaColours::bg);
    g.setFont(font((float)titleH - 8.0f, true));
    g.drawText(title, strip.reduced(8, 0), juce::Justification::centredLeft);
  }
  // Separator under the strip + raised outer bevel around the whole panel.
  g.setColour(OpnaColours::bevelDark);
  g.fillRect(strip.getX(), strip.getBottom(), strip.getWidth(), 1);
  drawBezel(g, full, true, 2);

  return bounds.reduced(4);  // content area below the title strip
}

void OpnaLookAndFeel::drawValueBar(juce::Graphics& g, juce::Rectangle<int> trough,
                                   float proportion, juce::Colour accent) {
  // Sunken trough.
  g.setColour(OpnaColours::bg);
  g.fillRect(trough);
  drawBezel(g, trough, false, 1);

  auto inner = trough.reduced(2).toFloat();
  const float gap = 1.0f;
  const int n = juce::jmax(1, (int)(inner.getWidth() / 6.0f));  // ~6px cells
  // Size the segments so they span the full trough width (no end gap).
  const float seg = (inner.getWidth() - gap * (float)(n - 1)) / (float)n;
  const int lit = juce::roundToInt(proportion * (float)n);
  for (int i = 0; i < n; ++i) {
    const float bx = inner.getX() + (float)i * (seg + gap);
    auto block = juce::Rectangle<float>(bx, inner.getY(), seg, inner.getHeight());
    g.setColour(i < lit ? accent : OpnaColours::line.withAlpha(0.30f));
    g.fillRect(block);
  }
}

namespace {
constexpr int kTipPadX = 9;    // horizontal text inset inside the tooltip box
constexpr int kTipPadY = 6;    // vertical text inset
constexpr float kTipMaxW = 360.0f;  // wrap long descriptions past this width

// Round a logical font height so the rendered glyph cell lands on whole device
// pixels. The PC-9800 face is a bitmap/pixel font: at fractional Windows DPI
// (125/150%) a fractional *physical* height anti-aliases the cell edges into
// grey mush. Quantising height*scale to an integer and dividing back to logical
// units keeps the cell aligned to the device grid, so the glyphs stay crisp.
float snapFontHeight(float logicalHeight, float scale) {
  if (scale <= 0.0f) return logicalHeight;
  return (float)juce::jmax(1, juce::roundToInt(logicalHeight * scale)) / scale;
}

// Lay out a glossary tooltip: line 0 is the name (amber, slightly larger), the
// last line is the register/footnote (dim), the middle is the description (ink).
// `scale` is the device-pixel scale factor used to snap each height.
juce::TextLayout layoutTooltip(const juce::String& text, float maxWidth,
                               float scale) {
  const juce::StringArray lines = juce::StringArray::fromLines(text);
  juce::AttributedString s;
  s.setJustification(juce::Justification::topLeft);
  s.setLineSpacing(2.0f);
  for (int i = 0; i < lines.size(); ++i) {
    const bool first = (i == 0);
    const bool last = (i == lines.size() - 1);
    const juce::Colour c = first ? OpnaColours::amber
                           : last ? OpnaColours::dim
                                  : OpnaColours::ink;
    s.append(lines[i] + (last ? "" : "\n"),
             OpnaLookAndFeel::font(snapFontHeight(first ? 14.0f : 13.0f, scale)),
             c);
  }
  juce::TextLayout tl;
  tl.createLayout(s, maxWidth);
  return tl;
}
}  // namespace

void OpnaLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text,
                                  int width, int height) {
  auto bounds = juce::Rectangle<int>(0, 0, width, height);
  // Solid opaque base first: the stepped gradient tiles tiny 2px rects whose
  // anti-aliased seams reveal sub-pixel gaps at fractional DPI (e.g. 150%), so a
  // single-rect fill underneath guarantees every device pixel is covered navy
  // (the page behind was bleeding through those seams as white scanlines).
  g.fillAll(OpnaColours::panel);
  // Banded navy fill + chunky raised bevel, matching the rest of the chrome.
  drawSteppedGradient(g, bounds, OpnaColours::panel2, OpnaColours::panel, 5, true);
  drawBezel(g, bounds, true, 2);
  const float scale =
      (float)g.getInternalContext().getPhysicalPixelScaleFactor();
  layoutTooltip(text, (float)(width - 2 * kTipPadX), scale)
      .draw(g, bounds.toFloat().reduced((float)kTipPadX, (float)kTipPadY));
}

juce::Rectangle<int> OpnaLookAndFeel::getTooltipBounds(
    const juce::String& tipText, juce::Point<int> screenPos,
    juce::Rectangle<int> parentArea) {
  // Match drawTooltip's snapping so the box is sized for the same glyphs: take
  // the scale of the display the tooltip will appear on (under the cursor).
  float scale = 1.0f;
  if (auto* d =
          juce::Desktop::getInstance().getDisplays().getDisplayForPoint(screenPos))
    scale = (float)d->scale;
  const auto tl = layoutTooltip(tipText, kTipMaxW, scale);
  const int w = (int)tl.getWidth() + 1 + 2 * kTipPadX;
  const int h = (int)tl.getHeight() + 1 + 2 * kTipPadY;
  return juce::Rectangle<int>(
             screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12)
                                                   : screenPos.x + 24,
             screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)
                                                   : screenPos.y + 6,
             w, h)
      .constrainedWithin(parentArea);
}

}  // namespace audio_plugin
