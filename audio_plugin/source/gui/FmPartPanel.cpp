#include "FmPartPanel.h"

#include "FmPanelLayout.h"
#include "PatchParameters.h"  // fmGroupParamId / fmOpParamId

namespace audio_plugin {

// ------------------------------------------------------------------- OperatorCard
OperatorCard::OperatorCard(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts), env_(apvts) {
  addAndMakeVisible(env_);
  for (auto* b : {&attack_, &decay1_, &sustain_, &decay2_, &release_, &multiple_,
                  &detune_, &level_, &rateScale_, &ssgEg_})
    addAndMakeVisible(*b);
  addAndMakeVisible(am_);
}

void OperatorCard::setTarget(int part, int op) {
  opIndex_ = op;
  env_.setTarget(part, op);

  auto id = [&](const char* f) { return fmOpParamId(part, op, f); };
  // Labels are the register mnemonics (matching the param IDs and display names).
  attack_.attach(apvts_, id("ar"), "AR");
  decay1_.attach(apvts_, id("dr"), "DR");
  sustain_.attach(apvts_, id("sl"), "SL");
  decay2_.attach(apvts_, id("sr"), "SR");
  release_.attach(apvts_, id("rr"), "RR");
  multiple_.attach(apvts_, id("mul"), "MUL");
  detune_.attach(apvts_, id("dt"), "DT");
  level_.attach(apvts_, id("tl"), "TL");
  rateScale_.attach(apvts_, id("ks"), "KS");
  ssgEg_.attach(apvts_, id("ssgeg"), "SSG-EG");
  am_.attach(apvts_, id("am"), "AM");
}

void OperatorCard::setAccent(juce::Colour c) {
  accent_ = c;
  env_.setAccent(c);
  for (auto* b : {&attack_, &decay1_, &sustain_, &decay2_, &release_, &multiple_,
                  &detune_, &level_, &rateScale_, &ssgEg_})
    b->setAccent(c);
  repaint();
}

void OperatorCard::setRole(bool carrier) {
  if (carrier != carrier_) {
    carrier_ = carrier;
    repaint();
  }
}

void OperatorCard::paint(juce::Graphics& g) {
  const juce::String title = "OP" + juce::String(opIndex_ + 1) +
                             (carrier_ ? "  \xe2\x80\x94  CARRIER"
                                       : "  \xe2\x80\x94  MODULATOR");
  OpnaLookAndFeel::drawPanel(g, getLocalBounds(), accent_, title);
}

void OperatorCard::resized() {
  auto r = FmLayout::panelContent(getLocalBounds());
  // Taller than the param rows below: the envelope plot carries an A/D/S/R stage
  // label strip, so it needs the extra height (taken from the param bars).
  auto envRow = r.removeFromTop(58);
  am_.setBounds(envRow.removeFromRight(46).withSizeKeepingCentre(46, 22));
  envRow.removeFromRight(4);
  env_.setBounds(envRow);
  r.removeFromTop(4);

  const int cols = 5;
  const int rowH = (r.getHeight() - 4) / 2;
  auto row1 = r.removeFromTop(rowH);
  r.removeFromTop(4);
  auto row2 = r.removeFromTop(rowH);
  const int cw = row1.getWidth() / cols;

  for (auto* c : {&attack_, &decay1_, &sustain_, &decay2_, &release_})
    c->setBounds(row1.removeFromLeft(cw).reduced(3, 0));
  for (auto* c : {&multiple_, &detune_, &level_, &rateScale_, &ssgEg_})
    c->setBounds(row2.removeFromLeft(cw).reduced(3, 0));
}

// ------------------------------------------------------------------- FmPartPanel
FmPartPanel::FmPartPanel(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts), matrix_(apvts), lfo_(apvts), algo_(apvts),
      ch3spPanel_(apvts) {
  for (int i = 0; i < opna::kNumFmGroups; ++i) {
    auto& t = tabs_[(size_t)i];
    t.setButtonText("P" + juce::String(i + 1));
    t.setClickingTogglesState(false);
    t.onClick = [this, i] { setPart(i + 1); };
    addAndMakeVisible(t);
  }
  // The CH3 special pseudo-part tab sits past P1..P6. It is intentionally out of
  // the way: hidden until special mode is enabled from the routing grid (an
  // advanced multi-frequency mode, not normal polyphony).
  ch3spTab_.setButtonText("FM3 SP");
  ch3spTab_.setClickingTogglesState(false);
  ch3spTab_.onClick = [this] { setPart(kCh3SpecialPart); };
  addChildComponent(ch3spTab_);  // shown only while special mode is enabled
  ch3spEnableParam_ = apvts.getRawParameterValue(ch3SpParamId("enable"));

  addChildComponent(ch3spPanel_);  // shown only on the special tab
  addAndMakeVisible(matrix_);

  // Shared LFO box + algorithm panel (both retargetable; algo_ rebinds per part).
  addAndMakeVisible(lfo_);
  addAndMakeVisible(algo_);

  addAndMakeVisible(feedback_);
  addAndMakeVisible(ams_);
  addAndMakeVisible(pms_);

  for (auto& op : ops_) {
    op = std::make_unique<OperatorCard>(apvts_);
    addAndMakeVisible(*op);
  }

  setPart(1);
  startTimerHz(30);
}

void FmPartPanel::setPart(int part) {
  part_ = juce::jlimit(1, kCh3SpecialPart, part);

  if (onSpecialTab()) {
    // The special panel is fully self-contained (its own ch3sp_ parameters);
    // reveal it and hide the normal FM widgets. The routing matrix stays visible
    // (it shows CH3's locked "SP" column + pan) but highlights no part row.
    setNormalWidgetsVisible(false);
    matrix_.setActivePart(0);
    ch3spPanel_.setVisible(true);
    // The panel covers the whole area; drop it to the back so the tab buttons
    // and the routing matrix (col1 top-left) stay visible and clickable on top.
    ch3spPanel_.toBack();
    refreshTabs();
    resized();
    repaint();
    return;
  }

  setNormalWidgetsVisible(true);
  ch3spPanel_.setVisible(false);

  feedback_.attach(apvts_, fmGroupParamId(part_, "feedback"), "FB");
  ams_.attach(apvts_, fmGroupParamId(part_, "ams"), "AMS");
  pms_.attach(apvts_, fmGroupParamId(part_, "pms"), "PMS");

  algo_.setGroup(part_);
  matrix_.setActivePart(part_);

  for (int op = 0; op < 4; ++op)
    ops_[(size_t)op]->setTarget(part_, op);

  lastAlgo_ = -1;  // force role/highlight refresh on next tick
  applyAccent();
  refreshTabs();
  repaint();
}

void FmPartPanel::setNormalWidgetsVisible(bool v) {
  // matrix_ stays visible on the special tab too (see setPart), so it is not
  // toggled here.
  lfo_.setVisible(v);
  algo_.setVisible(v);
  feedback_.setVisible(v);
  ams_.setVisible(v);
  pms_.setVisible(v);
  for (auto& op : ops_)
    op->setVisible(v);
}

void FmPartPanel::applyAccent() {
  const auto c = accent();
  algo_.setAccent(c);
  feedback_.setAccent(c);
  ams_.setAccent(c);
  pms_.setAccent(c);
  for (auto& op : ops_)
    op->setAccent(c);
}

void FmPartPanel::refreshTabs() {
  for (int i = 0; i < opna::kNumFmGroups; ++i) {
    auto& t = tabs_[(size_t)i];
    const bool active = (i + 1) == part_;
    t.setColour(juce::TextButton::buttonColourId,
                active ? OpnaColours::part(i + 1) : OpnaColours::panel);
    t.setColour(juce::TextButton::textColourOffId,
                active ? OpnaColours::bg : OpnaColours::part(i + 1));
  }
  const bool spActive = onSpecialTab();
  ch3spTab_.setColour(juce::TextButton::buttonColourId,
                      spActive ? OpnaColours::ch3sp : OpnaColours::panel);
  ch3spTab_.setColour(juce::TextButton::textColourOffId,
                      spActive ? OpnaColours::bg : OpnaColours::ch3sp);
}

void FmPartPanel::timerCallback() {
  // Reveal/hide the CH3 special tab as the routing-grid enable toggles. Enabling
  // only *reveals* the tab (the user clicks it to navigate); disabling drops it.
  const bool spEnabled =
      ch3spEnableParam_ != nullptr && ch3spEnableParam_->load() >= 0.5f;
  if (spEnabled != lastCh3SpEnabled_) {
    lastCh3SpEnabled_ = spEnabled;
    ch3spTab_.setVisible(spEnabled);
    // Shorten the part tabs to single digits while the CH3 SP tab is taking
    // space, so "P1".."P6" don't get squeezed into "...".
    for (int i = 0; i < opna::kNumFmGroups; ++i)
      tabs_[(size_t)i].setButtonText((spEnabled ? "" : "P") + juce::String(i + 1));
    if (!spEnabled && onSpecialTab())
      setPart(1);  // the page vanished from under us
    resized();
    refreshTabs();
  }

  if (onSpecialTab())
    return;  // the special panel manages its own controls
  // The algorithm selector/diagram self-refresh (FmAlgoPanel); here we only need
  // to retint the operator cards as carrier/modulator when the algorithm changes.
  int algo = 0;
  if (auto* p = apvts_.getRawParameterValue(fmGroupParamId(part_, "algorithm")))
    algo = (int)(p->load() + 0.5f);
  if (algo != lastAlgo_) {
    lastAlgo_ = algo;
    const int mask = algoCarrierMask(algo);
    for (int op = 0; op < 4; ++op)
      ops_[(size_t)op]->setRole((mask & (1 << op)) != 0);
  }
}

void FmPartPanel::paint(juce::Graphics& g) {
  g.fillAll(OpnaColours::bg);
  if (onSpecialTab())
    return;  // ch3spPanel_ paints its own background + panels
  // The LFO box and algorithm panel paint themselves; only the part-globals
  // panel chrome is ours.
  OpnaLookAndFeel::drawPanel(g, partBounds_, accent(), "PART");
}

void FmPartPanel::resized() {
  // No extra inset here: the page already insets by the window margin, matching
  // the SSG/ADPCM tab's spacing.
  auto r = getLocalBounds();

  // ---- Top band ----
  auto top = r.removeFromTop(FmLayout::kTopBandH);
  r.removeFromTop(FmLayout::kColGap);

  // Column 1: part tabs above the routing matrix. The CH3 SP tab only claims a
  // slice on the right while it is visible (special mode enabled); otherwise the
  // six P-tabs use the full width.
  auto col1 = top.removeFromLeft(FmLayout::kMatrixW);
  {
    auto tabRow = col1.removeFromTop(26);
    if (ch3spTab_.isVisible())
      ch3spTab_.setBounds(tabRow.removeFromRight(58).reduced(2, 0));
    const int tw = tabRow.getWidth() / opna::kNumFmGroups;
    for (auto& t : tabs_)
      t.setBounds(tabRow.removeFromLeft(tw).reduced(2, 0));
    col1.removeFromTop(6);
    matrix_.setBounds(col1);
  }

  // On the special tab the editor spans the FULL area so its top band lines up
  // with the tab-button row -- exactly like a part page's LFO/ALGO/PART panels,
  // which also start at y=0 (only col1 carries the tab row above the matrix).
  // The tab buttons and routing matrix sit in front of it (z-order set in
  // setPart) and reclaim col1's top-left corner.
  if (onSpecialTab()) {
    ch3spPanel_.setBounds(getLocalBounds());
    return;
  }
  top.removeFromLeft(FmLayout::kColGap);

  // Columns 2-4: LFO box, algorithm panel, part globals (each self-painting).
  lfo_.setBounds(top.removeFromLeft(FmLayout::kLfoW));
  top.removeFromLeft(FmLayout::kColGap);
  algo_.setBounds(top.removeFromLeft(FmLayout::kAlgoW));
  top.removeFromLeft(FmLayout::kColGap);

  partBounds_ = top;
  {
    auto c = FmLayout::panelContent(partBounds_);
    auto gRow = c.removeFromTop(50);
    const int w = gRow.getWidth() / 3;
    feedback_.setBounds(gRow.removeFromLeft(w).reduced(3, 0));
    ams_.setBounds(gRow.removeFromLeft(w).reduced(3, 0));
    pms_.setBounds(gRow.removeFromLeft(w).reduced(3, 0));
  }

  // ---- Operator cards: 2x2 grid ----
  const auto cells = FmLayout::operatorGrid(r);
  for (int i = 0; i < 4; ++i)
    ops_[(size_t)i]->setBounds(cells[(size_t)i]);
}

}  // namespace audio_plugin
