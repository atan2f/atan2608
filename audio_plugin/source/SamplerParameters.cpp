#include "SamplerParameters.h"

#include "KeyParameter.h"

namespace audio_plugin {

namespace {
int smpAsInt(const std::atomic<float>* p) {
  return p != nullptr ? static_cast<int>(p->load() + 0.5f) : 0;
}
}  // namespace

void addSamplerParameters(
    juce::AudioProcessorValueTreeState::ParameterLayout& layout) {
  using Int = juce::AudioParameterInt;
  layout.add(std::make_unique<Int>(juce::ParameterID{"smp_level", 1},
                                   "SMP LEVEL", 0, 255, 200));
  layout.add(makeKeyParameter(juce::ParameterID{"smp_root", 1},
                              "SMP ROOT", 60));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"smp_pan", 1}, "SMP PAN",
      juce::StringArray{"L", "C", "R"}, 1));

  juce::StringArray rateItems;
  for (double r : kSamplerRates)
    rateItems.add(juce::String(juce::roundToInt(r)) + " Hz");
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"smp_rate", 1}, "SMP RATE", rateItems,
      kDefaultSamplerRateIndex));

  // Import window position for samples longer than fits in the chip RAM at the
  // chosen rate. Applied at load time (per-mille of the source file).
  layout.add(std::make_unique<Int>(juce::ParameterID{"smp_offset", 1},
                                   "SMP OFFSET", 0, 1000, 0));

  // Playback window + loop. Start/end are per-mille of the sample so they stay
  // meaningful across different sample lengths; they bound playback only.
  layout.add(std::make_unique<Int>(juce::ParameterID{"smp_start", 1},
                                   "SMP START", 0, 1000, 0));
  layout.add(std::make_unique<Int>(juce::ParameterID{"smp_end", 1},
                                   "SMP END", 0, 1000, 1000));
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"smp_loop", 1}, "SMP LOOP", false));

  // Re-encode the ADPCM-B data so a non-zero START sounds at full level. ADPCM is
  // differential: the chip resets its predictor/step at the start address, so
  // mid-sample starts normally play quiet until the step re-converges. When on,
  // the data is re-encoded with a fresh predictor at the start point. Off by
  // default (authentic hardware behaviour).
  layout.add(std::make_unique<juce::AudioParameterBool>(
      juce::ParameterID{"smp_reencode", 1}, "SMP RE-ENC", false));
}

void SamplerParameters::connect(juce::AudioProcessorValueTreeState& apvts) {
  level_ = apvts.getRawParameterValue("smp_level");
  rootNote_ = apvts.getRawParameterValue("smp_root");
  pan_ = apvts.getRawParameterValue("smp_pan");
  start_ = apvts.getRawParameterValue("smp_start");
  end_ = apvts.getRawParameterValue("smp_end");
  loop_ = apvts.getRawParameterValue("smp_loop");
  reencode_ = apvts.getRawParameterValue("smp_reencode");
}

int SamplerParameters::level() const {
  return smpAsInt(level_);
}

int SamplerParameters::rootNote() const {
  return smpAsInt(rootNote_);
}

int SamplerParameters::pan() const {
  return smpAsInt(pan_);
}

int SamplerParameters::startPermille() const {
  return smpAsInt(start_);
}

int SamplerParameters::endPermille() const {
  return smpAsInt(end_);
}

bool SamplerParameters::loop() const {
  return loop_ != nullptr && loop_->load() >= 0.5f;
}

bool SamplerParameters::reencodeStart() const {
  return reencode_ != nullptr && reencode_->load() >= 0.5f;
}

}  // namespace audio_plugin
