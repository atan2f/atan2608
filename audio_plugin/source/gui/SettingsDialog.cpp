#include "SettingsDialog.h"

#include "ParamWidgets.h"  // setGlossaryTooltip
#include "Version.h"

namespace audio_plugin {

namespace {
constexpr int kSettingsPanelW = 480;
constexpr int kSetRowH = 28;
constexpr int kSetLabelW = 130;
constexpr int kSetComboW = 220;
constexpr int kFooterH = 18;
const juce::Colour kSettingsAccent = OpnaColours::cyan;
}  // namespace

SettingsDialog::SettingsDialog(int initialFidelity, const juce::String& romStatus,
                               std::function<void(int)> onFidelity,
                               std::function<void()> onLoadRom,
                               std::function<void()> onUseDefaultRom,
                               std::function<void()> onResetSize)
    : OpnaModalBase("SETTINGS", "Done", kSettingsAccent, kSettingsPanelW),
      onLoadRom_(std::move(onLoadRom)) {
  // Settings apply live, so the only dismissal control is "Done" (and Esc).
  cancelButton_.setVisible(false);

  auto label = [this](juce::Label& l, const juce::String& text) {
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, OpnaColours::dim);
    l.setFont(OpnaLookAndFeel::font(14.0f));
    addAndMakeVisible(l);
  };
  label(fidelityLabel_, "FIDELITY");
  label(romLabel_, "RHYTHM ROM");

  // Hover help (shown when the editor's "?" toggle is on), keyed by label.
  setGlossaryTooltip(fidelityBox_, "FIDELITY");
  setGlossaryTooltip(fidelityLabel_, "FIDELITY");
  setGlossaryTooltip(romLabel_, "RHYTHM ROM");

  romStatus_.setText(romStatus, juce::dontSendNotification);
  romStatus_.setColour(juce::Label::textColourId, OpnaColours::ink);
  romStatus_.setFont(OpnaLookAndFeel::font(14.0f));
  addAndMakeVisible(romStatus_);

  // Fidelity is a machine-local preference (not part of the patch), so it is
  // driven by callback rather than an APVTS attachment.
  fidelityBox_.addItem("Low (less CPU)", 1);
  fidelityBox_.addItem("Medium", 2);
  fidelityBox_.addItem("High (less aliasing)", 3);
  fidelityBox_.setSelectedId(juce::jlimit(0, 2, initialFidelity) + 1,
                             juce::dontSendNotification);
  fidelityBox_.onChange = [this, cb = std::move(onFidelity)] {
    if (cb)
      cb(fidelityBox_.getSelectedId() - 1);
  };
  addAndMakeVisible(fidelityBox_);

  // Load ROM runs the editor's (async) file chooser while this overlay stays
  // open; the editor refreshes our status via setRomStatus() when it completes.
  loadRomButton_.onClick = [this] {
    if (onLoadRom_)
      onLoadRom_();
  };
  addAndMakeVisible(loadRomButton_);

  // Revert to the default-location ROM; the editor refreshes our status text via
  // setRomStatus() (it may now read "not found" if the file is absent).
  builtInRomButton_.onClick = [cb = std::move(onUseDefaultRom)] {
    if (cb)
      cb();
  };
  addAndMakeVisible(builtInRomButton_);

  resetSizeButton_.onClick = [cb = std::move(onResetSize)] {
    if (cb)
      cb();
  };
  addAndMakeVisible(resetSizeButton_);
}

SettingsDialog* SettingsDialog::show(juce::Component& parent,
                                     int initialFidelity,
                                     const juce::String& romStatus,
                                     std::function<void(int)> onFidelity,
                                     std::function<void()> onLoadRom,
                                     std::function<void()> onUseDefaultRom,
                                     std::function<void()> onResetSize) {
  auto* dialog = new SettingsDialog(initialFidelity, romStatus,
                                    std::move(onFidelity), std::move(onLoadRom),
                                    std::move(onUseDefaultRom),
                                    std::move(onResetSize));
  launchOverlay(parent, dialog);  // non-blocking; everything applies live
  return dialog;
}

void SettingsDialog::setRomStatus(const juce::String& status) {
  romStatus_.setText(status, juce::dontSendNotification);
}

void SettingsDialog::onResult(bool /*confirmed*/) {
  // Everything applies live; Done/Esc/click-off just closes.
}

int SettingsDialog::contentHeight() const {
  // fidelity, rom-label, rom-buttons, reset-size rows + footer.
  return 4 * kSetRowH + 3 * kPad + kPad + kFooterH;
}

void SettingsDialog::paintContent(juce::Graphics& g, juce::Rectangle<int> area) {
  // Build identity in the bottom corner.
  auto footer = area.removeFromBottom(kFooterH);
  g.setColour(OpnaColours::dim);
  g.setFont(OpnaLookAndFeel::font(12.0f));
  g.drawFittedText(juce::String("atan2608 ") + OPNA_VERSION + "  " + OPNA_GIT_COMMIT,
                   footer, juce::Justification::bottomRight, 1);
}

void SettingsDialog::layoutContent(juce::Rectangle<int> area) {
  auto row = [&area](int h) {
    auto r = area.removeFromTop(h);
    return r;
  };
  auto comboRow = [&](juce::Label& lab, juce::Component& box) {
    auto r = row(kSetRowH);
    lab.setBounds(r.removeFromLeft(kSetLabelW));
    box.setBounds(r.removeFromLeft(kSetComboW));
    area.removeFromTop(kPad);
  };

  comboRow(fidelityLabel_, fidelityBox_);

  // Rhythm ROM: label + current status on one row, the two buttons on the next.
  {
    auto r = row(kSetRowH);
    romLabel_.setBounds(r.removeFromLeft(kSetLabelW));
    romStatus_.setBounds(r);
    area.removeFromTop(kPad);
  }
  {
    auto r = row(kSetRowH);
    loadRomButton_.setBounds(r.removeFromLeft(140));
    r.removeFromLeft(kPad);
    builtInRomButton_.setBounds(r.removeFromLeft(140));
    area.removeFromTop(kPad);
  }

  resetSizeButton_.setBounds(row(kSetRowH).removeFromLeft(180));
}

}  // namespace audio_plugin
