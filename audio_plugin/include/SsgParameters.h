#pragma once

#include <array>
#include <atomic>

#include "Routing.h"
#include "SsgPatch.h"

namespace audio_plugin {

// Appends SSG patch parameters and per-part MIDI routing parameters to a layout.
void addSsgParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout);
void addRoutingParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

// Cached pointers for assembling the SSG state on the audio thread. Covers the
// shared generators + pooled voice (SsgPatch) plus the per-channel mode and
// independent voices/routing.
class SsgParameters {
public:
  static constexpr int kNumChannels = 3;

  void connect(juce::AudioProcessorValueTreeState& apvts);

  // Full chip-relevant state (pooled voice + shared generators + per-channel
  // mode and independent voices).
  opna::SsgState buildState() const;

  // Pooled voice + shared generators alone (used where only the part-global
  // patch is needed, e.g. presets).
  opna::SsgPatch buildPooled() const;

  // MIDI routing + solo for an independent channel (0..2). Kept separate from
  // the chip state, matching the other parts.
  opna::PartRouting indepRouting(int channel) const;
  bool indepSolo(int channel) const;
  bool independent(int channel) const;

private:
  std::atomic<float>* mix_ = nullptr;
  std::atomic<float>* volume_ = nullptr;
  std::atomic<float>* envEnable_ = nullptr;
  std::atomic<float>* envPeriod_ = nullptr;
  std::atomic<float>* envShape_ = nullptr;
  std::atomic<float>* noisePeriod_ = nullptr;

  // Per-channel A/B/C.
  struct Channel {
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* volume = nullptr;
    std::atomic<float>* envEnable = nullptr;
    std::atomic<float>* channel = nullptr;  // MIDI channel (routing)
    std::atomic<float>* low = nullptr;
    std::atomic<float>* high = nullptr;
    std::atomic<float>* octave = nullptr;   // play-time octave transpose
    std::atomic<float>* solo = nullptr;
  };
  std::array<Channel, kNumChannels> ch_{};
};

// Cached pointers for the SSG / rhythm / sampler part routing. FM routing is
// per-group and lives in PatchParameters.
class RoutingParameters {
public:
  void connect(juce::AudioProcessorValueTreeState& apvts);
  opna::PartRouting ssg() const;
  opna::PartRouting rhythm() const;
  opna::PartRouting sampler() const;
  bool ssgSolo() const;
  bool rhythmSolo() const;
  bool samplerSolo() const;

private:
  std::atomic<float>* ssgChannel_ = nullptr;
  std::atomic<float>* ssgLow_ = nullptr;
  std::atomic<float>* ssgHigh_ = nullptr;
  std::atomic<float>* ssgOctave_ = nullptr;
  std::atomic<float>* rhythmChannel_ = nullptr;
  std::atomic<float>* rhythmLow_ = nullptr;
  std::atomic<float>* rhythmHigh_ = nullptr;
  std::atomic<float>* samplerChannel_ = nullptr;
  std::atomic<float>* samplerLow_ = nullptr;
  std::atomic<float>* samplerHigh_ = nullptr;
  std::atomic<float>* samplerOctave_ = nullptr;
  std::atomic<float>* ssgSolo_ = nullptr;
  std::atomic<float>* rhythmSolo_ = nullptr;
  std::atomic<float>* samplerSolo_ = nullptr;
};

}  // namespace audio_plugin
