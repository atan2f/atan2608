#include "OpnaModalBase.h"

namespace audio_plugin {

OpnaModalBase::OpnaModalBase(juce::String title,
                             const juce::String& confirmLabel,
                             juce::Colour accent, int panelWidth)
    : accent_(accent),
      confirmButton_(confirmLabel),
      cancelButton_("Cancel"),
      title_(std::move(title)),
      panelWidth_(panelWidth) {
  setInterceptsMouseClicks(true, true);  // scrim eats clicks beneath
  setWantsKeyboardFocus(true);

  confirmButton_.setColour(juce::TextButton::buttonColourId, accent_);
  confirmButton_.setColour(juce::TextButton::textColourOffId, OpnaColours::bg);
  confirmButton_.onClick = [this] { finish(true); };
  addAndMakeVisible(confirmButton_);

  cancelButton_.setColour(juce::TextButton::buttonColourId, OpnaColours::panel2);
  cancelButton_.onClick = [this] { finish(false); };
  addAndMakeVisible(cancelButton_);
}

OpnaModalBase::~OpnaModalBase() {
  if (trackedParent_ != nullptr)
    trackedParent_->removeComponentListener(this);
}

void OpnaModalBase::launch(juce::Component& parent, OpnaModalBase* modal) {
  // Parent to `parent` (the editor) rather than the top-level component: in the
  // standalone the top-level is the DocumentWindow, which doesn't carry our
  // OpnaLookAndFeel, so a modal parked there would render with stock chrome.
  parent.addAndMakeVisible(modal);
  modal->setBounds(parent.getLocalBounds());
  modal->toFront(true);
  modal->grabInitialFocus();
  modal->trackedParent_ = &parent;
  parent.addComponentListener(modal);  // follow editor resizes
  // deleteWhenDismissed=true: the component frees itself once dismissed; the
  // user callback runs first (so it may safely spawn a follow-up modal).
  modal->enterModalState(true, nullptr, true);
}

void OpnaModalBase::launchOverlay(juce::Component& parent, OpnaModalBase* modal) {
  // Same parenting + resize-follow as launch(), but no enterModalState: it stays
  // a plain (input-intercepting) child, so it doesn't block app close and a
  // native file chooser can sit on top of it. Self-deletes on finish().
  modal->blocking_ = false;
  parent.addAndMakeVisible(modal);
  modal->setBounds(parent.getLocalBounds());
  modal->toFront(true);
  modal->grabInitialFocus();
  modal->trackedParent_ = &parent;
  parent.addComponentListener(modal);
}

void OpnaModalBase::componentMovedOrResized(juce::Component& c, bool, bool) {
  if (&c == trackedParent_)
    setBounds(trackedParent_->getLocalBounds());
}

void OpnaModalBase::finish(bool confirmed) {
  if (finished_)
    return;  // guard Return + button double-fire
  finished_ = true;
  if (trackedParent_ != nullptr) {
    trackedParent_->removeComponentListener(this);
    trackedParent_ = nullptr;
  }
  if (blocking_) {
    // Exit modality *first*, so this modal leaves the modal stack while it is
    // still foremost; only then run onResult, which may spawn a follow-up modal
    // as the new foremost modal. (deleteWhenDismissed frees `this`
    // asynchronously, so members stay valid for the duration of this call.)
    exitModalState(confirmed ? 1 : 0);
    onResult(confirmed);
  } else {
    // Overlay: run the result, then self-delete on the next message loop (we are
    // inside an event callback, so deleting `this` inline would be unsafe).
    onResult(confirmed);
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<OpnaModalBase>(this)] {
          if (auto* c = safe.getComponent())
            delete c;
        });
  }
}

bool OpnaModalBase::keyPressed(const juce::KeyPress& key) {
  if (key == juce::KeyPress::escapeKey) {
    finish(false);
    return true;
  }
  if (key == juce::KeyPress::returnKey) {
    finish(true);
    return true;
  }
  return false;
}

void OpnaModalBase::mouseDown(const juce::MouseEvent& e) {
  // A click on the dimmed scrim outside the panel dismisses the dialog, same as
  // Esc / Cancel. Clicks on the panel chrome (and its child controls) don't -
  // children consume their own events, and the panel background is ignored.
  if (!panelBounds_.contains(e.getPosition()))
    finish(false);
}

void OpnaModalBase::paint(juce::Graphics& g) {
  g.fillAll(OpnaColours::bg.withAlpha(0.72f));
  OpnaLookAndFeel::drawPanel(g, panelBounds_, accent_, title_);
  paintContent(g, contentArea_);
}

void OpnaModalBase::resized() {
  const int totalH = kPad + contentHeight() + kPad + kButtonH + kPad;
  const int panelH = OpnaLookAndFeel::kTitleH + totalH;
  panelBounds_ = juce::Rectangle<int>(0, 0, panelWidth_, panelH)
                     .withCentre(getLocalBounds().getCentre());

  // Content area below the title strip (drawPanel insets by 4), inset by kPad.
  auto content = panelBounds_.withTrimmedTop(OpnaLookAndFeel::kTitleH)
                     .reduced(4)
                     .reduced(kPad, 0);
  content.removeFromTop(kPad);
  contentArea_ = content.removeFromTop(contentHeight());
  layoutContent(contentArea_);

  content.removeFromTop(kPad);
  auto buttons = content.removeFromTop(kButtonH);
  confirmButton_.setBounds(buttons.removeFromRight(kButtonW));
  buttons.removeFromRight(kPad);
  cancelButton_.setBounds(buttons.removeFromRight(kButtonW));
}

}  // namespace audio_plugin
