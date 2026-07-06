#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace audio_plugin {

// ComboBox subclass whose popup, when dismissed by a click outside the popup
// but still inside the host plugin editor, swallows that click so it doesn't
// activate the component beneath the cursor.
//
// Mechanism: while the popup is open, an invisible full-bounds overlay is parked
// at the top of the top-level editor's child list. JUCE's hit-testing routes the
// dismiss click to that overlay (above every other editor child), which eats the
// click in mouseDown. The overlay self-removes when the popup closes.
class OpnaComboBox : public juce::ComboBox {
public:
  using juce::ComboBox::ComboBox;

  void showPopup() override {
    installDismissEater();
    juce::ComboBox::showPopup();
  }

private:
  struct DismissEater : public juce::Component, private juce::Timer {
    juce::ComboBox::SafePointer<juce::ComboBox> owner;

    explicit DismissEater(juce::ComboBox& o) : owner(&o) {
      setInterceptsMouseClicks(true, false);
      setWantsKeyboardFocus(false);
      startTimer(30);
    }

    void mouseDown(const juce::MouseEvent&) override {
      if (owner != nullptr)
        owner->hidePopup();
      remove();
    }

    void timerCallback() override {
      if (owner == nullptr || !owner->isPopupActive())
        remove();
    }

    void remove() { delete this; }
  };

  void installDismissEater() {
    auto* top = getTopLevelComponent();
    if (top == nullptr)
      return;
    auto* eater = new DismissEater(*this);
    top->addAndMakeVisible(eater);
    eater->setBounds(top->getLocalBounds());
    eater->toFront(false);
  }
};

}  // namespace audio_plugin
