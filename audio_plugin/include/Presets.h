#pragma once

#include "FmPatch.h"

namespace audio_plugin {

// The default FM patch. The APVTS parameter defaults are derived from this, so
// the Init button (which resets parameters to their defaults) returns here.
//
// The selectable factory voices are full .opnapreset files embedded as
// BinaryData (authored by PresetGeneratorTest, surfaced by FactoryPresets) and
// loaded like any user preset.
opna::FmPatch initPatch();

}  // namespace audio_plugin
