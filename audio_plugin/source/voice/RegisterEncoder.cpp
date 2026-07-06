#include "RegisterEncoder.h"

#include <cmath>

namespace opna {

constexpr int RegisterEncoder::kSlotOffset[4];

namespace {

// Channel -> hardware port (0 for ch 0-2, 1 for ch 3-5).
inline int portOf(int channel) {
  return channel / 3;
}
// Channel index within its group of three (0,1,2).
inline int idxOf(int channel) {
  return channel % 3;
}
// Channel code used by the key-on/off register: 0,1,2,4,5,6.
inline int keyCode(int channel) {
  return idxOf(channel) | ((channel / 3) << 2);
}

}  // namespace

void RegisterEncoder::encodePatch(const FmPatch& patch,
                                  int channel,
                                  std::vector<RegWrite>& out,
                                  FmPan pan) {
  const int port = portOf(channel);
  const int idx = idxOf(channel);

  for (int op = 0; op < 4; ++op) {
    const FmOperator& o = patch.op[op];
    const int slot = kSlotOffset[op] + idx;

    out.push_back({port, 0x30 + slot, ((o.detune & 0x07) << 4) | (o.multiple & 0x0F)});
    out.push_back({port, 0x40 + slot, o.totalLevel & 0x7F});
    out.push_back({port, 0x50 + slot, ((o.keyScale & 0x03) << 6) | (o.attackRate & 0x1F)});
    out.push_back({port, 0x60 + slot, (o.amEnable ? 0x80 : 0x00) | (o.decayRate & 0x1F)});
    out.push_back({port, 0x70 + slot, o.sustainRate & 0x1F});
    out.push_back({port, 0x80 + slot, ((o.sustainLevel & 0x0F) << 4) | (o.releaseRate & 0x0F)});
    out.push_back({port, 0x90 + slot, o.ssgEg & 0x0F});
  }

  out.push_back({port, 0xB0 + idx, ((patch.feedback & 0x07) << 3) | (patch.algorithm & 0x07)});
  out.push_back({port, 0xB4 + idx,
                 panBits(pan) | ((patch.ams & 0x03) << 4) | (patch.pms & 0x07)});
}

RegWrite RegisterEncoder::encodeLfo(const FmGlobal& global) {
  return {0, 0x22, (global.lfoEnable ? 0x08 : 0x00) | (global.lfoRate & 0x07)};
}

void RegisterEncoder::noteToFnum(double midiNote,
                                 double clockHz,
                                 int& block,
                                 int& fnum) {
  const double freq = 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);

  // realised freq = fnum * (fM/144) / 2^(20-block); solve for fnum, choosing
  // block so the F-number lands in [1024, 2047].
  double value = 144.0 * freq * 1048576.0 / clockHz;
  int b = 1;
  while (value >= 2048.0 && b < 7) {
    value *= 0.5;
    ++b;
  }
  while (value < 1024.0 && b > 0) {
    value *= 2.0;
    --b;
  }
  int f = static_cast<int>(value + 0.5);
  if (f < 0)
    f = 0;
  if (f > 2047)
    f = 2047;
  block = b;
  fnum = f;
}

void RegisterEncoder::encodeFrequencyBend(double midiNote,
                                          int channel,
                                          double clockHz,
                                          std::vector<RegWrite>& out) {
  int block = 0, fnum = 0;
  noteToFnum(midiNote, clockHz, block, fnum);

  const int port = portOf(channel);
  const int idx = idxOf(channel);
  out.push_back({port, 0xA4 + idx, ((block & 0x07) << 3) | ((fnum >> 8) & 0x07)});
  out.push_back({port, 0xA0 + idx, fnum & 0xFF});
}

void RegisterEncoder::encodeFrequency(int midiNote,
                                      int channel,
                                      double clockHz,
                                      std::vector<RegWrite>& out) {
  encodeFrequencyBend(static_cast<double>(midiNote), channel, clockHz, out);
}

RegWrite RegisterEncoder::keyOn(int channel) {
  return {0, 0x28, 0xF0 | keyCode(channel)};
}

RegWrite RegisterEncoder::keyOff(int channel) {
  return {0, 0x28, keyCode(channel)};
}

}  // namespace opna
