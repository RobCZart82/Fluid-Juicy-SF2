/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once


#include "JuceHeader.h"
#include "FluidSynthModel.h"
#include "SoundFontLibrary.h"
#include <list>
#include <atomic>
#include "GuiConstants.h"

using namespace std;

//==============================================================================
/**
*/
class JuicySFAudioProcessor  : public AudioProcessor, private juce::Timer
{
public:
    //==============================================================================
    JuicySFAudioProcessor();
    ~JuicySFAudioProcessor();

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override;

    //==============================================================================
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool supportsDoublePrecisionProcessing() const override;

    FluidSynthModel& getFluidSynthModel();

    SoundFontLibrary soundFontLibrary;
    // Transient post-gain metering. Does not alter audio or serialized project state.
    float consumeOutputPeak() { return outputPeak.exchange(0.0f, std::memory_order_relaxed); }
    bool hasOutputOver() const { return outputOver.load(std::memory_order_relaxed); }
    void clearOutputOver() { outputOver.store(false, std::memory_order_relaxed); }
    MidiKeyboardState keyboardState;
    std::pair<int, int> getEditorSize() const;
    void setEditorSize(int width, int height);

private:
    void timerCallback() override;
    void initialiseSynth();
    std::atomic<uint64_t> editorSize{(uint64_t(GuiConstants::minWidth) << 32)
                                    | uint64_t(GuiConstants::minHeight)};

    AudioProcessorValueTreeState valueTreeState;

    FluidSynthModel fluidSynthModel;
    Synthesiser synth;
    juce::SmoothedValue<float> outputLevel;
    static_assert(std::atomic<float>::is_always_lock_free, "Meter transport requires lock-free float atomics");
    std::atomic<float> outputPeak{0};
    std::atomic<bool> outputOver{false};

    AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static BusesProperties getBusesProperties();

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessor)
};
