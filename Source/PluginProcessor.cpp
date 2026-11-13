/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "MidiConstants.h"
#include "Util.h"
#include "GuiConstants.h"
#include "BookmarkCodec.h"
#include <limits>

using namespace std;


AudioProcessor* JUCE_CALLTYPE createPluginFilter();


//==============================================================================
JuicySFAudioProcessor::JuicySFAudioProcessor()
: AudioProcessor{getBusesProperties()}
, valueTreeState{
    *this,
    nullptr,
    "MYPLUGINSETTINGS",
    createParameterLayout()}
, fluidSynthModel{valueTreeState}
{
    MemoryBlock bookmarkBuffer;
    valueTreeState.state.appendChild({ "uiState", {
            { "width", GuiConstants::minWidth },
            { "height", GuiConstants::minHeight }
        }, {} }, nullptr);
    valueTreeState.state.appendChild({ "soundFont", {
        { "path", "" },
        { "bookmark", std::move(bookmarkBuffer) },
    }, {} }, nullptr);
    // no properties, no subtrees (yet)
    valueTreeState.state.appendChild({ "banks", {}, {} }, nullptr);

    initialiseSynth();
    startTimerHz(30); // Host state notifications must also work with the editor closed.
}

AudioProcessorValueTreeState::ParameterLayout JuicySFAudioProcessor::createParameterLayout() {
    // https://stackoverflow.com/a/8469002/5257399
    unique_ptr<RangedAudioParameter> params[] {
        // SoundFont 2.4 spec section 7.2: zero through 127, or 128.
        make_unique<AudioParameterInt>("bank", "which bank is selected in the soundfont", MidiConstants::midiMinValue, 128, MidiConstants::midiMinValue, "Bank" ),
        // note: banks may be sparse, and lack a 0th preset. so defend against this.
        make_unique<AudioParameterInt>("preset", "which patch (aka patch, program, instrument) is selected in the soundfont", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Preset" ),
        make_unique<AudioParameterInt>("attack", "volume envelope attack time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "A" ),
        make_unique<AudioParameterInt>("decay", "Volume envelope decay time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "D" ),
        make_unique<AudioParameterInt>("sustain", "Volume envelope sustain attenuation", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "S" ),
        make_unique<AudioParameterInt>("release", "volume envelope release time", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "R" ),
        make_unique<AudioParameterInt>("filterCutOff", "low-pass filter cut-off frequency", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Cut" ),
        make_unique<AudioParameterInt>("filterResonance", "low-pass filter resonance attentuation", MidiConstants::midiMinValue, MidiConstants::midiMaxValue, MidiConstants::midiMinValue, "Res" ),
        make_unique<juce::AudioParameterFloat>("outputGain", "Output level",
            juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), -12.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")),
    };

    AudioProcessorValueTreeState::ParameterLayout layout{
        make_move_iterator(begin(params)), make_move_iterator(end(params))};
    for (int part = 0; part < PartParameters::count; ++part) {
        const auto add = [&](const String& name, const String& label, int maximum, int initial) {
            layout.add(make_unique<AudioParameterInt>(PartParameters::id(part, name),
                "Part " + String(part + 1) + " " + label, 0, maximum, initial));
        };
        if (part > 0) {
            add("bank", "Bank", 128, part == 9 ? 128 : 0);
            add("preset", "Program", 127, 0);
        }
        for (const auto& control : PartParameters::controls) {
            if (part == 0 && control.cc >= 71) continue; // Original six controls already exist.
            add(control.name, control.label, 127, control.initial);
        }
        add("pitchBend", "Pitch bend", 16383, 8192);
        add("pressure", "Channel pressure", 127, 0);
        add("bendRange", "Pitch bend range", 127, 2);
    }
    return layout;
}

JuicySFAudioProcessor::~JuicySFAudioProcessor()
{
    stopTimer();
}

void JuicySFAudioProcessor::timerCallback() {
    if (fluidSynthModel.consumeHostStateChange())
        updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
}

void JuicySFAudioProcessor::initialiseSynth() {
    fluidSynthModel.initialise();
}

//==============================================================================
const String JuicySFAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool JuicySFAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool JuicySFAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

double JuicySFAudioProcessor::getTailLengthSeconds() const
{
    // Bank envelopes and effects cannot be bounded by a short fixed constant.
    // JUCE's VST3 wrapper maps infinity to kInfiniteTail. Hosts may require an
    // explicit offline-render end/tail length rather than automatic detection.
    return std::numeric_limits<double>::infinity();
}

int JuicySFAudioProcessor::getNumPrograms()
{
    return fluidSynthModel.getNumPrograms();   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int JuicySFAudioProcessor::getCurrentProgram()
{
    return fluidSynthModel.getCurrentProgram();
}

void JuicySFAudioProcessor::setCurrentProgram(int index)
{
    fluidSynthModel.setCurrentProgram(index);
}

const String JuicySFAudioProcessor::getProgramName(int index)
{
    return fluidSynthModel.getProgramName(index);
}

void JuicySFAudioProcessor::changeProgramName (int index, const String& newName)
{
}

//==============================================================================
void JuicySFAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    synth.setCurrentPlaybackSampleRate (sampleRate);
    keyboardState.reset();
    fluidSynthModel.setSampleRate(static_cast<float>(sampleRate));
    outputLevel.reset(sampleRate, 0.02);
    outputLevel.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        valueTreeState.getRawParameterValue("outputGain")->load()));
    reset();
}

void JuicySFAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
    reset();
}

void JuicySFAudioProcessor::reset()
{
    keyboardState.reset();
    fluidSynthModel.resetPlayback();
}

bool JuicySFAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Only mono/stereo and input/output must have same layout
    const AudioChannelSet& mainOutput = layouts.getMainOutputChannelSet();
    const AudioChannelSet& mainInput  = layouts.getMainInputChannelSet();

    // input and output layout must either be the same or the input must be disabled altogether
    if (! mainInput.isDisabled() && mainInput != mainOutput)
        return false;

    // do not allow disabling the main buses
    if (mainOutput.isDisabled())
        return false;

    // only allow stereo and mono
    return mainOutput.size() <= 2;
}

AudioProcessor::BusesProperties JuicySFAudioProcessor::getBusesProperties() {
    return BusesProperties()
            .withOutput ("Output", AudioChannelSet::stereo(), true);
}

void JuicySFAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    jassert (!isUsingDoublePrecision());

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (int i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Now pass any incoming midi messages to our keyboard state object, and let it
    // add messages to the buffer if the user is clicking on the on-screen keys
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    fluidSynthModel.processBlock(buffer, midiMessages);
    outputLevel.setTargetValue(juce::Decibels::decibelsToGain(
        valueTreeState.getRawParameterValue("outputGain")->load()));
    for (int frame = 0; frame < buffer.getNumSamples(); ++frame) {
        const float gain = outputLevel.getNextValue();
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, frame, buffer.getSample(channel, frame) * gain);
    }

    // and now get our synth to process these midi events and generate its output.
    // synth.renderNextBlock(buffer, midiMessages, 0, numSamples);

    // (see juce_VST3_Wrapper.cpp for the assertion this would trip otherwise)
    // we are !JucePlugin_ProducesMidiOutput, so clear remaining MIDI messages from our buffer
    midiMessages.clear();
}

//==============================================================================
bool JuicySFAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

AudioProcessorEditor* JuicySFAudioProcessor::createEditor()
{
    // grab a raw pointer to it for our own use
    return /*pluginEditor = */new JuicySFAudioProcessorEditor (*this, valueTreeState);
}

//==============================================================================
void JuicySFAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    // Create an outer XML element..
    XmlElement xml{"MYPLUGINSETTINGS"};
    xml.setAttribute("partFormat", 1);
    xml.setAttribute("routing", fluidSynthModel.isLegacyOmni() ? "legacyOmni" : "sixteen");

    // Store the values of all our parameters, using their param ID as the XML attribute
    const auto fontState = fluidSynthModel.getSoundFontState();

    XmlElement* params{xml.createNewChildElement("params")};
    for (auto* param : getParameters()) {
         if (auto* p = dynamic_cast<AudioProcessorParameterWithID*> (param)) {
             float value = p->getValue();
             for (int part = 0; part < PartParameters::count; ++part) {
                 const auto selection = fluidSynthModel.getBankProgram(part);
                 if (p->paramID == PartParameters::id(part, "bank")) value = static_cast<float>(selection >> 16) / 128.0f;
                 if (p->paramID == PartParameters::id(part, "preset")) value = static_cast<float>(selection & 0xffffu) / 127.0f;
             }
             params->setAttribute(p->paramID, value);
         }
    }
    const auto size = getEditorSize();
    auto* ui = xml.createNewChildElement("uiState");
    ui->setAttribute("width", size.first);
    ui->setAttribute("height", size.second);
    ui->setAttribute("part", fluidSynthModel.getChannel());
    auto* font = xml.createNewChildElement("soundFont");
    font->setAttribute("path", fontState.path);
    font->setAttribute("bookmark", fontState.bookmark.toBase64Encoding());

    auto* midi = xml.createNewChildElement("partControllers");
    for (int part = 0; part < PartParameters::count; ++part) {
        auto* channel = midi->createNewChildElement("part");
        channel->setAttribute("index", part);
        const auto state = fluidSynthModel.getPartState(part);
        for (int cc = 0; cc < 120; ++cc)
            if (state.controllers[static_cast<size_t>(cc)] >= 0 && !PartParameters::isTransaction(cc))
                channel->setAttribute("cc" + String(cc), state.controllers[static_cast<size_t>(cc)]);
    }
    DEBUG_PRINT(xml.createDocument("",false,false));

    copyXmlToBinary(xml, destData);
}

void JuicySFAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
    // This getXmlFromBinary() helper function retrieves our XML from the binary blob..
    if (data == nullptr || sizeInBytes <= 0) return;
    auto xmlState = getXmlFromBinary(data, sizeInBytes);
    if (xmlState) { DEBUG_PRINT(xmlState->createDocument("",false,false)); }

    if (xmlState.get() != nullptr) {
        // make sure that it's actually our type of XML object..
        if (xmlState->hasTagName(valueTreeState.state.getType())) {
            if (xmlState->getIntAttribute("partFormat", 0) < 0 || xmlState->getIntAttribute("partFormat", 0) > 1) return;
            {
                XmlElement* xmlElement{xmlState->getChildByName("soundFont")};
                if (xmlElement) {
                    MemoryBlock bookmark;
                    if (!BookmarkCodec::decode(xmlElement->getStringAttribute("bookmark"), bookmark))
                        return; // Reject the entire invalid state before changing bank, parameters or UI.
                    if (!fluidSynthModel.loadSoundFont(xmlElement->getStringAttribute("path"), bookmark)
                        && fluidSynthModel.hasLoadedSoundFont())
                        return; // A failed project-bank replacement must preserve the working parameters too.
                }
            }
            fluidSynthModel.setLegacyOmni(xmlState->getStringAttribute("routing") != "sixteen");
            fluidSynthModel.clearExtraControllers();
            if (auto* ui = xmlState->getChildByName("uiState")) {
                fluidSynthModel.setSelectedPart(fluidSynthModel.isLegacyOmni() ? 0 : ui->getIntAttribute("part", 0));
                const auto size = getEditorSize();
                setEditorSize(ui->getIntAttribute("width", size.first),
                              ui->getIntAttribute("height", size.second));
            }
            XmlElement* params{xmlState->getChildByName("params")};
            if (params) {
                for (auto* param : getParameters()) {
                    if (auto* p = dynamic_cast<AudioProcessorParameterWithID*>(param)) {
                        // Old previews had unity output with no outputGain field.
                        // Preserve their balance while new instances start at -12 dB.
                        const bool oldPartParameter = p->paramID == "bank" || p->paramID == "preset"
                            || p->paramID == "attack" || p->paramID == "decay" || p->paramID == "sustain"
                            || p->paramID == "release" || p->paramID == "filterCutOff" || p->paramID == "filterResonance";
                        const float fallback = p->paramID == "outputGain"
                            ? valueTreeState.getParameter("outputGain")->convertTo0to1(0.0f)
                            : oldPartParameter ? p->getValue() : p->getDefaultValue();
                        p->setValueNotifyingHost(static_cast<float>(params->getDoubleAttribute(p->paramID, fallback)));
                    }
                }
            }
            if (auto* midi = xmlState->getChildByName("partControllers")) {
                for (const auto* channel : midi->getChildIterator()) {
                    const int part = channel->getIntAttribute("index", -1);
                    if (!channel->hasTagName("part") || part < 0 || part >= PartParameters::count) continue;
                    FluidSynthModel::PartState state; state.controllers.fill(-1);
                    for (int cc = 0; cc < 120; ++cc) {
                        const int value = channel->getIntAttribute("cc" + String(cc), -1);
                        if (value >= 0 && value <= 127) state.controllers[static_cast<size_t>(cc)] = value;
                    }
                    fluidSynthModel.restorePartState(part, state);
                }
            }
        }
    }
}

// FluidSynth only supports float in its process function, so that's all we can support.
bool JuicySFAudioProcessor::supportsDoublePrecisionProcessing() const {
    return false;
}

FluidSynthModel& JuicySFAudioProcessor::getFluidSynthModel() {
    return fluidSynthModel;
}

//==============================================================================
// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new JuicySFAudioProcessor();
}

std::pair<int, int> JuicySFAudioProcessor::getEditorSize() const {
    const auto size = editorSize.load();
    return {static_cast<int>(size >> 32), static_cast<int>(size & 0xffffffffu)};
}
void JuicySFAudioProcessor::setEditorSize(int width, int height) {
    width = juce::jlimit(GuiConstants::minWidth, GuiConstants::maxWidth, width);
    height = juce::jlimit(GuiConstants::minHeight, GuiConstants::maxHeight, height);
    editorSize.store((uint64_t(width) << 32) | uint64_t(height));
}
