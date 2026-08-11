#include "Ch3SpecialPanel.h"

#include "FmPanelLayout.h"
#include "PatchParameters.h"  // ch3SpParamId / ch3SpOpParamId

namespace audio_plugin {

// --------------------------------------------------------------- Ch3OperatorCard
Ch3OperatorCard::Ch3OperatorCard(juce::AudioProcessorValueTreeState& apvts, int op)
    : op_(op) {
  for (auto* b : {&attack_, &decay1_, &sustain_, &decay2_, &release_, &multiple_,
                  &detune_, &level_, &rateScale_, &ssgEg_, &coarse_, &cents_})
    addAndMakeVisible(*b);
  for (auto* t : {&am_, &follow_, &keyEnable_})
    addAndMakeVisible(*t);

  auto id = [&](const char* f) { return ch3SpOpParamId(op, f); };
  attack_.attach(apvts, id("ar"), "AR");
  decay1_.attach(apvts, id("dr"), "DR");
  sustain_.attach(apvts, id("sl"), "SL");
  decay2_.attach(apvts, id("sr"), "SR");
  release_.attach(apvts, id("rr"), "RR");
  multiple_.attach(apvts, id("mul"), "MUL");
  detune_.attach(apvts, id("dt"), "DT");
  level_.attach(apvts, id("tl"), "TL");
  rateScale_.attach(apvts, id("ks"), "KS");
  ssgEg_.attach(apvts, id("ssgeg"), "SSG-EG");
  am_.attach(apvts, id("am"), "AM");
  coarse_.attach(apvts, id("coarse"), "COARSE");
  cents_.attach(apvts, id("cents"), "CENTS");
  follow_.attach(apvts, id("follow"), "FOLLOW");
  keyEnable_.attach(apvts, id("ken"), "KEY-EN");
  setAccent(accent_);
}

void Ch3OperatorCard::setAccent(juce::Colour c) {
  accent_ = c;
  for (auto* b : {&attack_, &decay1_, &sustain_, &decay2_, &release_, &multiple_,
                  &detune_, &level_, &rateScale_, &ssgEg_, &coarse_, &cents_})
    b->setAccent(c);
  repaint();
}

void Ch3OperatorCard::paint(juce::Graphics& g) {
  OpnaLookAndFeel::drawPanel(g, getLocalBounds(), accent_,
                             "OP" + juce::String(op_ + 1));
}

void Ch3OperatorCard::resized() {
  auto r = FmLayout::panelContent(getLocalBounds());
  const int rows = 3;
  const int rowH = (r.getHeight() - 2 * 4) / rows;

  auto row1 = r.removeFromTop(rowH);
  r.removeFromTop(4);
  auto row2 = r.removeFromTop(rowH);
  r.removeFromTop(4);
  auto row3 = r.removeFromTop(rowH);

  const int cw = row1.getWidth() / 5;
  for (auto* c : {&attack_, &decay1_, &sustain_, &decay2_, &release_})
    c->setBounds(row1.removeFromLeft(cw).reduced(3, 0));
  for (auto* c : {&multiple_, &detune_, &level_, &rateScale_, &ssgEg_})
    c->setBounds(row2.removeFromLeft(cw).reduced(3, 0));

  // Pitch strip: COARSE | CENTS | FOLLOW | KEY-EN | AM.
  coarse_.setBounds(row3.removeFromLeft(cw).reduced(3, 0));
  cents_.setBounds(row3.removeFromLeft(cw).reduced(3, 0));
  follow_.setBounds(row3.removeFromLeft(cw).reduced(3, 6));
  keyEnable_.setBounds(row3.removeFromLeft(cw).reduced(3, 6));
  am_.setBounds(row3.removeFromLeft(cw).reduced(3, 6));
}

// ---------------------------------------------------------------- Ch3SpecialPanel
Ch3SpecialPanel::Ch3SpecialPanel(juce::AudioProcessorValueTreeState& apvts)
    : lfo_(apvts), algo_(apvts) {
  addAndMakeVisible(lfo_);

  algo_.setParamIds([](const char* f) { return ch3SpParamId(f); });
  algo_.setAccent(accent());
  addAndMakeVisible(algo_);

  for (auto* b : {&feedback_, &ams_, &pms_})
    addAndMakeVisible(*b);
  feedback_.attach(apvts, ch3SpParamId("feedback"), "FB");
  ams_.attach(apvts, ch3SpParamId("ams"), "AMS");
  pms_.attach(apvts, ch3SpParamId("pms"), "PMS");
  for (auto* b : {&feedback_, &ams_, &pms_})
    b->setAccent(accent());

  addAndMakeVisible(csm_);
  csm_.attach(apvts, ch3SpParamId("csm"), "CSM");

  for (int op = 0; op < 4; ++op) {
    ops_[(size_t)op] = std::make_unique<Ch3OperatorCard>(apvts, op);
    ops_[(size_t)op]->setAccent(accent());
    addAndMakeVisible(*ops_[(size_t)op]);
  }
}

void Ch3SpecialPanel::paint(juce::Graphics& g) {
  g.fillAll(OpnaColours::bg);
  // The LFO box and algorithm panel paint themselves; only the CH3-globals
  // panel chrome is ours.
  OpnaLookAndFeel::drawPanel(g, globalsBounds_, accent(), "CH3 SPECIAL");
}

void Ch3SpecialPanel::resized() {
  auto r = getLocalBounds();

  // Column geometry mirrors a normal part page exactly (matrix | LFO | ALGORITHM
  // | globals) so nothing jumps when switching tabs. The matrix column (owned by
  // FmPartPanel) is skipped here.
  constexpr int kLeftBand = FmLayout::kMatrixW + FmLayout::kColGap;

  auto top = r.removeFromTop(FmLayout::kTopBandH);
  r.removeFromTop(FmLayout::kColGap);
  top.removeFromLeft(kLeftBand);

  lfo_.setBounds(top.removeFromLeft(FmLayout::kLfoW));
  top.removeFromLeft(FmLayout::kColGap);
  algo_.setBounds(top.removeFromLeft(FmLayout::kAlgoW));
  top.removeFromLeft(FmLayout::kColGap);

  globalsBounds_ = top;
  {
    auto c = FmLayout::panelContent(globalsBounds_);
    auto gRow = c.removeFromTop(50);
    const int w = gRow.getWidth() / 3;
    feedback_.setBounds(gRow.removeFromLeft(w).reduced(3, 0));
    ams_.setBounds(gRow.removeFromLeft(w).reduced(3, 0));
    pms_.setBounds(gRow.removeFromLeft(w).reduced(3, 0));
    c.removeFromTop(4);
    csm_.setBounds(c.removeFromTop(28).removeFromLeft(w).reduced(3, 0));
  }

  const auto cells = FmLayout::operatorGrid(r);
  for (int i = 0; i < 4; ++i)
    ops_[(size_t)i]->setBounds(cells[(size_t)i]);
}

}  // namespace audio_plugin
