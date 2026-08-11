#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "OpnaComboBox.h"
#include "OpnaModalBase.h"

namespace audio_plugin {

// Modal shown when importing an audio file into the ADPCM-B sampler. The two
// settings here are baked into the encode (not live hardware registers), so they
// are chosen once per import rather than living on the panel: the storage rate
// (fidelity vs. pitch-range/length) and, for files too long to fit the chip RAM
// at that rate, where the imported window starts. Built on OpnaModalBase for the
// shared chrome and lifecycle.
class SampleImportDialog : public OpnaModalBase {
public:
  // Launch over `parent`. onImport(rateIndex, offsetPermille) runs on confirm
  // only. fileSeconds is the source file's duration.
  static void show(juce::Component& parent, const juce::String& fileName,
                   double fileSeconds, int initialRateIndex, int initialOffset,
                   std::function<void(int, int)> onImport);

protected:
  int contentHeight() const override;
  void layoutContent(juce::Rectangle<int> area) override;
  void paintContent(juce::Graphics&, juce::Rectangle<int> area) override;
  void onResult(bool confirmed) override;

private:
  SampleImportDialog(const juce::String& fileName, double fileSeconds,
                     int initialRateIndex, int initialOffset,
                     std::function<void(int, int)> onImport);
  void updateReadout();  // refresh the fits / window-position line

  juce::String fileName_;
  double fileSeconds_ = 0.0;
  std::function<void(int, int)> onImport_;

  juce::Label rateLabel_, offsetLabel_, readout_;
  OpnaComboBox rateBox_;
  juce::Slider offsetSlider_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleImportDialog)
};

}  // namespace audio_plugin
