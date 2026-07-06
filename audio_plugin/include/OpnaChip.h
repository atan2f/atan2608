#pragma once

#include <memory>

namespace opna {

// JUCE-free, allocation-on-prepare wrapper around ymfm's YM2608 (OPNA) core.
// The ymfm headers live entirely inside OpnaChip.cpp so the strict
// warnings-as-errors plugin TU never sees them.
class OpnaChip {
public:
  OpnaChip();
  ~OpnaChip();

  OpnaChip(const OpnaChip&) = delete;
  OpnaChip& operator=(const OpnaChip&) = delete;

  // Resampler/oversampling quality. Maps to ymfm's OPN_FIDELITY_MIN/MED/MAX:
  // higher = higher native rate (less aliasing, more CPU). Med is the default
  // and what every prior build used.
  enum class Fidelity { Min, Med, Max };

  // Allocate resampler state for the given host sample rate and reset the chip.
  void prepare(double hostSampleRate);

  // Reset the chip to power-on state (does not change prepared rate).
  void reset();

  // Set the master crystal (Hz) and prescale (6/3/2). RT-safe: updates the
  // native/host resample ratio in place (no kernel reallocation - the clock
  // options differ by well under 1%, so the existing FIR cutoff stays valid) and
  // writes the prescale select registers. Does not rebuild voices; the processor
  // re-pitches sounding notes and re-pushes patches after calling this. Safe to
  // call before prepare() (the values are latched and applied on the next reset).
  void setChipConfig(double clockHz, int prescale);

  // Set the oversampling fidelity. NOT RT-safe - rebuilds the FIR kernel and
  // changes the native rate substantially - so call it only from the message
  // thread with audio processing suspended (or before prepare()).
  void setFidelity(Fidelity f);

  // Provide the YM2608 ADPCM-A rhythm ROM (the drum samples). The buffer must
  // outlive the chip; pass nullptr to disable rhythm.
  void setRhythmRom(const unsigned char* data, int size);

  // Size of the external ADPCM-B sample RAM, in bytes (256 KB). Each byte holds
  // two ADPCM nibbles, so it stores up to 2 * kAdpcmBRamBytes decoded samples.
  static constexpr int kAdpcmBRamBytes = 256 * 1024;

  // Copy encoded ADPCM-B sample data into the chip's external RAM (256 KB).
  void loadAdpcmB(const unsigned char* data, int size);

  // Register writes. Port 0 covers FM channels 1-3 / SSG / common registers;
  // port 1 covers FM channels 4-6.
  void writeReg0(int reg, int value);
  void writeReg1(int reg, int value);

  // Render numSamples of stereo audio at the prepared host rate.
  // left/right must point to numSamples floats each.
  void render(float* left, float* right, int numSamples);

  // Clock the chip forward by n native frames, discarding the audio. The chip
  // only edge-detects key-on/off at clock time, so a key-off immediately
  // followed by a key-on within one render segment collapses to "still on" and
  // never re-attacks. Inserting this gap between them commits the key-off
  // keystate, making the following key-on a clean 0->1 edge - the chip-accurate
  // retrigger a tracker expects. Bypasses the resampler entirely (discarded
  // frames never enter its history/phase), so it does not perturb host-rate
  // output beyond the small chip-time advance (hardware-accurate, shared across
  // the whole chip's one clock domain - well under 1 ms, inaudible).
  //
  // n <= 0 (the default) auto-sizes the gap to the current fidelity/prescale:
  // ymfm only clocks the FM/ADPCM engine once every `fmSamplesPerOutput` native
  // frames (= prescale at MED, but 18 at MAX/ps6), so the gap must exceed that
  // or the key-off stays un-clocked. The auto value is fmSamplesPerOutput + 2.
  void advanceNativeFrames(int n = 0);

  // Default master clock, in Hz (PC-9801-86 standard). The live clock is set via
  // setChipConfig(); this is the power-on/default value.
  static constexpr double kClockHz = 7987200.0;

  double nativeSampleRate() const;

  // Number of times the given chip timer (0 = Timer A, 1 = Timer B) has expired
  // since construction. Timer A drives CH3 CSM retriggers; exposed for tests.
  unsigned long long timerExpiryCount(int timer) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace opna
