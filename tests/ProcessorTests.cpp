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
    check(editor->getWidth() == 575 && editor->getHeight() == 345,
          "new editor opens at the 575 x 345 minimum");
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

    auto* uiState = xml->getChildByName("uiState");
    const auto restoreEditorSize = [&](int width, int height) {
        uiState->setAttribute("width", width);
        uiState->setAttribute("height", height);
        MemoryBlock editorState;
        AudioProcessor::copyXmlToBinary(*xml, editorState);
        // The console runner has no host message loop to deliver Value callbacks.
        // Reopen the editor to read the restored dimensions as a reopened project does.
        editor.reset();
        processor.setStateInformation(editorState.getData(), static_cast<int>(editorState.getSize()));
        editor.reset(processor.createEditor());
    };
    restoreEditorSize(500, 300);
    check(editor->getWidth() == 575 && editor->getHeight() == 345,
          "legacy editor size is raised to the new minimum");
    restoreEditorSize(720, 450);
    check(editor->getWidth() == 720 && editor->getHeight() == 450,
          "larger saved editor size is preserved");

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
    std::function<juce::ComboBox*(Component&)> findBankList = [&](Component& component) -> juce::ComboBox* {
        if (auto* list = dynamic_cast<juce::ComboBox*>(&component))
            if (list->getTitle() == "SoundFont bank")
                return list;
        for (auto* child : component.getChildren())
            if (auto* list = findBankList(*child))
                return list;
        return nullptr;
    };
    auto* bankList = findBankList(*editor);
    check(bankList != nullptr && bankList->getNumItems() == 2
              && bankList->getItemText(0) == "Bank 0" && bankList->getItemText(1) == "Bank 8",
          "bank dropdown lists the actual sparse bank numbers in order");
    if (bankList != nullptr) {
        editor->keyPressed(KeyPress(KeyPress::rightKey));
        check(bankList->getSelectedId() == 9, "right arrow advances from bank 0 to bank 8");
        editor->keyPressed(KeyPress(KeyPress::rightKey));
        check(bankList->getSelectedId() == 9, "right arrow stops at the last bank");
        editor->keyPressed(KeyPress(KeyPress::leftKey));
        check(bankList->getSelectedId() == 1, "left arrow returns to bank 0");
    }
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
    // Audit reliability regressions use fresh engines so prior voices/effects
    // cannot hide a failure. Assert public behavior, not internal API calls.
    const auto energy = [](const AudioBuffer<float>& block) {
        double sum = 0;
        for (int c = 0; c < block.getNumChannels(); ++c)
            for (int i = 0; i < block.getNumSamples(); ++i) {
                const double sample = block.getSample(c, i);
                sum += sample * sample;
            }
        return sum;
    };
    const auto loadFixture = [&](JuicySFAudioProcessor& target) {
        target.prepareToPlay(44100.0, 1024);
        check(target.getFluidSynthModel().loadSoundFont(originalPath, MemoryBlock()), "reliability fixture loads");
    };
    {
        JuicySFAudioProcessor mono;
        auto layout = mono.getBusesLayout();
        layout.outputBuses.set(0, AudioChannelSet::mono());
        check(mono.setBusesLayout(layout), "mono host layout is supported");
        loadFixture(mono);
        AudioBuffer<float> block(1, 4097); // larger than the prepare size and scratch quantum
        MidiBuffer events;
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 17);
        mono.processBlock(block, events);
        check(std::isfinite(energy(block)) && energy(block) > 1.0e-5,
              "mono renders finite audible output with an oversized odd block");
    }
    {
        JuicySFAudioProcessor resetTarget;
        loadFixture(resetTarget);
        AudioBuffer<float> block(2, 1024);
        MidiBuffer events;
        events.addEvent(MidiMessage::controllerEvent(1, 64, 127), 0);
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        resetTarget.processBlock(block, events);
        check(energy(block) > 1.0e-5, "reset fixture is initially audible");
        const auto playingEnergy = energy(block);
        resetTarget.reset();
        resetTarget.processBlock(block, events);
        check(energy(block) < playingEnergy * 0.01, "host reset stops the held voice immediately");
        // FluidSynth's effect reset may emit a brief mixer residual. It must
        // settle by the next block, rather than resuming the held loop forever.
        resetTarget.processBlock(block, events);
        check(energy(block) < 1.0e-10, "host reset leaves no sustained voice or effect tail");
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        resetTarget.processBlock(block, events);
        resetTarget.releaseResources();
        resetTarget.prepareToPlay(44100.0, 1024);
        resetTarget.processBlock(block, events);
        check(energy(block) < 1.0e-10, "release and same-rate prepare do not resume an old voice");
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        resetTarget.processBlock(block, events);
        check(energy(block) > 1.0e-5, "new notes still play after reset");
        JuicySFAudioProcessor blank;
        restoreXml(resetTarget, *saveXml(blank));
        auto cleared = saveXml(resetTarget);
        check(!resetTarget.getFluidSynthModel().hasLoadedSoundFont()
                  && cleared->getChildByName("soundFont")->getStringAttribute("path").isEmpty(),
              "blank project state unloads the working bank and clears its saved reference");
        resetTarget.processBlock(block, events);
        check(energy(block) < 1.0e-10, "blank project state leaves no old notes or effects");
    }
    {
        JuicySFAudioProcessor muted;
        loadFixture(muted);
        AudioBuffer<float> block(2, 1024);
        MidiBuffer events;
        events.addEvent(MidiMessage::controllerEvent(1, 7, 0), 0);
        muted.processBlock(block, events);
        for (const double rate : {48000.0, 96000.0, 44100.0}) {
            muted.prepareToPlay(rate, 1024);
            events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
            muted.processBlock(block, events);
            check(energy(block) < 1.0e-6, "CC7 mute survives a host sample-rate change");
        }
        events.addEvent(MidiMessage::controllerEvent(1, 7, 100), 0);
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        muted.processBlock(block, events);
        check(energy(block) > 1.0e-5, "CC7 can unmute the rebuilt synth");
    }
    {
        const auto effectTail = [&](int send) {
            JuicySFAudioProcessor fx;
            loadFixture(fx);
            AudioBuffer<float> block(2, 1024);
            MidiBuffer events;
            events.addEvent(MidiMessage::controllerEvent(1, 91, send), 0);
            events.addEvent(MidiMessage::controllerEvent(1, 93, send), 0);
            events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
            for (int i = 0; i < 24; ++i) fx.processBlock(block, events);
            events.addEvent(MidiMessage::controllerEvent(1, 120, 0), 0);
            fx.processBlock(block, events);
            double tail = 0;
            for (int i = 0; i < 8; ++i) { fx.processBlock(block, events); tail += energy(block); }
            return tail;
        };
        const double dry = effectTail(0);
        const double wet = effectTail(127);
        check(dry < 1.0e-10 && wet > 1.0e-5, "CC91/93 generate audible wet output rather than discarded effects");
        JuicySFAudioProcessor releaseTail;
        loadFixture(releaseTail);
        for (auto* parameter : releaseTail.getParameters())
            if (auto* id = dynamic_cast<AudioProcessorParameterWithID*>(parameter))
                if (id->paramID == "release") id->setValueNotifyingHost(1.0f);
        AudioBuffer<float> block(2, 1024);
        MidiBuffer events;
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        releaseTail.processBlock(block, events);
        events.addEvent(MidiMessage::noteOff(1, 69), 0);
        releaseTail.processBlock(block, events);
        releaseTail.processBlock(block, events);
        check(energy(block) > 1.0e-5 && releaseTail.getTailLengthSeconds() > 1.0,
              "host tail reporting accounts for a long audible release");
    }
    // FluidSynth 2.6.2 buffers 64 samples internally. Splitting the host block
    // does not make event dispatch sample-accurate. Lock down the documented
    // 0..63-sample quantization instead of claiming arbitrary-offset precision.
    for (const int hostBlock : {32, 127, 512}) {
        for (const int offset : {0, 1, 17, 63, 64, 65, 127, 257}) {
            JuicySFAudioProcessor timing;
            loadFixture(timing);
            int firstSignal = -1;
            for (int start = 0; start < 512; start += hostBlock) {
                const int count = juce::jmin(hostBlock, 512 - start);
                AudioBuffer<float> block(2, count);
                MidiBuffer events;
                if (offset >= start && offset < start + count)
                    events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), offset - start);
                timing.processBlock(block, events);
                for (int i = 0; i < count; ++i)
                    if (firstSignal < 0 && std::abs(block.getSample(0, i)) > 1.0e-8f)
                        firstSignal = start + i;
            }
            check(firstSignal >= offset + 1 && firstSignal <= offset + 64,
                  "unaligned MIDI note stays within the documented FluidSynth quantum across host blocks");
        }
    }
    return failures == 0 ? 0 : 1;
}
