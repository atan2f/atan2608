#include "Presets.h"

namespace audio_plugin {

opna::FmPatch initPatch() {
  opna::FmPatch p;
  p.algorithm = 7;
  p.op[0] = {0, 1, 0, 0, 31, 0, 0, 15, 0, 0, false};
  p.op[1] = {0, 1, 127, 0, 31, 0, 0, 7, 0, 0, false};
  p.op[2] = {0, 1, 127, 0, 31, 0, 0, 7, 0, 0, false};
  p.op[3] = {0, 1, 127, 0, 31, 0, 0, 7, 0, 0, false};
  return p;
}

}  // namespace audio_plugin
