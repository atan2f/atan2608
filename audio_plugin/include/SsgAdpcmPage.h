#pragma once

#include <array>
#include <vector>

#include "ControlGating.h"
#include "ParamWidgets.h"

namespace audio_plugin {

// Draws the currently-loaded ADPCM-B sample as a min/max waveform in a sunken
// bevelled trough, with a rate/length/count readout. Tinted with the accent.
// Accepts audio files dropped onto it (forwarded via onFileDropped). Shades the
// region outside the start/end playback window and marks the loop, polling the
// smp_start / smp_end / smp_loop parameters so it tracks the controls live.
class WaveformDisplay : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer {
public:
  void setWaveform(const float* data, int numSamples, double sampleRate);
  void setAccent(juce::Colour c) { accent_ = c; repaint(); }
  // Begin polling the playback-window parameters from the tree.
  void connect(juce::AudioProcessorValueTreeState& apvts);
  void paint(juce::Graphics&) override;

  bool isInterestedInFileDrag(const juce::StringArray& files) override;
  void fileDragEnter(const juce::StringArray&, int, int) override;
  void fileDragExit(const juce::StringArray&) override;
  void filesDropped(const juce::StringArray& files, int x, int y) override;

  std::function<void(const juce::File&)> onFileDropped;

private:
  void timerCallback() override;
  std::vector<float> minPeaks_, maxPeaks_;
  double sampleRate_ = 0.0;
  int numSamples_ = 0;
  bool dragOver_ = false;
  juce::Colour accent_ = OpnaColours::cyan;
  juce::AudioProcessorValueTreeState* apvts_ = nullptr;
  int startPermille_ = 0, endPermille_ = 1000;
  bool loop_ = false;
};

// Draws the selected SSG hardware-envelope shape (one of the 8 canonical AY
// shapes), polling ssg_env_shape / ssg_env_on so it tracks the controls.
class SsgEnvDisplay : public juce::Component, private juce::Timer {
public:
  explicit SsgEnvDisplay(juce::AudioProcessorValueTreeState& apvts);
  void setAccent(juce::Colour c) { accent_ = c; repaint(); }
  void paint(juce::Graphics&) override;

private:
  void timerCallback() override;
  juce::AudioProcessorValueTreeState& apvts_;
  int shape_ = -1;
  bool on_ = false;
  juce::Colour accent_ = OpnaColours::cyan;
};

// The non-FM parts: SSG, Rhythm and the ADPCM-B sampler, on one page. Each
// section is a bevelled panel with its own accent colour.
class SsgAdpcmPage : public juce::Component {
public:
  SsgAdpcmPage(juce::AudioProcessorValueTreeState& apvts,
            std::function<void()> onLoadSample,
            std::function<void(const juce::File&)> onDropSample);

  void setSampleWaveform(const float* data, int numSamples, double sampleRate);

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  juce::Rectangle<int> smpBounds_, ssgBounds_, rhyBounds_, ssgPerChanBounds_;

  // Greys controls the chip is currently ignoring (HW ENV vs VOLUME / ENV params,
  // Mix=Tone vs NOISE). See the rule list in the ctor.
  ControlGating gating_;

  // SSG. The top controls are the shared generators + the "pooled" voice; the
  // per-channel A/B/C strip below selects each channel's pooled/independent mode
  // and (when independent) its own mix/volume/HW-env.
  ParamCombo ssgMix_;
  ParamBar ssgVol_, ssgNoise_, ssgEnvPeriod_, ssgEnvShape_;
  ParamToggle ssgEnvOn_;
  SsgEnvDisplay ssgEnv_;
  std::array<juce::Label, 3> ssgChLabel_;
  std::array<ParamCombo, 3> ssgChMode_, ssgChMix_;
  std::array<ParamBar, 3> ssgChVol_;
  std::array<ParamToggle, 3> ssgChEnv_;

  // Rhythm.
  ParamBar rhyTotal_, rhyBd_, rhySd_, rhyTop_, rhyHh_, rhyTom_, rhyRim_;
  std::array<ParamCombo, 6> rhyPan_;  // BD,SD,Top,HH,Tom,Rim

  // Sampler.
  WaveformDisplay wave_;
  juce::TextButton loadButton_{"Load File..."};
  ParamBar smpLevel_, smpRoot_, smpStart_, smpEnd_;
  ParamCombo smpPan_;
  ParamToggle smpLoop_, smpReencode_;
};

}  // namespace audio_plugin
