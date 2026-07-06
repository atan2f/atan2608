#pragma once

#include <algorithm>
#include <cmath>

namespace opna {

// Global output-stage peak limiter (JUCE-free so the plugin TU and the tests can
// both use it). Feed-forward, stereo-linked, soft-knee: a single gain is derived
// from the louder of the two channels and smoothed with a fast attack / slow
// release one-pole, so transients are tamed without the channels drifting apart.
// A final hard clamp at the ceiling guarantees no true overs even on the brief
// overshoot before the smoothed gain catches a fast transient.
//
// The chip can spike hard (notably the SSG with its hardware envelope), so this
// sits after the master gain in processBlock when the "limiter" param is on.
class Limiter {
public:
  void prepare(double sampleRate) {
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    attackCoeff_ = coeff(attackMs_);
    releaseCoeff_ = coeff(releaseMs_);
    reset();
  }

  void reset() { gain_ = 1.0f; }

  // ceiling: linear peak ceiling (default ~ -0.3 dBFS). left/right are the output
  // channels; pass right == nullptr for mono.
  void process(float* left, float* right, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
      const float l = left[i];
      const float r = right != nullptr ? right[i] : 0.0f;
      const float peak = std::max(std::abs(l), std::abs(r));

      float target = 1.0f;
      if (peak > ceiling_)
        target = ceiling_ / peak;

      // Fast attack (clamp down quickly), slow release (recover gently).
      const float c = target < gain_ ? attackCoeff_ : releaseCoeff_;
      gain_ = target + (gain_ - target) * c;

      const float gl = clampToCeiling(l * gain_);
      left[i] = gl;
      if (right != nullptr)
        right[i] = clampToCeiling(r * gain_);
    }
  }

private:
  float coeff(double timeMs) const {
    const double t = 0.001 * timeMs * sampleRate_;
    return t > 0.0 ? static_cast<float>(std::exp(-1.0 / t)) : 0.0f;
  }

  float clampToCeiling(float x) const {
    return std::max(-ceiling_, std::min(ceiling_, x));
  }

  double sampleRate_ = 44100.0;
  float ceiling_ = 0.96605f;  // -0.3 dBFS
  float attackMs_ = 1.0f;
  float releaseMs_ = 100.0f;
  float attackCoeff_ = 0.0f;
  float releaseCoeff_ = 0.0f;
  float gain_ = 1.0f;
};

}  // namespace opna
