#pragma once
#include "JuceHeader.h"

class OutputMeter final : public juce::Component, public juce::SettableTooltipClient {
public:
    OutputMeter() {
        setComponentID("outputPeakMeter");
        setTooltip("Post-Output peak. OVER means samples exceeded 0 dBFS. Lower Output or part levels. Click to clear OVER.");
    }
    void update(float peak, bool exceeded) {
        level = juce::jmax(peak, level * 0.85f);
        over = exceeded;
        repaint();
    }
    std::function<void()> onClear;
    void mouseDown(const juce::MouseEvent&) override { if (onClear) onClear(); }
    void paint(juce::Graphics& g) override {
        auto area = getLocalBounds();
        auto text = area.removeFromRight(72);
        auto bar = area.reduced(0, 5).toFloat();
        const float db = juce::Decibels::gainToDecibels(level, -60.0f);
        const float fraction = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour(juce::Colour(0xff20282e)); g.fillRect(bar);
        g.setColour(over ? juce::Colour(0xffed6868) : db > -3.0f ? juce::Colour(0xffd8ba55) : juce::Colour(0xff60c5ba));
        g.fillRect(bar.withWidth(bar.getWidth() * fraction));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(over ? "OVER" : level < 0.001f ? "-inf dBFS" : juce::String(db, 1) + " dBFS", text, juce::Justification::centredRight);
    }
private:
    float level = 0;
    bool over = false;
};
