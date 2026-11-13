#pragma once
#include "JuceHeader.h"
#include <array>

namespace PartParameters {
inline constexpr int count = 16;
inline constexpr int maxBendRange = 72; // FluidSynth public pitch-wheel-sensitivity API limit.
struct Control { int cc; const char* name; const char* label; int initial; };
inline constexpr std::array<Control, 11> controls{{
    {71, "filterResonance", "Filter resonance", 0}, {72, "release", "Release", 0},
    {73, "attack", "Attack", 0}, {74, "filterCutOff", "Filter cutoff", 0},
    {75, "decay", "Decay", 0}, {79, "sustain", "Envelope sustain", 0},
    {1, "modulation", "Modulation", 0}, {7, "volume", "Volume", 100},
    {10, "pan", "Pan", 64}, {11, "expression", "Expression", 127},
    {64, "hold", "Sustain pedal", 0}
}};
inline juce::String id(int part, const juce::String& name) {
    if (part == 0 && (name == "bank" || name == "preset" || name == "attack" || name == "decay"
        || name == "sustain" || name == "release" || name == "filterCutOff" || name == "filterResonance"))
        return name; // Preserve every historical part-1 parameter ID and index.
    return "part" + juce::String(part + 1) + "." + name;
}
inline bool isTransaction(int cc) { return cc == 6 || cc == 38 || (cc >= 96 && cc <= 101); }
}
