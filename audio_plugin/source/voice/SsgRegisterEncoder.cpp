#include "SsgRegisterEncoder.h"

#include <cmath>

namespace opna {

int SsgRegisterEncoder::noteToPeriodBend(double midiNote, double ssgClockHz) {
  const double freq = 440.0 * std::pow(2.0, (midiNote - 69) / 12.0);
  // SSG tone frequency = ssgClock / (16 * period).
  int period = static_cast<int>(ssgClockHz / (16.0 * freq) + 0.5);
  if (period < 1)
    period = 1;
  if (period > 4095)
    period = 4095;
  return period;
}

int SsgRegisterEncoder::noteToPeriod(int midiNote, double ssgClockHz) {
  return noteToPeriodBend(static_cast<double>(midiNote), ssgClockHz);
}

namespace {
bool mixHasTone(int mix) {
  return mix == SsgPatch::kTone || mix == SsgPatch::kToneNoise;
}
bool mixHasNoise(int mix) {
  return mix == SsgPatch::kNoise || mix == SsgPatch::kToneNoise;
}
}  // namespace

void SsgRegisterEncoder::encodePatch(const SsgPatch& patch,
                                     std::vector<RegWrite>& out) {
  const bool tone = mixHasTone(patch.mix);
  const bool noise = mixHasNoise(patch.mix);

  // Mixer 0x07: bits 0-2 disable tone A/B/C, bits 3-5 disable noise A/B/C
  // (active low). IO-port bits 6-7 left as inputs (0).
  int mixer = 0x3F;
  for (int ch = 0; ch < kNumChannels; ++ch) {
    if (tone)
      mixer &= ~(1 << ch);
    if (noise)
      mixer &= ~(1 << (ch + 3));
  }
  out.push_back({0, 0x07, mixer});
  out.push_back({0, 0x06, patch.noisePeriod & 0x1F});
  out.push_back({0, 0x0B, patch.envPeriod & 0xFF});
  out.push_back({0, 0x0C, (patch.envPeriod >> 8) & 0xFF});
  out.push_back({0, 0x0D, patch.envShape & 0x0F});
}

void SsgRegisterEncoder::encodeGlobal(const SsgPatch& patch,
                                      std::vector<RegWrite>& out) {
  out.push_back({0, 0x06, patch.noisePeriod & 0x1F});
  out.push_back({0, 0x0B, patch.envPeriod & 0xFF});
  out.push_back({0, 0x0C, (patch.envPeriod >> 8) & 0xFF});
  out.push_back({0, 0x0D, patch.envShape & 0x0F});
}

int SsgRegisterEncoder::mixerByte(const SsgState& state) {
  int mixer = 0x3F;  // all tone+noise disabled (active low)
  for (int ch = 0; ch < kNumChannels; ++ch) {
    const SsgVoice v = state.effectiveVoice(ch);
    if (mixHasTone(v.mix))
      mixer &= ~(1 << ch);
    if (mixHasNoise(v.mix))
      mixer &= ~(1 << (ch + 3));
  }
  return mixer;
}

void SsgRegisterEncoder::encodeMixer(const SsgState& state,
                                     std::vector<RegWrite>& out) {
  out.push_back({0, 0x07, mixerByte(state)});
}

void SsgRegisterEncoder::encodeToneBend(double midiNote,
                                        int channel,
                                        double ssgClockHz,
                                        std::vector<RegWrite>& out) {
  const int period = noteToPeriodBend(midiNote, ssgClockHz);
  out.push_back({0, channel * 2, period & 0xFF});
  out.push_back({0, channel * 2 + 1, (period >> 8) & 0x0F});
}

void SsgRegisterEncoder::encodeTone(int midiNote,
                                    int channel,
                                    double ssgClockHz,
                                    std::vector<RegWrite>& out) {
  encodeToneBend(static_cast<double>(midiNote), channel, ssgClockHz, out);
}

RegWrite SsgRegisterEncoder::amplitude(int mix, int volume, bool envEnable,
                                       int channel) {
  // kOff silences the channel (the mixer disables it too); otherwise bit 4
  // selects the hardware envelope and bits 0-3 are a fixed level.
  const int value = mix == SsgPatch::kOff ? 0 : (envEnable ? 0x10 : (volume & 0x0F));
  return {0, 0x08 + channel, value};
}

RegWrite SsgRegisterEncoder::noteOnAmplitude(const SsgPatch& patch, int channel) {
  return amplitude(patch.mix, patch.volume, patch.envEnable, channel);
}

RegWrite SsgRegisterEncoder::noteOnAmplitude(const SsgVoice& voice, int channel) {
  return amplitude(voice.mix, voice.volume, voice.envEnable, channel);
}

RegWrite SsgRegisterEncoder::noteOffAmplitude(int channel) {
  return {0, 0x08 + channel, 0x00};
}

RegWrite SsgRegisterEncoder::envelopeShape(const SsgPatch& patch) {
  return {0, 0x0D, patch.envShape & 0x0F};
}

}  // namespace opna
