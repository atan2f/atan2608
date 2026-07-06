#pragma once

#include <cstdint>

namespace opna {

// Maps MIDI notes onto a fixed set of chip channels: prefers a free channel,
// otherwise steals the oldest-sounding one. Reused for both the 6 FM channels
// and the 3 SSG channels.
class VoiceAllocator {
public:
  static constexpr int kMaxChannels = 6;

  explicit VoiceAllocator(int numChannels = kMaxChannels);

  // Returns the assigned channel (0..numChannels-1).
  int noteOn(int midiNote);

  // Returns the channel that was playing the note, or -1 if none.
  int noteOff(int midiNote);

  void reset();

  int numChannels() const { return numChannels_; }

  // Current note on a channel, or -1 if free.
  int noteAt(int channel) const { return voices_[channel].note; }

private:
  struct Voice {
    int note = -1;
    uint64_t age = 0;
  };
  int numChannels_;
  Voice voices_[kMaxChannels];
  uint64_t counter_ = 0;
};

}  // namespace opna
