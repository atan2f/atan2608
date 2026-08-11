#include "SsgParameters.h"

#include <cmath>

#include "KeyParameter.h"

namespace audio_plugin {

namespace {
int ssgAsInt(const std::atomic<float>* p) {
  // Several parameters using this path have signed ranges (SSG and sampler
  // octave transpose). Adding 0.5 then truncating biases every negative value
  // upward: -1 became 0, -2 became -1, etc.
  return p != nullptr ? static_cast<int>(std::lround(p->load())) : 0;
}
bool ssgAsBool(const std::atomic<float>* p) {
  return p != nullptr && p->load() >= 0.5f;
}
}  // namespace

namespace {
const char* ssgChannelLetter(int channel) {
  static const char* k[] = {"A", "B", "C"};
  return k[channel];
}
const char* ssgChannelTag(int channel) {
  static const char* k[] = {"a", "b", "c"};
  return k[channel];
}
// Per-channel parameter id, e.g. ssgParamId(0, "mix") -> "ssga_mix".
juce::String ssgParamId(int channel, const char* field) {
  return juce::String("ssg") + ssgChannelTag(channel) + "_" + field;
}
}  // namespace

void addSsgParameters(
    juce::AudioProcessorValueTreeState::ParameterLayout& layout) {
  using Int = juce::AudioParameterInt;
  using Bool = juce::AudioParameterBool;
  const opna::SsgPatch d;

  // Mix choices include "Off" (kOff) so an independent channel can be silenced.
  const juce::StringArray mixItems{"Tone", "Noise", "Tone+Noise", "Off"};

  // Pooled voice + shared generators.
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{"ssg_mix", 1}, "SSG MIX", mixItems, d.mix));
  layout.add(std::make_unique<Int>(juce::ParameterID{"ssg_vol", 1}, "SSG VOLUME", 0, 15, d.volume));
  layout.add(std::make_unique<Bool>(juce::ParameterID{"ssg_env_on", 1}, "SSG HW ENV", d.envEnable));
  layout.add(std::make_unique<Int>(juce::ParameterID{"ssg_env_period", 1}, "SSG ENV PER", 0, 65535, d.envPeriod));
  layout.add(std::make_unique<Int>(juce::ParameterID{"ssg_env_shape", 1}, "SSG ENV SHP", 0, 15, d.envShape));
  layout.add(std::make_unique<Int>(juce::ParameterID{"ssg_noise", 1}, "SSG NOISE", 0, 31, d.noisePeriod));

  // Per-channel A/B/C: mode (Pooled/Independent), independent timbre and MIDI
  // routing. All default to Pooled (one shared 3-voice poly part).
  for (int c = 0; c < SsgParameters::kNumChannels; ++c) {
    const juce::String tag = juce::String("SSG ") + ssgChannelLetter(c) + " ";
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ssgParamId(c, "mode"), 1}, tag + "MODE",
        juce::StringArray{"Pooled", "Independent"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ssgParamId(c, "mix"), 1}, tag + "MIX", mixItems, d.mix));
    layout.add(std::make_unique<Int>(juce::ParameterID{ssgParamId(c, "vol"), 1}, tag + "VOLUME", 0, 15, d.volume));
    layout.add(std::make_unique<Bool>(juce::ParameterID{ssgParamId(c, "env"), 1}, tag + "HW ENV", d.envEnable));
    layout.add(std::make_unique<Int>(juce::ParameterID{ssgParamId(c, "ch"), 1}, tag + "MIDI CH", 0, 16, 11 + c));
    layout.add(makeKeyParameter(juce::ParameterID{ssgParamId(c, "lo"), 1}, tag + "LOW KEY", 0));
    layout.add(makeKeyParameter(juce::ParameterID{ssgParamId(c, "hi"), 1}, tag + "HI KEY", 127));
    layout.add(std::make_unique<Int>(juce::ParameterID{ssgParamId(c, "oct"), 1}, tag + "OCT", -4, 4, 0));
    layout.add(std::make_unique<Bool>(juce::ParameterID{ssgParamId(c, "solo"), 1}, tag + "SOLO", false));
  }
}

void addRoutingParameters(
    juce::AudioProcessorValueTreeState::ParameterLayout& layout) {
  using Int = juce::AudioParameterInt;
  // Channel 0 = Omni, 1..16 = specific MIDI channel. FM routing is per-group and
  // is created in PatchParameters.
  using Bool = juce::AudioParameterBool;
  layout.add(std::make_unique<Int>(juce::ParameterID{"ssg_ch", 1}, "SSG MIDI CH", 0, 16, 8));
  layout.add(makeKeyParameter(juce::ParameterID{"ssg_lo", 1}, "SSG LOW KEY", 0));
  layout.add(makeKeyParameter(juce::ParameterID{"ssg_hi", 1}, "SSG HI KEY", 127));
  layout.add(std::make_unique<Int>(juce::ParameterID{"ssg_oct", 1}, "SSG OCT", -4, 4, 0));
  layout.add(std::make_unique<Bool>(juce::ParameterID{"ssg_solo", 1}, "SSG SOLO", false));
  // Rhythm defaults to MIDI channel 10 (General MIDI drums) so it is separate
  // from FM/SSG out of the box.
  layout.add(std::make_unique<Int>(juce::ParameterID{"rhy_ch", 1}, "RHY MIDI CH", 0, 16, 10));
  layout.add(makeKeyParameter(juce::ParameterID{"rhy_lo", 1}, "RHY LOW KEY", 0));
  layout.add(makeKeyParameter(juce::ParameterID{"rhy_hi", 1}, "RHY HI KEY", 127));
  layout.add(std::make_unique<Bool>(juce::ParameterID{"rhy_solo", 1}, "RHY SOLO", false));
  layout.add(std::make_unique<Int>(juce::ParameterID{"smp_ch", 1}, "SMP MIDI CH", 0, 16, 9));
  layout.add(makeKeyParameter(juce::ParameterID{"smp_lo", 1}, "SMP LOW KEY", 0));
  layout.add(makeKeyParameter(juce::ParameterID{"smp_hi", 1}, "SMP HI KEY", 127));
  layout.add(std::make_unique<Int>(juce::ParameterID{"smp_oct", 1}, "SMP OCT", -4, 4, 0));
  layout.add(std::make_unique<Bool>(juce::ParameterID{"smp_solo", 1}, "SMP SOLO", false));
}

void SsgParameters::connect(juce::AudioProcessorValueTreeState& apvts) {
  mix_ = apvts.getRawParameterValue("ssg_mix");
  volume_ = apvts.getRawParameterValue("ssg_vol");
  envEnable_ = apvts.getRawParameterValue("ssg_env_on");
  envPeriod_ = apvts.getRawParameterValue("ssg_env_period");
  envShape_ = apvts.getRawParameterValue("ssg_env_shape");
  noisePeriod_ = apvts.getRawParameterValue("ssg_noise");
  for (int c = 0; c < kNumChannels; ++c) {
    Channel& ch = ch_[(size_t)c];
    ch.mode = apvts.getRawParameterValue(ssgParamId(c, "mode"));
    ch.mix = apvts.getRawParameterValue(ssgParamId(c, "mix"));
    ch.volume = apvts.getRawParameterValue(ssgParamId(c, "vol"));
    ch.envEnable = apvts.getRawParameterValue(ssgParamId(c, "env"));
    ch.channel = apvts.getRawParameterValue(ssgParamId(c, "ch"));
    ch.low = apvts.getRawParameterValue(ssgParamId(c, "lo"));
    ch.high = apvts.getRawParameterValue(ssgParamId(c, "hi"));
    ch.octave = apvts.getRawParameterValue(ssgParamId(c, "oct"));
    ch.solo = apvts.getRawParameterValue(ssgParamId(c, "solo"));
  }
}

opna::SsgPatch SsgParameters::buildPooled() const {
  opna::SsgPatch p;
  p.mix = ssgAsInt(mix_);
  p.volume = ssgAsInt(volume_);
  p.envEnable = ssgAsBool(envEnable_);
  p.envPeriod = ssgAsInt(envPeriod_);
  p.envShape = ssgAsInt(envShape_);
  p.noisePeriod = ssgAsInt(noisePeriod_);
  return p;
}

opna::SsgState SsgParameters::buildState() const {
  opna::SsgState s;
  s.pooled = buildPooled();
  for (int c = 0; c < kNumChannels; ++c) {
    const Channel& ch = ch_[(size_t)c];
    s.mode[(size_t)c] = ssgAsBool(ch.mode) ? opna::SsgChannelMode::Independent
                                           : opna::SsgChannelMode::Pooled;
    s.voice[(size_t)c] = {ssgAsInt(ch.mix), ssgAsInt(ch.volume),
                          ssgAsBool(ch.envEnable)};
  }
  return s;
}

opna::PartRouting SsgParameters::indepRouting(int channel) const {
  const Channel& ch = ch_[(size_t)channel];
  return {ssgAsInt(ch.channel), ssgAsInt(ch.low), ssgAsInt(ch.high),
          ssgAsInt(ch.octave)};
}

bool SsgParameters::indepSolo(int channel) const {
  return ssgAsBool(ch_[(size_t)channel].solo);
}

bool SsgParameters::independent(int channel) const {
  return ssgAsBool(ch_[(size_t)channel].mode);
}

void RoutingParameters::connect(juce::AudioProcessorValueTreeState& apvts) {
  ssgChannel_ = apvts.getRawParameterValue("ssg_ch");
  ssgLow_ = apvts.getRawParameterValue("ssg_lo");
  ssgHigh_ = apvts.getRawParameterValue("ssg_hi");
  ssgOctave_ = apvts.getRawParameterValue("ssg_oct");
  rhythmChannel_ = apvts.getRawParameterValue("rhy_ch");
  rhythmLow_ = apvts.getRawParameterValue("rhy_lo");
  rhythmHigh_ = apvts.getRawParameterValue("rhy_hi");
  samplerChannel_ = apvts.getRawParameterValue("smp_ch");
  samplerLow_ = apvts.getRawParameterValue("smp_lo");
  samplerHigh_ = apvts.getRawParameterValue("smp_hi");
  samplerOctave_ = apvts.getRawParameterValue("smp_oct");
  ssgSolo_ = apvts.getRawParameterValue("ssg_solo");
  rhythmSolo_ = apvts.getRawParameterValue("rhy_solo");
  samplerSolo_ = apvts.getRawParameterValue("smp_solo");
}

opna::PartRouting RoutingParameters::ssg() const {
  return {ssgAsInt(ssgChannel_), ssgAsInt(ssgLow_), ssgAsInt(ssgHigh_),
          ssgAsInt(ssgOctave_)};
}

opna::PartRouting RoutingParameters::rhythm() const {
  return {ssgAsInt(rhythmChannel_), ssgAsInt(rhythmLow_), ssgAsInt(rhythmHigh_)};
}

opna::PartRouting RoutingParameters::sampler() const {
  return {ssgAsInt(samplerChannel_), ssgAsInt(samplerLow_), ssgAsInt(samplerHigh_),
          ssgAsInt(samplerOctave_)};
}

bool RoutingParameters::ssgSolo() const { return ssgAsBool(ssgSolo_); }
bool RoutingParameters::rhythmSolo() const { return ssgAsBool(rhythmSolo_); }
bool RoutingParameters::samplerSolo() const { return ssgAsBool(samplerSolo_); }

}  // namespace audio_plugin
