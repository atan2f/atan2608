#pragma once

#include <array>

namespace opna {

// Modulation graph for each of the 8 OPN FM algorithms: the modulator->carrier
// edges plus the carrier bitmask. Operators are 0-based (op1..op4 = 0..3).
// JUCE-free so both the GUI (algorithm diagram) and the JUCE-free voice code
// (CH3 special polyphony) can share one source of truth.
struct AlgoTopology {
  int edges[6][2];  // modulator -> carrier; a {-1,-1} entry terminates the list
  int carriers;     // bitmask over ops (bit op = carrier)
};

inline constexpr AlgoTopology kAlgoTopology[8] = {
    // 0: 1->2->3->4
    {{{0, 1}, {1, 2}, {2, 3}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1000},
    // 1: (1+2)->3->4
    {{{0, 2}, {1, 2}, {2, 3}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1000},
    // 2: 1->4, 2->3->4
    {{{0, 3}, {1, 2}, {2, 3}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1000},
    // 3: 1->2->4, 3->4
    {{{0, 1}, {1, 3}, {2, 3}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1000},
    // 4: 1->2, 3->4
    {{{0, 1}, {2, 3}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1010},
    // 5: 1->2, 1->3, 1->4
    {{{0, 1}, {0, 2}, {0, 3}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1110},
    // 6: 1->2, 3, 4
    {{{0, 1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1110},
    // 7: all carriers
    {{{-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}, {-1, -1}}, 0b1111},
};

inline int clampAlgo(int algo) { return algo < 0 ? 0 : (algo > 7 ? 7 : algo); }

// Carrier bitmask for an algorithm (bit op = carrier).
inline int algoCarrierMask(int algo) { return kAlgoTopology[clampAlgo(algo)].carriers; }

}  // namespace opna
