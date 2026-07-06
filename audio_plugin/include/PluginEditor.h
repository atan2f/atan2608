#pragma once

#include <memory>
#include <vector>

#include "FmPage.h"
#include "GearButton.h"
#include "OpnaLookAndFeel.h"
#include "ParamWidgets.h"
#include "GlobalPage.h"
#include "PresetManager.h"
#include "SsgAdpcmPage.h"
#include "SettingsDialog.h"

namespace audio_plugin {
class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::AudioProcessorParameter::Listener,
                     private juce::AsyncUpdater {
public:
  explicit PluginEditor(PluginProcessor&);
  ~PluginEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  double currentSamplerRate() const;  // Hz for the smp_rate choice

  void loadPreset(int index);  // load embedded factory .opnapreset by index
  void refreshPresetCombo();   // rebuild Init + factory + user-file entries
  void stepPreset(int dir);    // <</>> : move through presetOrder_ and load
  void selectPresetSilently(int id);  // set the combo without notifying + track it
  void loadSelectedPreset(int id);    // perform the actual load for a combo id
  void savePresetDialog();     // prompt for a name, then save full state
  void writePreset(const juce::String& name);  // save + refresh + select
  void deletePresetDialog();   // confirm, then delete the selected user preset
  void updateDeleteEnabled();  // Del enabled only for a user preset selection
  void resetToDefaults();
  // Patch "dirty" tracking vs. the loaded preset. The baseline is the APVTS
  // state captured at the last load / Init / save; while the live state differs
  // the Rev button appears (its presence is the modified indicator) and reverts.
  // Driven off parameter-change callbacks (immediate) coalesced through the
  // AsyncUpdater, comparing a *flushed* state snapshot so Rev tracks edits with
  // no lag (the APVTS ValueTree itself only updates on a timer).
  void captureBaseline();
  bool isDirty() const;  // live state differs from the loaded-preset baseline
  void updateDirtyState();
  void revertToBaseline();
  void parameterValueChanged(int parameterIndex, float newValue) override;
  void parameterGestureChanged(int, bool) override {}
  void handleAsyncUpdate() override;
  void loadSampleDialog();
  void loadSampleWithDialog(const juce::File& file);  // import-settings modal
  void loadSampleFromFile(const juce::File& file);    // does the actual encode
  void refreshSampleDisplay();  // show the processor's loaded sample (or clear)
  // Page identifiers match the on-screen tab labels and the page component types
  // FmPage / SsgAdpcmPage / GlobalPage.
  enum class Page { Fm, SsgAdpcm, Global };
  void showPage(Page page);
  void setHelpEnabled(bool on);  // create/destroy the tooltip window + persist

  void openSettings();             // the gear button's modal panel
  void applyFidelityPref(int idx); // apply + persist the resampler fidelity
  void loadRhythmRomDialog();      // pick a user rhythm ROM file
  void applyRhythmRomFile(const juce::File& file);  // validate + load + persist
  void useDefaultRhythmRom();      // revert to the default-location ROM + persist
  juce::String rhythmRomStatus() const;  // short description for the Settings panel
  void applySavedSettings();       // push fidelity + ROM prefs to the processor

  PluginProcessor& processorRef;
  PresetManager presetManager_;
  OpnaLookAndFeel lookAndFeel_;

  // Combo id scheme: 1 = Init (reset to defaults), 2.. = embedded factory
  // presets, kUserPresetIdBase.. = on-disk user presets.
  static constexpr int kInitPresetId = 1;
  static constexpr int kFactoryPresetIdBase = 2;
  static constexpr int kUserPresetIdBase = 1000;
  juce::Array<juce::File> userPresetFiles_;
  // Selectable combo ids in display order (Init, factory, user). Factory presets
  // live in a "Factory" submenu, so getSelectedItemIndex()/getNumItems() can't
  // see them; the <</>> buttons step through this list instead.
  std::vector<int> presetOrder_;
  // The combo id currently loaded, so a dirty-discard prompt can bounce the combo
  // back to it if the user cancels switching.
  int lastPresetId_ = 0;

  // Toolbar.
  juce::TextButton fmTab_{"FM"}, ssgAdpcmTab_{"SSG/ADPCM"}, globalTab_{"GLOBAL"};
  GearButton settingsButton_;  // gear icon (far right) opens the settings panel
  juce::TextButton helpButton_{"?"};  // toggles register/param help tooltips
  juce::Label presetLabel_;
  OpnaComboBox presetBox_;
  juce::TextButton prevPreset_{"<"}, nextPreset_{">"};
  juce::TextButton saveButton_{"Save"};
  juce::TextButton deleteButton_{"Del"};
  juce::TextButton revertButton_{"Rev"};  // shown only when the patch is dirty
  juce::TextButton initButton_{"Init"};

  // APVTS state at the last load / Init / save; the revert point + dirty ref.
  juce::ValueTree baselineParams_;

  FmPage fmPage_;
  SsgAdpcmPage ssgAdpcmPage_;
  GlobalPage globalPage_;

  std::unique_ptr<juce::FileChooser> fileChooser_;
  // The open settings overlay (non-blocking, self-deleting); held weakly so we
  // can refresh its ROM status after an async file chooser completes.
  juce::Component::SafePointer<SettingsDialog> settingsPanel_;

  // Help tooltips: the window only exists while enabled, so controls with a
  // glossary tooltip stay silent until the user opts in. The on/off choice is a
  // global UI preference persisted across sessions and instances.
  std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
  juce::PropertiesFile uiProps_;

  // Gate resized() from persisting the editor size until the constructor has
  // applied the restored/default size. Otherwise the setResizeLimits() clamp of
  // the still-0x0 editor fires resized() and saves the minimum as the size.
  bool windowSizeReady_ = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
}  // namespace audio_plugin
