//
// Created by Alex Birch on 01/10/2017.
//

#include "Pills.h"
#include "PartParameters.h"
#include <set>

Pills::Pills(AudioProcessorValueTreeState& valueTreeState)
    : valueTreeState{valueTreeState}
{
    bankList.setTitle("SoundFont bank");
    bankList.setTextWhenNothingSelected("Select bank");
    bankList.setTextWhenNoChoicesAvailable("No banks available");
    bankList.onChange = [this] {
        if (const int id = bankList.getSelectedId(); id > 0)
            selectBank(id - 1);
    };
    previousBank.setTitle("Previous bank");
    nextBank.setTitle("Next bank");
    previousBank.onClick = [this] { cycle(false); };
    nextBank.onClick = [this] { cycle(true); };

    addAndMakeVisible(previousBank);
    addAndMakeVisible(bankList);
    addAndMakeVisible(nextBank);

    rebuildBanks(valueTreeState.state.getChildWithName("banks"));
    valueTreeState.state.addListener(this);
    startTimerHz(30);
}

Pills::~Pills()
{
    stopTimer();
    valueTreeState.state.removeListener(this);
}

void Pills::resized()
{
    auto area = getLocalBounds();
    previousBank.setBounds(area.removeFromLeft(27));
    nextBank.setBounds(area.removeFromRight(27));
    bankList.setBounds(area.reduced(4, 0));
}

int Pills::currentBank() const
{
    return static_cast<int>(valueTreeState.getRawParameterValue(bankID)->load());
}

void Pills::timerCallback()
{
    if (currentBank() != displayedBank)
        refreshSelection();
}

void Pills::valueTreePropertyChanged(ValueTree& tree, const Identifier& property)
{
    if (tree.hasType("banks") && property == Identifier("synthetic"))
        rebuildBanks(tree);
}

void Pills::rebuildBanks(const ValueTree& banks)
{
    std::set<int> bankNumbers;
    for (const auto& bank : banks)
        bankNumbers.insert(static_cast<int>(bank.getProperty("num")));

    bankList.clear(dontSendNotification);
    for (const auto bank : bankNumbers)
        bankList.addItem("Bank " + String(bank), bank + 1);

    const bool hasBanks = bankList.getNumItems() > 0;
    bankList.setEnabled(hasBanks);
    bankList.setTextWhenNothingSelected(hasBanks ? "Select bank" : "No banks available");
    refreshSelection();
}

void Pills::refreshSelection()
{
    displayedBank = currentBank();
    bankList.setSelectedId(displayedBank + 1, dontSendNotification);
    const int index = bankList.getSelectedItemIndex();
    const int count = bankList.getNumItems();
    previousBank.setEnabled(count > 0 && index != 0);
    nextBank.setEnabled(count > 0 && index < count - 1);
}

void Pills::cycle(bool right)
{
    refreshSelection();
    const int count = bankList.getNumItems();
    const int current = bankList.getSelectedItemIndex();
    const int target = current < 0 ? (right ? 0 : count - 1) : current + (right ? 1 : -1);
    if (target >= 0 && target < count)
        selectBank(bankList.getItemId(target) - 1);
}

void Pills::selectBank(int bank)
{
    if (bank == currentBank())
        return;

    for (const auto& entry : valueTreeState.state.getChildWithName("banks"))
    {
        if (static_cast<int>(entry.getProperty("num")) == bank && entry.getNumChildren() > 0)
        {
            *static_cast<AudioParameterInt*>(valueTreeState.getParameter(bankID)) = bank;
            *static_cast<AudioParameterInt*>(valueTreeState.getParameter(presetID)) =
                static_cast<int>(entry.getChild(0).getProperty("num"));
            refreshSelection();
            return;
        }
    }
}

void Pills::setPart(int part) {
    bankID = PartParameters::id(part, "bank");
    presetID = PartParameters::id(part, "preset");
    refreshSelection();
}
