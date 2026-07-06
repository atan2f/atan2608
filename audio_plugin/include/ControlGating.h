#pragma once

#include <functional>
#include <vector>

#include <juce_events/juce_events.h>

namespace audio_plugin {

// A central registry for "control X greys out when condition Y holds" rules.
//
// Each rule pairs a condition (read live from parameter atomics) with one or more
// dim-target callbacks (typically ParamBar::setDimmed). The registry polls on a
// timer and applies a target only when its condition's result changes, so the
// scattered per-page polling timers collapse into one uniform mechanism. Greying
// is purely cosmetic -- values stay settable, they just signal "the chip is
// ignoring this right now".
class ControlGating : private juce::Timer {
public:
  using DimFn = std::function<void(bool)>;  // e.g. [&]{ bar.setDimmed(d); }

  explicit ControlGating(int hz = 30) { startTimerHz(hz); }

  // shouldDim() returns true when every target should be greyed.
  void addRule(std::function<bool()> shouldDim, std::vector<DimFn> targets) {
    rules_.push_back({std::move(shouldDim), std::move(targets), -1});
  }

  // Re-evaluate every rule; apply a target only on a change unless forceApply is
  // set (used after re-targeting widgets so the dim state snaps immediately).
  void evaluate(bool forceApply = false) {
    for (auto& r : rules_) {
      const int now = r.shouldDim() ? 1 : 0;
      if (now != r.last || forceApply) {
        r.last = now;
        for (auto& a : r.apply)
          a(now != 0);
      }
    }
  }

private:
  void timerCallback() override { evaluate(false); }

  struct Rule {
    std::function<bool()> shouldDim;
    std::vector<DimFn> apply;
    int last;  // -1 = never applied
  };
  std::vector<Rule> rules_;
};

}  // namespace audio_plugin
