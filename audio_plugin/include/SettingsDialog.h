#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "OpnaComboBox.h"
#include "OpnaModalBase.h"

namespace audio_plugin {

// The "gear" Settings panel: resampler fidelity, a user-supplied rhythm ROM, and
// a reset-window-size action - the machine-local preferences that don't belong on
// a part/patch tab. Everything here is a per-machine preference applied via
// callbacks, not plugin state (so it never travels inside a preset). The build's
// git commit + version is drawn in the panel's bottom corner.
//
// Everything applies live, so the only button is "Done" (Esc also closes); built
// on OpnaModalBase for the shared chrome and lifecycle.
class SettingsDialog : public OpnaModalBase {
public:
  // initialFidelity: 0=Low 1=Medium 2=High. romStatus: a short description of the
  // current rhythm ROM. The callbacks run live (not just on confirm):
  //   onFidelity(index)   apply + persist the fidelity choice
  //   onLoadRom()         pick + load a user ROM (panel stays open; async)
  //   onUseDefaultRom()   revert to the default-location ROM (synchronous)
  //   onResetSize()       reset the editor to its default size
  // Returns the panel (a non-blocking overlay, self-deleting on close) so the
  // caller can keep a SafePointer to refresh the ROM status after a load.
  static SettingsDialog* show(juce::Component& parent, int initialFidelity,
                              const juce::String& romStatus,
                              std::function<void(int)> onFidelity,
                              std::function<void()> onLoadRom,
                              std::function<void()> onUseDefaultRom,
                              std::function<void()> onResetSize);

  // Update the displayed rhythm-ROM status (after a load/revert completes).
  void setRomStatus(const juce::String& status);

protected:
  int contentHeight() const override;
  void layoutContent(juce::Rectangle<int> area) override;
  void paintContent(juce::Graphics&, juce::Rectangle<int> area) override;
  void onResult(bool confirmed) override;

private:
  SettingsDialog(int initialFidelity, const juce::String& romStatus,
                 std::function<void(int)> onFidelity,
                 std::function<void()> onLoadRom,
                 std::function<void()> onUseDefaultRom,
                 std::function<void()> onResetSize);

  std::function<void()> onLoadRom_;

  juce::Label fidelityLabel_, romLabel_, romStatus_;
  OpnaComboBox fidelityBox_;
  juce::TextButton loadRomButton_{"Load ROM..."};
  juce::TextButton builtInRomButton_{"Use Default"};
  juce::TextButton resetSizeButton_{"Reset Window Size"};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsDialog)
};

}  // namespace audio_plugin
