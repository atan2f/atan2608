#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace opna {

// Polyphase windowed-sinc resampler (native chip rate -> host rate).
//
// When the chip's native rate (~55.5 kHz at OPN_FIDELITY_MED) is decimated to a
// lower host rate (e.g. 48 kHz), the YM2608's bright FM harmonics above the host
// Nyquist would fold back into the audible band as inharmonic aliasing. The
// windowed-sinc kernel is a steep lowpass whose cutoff tracks the host Nyquist,
// so out-of-band content is removed instead of folded.
//
// JUCE-free and header-only so both the ymfm_opna static lib and the tests can
// use it. Allocation happens only in prepare(); process() is allocation-free.
class FirResampler {
public:
  // Half the FIR length, in input samples each side of the output point. 16 ->
  // 32 taps, a good fidelity/cost balance for a ~1.15x decimation.
  static constexpr int kHalf = 16;
  static constexpr int kTaps = 2 * kHalf;
  // The history is a power-of-two ring buffer so the logical->physical index map
  // is a cheap bitmask (see process()); keep kTaps a power of two.
  static_assert((kTaps & (kTaps - 1)) == 0, "kTaps must be a power of two");
  static constexpr int kMask = kTaps - 1;
  // Fractional-phase subdivisions of the precomputed polyphase table.
  static constexpr int kSub = 512;

  // ratio = nativeRate / hostRate (native input samples consumed per output
  // sample). >1 decimates (the aliasing-prone case); <=1 interpolates.
  void prepare(double ratio) {
    ratio_ = ratio > 0.0 ? ratio : 1.0;
    buildKernel();
    reset();
  }

  void reset() {
    histL_.fill(0.0f);
    histR_.fill(0.0f);
    head_ = 0;
    phase_ = 0.0;
    primed_ = false;
  }

  // Update only the resample ratio, keeping the existing FIR kernel and history.
  // RT-safe (no allocation). Intended for small ratio changes (e.g. swapping the
  // chip crystal between near-identical clocks) where rebuilding the kernel's
  // cutoff is unnecessary; for large changes use prepare() instead.
  void setRatio(double ratio) { ratio_ = ratio > 0.0 ? ratio : 1.0; }
  double ratio() const { return ratio_; }

  // Produce numSamples stereo output frames. pull(float& l, float& r) is invoked
  // whenever a fresh native frame is needed; it must fill l/r with one native
  // sample. Introduces kHalf input samples of latency (sub-millisecond).
  template <typename Pull>
  void process(float* left, float* right, int numSamples, Pull&& pull) {
    if (!primed_) {
      for (int k = 0; k < kTaps; ++k)
        pull(histL_[(size_t)k], histR_[(size_t)k]);
      head_ = 0;
      phase_ = 0.0;
      primed_ = true;
    }

    // History is a ring buffer: head_ is the oldest sample (logical index 0), so
    // logical tap k lives at physical (head_ + k) & kMask. Advancing consumes one
    // input by overwriting the oldest slot with the new sample and bumping head_,
    // which makes that sample the newest (logical kTaps-1) - no array shift.
    for (int i = 0; i < numSamples; ++i) {
      int sub = static_cast<int>(phase_ * kSub);
      if (sub < 0)
        sub = 0;
      else if (sub >= kSub)
        sub = kSub - 1;
      const float* ker = &kernel_[static_cast<size_t>(sub) * kTaps];

      float accL = 0.0f, accR = 0.0f;
      for (int k = 0; k < kTaps; ++k) {
        const size_t idx = static_cast<size_t>((head_ + k) & kMask);
        accL += ker[k] * histL_[idx];
        accR += ker[k] * histR_[idx];
      }
      left[i] = accL;
      right[i] = accR;

      phase_ += ratio_;
      while (phase_ >= 1.0) {
        pull(histL_[(size_t)head_], histR_[(size_t)head_]);
        head_ = (head_ + 1) & kMask;
        phase_ -= 1.0;
      }
    }
  }

private:
  static double sinc(double x) {
    if (std::abs(x) < 1e-9)
      return 1.0;
    const double px = 3.14159265358979323846 * x;
    return std::sin(px) / px;
  }

  void buildKernel() {
    kernel_.assign(static_cast<size_t>(kSub) * kTaps, 0.0f);

    // Cutoff in cycles/sample at the native rate: half the host Nyquist when
    // decimating, with a small rolloff so the transition band stays inside
    // Nyquist. No extra lowpass when interpolating (ratio <= 1).
    constexpr double rolloff = 0.90;
    const double fc = 0.5 * std::min(1.0, 1.0 / ratio_) * rolloff;

    for (int sub = 0; sub < kSub; ++sub) {
      const double frac = static_cast<double>(sub) / kSub;
      double row[kTaps];
      double sum = 0.0;
      for (int k = 0; k < kTaps; ++k) {
        // Output point sits between hist[kHalf-1] and hist[kHalf] at `frac`.
        const double d = static_cast<double>(kHalf - 1) + frac - k;
        // Blackman window in terms of normalised distance x in [-1, 1].
        const double x = d / kHalf;
        double w = 0.0;
        if (x > -1.0 && x < 1.0)
          w = 0.42 + 0.5 * std::cos(3.14159265358979323846 * x) +
              0.08 * std::cos(2.0 * 3.14159265358979323846 * x);
        row[k] = sinc(2.0 * fc * d) * w;
        sum += row[k];
      }
      // Normalise to unity DC gain.
      const double inv = sum != 0.0 ? 1.0 / sum : 1.0;
      for (int k = 0; k < kTaps; ++k)
        kernel_[static_cast<size_t>(sub) * kTaps + k] =
            static_cast<float>(row[k] * inv);
    }
  }

  double ratio_ = 1.0;
  double phase_ = 0.0;
  int head_ = 0;  // ring-buffer index of the oldest history sample (logical 0)
  bool primed_ = false;
  std::array<float, kTaps> histL_{};
  std::array<float, kTaps> histR_{};
  std::vector<float> kernel_;
};

}  // namespace opna
