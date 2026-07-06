#include "SampleImportDialog.h"

#include "OpnaChip.h"
#include "SamplerParameters.h"

namespace audio_plugin {

namespace {
constexpr int kImportPanelW = 480;
constexpr int kRowH = 28;
constexpr int kNameH = 20;
constexpr int kReadoutH = 40;
const juce::Colour kImportAccent{0xffff6ec7};  // matches the sampler panel accent

// Format seconds compactly (e.g. "1.80 s", "47.6 s").
juce::String importSecs(double s) {
  return juce::String(s, s < 10.0 ? 2 : 1) + " s";
}
}  // namespace

SampleImportDialog::SampleImportDialog(const juce::String& fileName,
                                       double fileSeconds, int initialRateIndex,
                                       int initialOffset,
                                       std::function<void(int, int)> onImport)
    : OpnaModalBase("IMPORT SAMPLE", "Import", kImportAccent, kImportPanelW),
      fileName_(fileName),
      fileSeconds_(fileSeconds),
      onImport_(std::move(onImport)) {
  auto label = [this](juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, OpnaColours::dim);
    l.setFont(OpnaLookAndFeel::font(14.0f));
    addAndMakeVisible(l);
  };
  label(rateLabel_, "STORAGE RATE");
  label(offsetLabel_, "IMPORT FROM");

  for (int i = 0; i < (int)kSamplerRates.size(); ++i)
    rateBox_.addItem(juce::String(juce::roundToInt(kSamplerRates[(size_t)i])) +
                         " Hz",
                     i + 1);
  rateBox_.setSelectedId(
      juce::jlimit(0, (int)kSamplerRates.size() - 1, initialRateIndex) + 1,
      juce::dontSendNotification);
  rateBox_.onChange = [this] { updateReadout(); };
  addAndMakeVisible(rateBox_);

  offsetSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  offsetSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  offsetSlider_.setRange(0.0, 1000.0, 1.0);
  offsetSlider_.setValue(juce::jlimit(0, 1000, initialOffset),
                         juce::dontSendNotification);
  offsetSlider_.setColour(juce::Slider::trackColourId, accent_);
  offsetSlider_.onValueChange = [this] { updateReadout(); };
  addAndMakeVisible(offsetSlider_);

  readout_.setColour(juce::Label::textColourId, OpnaColours::ink);
  readout_.setFont(OpnaLookAndFeel::font(14.0f));
  readout_.setJustificationType(juce::Justification::topLeft);
  addAndMakeVisible(readout_);

  updateReadout();
}

void SampleImportDialog::show(juce::Component& parent,
                              const juce::String& fileName, double fileSeconds,
                              int initialRateIndex, int initialOffset,
                              std::function<void(int, int)> onImport) {
  launch(parent, new SampleImportDialog(fileName, fileSeconds, initialRateIndex,
                                        initialOffset, std::move(onImport)));
}

void SampleImportDialog::updateReadout() {
  const int idx = juce::jlimit(0, (int)kSamplerRates.size() - 1,
                               rateBox_.getSelectedId() - 1);
  const double rate = kSamplerRates[(size_t)idx];
  // The chip RAM holds two ADPCM samples per byte; that many samples / rate is
  // the longest the sample can be at this storage rate.
  const double budgetSec = (2.0 * opna::OpnaChip::kAdpcmBRamBytes) / rate;
  const bool fits = fileSeconds_ <= budgetSec;
  offsetSlider_.setEnabled(!fits);
  offsetLabel_.setEnabled(!fits);

  juce::String s;
  if (fits) {
    s = "File " + importSecs(fileSeconds_) + " - fits whole (max " + importSecs(budgetSec) +
        " at this rate).";
  } else {
    const double start =
        (offsetSlider_.getValue() / 1000.0) * (fileSeconds_ - budgetSec);
    s = "File " + importSecs(fileSeconds_) + " - too long. Importing " +
        importSecs(budgetSec) + " starting at " + importSecs(start) + ".";
  }
  readout_.setText(s, juce::dontSendNotification);
}

void SampleImportDialog::onResult(bool confirmed) {
  if (onImport_ == nullptr)
    return;
  auto cb = std::move(onImport_);
  if (!confirmed)
    return;
  const int rateIdx = juce::jlimit(0, (int)kSamplerRates.size() - 1,
                                   rateBox_.getSelectedId() - 1);
  cb(rateIdx, (int)offsetSlider_.getValue());
}

int SampleImportDialog::contentHeight() const {
  return kNameH + kPad + kRowH + kPad + kRowH + kPad + kReadoutH;
}

void SampleImportDialog::paintContent(juce::Graphics& g,
                                      juce::Rectangle<int> area) {
  auto nameRow = area.withHeight(kNameH);
  g.setColour(OpnaColours::amber);
  g.setFont(OpnaLookAndFeel::font(15.0f));
  g.drawFittedText(fileName_, nameRow, juce::Justification::topLeft, 1, 1.0f);
}

void SampleImportDialog::layoutContent(juce::Rectangle<int> area) {
  area.removeFromTop(kNameH);  // file name (drawn in paintContent)

  const int labelW = 130;
  area.removeFromTop(kPad);
  auto rateRow = area.removeFromTop(kRowH);
  rateLabel_.setBounds(rateRow.removeFromLeft(labelW));
  rateBox_.setBounds(rateRow.removeFromLeft(160));

  area.removeFromTop(kPad);
  auto offRow = area.removeFromTop(kRowH);
  offsetLabel_.setBounds(offRow.removeFromLeft(labelW));
  offsetSlider_.setBounds(offRow);

  area.removeFromTop(kPad);
  readout_.setBounds(area.removeFromTop(kReadoutH));
}

}  // namespace audio_plugin
