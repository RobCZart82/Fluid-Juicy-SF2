//
// Created by Alex Birch on 03/10/2017.
//

#include "MyColours.h"
#include "SurjectiveMidiKeyboardComponent.h"

MyColours::MyColours()
{
    const Colour background{0xff191d21};
    const Colour panel{0xff22282d};
    const Colour outline{0xff3b444c};
    const Colour text{0xfff2f5f6};
    const Colour teal{accent};
    const Colour selection{selectedRow};

    setColourScheme(ColourScheme(background, panel, panel, outline, text,
                                teal, text, selection, text));

    setColour(TextButton::buttonColourId, panel);
    setColour(TextButton::textColourOffId, text);
    setColour(juce::ComboBox::backgroundColourId, panel);
    setColour(juce::ComboBox::outlineColourId, outline);
    setColour(juce::ComboBox::arrowColourId, text);
    setColour(juce::ListBox::backgroundColourId, background);
    setColour(juce::ListBox::textColourId, text);
    setColour(juce::ListBox::outlineColourId, outline);
    setColour(juce::TableHeaderComponent::backgroundColourId, panel);
    setColour(juce::TableHeaderComponent::textColourId, text);
    setColour(juce::TableHeaderComponent::outlineColourId, outline);
    setColour(juce::TableHeaderComponent::highlightColourId, selection);
    setColour(juce::Slider::thumbColourId, teal);
    setColour(juce::Slider::trackColourId, teal);
    setColour(juce::Slider::textBoxOutlineColourId, outline);
    setColour(juce::ScrollBar::thumbColourId, Colour(0xff77848d));
    setColour(juce::SurjectiveMidiKeyboardComponent::keyDownOverlayColourId, teal);
}
