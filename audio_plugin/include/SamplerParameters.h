#pragma once

#include <array>
#include <atomic>

#include "Routing.h"

namespace audio_plugin {

// Storage sample rates offered for ADPCM-B (the "smp_rate" choice). Lower rates
// trade top-end fidelity for a wider upward playback range: delta-N (and so the
// max playback rate) is capped at ~55.5 kHz, so a sample stored at a high rate
// can only be transposed up a few semitones before it clamps. Storing at, say,
// 16 kHz leaves headroom for ~21 semitones up. The chosen rate is the rate the
// PCM is resampled to before encoding, and the delta-N root the chip plays at.
inline constexpr std::array<double, 6> kSamplerRates{8000.0, 11025.0, 16000.0,
                                                     22050.0, 32000.0, 44100.0};
inline constexpr int kDefaultSamplerRateIndex = 2;  // 16 kHz

void addSamplerParameters(
    juce::AudioProcessorValueTreeState::ParameterLayout& layout);

// Cached pointers for the ADPCM-B sampler part.
class SamplerParameters {
public:
  void connect(juce::AudioProcessorValueTreeState& apvts);
  int level() const;      // 0..255
  int rootNote() const;   // MIDI note the sample plays at original pitch
  int pan() const;        // 0=L 1=C 2=R
  int startPermille() const;  // playback start, 0..1000 of the sample
  int endPermille() const;    // playback end, 0..1000 of the sample
  bool loop() const;          // repeat start..end until note-off
  bool reencodeStart() const;  // re-encode so a non-zero start plays full-level

private:
  std::atomic<float>* level_ = nullptr;
  std::atomic<float>* rootNote_ = nullptr;
  std::atomic<float>* pan_ = nullptr;
  std::atomic<float>* start_ = nullptr;
  std::atomic<float>* end_ = nullptr;
  std::atomic<float>* loop_ = nullptr;
  std::atomic<float>* reencode_ = nullptr;
};

}  // namespace audio_plugin
