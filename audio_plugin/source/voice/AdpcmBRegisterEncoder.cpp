#include "AdpcmBRegisterEncoder.h"

#include <cmath>

namespace opna {

int AdpcmBRegisterEncoder::rootDeltaN(double sampleRateHz, double adpcmClockHz) {
  // Fs = adpcmClockHz * deltaN / 65536  =>  deltaN = 65536 * Fs / adpcmClockHz
  double dn = 65536.0 * sampleRateHz / adpcmClockHz + 0.5;
  if (dn < 1.0)
    dn = 1.0;
  if (dn > 65535.0)
    dn = 65535.0;
  return static_cast<int>(dn);
}

int AdpcmBRegisterEncoder::noteDeltaNBend(double midiNote, int rootNote, int root) {
  double dn = root * std::pow(2.0, (midiNote - rootNote) / 12.0) + 0.5;
  if (dn < 1.0)
    dn = 1.0;
  if (dn > 65535.0)
    dn = 65535.0;
  return static_cast<int>(dn);
}

int AdpcmBRegisterEncoder::noteDeltaN(int midiNote, int rootNote, int root) {
  return noteDeltaNBend(static_cast<double>(midiNote), rootNote, root);
}

int AdpcmBRegisterEncoder::control2(int pan) {
  // bit7 = L, bit6 = R (both = centre); bit1 = 8-bit external DRAM (shift 5).
  const int panBits = pan == 0 ? 0x80 : pan == 2 ? 0x40 : 0xC0;
  return panBits | 0x02;
}

int AdpcmBRegisterEncoder::endUnitForBytes(int dataBytes) {
  return dataBytes > 0 ? (dataBytes - 1) / kAddressUnit : 0;
}

int AdpcmBRegisterEncoder::unitForPermille(int dataBytes, int permille) {
  const int maxUnit = endUnitForBytes(dataBytes);
  if (permille < 0)
    permille = 0;
  if (permille > 1000)
    permille = 1000;
  return static_cast<int>(static_cast<long long>(permille) * maxUnit / 1000);
}

void AdpcmBRegisterEncoder::setRegion(int startUnit,
                                      int endUnit,
                                      std::vector<RegWrite>& out) {
  out.push_back({1, 0x02, startUnit & 0xFF});
  out.push_back({1, 0x03, (startUnit >> 8) & 0xFF});
  out.push_back({1, 0x04, endUnit & 0xFF});
  out.push_back({1, 0x05, (endUnit >> 8) & 0xFF});
}

void AdpcmBRegisterEncoder::configure(int dataBytes,
                                      int level,
                                      int pan,
                                      std::vector<RegWrite>& out) {
  const int endUnit = endUnitForBytes(dataBytes);

  out.push_back({1, 0x00, 0x00});  // stop / clear control
  // Control 2: pan + 8-bit external DRAM (address shift 5), not ROM.
  out.push_back({1, 0x01, control2(pan)});
  out.push_back({1, 0x02, 0x00});  // start address (units of 32 bytes) = 0
  out.push_back({1, 0x03, 0x00});
  out.push_back({1, 0x04, endUnit & 0xFF});  // end address
  out.push_back({1, 0x05, (endUnit >> 8) & 0xFF});
  out.push_back({1, 0x0C, 0xFF});  // limit = max so playback doesn't wrap early
  out.push_back({1, 0x0D, 0xFF});
  out.push_back({1, 0x0B, level & 0xFF});  // level
}

void AdpcmBRegisterEncoder::setDeltaN(int deltaN, std::vector<RegWrite>& out) {
  out.push_back({1, 0x09, deltaN & 0xFF});
  out.push_back({1, 0x0A, (deltaN >> 8) & 0xFF});
}

RegWrite AdpcmBRegisterEncoder::keyOn(bool loop) {
  // execute (bit7) + external memory (bit5) + speaker (bit3); add repeat (bit4)
  // for looping. 0xA8 one-shot, 0xB8 looped.
  return {1, 0x00, loop ? 0xB8 : 0xA8};
}

RegWrite AdpcmBRegisterEncoder::keyOff() {
  return {1, 0x00, 0x01};  // reset
}

}  // namespace opna
