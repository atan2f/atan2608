#include "OpnaModal.h"

namespace audio_plugin {

namespace {
constexpr int kPanelW = 440;
constexpr int kEditorH = 28;
constexpr int kMessageH = 60;
}  // namespace

OpnaModal::OpnaModal(const juce::String& title, const juce::String& message,
                     const juce::String& confirmLabel, juce::Colour accent,
                     bool withEditor, const juce::String& initialText,
                     Callback cb)
    : OpnaModalBase(title, confirmLabel, accent, kPanelW),
      message_(message),
      withEditor_(withEditor),
      callback_(std::move(cb)) {
  if (withEditor_) {
    editor_.setMultiLine(false);
    editor_.setReturnKeyStartsNewLine(false);
    editor_.setFont(OpnaLookAndFeel::font(15.0f));
    editor_.setColour(juce::TextEditor::backgroundColourId, OpnaColours::bg);
    editor_.setColour(juce::TextEditor::textColourId, OpnaColours::ink);
    editor_.setColour(juce::TextEditor::outlineColourId, OpnaColours::line);
    editor_.setColour(juce::TextEditor::focusedOutlineColourId, accent_);
    editor_.setColour(juce::TextEditor::highlightColourId,
                      accent_.withAlpha(0.4f));
    editor_.setText(initialText, juce::dontSendNotification);
    editor_.selectAll();
    editor_.onReturnKey = [this] { finish(true); };
    editor_.onEscapeKey = [this] { finish(false); };
    addAndMakeVisible(editor_);
  }
}

void OpnaModal::confirm(juce::Component& parent, const juce::String& title,
                        const juce::String& message,
                        const juce::String& confirmLabel, juce::Colour accent,
                        std::function<void()> onConfirm,
                        std::function<void()> onCancel) {
  auto* m = new OpnaModal(
      title, message, confirmLabel, accent, /*withEditor=*/false, {},
      [yes = std::move(onConfirm), no = std::move(onCancel)](
          bool confirmed, const juce::String&) {
        if (confirmed) {
          if (yes)
            yes();
        } else if (no) {
          no();
        }
      });
  launch(parent, m);
}

void OpnaModal::prompt(juce::Component& parent, const juce::String& title,
                       const juce::String& message,
                       const juce::String& initialText,
                       std::function<void(const juce::String&)> onConfirm) {
  auto* m = new OpnaModal(
      title, message, "Save", OpnaColours::cyan, /*withEditor=*/true,
      initialText,
      [cb = std::move(onConfirm)](bool confirmed, const juce::String& text) {
        if (confirmed && cb && text.isNotEmpty())
          cb(text);
      });
  launch(parent, m);
}

int OpnaModal::contentHeight() const {
  return kMessageH + (withEditor_ ? kPad + kEditorH : 0);
}

void OpnaModal::layoutContent(juce::Rectangle<int> area) {
  area.removeFromTop(kMessageH);  // message is drawn in paintContent
  if (withEditor_) {
    area.removeFromTop(kPad);
    editor_.setBounds(area.removeFromTop(kEditorH));
  }
}

void OpnaModal::paintContent(juce::Graphics& g, juce::Rectangle<int> area) {
  auto msg = area.withHeight(kMessageH);
  g.setColour(OpnaColours::ink);
  g.setFont(OpnaLookAndFeel::font(15.0f));
  // minimumHorizontalScale=1.0 forbids glyph squashing: the text wraps across
  // lines instead of being crunched onto one.
  g.drawFittedText(message_, msg, juce::Justification::topLeft,
                   /*maxLines=*/3, /*minimumHorizontalScale=*/1.0f);
}

void OpnaModal::onResult(bool confirmed) {
  if (callback_ == nullptr)
    return;
  auto cb = std::move(callback_);
  cb(confirmed, withEditor_ ? editor_.getText().trim() : juce::String());
}

void OpnaModal::grabInitialFocus() {
  if (withEditor_)
    editor_.grabKeyboardFocus();
  else
    grabKeyboardFocus();
}

}  // namespace audio_plugin
