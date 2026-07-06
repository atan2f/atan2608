#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "OpnaModalBase.h"

namespace audio_plugin {

// A simple confirm / text-prompt modal built on OpnaModalBase: a message and an
// optional single-line text field above the shared button row.
//
// Use the static factories rather than constructing directly:
//   OpnaModal::confirm(...)  -- a yes/no warning (overwrite, delete, init).
//   OpnaModal::prompt(...)   -- a single-line text entry (save preset name).
class OpnaModal : public OpnaModalBase {
public:
  // A yes/no confirmation. `accent` tints the title strip and confirm button.
  // `onCancel` (optional) runs on dismissal without confirming (Cancel / Esc /
  // scrim click) - e.g. to roll back UI a caller optimistically moved forward.
  static void confirm(juce::Component& parent, const juce::String& title,
                      const juce::String& message,
                      const juce::String& confirmLabel, juce::Colour accent,
                      std::function<void()> onConfirm,
                      std::function<void()> onCancel = {});

  // A single-line text prompt. `onConfirm` receives the trimmed entered text
  // (never called on cancel, nor with empty text).
  static void prompt(juce::Component& parent, const juce::String& title,
                     const juce::String& message,
                     const juce::String& initialText,
                     std::function<void(const juce::String&)> onConfirm);

protected:
  int contentHeight() const override;
  void layoutContent(juce::Rectangle<int> area) override;
  void paintContent(juce::Graphics&, juce::Rectangle<int> area) override;
  void onResult(bool confirmed) override;
  void grabInitialFocus() override;

private:
  // confirmed=true on Confirm/Return; text is the trimmed editor contents (empty
  // when there is no editor).
  using Callback = std::function<void(bool confirmed, const juce::String& text)>;

  OpnaModal(const juce::String& title, const juce::String& message,
            const juce::String& confirmLabel, juce::Colour accent,
            bool withEditor, const juce::String& initialText, Callback cb);

  juce::String message_;
  bool withEditor_;
  Callback callback_;
  juce::TextEditor editor_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpnaModal)
};

}  // namespace audio_plugin
