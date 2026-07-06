#include "RoutingMatrix.h"

#include "PatchParameters.h"  // fmChanParamId

namespace audio_plugin {

namespace {
constexpr int kHeaderH = 14;
constexpr int kLabelW = 30;
constexpr int kCh3Col = 2;  // channel 3 -> column index 2 (special-mode owner)
const char* kPanText[3] = {"L", "C", "R"};
}  // namespace

RoutingMatrix::RoutingMatrix(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts) {
  for (int c = 0; c < opna::kNumFmChannels; ++c) {
    chGroup_[(size_t)c] = channelGroup(c + 1);
    chPan_[(size_t)c] = channelPan(c + 1);
  }
  ch3Special_ = ch3Special();
  startTimerHz(30);
}

int RoutingMatrix::channelGroup(int ch) const {
  if (auto* p = apvts_.getRawParameterValue(fmChanParamId(ch, "group")))
    return (int)(p->load() + 0.5f);
  return 0;
}
int RoutingMatrix::channelPan(int ch) const {
  if (auto* p = apvts_.getRawParameterValue(fmChanParamId(ch, "pan")))
    return (int)(p->load() + 0.5f);
  return 1;
}
int RoutingMatrix::ch3SpecialPan() const {
  if (auto* p = apvts_.getRawParameterValue(ch3SpParamId("pan")))
    return (int)(p->load() + 0.5f);
  return 1;
}
bool RoutingMatrix::ch3Special() const {
  if (auto* p = apvts_.getRawParameterValue(ch3SpParamId("enable")))
    return p->load() >= 0.5f;
  return false;
}
void RoutingMatrix::setParamInt(const juce::String& id, int v) {
  if (auto* p = apvts_.getParameter(id))
    p->setValueNotifyingHost(p->convertTo0to1((float)v));
}

void RoutingMatrix::setActivePart(int part) {
  // 0 highlights no row (used by the CH3 special tab, which isn't a part row).
  activePart_ = juce::jlimit(0, opna::kNumFmGroups, part);
  repaint();
}

void RoutingMatrix::timerCallback() {
  bool changed = false;
  for (int c = 0; c < opna::kNumFmChannels; ++c) {
    const int g = channelGroup(c + 1), pn = channelPan(c + 1);
    if (g != chGroup_[(size_t)c]) { chGroup_[(size_t)c] = g; changed = true; }
    if (pn != chPan_[(size_t)c]) { chPan_[(size_t)c] = pn; changed = true; }
  }
  const bool sp = ch3Special();
  if (sp != ch3Special_) { ch3Special_ = sp; changed = true; }
  const int spPan = ch3SpecialPan();
  if (spPan != ch3SpPan_) { ch3SpPan_ = spPan; changed = true; }
  if (changed)
    repaint();
}

RoutingMatrix::Geom RoutingMatrix::geom() const {
  // Mirrors OpnaLookAndFeel::drawPanel's content rect (title strip + bevel).
  const auto content =
      getLocalBounds().withTrimmedTop(OpnaLookAndFeel::kTitleH).reduced(4);
  const int cols = opna::kNumFmChannels;
  const int rows = opna::kNumFmGroups + 1;  // +1 PAN row
  const int gridX = content.getX() + kLabelW;
  const int gridY = content.getY() + kHeaderH;
  const int cellW = juce::jmax(1, (content.getRight() - gridX) / cols);
  const int rowH = juce::jmax(1, (content.getBottom() - gridY) / rows);
  return {gridX, gridY, cellW, rowH, content};
}

void RoutingMatrix::paint(juce::Graphics& g) {
  OpnaLookAndFeel::drawPanel(g, getLocalBounds(), OpnaColours::cyan,
                             "FM CH PART ASSIGN");

  const auto gm = geom();
  const int cols = opna::kNumFmChannels;
  const int rows = opna::kNumFmGroups;
  g.setFont(OpnaLookAndFeel::font(10.0f));

  // Column headers (channel numbers). CH3's header doubles as the special-mode
  // toggle, so it greens up when special mode owns the channel.
  for (int c = 0; c < cols; ++c) {
    auto h = juce::Rectangle<int>(gm.gridX + c * gm.cellW, gm.content.getY(),
                                  gm.cellW, kHeaderH);
    const bool spHeader = (c == kCh3Col && ch3Special_);
    g.setColour(spHeader ? OpnaColours::ch3sp : OpnaColours::dim);
    g.drawText(juce::String(c + 1), h, juce::Justification::centred);
  }

  // Row labels (P1..P6 in part colour, PAN dim) + active-row tint.
  for (int r = 0; r < rows; ++r) {
    const int y = gm.gridY + r * gm.rowH;
    if (r + 1 == activePart_) {
      g.setColour(OpnaColours::part(r + 1).withAlpha(0.12f));
      g.fillRect(gm.gridX, y, cols * gm.cellW, gm.rowH);
    }
    g.setColour(OpnaColours::part(r + 1));
    g.drawText("P" + juce::String(r + 1),
               juce::Rectangle<int>(gm.content.getX(), y, kLabelW, gm.rowH),
               juce::Justification::centred);
  }
  g.setColour(OpnaColours::dim);
  g.drawText("PAN",
             juce::Rectangle<int>(gm.content.getX(), gm.gridY + rows * gm.rowH,
                                  kLabelW, gm.rowH),
             juce::Justification::centred);

  // Grid lines.
  g.setColour(OpnaColours::line);
  for (int c = 0; c <= cols; ++c)
    g.drawVerticalLine(gm.gridX + c * gm.cellW, (float)gm.gridY,
                       (float)(gm.gridY + (rows + 1) * gm.rowH));
  for (int r = 0; r <= rows + 1; ++r)
    g.drawHorizontalLine(gm.gridY + r * gm.rowH, (float)gm.gridX,
                         (float)(gm.gridX + cols * gm.cellW));

  // X marks: each channel's assigned part, in that part's colour.
  for (int c = 0; c < cols; ++c) {
    // CH3 owned by special mode: lock the column and show "SP" instead of a part
    // assignment. The PAN cell stays live (it drives the special part's pan).
    if (c == kCh3Col && ch3Special_) {
      auto colArea = juce::Rectangle<int>(gm.gridX + c * gm.cellW, gm.gridY,
                                          gm.cellW, rows * gm.rowH);
      g.setColour(OpnaColours::ch3sp.withAlpha(0.12f));
      g.fillRect(colArea);
      g.setColour(OpnaColours::ch3sp);
      g.setFont(OpnaLookAndFeel::font(11.0f));
      g.drawText("SP", colArea, juce::Justification::centred);
      g.setFont(OpnaLookAndFeel::font(10.0f));

      // CH3's pan is still live in special mode -- it drives the special part's
      // own pan parameter. Draw it in the special accent so it reads as active.
      auto panCell = juce::Rectangle<int>(gm.gridX + c * gm.cellW,
                                          gm.gridY + rows * gm.rowH, gm.cellW,
                                          gm.rowH);
      g.setColour(OpnaColours::ch3sp);
      g.drawText(kPanText[juce::jlimit(0, 2, ch3SpPan_)], panCell,
                 juce::Justification::centred);
      continue;
    }

    const int part = chGroup_[(size_t)c];
    if (part >= 1 && part <= rows) {
      auto cell = juce::Rectangle<int>(gm.gridX + c * gm.cellW,
                                       gm.gridY + (part - 1) * gm.rowH, gm.cellW,
                                       gm.rowH)
                      .reduced(juce::jmin(gm.cellW, gm.rowH) / 4);
      g.setColour(OpnaColours::part(part));
      g.drawLine((float)cell.getX(), (float)cell.getY(), (float)cell.getRight(),
                 (float)cell.getBottom(), 2.0f);
      g.drawLine((float)cell.getX(), (float)cell.getBottom(),
                 (float)cell.getRight(), (float)cell.getY(), 2.0f);
    }

    // Pan letter.
    auto panCell = juce::Rectangle<int>(gm.gridX + c * gm.cellW,
                                        gm.gridY + rows * gm.rowH, gm.cellW,
                                        gm.rowH);
    g.setColour(OpnaColours::ink);
    g.drawText(kPanText[juce::jlimit(0, 2, chPan_[(size_t)c])], panCell,
               juce::Justification::centred);
  }
}

void RoutingMatrix::mouseDown(const juce::MouseEvent& e) {
  const auto gm = geom();
  const int cols = opna::kNumFmChannels;
  const int rows = opna::kNumFmGroups;

  if (e.x < gm.gridX)
    return;  // row-label gutter
  const int col = (e.x - gm.gridX) / gm.cellW;
  if (col < 0 || col >= cols)
    return;

  // CH3's column is the special-mode control. Its header toggles special mode
  // on; while special mode is on the locked part rows toggle it back off (a
  // large, easy disable target), but the PAN cell stays live and cycles the
  // special part's own pan. When off, CH3 behaves like any channel.
  if (col == kCh3Col) {
    const bool inHeader = e.y >= gm.content.getY() && e.y < gm.gridY;
    if (ch3Special_) {
      const int row = (e.y - gm.gridY) / gm.rowH;
      const bool inPanRow = e.y >= gm.gridY && row == rows;
      if (inPanRow)
        setParamInt(ch3SpParamId("pan"), (ch3SpecialPan() + 1) % 3);
      else
        setParamInt(ch3SpParamId("enable"), 0);
      return;
    }
    if (inHeader) {
      setParamInt(ch3SpParamId("enable"), 1);
      return;
    }
  }

  const int row = (e.y - gm.gridY) / gm.rowH;
  if (e.y < gm.gridY || row < 0 || row > rows)
    return;

  const int ch = col + 1;
  if (row < rows) {  // part cell: assign, or toggle off if already this part
    const int part = row + 1;
    const int cur = channelGroup(ch);
    setParamInt(fmChanParamId(ch, "group"), cur == part ? 0 : part);
  } else {  // PAN row: cycle L -> C -> R
    setParamInt(fmChanParamId(ch, "pan"), (channelPan(ch) + 1) % 3);
  }
}

}  // namespace audio_plugin
