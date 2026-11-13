//
// Created by Alex Birch on 01/10/2017.
//

#pragma once

#include "JuceHeader.h"

class Pills : public Component,
              private ValueTree::Listener,
              private juce::Timer
{
public:
    explicit Pills(AudioProcessorValueTreeState& valueTreeState);
    ~Pills() override;

    void cycle(bool right);
    void setPart(int part);

private:
    void resized() override;
    void timerCallback() override;
    void valueTreePropertyChanged(ValueTree& tree, const Identifier& property) override;
    void rebuildBanks(const ValueTree& banks);
    int currentBank() const;
    void refreshSelection();
    void selectBank(int bank);

    String bankID{"bank"}, presetID{"preset"};
    AudioProcessorValueTreeState& valueTreeState;
    juce::ComboBox bankList;
    TextButton previousBank{String::charToString(0x2039)};
    TextButton nextBank{String::charToString(0x203a)};
    int displayedBank = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Pills)
};
