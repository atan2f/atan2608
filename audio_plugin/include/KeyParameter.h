#pragma once

#include <memory>

namespace audio_plugin {

// MIDI note number <-> note-name display, using octave 3 for middle C (so
// note 60 reads "C3"). Shared by every LOW KEY / HI KEY routing parameter so
// the on-screen value shows e.g. "C3" instead of a bare "60".
inline juce::String keyToNoteName(int note) {
  return juce::MidiMessage::getMidiNoteName(note, /*useSharps*/ true,
                                            /*includeOctave*/ true,
                                            /*octaveForMiddleC*/ 3);
}

// Parse a note name ("C3", "F#4", "Bb2") back to a MIDI note number, falling
// back to a plain integer so typing a number still works. Returns -1 if the
// text is neither.
inline int noteNameToKey(const juce::String& text) {
  juce::String s = text.trim();
  // Accept the "C3 (60)" display form: a parenthesised number wins outright.
  if (s.contains("(")) {
    const juce::String inner =
        s.fromFirstOccurrenceOf("(", false, false).upToFirstOccurrenceOf(")", false, false).trim();
    if (inner.containsOnly("0123456789") && inner.isNotEmpty())
      return inner.getIntValue();
    s = s.upToFirstOccurrenceOf("(", false, false).trim();
  }
  if (s.isEmpty()) return -1;

  const juce::juce_wchar c = s[0];
  static constexpr int semis[7] = {9, 11, 0, 2, 4, 5, 7};  // A..G
  if ((c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g')) {
    const int letter = juce::CharacterFunctions::toUpperCase(c) - 'A';
    int semitone = semis[letter];
    int i = 1;
    if (i < s.length() && (s[i] == '#' || s[i] == 'b' || s[i] == 'B')) {
      semitone += (s[i] == '#') ? 1 : -1;
      ++i;
    }
    const juce::String octStr = s.substring(i).trim();
    if (octStr.containsOnly("-0123456789") && octStr.isNotEmpty()) {
      const int octave = octStr.getIntValue();
      return semitone + (octave + 2) * 12;  // octave 3 = middle C
    }
  }
  if (s.containsOnly("0123456789")) return s.getIntValue();
  return -1;
}

// Builds a 0..127 MIDI-key routing parameter that displays note names.
inline std::unique_ptr<juce::AudioParameterInt> makeKeyParameter(
    juce::ParameterID id, const juce::String& name, int defaultValue) {
  auto attrs =
      juce::AudioParameterIntAttributes()
          .withStringFromValueFunction([](int v, int) {
            return keyToNoteName(v) + " (" + juce::String(v) + ")";
          })
          .withValueFromStringFunction([](const juce::String& t) {
            const int n = noteNameToKey(t);
            return n < 0 ? 0 : juce::jlimit(0, 127, n);
          });
  return std::make_unique<juce::AudioParameterInt>(id, name, 0, 127,
                                                   defaultValue, attrs);
}

}  // namespace audio_plugin
