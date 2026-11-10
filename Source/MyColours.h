//
// Created by Alex Birch on 03/10/2017.
//

#pragma once

#include "JuceHeader.h"

class MyColours final : public LookAndFeel_V4
{
public:
    MyColours();

    static constexpr juce::uint32 selectedRow = 0xff214640;
    static constexpr juce::uint32 accent = 0xff63c7bb;
};