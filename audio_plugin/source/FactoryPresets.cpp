#include "FactoryPresets.h"

#include <algorithm>

#include "BinaryData.h"

namespace audio_plugin {

const std::vector<FactoryPreset>& factoryPresets() {
  static const std::vector<FactoryPreset> presets = [] {
    std::vector<FactoryPreset> v;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
      const juce::String original = BinaryData::originalFilenames[i];
      if (!original.endsWith(".opnapreset"))
        continue;
      int size = 0;
      const char* data = BinaryData::getNamedResource(
          BinaryData::namedResourceList[i], size);
      if (data == nullptr || size <= 0)
        continue;
      v.push_back({original.dropLastCharacters(
                       juce::String(".opnapreset").length()),
                   data, size});
    }
    std::sort(v.begin(), v.end(), [](const FactoryPreset& a, const FactoryPreset& b) {
      return a.name.compareNatural(b.name) < 0;
    });
    return v;
  }();
  return presets;
}

}  // namespace audio_plugin
