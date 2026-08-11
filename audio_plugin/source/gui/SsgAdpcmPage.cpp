#include "SsgAdpcmPage.h"

namespace audio_plugin {

namespace {
// Per-section accents (all non-teal; teal stays reserved for global).
const juce::Colour kSsgAccent{0xff7dffa6};  // green
const juce::Colour kRhyAccent{0xffb98cff};  // violet
const juce::Colour kSmpAccent{0xffff6ec7};  // pink

// Content rect of a titled panel (mirrors OpnaLookAndFeel::drawPanel).
juce::Rectangle<int> partsContentOf(juce::Rectangle<int> panel) {
  return panel.withTrimmedTop(OpnaLookAndFeel::kTitleH).reduced(6);
}

// Polyline (cycle units x in [0,3], level y in [0,1], 1 = top) for one of the 8
// canonical AY/SSG envelope shapes. Values 0-7 collapse to 0x9 (<4) or 0xF.
std::vector<juce::Point<float>> ssgShapePoints(int shape) {
  const int s = shape & 0x0F;
  const int idx = s >= 8 ? s : (s < 4 ? 9 : 15);
  switch (idx) {
    case 8:  return {{0, 1}, {1, 0}, {1, 1}, {2, 0}, {2, 1}, {3, 0}};  // saw down
    case 9:  return {{0, 1}, {1, 0}, {3, 0}};                          // down, hold low
    case 10: return {{0, 1}, {1, 0}, {2, 1}, {3, 0}};                  // triangle (down first)
    case 11: return {{0, 1}, {1, 0}, {1, 1}, {3, 1}};                  // down, hold high
    case 12: return {{0, 0}, {1, 1}, {1, 0}, {2, 1}, {2, 0}, {3, 1}};  // saw up
    case 13: return {{0, 0}, {1, 1}, {3, 1}};                          // up, hold high
    case 14: return {{0, 0}, {1, 1}, {2, 0}, {3, 1}};                  // triangle (up first)
    case 15: return {{0, 0}, {1, 1}, {1, 0}, {3, 0}};                  // up, hold low
    default: return {{0, 1}, {1, 0}, {3, 0}};
  }
}
}  // namespace

// ---------------------------------------------------------------- WaveformDisplay
void WaveformDisplay::setWaveform(const float* data, int numSamples,
                                  double sampleRate) {
  sampleRate_ = sampleRate;
  numSamples_ = numSamples;
  const int buckets = 512;
  minPeaks_.assign((size_t)buckets, 0.0f);
  maxPeaks_.assign((size_t)buckets, 0.0f);
  if (numSamples <= 0 || data == nullptr) {
    repaint();
    return;
  }
  for (int b = 0; b < buckets; ++b) {
    const int s0 = (int)((int64_t)b * numSamples / buckets);
    const int s1 = (int)((int64_t)(b + 1) * numSamples / buckets);
    float mn = 0.0f, mx = 0.0f;
    for (int i = s0; i < s1 && i < numSamples; ++i) {
      mn = juce::jmin(mn, data[i]);
      mx = juce::jmax(mx, data[i]);
    }
    minPeaks_[(size_t)b] = mn;
    maxPeaks_[(size_t)b] = mx;
  }
  repaint();
}

void WaveformDisplay::connect(juce::AudioProcessorValueTreeState& apvts) {
  apvts_ = &apvts;
  startTimerHz(30);
}

void WaveformDisplay::timerCallback() {
  if (apvts_ == nullptr)
    return;
  auto permille = [this](const char* id, int dflt) {
    if (auto* p = apvts_->getRawParameterValue(id))
      return juce::jlimit(0, 1000, (int)(p->load() + 0.5f));
    return dflt;
  };
  const int s = permille("smp_start", 0);
  const int e = permille("smp_end", 1000);
  bool lp = false;
  if (auto* p = apvts_->getRawParameterValue("smp_loop"))
    lp = p->load() >= 0.5f;
  if (s != startPermille_ || e != endPermille_ || lp != loop_) {
    startPermille_ = s;
    endPermille_ = e;
    loop_ = lp;
    repaint();
  }
}

void WaveformDisplay::paint(juce::Graphics& g) {
  auto b = getLocalBounds();
  g.setColour(OpnaColours::bg);
  g.fillRect(b);
  OpnaLookAndFeel::drawBezel(g, b, false, 1);

  if (numSamples_ <= 0 || maxPeaks_.empty()) {
    g.setColour(OpnaColours::dim);
    g.setFont(OpnaLookAndFeel::font(13.0f));
    g.drawText("No sample loaded", b, juce::Justification::centred);
    return;
  }

  auto bf = b.toFloat();
  const float mid = bf.getCentreY();
  const float halfH = bf.getHeight() * 0.45f;
  const int n = (int)maxPeaks_.size();
  g.setColour(accent_);
  for (int i = 0; i < n; ++i) {
    const float x = bf.getX() + (float)i / (float)n * bf.getWidth();
    g.drawLine(x, mid - maxPeaks_[(size_t)i] * halfH, x,
               mid - minPeaks_[(size_t)i] * halfH, 1.0f);
  }

  // Playback window: dim the regions outside [start, end] and mark the edges.
  // The marks bound playback only -- the data in RAM is unchanged.
  const float xs = bf.getX() + (float)startPermille_ / 1000.0f * bf.getWidth();
  const float xe = bf.getX() + (float)endPermille_ / 1000.0f * bf.getWidth();
  g.setColour(OpnaColours::bg.withAlpha(0.62f));
  if (xs > bf.getX())
    g.fillRect(juce::Rectangle<float>(bf.getX(), bf.getY(), xs - bf.getX(),
                                      bf.getHeight()));
  if (xe < bf.getRight())
    g.fillRect(
        juce::Rectangle<float>(xe, bf.getY(), bf.getRight() - xe, bf.getHeight()));
  g.setColour(accent_.withAlpha(0.9f));
  g.drawLine(xs, bf.getY(), xs, bf.getBottom(), 1.5f);
  g.drawLine(xe, bf.getY(), xe, bf.getBottom(), 1.5f);

  // Info readout: rate / length / sample count.
  const double secs = sampleRate_ > 0.0 ? (double)numSamples_ / sampleRate_ : 0.0;
  juce::String info = juce::String(juce::roundToInt(sampleRate_)) + " Hz   " +
                      juce::String(secs, 2) + " s   " +
                      juce::String(numSamples_) + " smp";
  if (loop_)
    info = juce::String::fromUTF8("\xe2\x86\xba") + " LOOP   " + info;  // ↺
  g.setColour(OpnaColours::dim);
  g.setFont(OpnaLookAndFeel::font(12.0f));
  g.drawText(info, b.reduced(6, 4), juce::Justification::bottomLeft);

  if (dragOver_) {
    g.setColour(accent_);
    g.drawRect(b, 2);
    g.setFont(OpnaLookAndFeel::font(13.0f));
    g.drawText("Drop sample", b, juce::Justification::centred);
  }
}

namespace {
bool isAudioFile(const juce::String& path) {
  const juce::String ext = juce::File(path).getFileExtension().toLowerCase();
  return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
}
}  // namespace

bool WaveformDisplay::isInterestedInFileDrag(const juce::StringArray& files) {
  for (const auto& f : files)
    if (isAudioFile(f))
      return true;
  return false;
}

void WaveformDisplay::fileDragEnter(const juce::StringArray&, int, int) {
  dragOver_ = true;
  repaint();
}

void WaveformDisplay::fileDragExit(const juce::StringArray&) {
  dragOver_ = false;
  repaint();
}

void WaveformDisplay::filesDropped(const juce::StringArray& files, int, int) {
  dragOver_ = false;
  repaint();
  for (const auto& f : files)
    if (isAudioFile(f)) {
      if (onFileDropped)
        onFileDropped(juce::File(f));
      break;
    }
}

// ----------------------------------------------------------------- SsgEnvDisplay
SsgEnvDisplay::SsgEnvDisplay(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts) {
  startTimerHz(30);
}

void SsgEnvDisplay::timerCallback() {
  int sh = 0;
  bool on = false;
  if (auto* p = apvts_.getRawParameterValue("ssg_env_shape"))
    sh = (int)(p->load() + 0.5f);
  if (auto* p = apvts_.getRawParameterValue("ssg_env_on"))
    on = p->load() >= 0.5f;
  if (sh != shape_ || on != on_) {
    shape_ = sh;
    on_ = on;
    repaint();
  }
}

void SsgEnvDisplay::paint(juce::Graphics& g) {
  auto b = getLocalBounds();
  g.setColour(OpnaColours::bg);
  g.fillRect(b);
  OpnaLookAndFeel::drawBezel(g, b, false, 1);

  auto area = b.reduced(8).toFloat();
  const auto pts = ssgShapePoints(shape_ < 0 ? 0 : shape_);
  auto X = [&](float cyc) { return area.getX() + cyc / 3.0f * area.getWidth(); };
  auto Y = [&](float lvl) { return area.getBottom() - lvl * area.getHeight(); };

  juce::Path p;
  for (size_t i = 0; i < pts.size(); ++i) {
    const juce::Point<float> pt{X(pts[i].x), Y(pts[i].y)};
    if (i == 0)
      p.startNewSubPath(pt);
    else
      p.lineTo(pt);
  }
  g.setColour(on_ ? accent_ : OpnaColours::dim.withAlpha(0.5f));
  g.strokePath(p, juce::PathStrokeType(2.0f));

  if (!on_) {
    g.setColour(OpnaColours::dim);
    g.setFont(OpnaLookAndFeel::font(12.0f));
    g.drawText("HW ENV OFF", b.reduced(6, 4), juce::Justification::topRight);
  }
}

// --------------------------------------------------------------------- SsgAdpcmPage
SsgAdpcmPage::SsgAdpcmPage(juce::AudioProcessorValueTreeState& apvts,
                     std::function<void()> onLoadSample,
                     std::function<void(const juce::File&)> onDropSample)
    : ssgEnv_(apvts) {
  // SSG.
  ssgMix_.setItems({"Tone", "Noise", "Tone+Noise", "Off"});
  ssgMix_.setLabelWidth(40);
  ssgMix_.attach(apvts, "ssg_mix", "Mix");
  addAndMakeVisible(ssgMix_);
  ssgVol_.attach(apvts, "ssg_vol", "VOLUME");
  ssgNoise_.attach(apvts, "ssg_noise", "NOISE");
  ssgEnvPeriod_.attach(apvts, "ssg_env_period", "ENV PER");
  ssgEnvShape_.attach(apvts, "ssg_env_shape", "ENV SHP");
  ssgEnvOn_.attach(apvts, "ssg_env_on", "HW ENV");
  for (auto* c : {&ssgVol_, &ssgNoise_, &ssgEnvPeriod_, &ssgEnvShape_}) {
    c->setAccent(kSsgAccent);
    addAndMakeVisible(*c);
  }
  addAndMakeVisible(ssgEnvOn_);
  ssgEnv_.setAccent(kSsgAccent);
  addAndMakeVisible(ssgEnv_);

  // Per-channel A/B/C strip. Routing for independent channels lives on the
  // GLOBAL page (gated rows), keeping all MIDI routing in one place.
  const char* chTag[3] = {"a", "b", "c"};
  const char* chName[3] = {"A", "B", "C"};
  for (int i = 0; i < 3; ++i) {
    auto id = [&](const char* f) { return juce::String("ssg") + chTag[i] + "_" + f; };
    ssgChLabel_[(size_t)i].setText(chName[i], juce::dontSendNotification);
    ssgChLabel_[(size_t)i].setColour(juce::Label::textColourId, kSsgAccent);
    ssgChLabel_[(size_t)i].setFont(OpnaLookAndFeel::font(16.0f));
    ssgChLabel_[(size_t)i].setJustificationType(juce::Justification::centred);
    addAndMakeVisible(ssgChLabel_[(size_t)i]);

    ssgChMode_[(size_t)i].setItems({"Pooled", "Indep"});
    ssgChMode_[(size_t)i].setLabelWidth(0);
    ssgChMode_[(size_t)i].nameLabel.setVisible(false);
    ssgChMode_[(size_t)i].attach(apvts, id("mode"), "MODE");
    addAndMakeVisible(ssgChMode_[(size_t)i]);

    ssgChMix_[(size_t)i].setItems({"Tone", "Noise", "Tone+Noise", "Off"});
    ssgChMix_[(size_t)i].setLabelWidth(0);
    ssgChMix_[(size_t)i].nameLabel.setVisible(false);
    ssgChMix_[(size_t)i].attach(apvts, id("mix"), "Mix");
    addAndMakeVisible(ssgChMix_[(size_t)i]);

    ssgChVol_[(size_t)i].attach(apvts, id("vol"), "VOLUME");  // "VOLUME" = tooltip key
    ssgChVol_[(size_t)i].nameLabel.setText("VOL", juce::dontSendNotification);
    ssgChVol_[(size_t)i].setAccent(kSsgAccent);
    ssgChVol_[(size_t)i].setCompact(true);  // inline bar in the short strip row
    addAndMakeVisible(ssgChVol_[(size_t)i]);

    ssgChEnv_[(size_t)i].attach(apvts, id("env"), "HW ENV");
    addAndMakeVisible(ssgChEnv_[(size_t)i]);

    // A channel's own timbre controls (mix, volume, HW env) only do anything
    // while it is Independent; grey them all when it is Pooled (it then uses the
    // pooled voice above).
    auto* mode = apvts.getRawParameterValue(id("mode"));
    gating_.addRule([mode] { return mode == nullptr || mode->load() < 0.5f; },
                    {[this, i](bool d) { ssgChMix_[(size_t)i].setDimmed(d); },
                     [this, i](bool d) { ssgChVol_[(size_t)i].setDimmed(d); },
                     [this, i](bool d) { ssgChEnv_[(size_t)i].setDimmed(d); }});
  }

  // Rhythm.
  rhyTotal_.attach(apvts, "rhy_tl", "TOTAL");
  rhyBd_.attach(apvts, "rhy_bd", "BASS");
  rhySd_.attach(apvts, "rhy_sd", "SNARE");
  rhyTop_.attach(apvts, "rhy_top", "CYMBAL");
  rhyHh_.attach(apvts, "rhy_hh", "HI-HAT");
  rhyTom_.attach(apvts, "rhy_tom", "TOM");
  rhyRim_.attach(apvts, "rhy_rim", "RIM");
  for (auto* c : {&rhyTotal_, &rhyBd_, &rhySd_, &rhyTop_, &rhyHh_, &rhyTom_, &rhyRim_}) {
    c->setAccent(kRhyAccent);
    addAndMakeVisible(*c);
  }
  // Per-drum pan (instrument order: BD,SD,Top,HH,Tom,Rim).
  const char* panIds[6] = {"rhy_bd_pan", "rhy_sd_pan", "rhy_top_pan",
                           "rhy_hh_pan", "rhy_tom_pan", "rhy_rim_pan"};
  for (int i = 0; i < 6; ++i) {
    rhyPan_[(size_t)i].setItems({"L", "C", "R"});
    rhyPan_[(size_t)i].setLabelWidth(2);  // box-only (label above the bar)
    rhyPan_[(size_t)i].attach(apvts, panIds[i], "");
    addAndMakeVisible(rhyPan_[(size_t)i]);
  }

  // Sampler.
  wave_.setAccent(kSmpAccent);
  wave_.onFileDropped = std::move(onDropSample);
  wave_.connect(apvts);
  addAndMakeVisible(wave_);
  loadButton_.onClick = [onLoadSample] {
    if (onLoadSample)
      onLoadSample();
  };
  addAndMakeVisible(loadButton_);
  smpLevel_.attach(apvts, "smp_level", "LEVEL");
  smpRoot_.attach(apvts, "smp_root", "ROOT");
  smpStart_.attach(apvts, "smp_start", "START");
  smpEnd_.attach(apvts, "smp_end", "END");
  for (auto* c : {&smpLevel_, &smpRoot_, &smpStart_, &smpEnd_}) {
    c->setAccent(kSmpAccent);
    addAndMakeVisible(*c);
  }
  smpLoop_.attach(apvts, "smp_loop", "LOOP");
  addAndMakeVisible(smpLoop_);
  smpReencode_.attach(apvts, "smp_reencode", "RE-ENC");
  addAndMakeVisible(smpReencode_);
  smpPan_.setItems({"L", "C", "R"});
  smpPan_.setLabelWidth(40);
  smpPan_.attach(apvts, "smp_pan", "Pan");
  addAndMakeVisible(smpPan_);

  // Control-gating rules (cosmetic greying of controls the chip is ignoring).
  // When the hardware envelope is on it drives amplitude (VOLUME ignored); when
  // it is off the envelope shape/period do nothing. Mix=Tone (item 0) means no
  // noise in the mix, so NOISE is inert.
  auto* envOn = apvts.getRawParameterValue("ssg_env_on");
  auto* mix = apvts.getRawParameterValue("ssg_mix");
  gating_.addRule([envOn] { return envOn != nullptr && envOn->load() >= 0.5f; },
                  {[this](bool d) { ssgVol_.setDimmed(d); }});
  gating_.addRule([envOn] { return envOn == nullptr || envOn->load() < 0.5f; },
                  {[this](bool d) { ssgEnvPeriod_.setDimmed(d); },
                   [this](bool d) { ssgEnvShape_.setDimmed(d); }});
  gating_.addRule([mix] { return mix != nullptr && mix->load() < 0.5f; },
                  {[this](bool d) { ssgNoise_.setDimmed(d); }});
  gating_.evaluate(true);
}

void SsgAdpcmPage::setSampleWaveform(const float* data, int numSamples,
                                  double sampleRate) {
  wave_.setWaveform(data, numSamples, sampleRate);
}

void SsgAdpcmPage::paint(juce::Graphics& g) {
  g.fillAll(OpnaColours::bg);
  OpnaLookAndFeel::drawPanel(g, smpBounds_, kSmpAccent, "SAMPLER  (ADPCM-B)");
  OpnaLookAndFeel::drawPanel(g, ssgBounds_, kSsgAccent, "SSG  (PSG - 3 VOICE)");
  OpnaLookAndFeel::drawPanel(g, rhyBounds_, kRhyAccent, "RHYTHM  (ADPCM-A - 6 DRUMS)");

  // Per-channel strip header + column captions.
  if (!ssgPerChanBounds_.isEmpty()) {
    g.setColour(OpnaColours::dim);
    g.setFont(OpnaLookAndFeel::font(12.0f));
    g.drawText("PER-CHANNEL  (POOLED = shared voice above)",
               ssgPerChanBounds_.withHeight(16), juce::Justification::centredLeft);
  }
}

void SsgAdpcmPage::resized() {
  auto r = getLocalBounds();

  // Sampler across the top (taller so the waveform has room); SSG + Rhythm below.
  smpBounds_ = r.removeFromTop(300);
  r.removeFromTop(10);
  auto bottom = r;
  const int half = (bottom.getWidth() - 10) / 2;
  ssgBounds_ = bottom.removeFromLeft(half);
  bottom.removeFromLeft(10);
  rhyBounds_ = bottom;

  // --- Sampler: waveform on top, a row of bars then a row of selectors below ---
  {
    auto c = partsContentOf(smpBounds_);
    auto barRow = c.removeFromBottom(52);
    c.removeFromBottom(8);
    auto ctlRow = c.removeFromBottom(30);
    c.removeFromBottom(6);
    wave_.setBounds(c.reduced(0, 4));

    const int bw = (barRow.getWidth() - 30) / 4;  // four bars, three 10px gaps
    for (auto* bar : {&smpLevel_, &smpRoot_, &smpStart_, &smpEnd_}) {
      bar->setBounds(barRow.removeFromLeft(bw));
      barRow.removeFromLeft(10);
    }

    auto place = [&ctlRow](juce::Component& comp, int w, int h) {
      comp.setBounds(ctlRow.removeFromLeft(w).withSizeKeepingCentre(w, h));
      ctlRow.removeFromLeft(16);
    };
    place(smpPan_, 140, 26);
    place(smpLoop_, 80, 26);
    place(smpReencode_, 90, 26);
    place(loadButton_, 130, 30);
  }

  // --- SSG ---
  {
    auto c = partsContentOf(ssgBounds_);
    auto row = c.removeFromTop(50);
    const int w = row.getWidth() / 4;
    for (auto* k : {&ssgVol_, &ssgNoise_, &ssgEnvPeriod_, &ssgEnvShape_})
      k->setBounds(row.removeFromLeft(w).reduced(4, 0));
    c.removeFromTop(6);
    auto ctlRow = c.removeFromTop(24);
    ssgMix_.setBounds(ctlRow.removeFromLeft(240));
    ctlRow.removeFromLeft(16);
    ssgEnvOn_.setBounds(ctlRow.removeFromLeft(120));
    c.removeFromTop(8);

    // Per-channel A/B/C strip (one row each: label, mode, mix, volume, HW env).
    ssgPerChanBounds_ = c.removeFromTop(18 + 3 * 26 + 2 * 4);
    auto strip = ssgPerChanBounds_;
    strip.removeFromTop(18);  // header text (drawn in paint)
    for (int i = 0; i < 3; ++i) {
      auto cr = strip.removeFromTop(26);
      if (i < 2)
        strip.removeFromTop(4);
      ssgChLabel_[(size_t)i].setBounds(cr.removeFromLeft(22));
      cr.removeFromLeft(4);
      ssgChMode_[(size_t)i].setBounds(cr.removeFromLeft(92).withSizeKeepingCentre(92, 24));
      cr.removeFromLeft(6);
      ssgChMix_[(size_t)i].setBounds(cr.removeFromLeft(120).withSizeKeepingCentre(120, 24));
      cr.removeFromLeft(8);
      ssgChEnv_[(size_t)i].setBounds(cr.removeFromRight(78).withSizeKeepingCentre(78, 24));
      cr.removeFromRight(8);
      ssgChVol_[(size_t)i].setBounds(cr);
    }

    c.removeFromTop(8);
    // Envelope-shape graph fills the remaining space.
    ssgEnv_.setBounds(c.reduced(0, 2));
  }

  // --- Rhythm: TOTAL + 6 drums (bar + pan) in a 4-column grid ---
  {
    auto c = partsContentOf(rhyBounds_);
    const int cols = 4;
    const int w = c.getWidth() / cols;
    auto placeCell = [&](juce::Rectangle<int> cell, ParamBar& bar,
                         ParamCombo* pan) {
      bar.setBounds(cell.removeFromTop(48).reduced(4, 0));
      cell.removeFromTop(4);
      if (pan != nullptr)
        pan->setBounds(cell.removeFromTop(24).reduced(4, 0));
    };
    auto row1 = c.removeFromTop(76);
    placeCell(row1.removeFromLeft(w), rhyTotal_, nullptr);
    placeCell(row1.removeFromLeft(w), rhyBd_, &rhyPan_[0]);
    placeCell(row1.removeFromLeft(w), rhySd_, &rhyPan_[1]);
    placeCell(row1.removeFromLeft(w), rhyTop_, &rhyPan_[2]);
    c.removeFromTop(8);
    auto row2 = c.removeFromTop(76);
    placeCell(row2.removeFromLeft(w), rhyHh_, &rhyPan_[3]);
    placeCell(row2.removeFromLeft(w), rhyTom_, &rhyPan_[4]);
    placeCell(row2.removeFromLeft(w), rhyRim_, &rhyPan_[5]);
  }
}

}  // namespace audio_plugin
