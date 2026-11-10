//
// Created by Alex Birch on 10/09/2017.
//

#pragma once

#include "JuceHeader.h"
#include <fluidsynth.h>
#include <memory>
#include <map>
#include <vector>
#include "MidiConstants.h"
#include "MacFileAccess.h"

using namespace std;

class FluidSynthModel
: public AudioProcessorValueTreeState::Listener {
public:
    FluidSynthModel(
        AudioProcessorValueTreeState& valueTreeState
        );
     ~FluidSynthModel();

    void initialise();
    
    int getChannel();

    void setControllerValue(int controller, int value);

    void processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages);


    void setSampleRate(float sampleRate);
    
    //==============================================================================
    virtual void parameterChanged (const String& parameterID, float newValue) override;
    
    // Explicit request keeps path/bookmark changes transactional rather than
    // loading halfway through separate ValueTree property notifications.
    bool loadSoundFont(const String& path, const MemoryBlock& bookmark);
    String getLoadError() const;
    bool hasLoadedSoundFont() const { return sfont_id >= 0; }

    //==============================================================================
    int getNumPrograms();
    int getCurrentProgram();
    void setCurrentProgram(int index);
    const String getProgramName(int index);
    void changeProgramName(int index, const String& newName);

private:
    static const StringArray programChangeParams;

    // there's no bimap in the standard library!
    static const map<fluid_midi_control_change, String> controllerToParam;
    static const map<String, fluid_midi_control_change> paramToController;

    void refreshBanks();
    void publishCurrentSelection();
    static thread_local FluidSynthModel* publishingSelection;

    AudioProcessorValueTreeState& valueTreeState;

    // https://stackoverflow.com/questions/38980315/is-stdunique-ptr-deletion-order-guaranteed
    // members are destroyed in reverse of the order they're declared
    // http://www.fluidsynth.org/api/
    // in their examples, they destroy the synth before destroying the settings
#if JUCE_MAC || JUCE_IOS
    // Declared before the synth so access outlives FluidSynth destruction.
    std::unique_ptr<JuicyMac::ScopedFileAccess> fileAccess;
    std::vector<std::unique_ptr<JuicyMac::ScopedFileAccess>> retiredFileAccess;
#endif
    unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings;
    unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth;

    float currentSampleRate;

    bool replaceFont(const String& path);
    
    int sfont_id;
    unsigned int channel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FluidSynthModel)
};
