#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    int failures = 0;
    const auto check = [&](bool ok, const char* description) {
        std::cout << (ok ? "PASS " : "FAIL ") << description << std::endl;
        if (!ok) ++failures;
    };
    JuicySFAudioProcessor processor;
    processor.prepareToPlay(44100.0, 2048);
    processor.setStateInformation(nullptr, 0);
    const char invalid[] = "this is not a JUCE state";
    processor.setStateInformation(invalid, sizeof(invalid));
    MemoryBlock saved;
    processor.getStateInformation(saved);
    auto xml = AudioProcessor::getXmlFromBinary(saved.getData(), static_cast<int>(saved.getSize()));
    check(xml && xml->hasTagName("MYPLUGINSETTINGS"), "invalid state ignored; original state format retained");
    processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    MemoryBlock restored;
    processor.getStateInformation(restored);
    check(saved == restored, "default project state round trip");

    if (argc != 2) {
        check(false, "generated SoundFont fixture argument required");
        return failures;
    }
    auto* font = xml->getChildByName("soundFont");
    font->setAttribute("path", String(argv[1]));
    font->setAttribute("bookmark", "");
    MemoryBlock state;
    AudioProcessor::copyXmlToBinary(*xml, state);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    AudioBuffer<float> audio(2, 2048);
    audio.clear();
    MidiBuffer midi;
    midi.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 512);
    midi.addEvent(MidiMessage::noteOff(1, 69), 1536);
    processor.processBlock(audio, midi);
    double before = 0, after = 0;
    bool finite = true;
    for (int ch = 0; ch < 2; ++ch) {
        for (int i = 0; i < audio.getNumSamples(); ++i) {
            const double sample = audio.getSample(ch, i);
            finite = finite && std::isfinite(sample);
            (i < 512 ? before : after) += sample * sample;
        }
    }
    check(finite, "all rendered samples finite");
    check(before < 1.0e-12, "no audio before the scheduled note-on");
    check(after > 1.0e-8, "SoundFont loads and renders after note-on");
    check(midi.isEmpty(), "instrument does not forward consumed MIDI");
    // The same loaded font must survive host sample-rate changes. Counting
    // crossings catches a synth silently left at 44100 Hz in a 48/96 kHz host.
    for (const double rate : {48000.0, 96000.0, 44100.0}) {
        processor.prepareToPlay(rate, 8192);
        AudioBuffer<float> pitched(2, 8192);
        MidiBuffer notes;
        notes.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        processor.processBlock(pitched, notes);
        processor.processBlock(pitched, notes);
        int crossings = 0;
        for (int i = 1; i < pitched.getNumSamples(); ++i)
            if (pitched.getSample(0, i - 1) <= 0 && pitched.getSample(0, i) > 0) ++crossings;
        const double frequency = crossings * rate / pitched.getNumSamples();
        std::cout << "Host rate " << rate << ": measured " << frequency << " Hz\n";
        check(std::abs(frequency - 440.0) < 15.0, "font and pitch preserved across host sample-rate change");
    }
    return failures == 0 ? 0 : 1;
}
