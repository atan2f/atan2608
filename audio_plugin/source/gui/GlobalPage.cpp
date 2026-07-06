#include "GlobalPage.h"

#include "PatchParameters.h"  // fmGroupParamId

namespace audio_plugin {

namespace {
// Non-FM part accents (match the SSG/ADPCM page). Distinct names from
// SsgAdpcmPage's equivalents because the unity build merges these translation
// units.
const juce::Colour kRouteSsgAccent{0xff7dffa6};  // green
const juce::Colour kRouteRhyAccent{0xffb98cff};  // violet
const juce::Colour kRouteSmpAccent{0xffff6ec7};  // pink
constexpr int kCh3SpecialRow = opna::kNumFmGroups;

juce::StringArray routeMidiItems() {
  juce::StringArray a;
  a.add("Omni");
  for (int i = 1; i <= 16; ++i)
    a.add(juce::String(i));
  return a;
}

// Identifies one part's parameter ids plus its display name and accent. octId is
// empty for parts without an octave transpose (Rhythm).
struct PartDef {
  juce::String name;
  juce::Colour accent;
  juce::String chId, loId, hiId, octId, soloId;
};

std::array<PartDef, GlobalPage::kNumRows> partDefs() {
  std::array<PartDef, GlobalPage::kNumRows> d{};
  for (int g = 1; g <= opna::kNumFmGroups; ++g) {
    d[(size_t)(g - 1)] = {"FM P" + juce::String(g), OpnaColours::part(g),
                          fmGroupParamId(g, "ch"), fmGroupParamId(g, "lo"),
                          fmGroupParamId(g, "hi"), fmGroupParamId(g, "oct"),
                          fmGroupParamId(g, "solo")};
  }
  int r = opna::kNumFmGroups;
  // Gated FM CH3 special row: only laid out while special mode is on. Keep it
  // next to the FM parts because it takes over physical channel 3.
  d[(size_t)r++] = {"FM CH3 SP", OpnaColours::ch3sp, "ch3sp_ch", "ch3sp_lo",
                    "ch3sp_hi", "ch3sp_oct", "ch3sp_solo"};
  d[(size_t)r++] = {"SSG POOL", kRouteSsgAccent, "ssg_ch", "ssg_lo", "ssg_hi",
                    "ssg_oct", "ssg_solo"};
  // SSG A/B/C rows are always shown (each is routable whenever it is set to
  // Independent on the SSG page); they sit right under the pooled SSG row.
  const char* tag[3] = {"a", "b", "c"};
  const char* nm[3] = {"SSG A", "SSG B", "SSG C"};
  for (int i = 0; i < 3; ++i) {
    const juce::String t = juce::String("ssg") + tag[i] + "_";
    d[(size_t)r++] = {nm[i], kRouteSsgAccent, t + "ch", t + "lo", t + "hi",
                      t + "oct", t + "solo"};
  }
  // Rhythm has no octave transpose (each note maps to a different drum).
  d[(size_t)r++] = {"RHYTHM", kRouteRhyAccent, "rhy_ch", "rhy_lo", "rhy_hi",
                    "", "rhy_solo"};
  d[(size_t)r++] = {"SAMPLER", kRouteSmpAccent, "smp_ch", "smp_lo", "smp_hi",
                    "smp_oct", "smp_solo"};
  return d;
}
}  // namespace

GlobalPage::GlobalPage(juce::AudioProcessorValueTreeState& apvts) {
  const auto defs = partDefs();
  for (int i = 0; i < kNumRows; ++i) {
    Row& row = rows_[(size_t)i];
    const PartDef& def = defs[(size_t)i];
    row.accent = def.accent;

    row.name.setText(def.name, juce::dontSendNotification);
    row.name.setColour(juce::Label::textColourId, def.accent);
    row.name.setFont(OpnaLookAndFeel::font(16.0f));
    row.name.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(row.name);

    // Solo: a bare checkbox (the column header labels it); keep the glossary
    // tooltip and tint the tick with the part accent.
    row.solo.attach(apvts, def.soloId, "SOLO");
    row.soloParam = apvts.getParameter(def.soloId);
    row.solo.button.setButtonText("");
    row.solo.button.setColour(juce::ToggleButton::tickColourId, def.accent);
    row.solo.button.onClick = [this, i] { soloClicked(i); };
    addAndMakeVisible(row.solo);

    // MIDI channel + key range reuse the shared widgets; the per-control name
    // labels are hidden so the painted column headers carry the meaning.
    row.chan.setItems(routeMidiItems());
    row.chan.attach(apvts, def.chId, "MIDI CH");
    row.chan.nameLabel.setVisible(false);
    addAndMakeVisible(row.chan);

    row.low.attach(apvts, def.loId, "LOW KEY");
    row.low.nameLabel.setVisible(false);
    row.low.setAccent(def.accent);
    addAndMakeVisible(row.low);

    row.high.attach(apvts, def.hiId, "HI KEY");
    row.high.nameLabel.setVisible(false);
    row.high.setAccent(def.accent);
    addAndMakeVisible(row.high);

    // Octave transpose: present for every melodic part (not Rhythm).
    row.hasOct = def.octId.isNotEmpty();
    if (row.hasOct) {
      row.oct.attach(apvts, def.octId, "OCT");
      row.oct.nameLabel.setVisible(false);
      row.oct.setAccent(def.accent);
      addAndMakeVisible(row.oct);
    }
  }

  // Only the FM CH3 SP row is gated -- on special-mode enable. The SSG A/B/C
  // rows are always visible (mode is chosen on the SSG page).
  gate_[(size_t)kCh3SpecialRow] = apvts.getRawParameterValue(ch3SpParamId("enable"));

  for (int i = 0; i < kNumRows; ++i) {
    visible_[(size_t)i] = rowVisible(i);
    setRowVisible(i, visible_[(size_t)i]);
  }

  // Chip-wide per-patch controls. APVTS params, so they travel with the preset
  // like the routing rows.
  master_.attach(apvts, "master", "MASTER");
  master_.setAccent(OpnaColours::cyan);
  addAndMakeVisible(master_);

  // Item id N -> choice value N-1.
  prescale_.setItems({"6 (default)", "3 (+1 octave range)", "2"});
  prescale_.attach(apvts, "chip_prescale", "PRESCALE");
  prescale_.setLabelWidth(64);
  addAndMakeVisible(prescale_);

  limiter_.attach(apvts, "limiter", "LIMITER");
  addAndMakeVisible(limiter_);

  startTimerHz(30);
}

bool GlobalPage::rowVisible(int i) const {
  std::atomic<float>* g = gate_[(size_t)i];
  return g == nullptr || g->load() >= 0.5f;
}

void GlobalPage::setRowVisible(int i, bool v) {
  Row& row = rows_[(size_t)i];
  row.name.setVisible(v);
  row.solo.setVisible(v);
  row.chan.setVisible(v);
  row.low.setVisible(v);
  row.high.setVisible(v);
  if (row.hasOct)
    row.oct.setVisible(v);
}

void GlobalPage::soloClicked(int rowIndex) {
  const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
  if (mods.isCommandDown() || !rows_[(size_t)rowIndex].solo.button.getToggleState())
    return;

  for (int i = 0; i < kNumRows; ++i) {
    if (i == rowIndex)
      continue;
    if (juce::RangedAudioParameter* p = rows_[(size_t)i].soloParam) {
      p->beginChangeGesture();
      p->setValueNotifyingHost(0.0f);
      p->endChangeGesture();
    }
  }
}

void GlobalPage::timerCallback() {
  bool changed = false;
  for (int i = 0; i < kNumRows; ++i) {
    const bool v = rowVisible(i);
    if (v != visible_[(size_t)i]) {
      visible_[(size_t)i] = v;
      setRowVisible(i, v);
      changed = true;
    }
  }
  if (changed) {
    resized();
    repaint();
  }
}

std::array<juce::Rectangle<int>, 6> GlobalPage::columns(
    juce::Rectangle<int> r) const {
  std::array<juce::Rectangle<int>, 6> cols{};
  const int gap = 12;
  cols[0] = r.removeFromLeft(110);  // part name
  r.removeFromLeft(gap);
  cols[1] = r.removeFromLeft(70);   // solo
  r.removeFromLeft(gap);
  cols[2] = r.removeFromLeft(170);  // MIDI CH
  r.removeFromLeft(gap);
  cols[3] = r.removeFromLeft(64);   // OCT
  r.removeFromLeft(gap);
  const int keyW = (r.getWidth() - gap) / 2;
  cols[4] = r.removeFromLeft(keyW);  // low key
  r.removeFromLeft(gap);
  cols[5] = r;                       // high key
  return cols;
}

void GlobalPage::paint(juce::Graphics& g) {
  g.fillAll(OpnaColours::bg);
  OpnaLookAndFeel::drawPanel(g, getLocalBounds(), OpnaColours::cyan, "GLOBAL");

  // Column headers across the header strip.
  const char* titles[6] = {"PART", "SOLO", "MIDI CH", "OCT", "LOW KEY", "HI KEY"};
  const auto cols = columns(headerBounds_);
  g.setColour(OpnaColours::dim);
  g.setFont(OpnaLookAndFeel::font(12.0f));
  for (int i = 0; i < 6; ++i)
    g.drawText(titles[i], cols[(size_t)i], juce::Justification::centredLeft);

  // A faint rule under the headers.
  g.setColour(OpnaColours::line);
  g.fillRect(headerBounds_.getX(), headerBounds_.getBottom() + 2,
             headerBounds_.getWidth(), 1);

  // A matching rule above the global-controls strip, separating it from the rows.
  if (!footerBounds_.isEmpty())
    g.fillRect(footerBounds_.getX(), footerBounds_.getY() - 5,
               footerBounds_.getWidth(), 1);
}

void GlobalPage::resized() {
  auto c = getLocalBounds().withTrimmedTop(OpnaLookAndFeel::kTitleH).reduced(10);
  headerBounds_ = c.removeFromTop(18);
  c.removeFromTop(8);

  // Reserve a strip at the bottom for the global (per-patch) chip controls, so
  // the routing rows size to the space above it.
  footerBounds_ = c.removeFromBottom(46);
  c.removeFromBottom(10);
  {
    auto f = footerBounds_;
    const int gap = 24;
    auto left = f.removeFromLeft((f.getWidth() - gap) / 2);
    f.removeFromLeft(gap);
    master_.setBounds(left);
    // Right half: LIMITER toggle on the left (beside MASTER), PRESCALE to its right.
    auto limiterArea = f.removeFromLeft(100);
    limiter_.setBounds(limiterArea.withSizeKeepingCentre(limiterArea.getWidth(), 22));
    f.removeFromLeft(gap);
    prescale_.setBounds(f.withSizeKeepingCentre(f.getWidth(), 28));
  }

  // With the SSG split there can be up to 13 rows, so size them to fit the
  // available height rather than using a fixed pitch (clamped to a comfortable
  // max). Pitch = row height + gap.
  int visibleRows = 0;
  for (int i = 0; i < kNumRows; ++i)
    if (visible_[(size_t)i])
      ++visibleRows;
  const int pitch =
      visibleRows > 0 ? juce::jmin(46, c.getHeight() / visibleRows) : 46;
  const int rowGap = juce::jlimit(2, 6, pitch / 8);
  const int rowH = juce::jmax(22, pitch - rowGap);
  for (int i = 0; i < kNumRows; ++i) {
    if (!visible_[(size_t)i])
      continue;  // hidden gated row: collapse so the rows below pack up
    Row& row = rows_[(size_t)i];
    auto r = c.removeFromTop(rowH);
    c.removeFromTop(rowGap);
    const auto cols = columns(r);
    row.name.setBounds(cols[0]);
    // Left-align the checkbox under the "SOLO" header (which is drawn
    // centred-left in the same column) so the two line up.
    row.solo.setBounds(
        cols[1].withWidth(28).withSizeKeepingCentre(28, 28).withX(cols[1].getX()));
    row.chan.setBounds(cols[2].withSizeKeepingCentre(cols[2].getWidth(), 26));
    if (row.hasOct)
      row.oct.setBounds(cols[3]);
    row.low.setBounds(cols[4]);
    row.high.setBounds(cols[5]);
  }
}

}  // namespace audio_plugin
