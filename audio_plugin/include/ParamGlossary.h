#pragma once

#include <string_view>

namespace audio_plugin {

// Maps a control's terse on-screen label (the register-style abbreviation
// passed to ParamWidget::attach) to a detailed help string: friendly name, a
// one-line description, and the underlying YM2608 register/bit. Surfaced as a
// hover tooltip when the editor's "?" help toggle is on, so the terse labels
// stay put but their meaning is one hover away.
//
// JUCE-free so it lives next to the other shared headers. Returns nullptr when a
// label has no entry, in which case the widget gets no tooltip.
inline const char* paramTooltip(std::string_view label) {
  struct Entry {
    std::string_view key;
    const char* text;
  };
  // Register references are per the YM2608 (OPNA) map; FM operator regs span the
  // four operators of the addressed channel.
  static constexpr Entry table[] = {
      // --- FM operator envelope (per operator) ---
      {"AR",
       "Attack Rate (AR)\n"
       "How fast the operator's envelope rises to peak after key-on.\n"
       "Reg 0x50-0x5F, bits 0-4"},
      {"DR",
       "Decay Rate (DR)\n"
       "First decay: fall from peak down to the sustain level.\n"
       "Reg 0x60-0x6F, bits 0-4"},
      {"SL",
       "Sustain Level (SL)\n"
       "Level where the first decay ends and sustain begins.\n"
       "Reg 0x80-0x8F, bits 4-7"},
      {"SR",
       "Sustain Rate (SR)\n"
       "Second, slower decay while the key is held.\n"
       "Reg 0x70-0x7F, bits 0-4"},
      {"RR",
       "Release Rate (RR)\n"
       "How fast the envelope fades after key-off.\n"
       "Reg 0x80-0x8F, bits 0-3"},
      {"TL",
       "Total Level (TL)\n"
       "Operator output attenuation (0 = loudest, 127 = silent).\n"
       "Reg 0x40-0x4F, bits 0-6"},
      {"MUL",
       "Frequency Multiple (MUL)\n"
       "Operator pitch = note x MUL (0 acts as x0.5). Sets harmonic ratio.\n"
       "Reg 0x30-0x3F, bits 0-3"},
      {"DT",
       "Detune (DT)\n"
       "Fine pitch offset for this operator; drives chorus/beating.\n"
       "Reg 0x30-0x3F, bits 4-6"},
      {"KS",
       "Key Scale / Rate Scaling (KS)\n"
       "Higher notes run their envelopes faster.\n"
       "Reg 0x50-0x5F, bits 6-7"},
      {"SSG-EG",
       "SSG-type Envelope (SSG-EG)\n"
       "Looping/repeating envelope modes borrowed from the SSG.\n"
       "Reg 0x90-0x9F, bits 0-3"},
      {"AM",
       "Amplitude Modulation enable (AM)\n"
       "Routes the LFO to this operator's level (tremolo). Depth set by AMS.\n"
       "Reg 0x60-0x6F, bit 7"},
      // --- FM channel-level ---
      {"FB",
       "Feedback (FB)\n"
       "Operator 1 self-modulation amount; adds brightness/noise.\n"
       "Reg 0xB0-0xB2, bits 3-5"},
      {"AMS",
       "Amplitude Mod Sensitivity (AMS)\n"
       "Tremolo depth from the LFO (needs AM enabled per operator).\n"
       "Reg 0xB4-0xB6, bits 4-5"},
      {"PMS",
       "Pitch Mod Sensitivity (PMS)\n"
       "Vibrato depth from the LFO.\n"
       "Reg 0xB4-0xB6, bits 0-2"},
      // --- FM LFO (chip-global) ---
      {"RATE",
       "LFO Rate\n"
       "Speed of the low-frequency oscillator driving vibrato/tremolo.\n"
       "Reg 0x22, bits 0-2"},
      {"ENABLE",
       "LFO Enable\n"
       "Master switch for the low-frequency oscillator.\n"
       "Reg 0x22, bit 3"},
      // --- SSG (square/noise PSG) ---
      {"Mix",
       "Tone / Noise Mix\n"
       "Selects tone, noise, or both on the SSG channel.\n"
       "Reg 0x07 (mixer)"},
      {"VOLUME",
       "Volume / Amplitude\n"
       "Fixed SSG output level (bit 4 instead hands control to the HW envelope).\n"
       "Reg 0x08-0x0A, bits 0-3"},
      {"NOISE",
       "Noise Period\n"
       "Pitch/colour of the shared noise generator.\n"
       "Reg 0x06, bits 0-4"},
      {"ENV PER",
       "Envelope Period\n"
       "Speed of the SSG hardware envelope.\n"
       "Reg 0x0B-0x0C"},
      {"ENV SHP",
       "Envelope Shape\n"
       "Shape and looping behaviour of the SSG hardware envelope.\n"
       "Reg 0x0D, bits 0-3"},
      {"HW ENV",
       "Hardware Envelope enable\n"
       "Amplitude follows the SSG envelope instead of the fixed volume.\n"
       "Reg 0x08-0x0A, bit 4"},
      {"MODE",
       "Channel Mode (Pooled / Independent)\n"
       "Pooled channels share one voice + routing and form the SSG poly pool. "
       "Independent gives the channel its own mix/volume/HW-env + MIDI routing.\n"
       "(noise colour and envelope period/shape stay shared by the hardware)"},
      // --- ADPCM-A rhythm ---
      {"TOTAL",
       "Rhythm Total Level\n"
       "Master level for all six rhythm drums (higher = louder).\n"
       "Reg 0x11, bits 0-5"},
      {"BASS",
       "Bass Drum Level\n"
       "Per-instrument level for the bass drum (higher = louder).\n"
       "Reg 0x18, bits 0-4"},
      {"SNARE",
       "Snare Drum Level\n"
       "Per-instrument level for the snare.\n"
       "Reg 0x19, bits 0-4"},
      {"CYMBAL",
       "Top Cymbal Level\n"
       "Per-instrument level for the top cymbal.\n"
       "Reg 0x1A, bits 0-4"},
      {"HI-HAT",
       "Hi-Hat Level\n"
       "Per-instrument level for the hi-hat.\n"
       "Reg 0x1B, bits 0-4"},
      {"TOM",
       "Tom-Tom Level\n"
       "Per-instrument level for the tom-tom.\n"
       "Reg 0x1C, bits 0-4"},
      {"RIM",
       "Rim Shot Level\n"
       "Per-instrument level for the rim shot.\n"
       "Reg 0x1D, bits 0-4"},
      // --- ADPCM-B sampler ---
      {"LEVEL",
       "Playback Level\n"
       "Sample output level. ADPCM-B has no envelope, so this is the only volume.\n"
       "Reg 0x10B"},
      {"ROOT",
       "Root Note\n"
       "MIDI note that plays the sample at its recorded pitch; others transpose.\n"
       "(sets ADPCM-B delta-N)"},
      {"Pan",
       "Pan\n"
       "Left/right output routing for this part.\n"
       "Reg 0x101, bits 6-7"},
      {"START",
       "Loop / Play Start\n"
       "Where playback begins, as a fraction of the sample. Bounds playback "
       "only; the sample data in RAM is untouched. ~64-sample resolution.\n"
       "Reg 0x102/0x103"},
      {"END",
       "Loop / Play End\n"
       "Where playback (and the loop) ends, as a fraction of the sample. Bounds "
       "playback only; the sample data in RAM is untouched.\n"
       "Reg 0x104/0x105"},
      {"LOOP",
       "Loop\n"
       "Repeat the start..end window until note-off instead of playing once.\n"
       "Reg 0x100, bit 4"},
      {"RE-ENC",
       "Re-encode Start\n"
       "Re-encode the ADPCM-B data so a non-zero START plays at full level. "
       "ADPCM is differential -- the chip resets its predictor at the start "
       "address, so mid-sample starts otherwise play quiet until the step "
       "re-converges. Off = authentic hardware.\n"
       "(re-encodes RAM data, not a chip register)"},
      // --- Routing (not chip registers) ---
      {"MIDI CH",
       "MIDI Channel\n"
       "Channel this part responds to. 0 = Omni (all channels).\n"
       "(routing, not a chip register)"},
      {"LOW KEY",
       "Low Key\n"
       "Lowest MIDI note this part will play (C3 = middle C).\n"
       "(routing, not a chip register)"},
      {"HI KEY",
       "High Key\n"
       "Highest MIDI note this part will play (C3 = middle C).\n"
       "(routing, not a chip register)"},
      {"OCT",
       "Octave Transpose\n"
       "Shifts this part's notes up/down by whole octaves before they sound. "
       "The key range/split point still uses the untransposed note.\n"
       "(routing, not a chip register)"},
      // --- CH3 special (multi-frequency) mode ---
      {"COARSE",
       "Operator Coarse Pitch (CH3 special)\n"
       "Per-operator pitch offset in semitones. With FOLLOW on it adds to the "
       "played note; with FOLLOW off it tunes a fixed pitch (relative to C4).\n"
       "CH3 multi-freq regs 0xA8-0xAE / 0xA2,0xA6"},
      {"CENTS",
       "Operator Fine Pitch (CH3 special)\n"
       "Per-operator fine tuning in cents (+/-100); detunes the operator.\n"
       "CH3 multi-freq regs 0xA8-0xAE / 0xA2,0xA6"},
      {"FOLLOW",
       "Key Follow (CH3 special)\n"
       "On: the operator tracks the played note. Off: it sits at a fixed pitch "
       "(drone/percussion). Each CH3 operator can differ. In CSM the pitch comes "
       "from Timer A, so this only moves the formant: off anchors a fixed formant "
       "(vowel-like), on transposes it with the note.\n"
       "(affects which frequency is written on key-on)"},
      {"KEY-EN",
       "Operator Key Enable (CH3 special)\n"
       "Whether this operator is keyed on with the note. Lets you silence an "
       "operator without changing the algorithm.\n"
       "Reg 0x28 operator bits (channel 3). Ignored in CSM (all four keyed)."},
      {"CSM",
       "Composite Sine Mode (CH3 special)\n"
       "Timer A auto-retriggers all four operators: the trigger rate IS the "
       "pitch (the note drives Timer A) and the operator frequencies become "
       "formants -- the chip's speech/choir mode. Monophonic. While on, the "
       "per-op COARSE/CENTS/FOLLOW shape formant colour, not pitch; FOLLOW off = "
       "fixed formant, on = formant tracks the note.\n"
       "Reg 0x27 CSM bits + Timer A (0x24/0x25)"},
      {"SOLO",
       "Solo\n"
       "While any part is soloed, only soloed parts sound; a soloed part responds "
       "on all channels (Omni) but keeps its key range.\n"
       "(routing, not a chip register)"},
      // --- Settings panel ---
      {"PRESCALE",
       "Clock Prescale\n"
       "The chip's FM/SSG clock divider (6/3/2). A real period-software control; "
       "lower values raise the internal rate (and the SSG/ADPCM character). The "
       "plugin recompensates pitch so notes stay in tune.\n"
       "Reg 0x2D/0x2E/0x2F"},
      {"LIMITER",
       "Output Limiter\n"
       "A global soft-knee peak limiter on the final output (after Master), "
       "stereo-linked. Off by default; switch on to tame the loud spikes the "
       "chip can produce (notably the SSG with its hardware envelope). Ceiling "
       "~ -0.3 dBFS.\n"
       "(Output-stage DSP, not a chip register.)"},
      {"FIDELITY",
       "Resampler Fidelity\n"
       "Chip oversampling quality vs. CPU: Low/Medium/High. Higher reduces "
       "aliasing on bright FM tones at more CPU cost.\n"
       "(Emulation setting, not a chip register; a per-machine preference.)"},
      {"RHYTHM ROM",
       "Rhythm ROM\n"
       "The 8 KB YM2608 ADPCM-A drum sample ROM. Auto-loaded from "
       "%APPDATA%/atan2608/ym2608_adpcm_rom.bin; load your own dump to override "
       "it. Rhythm is silent if no ROM is found.\n"
       "(External ROM data.)"},
  };

  for (const auto& e : table)
    if (e.key == label)
      return e.text;
  return nullptr;
}

}  // namespace audio_plugin
