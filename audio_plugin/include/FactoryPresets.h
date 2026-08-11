#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace audio_plugin {

// One embedded factory preset: a display name and a pointer to its full-state
// .opnapreset bytes (a getStateInformation blob), loaded via
// PluginProcessor::setStateInformation like any user preset.
struct FactoryPreset {
  juce::String name;
  const void* data = nullptr;
  int size = 0;
};

// The factory bank, discovered from the embedded BinaryData resources (every
// *.opnapreset file compiled in by juce_add_binary_data), sorted by name. The
// bank is generated/committed by PresetGeneratorTest; adding a preset is just
// dropping a file in factory_presets/ and reconfiguring -- no code change here.
const std::vector<FactoryPreset>& factoryPresets();

}  // namespace audio_plugin
