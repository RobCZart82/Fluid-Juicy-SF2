#include "../Source/PluginProcessor.h"
#include <cmath>
#include <iostream>
#include <thread>
#include <functional>

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI initialise;
    int failures = 0;
    const auto check = [&](bool ok, const char* description) {
        std::cout << (ok ? "PASS " : "FAIL ") << description << std::endl;
        if (!ok) ++failures;
    };
    JuicySFAudioProcessor processor;
    processor.prepareToPlay(44100.0, 2048);
    std::unique_ptr<AudioProcessorEditor> editor{processor.createEditor()};
    editor->keyPressed(KeyPress(KeyPress::leftKey));
    editor->keyPressed(KeyPress(KeyPress::rightKey));
    check(processor.getCurrentProgram() == 0, "empty-bank navigation is safe before loading");
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
    // A real MIDI render on a worker must not synchronously mutate the open UI.
    std::function<TableListBox*(Component&)> findTable = [&](Component& component) -> TableListBox* {
        if (auto* result = dynamic_cast<TableListBox*>(&component)) return result;
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            if (auto* result = findTable(*component.getChildComponent(i))) return result;
        return nullptr;
    };
    auto* table = findTable(*editor);
    check(table != nullptr && table->getNumRows() == 2, "editor lists both original fixture presets");
    if (table != nullptr) {
        const int beforeSelection = table->getSelectedRow();
        std::thread render([&] {
            MidiBuffer events;
            events.addEvent(MidiMessage::programChange(1, 1), 0);
            events.addEvent(MidiMessage::controllerEvent(1, 74, 12), 0);
            AudioBuffer<float> block(2, 256);
            processor.processBlock(block, events);
        });
        render.join();
        check(processor.getCurrentProgram() == 1, "worker MIDI updates the processor program");
        check(table->getSelectedRow() == beforeSelection, "worker MIDI leaves the editor untouched until message-thread refresh");
        // A console regression runner has no host event loop. Pump timer
        // callbacks until delivery (bounded), rather than assuming one 80 ms
        // sleep also advances JUCE's internal timer-thread countdown.
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500.0;
        while (table->getSelectedRow() != 1 && juce::Time::getMillisecondCounterHiRes() < deadline) {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep(5);
        }
        check(table->getSelectedRow() == 1, "message-thread timer reflects the new program");
    }
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
    const auto saveXml = [](JuicySFAudioProcessor& target) {
        MemoryBlock data;
        target.getStateInformation(data);
        return AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize()));
    };
    const auto restoreXml = [](JuicySFAudioProcessor& target, const XmlElement& document) {
        MemoryBlock data;
        AudioProcessor::copyXmlToBinary(document, data);
        target.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    };
    JuicySFAudioProcessor bankProcessor;
    bankProcessor.prepareToPlay(44100.0, 8192);
    restoreXml(bankProcessor, *xml);
    const auto selectMidiBank = [&](int bank, int program) {
        MidiBuffer events;
        events.addEvent(MidiMessage::controllerEvent(1, 0, bank), 0);
        events.addEvent(MidiMessage::programChange(1, program), 0);
        AudioBuffer<float> block(2, 64);
        bankProcessor.processBlock(block, events);
    };
    const auto checkFrequency = [&](JuicySFAudioProcessor& target, double expected, const char* description) {
        AudioBuffer<float> block(2, 8192);
        MidiBuffer events;
        events.addEvent(MidiMessage::controllerEvent(1, 120, 0), 0);
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        target.processBlock(block, events);
        target.processBlock(block, events);
        int crossings = 0;
        for (int i = 1; i < block.getNumSamples(); ++i)
            if (block.getSample(0, i - 1) <= 0 && block.getSample(0, i) > 0) ++crossings;
        const double measured = crossings * 44100.0 / block.getNumSamples();
        std::cout << description << ": measured " << measured << " Hz\n";
        check(std::abs(measured - expected) < 15.0, description);
    };
    selectMidiBank(8, 1); // Previously the preset callback silently restored bank 0.
    check(bankProcessor.getProgramName(1) == "Octave 2", "MIDI bank and preset are reflected in saved/UI selection");
    checkFrequency(bankProcessor, 880.0, "Bank Select survives a different Program Change");
    selectMidiBank(0, 1);
    checkFrequency(bankProcessor, 440.0, "same-program MIDI bank change returns to bank zero");
    selectMidiBank(8, 1);
    checkFrequency(bankProcessor, 880.0, "same-program MIDI bank change selects the octave bank");
    auto selectedState = saveXml(bankProcessor);
    JuicySFAudioProcessor recalled;
    recalled.prepareToPlay(44100.0, 8192);
    restoreXml(recalled, *selectedState);
    checkFrequency(recalled, 880.0, "saved MIDI bank/program survive project recall");

    const String originalPath = selectedState->getChildByName("soundFont")->getStringAttribute("path");
    const juce::File original(originalPath);
    const auto missing = original.getSiblingFile("definitely-missing-juicy-regression.sf2");
    check(!bankProcessor.getFluidSynthModel().loadSoundFont(missing.getFullPathName(), MemoryBlock()),
          "missing replacement reports failure");
    check(bankProcessor.getFluidSynthModel().getLoadError().isNotEmpty(), "missing replacement provides a user-readable error");
    check(saveXml(bankProcessor)->getChildByName("soundFont")->getStringAttribute("path") == originalPath,
          "missing replacement preserves the saved working bank reference");
    checkFrequency(bankProcessor, 880.0, "missing replacement leaves the working bank audible");

    juce::TemporaryFile corrupt(".sf2");
    check(corrupt.getFile().replaceWithText("not a SoundFont"), "corrupt replacement fixture created");
    check(!bankProcessor.getFluidSynthModel().loadSoundFont(corrupt.getFile().getFullPathName(), MemoryBlock()),
          "corrupt replacement reports failure");
    checkFrequency(bankProcessor, 880.0, "corrupt replacement preserves bank and program");
    auto brokenProject = saveXml(bankProcessor);
    brokenProject->getChildByName("soundFont")->setAttribute("path", missing.getFullPathName());
    brokenProject->getChildByName("params")->setAttribute("bank", 0.0);
    brokenProject->getChildByName("params")->setAttribute("preset", 0.0);
    restoreXml(bankProcessor, *brokenProject);
    checkFrequency(bankProcessor, 880.0, "failed project-bank replacement preserves the working selection");

    juce::TemporaryFile replacement(".sf2");
    check(original.copyFileTo(replacement.getFile()), "valid replacement fixture copied");
    check(bankProcessor.getFluidSynthModel().loadSoundFont(replacement.getFile().getFullPathName(), MemoryBlock()),
          "valid replacement commits successfully");
    check(bankProcessor.getFluidSynthModel().getLoadError().isEmpty(), "successful replacement clears the earlier error");
    check(saveXml(bankProcessor)->getChildByName("soundFont")->getStringAttribute("path") == replacement.getFile().getFullPathName(),
          "successful replacement commits its new saved bank reference");
    checkFrequency(bankProcessor, 880.0, "successful replacement retains the selected bank/program");

    JuicySFAudioProcessor missingRecall;
    missingRecall.prepareToPlay(44100.0, 8192);
    restoreXml(missingRecall, *brokenProject);
    check(saveXml(missingRecall)->getChildByName("soundFont")->getStringAttribute("path") == missing.getFullPathName(),
          "fresh missing-media recall retains its reference for relocation");
    return failures == 0 ? 0 : 1;
}
