#pragma once

#include <vector>

#include "OpnaChip.h"
#include "RegisterEncoder.h"  // for RegWrite + applyWrite
#include "SsgPatch.h"

namespace opna {

// Translates SSG notes and patch settings to YM2608 SSG registers (0x00-0x0D,
// all on port 0). The SSG has no key-on; a channel sounds whenever its mixer
// bit is enabled and its amplitude is non-zero, so note-off simply zeroes the
// channel amplitude.
class SsgRegisterEncoder {
public:
  static constexpr int kNumChannels = 3;

  // The SSG runs at the chip clock divided by 4 (~1.9968 MHz at 7.9872 MHz).
  static constexpr double kSsgClockHz = OpnaChip::kClockHz / 4.0;

  // 12-bit tone period for a MIDI note (clamped to 1..4095).
  static int noteToPeriod(int midiNote, double ssgClockHz);
  // As noteToPeriod, but takes a fractional MIDI note for pitch bend. The 12-bit
  // period quantises coarsely up high, so a bend there is audibly stepped --
  // authentic to the hardware.
  static int noteToPeriodBend(double midiNote, double ssgClockHz);

  // Part-global registers from a single patch: mixer (0x07, same mix on all
  // three channels), noise period (0x06), envelope period (0x0B/0x0C) and shape
  // (0x0D). Used by the all-channels-identical path and the encoder tests.
  static void encodePatch(const SsgPatch& patch, std::vector<RegWrite>& out);

  // The shared generators only: noise period (0x06), envelope period
  // (0x0B/0x0C) and shape (0x0D). The mixer is per-channel (see encodeMixer).
  static void encodeGlobal(const SsgPatch& patch, std::vector<RegWrite>& out);

  // Mixer byte (0x07) computed per channel from each channel's effective mix.
  // Bits 0-2 disable tone A/B/C, bits 3-5 disable noise A/B/C (active low).
  static int mixerByte(const SsgState& state);
  static void encodeMixer(const SsgState& state, std::vector<RegWrite>& out);

  // Per-note tone period (0x00/0x01 for channel A, etc.).
  static void encodeTone(int midiNote,
                         int channel,
                         double ssgClockHz,
                         std::vector<RegWrite>& out);
  // As encodeTone, but takes a fractional MIDI note so pitch bend can shift the
  // tone period continuously. Writes only the period regs -- no amplitude or
  // envelope re-arm -- so a live bend doesn't restart the channel.
  static void encodeToneBend(double midiNote,
                             int channel,
                             double ssgClockHz,
                             std::vector<RegWrite>& out);

  // Amplitude register (0x08+channel) for note-on / note-off. A channel whose
  // mix is kOff is silenced (the mixer also disables it); otherwise bit 4
  // selects the hardware envelope and bits 0-3 are a fixed level.
  static RegWrite amplitude(int mix, int volume, bool envEnable, int channel);
  static RegWrite noteOnAmplitude(const SsgPatch& patch, int channel);
  static RegWrite noteOnAmplitude(const SsgVoice& voice, int channel);
  static RegWrite noteOffAmplitude(int channel);

  // Re-trigger the hardware envelope by rewriting the shared shape register.
  static RegWrite envelopeShape(const SsgPatch& patch);
};

}  // namespace opna
