#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "OpnaComboBox.h"
#include "OpnaLookAndFeel.h"
#include "ParamGlossary.h"

namespace audio_plugin {

// Attach a help tooltip to a control from the shared glossary, keyed by its
// short label. No-op when the label has no glossary entry. The tooltip only
// appears while the editor's TooltipWindow exists (the "?" help toggle).
inline void setGlossaryTooltip(juce::SettableTooltipClient& c,
                               const juce::String& name) {
  if (const char* t = paramTooltip(name.toRawUTF8()))
    c.setTooltip(t);
}

// A rotary knob with a name label beneath and the value in the knob's text box.
// Re-attachable so one widget can follow different parameters (e.g. when the
// edited FM group changes).
class ParamKnob : public juce::Component {
public:
  ParamKnob() {
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 48, 13);
    slider.setColour(juce::Slider::textBoxTextColourId, OpnaColours::amber);
    addAndMakeVisible(slider);

    nameLabel.setJustificationType(juce::Justification::centredTop);
    nameLabel.setColour(juce::Label::textColourId, OpnaColours::dim);
    nameLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    addAndMakeVisible(nameLabel);
  }

  void attach(juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
              const juce::String& name) {
    attachment.reset();
    attachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, id, slider);
    nameLabel.setText(name, juce::dontSendNotification);
    setGlossaryTooltip(slider, name);
    setGlossaryTooltip(nameLabel, name);
  }

  void resized() override {
    auto r = getLocalBounds();
    nameLabel.setBounds(r.removeFromBottom(13));
    slider.setBounds(r);
  }

  juce::Slider slider;
  juce::Label nameLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A chunky horizontal value bar with the name on the left and the live value on
// the right (PC-98 styling). Re-attachable to a different parameter. The bar fill
// colour follows setAccent().
class ParamBar : public juce::Component {
public:
  ParamBar() {
    slider.setSliderStyle(juce::Slider::LinearBar);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setColour(juce::Slider::trackColourId, OpnaColours::cyan);
    slider.onValueChange = [this] { updateValueText(); };
    addAndMakeVisible(slider);

    nameLabel.setJustificationType(juce::Justification::centredLeft);
    nameLabel.setColour(juce::Label::textColourId, OpnaColours::dim);
    nameLabel.setFont(juce::Font(juce::FontOptions(14.0f)));
    addAndMakeVisible(nameLabel);

    valueLabel.setJustificationType(juce::Justification::centredRight);
    valueLabel.setColour(juce::Label::textColourId, OpnaColours::amber);
    valueLabel.setFont(juce::Font(juce::FontOptions(15.0f)));
    addAndMakeVisible(valueLabel);
  }

  void attach(juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
              const juce::String& name) {
    attachment.reset();
    attachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, id, slider);
    nameLabel.setText(name, juce::dontSendNotification);
    setGlossaryTooltip(slider, name);
    setGlossaryTooltip(nameLabel, name);
    updateValueText();
  }

  // Tint the bar fill to a part's accent colour.
  void setAccent(juce::Colour c) {
    accent_ = c;
    applyColours();
  }

  // Grey the bar + labels for a parameter the chip is currently ignoring. Purely
  // cosmetic -- the value stays settable (it just has no effect until re-enabled).
  void setDimmed(bool d) {
    if (d == dimmed_)
      return;
    dimmed_ = d;
    applyColours();
  }

  // Compact inline layout: name | bar | value laid out side by side in a single
  // short row, instead of the tall stacked bar with the label above it. Used in
  // strips where the bar must sit inline next to combos/toggles.
  void setCompact(bool c) {
    compact_ = c;
    resized();
  }

  void resized() override {
    if (compact_) {
      auto r = getLocalBounds();
      nameLabel.setBounds(r.removeFromLeft(30));
      valueLabel.setBounds(r.removeFromRight(26));
      slider.setBounds(r.reduced(4, 3));
      return;
    }
    auto r = getLocalBounds();
    auto top = r.removeFromTop(18);
    valueLabel.setBounds(top.removeFromRight(juce::jmax(40, top.getWidth() / 2)));
    nameLabel.setBounds(top);
    // Bar sits below the label, a touch shorter than the full remaining height.
    slider.setBounds(r.reduced(0, 3));
  }

  juce::Slider slider;
  juce::Label nameLabel, valueLabel;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

private:
  void updateValueText() {
    valueLabel.setText(slider.getTextFromValue(slider.getValue()),
                       juce::dontSendNotification);
  }

  void applyColours() {
    slider.setColour(juce::Slider::trackColourId,
                     dimmed_ ? OpnaColours::line : accent_);
    nameLabel.setColour(juce::Label::textColourId,
                        dimmed_ ? OpnaColours::line : OpnaColours::dim);
    valueLabel.setColour(juce::Label::textColourId,
                         dimmed_ ? OpnaColours::line : OpnaColours::amber);
    repaint();
  }

  juce::Colour accent_ = OpnaColours::cyan;
  bool dimmed_ = false;
  bool compact_ = false;
};

// A labelled combo box. Item N (1-based id) maps to discrete parameter value
// N-1, which matches AudioParameterInt/Choice ordering used in this project.
class ParamCombo : public juce::Component {
public:
  ParamCombo() {
    nameLabel.setColour(juce::Label::textColourId, OpnaColours::dim);
    nameLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    nameLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nameLabel);
    addAndMakeVisible(box);
  }

  void setItems(const juce::StringArray& items) {
    box.clear(juce::dontSendNotification);
    for (int i = 0; i < items.size(); ++i)
      box.addItem(items[i], i + 1);
  }

  void attach(juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
              const juce::String& name) {
    attachment.reset();
    attachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, id, box);
    nameLabel.setText(name, juce::dontSendNotification);
    setGlossaryTooltip(box, name);
    setGlossaryTooltip(nameLabel, name);
  }

  void setLabelWidth(int w) { labelWidth = w; }

  // Grey the box (text + arrow) for a control the chip is currently ignoring.
  // Purely cosmetic -- the selection stays settable. Matches ParamBar::setDimmed.
  void setDimmed(bool d) {
    const juce::Colour text = d ? OpnaColours::line : OpnaColours::ink;
    const juce::Colour arrow = d ? OpnaColours::line : OpnaColours::cyan;
    box.setColour(juce::ComboBox::textColourId, text);
    box.setColour(juce::ComboBox::arrowColourId, arrow);
    box.repaint();
  }

  void resized() override {
    auto r = getLocalBounds();
    // Only reserve space for the name label when it is shown; a hidden label
    // (callers paint their own column header) lets the box fill the bounds.
    if (nameLabel.isVisible()) {
      if (labelWidth > 0)
        nameLabel.setBounds(r.removeFromLeft(labelWidth));
      else
        nameLabel.setBounds(r.removeFromTop(14));
    }
    box.setBounds(r.reduced(0, 1));
  }

  OpnaComboBox box;
  juce::Label nameLabel;
  int labelWidth = 0;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// A toggle button bound to a Bool parameter.
class ParamToggle : public juce::Component {
public:
  ParamToggle() { addAndMakeVisible(button); }

  void attach(juce::AudioProcessorValueTreeState& apvts, const juce::String& id,
              const juce::String& name) {
    button.setButtonText(name);
    attachment.reset();
    attachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, id, button);
    setGlossaryTooltip(button, name);
  }

  // Grey the label + tick for a control the chip is currently ignoring. Purely
  // cosmetic -- the toggle stays settable. Matches ParamBar::setDimmed.
  void setDimmed(bool d) {
    button.setColour(juce::ToggleButton::textColourId,
                     d ? OpnaColours::line : OpnaColours::dim);
    button.setColour(juce::ToggleButton::tickColourId,
                     d ? OpnaColours::line : OpnaColours::cyan);
    button.repaint();
  }

  void resized() override { button.setBounds(getLocalBounds()); }

  juce::ToggleButton button;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

}  // namespace audio_plugin
