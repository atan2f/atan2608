#include "../include/OpnaChip.h"

#include <cstdint>
#include <cstring>
#include <vector>

#include "FirResampler.h"
#include "ymfm_opn.h"

namespace opna {

// Interface implementation: serves the rhythm ROM / ADPCM-B RAM and drives the
// chip's two internal timers. ymfm delegates timer scheduling to the host: it
// calls ymfm_set_timer when a timer is (re)loaded and expects us to call back
// engine_timer_expired() after the given number of master clocks. We track a
// per-timer countdown and tick it in lockstep with chip generation (see
// advanceClocks). This is what makes Timer A - and therefore CH3 CSM mode -
// actually fire; without it the timers are inert.
class OpnaInterface : public ymfm::ymfm_interface {
public:
  uint8_t ymfm_external_read(ymfm::access_class type,
                             uint32_t address) override {
    if (type == ymfm::ACCESS_ADPCM_A && rhythmRom != nullptr &&
        address < static_cast<uint32_t>(rhythmRomSize))
      return rhythmRom[address];
    if (type == ymfm::ACCESS_ADPCM_B && address < adpcmBRam.size())
      return adpcmBRam[address];
    return 0;
  }
  void ymfm_external_write(ymfm::access_class type,
                           uint32_t address,
                           uint8_t data) override {
    if (type == ymfm::ACCESS_ADPCM_B && address < adpcmBRam.size())
      adpcmBRam[address] = data;
  }

  // Schedule (or cancel, when duration < 0) a timer expiry. Called by the chip
  // from the audio thread inside chip.write - same thread as advanceClocks, so
  // no synchronisation is needed.
  void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override {
    if (tnum < 2)
      timerClocks[tnum] = (duration_in_clocks < 0)
                              ? -1
                              : static_cast<int64_t>(duration_in_clocks);
  }

  // Advance both timers by `clocks` master clocks, firing every expiry that
  // falls in the interval. engine_timer_expired() reloads the timer (via another
  // ymfm_set_timer call) and, in CSM mode, retriggers CH3's operators. The
  // overshoot is carried into the next period so the trigger rate stays phase-
  // accurate over time (no slow pitch drift).
  void advanceClocks(int clocks) {
    if (m_engine == nullptr)
      return;
    for (uint32_t t = 0; t < 2; ++t) {
      if (timerClocks[t] < 0)
        continue;  // idle
      timerClocks[t] -= clocks;
      while (timerClocks[t] <= 0) {
        const int64_t overshoot = timerClocks[t];  // <= 0
        ++timerExpiries[t];
        m_engine->engine_timer_expired(t);  // reloads timerClocks[t]
        if (timerClocks[t] < 0)
          break;  // timer was turned off during the callback
        timerClocks[t] += overshoot;  // consume the overshoot from the new period
      }
    }
  }

  const uint8_t* rhythmRom = nullptr;
  int rhythmRomSize = 0;
  std::vector<uint8_t> adpcmBRam;  // external ADPCM-B memory (256 KB)

  // Per-timer countdown in master clocks (-1 = idle) and a free-running expiry
  // counter (for tests).
  int64_t timerClocks[2] = {-1, -1};
  uint64_t timerExpiries[2] = {0, 0};
};

struct OpnaChip::Impl {
  OpnaInterface intf;
  ymfm::ym2608 chip{intf};

  double nativeRate = 0.0;
  double hostRate = 0.0;
  double ratio = 1.0;  // native samples advanced per host sample
  int clocksPerSample = 24;  // master clocks per native frame (clock/24 at MED)

  // Live chip configuration (defaults match every prior build).
  double clockHz = OpnaChip::kClockHz;
  int prescale = 6;
  ymfm::opn_fidelity fidelity = ymfm::OPN_FIDELITY_MED;
  // ymfm clocks the FM/ADPCM engine once every this many native frames; the
  // retrigger gap must exceed it. Recomputed from fidelity+prescale.
  int fmSamplesPerOutput = 6;

  // Polyphase windowed-sinc resampler (native rate -> host rate) - anti-aliased
  // decimation. See FirResampler.h.
  FirResampler resampler;

  // ymfm's per-fidelity/prescale FM-sample-per-output cadence (see
  // ym2608::update_prescale). 0 is the MIN/ps3 "1.5" special case; clamp to 2 so
  // the retrigger gap still clears it.
  static int fmSamplesFor(ymfm::opn_fidelity fid, int ps) {
    int v = 6;
    if (fid == ymfm::OPN_FIDELITY_MIN)
      v = (ps == 6) ? 3 : (ps == 3) ? 0 : 1;
    else if (fid == ymfm::OPN_FIDELITY_MAX)
      v = (ps == 6) ? 18 : (ps == 3) ? 9 : 6;
    else  // MED
      v = (ps == 6) ? 6 : (ps == 3) ? 3 : 2;
    return v < 2 ? 2 : v;
  }

  // Recompute native rate, resample ratio and timer cadence from the current
  // clock/fidelity. rebuildKernel rebuilds the FIR (needed when the ratio moves
  // a lot, e.g. a fidelity change); otherwise only the ratio is updated in place
  // (RT-safe) so a small clock swap doesn't reallocate.
  void deriveRates(bool rebuildKernel) {
    nativeRate = static_cast<double>(
        chip.sample_rate(static_cast<uint32_t>(clockHz)));
    ratio = (hostRate > 0.0) ? nativeRate / hostRate : 1.0;
    clocksPerSample = static_cast<int>(clockHz / nativeRate + 0.5);
    fmSamplesPerOutput = fmSamplesFor(fidelity, prescale);
    if (rebuildKernel)
      resampler.prepare(ratio);
    else
      resampler.setRatio(ratio);
  }

  // Write the prescale-select address sequence (reg 0x2D/0x2E/0x2F). Always
  // route through 0x2D (= prescale 6) first because ymfm only honours 0x2E from
  // prescale 6, so this works regardless of the current prescale.
  void applyPrescale(int p) {
    chip.write(0, 0x2d);
    chip.write(1, 0x00);  // -> prescale 6 (the reset default)
    if (p == 3) {
      chip.write(0, 0x2e);
      chip.write(1, 0x00);
    } else if (p == 2) {
      chip.write(0, 0x2f);
      chip.write(1, 0x00);
    }
  }

  // Reset the chip and switch it into 6-FM-channel mode. The YM2608 powers up
  // in 3-channel (OPN-compatible) mode; register 0x29 bit 7 enables channels
  // 4-6. ymfm's reset clears this, so it must be re-applied after every reset.
  // The prescale also resets to 6, so re-apply the configured value too.
  void resetChip() {
    chip.reset();
    chip.write(0, 0x29);
    chip.write(1, 0x80);
    applyPrescale(prescale);
  }

  // Generate one native-rate stereo frame from the chip. Ticks the timers in
  // lockstep so Timer A (CSM) fires at the right sample.
  void genFrame(float& l, float& r) {
    intf.advanceClocks(clocksPerSample);
    ymfm::ym2608::output_data out;
    chip.generate(&out, 1);
    // data[0]/[1] = FM left/right, data[2] = SSG (mono). Mix SSG into both.
    constexpr float scale = 1.0f / 32768.0f;
    l = static_cast<float>(out.data[0] + out.data[2]) * scale;
    r = static_cast<float>(out.data[1] + out.data[2]) * scale;
  }
};

OpnaChip::OpnaChip() : impl_(std::make_unique<Impl>()) {}
OpnaChip::~OpnaChip() = default;

void OpnaChip::prepare(double hostSampleRate) {
  if (impl_->intf.adpcmBRam.empty())
    impl_->intf.adpcmBRam.assign(kAdpcmBRamBytes, 0);  // 256 KB external RAM

  impl_->chip.set_fidelity(impl_->fidelity);
  impl_->resetChip();

  impl_->hostRate = hostSampleRate;
  impl_->deriveRates(/*rebuildKernel=*/true);
}

void OpnaChip::reset() {
  impl_->resetChip();
  impl_->resampler.reset();
}

void OpnaChip::setChipConfig(double clockHz, int prescale) {
  if (clockHz > 0.0)
    impl_->clockHz = clockHz;
  if (prescale == 6 || prescale == 3 || prescale == 2)
    impl_->prescale = prescale;
  // Resampler is unaffected by prescale (sample_rate depends only on fidelity);
  // a clock swap only nudges the ratio, so update it in place without rebuilding.
  impl_->deriveRates(/*rebuildKernel=*/false);
  impl_->applyPrescale(impl_->prescale);
}

void OpnaChip::setFidelity(Fidelity f) {
  impl_->fidelity = (f == Fidelity::Min)   ? ymfm::OPN_FIDELITY_MIN
                    : (f == Fidelity::Max) ? ymfm::OPN_FIDELITY_MAX
                                           : ymfm::OPN_FIDELITY_MED;
  impl_->chip.set_fidelity(impl_->fidelity);
  // Native rate moves a lot, so rebuild the FIR kernel (allocates - caller must
  // ensure this runs off the audio thread / with processing suspended).
  impl_->deriveRates(/*rebuildKernel=*/true);
  impl_->resampler.reset();
}

void OpnaChip::setRhythmRom(const unsigned char* data, int size) {
  impl_->intf.rhythmRom = data;
  impl_->intf.rhythmRomSize = size;
}

void OpnaChip::loadAdpcmB(const unsigned char* data, int size) {
  auto& ram = impl_->intf.adpcmBRam;
  if (ram.empty())
    ram.assign(kAdpcmBRamBytes, 0);
  const int n = size < static_cast<int>(ram.size()) ? size
                                                    : static_cast<int>(ram.size());
  if (n > 0)
    std::memcpy(ram.data(), data, static_cast<size_t>(n));
}

void OpnaChip::writeReg0(int reg, int value) {
  impl_->chip.write(0, static_cast<uint8_t>(reg));
  impl_->chip.write(1, static_cast<uint8_t>(value));
}

void OpnaChip::writeReg1(int reg, int value) {
  impl_->chip.write(2, static_cast<uint8_t>(reg));
  impl_->chip.write(3, static_cast<uint8_t>(value));
}

void OpnaChip::render(float* left, float* right, int numSamples) {
  Impl& s = *impl_;
  s.resampler.process(left, right, numSamples,
                      [&s](float& l, float& r) { s.genFrame(l, r); });
}

void OpnaChip::advanceNativeFrames(int n) {
  if (n <= 0)
    n = impl_->fmSamplesPerOutput + 2;  // clear the FM-clock cadence with margin
  ymfm::ym2608::output_data out;
  for (int i = 0; i < n; ++i) {
    impl_->intf.advanceClocks(impl_->clocksPerSample);  // keep timers in step
    impl_->chip.generate(&out, 1);  // discard: only advances the chip clock
  }
}

double OpnaChip::nativeSampleRate() const {
  return impl_->nativeRate;
}

unsigned long long OpnaChip::timerExpiryCount(int timer) const {
  return (timer >= 0 && timer < 2)
             ? static_cast<unsigned long long>(impl_->intf.timerExpiries[timer])
             : 0ull;
}

}  // namespace opna
