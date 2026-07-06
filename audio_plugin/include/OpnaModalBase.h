#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "OpnaLookAndFeel.h"

namespace audio_plugin {

// Shared base for the editor's modal dialogs. Owns the common chrome and
// lifecycle so every modal looks and behaves the same: a dimmed scrim, a
// centered bevelled panel with a title strip, a Confirm/Cancel button row, and
// Esc/Return handling. It enters the modal state on launch and self-deletes on
// dismissal.
//
// Subclasses supply only their middle content: contentHeight() reserves the
// space between the title and the buttons, layoutContent() places child
// components in it, paintContent() draws into it, and onResult() runs once when
// the dialog is dismissed (after the modal state has been exited, so it may
// safely spawn a follow-up modal).
class OpnaModalBase : public juce::Component,
                      private juce::ComponentListener {
public:
  ~OpnaModalBase() override;

  void paint(juce::Graphics&) override;
  void resized() override;
  bool keyPressed(const juce::KeyPress&) override;
  void mouseDown(const juce::MouseEvent&) override;

protected:
  OpnaModalBase(juce::String title, const juce::String& confirmLabel,
                juce::Colour accent, int panelWidth);

  // Parent to `parent` (the editor, which carries the OpnaLookAndFeel), enter
  // the modal state and self-delete on dismissal. Blocking: sits on the modal
  // stack until answered.
  static void launch(juce::Component& parent, OpnaModalBase* modal);

  // Like launch(), but as a *non-blocking* overlay: it dims and intercepts input
  // over the editor (so it reads like a modal) yet never enters the modal stack,
  // so it doesn't block app close or stack awkwardly under a native file chooser.
  // Self-deletes on dismissal; the caller may keep a SafePointer to it.
  static void launchOverlay(juce::Component& parent, OpnaModalBase* modal);

  // Dismiss the dialog and run onResult(confirmed) exactly once.
  void finish(bool confirmed);

  // Keep the dimmed overlay glued to the editor as it is resized.
  void componentMovedOrResized(juce::Component&, bool, bool) override;

  // --- subclass hooks ---
  virtual int contentHeight() const = 0;  // px reserved between title and buttons
  virtual void layoutContent(juce::Rectangle<int> area) = 0;
  virtual void paintContent(juce::Graphics&, juce::Rectangle<int> /*area*/) {}
  virtual void onResult(bool confirmed) = 0;
  virtual void grabInitialFocus() { grabKeyboardFocus(); }

  juce::Colour accent_;
  juce::TextButton confirmButton_, cancelButton_;

  static constexpr int kPad = 14;
  static constexpr int kButtonW = 96;
  static constexpr int kButtonH = 28;

private:
  juce::String title_;
  int panelWidth_;
  juce::Rectangle<int> panelBounds_, contentArea_;
  bool finished_ = false;
  bool blocking_ = true;                    // false once launched as an overlay
  juce::Component* trackedParent_ = nullptr;  // for resize-follow + cleanup

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OpnaModalBase)
};

}  // namespace audio_plugin
