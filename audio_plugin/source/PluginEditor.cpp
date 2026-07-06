#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <limits>

#include "AdpcmBEncoder.h"
#include "BinaryData.h"
#include "FactoryPresets.h"
#include "OpnaModal.h"
#include "SampleImportDialog.h"
#include "SamplerParameters.h"
#include "SettingsDialog.h"

namespace audio_plugin {

namespace {
// Resample mono PCM between rates with a Lagrange interpolator (mild low-pass
// on downsample -- adequate for this retro sampler). Returns the input
// unchanged when the rates already match.
std::vector<float> resamplePcm(const std::vector<float>& in, double fromRate,
                               double toRate) {
  if (in.empty() || fromRate <= 0.0 || toRate <= 0.0)
    return in;
  if (std::abs(fromRate - toRate) < 1.0)
    return in;
  const double ratio = fromRate / toRate;  // input samples consumed per output
  const int numOut =
      juce::jmax(1, (int)std::floor((double)in.size() / ratio));
  std::vector<float> out((size_t)numOut, 0.0f);
  juce::LagrangeInterpolator interp;
  interp.process(ratio, in.data(), out.data(), numOut);
  return out;
}

constexpr int kToolbarH = 46;
constexpr int kMargin = 9;
constexpr int kDefaultW = 1180;
constexpr int kDefaultH = 720;
constexpr int kBtnH = 26;      // standard toolbar control height
constexpr int kBtnGap = 6;     // spacing between controls within a group
constexpr int kGroupGap = 22;  // spacing between logical groups

// Machine-local preferences: things that depend on the machine, not the project
// or patch - help toggle, resampler fidelity, user ROM path, and the editor
// window size. NONE of these belong in the plugin state (they'd travel inside
// every saved preset/project), so they live in their own preferences file.
//
// NOTE: this must NOT collide with the JUCE standalone wrapper's own settings
// file. The wrapper derives its file from the product name, writing
// %APPDATA%/atan2608/atan2608.settings (windowX/Y + filterState); we live in the
// same folder but use a distinct filename (atan2608-prefs.settings). Two
// PropertiesFile objects on ONE file clobber each other (this object's destructor
// would rewrite the file from a stale snapshot, reverting the wrapper's
// filterState), so the filename must stay distinct. The wrapper's file only
// exists for the Standalone anyway; as a VST3 there is none.
juce::PropertiesFile::Options uiPropsOptions() {
  juce::PropertiesFile::Options o;
  o.applicationName = "atan2608-prefs";
  o.filenameSuffix = "settings";
  o.folderName = "atan2608";
  o.osxLibrarySubFolder = "Application Support";
  return o;
}
constexpr const char* kHelpEnabledKey = "showHelpTooltips";
constexpr const char* kFidelityKey = "resamplerFidelity";  // 0=Low 1=Med 2=High
constexpr const char* kRomPathKey = "userRhythmRomPath";   // empty = default location
constexpr const char* kWindowWKey = "windowWidth";   // 0/absent = editor default
constexpr const char* kWindowHKey = "windowHeight";
// The YM2608 rhythm ROM is exactly 8 KB; reject anything else.
constexpr int kRhythmRomBytes = 8192;

opna::OpnaChip::Fidelity fidelityFromIndex(int idx) {
  switch (idx) {
    case 0:  return opna::OpnaChip::Fidelity::Min;
    case 2:  return opna::OpnaChip::Fidelity::Max;
    default: return opna::OpnaChip::Fidelity::Med;
  }
}
int indexFromFidelity(opna::OpnaChip::Fidelity f) {
  switch (f) {
    case opna::OpnaChip::Fidelity::Min: return 0;
    case opna::OpnaChip::Fidelity::Max: return 2;
    default:                            return 1;
  }
}
}  // namespace

PluginEditor::PluginEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
      presetManager_(p),
      fmPage_(p.getApvts()),
      ssgAdpcmPage_(p.getApvts(), [this] { loadSampleDialog(); },
                 [this](const juce::File& f) { loadSampleWithDialog(f); }),
      globalPage_(p.getApvts()),
      uiProps_(uiPropsOptions()) {
  setLookAndFeel(&lookAndFeel_);

  // Page tabs.
  for (auto* t : {&fmTab_, &ssgAdpcmTab_, &globalTab_}) {
    t->setClickingTogglesState(false);
    addAndMakeVisible(*t);
  }
  fmTab_.onClick = [this] { showPage(Page::Fm); };
  ssgAdpcmTab_.onClick = [this] { showPage(Page::SsgAdpcm); };
  globalTab_.onClick = [this] { showPage(Page::Global); };

  settingsButton_.onClick = [this] { openSettings(); };
  addAndMakeVisible(settingsButton_);

  // Help toggle: turns the hover tooltips (friendly name + register/bit) on or
  // off. Off by default so the terse register labels never nag; the choice is
  // remembered across sessions.
  helpButton_.setClickingTogglesState(true);
  // When toggled on, match the active-tab highlight (cyan fill, bg text); the
  // On* colour ids are what TextButton paints from while its toggle state is set.
  helpButton_.setColour(juce::TextButton::buttonOnColourId, OpnaColours::cyan);
  helpButton_.setColour(juce::TextButton::textColourOnId, OpnaColours::bg);
  helpButton_.setColour(juce::TextButton::buttonColourId, OpnaColours::panel);
  helpButton_.setColour(juce::TextButton::textColourOffId, OpnaColours::dim);
  helpButton_.onClick = [this] { setHelpEnabled(helpButton_.getToggleState()); };
  addAndMakeVisible(helpButton_);
  setHelpEnabled(uiProps_.getBoolValue(kHelpEnabledKey, false));

  // Preset controls.
  presetLabel_.setText("PRESET", juce::dontSendNotification);
  presetLabel_.setColour(juce::Label::textColourId, OpnaColours::dim);
  presetLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(presetLabel_);

  refreshPresetCombo();
  presetBox_.onChange = [this] {
    updateDeleteEnabled();
    const int id = presetBox_.getSelectedId();
    if (id <= 0 || id == lastPresetId_)
      return;
    if (!isDirty()) {
      loadSelectedPreset(id);
      return;
    }
    // Switching away from an edited patch discards those edits, so warn first
    // (same check that drives the Rev button). The combo stays on the newly
    // picked entry while we ask (no jarring bounce); on confirm we load it, on
    // cancel we roll the combo back to the loaded preset. The prompt is deferred
    // to the next message loop so it isn't launched while the combo's own popup
    // modal is still tearing down (which left the confirm dialog unresponsive).
    juce::Component::SafePointer<PluginEditor> safe(this);
    juce::MessageManager::callAsync([safe, id] {
      auto* self = safe.getComponent();
      if (self == nullptr)
        return;
      OpnaModal::confirm(
          *self, "Discard Changes",
          "Discard unsaved changes to the current patch?", "Discard",
          OpnaColours::amber,
          [safe, id] {
            if (auto* s = safe.getComponent())
              s->loadSelectedPreset(id);
          },
          [safe] {
            if (auto* s = safe.getComponent())
              s->selectPresetSilently(s->lastPresetId_);
          });
    });
  };
  addAndMakeVisible(presetBox_);

  // Step through every selectable entry (Init, the factory submenu's items, and
  // user files) via the explicit order list, since the factory items live in a
  // submenu that getSelectedItemIndex() can't navigate. setSelectedId notifies,
  // so the stepped-to preset loads.
  prevPreset_.onClick = [this] { stepPreset(-1); };
  nextPreset_.onClick = [this] { stepPreset(+1); };
  addAndMakeVisible(prevPreset_);
  addAndMakeVisible(nextPreset_);

  saveButton_.onClick = [this] { savePresetDialog(); };
  addAndMakeVisible(saveButton_);

  deleteButton_.onClick = [this] { deletePresetDialog(); };
  addAndMakeVisible(deleteButton_);

  // Revert: only visible while the patch differs from the loaded preset (so its
  // mere presence flags "modified"). Amber to read as an unsaved-change cue.
  revertButton_.setColour(juce::TextButton::textColourOffId, OpnaColours::amber);
  revertButton_.onClick = [this] {
    OpnaModal::confirm(*this, "Discard Changes",
                       "Discard unsaved changes to the current patch?", "Discard",
                       OpnaColours::amber, [this] { revertToBaseline(); });
  };
  addChildComponent(revertButton_);  // hidden until dirty

  initButton_.onClick = [this] {
    OpnaModal::confirm(*this, "Initialize",
                       "Discard the current patch and reset everything to "
                       "defaults?",
                       "Init", OpnaColours::amber, [this] { resetToDefaults(); });
  };
  addAndMakeVisible(initButton_);

  addChildComponent(fmPage_);
  addChildComponent(ssgAdpcmPage_);
  addChildComponent(globalPage_);
  showPage(Page::Fm);
  refreshSampleDisplay();  // reflect a sample restored by the host before open

  applySavedSettings();  // push fidelity + user ROM prefs to the processor

  // Restore the persisted window size (a machine-local pref, not plugin state).
  // Read it BEFORE applying resize limits: setResizeLimits on the still-0x0 editor
  // clamps it up to the minimum and fires resized(), which (once windowSizeReady_
  // is set) would overwrite the saved size.
  const int savedW = uiProps_.getIntValue(kWindowWKey, 0);
  const int savedH = uiProps_.getIntValue(kWindowHKey, 0);
  setResizable(true, true);
  setResizeLimits(1040, 660, 1900, 1300);
  setSize(savedW > 0 ? savedW : kDefaultW, savedH > 0 ? savedH : kDefaultH);
  windowSizeReady_ = true;  // from here, real resizes may persist the size

  // Watch for parameter edits to drive the Revert/dirty indicator. Listening to
  // the parameters directly (not the APVTS ValueTree, which only syncs on a
  // timer) makes Rev appear the instant a value changes. The processor already
  // holds the restored baseline, so reflect the initial dirty state too (a patch
  // left modified before a reopen shows Rev right away).
  for (auto* param : processorRef.getParameters())
    param->addListener(this);
  updateDirtyState();
}

PluginEditor::~PluginEditor() {
  for (auto* param : processorRef.getParameters())
    param->removeListener(this);
  cancelPendingUpdate();
  setLookAndFeel(nullptr);
}

double PluginEditor::currentSamplerRate() const {
  int idx = kDefaultSamplerRateIndex;
  if (auto* p = processorRef.getApvts().getRawParameterValue("smp_rate"))
    idx = juce::jlimit(0, (int)kSamplerRates.size() - 1, (int)(p->load() + 0.5f));
  return kSamplerRates[(size_t)idx];
}

void PluginEditor::showPage(Page page) {
  fmPage_.setVisible(page == Page::Fm);
  ssgAdpcmPage_.setVisible(page == Page::SsgAdpcm);
  globalPage_.setVisible(page == Page::Global);

  auto highlight = [](juce::TextButton& tab, bool active) {
    tab.setColour(juce::TextButton::buttonColourId,
                  active ? OpnaColours::cyan : OpnaColours::panel);
    tab.setColour(juce::TextButton::textColourOffId,
                  active ? OpnaColours::bg : OpnaColours::dim);
  };
  highlight(fmTab_, page == Page::Fm);
  highlight(ssgAdpcmTab_, page == Page::SsgAdpcm);
  highlight(globalTab_, page == Page::Global);
}

void PluginEditor::setHelpEnabled(bool on) {
  // Toggle state drives the cyan-vs-panel fill (see the On* colours set in the
  // ctor), so this both updates the look and gates the tooltip window.
  helpButton_.setToggleState(on, juce::dontSendNotification);
  helpButton_.repaint();

  if (on && tooltipWindow_ == nullptr)
    tooltipWindow_ = std::make_unique<juce::TooltipWindow>(this);
  else if (!on)
    tooltipWindow_.reset();

  uiProps_.setValue(kHelpEnabledKey, on);
  uiProps_.saveIfNeeded();
}

void PluginEditor::openSettings() {
  settingsPanel_ = SettingsDialog::show(
      *this, indexFromFidelity(processorRef.fidelity()), rhythmRomStatus(),
      [this](int idx) { applyFidelityPref(idx); },
      [this] { loadRhythmRomDialog(); }, [this] { useDefaultRhythmRom(); },
      [this] { setSize(kDefaultW, kDefaultH); });
}

void PluginEditor::applyFidelityPref(int idx) {
  processorRef.setFidelity(fidelityFromIndex(idx));
  uiProps_.setValue(kFidelityKey, idx);
  uiProps_.saveIfNeeded();
}

void PluginEditor::loadRhythmRomDialog() {
  fileChooser_ = std::make_unique<juce::FileChooser>(
      "Load a YM2608 rhythm ROM (8 KB)", juce::File{}, "*.bin;*.rom");
  const auto chooserFlags = juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles;
  fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
    const juce::File file = fc.getResult();
    if (file != juce::File{})
      applyRhythmRomFile(file);
  });
}

void PluginEditor::applyRhythmRomFile(const juce::File& file) {
  juce::MemoryBlock mb;
  if (!file.loadFileAsData(mb) ||
      static_cast<int>(mb.getSize()) != kRhythmRomBytes) {
    OpnaModal::confirm(
        *this, "INVALID ROM",
        "The rhythm ROM must be exactly 8 KB (8192 bytes). This file is " +
            juce::String(mb.getSize()) + " bytes.",
        "OK", OpnaColours::amber, [] {});
    return;
  }
  processorRef.setUserRhythmRom(mb.getData(), static_cast<int>(mb.getSize()));
  uiProps_.setValue(kRomPathKey, file.getFullPathName());
  uiProps_.saveIfNeeded();
  if (settingsPanel_ != nullptr)
    settingsPanel_->setRomStatus(rhythmRomStatus());
}

void PluginEditor::useDefaultRhythmRom() {
  processorRef.setUserRhythmRom(nullptr, 0);  // revert to the default-location ROM
  uiProps_.setValue(kRomPathKey, juce::String());
  uiProps_.saveIfNeeded();
  if (settingsPanel_ != nullptr)
    settingsPanel_->setRomStatus(rhythmRomStatus());
}

juce::String PluginEditor::rhythmRomStatus() const {
  if (processorRef.usingUserRhythmRom()) {
    const juce::File f(uiProps_.getValue(kRomPathKey, {}));
    return "User: " + (f != juce::File{} ? f.getFileName() : juce::String("loaded"));
  }
  // No manual override: report the default-location auto-load.
  return processorRef.hasRhythmRom() ? juce::String("Default location")
                                     : juce::String("Not found (rhythm silent)");
}

void PluginEditor::applySavedSettings() {
  processorRef.setFidelity(fidelityFromIndex(uiProps_.getIntValue(kFidelityKey, 1)));

  const juce::File romFile(uiProps_.getValue(kRomPathKey, {}));
  if (romFile != juce::File{} && romFile.existsAsFile()) {
    juce::MemoryBlock mb;
    if (romFile.loadFileAsData(mb) &&
        static_cast<int>(mb.getSize()) == kRhythmRomBytes)
      processorRef.setUserRhythmRom(mb.getData(), static_cast<int>(mb.getSize()));
  }
}

void PluginEditor::loadPreset(int index) {
  const auto& presets = factoryPresets();
  if (index < 0 || index >= (int)presets.size())
    return;
  // Factory presets are full-state .opnapreset blobs: setStateInformation
  // restores every part and clears any loaded sample (they carry none). Record
  // the factory voice's name (the blob itself carries none, so setStateInformation
  // cleared it) so it persists in the plugin state and the dropdown re-selects it
  // on reopen instead of falling back to "Init".
  processorRef.setStateInformation(presets[(size_t)index].data,
                                   presets[(size_t)index].size);
  processorRef.setLoadedPresetName(presets[(size_t)index].name);
  refreshSampleDisplay();
  captureBaseline();  // the just-loaded preset is the clean revert point
}

void PluginEditor::resetToDefaults() {
  for (auto* param : processorRef.getParameters())
    param->setValueNotifyingHost(param->getDefaultValue());
  processorRef.unloadSample();
  ssgAdpcmPage_.setSampleWaveform(nullptr, 0, 0.0);
  processorRef.setLoadedPresetName({});
  selectPresetSilently(kInitPresetId);
  captureBaseline();  // Init defaults are the clean revert point
}

void PluginEditor::captureBaseline() {
  // Mark the current state clean: it becomes the processor-persisted revert
  // point (so the dirty/Revert indicator survives a reopen).
  processorRef.setPresetBaseline(processorRef.getApvts().copyState());
  updateDirtyState();
}

bool PluginEditor::isDirty() const {
  const juce::ValueTree& baseline = processorRef.getPresetBaseline();
  // copyState() flushes pending parameter values into the tree first, so the
  // comparison reflects the live edit immediately (the bare .state lags it).
  return baseline.isValid() &&
         !processorRef.getApvts().copyState().isEquivalentTo(baseline);
}

void PluginEditor::updateDirtyState() {
  const bool dirty = isDirty();
  if (dirty != revertButton_.isVisible()) {
    revertButton_.setVisible(dirty);
    resized();  // the centred block stays put; Rev appears to the right of Del
  }
}

void PluginEditor::revertToBaseline() {
  const juce::ValueTree& baseline = processorRef.getPresetBaseline();
  if (!baseline.isValid())
    return;
  processorRef.getApvts().replaceState(baseline.createCopy());
  updateDirtyState();
}

void PluginEditor::parameterValueChanged(int, float) {
  // Called on the message thread (UI edits) or audio thread (automation);
  // coalesce onto the message thread before touching components.
  triggerAsyncUpdate();
}

void PluginEditor::handleAsyncUpdate() {
  updateDirtyState();
}

void PluginEditor::refreshPresetCombo() {
  presetBox_.clear(juce::dontSendNotification);
  presetOrder_.clear();

  presetBox_.addItem("Init", kInitPresetId);
  presetOrder_.push_back(kInitPresetId);

  // Factory presets are tucked into a "Factory" submenu to keep the root list
  // short. Their ids still drive selection/loading; only the navigation order is
  // tracked separately (presetOrder_).
  const auto& presets = factoryPresets();
  juce::PopupMenu factoryMenu;
  for (int i = 0; i < (int)presets.size(); ++i) {
    factoryMenu.addItem(kFactoryPresetIdBase + i, presets[(size_t)i].name);
    presetOrder_.push_back(kFactoryPresetIdBase + i);
  }
  if (!presets.empty())
    presetBox_.getRootMenu()->addSubMenu("Factory", factoryMenu);

  userPresetFiles_ = presetManager_.getPresetFiles();
  if (userPresetFiles_.size() > 0) {
    presetBox_.addSeparator();
    for (int i = 0; i < userPresetFiles_.size(); ++i) {
      presetBox_.addItem(userPresetFiles_[i].getFileNameWithoutExtension(),
                         kUserPresetIdBase + i);
      presetOrder_.push_back(kUserPresetIdBase + i);
    }
  }

  // Reflect a persisted preset name if one is loaded; else default to the first
  // (Init) entry without triggering a load. A user file wins over a same-named
  // factory voice (it is the user's own save); a factory name is the fallback.
  const juce::String& loaded = processorRef.getLoadedPresetName();
  int selectId = 1;
  if (loaded.isNotEmpty()) {
    for (int i = 0; i < userPresetFiles_.size(); ++i) {
      if (userPresetFiles_[i].getFileNameWithoutExtension() == loaded) {
        selectId = kUserPresetIdBase + i;
        break;
      }
    }
    if (selectId == 1)
      for (int i = 0; i < (int)presets.size(); ++i)
        if (presets[(size_t)i].name == loaded) {
          selectId = kFactoryPresetIdBase + i;
          break;
        }
  }
  selectPresetSilently(selectId);
  updateDeleteEnabled();
}

void PluginEditor::stepPreset(int dir) {
  if (presetOrder_.empty())
    return;
  const int cur = presetBox_.getSelectedId();
  int idx = 0;
  for (int i = 0; i < (int)presetOrder_.size(); ++i)
    if (presetOrder_[(size_t)i] == cur) {
      idx = i;
      break;
    }
  const int n = (int)presetOrder_.size();
  idx = (idx + dir + n) % n;
  presetBox_.setSelectedId(presetOrder_[(size_t)idx]);  // notifies -> loads
}

void PluginEditor::updateDeleteEnabled() {
  deleteButton_.setEnabled(presetBox_.getSelectedId() >= kUserPresetIdBase);
}

void PluginEditor::selectPresetSilently(int id) {
  presetBox_.setSelectedId(id, juce::dontSendNotification);
  lastPresetId_ = id;
}

void PluginEditor::loadSelectedPreset(int id) {
  if (id == kInitPresetId) {
    resetToDefaults();
  } else if (id >= kUserPresetIdBase) {
    const int idx = id - kUserPresetIdBase;
    if (idx >= 0 && idx < userPresetFiles_.size()) {
      presetManager_.loadPreset(userPresetFiles_[idx]);
      refreshSampleDisplay();
      captureBaseline();  // the loaded user preset is the clean revert point
    }
  } else {
    loadPreset(id - kFactoryPresetIdBase);  // embedded factory .opnapreset
  }
  lastPresetId_ = id;
}

void PluginEditor::savePresetDialog() {
  OpnaModal::prompt(
      *this, "Save Preset", "Save the current state as a preset.",
      processorRef.getLoadedPresetName(), [this](const juce::String& name) {
        if (presetManager_.presetExists(name)) {
          OpnaModal::confirm(*this, "Overwrite",
                             "A preset named \"" + name +
                                 "\" already exists. Overwrite it?",
                             "Overwrite", OpnaColours::amber,
                             [this, name] { writePreset(name); });
        } else {
          writePreset(name);
        }
      });
}

void PluginEditor::writePreset(const juce::String& name) {
  // Saving makes the current state the new clean revert point; capture it before
  // the file is written so the saved blob embeds a self-consistent baseline.
  captureBaseline();
  const juce::File file = presetManager_.savePreset(name);
  if (!file.existsAsFile())
    return;
  refreshPresetCombo();
  for (int i = 0; i < userPresetFiles_.size(); ++i)
    if (userPresetFiles_[i] == file)
      selectPresetSilently(kUserPresetIdBase + i);
  updateDeleteEnabled();
}

void PluginEditor::deletePresetDialog() {
  const int id = presetBox_.getSelectedId();
  if (id < kUserPresetIdBase)
    return;  // factory/Init: nothing deletable selected
  const int idx = id - kUserPresetIdBase;
  if (idx < 0 || idx >= userPresetFiles_.size())
    return;
  const juce::File file = userPresetFiles_[idx];
  const juce::String name = file.getFileNameWithoutExtension();
  OpnaModal::confirm(
      *this, "Delete Preset",
      "Delete the preset \"" + name + "\"? This cannot be undone.", "Delete",
      OpnaColours::part(3) /* red */, [this, file] {
        if (!presetManager_.deletePreset(file))
          return;
        processorRef.setLoadedPresetName({});
        refreshPresetCombo();
        selectPresetSilently(kInitPresetId);
        updateDeleteEnabled();
      });
}

void PluginEditor::loadSampleDialog() {
  fileChooser_ = std::make_unique<juce::FileChooser>(
      "Load an audio file for the ADPCM-B sampler", juce::File{},
      "*.wav;*.aif;*.aiff;*.flac");
  const auto chooserFlags = juce::FileBrowserComponent::openMode |
                            juce::FileBrowserComponent::canSelectFiles;

  fileChooser_->launchAsync(chooserFlags, [this](const juce::FileChooser& fc) {
    const juce::File file = fc.getResult();
    if (file != juce::File{})
      loadSampleWithDialog(file);
  });
}

void PluginEditor::loadSampleWithDialog(const juce::File& file) {
  // Peek the file's length so the dialog can show the fits / window readout,
  // then let the user choose the encode-time settings before importing.
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
  if (reader == nullptr || reader->lengthInSamples <= 0 ||
      reader->sampleRate <= 0.0)
    return;
  const double fileSeconds = (double)reader->lengthInSamples / reader->sampleRate;

  auto& apvts = processorRef.getApvts();
  int rateIdx = kDefaultSamplerRateIndex;
  if (auto* p = apvts.getRawParameterValue("smp_rate"))
    rateIdx = (int)(p->load() + 0.5f);
  int offset = 0;
  if (auto* p = apvts.getRawParameterValue("smp_offset"))
    offset = (int)(p->load() + 0.5f);

  SampleImportDialog::show(
      *this, file.getFileName(), fileSeconds, rateIdx, offset,
      [this, file](int chosenRate, int chosenOffset) {
        auto& tree = processorRef.getApvts();
        if (auto* rp = dynamic_cast<juce::AudioParameterChoice*>(
                tree.getParameter("smp_rate")))
          *rp = chosenRate;
        if (auto* op = dynamic_cast<juce::AudioParameterInt*>(
                tree.getParameter("smp_offset")))
          *op = chosenOffset;
        loadSampleFromFile(file);
      });
}

void PluginEditor::loadSampleFromFile(const juce::File& file) {
  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
  if (reader == nullptr || reader->lengthInSamples <= 0)
    return;

  // A file longer than fits in the chip RAM at the chosen storage rate is
  // windowed rather than always truncated from the start: smp_offset slides the
  // fixed-size window across the source (0 = head, 1000 = tail). The chip holds
  // two ADPCM samples per RAM byte, so the in-RAM budget is 2*kAdpcmBRamBytes
  // stored samples; convert that back to source samples at the file's rate.
  const double target = currentSamplerRate();
  const juce::int64 maxStored = 2LL * opna::OpnaChip::kAdpcmBRamBytes;
  const juce::int64 budget = juce::jmax(
      (juce::int64)1,
      (juce::int64)std::llround((double)maxStored * reader->sampleRate / target));

  const juce::int64 fileLen = reader->lengthInSamples;
  juce::int64 startPos = 0;
  juce::int64 readLen = fileLen;
  if (fileLen > budget) {
    double offsetFrac = 0.0;
    if (auto* p = processorRef.getApvts().getRawParameterValue("smp_offset"))
      offsetFrac = juce::jlimit(0.0, 1.0, p->load() / 1000.0);
    startPos = (juce::int64)std::llround(offsetFrac * (double)(fileLen - budget));
    readLen = budget;
  }

  const int n = (int)juce::jmin((juce::int64)std::numeric_limits<int>::max(),
                                readLen);
  juce::AudioBuffer<float> buf((int)reader->numChannels, n);
  reader->read(&buf, 0, n, startPos, true, true);

  std::vector<float> mono((size_t)n, 0.0f);
  for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
    const float* src = buf.getReadPointer(ch);
    for (int i = 0; i < n; ++i)
      mono[(size_t)i] += src[i];
  }
  if (buf.getNumChannels() > 1)
    for (auto& s : mono)
      s /= (float)buf.getNumChannels();

  // Resample to the chosen storage rate before encoding. A lower rate widens the
  // sample's upward pitch range (delta-N is capped at ~55.5 kHz) at the cost of
  // treble; the original file PCM is the highest-quality source we will have.
  const auto resampled = resamplePcm(mono, reader->sampleRate, target);
  processorRef.loadSample(resampled.data(), (int)resampled.size(), target);
  // Display the decoded ADPCM-B (what actually plays back), so the load path and
  // the preset-restore path render the same faithful waveform.
  refreshSampleDisplay();
}

void PluginEditor::refreshSampleDisplay() {
  const auto& data = processorRef.loadedSampleData();
  if (data.empty()) {
    ssgAdpcmPage_.setSampleWaveform(nullptr, 0, 0.0);
    return;
  }
  // Reconstruct an approximate waveform by decoding the stored ADPCM-B.
  const auto pcm =
      opna::AdpcmBEncoder::decode(data.data(), static_cast<int>(data.size()));
  std::vector<float> mono(pcm.size());
  for (size_t i = 0; i < pcm.size(); ++i)
    mono[i] = pcm[i] / 32768.0f;
  ssgAdpcmPage_.setSampleWaveform(mono.data(), static_cast<int>(mono.size()),
                               processorRef.loadedSampleRate());
}

void PluginEditor::paint(juce::Graphics& g) {
  g.fillAll(OpnaColours::bg);
  // Toolbar strip: a banded gradient with a raised bevel (global chrome).
  auto bar = juce::Rectangle<int>(0, 0, getWidth(), kToolbarH);
  OpnaLookAndFeel::drawSteppedGradient(g, bar, OpnaColours::panel2,
                                       OpnaColours::panel, 6, true);
  OpnaLookAndFeel::drawBezel(g, bar, true, 2);
}

void PluginEditor::resized() {
  auto area = getLocalBounds();
  auto bar = area.removeFromTop(kToolbarH).reduced(kMargin, 6);

  // Each logical group has even kBtnGap spacing inside it and a wider kGroupGap
  // separating it from the next; the slack collects in the middle of the bar.

  // Left -- page tabs.
  fmTab_.setBounds(bar.removeFromLeft(74));
  bar.removeFromLeft(kBtnGap);
  ssgAdpcmTab_.setBounds(bar.removeFromLeft(110));
  bar.removeFromLeft(kBtnGap);
  globalTab_.setBounds(bar.removeFromLeft(96));

  // Right -- the help toggle, then the settings gear at the far edge.
  settingsButton_.setBounds(
      bar.removeFromRight(kBtnH).withSizeKeepingCentre(kBtnH, kBtnH));
  bar.removeFromRight(kBtnGap);
  helpButton_.setBounds(bar.removeFromRight(kBtnH).withSizeKeepingCentre(kBtnH, kBtnH));

  // Centre -- the preset block: Init/Save, then the picker (dropdown + nav),
  // then Del, then Rev. Rev only shows when the patch is dirty, so the core block
  // (through Del) is centred at a fixed width and Rev hangs off its right edge,
  // keeping the block from shifting as Rev appears/disappears.
  constexpr int kInitW = 56, kSaveW = 56, kBoxW = 210, kStepW = 30, kDelW = 48,
                kRevW = 48;
  const int coreW =
      kInitW + kSaveW + kBoxW + 2 * kStepW + kDelW + 5 * kBtnGap;
  auto block = bar.withSizeKeepingCentre(coreW, kBtnH);
  auto place = [&block](juce::Component& c, int w) {
    c.setBounds(block.removeFromLeft(w));
    block.removeFromLeft(kBtnGap);
  };
  place(initButton_, kInitW);
  place(saveButton_, kSaveW);
  place(presetBox_, kBoxW);
  place(prevPreset_, kStepW);
  place(nextPreset_, kStepW);
  deleteButton_.setBounds(block.removeFromLeft(kDelW));
  // Rev: just right of the (now fully consumed) core block.
  revertButton_.setBounds(block.getX() + kBtnGap, block.getY(), kRevW, kBtnH);

  auto body = area.reduced(kMargin);
  fmPage_.setBounds(body);
  ssgAdpcmPage_.setBounds(body);
  globalPage_.setBounds(body);

  // Persist the window size as a machine-local pref (PropertiesFile auto-saves on
  // its timer + destructor, so per-pixel resizes don't hammer the disk).
  // windowSizeReady_ suppresses the construction-time resize-limit clamp, which
  // would otherwise save the minimum (1040x660) as the size.
  if (windowSizeReady_ && getWidth() > 0 && getHeight() > 0) {
    uiProps_.setValue(kWindowWKey, getWidth());
    uiProps_.setValue(kWindowHKey, getHeight());
  }
}

}  // namespace audio_plugin
