#pragma once

#include <juce_core/juce_core.h>

namespace audio_plugin {

class PluginProcessor;  // forward declaration

// User preset bank: full plugin state serialised to files. A preset is the
// entire APVTS state (every FM part, SSG, rhythm, sampler config, routing, LFO,
// output) - the same blob as getStateInformation - so loading one restores the
// whole instrument. The in-memory factory voices in Presets.cpp are a separate,
// partial-apply quick-patch bank and are left untouched.
class PresetManager {
public:
  static constexpr const char* kExtension = "opnapreset";

  explicit PresetManager(PluginProcessor& processor);

  // %APPDATA%/atan2608/Presets (created on demand).
  juce::File getPresetsDir() const;
  juce::Array<juce::File> getPresetFiles() const;

  // The on-disk file a preset of this name would occupy (legal-filename-cleaned).
  // Returns an invalid File if the cleaned name is empty.
  juce::File presetFile(const juce::String& name) const;
  // True if a user preset of this name already exists on disk.
  bool presetExists(const juce::String& name) const;

  // Save the processor's current full state to <name>.opnapreset. Returns the
  // written file (or an invalid File on failure).
  juce::File savePreset(const juce::String& name);
  bool loadPreset(const juce::File& file);
  bool deletePreset(const juce::File& file);

private:
  PluginProcessor& processor_;
};

}  // namespace audio_plugin
