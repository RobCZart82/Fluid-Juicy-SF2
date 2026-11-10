//
// Created by Alex Birch on 01/10/2017.
//

#pragma once

#include "JuceHeader.h"

using namespace std;

class Pill
: public Component
, public Button::Listener
{
public:
    Pill(
        AudioProcessorValueTreeState& valueTreeState,
        int bank,
        bool isFirst,
        bool isLast
    );
    ~Pill();

    void buttonClicked(Button* button) override;

    void resized() override;
    void paint(Graphics& g) override;

    void bankChanged(int bank);
private:

    AudioProcessorValueTreeState& valueTreeState;
    int bank;
    TextButton textButton;

    friend class Pills;
};

class Pills
: public Component
, public ValueTree::Listener
, private juce::Timer
{
public:
    Pills(
        AudioProcessorValueTreeState& valueTreeState
    );
    ~Pills();
    
    void cycle(bool right);



    virtual void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                           const Identifier& property) override;
    inline virtual void valueTreeChildAdded (ValueTree& parentTree,
                                             ValueTree& childWhichHasBeenAdded) override {};
    inline virtual void valueTreeChildRemoved (ValueTree& parentTree,
                                               ValueTree& childWhichHasBeenRemoved,
                                               int indexFromWhichChildWasRemoved) override {};
    inline virtual void valueTreeChildOrderChanged (ValueTree& parentTreeWhoseChildrenHaveMoved,
                                                    int oldIndex, int newIndex) override {};
    inline virtual void valueTreeParentChanged (ValueTree& treeWhoseParentHasChanged) override {};
    inline virtual void valueTreeRedirected (ValueTree& treeWhichHasBeenChanged) override {};
private:
    void timerCallback() override;
    int displayedBank = -1;
    void loadModelFrom(ValueTree& banks);

    AudioProcessorValueTreeState& valueTreeState;

    vector<unique_ptr<Pill>> pills;
    Button *selected;

    void updatePillToggleStates();

    void populate(int initiallySelectedItem);
    void resized() override;
    void paint(Graphics& g) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pills)
};
