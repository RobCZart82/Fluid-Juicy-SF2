/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once


#include "JuceHeader.h"
#include "FluidSynthModel.h"
#include <list>
#include <atomic>
#include "GuiConstants.h"

using namespace std;

//==============================================================================
/**
*/
class JuicySFAudioProcessor  : public AudioProcessor
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

    MidiKeyboardState keyboardState;
    std::pair<int, int> getEditorSize() const;
    void setEditorSize(int width, int height);

private:
    void initialiseSynth();
    std::atomic<uint64_t> editorSize{(uint64_t(GuiConstants::minWidth) << 32)
                                    | uint64_t(GuiConstants::minHeight)};

    AudioProcessorValueTreeState valueTreeState;

    FluidSynthModel fluidSynthModel;
    Synthesiser synth;
    juce::SmoothedValue<float> outputLevel;

    AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    static BusesProperties getBusesProperties();

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessor)
};
