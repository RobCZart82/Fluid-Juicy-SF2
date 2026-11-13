/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GuiConstants.h"

//==============================================================================
JuicySFAudioProcessorEditor::JuicySFAudioProcessorEditor(
    JuicySFAudioProcessor& p,
    AudioProcessorValueTreeState& valueTreeState)
: AudioProcessorEditor{&p}
, processor{p}
, valueTreeState{valueTreeState}
, midiKeyboard{p.keyboardState, SurjectiveMidiKeyboardComponent::horizontalKeyboard}
, mixer{valueTreeState}
, tablesComponent{valueTreeState}
, filePicker{valueTreeState, p.getFluidSynthModel(), p.soundFontLibrary}
, slidersComponent{valueTreeState, p.getFluidSynthModel()}
{
    partSelector.setComponentID("partSelector");
    partSelector.setTitle("MIDI part");
    for (int part = 0; part < PartParameters::count; ++part)
        partSelector.addItem("Part " + String(part + 1) + (part == 9 ? " - Drums" : ""), part + 1);
    partSelector.onChange = [this] { selectPart(partSelector.getSelectedId() - 1); };
    mixerButton.setComponentID("showMixer");
    routingButton.setComponentID("routingMode");
    mixerButton.setClickingTogglesState(true);
    mixerButton.onClick = [this] {
        mixer.setVisible(mixerButton.getToggleState());
        tablesComponent.setVisible(!mixerButton.getToggleState());
        resized();
    };
    routingButton.onClick = [this] {
        processor.keyboardState.allNotesOff(0);
        processor.getFluidSynthModel().setLegacyOmni(!processor.getFluidSynthModel().isLegacyOmni(), true);
        timerCallback();
    };
    routingButton.setTooltip("16-part: MIDI channel 1-16 controls the matching part. Legacy omni routes every channel to part 1.");
    mixer.onSelect = [this](int part) { selectPart(part); };
    addAndMakeVisible(partSelector);
    addAndMakeVisible(partName);
    addAndMakeVisible(mixerButton);
    addAndMakeVisible(routingButton);
    addChildComponent(mixer);
    selectPart(processor.getFluidSynthModel().getChannel());
    partSelector.setEnabled(!processor.getFluidSynthModel().isLegacyOmni());
    mixerButton.setEnabled(!processor.getFluidSynthModel().isLegacyOmni());
    slidersComponent.onClearOutputOver = [this] { processor.clearOutputOver(); };
    filePicker.onLayoutChanged = [this] { resized(); };
    const auto restoredSize = processor.getEditorSize();
    setLookAndFeel(&colours);

    // set resize limits for this plug-in
    setResizeLimits(
        GuiConstants::minWidth,
        GuiConstants::minHeight,
        GuiConstants::maxWidth,
        GuiConstants::maxHeight);

    setSize(restoredSize.first, restoredSize.second);
    processor.getFluidSynthModel().synchroniseUi();
    startTimerHz(30);

    midiKeyboard.setName ("MIDI Keyboard");

    midiKeyboard.setWantsKeyboardFocus(false);
    tablesComponent.setWantsKeyboardFocus(false);

    setWantsKeyboardFocus(true);
    addAndMakeVisible(midiKeyboard);

    addAndMakeVisible(slidersComponent);
    addAndMakeVisible(tablesComponent);
    addAndMakeVisible(filePicker);

}

void JuicySFAudioProcessorEditor::timerCallback() {
    slidersComponent.updateOutputMeter(processor.consumeOutputPeak(), processor.hasOutputOver());
    auto& model = processor.getFluidSynthModel();
    model.synchroniseUi();
    partSelector.setEnabled(!model.isLegacyOmni());
    mixerButton.setEnabled(!model.isLegacyOmni());
    if (model.isLegacyOmni()) {
        model.setSelectedPart(0);
        mixerButton.setToggleState(false, dontSendNotification);
        mixer.setVisible(false); tablesComponent.setVisible(true);
    }
    if (model.getChannel() != displayedPart) selectPart(model.getChannel());
    partName.setText(model.getPartName(displayedPart), dontSendNotification);
    routingButton.setButtonText(model.isLegacyOmni() ? "Legacy omni" : "16-part MIDI");
    const auto size = processor.getEditorSize();
    if (size.first != getWidth() || size.second != getHeight()) setSize(size.first, size.second);
}

JuicySFAudioProcessorEditor::~JuicySFAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

//==============================================================================
void JuicySFAudioProcessorEditor::paint (Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));

    if (!focusInitialized) {
        if (!hasKeyboardFocus(false) && isVisible()) {
            grabKeyboardFocus();
        }
        if (getCurrentlyFocusedComponent() == this) {
            focusInitialized = true;
        }
    }
}

void JuicySFAudioProcessorEditor::resized()
{
    const int padding{8};
    const int pianoHeight{70};
    const int filePickerHeight{filePicker.getPreferredHeight()};
    Rectangle<int> r{getLocalBounds()};
    filePicker.setBounds(r.removeFromTop(filePickerHeight + padding).reduced(padding, 0).withTrimmedTop(padding));

    auto partRow = r.removeFromTop(30).reduced(padding, 2);
    partSelector.setBounds(partRow.removeFromLeft(150));
    mixerButton.setBounds(partRow.removeFromLeft(72).reduced(3, 0));
    routingButton.setBounds(partRow.removeFromLeft(130).reduced(3, 0));
    partName.setBounds(partRow.reduced(6, 0));
    midiKeyboard.setBounds (r.removeFromBottom (pianoHeight).reduced(padding, 0));

    Rectangle<int> rContent{r.reduced(0, padding)};
    slidersComponent.setBounds(rContent.removeFromRight(slidersComponent.getDesiredWidth() + padding).withTrimmedRight(padding));

    tablesComponent.setBounds(rContent);
    mixer.setBounds(rContent);

    processor.setEditorSize(getWidth(), getHeight());
}

bool JuicySFAudioProcessorEditor::keyPressed(const KeyPress &key) {
    const int cursorKeys[] = {
            KeyPress::leftKey,
            KeyPress::rightKey,
            KeyPress::upKey,
            KeyPress::downKey
    };
    if (any_of(
            begin(cursorKeys),
            end(cursorKeys),
            [&](int i) { return i == key.getKeyCode(); }
    )) {
        return tablesComponent.keyPressed(key);
    } else {
        return midiKeyboard.keyPressed(key);
    }
    return false;
}

bool JuicySFAudioProcessorEditor::keyStateChanged (bool isKeyDown) {
    return midiKeyboard.keyStateChanged(isKeyDown);
}

void JuicySFAudioProcessorEditor::selectPart(int part) {
    part = processor.getFluidSynthModel().isLegacyOmni() ? 0 : juce::jlimit(0, PartParameters::count - 1, part);
    if (displayedPart >= 0 && displayedPart != part) processor.keyboardState.allNotesOff(displayedPart + 1);
    displayedPart = part;
    processor.getFluidSynthModel().setSelectedPart(part);
    partSelector.setSelectedId(part + 1, dontSendNotification);
    midiKeyboard.setMidiChannel(part + 1);
    mixer.setSelectedPart(part);
    partName.setText(processor.getFluidSynthModel().getPartName(part), dontSendNotification);
    tablesComponent.setPart(part);
    slidersComponent.setPart(part);
    routingButton.setButtonText(processor.getFluidSynthModel().isLegacyOmni() ? "Legacy omni" : "16-part MIDI");
}
