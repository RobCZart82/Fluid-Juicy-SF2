#pragma once

// A host-facing listener verifies persisted, non-parameter edits without an editor.
inline int runHostStateTests(const String& path) {
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::cout << (ok ? "PASS " : "FAIL ") << text << std::endl;
        if (!ok) ++failures;
    };
    struct Listener final : juce::AudioProcessorListener {
        int changes = 0;
        bool wrongThread = false;
        String lastPath;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor* processor, const ChangeDetails& details) override {
            if (!details.nonParameterStateChanged) return;
            ++changes;
            wrongThread |= !juce::MessageManager::getInstance()->isThisTheMessageThread();
            // Hosts can ask for state inside this callback: no model lock may be held.
            MemoryBlock state;
            processor->getStateInformation(state);
            const auto xml = AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
            lastPath = xml->getChildByName("soundFont")->getStringAttribute("path");
        }
    } listener;
    JuicySFAudioProcessor processor;
    processor.prepareToPlay(44100, 64);
    processor.addListener(&listener);
    auto& model = processor.getFluidSynthModel();
    const auto flush = [](const std::function<bool()>& delivered = {}) {
        // The console harness has no host loop. Pump until expected delivery,
        // while allowing the JUCE timer thread to run on a busy CI machine.
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + (delivered ? 1500.0 : 100.0);
        do {
            juce::Timer::callPendingTimersSynchronously();
            juce::Thread::sleep(5);
        } while (juce::Time::getMillisecondCounterHiRes() < deadline && (!delivered || !delivered()));
    };
    const auto waitForLoad = [&] {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (model.isLoading() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::sleep(1);
        return !model.isLoading();
    };
    flush();
    check(listener.changes == 0, "new processor does not report an unsaved bank edit");
    model.pauseLoadsForTests(true);
    model.requestSoundFont(path, {});
    flush();
    check(listener.changes == 0, "queued bank preparation does not prematurely dirty the host project");
    model.pauseLoadsForTests(false);
    check(waitForLoad() && model.hasLoadedSoundFont(), "host-notification fixture loads in the background");
    AudioBuffer<float> block(2, 64);
    MidiBuffer midi;
    processor.processBlock(block, midi);
    check(listener.changes == 0, "loader and audio callbacks do not deliver host state notifications");
    flush([&] { return listener.changes >= 1; });
    check(listener.changes == 1 && listener.lastPath == path && !listener.wrongThread,
          "committed GUI bank edit notifies on the message thread with the editor closed");
    flush();
    check(listener.changes == 1, "a bank edit emits no repeated notifications on later timer ticks");
    model.requestSoundFont(path, {});
    check(waitForLoad(), "same-reference GUI reload completes");
    flush();
    check(listener.changes == 1, "reloading an unchanged reference does not dirty the project");
    model.requestSoundFont(File(path).getSiblingFile("missing-host-state.sf2").getFullPathName(), {});
    check(waitForLoad() && model.getLoadError().isNotEmpty(), "failed replacement finishes");
    flush();
    check(listener.changes == 1, "failed replacement preserving a working bank does not dirty the project");
    juce::TemporaryFile moved(".sf2");
    check(File(path).copyFileTo(moved.getFile()), "host-notification relocation fixture copied");
    model.requestSoundFont(moved.getFile().getFullPathName(), {});
    check(waitForLoad(), "replacement reference prepares");
    flush([&] { return listener.changes >= 2; });
    check(listener.changes == 2 && listener.lastPath == moved.getFile().getFullPathName(),
          "successful same-preset bank relocation requests host state saving");
    processor.prepareToPlay(48000, 64);
    flush();
    check(listener.changes == 2, "host sample-rate preparation does not report a user edit");
    model.requestSoundFont(path, {});
    check(waitForLoad(), "GUI change prepares before host recall");
    // Host restoration wins over a GUI change not yet delivered to the host.
    check(model.loadSoundFont(moved.getFile().getFullPathName(), {}), "synchronous recall succeeds");
    flush();
    check(listener.changes == 2, "host recall suppresses a superseded pending GUI notification");
    MemoryBlock state;
    processor.getStateInformation(state);
    processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    flush();
    check(listener.changes == 2, "ordinary project state recall does not dirty the restored project");
    model.requestSoundFont("", {});
    check(waitForLoad(), "GUI bank clearing completes");
    flush([&] { return listener.changes >= 3; });
    check(listener.changes == 3 && listener.lastPath.isEmpty(), "clearing a bank requests host state saving once");
    model.requestSoundFont(path, {});
    check(waitForLoad(), "first coalesced edit prepares");
    model.requestSoundFont(moved.getFile().getFullPathName(), {});
    check(waitForLoad(), "latest coalesced edit prepares");
    flush([&] { return listener.changes >= 4; });
    check(listener.changes == 4 && listener.lastPath == moved.getFile().getFullPathName(),
          "multiple completed GUI edits coalesce to the latest saved reference");
    processor.removeListener(&listener);
    {
        JuicySFAudioProcessor missing;
        missing.prepareToPlay(44100, 64);
        Listener recovered;
        missing.addListener(&recovered);
        auto xml = AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
        xml->getChildByName("soundFont")->setAttribute("path",
            File(path).getSiblingFile("missing-host-state.sf2").getFullPathName());
        MemoryBlock restore;
        AudioProcessor::copyXmlToBinary(*xml, restore);
        missing.setStateInformation(restore.getData(), static_cast<int>(restore.getSize()));
        flush();
        check(recovered.changes == 0, "missing-media project recall is not reported as a user edit");
        missing.getFluidSynthModel().requestSoundFont(path, {});
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (missing.getFluidSynthModel().isLoading() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::sleep(1);
        flush([&] { return recovered.changes >= 1; });
        check(recovered.changes == 1 && recovered.lastPath == path && !recovered.wrongThread,
              "recovering a missing project bank notifies the host to save the new path");
        missing.removeListener(&recovered);
    }
    return failures;
}
