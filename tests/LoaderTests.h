// Deterministic ownership/lifetime regressions for the SoundFont loader.
#pragma once
#include <atomic>
#include <thread>

inline int runLoaderTests(const String& path) {
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::cout << (ok ? "PASS " : "FAIL ") << text << std::endl;
        if (!ok) ++failures;
    };
    const auto energy = [](const AudioBuffer<float>& block) {
        double result = 0;
        for (int c = 0; c < block.getNumChannels(); ++c)
            for (int i = 0; i < block.getNumSamples(); ++i) {
                const double value = block.getSample(c, i);
                result += value * value;
            }
        return result;
    };
    const auto wait = [&](const std::function<bool()>& done) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (!done() && juce::Time::getMillisecondCounterHiRes() < deadline) juce::Thread::sleep(1);
        return done();
    };
    const auto save = [](JuicySFAudioProcessor& p) {
        MemoryBlock data;
        p.getStateInformation(data);
        return AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize()));
    };
    const auto restore = [](JuicySFAudioProcessor& p, const XmlElement& xml) {
        MemoryBlock data;
        AudioProcessor::copyXmlToBinary(xml, data);
        p.setStateInformation(data.getData(), static_cast<int>(data.getSize()));
    };
    juce::TemporaryFile replacement(".sf2");
    check(File(path).copyFileTo(replacement.getFile()), "loader replacement fixture copied");
    const auto missing = File(path).getSiblingFile("missing-loader-test.sf2").getFullPathName();
    {
        JuicySFAudioProcessor p;
        p.prepareToPlay(44100, 256);
        auto& model = p.getFluidSynthModel();
        check(model.loadSoundFont(path, MemoryBlock()), "initial synchronous host load prepares a bank");
        AudioBuffer<float> block(2, 256);
        MidiBuffer events;
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        p.processBlock(block, events);
        const auto initialRequest = model.getActiveRequestForTests();
        model.pauseLoadsForTests(true);
        model.requestSoundFont(replacement.getFile().getFullPathName(), MemoryBlock());
        check(model.isLoading(), "GUI request returns while the loader is deliberately paused");
        bool uninterrupted = true;
        for (int i = 0; i < 100; ++i) {
            p.processBlock(block, events);
            uninterrupted = uninterrupted && std::isfinite(energy(block)) && energy(block) > 1.0e-5;
        }
        check(uninterrupted && model.getActiveRequestForTests() == initialRequest,
              "one hundred audio blocks keep the old voice while bank I/O is paused");
        check(model.getSoundFontState().path == path, "incomplete load does not replace the committed project path");
        model.pauseLoadsForTests(false);
        check(wait([&] { return !model.isLoading(); }), "background candidate completes");
        check(wait([&] {
            p.processBlock(block, events);
            return model.getActiveRequestForTests() == model.getLatestRequestForTests();
        }), "prepared engine is adopted at an audio boundary without waiting for reclamation");
        check(model.getSoundFontState().path == replacement.getFile().getFullPathName(),
              "successful background replacement commits its path");
        check(energy(block) > 1.0e-5, "engine handover fades the old voice instead of abruptly cutting it");
        for (int i = 0; i < 5; ++i) p.processBlock(block, events);
        check(energy(block) < 1.0e-10, "the old engine fade finishes without leaving a held voice");
        events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        p.processBlock(block, events);
        check(energy(block) > 1.0e-5, "new engine plays after handover");
        const auto working = model.getActiveRequestForTests();
        model.requestSoundFont(missing, MemoryBlock());
        check(wait([&] { return !model.isLoading(); }), "failed background load completes");
        p.processBlock(block, events);
        check(model.getActiveRequestForTests() == working && energy(block) > 1.0e-5
                  && model.getLoadError().isNotEmpty(), "failed background load preserves the playing engine");
        check(model.loadSoundFont("", MemoryBlock()) && model.loadSoundFont(path, MemoryBlock()),
              "unload followed by reload can supersede an unadopted empty candidate");
        check(wait([&] {
            p.processBlock(block, events);
            return model.getActiveRequestForTests() == model.getLatestRequestForTests();
        }), "reload following an unobserved unload is adopted");
        check(energy(block) == 0, "an unload epoch prevents an earlier held voice from fading back on reload");
        model.pauseLoadsForTests(true);
        model.requestSoundFont(path, MemoryBlock());
        model.requestSoundFont(missing, MemoryBlock());
        model.requestSoundFont("", MemoryBlock());
        model.pauseLoadsForTests(false);
        check(wait([&] { return !model.isLoading(); }), "latest unload request completes after coalescing replacements");
        check(wait([&] {
            p.processBlock(block, events);
            return model.getActiveRequestForTests() == model.getLatestRequestForTests();
        }), "latest unload wins over superseded candidates");
        check(!model.hasLoadedSoundFont() && model.getSoundFontState().path.isEmpty() && energy(block) == 0,
              "late load results cannot resurrect the unloaded bank");
        check(model.getAudioReclaimsForTests() == 0, "engine reclamation never runs on the render stack");
    }
    {
        JuicySFAudioProcessor p;
        p.prepareToPlay(44100, 128);
        auto& model = p.getFluidSynthModel();
        check(model.loadSoundFont(path, MemoryBlock()), "concurrent restore fixture loads");
        std::unique_ptr<AudioProcessorEditor> editor(p.createEditor());
        std::function<juce::ComboBox*(Component&)> find = [&](Component& component) -> juce::ComboBox* {
            if (auto* box = dynamic_cast<juce::ComboBox*>(&component))
                if (box->getTitle() == "SoundFont bank") return box;
            for (auto* child : component.getChildren()) if (auto* box = find(*child)) return box;
            return nullptr;
        };
        auto* box = find(*editor);
        check(box && box->getNumItems() == 2, "open editor starts with the loaded immutable metadata");
        auto populated = save(p);
        JuicySFAudioProcessor blank;
        auto empty = save(blank);
        empty->getChildByName("uiState")->setAttribute("width", 720);
        empty->getChildByName("uiState")->setAttribute("height", 450);
        std::atomic<bool> running{true}, audioFinite{true};
        std::atomic<unsigned int> blocks{0};
        std::thread audio([&] {
            AudioBuffer<float> block(2, 128);
            unsigned int iteration = 0;
            while (running.load()) {
                MidiBuffer midi;
                midi.addEvent(MidiMessage::programChange(1, static_cast<int>(iteration % 2)), 0);
                midi.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 17);
                midi.addEvent(MidiMessage::noteOff(1, 69), 100);
                p.processBlock(block, midi);
                if (!std::isfinite(energy(block))) audioFinite.store(false);
                ++blocks;
                ++iteration;
                std::this_thread::yield();
            }
        });
        check(wait([&] { return blocks.load() >= 128; }), "parallel render workload starts before restore");
        std::thread worker([&] { restore(p, *empty); });
        worker.join();
        check(box && box->getNumItems() == 2 && editor->getWidth() == 575,
              "worker restore cannot synchronously mutate the open bank chooser or editor dimensions");
        model.synchroniseUi();
        check(box && box->getNumItems() == 0, "message-thread publication clears the bank chooser");
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1500;
        while (editor->getWidth() != 720 && juce::Time::getMillisecondCounterHiRes() < deadline) {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep(5);
        }
        check(editor->getWidth() == 720 && editor->getHeight() == 450,
              "message-thread timer applies restored editor size coherently");
        std::atomic<bool> restored{false}, namesValid{true};
        std::thread host([&] {
            for (int i = 0; i < 16; ++i) {
                restore(p, i % 3 == 0 ? *empty : *populated);
                for (int n = 0; n < 8; ++n)
                    if (p.getProgramName(n).isEmpty()) namesValid.store(false);
                auto snapshot = save(p);
                if (!snapshot || !snapshot->getChildByName("soundFont")) namesValid.store(false);
            }
            restored.store(true);
        });
        while (!restored.load()) {
            model.synchroniseUi();
            juce::Timer::callPendingTimersSynchronously();
            for (int n = 0; n < 8; ++n) if (p.getProgramName(n).isEmpty()) namesValid.store(false);
            juce::Thread::sleep(1);
        }
        host.join();
        running.store(false);
        audio.join();
        model.synchroniseUi();
        check(blocks.load() > 100 && audioFinite.load() && namesValid.load(),
              "audio, worker state restore, GUI delivery and host program queries remain valid concurrently");
        check(model.getAudioReclaimsForTests() == 0 && model.getUiViolationsForTests() == 0,
              "concurrent restore neither reclaims on audio nor publishes UI on a worker");
    }
    {
        JuicySFAudioProcessor p;
        p.prepareToPlay(44100, 256);
        auto& model = p.getFluidSynthModel();
        model.pauseLoadsForTests(true);
        model.requestSoundFont(path, MemoryBlock());
        std::thread release([&] { juce::Thread::sleep(20); model.pauseLoadsForTests(false); });
        p.prepareToPlay(48000, 256); // Audio is stopped during a host lifecycle call.
        release.join();
        check(wait([&] { return !model.isLoading(); }), "load and stopped sample-rate change complete");
        AudioBuffer<float> block(2, 8192);
        MidiBuffer midi;
        midi.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        p.processBlock(block, midi);
        p.processBlock(block, midi);
        int crossings = 0;
        for (int i = 1; i < block.getNumSamples(); ++i)
            if (block.getSample(0, i - 1) <= 0 && block.getSample(0, i) > 0) ++crossings;
        check(std::abs(crossings * 48000.0 / 8192.0 - 440.0) < 15,
              "candidate built around a sample-rate change renders at the host rate");
    }
    {
        juce::TemporaryFile moved(".sf2");
        check(File(path).copyFileTo(moved.getFile()), "rate-reload recovery fixture copied");
        JuicySFAudioProcessor p;
        p.prepareToPlay(44100, 256);
        auto& model = p.getFluidSynthModel();
        check(model.loadSoundFont(moved.getFile().getFullPathName(), MemoryBlock()), "temporary bank loads before removal");
        AudioBuffer<float> block(2, 256);
        MidiBuffer midi;
        midi.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        p.processBlock(block, midi);
        check(moved.getFile().deleteFile(), "bank file is removed after loading");
        p.prepareToPlay(48000, 256);
        p.processBlock(block, midi);
        check(!model.hasLoadedSoundFont() && energy(block) == 0 && model.getLoadError().isNotEmpty()
                  && model.getSoundFontState().path == moved.getFile().getFullPathName(),
              "failed rate reload reports missing media and retains its recovery reference");
        check(model.loadSoundFont(path, MemoryBlock()), "missing rate-reload bank can be relocated");
        p.processBlock(block, midi);
        midi.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        p.processBlock(block, midi);
        check(energy(block) > 1.0e-5, "relocated bank plays at the new host rate");
    }
    {
        JuicySFAudioProcessor p;
        p.prepareToPlay(44100, 256);
        auto& model = p.getFluidSynthModel();
        check(model.loadSoundFont(path, MemoryBlock()), "sparse handover fixture loads");
        AudioBuffer<float> block(2, 256);
        MidiBuffer midi;
        midi.addEvent(MidiMessage::controllerEvent(1, 0, 8), 0);
        midi.addEvent(MidiMessage::programChange(1, 1), 0);
        p.processBlock(block, midi);
        const auto shifted = File(path).getSiblingFile("test-shifted.sf2").getFullPathName();
        model.requestSoundFont(shifted, MemoryBlock());
        check(wait([&] { return !model.isLoading(); }), "different bank layout is prepared");
        // Old-engine MIDI can still arrive while retirement backpressure delays
        // handover. The new engine must resolve that intent to its own metadata.
        check(wait([&] {
            midi.addEvent(MidiMessage::controllerEvent(1, 0, 8), 0);
            midi.addEvent(MidiMessage::programChange(1, 1), 0);
            p.processBlock(block, midi);
            return model.getActiveRequestForTests() == model.getLatestRequestForTests();
        }), "different bank layout is adopted despite previous-bank MIDI");
        p.processBlock(block, midi);
        check((model.getBankProgram() >> 16) == 5 && p.getCurrentProgram() == 0,
              "unsupported selection resolves to the new bank's first playable preset");
        midi.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
        p.processBlock(block, midi);
        check(energy(block) > 1.0e-5, "sparse-layout replacement remains audible");
    }
    for (int i = 0; i < 8; ++i) {
        auto p = std::make_unique<JuicySFAudioProcessor>();
        p->getFluidSynthModel().pauseLoadsForTests(true);
        p->getFluidSynthModel().requestSoundFont(path, MemoryBlock());
        check(wait([&] {
            return p->getFluidSynthModel().getStartedRequestForTests()
                == p->getFluidSynthModel().getLatestRequestForTests();
        }), "destruction fixture has an in-flight paused request");
        p.reset(); // Destruction cancels the paused job and joins its owner.
    }
    check(true, "destruction joins pending loaders without late publication or lifetime failures");
    return failures;
}
