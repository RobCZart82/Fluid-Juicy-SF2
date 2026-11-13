#pragma once
#include "PartParameters.h"
#include <memory>

class PartMixer : public juce::Component {
public:
    explicit PartMixer(juce::AudioProcessorValueTreeState& state) {
        for (int part = 0; part < PartParameters::count; ++part) {
            auto& row = rows[static_cast<size_t>(part)];
            row.number.setButtonText(juce::String(part + 1) + (part == 9 ? " D" : ""));
            row.number.setTooltip("Select MIDI part " + juce::String(part + 1));
            row.number.onClick = [this, part] { if (onSelect) onSelect(part); };
            content.addAndMakeVisible(row.number);
            const char* names[] = {"volume", "pan", "expression"};
            for (int control = 0; control < 3; ++control) {
                auto& slider = row.sliders[static_cast<size_t>(control)];
                slider.setSliderStyle(juce::Slider::LinearHorizontal);
                slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 30, 20);
                slider.setTooltip("Part " + juce::String(part + 1) + " " + names[control]);
                slider.setComponentID(PartParameters::id(part, names[control]));
                row.attachments[static_cast<size_t>(control)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    state, PartParameters::id(part, names[control]), slider);
                content.addAndMakeVisible(slider);
            }
        }
        for (int i = 0; i < 4; ++i) {
            const char* titles[] = {"Part", "Vol CC7", "Pan CC10", "Expr CC11"};
            headers[static_cast<size_t>(i)].setText(titles[i], juce::dontSendNotification);
            headers[static_cast<size_t>(i)].setFont(juce::FontOptions(11.0f));
            addAndMakeVisible(headers[static_cast<size_t>(i)]);
        }
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        setComponentID("partMixer");
    }
    ~PartMixer() override { viewport.setViewedComponent(nullptr, false); }
    std::function<void(int)> onSelect;
    void setSelectedPart(int part) {
        for (int i = 0; i < PartParameters::count; ++i)
            rows[static_cast<size_t>(i)].number.setToggleState(i == part, juce::dontSendNotification);
    }
    void resized() override {
        auto area = getLocalBounds();
        auto heading = area.removeFromTop(24);
        const int width = juce::jmax(240, area.getWidth() - viewport.getScrollBarThickness());
        const int controlWidth = (width - 42) / 3;
        headers[0].setBounds(heading.removeFromLeft(42));
        for (int i = 1; i < 4; ++i) headers[static_cast<size_t>(i)].setBounds(heading.removeFromLeft(controlWidth));
        viewport.setBounds(area);
        content.setSize(width, PartParameters::count * 34);
        for (int part = 0; part < PartParameters::count; ++part) {
            auto line = juce::Rectangle<int>(0, part * 34, width, 32);
            auto& row = rows[static_cast<size_t>(part)];
            row.number.setBounds(line.removeFromLeft(42).reduced(2));
            for (auto& slider : row.sliders) slider.setBounds(line.removeFromLeft(controlWidth).reduced(2, 0));
        }
    }
private:
    struct Row {
        juce::TextButton number;
        std::array<juce::Slider, 3> sliders;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> attachments;
    };
    juce::Component content;
    std::array<Row, PartParameters::count> rows;
    std::array<juce::Label, 4> headers;
    juce::Viewport viewport;
};
