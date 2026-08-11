#include "PresetManager.h"

#include "PluginProcessor.h"

namespace audio_plugin {

PresetManager::PresetManager(PluginProcessor& processor)
    : processor_(processor) {}

juce::File PresetManager::getPresetsDir() const {
  auto dir = juce::File::getSpecialLocation(
                 juce::File::userApplicationDataDirectory)
                 .getChildFile("atan2608")
                 .getChildFile("Presets");
  dir.createDirectory();
  return dir;
}

juce::Array<juce::File> PresetManager::getPresetFiles() const {
  juce::Array<juce::File> files;
  getPresetsDir().findChildFiles(files, juce::File::findFiles, false,
                                 juce::String("*.") + kExtension);
  files.sort();
  return files;
}

juce::File PresetManager::presetFile(const juce::String& name) const {
  const juce::String clean = juce::File::createLegalFileName(name).trim();
  if (clean.isEmpty())
    return {};
  return getPresetsDir().getChildFile(clean + "." + kExtension);
}

bool PresetManager::presetExists(const juce::String& name) const {
  const juce::File file = presetFile(name);
  return file != juce::File{} && file.existsAsFile();
}

juce::File PresetManager::savePreset(const juce::String& name) {
  const juce::File file = presetFile(name);
  if (file == juce::File{})
    return {};

  processor_.setLoadedPresetName(file.getFileNameWithoutExtension());
  juce::MemoryBlock data;
  processor_.getStateInformation(data);

  if (!file.replaceWithData(data.getData(), data.getSize()))
    return {};
  return file;
}

bool PresetManager::loadPreset(const juce::File& file) {
  juce::MemoryBlock data;
  if (!file.loadFileAsData(data))
    return false;
  processor_.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
  // setStateInformation restores the name from the XML if present; otherwise
  // fall back to the filename so externally-created presets still display.
  if (processor_.getLoadedPresetName().isEmpty())
    processor_.setLoadedPresetName(file.getFileNameWithoutExtension());
  return true;
}

bool PresetManager::deletePreset(const juce::File& file) {
  return file.existsAsFile() && file.deleteFile();
}

}  // namespace audio_plugin
