#include <cmath>

#include "KeyParameter.h"
#include "Presets.h"
#include "RhythmParameters.h"
#include "SamplerParameters.h"
#include "SsgParameters.h"

namespace audio_plugin {

namespace {
int asInt(const std::atomic<float>* p) {
  // std::lround handles negative parameter ranges (e.g. CH3SP COARSE/CENTS)
  // correctly; +0.5 truncation would bias them toward zero.
  return p != nullptr ? static_cast<int>(std::lround(p->load())) : 0;
}
bool asBool(const std::atomic<float>* p) {
  return p != nullptr && p->load() >= 0.5f;
}
opna::FmPan asPan(const std::atomic<float>* p) {
  switch (asInt(p)) {
    case 0:  return opna::FmPan::Left;
    case 2:  return opna::FmPan::Right;
    default: return opna::FmPan::Center;
  }
}
}  // namespace

juce::String fmOpParamId(int group, int op, const char* field) {
  return "g" + juce::String(group) + "_op" + juce::String(op) + "_" + field;
}
juce::String fmGroupParamId(int group, const char* name) {
  return "g" + juce::String(group) + "_" + name;
}
juce::String fmChanParamId(int channel, const char* name) {
  return "fmch" + juce::String(channel) + "_" + name;
}
juce::String ch3SpOpParamId(int op, const char* field) {
  return "ch3sp_op" + juce::String(op) + "_" + field;
}
juce::String ch3SpParamId(const char* name) {
  return juce::String("ch3sp_") + name;
}

juce::AudioProcessorValueTreeState::ParameterLayout
createPatchParameterLayout() {
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  using Int = juce::AudioParameterInt;
  using Bool = juce::AudioParameterBool;
  using Choice = juce::AudioParameterChoice;

  // Per-group defaults come from the Init patch so each group's Init is a known-
  // good 2-op voice.
  const opna::FmPatch d = initPatch();

  // Display names are register mnemonics with a "P<part> OP<n>" locator, matching
  // the GUI labels and the param-id tokens (no separate "friendly" names).
  for (int g = 1; g <= opna::kNumFmGroups; ++g) {
    const juce::String pp = "P" + juce::String(g) + " ";

    for (int op = 0; op < 4; ++op) {
      const juce::String loc = pp + "OP" + juce::String(op + 1) + " ";
      auto id = [g, op](const char* f) {
        return juce::ParameterID{fmOpParamId(g, op, f), 1};
      };
      const opna::FmOperator& o = d.op[op];
      layout.add(std::make_unique<Int>(id("dt"), loc + "DT", 0, 7, o.detune));
      layout.add(std::make_unique<Int>(id("mul"), loc + "MUL", 0, 15, o.multiple));
      layout.add(std::make_unique<Int>(id("tl"), loc + "TL", 0, 127, o.totalLevel));
      layout.add(std::make_unique<Int>(id("ks"), loc + "KS", 0, 3, o.keyScale));
      layout.add(std::make_unique<Int>(id("ar"), loc + "AR", 0, 31, o.attackRate));
      layout.add(std::make_unique<Int>(id("dr"), loc + "DR", 0, 31, o.decayRate));
      layout.add(std::make_unique<Int>(id("sr"), loc + "SR", 0, 31, o.sustainRate));
      layout.add(std::make_unique<Int>(id("rr"), loc + "RR", 0, 15, o.releaseRate));
      layout.add(std::make_unique<Int>(id("sl"), loc + "SL", 0, 15, 15 - o.sustainLevel));
      layout.add(std::make_unique<Int>(id("ssgeg"), loc + "SSG-EG", 0, 15, o.ssgEg));
      layout.add(std::make_unique<Bool>(id("am"), loc + "AM", o.amEnable));
    }

    auto gid = [g](const char* n) { return juce::ParameterID{fmGroupParamId(g, n), 1}; };
    layout.add(std::make_unique<Int>(gid("algorithm"), pp + "ALG", 0, 7, d.algorithm));
    layout.add(std::make_unique<Int>(gid("feedback"), pp + "FB", 0, 7, d.feedback));
    layout.add(std::make_unique<Int>(gid("ams"), pp + "AMS", 0, 3, d.ams));
    layout.add(std::make_unique<Int>(gid("pms"), pp + "PMS", 0, 7, d.pms));
    // Per-part MIDI routing (0 = Omni). Default each FM part to its matching
    // MIDI channel for tracker-style multitimbral use out of the box.
    layout.add(std::make_unique<Int>(gid("ch"), pp + "MIDI CH", 0, 16, g));
    layout.add(makeKeyParameter(gid("lo"), pp + "LOW KEY", 0));
    layout.add(makeKeyParameter(gid("hi"), pp + "HI KEY", 127));
    layout.add(std::make_unique<Int>(gid("oct"), pp + "OCT", -4, 4, 0));
    // Solo: while any part is soloed only soloed parts sound (transient/audition
    // control; defaults off so Init and presets clear it).
    layout.add(std::make_unique<Bool>(gid("solo"), pp + "SOLO", false));
  }

  // Per-channel assignment: part (0 = off) and pan. Default: every channel in
  // part 1, centred -> a single 6-voice single-timbre poly.
  for (int c = 1; c <= opna::kNumFmChannels; ++c) {
    const juce::String cp = "CH" + juce::String(c) + " ";
    layout.add(std::make_unique<Int>(
        juce::ParameterID{fmChanParamId(c, "group"), 1}, cp + "PART",
        0, opna::kNumFmGroups, 1));
    layout.add(std::make_unique<Choice>(
        juce::ParameterID{fmChanParamId(c, "pan"), 1}, cp + "PAN",
        juce::StringArray{"L", "C", "R"}, 1));
  }

  // CH3 special (multi-frequency) pseudo-part. When enabled it takes over all of
  // physical channel 3 and gives each operator an independent pitch. It owns the
  // full CH3 patch (timbre + algorithm/feedback/AMS/PMS/pan) plus the per-op
  // pitch controls that normal parts have no use for.
  {
    const juce::String sp = "CH3SP ";
    for (int op = 0; op < 4; ++op) {
      const juce::String loc = sp + "OP" + juce::String(op + 1) + " ";
      auto id = [op](const char* f) {
        return juce::ParameterID{ch3SpOpParamId(op, f), 1};
      };
      const opna::FmOperator& o = d.op[op];
      layout.add(std::make_unique<Int>(id("dt"), loc + "DT", 0, 7, o.detune));
      layout.add(std::make_unique<Int>(id("mul"), loc + "MUL", 0, 15, o.multiple));
      layout.add(std::make_unique<Int>(id("tl"), loc + "TL", 0, 127, o.totalLevel));
      layout.add(std::make_unique<Int>(id("ks"), loc + "KS", 0, 3, o.keyScale));
      layout.add(std::make_unique<Int>(id("ar"), loc + "AR", 0, 31, o.attackRate));
      layout.add(std::make_unique<Int>(id("dr"), loc + "DR", 0, 31, o.decayRate));
      layout.add(std::make_unique<Int>(id("sr"), loc + "SR", 0, 31, o.sustainRate));
      layout.add(std::make_unique<Int>(id("rr"), loc + "RR", 0, 15, o.releaseRate));
      layout.add(std::make_unique<Int>(id("sl"), loc + "SL", 0, 15, 15 - o.sustainLevel));
      layout.add(std::make_unique<Int>(id("ssgeg"), loc + "SSG-EG", 0, 15, o.ssgEg));
      layout.add(std::make_unique<Bool>(id("am"), loc + "AM", o.amEnable));
      // Per-operator independent pitch (the point of special mode).
      layout.add(std::make_unique<Int>(id("coarse"), loc + "COARSE", -48, 48, 0));
      layout.add(std::make_unique<Int>(id("cents"), loc + "CENTS", -100, 100, 0));
      layout.add(std::make_unique<Bool>(id("follow"), loc + "FOLLOW", true));
      layout.add(std::make_unique<Bool>(id("ken"), loc + "KEY-EN", true));
    }
    auto spid = [](const char* n) { return juce::ParameterID{ch3SpParamId(n), 1}; };
    layout.add(std::make_unique<Bool>(spid("enable"), "CH3SP ENABLE", false));
    // CSM: Timer A auto-retriggers the four operators -- the trigger rate is the
    // pitch and the operator frequencies become formants (speech/choir mode).
    layout.add(std::make_unique<Bool>(spid("csm"), sp + "CSM", false));
    layout.add(std::make_unique<Int>(spid("algorithm"), sp + "ALG", 0, 7, d.algorithm));
    layout.add(std::make_unique<Int>(spid("feedback"), sp + "FB", 0, 7, d.feedback));
    layout.add(std::make_unique<Int>(spid("ams"), sp + "AMS", 0, 3, d.ams));
    layout.add(std::make_unique<Int>(spid("pms"), sp + "PMS", 0, 7, d.pms));
    layout.add(std::make_unique<Choice>(spid("pan"), sp + "PAN",
                                        juce::StringArray{"L", "C", "R"}, 1));
    layout.add(std::make_unique<Int>(spid("ch"), sp + "MIDI CH", 0, 16, 7));
    layout.add(makeKeyParameter(spid("lo"), sp + "LOW KEY", 0));
    layout.add(makeKeyParameter(spid("hi"), sp + "HI KEY", 127));
    layout.add(std::make_unique<Int>(spid("oct"), sp + "OCT", -4, 4, 0));
    layout.add(std::make_unique<Bool>(spid("solo"), sp + "SOLO", false));
  }

  // The chip's single global LFO.
  layout.add(std::make_unique<Int>(juce::ParameterID{"fm_lfo_rate", 1}, "LFO RATE", 0, 7, 0));
  layout.add(std::make_unique<Bool>(juce::ParameterID{"fm_lfo_on", 1}, "LFO ENABLE", false));

  // Chip prescale (Settings menu). Retunes the chip; the note encoders
  // compensate (ChipRates) so notes stay in tune. Default (6) reproduces every
  // prior build exactly. (The master clock is fixed at the PC-9801-86 crystal:
  // pitch compensation made a selectable clock audibly inert.)
  layout.add(std::make_unique<Choice>(
      juce::ParameterID{"chip_prescale", 1}, "PRESCALE",
      juce::StringArray{"6", "3", "2"}, 0));

  // Processor-level output gain (not part of the chip patch). The chip's native
  // single-carrier peak is ~-18 dBFS, so the default boosts to a usable level.
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{"master", 1}, "OUTPUT",
      juce::NormalisableRange<float>(0.0f, 8.0f, 0.001f), 4.0f));

  // Global output-stage soft-knee peak limiter (Settings menu). Off by default;
  // tames the loud spikes the chip can produce (e.g. SSG hardware envelope).
  layout.add(std::make_unique<Bool>(juce::ParameterID{"limiter", 1}, "LIMITER",
                                    false));

  addSsgParameters(layout);
  addRhythmParameters(layout);
  addSamplerParameters(layout);
  addRoutingParameters(layout);

  return layout;
}

void PatchParameters::connect(juce::AudioProcessorValueTreeState& apvts) {
  auto raw = [&apvts](const juce::String& id) {
    return apvts.getRawParameterValue(id);
  };
  for (int g = 0; g < opna::kNumFmGroups; ++g) {
    const int gid = g + 1;
    GroupPtrs& gp = groups_[(size_t)g];
    for (int op = 0; op < 4; ++op) {
      OpPtrs& o = gp.op[(size_t)op];
      o.detune = raw(fmOpParamId(gid, op, "dt"));
      o.multiple = raw(fmOpParamId(gid, op, "mul"));
      o.totalLevel = raw(fmOpParamId(gid, op, "tl"));
      o.keyScale = raw(fmOpParamId(gid, op, "ks"));
      o.attackRate = raw(fmOpParamId(gid, op, "ar"));
      o.decayRate = raw(fmOpParamId(gid, op, "dr"));
      o.sustainRate = raw(fmOpParamId(gid, op, "sr"));
      o.releaseRate = raw(fmOpParamId(gid, op, "rr"));
      o.sustainLevel = raw(fmOpParamId(gid, op, "sl"));
      o.ssgEg = raw(fmOpParamId(gid, op, "ssgeg"));
      o.amEnable = raw(fmOpParamId(gid, op, "am"));
    }
    gp.algorithm = raw(fmGroupParamId(gid, "algorithm"));
    gp.feedback = raw(fmGroupParamId(gid, "feedback"));
    gp.ams = raw(fmGroupParamId(gid, "ams"));
    gp.pms = raw(fmGroupParamId(gid, "pms"));
    gp.midiChannel = raw(fmGroupParamId(gid, "ch"));
    gp.lowKey = raw(fmGroupParamId(gid, "lo"));
    gp.highKey = raw(fmGroupParamId(gid, "hi"));
    gp.octave = raw(fmGroupParamId(gid, "oct"));
    gp.solo = raw(fmGroupParamId(gid, "solo"));
  }
  for (int c = 0; c < opna::kNumFmChannels; ++c) {
    channels_[(size_t)c].group = raw(fmChanParamId(c + 1, "group"));
    channels_[(size_t)c].pan = raw(fmChanParamId(c + 1, "pan"));
  }

  for (int op = 0; op < 4; ++op) {
    OpPtrs& o = ch3sp_.op[(size_t)op];
    o.detune = raw(ch3SpOpParamId(op, "dt"));
    o.multiple = raw(ch3SpOpParamId(op, "mul"));
    o.totalLevel = raw(ch3SpOpParamId(op, "tl"));
    o.keyScale = raw(ch3SpOpParamId(op, "ks"));
    o.attackRate = raw(ch3SpOpParamId(op, "ar"));
    o.decayRate = raw(ch3SpOpParamId(op, "dr"));
    o.sustainRate = raw(ch3SpOpParamId(op, "sr"));
    o.releaseRate = raw(ch3SpOpParamId(op, "rr"));
    o.sustainLevel = raw(ch3SpOpParamId(op, "sl"));
    o.ssgEg = raw(ch3SpOpParamId(op, "ssgeg"));
    o.amEnable = raw(ch3SpOpParamId(op, "am"));
    Ch3OpPitchPtrs& pp = ch3sp_.pitch[(size_t)op];
    pp.coarse = raw(ch3SpOpParamId(op, "coarse"));
    pp.cents = raw(ch3SpOpParamId(op, "cents"));
    pp.keyFollow = raw(ch3SpOpParamId(op, "follow"));
    pp.keyEnable = raw(ch3SpOpParamId(op, "ken"));
  }
  ch3sp_.enable = raw(ch3SpParamId("enable"));
  ch3sp_.csm = raw(ch3SpParamId("csm"));
  ch3sp_.algorithm = raw(ch3SpParamId("algorithm"));
  ch3sp_.feedback = raw(ch3SpParamId("feedback"));
  ch3sp_.ams = raw(ch3SpParamId("ams"));
  ch3sp_.pms = raw(ch3SpParamId("pms"));
  ch3sp_.pan = raw(ch3SpParamId("pan"));
  ch3sp_.midiChannel = raw(ch3SpParamId("ch"));
  ch3sp_.lowKey = raw(ch3SpParamId("lo"));
  ch3sp_.highKey = raw(ch3SpParamId("hi"));
  ch3sp_.octave = raw(ch3SpParamId("oct"));
  ch3sp_.solo = raw(ch3SpParamId("solo"));

  lfoRate_ = raw("fm_lfo_rate");
  lfoEnable_ = raw("fm_lfo_on");
  chipPrescale_ = raw("chip_prescale");
}

opna::FmPatch PatchParameters::build(int group) const {
  opna::FmPatch patch;
  if (group < 1 || group > opna::kNumFmGroups)
    return patch;
  const GroupPtrs& gp = groups_[(size_t)(group - 1)];
  for (int op = 0; op < 4; ++op) {
    const OpPtrs& src = gp.op[(size_t)op];
    opna::FmOperator& o = patch.op[op];
    o.detune = asInt(src.detune);
    o.multiple = asInt(src.multiple);
    o.totalLevel = asInt(src.totalLevel);
    o.keyScale = asInt(src.keyScale);
    o.attackRate = asInt(src.attackRate);
    o.decayRate = asInt(src.decayRate);
    o.sustainRate = asInt(src.sustainRate);
    o.releaseRate = asInt(src.releaseRate);
    o.sustainLevel = 15 - asInt(src.sustainLevel);
    o.ssgEg = asInt(src.ssgEg);
    o.amEnable = asBool(src.amEnable);
  }
  patch.algorithm = asInt(gp.algorithm);
  patch.feedback = asInt(gp.feedback);
  patch.ams = asInt(gp.ams);
  patch.pms = asInt(gp.pms);
  return patch;
}

opna::PartRouting PatchParameters::routing(int group) const {
  if (group < 1 || group > opna::kNumFmGroups)
    return {};
  const GroupPtrs& gp = groups_[(size_t)(group - 1)];
  return {asInt(gp.midiChannel), asInt(gp.lowKey), asInt(gp.highKey),
          asInt(gp.octave)};
}

bool PatchParameters::solo(int group) const {
  if (group < 1 || group > opna::kNumFmGroups)
    return false;
  return asBool(groups_[(size_t)(group - 1)].solo);
}

opna::FmChannelConfig PatchParameters::channelConfig(int channel) const {
  if (channel < 1 || channel > opna::kNumFmChannels)
    return {0, opna::FmPan::Center};
  const ChanPtrs& cp = channels_[(size_t)(channel - 1)];
  return {asInt(cp.group), asPan(cp.pan)};
}

opna::FmGlobal PatchParameters::global() const {
  return {asInt(lfoRate_), asBool(lfoEnable_)};
}

opna::ChipRates PatchParameters::chipRates() const {
  opna::ChipRates r;  // clockHz defaults to the PC-9801-86 crystal (fixed).
  // chip_prescale choice 0/1/2 -> prescale 6/3/2.
  switch (asInt(chipPrescale_)) {
    case 1:  r.prescale = 3; break;
    case 2:  r.prescale = 2; break;
    default: r.prescale = 6; break;
  }
  return r;
}

opna::Ch3SpecialState PatchParameters::buildCh3Special() const {
  opna::Ch3SpecialState s;
  s.enabled = asBool(ch3sp_.enable);
  s.solo = asBool(ch3sp_.solo);
  s.mode = asBool(ch3sp_.csm) ? 2 : 1;  // CSM vs multi-frequency
  for (int op = 0; op < 4; ++op) {
    const OpPtrs& src = ch3sp_.op[(size_t)op];
    opna::FmOperator& o = s.patch.op[op];
    o.detune = asInt(src.detune);
    o.multiple = asInt(src.multiple);
    o.totalLevel = asInt(src.totalLevel);
    o.keyScale = asInt(src.keyScale);
    o.attackRate = asInt(src.attackRate);
    o.decayRate = asInt(src.decayRate);
    o.sustainRate = asInt(src.sustainRate);
    o.releaseRate = asInt(src.releaseRate);
    o.sustainLevel = 15 - asInt(src.sustainLevel);
    o.ssgEg = asInt(src.ssgEg);
    o.amEnable = asBool(src.amEnable);

    const Ch3OpPitchPtrs& pp = ch3sp_.pitch[(size_t)op];
    opna::Ch3OpPitch& dst = s.op[(size_t)op];
    dst.coarse = asInt(pp.coarse);
    dst.cents = asInt(pp.cents);
    dst.keyFollow = asBool(pp.keyFollow);
    dst.keyEnable = asBool(pp.keyEnable);
  }
  s.patch.algorithm = asInt(ch3sp_.algorithm);
  s.patch.feedback = asInt(ch3sp_.feedback);
  s.patch.ams = asInt(ch3sp_.ams);
  s.patch.pms = asInt(ch3sp_.pms);
  s.pan = asPan(ch3sp_.pan);
  s.routing = {asInt(ch3sp_.midiChannel), asInt(ch3sp_.lowKey),
               asInt(ch3sp_.highKey), asInt(ch3sp_.octave)};
  return s;
}

}  // namespace audio_plugin
