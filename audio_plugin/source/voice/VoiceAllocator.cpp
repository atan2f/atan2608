#include "VoiceAllocator.h"

#include <algorithm>

namespace opna {

VoiceAllocator::VoiceAllocator(int numChannels)
    : numChannels_(std::clamp(numChannels, 1, kMaxChannels)) {}

int VoiceAllocator::noteOn(int midiNote) {
  ++counter_;

  // Prefer a free channel; among free channels pick the least-recently-active
  // one (never-used channels have age 0; a released channel keeps the age it
  // was stamped with at note-off, so the oldest release is reused first and a
  // fresh release tail is left to ring out).
  int chosen = -1;
  uint64_t bestAge = 0;
  for (int ch = 0; ch < numChannels_; ++ch) {
    if (voices_[ch].note < 0 && (chosen < 0 || voices_[ch].age < bestAge)) {
      chosen = ch;
      bestAge = voices_[ch].age;
    }
  }

  // Otherwise every channel is held: steal the oldest sounding voice.
  if (chosen < 0) {
    bestAge = voices_[0].age;
    chosen = 0;
    for (int ch = 1; ch < numChannels_; ++ch) {
      if (voices_[ch].age < bestAge) {
        bestAge = voices_[ch].age;
        chosen = ch;
      }
    }
  }

  voices_[chosen].note = midiNote;
  voices_[chosen].age = counter_;
  return chosen;
}

int VoiceAllocator::noteOff(int midiNote) {
  for (int ch = 0; ch < numChannels_; ++ch) {
    if (voices_[ch].note == midiNote) {
      voices_[ch].note = -1;
      // Stamp the release time so this channel ranks behind never-used
      // channels (and behind earlier releases) the next time we allocate.
      voices_[ch].age = ++counter_;
      return ch;
    }
  }
  return -1;
}

void VoiceAllocator::reset() {
  for (auto& v : voices_)
    v = Voice{};
  counter_ = 0;
}

}  // namespace opna
