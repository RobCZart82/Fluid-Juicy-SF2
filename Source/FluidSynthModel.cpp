//
// Created by Alex Birch on 10/09/2017.
//

#include <iostream>
#include <iterator>
#include <fluidsynth.h>
#include "FluidSynthModel.h"
#include "MidiConstants.h"
#include "Util.h"

#include "MacFileAccess.h"
#if JUCE_MAC || JUCE_IOS
using JuicyMac::CFPtr;
#endif

using namespace std;

thread_local FluidSynthModel* FluidSynthModel::publishingSelection = nullptr;

const map<fluid_midi_control_change, String> FluidSynthModel::controllerToParam{
    {SOUND_CTRL2, "filterResonance"}, // MIDI CC 71 Timbre/Harmonic Intensity (filter resonance)
    {SOUND_CTRL3, "release"}, // MIDI CC 72 Release time
    {SOUND_CTRL4, "attack"}, // MIDI CC 73 Attack time
    {SOUND_CTRL5, "filterCutOff"}, // MIDI CC 74 Brightness (cutoff frequency, FILTERFC)
    {SOUND_CTRL6, "decay"}, // MIDI CC 75 Decay Time
    {SOUND_CTRL10, "sustain"}}; // MIDI CC 79 undefined

const map<String, fluid_midi_control_change> FluidSynthModel::paramToController{[]{
    map<String, fluid_midi_control_change> map;
    transform(
        controllerToParam.begin(),
        controllerToParam.end(),
        inserter(map, map.begin()),
        [](const pair<fluid_midi_control_change, String>& pair) {
            return make_pair(pair.second, pair.first);
        });
    return map;
}()};

FluidSynthModel::FluidSynthModel(
    AudioProcessorValueTreeState& valueTreeState
    )
: valueTreeState{valueTreeState}
, settings{nullptr, nullptr}
, synth{nullptr, nullptr}
, currentSampleRate{44100}
, sfont_id{-1}
, channel{0}
{
    valueTreeState.addParameterListener("bank", this);
    valueTreeState.addParameterListener("preset", this);
    for (const auto &[param, controller]: paramToController) {
        valueTreeState.addParameterListener(param, this);
    }

}

FluidSynthModel::~FluidSynthModel() {
    for (const auto &[param, controller]: paramToController) {
        valueTreeState.removeParameterListener(param, this);
    }
    valueTreeState.removeParameterListener("bank", this);
    valueTreeState.removeParameterListener("preset", this);

}

void FluidSynthModel::initialise() {
    // deactivate all audio drivers in fluidsynth to avoid FL Studio deadlock when initialising CoreAudio
    // after all: we only use fluidsynth to render blocks of audio. it doesn't output to audio driver.
    const char *DRV[] {NULL};
    fluid_audio_driver_register(DRV);
    
    settings = { new_fluid_settings(), delete_fluid_settings };
    
    // https://sourceforge.net/p/fluidsynth/wiki/FluidSettings/
#if JUCE_DEBUG
    fluid_settings_setint(settings.get(), "synth.verbose", 1);
#endif

    fluid_settings_setnum(settings.get(), "synth.sample-rate", currentSampleRate);
    synth = { new_fluid_synth(settings.get()), delete_fluid_synth };

    // I can't hear a damned thing
    fluid_synth_set_gain(synth.get(), 2.0);
    
    // note: fluid_chan.c#fluid_channel_init_ctrl()
    // > Just like panning, a value of 64 indicates no change for sound ctrls
    // --
    // so, advice is to leave everything at 64
    // and yet, I'm finding that default modulators start at MIN,
    // i.e. we are forced to start at 0 and climb from there
    // --
    // let's zero out every audio param that we manage
    for (const auto &[controller, param]: controllerToParam) {
        setControllerValue(static_cast<int>(controller), 0);
    }
    
    // http://www.synthfont.com/SoundFont_NRPNs.PDF
    float env_amount{20000.0f};
    
    unique_ptr<fluid_mod_t, decltype(&delete_fluid_mod)> mod{new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL2), // MIDI CC 71 Timbre/Harmonic Intensity (filter resonance)
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_CONCAVE
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_FILTERQ);
    fluid_mod_set_amount(mod.get(), FLUID_PEAK_ATTENUATION);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL3), // MIDI CC 72 Release time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVRELEASE);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL4), // MIDI CC 73 Attack time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVATTACK);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    // soundfont spec says that if cutoff is >20kHz and resonance Q is 0, then no filtering occurs
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL5), // MIDI CC 74 Brightness (cutoff frequency, FILTERFC)
                          FLUID_MOD_CC
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_POSITIVE);
        fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_FILTERFC);
    fluid_mod_set_amount(mod.get(), -2400.0f);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL6), // MIDI CC 75 Decay Time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVDECAY);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL10), // MIDI CC 79 undefined
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_CONCAVE
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVSUSTAIN);
    // fluice_voice.c#fluid_voice_update_param()
    // clamps the range to between 0 and 1000, so we'll copy that
    fluid_mod_set_amount(mod.get(), 1000.0f);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
}

const StringArray FluidSynthModel::programChangeParams{"bank", "preset"};
void FluidSynthModel::parameterChanged(const String& parameterID, float newValue) {
    if (programChangeParams.contains(parameterID)) {
        if (publishingSelection == this || !synth || sfont_id < 0) return;
        int bank, preset;
        {
            RangedAudioParameter *param{valueTreeState.getParameter("bank")};
            jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
            AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
            bank = castParam->get();
        }
        {
            RangedAudioParameter *param{valueTreeState.getParameter("preset")};
            jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
            AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
            preset = castParam->get();
        }
        int bankOffset{fluid_synth_get_bank_offset(synth.get(), sfont_id)};
        fluid_synth_program_select(
            synth.get(),
            channel,
            sfont_id,
            static_cast<unsigned int>(bankOffset + bank),
            static_cast<unsigned int>(preset));
    } else if (
        // https://stackoverflow.com/a/55482091/5257399
        auto it{paramToController.find(parameterID)};
        it != end(paramToController)) {
        RangedAudioParameter *param{valueTreeState.getParameter(parameterID)};
        jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
        AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
        int value{castParam->get()};
        int controllerNumber{static_cast<int>(it->second)};
        
        fluid_synth_cc(
            synth.get(),
            channel,
            controllerNumber,
            value);
    }
}

bool FluidSynthModel::loadSoundFont(const String& requestedPath, const MemoryBlock& bookmark) {
    if (requestedPath.isEmpty() && bookmark.getSize() == 0) {
        resetPlayback();
        if (sfont_id >= 0) fluid_synth_sfunload(synth.get(), sfont_id, 0);
        sfont_id = -1;
#if JUCE_MAC || JUCE_IOS
        // Keep the previous scope until the old font has been destroyed by the engine.
        if (fileAccess) retiredFileAccess.push_back(std::move(fileAccess));
#endif
        refreshBanks();
        auto emptyState = valueTreeState.state.getChildWithName("soundFont");
        emptyState.setProperty("path", "", nullptr);
        emptyState.setProperty("bookmark", var(MemoryBlock()), nullptr);
        emptyState.setProperty("loadError", "", nullptr);
        return true;
    }
    auto state = valueTreeState.state.getChildWithName("soundFont");
    String path = requestedPath;
    MemoryBlock committedBookmark = bookmark;
#if JUCE_MAC || JUCE_IOS
    std::unique_ptr<JuicyMac::ScopedFileAccess> access;
    if (bookmark.getSize() > 0) {
        CFPtr<CFDataRef> data{CFDataCreate(nullptr, static_cast<const UInt8*>(bookmark.getData()),
            static_cast<CFIndex>(bookmark.getSize()))};
        CFErrorRef error = nullptr;
        Boolean stale = false;
        CFURLRef resolved = data ? CFURLCreateByResolvingBookmarkData(nullptr, data.get(),
            kCFURLBookmarkResolutionWithSecurityScope, nullptr, nullptr, &stale, &error) : nullptr;
        CFPtr<CFErrorRef> ownedError{error};
        access = std::make_unique<JuicyMac::ScopedFileAccess>(resolved);
        if (access->url) {
            CFPtr<CFStringRef> cfPath{CFURLCopyFileSystemPath(access->url.get(), kCFURLPOSIXPathStyle)};
            if (cfPath) path = String::fromCFString(cfPath.get());
        } else {
            access.reset();
            committedBookmark.reset();
        }
    }
#endif
    bool loaded = path.isNotEmpty() && replaceFont(path);
    if (!loaded && path != requestedPath && requestedPath.isNotEmpty()) {
        // A stale bookmark must not hide a valid saved/relocated path.
        path = requestedPath;
        committedBookmark.reset();
#if JUCE_MAC || JUCE_IOS
        access.reset();
#endif
        loaded = replaceFont(path);
    }
    if (!loaded) {
        if (sfont_id < 0) {
            // A fresh project with missing media must retain its reference for relocation.
            state.setProperty("path", requestedPath, nullptr);
            state.setProperty("bookmark", var(bookmark), nullptr);
        }
        state.setProperty("loadError", "Could not load SoundFont: " + requestedPath
            + ". Check that the file exists and is a valid SF2/SF3 bank. The previous bank is kept when available.", nullptr);
        return false;
    }
#if JUCE_MAC || JUCE_IOS
    // Old voices/fonts may be freed later by FluidSynth. Retain their security
    // scopes until the engine is rebuilt or destroyed, not merely until sfload returns.
    if (fileAccess) retiredFileAccess.push_back(std::move(fileAccess));
    fileAccess = std::move(access);
#endif
    state.setProperty("path", path, nullptr);
    state.setProperty("bookmark", var(committedBookmark), nullptr);
    state.setProperty("loadError", "", nullptr);
    return true;
}

String FluidSynthModel::getLoadError() const {
    return valueTreeState.state.getChildWithName("soundFont").getProperty("loadError", "");
}

void FluidSynthModel::publishCurrentSelection() {
    int font = -1, bank = 0, program = 0;
    if (fluid_synth_get_program(synth.get(), channel, &font, &bank, &program) != FLUID_OK) return;
    // Suppress only this same-thread feedback path. A GUI/host request on a
    // different thread is not silently swallowed by a process-wide guard.
    const juce::ScopedValueSetter<FluidSynthModel*> guard(publishingSelection, this);
    *static_cast<AudioParameterInt*>(valueTreeState.getParameter("bank")) = bank;
    *static_cast<AudioParameterInt*>(valueTreeState.getParameter("preset")) = program;
}

void FluidSynthModel::setControllerValue(int controller, int value) {
    fluid_synth_cc(
        synth.get(),
        channel,
        controller,
        value);
}

int FluidSynthModel::getChannel() {
    return channel;
}

bool FluidSynthModel::replaceFont(const String& path) {
    if (!synth) return false;
    // Do not reset channel presets or unload a working bank until the candidate
    // has loaded and a playable preset has been selected successfully.
    const int candidate = fluid_synth_sfload(synth.get(), path.toStdString().c_str(), 0);
    if (candidate < 0) return false;
    const int bank = static_cast<int>(valueTreeState.getRawParameterValue("bank")->load());
    const int program = static_cast<int>(valueTreeState.getRawParameterValue("preset")->load());
    int result = fluid_synth_program_select(synth.get(), channel, candidate, bank, program);
    if (result != FLUID_OK) {
        if (auto* font = fluid_synth_get_sfont_by_id(synth.get(), candidate)) {
            fluid_sfont_iteration_start(font);
            if (auto* first = fluid_sfont_iteration_next(font))
                result = fluid_synth_program_select(synth.get(), channel, candidate,
                    fluid_preset_get_banknum(first), fluid_preset_get_num(first));
        }
    }
    if (result != FLUID_OK) {
        fluid_synth_sfunload(synth.get(), candidate, 0);
        return false;
    }
    const int previous = sfont_id;
    sfont_id = candidate;
    if (previous >= 0) fluid_synth_sfunload(synth.get(), previous, 0);
    publishCurrentSelection();
    refreshBanks();
    return true;
}

void FluidSynthModel::refreshBanks() {
    ValueTree banks{"banks"};
    fluid_sfont_t* sfont{
        sfont_id == -1
        ? nullptr
        : fluid_synth_get_sfont_by_id(synth.get(), sfont_id)
    };
    if (sfont) {
        int greatestEncounteredBank{-1};
        ValueTree bank;

        fluid_sfont_iteration_start(sfont);
        for(fluid_preset_t* preset {fluid_sfont_iteration_next(sfont)};
        preset != nullptr;
        preset = fluid_sfont_iteration_next(sfont)) {
            int bankNum{fluid_preset_get_banknum(preset)};
            if (bankNum > greatestEncounteredBank) {
                if (greatestEncounteredBank > -1) {
                    banks.appendChild(bank, nullptr);
                }
                bank = { "bank", {
                    { "num", bankNum }
                } };
                greatestEncounteredBank = bankNum;
            }
            bank.appendChild({ "preset", {
                { "num", fluid_preset_get_num(preset) },
                { "name", String{fluid_preset_get_name(preset)} }
            }, {} }, nullptr);
        }
        if (greatestEncounteredBank > -1) {
            banks.appendChild(bank, nullptr);
        }
    }
    valueTreeState.state.getChildWithName("banks").copyPropertiesAndChildrenFrom(banks, nullptr);
    valueTreeState.state.getChildWithName("banks").sendPropertyChangeMessage("synthetic");
    
#if JUCE_DEBUG
//    unique_ptr<XmlElement> xml{valueTreeState.state.createXml()};
//    Logger::outputDebugString(xml->createDocument("",false,false));
#endif
}

void FluidSynthModel::setSampleRate(float sampleRate) {
    if (currentSampleRate == sampleRate) return;
    currentSampleRate = sampleRate;
    // https://stackoverflow.com/a/40856043/5257399
    // test if a smart pointer is null
    if (!synth) {
        // don't worry; we'll do this in initialise phase regardless
        return;
    }
    // FluidSynth >= 2.4.4 requires the rate at construction. prepareToPlay()
    // calls this while audio is stopped; preserve the loaded font and parameters.
    String path;
    if (sfont_id >= 0)
        if (auto* font = fluid_synth_get_sfont_by_id(synth.get(), sfont_id))
            path = String::fromUTF8(fluid_sfont_get_name(font));
    const auto savedChannel = captureChannel();
    synth.reset();
#if JUCE_MAC || JUCE_IOS
    retiredFileAccess.clear();
#endif
    settings.reset();
    sfont_id = -1;
    initialise();
    if (path.isNotEmpty()) replaceFont(path);
    restoreChannel(savedChannel, false);
    for (const auto& entry : paramToController)
        parameterChanged(entry.first, 0.0f);
    if (sfont_id >= 0) parameterChanged("preset", 0.0f);
}

FluidSynthModel::ChannelSnapshot FluidSynthModel::captureChannel() {
    ChannelSnapshot state;
    for (int cc = 0; cc < 120; ++cc)
        fluid_synth_get_cc(synth.get(), channel, cc, &state.controllers[static_cast<size_t>(cc)]);
    for (int gen = 0; gen < GEN_LAST; ++gen)
        state.generators[static_cast<size_t>(gen)] = fluid_synth_get_gen(synth.get(), channel, gen);
    fluid_synth_get_pitch_bend(synth.get(), channel, &state.pitch);
    fluid_synth_get_pitch_wheel_sens(synth.get(), channel, &state.pitchSensitivity);
    return state;
}

void FluidSynthModel::restoreChannel(const ChannelSnapshot& state, bool releasePedals) {
    // Restore ordinary controllers, never replay channel-mode commands or
    // RPN/NRPN data-entry transactions in numeric CC order.
    for (int cc = 0; cc < 120; ++cc) {
        if (cc == 6 || cc == 38 || (cc >= 96 && cc <= 101)) continue;
        const bool pedal = cc == 64 || cc == 66 || cc == 69;
        fluid_synth_cc(synth.get(), channel, cc,
            releasePedals && pedal ? 0 : state.controllers[static_cast<size_t>(cc)]);
    }
    for (int gen = 0; gen < GEN_LAST; ++gen)
        fluid_synth_set_gen(synth.get(), channel, gen, state.generators[static_cast<size_t>(gen)]);
    fluid_synth_pitch_wheel_sens(synth.get(), channel, state.pitchSensitivity);
    fluid_synth_pitch_bend(synth.get(), channel, state.pitch);
    // Leave data entry deselected. Complete tuning/SysEx and in-progress RPN
    // transactions need the future channel-state model; do not invent them here.
    fluid_synth_cc(synth.get(), channel, 101, 127);
    fluid_synth_cc(synth.get(), channel, 100, 127);
}

void FluidSynthModel::resetPlayback() {
    if (!synth) return;
    const auto savedChannel = captureChannel();
    fluid_synth_system_reset(synth.get());
    // Voice-off ramps may feed the effects during the first render quantum.
    // Finish that ramp (plus any cached fragment), then clear the effects again.
    fluid_synth_process(synth.get(), 128, 0, nullptr, 0, nullptr);
    fluid_synth_system_reset(synth.get());
    restoreChannel(savedChannel, true);
    if (sfont_id >= 0) parameterChanged("preset", 0.0f);
    // Drain the bounded reset/mixer pipeline without exposing pre-reset audio.
    // No allocation, bank I/O or synth reconstruction; output mixing is skipped.
    fluid_synth_process(synth.get(), 256, 0, nullptr, 0, nullptr);
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    int rendered = 0;
    const auto renderUntil = [&](int end) {
        const int count = end - rendered;
        if (count <= 0) return;
        if (sfont_id < 0) {
            // Empty projects are silent, including any residual effect output.
            // Advance the engine so stale buffers cannot return on a later load.
            fluid_synth_process(synth.get(), count, 0, nullptr, 0, nullptr);
            rendered = end;
            return;
        }
        if (buffer.getNumChannels() == 1) {
            // FluidSynth requires stereo pairs. A fixed scratch block avoids
            // allocations for mono hosts, including oversized host blocks.
            float left[64], right[64];
            for (int offset = 0; offset < count; offset += 64) {
                const int length = juce::jmin(64, count - offset);
                std::fill_n(left, length, 0.0f);
                std::fill_n(right, length, 0.0f);
                float* pair[] = {left, right};
                // Aliasing wet and dry buffers is supported by FluidSynth;
                // the effects channels wrap into this single stereo pair.
                fluid_synth_process(synth.get(), length, 2, pair, 2, pair);
                auto* mono = buffer.getWritePointer(0, rendered + offset);
                for (int i = 0; i < length; ++i) mono[i] += 0.5f * (left[i] + right[i]);
            }
        } else {
            float* outputs[] = {buffer.getWritePointer(0, rendered),
                                buffer.getWritePointer(1, rendered)};
            fluid_synth_process(synth.get(), count, 2, outputs, 2, outputs);
        }
        rendered = end;
    };
    for (const auto metadata : midiMessages) {
        const auto m = metadata.getMessage();
        renderUntil(juce::jlimit(rendered, buffer.getNumSamples(), metadata.samplePosition));
        DEBUG_PRINT(m.getDescription());
        
        if (m.isNoteOn()) {
            fluid_synth_noteon(
                synth.get(),
                channel,
                m.getNoteNumber(),
                m.getVelocity());
        } else if (m.isNoteOff()) {
            fluid_synth_noteoff(
                synth.get(),
                channel,
                m.getNoteNumber());
        } else if (m.isController()) {
            fluid_synth_cc(
                synth.get(),
                channel,
                m.getControllerNumber(),
                m.getControllerValue());

            fluid_midi_control_change controllerNum{static_cast<fluid_midi_control_change>(m.getControllerNumber())};
            if (auto it{controllerToParam.find(controllerNum)};
                it != end(controllerToParam)) {
                String parameterID{it->second};
                RangedAudioParameter *param{valueTreeState.getParameter(parameterID)};
                jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
                AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
                *castParam = m.getControllerValue();
            }
        } else if (m.isProgramChange()) {
#if JUCE_DEBUG
            String debug{"MIDI program change: "};
            debug << m.getProgramChangeNumber();
            Logger::outputDebugString(debug);
#endif
            int result{fluid_synth_program_change(
                synth.get(),
                channel,
                m.getProgramChangeNumber())};
            if (result == FLUID_OK) publishCurrentSelection();
        } else if (m.isPitchWheel()) {
            fluid_synth_pitch_bend(
                synth.get(),
                channel,
                m.getPitchWheelValue());
        } else if (m.isChannelPressure()) {
            fluid_synth_channel_pressure(
                synth.get(),
                channel,
                m.getChannelPressureValue());
        } else if (m.isAftertouch()) {
            fluid_synth_key_pressure(
                synth.get(),
                channel,
                m.getNoteNumber(),
                m.getAfterTouchValue());
//        } else if (m.isMetaEvent()) {
//            fluid_midi_event_t *midi_event{new_fluid_midi_event()};
//            fluid_midi_event_set_type(midi_event, static_cast<int>(MIDI_SYSTEM_RESET));
//            fluid_synth_handle_midi_event(synth.get(), midi_event);
//            delete_fluid_midi_event(midi_event);
        } else if (m.isSysEx()) {
            fluid_synth_sysex(
                synth.get(),
                reinterpret_cast<const char*>(m.getSysExData()),
                m.getSysExDataSize(),
                nullptr, // no response pointer because we have no interest in handling response currently
                nullptr, // no response_len pointer because we have no interest in handling response currently
                nullptr, // no handled pointer because we have no interest in handling response currently
                static_cast<int>(false));
        }
    }

    // fluid_synth_get_cc(fluidSynth, 0, 73, &pval);
    // Logger::outputDebugString ( juce::String::formatted("hey: %d\n", pval) );

    renderUntil(buffer.getNumSamples());
}

int FluidSynthModel::getNumPrograms()
{
    return 128;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int FluidSynthModel::getCurrentProgram()
{
    RangedAudioParameter *param{valueTreeState.getParameter("preset")};
    jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
    AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
    return castParam->get();
}

void FluidSynthModel::setCurrentProgram(int index)
{
    RangedAudioParameter *param{valueTreeState.getParameter("preset")};
    jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
    AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
    // GUI components poll parameter atomics on their own message-thread timer.
    // Never acquire a message-thread lock from a host/audio callback.
    *castParam = index;
}

const String FluidSynthModel::getProgramName(int index)
{
     fluid_sfont_t* sfont{
         sfont_id == -1
         ? nullptr
         : fluid_synth_get_sfont_by_id(synth.get(), sfont_id)
     };
     if (!sfont) {
         String presetName{"Preset "};
         return presetName << index;
     }
     RangedAudioParameter *param{valueTreeState.getParameter("bank")};
     jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
     AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
     int bank{castParam->get()};
    
     fluid_preset_t *preset{fluid_sfont_get_preset(
         sfont,
         bank,
         index)};
     if (!preset) {
         String presetName{"Preset "};
         return presetName << index;
     }
     return {fluid_preset_get_name(preset)};
}

void FluidSynthModel::changeProgramName(int index, const String& newName)
{
    // no-op; we don't support modifying the soundfont, so let's not support modification of preset names.
}
