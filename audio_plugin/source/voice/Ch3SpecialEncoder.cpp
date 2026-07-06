#include "Ch3SpecialEncoder.h"

#include <cmath>

namespace opna {

namespace {
// Logical OP1..OP4 -> multi_block_freq index (0xA8+i / 0xAC+i). OP4 (index 3)
// has no special register; it uses the normal CH3 frequency instead.
constexpr int kCh3SpecialFreqIndex[4] = {1, 2, 0, -1};

// OPNA Timer A overflow period = (1024 - NA) * OPERATORS(24) * prescale(6) master
// clocks, so the divider from clock to overflow frequency is 144 per tick.
constexpr double kTimerADivider = 144.0;
}  // namespace

RegWrite Ch3SpecialEncoder::encodeMode(int mode) {
  return {0, 0x27, (mode & 0x03) << 6};
}

RegWrite Ch3SpecialEncoder::encodeCsmRun(bool run) {
  // CSM mode (bits 6-7 = 0b10 = 0x80) plus, when running, the Timer A load bit
  // (bit 0 = 0x01). The chip auto-reloads the timer on every overflow, so the
  // load bit alone keeps it free-running.
  return {0, 0x27, run ? 0x81 : 0x80};
}

int Ch3SpecialEncoder::noteToTimerA(double midiNote, double clockHz) {
  const double freq = 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
  // ticks = (1024 - NA); overflow frequency = clock / (ticks * 144).
  int ticks = static_cast<int>(clockHz / (kTimerADivider * freq) + 0.5);
  if (ticks < 1)
    ticks = 1;     // highest pitch (NA -> 1023)
  if (ticks > 1024)
    ticks = 1024;  // lowest pitch (NA -> 0, ~54 Hz floor)
  return 1024 - ticks;
}

void Ch3SpecialEncoder::encodeTimerA(int na, std::vector<RegWrite>& out) {
  na &= 0x3FF;
  out.push_back({0, 0x24, (na >> 2) & 0xFF});  // upper 8 bits
  out.push_back({0, 0x25, na & 0x03});         // lower 2 bits
}

void Ch3SpecialEncoder::encodeOperatorFrequency(double midiNote,
                                                int logicalOp,
                                                double clockHz,
                                                std::vector<RegWrite>& out) {
  int block = 0, fnum = 0;
  RegisterEncoder::noteToFnum(midiNote, clockHz, block, fnum);
  const int high = ((block & 0x07) << 3) | ((fnum >> 8) & 0x07);
  const int low = fnum & 0xFF;

  const int idx = (logicalOp >= 0 && logicalOp < 4) ? kCh3SpecialFreqIndex[logicalOp] : -1;
  if (idx < 0) {
    // OP4: normal CH3 frequency registers (channel index 2). High before low.
    out.push_back({0, 0xA6, high});
    out.push_back({0, 0xA2, low});
    return;
  }
  out.push_back({0, 0xAC + idx, high});
  out.push_back({0, 0xA8 + idx, low});
}

RegWrite Ch3SpecialEncoder::keyMask(int opsOn) {
  return {0, 0x28, ((opsOn & 0x0F) << 4) | kCh3KeyCode};
}

}  // namespace opna
