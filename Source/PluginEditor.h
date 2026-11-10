/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include "JuceHeader.h"
#include "PluginProcessor.h"
#include "TablesComponent.h"
#include "SurjectiveMidiKeyboardComponent.h"
#include "FilePicker.h"
#include "SlidersComponent.h"
#include "MyColours.h"

using juce::SurjectiveMidiKeyboardComponent;

//==============================================================================
/**
*/
class JuicySFAudioProcessorEditor
: public AudioProcessorEditor
, private juce::Timer
{
public:
    JuicySFAudioProcessorEditor(
      JuicySFAudioProcessor&,
      AudioProcessorValueTreeState& valueTreeState
      );
    ~JuicySFAudioProcessorEditor();

    //==============================================================================
    void paint (Graphics&) override;
    void resized() override;

    bool keyPressed(const KeyPress &key) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    JuicySFAudioProcessor& processor;

    AudioProcessorValueTreeState& valueTreeState;

    MyColours colours;
    SurjectiveMidiKeyboardComponent midiKeyboard;
    TablesComponent tablesComponent;
    FilePicker filePicker;
    SlidersComponent slidersComponent;

    bool focusInitialized = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessorEditor)
};
