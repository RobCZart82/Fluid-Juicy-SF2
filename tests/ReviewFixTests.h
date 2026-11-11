#pragma once
#include "../Source/BookmarkCodec.h"
#include "../Source/SoundFontFolders.h"
#include "../Source/SoundFontLibrary.h"

inline int runReviewFixTests(const String& fixture) {
    int failures = 0;
    const auto check = [&](bool ok, const char* description) {
        std::cout << (ok ? "PASS " : "FAIL ") << description << std::endl;
        if (!ok) ++failures;
    };
    const auto save = [](JuicySFAudioProcessor& target) {
        MemoryBlock state;
        target.getStateInformation(state);
        return AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    };
    const auto restore = [](JuicySFAudioProcessor& target, const XmlElement& xml) {
        MemoryBlock state;
        AudioProcessor::copyXmlToBinary(xml, state);
        target.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    };
    const auto wait = [](const std::function<bool()>& ready) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (!ready() && juce::Time::getMillisecondCounterHiRes() < deadline) juce::Thread::sleep(1);
        return ready();
    };
    {
        JuicySFAudioProcessor target;
        target.prepareToPlay(44100, 64);
        auto& model = target.getFluidSynthModel();
        check(model.loadSoundFont(fixture, {}), "restore-during-fade fixture loads");
        target.prepareToPlay(44100, 64);
        const auto original = save(target);
        const auto shifted = File(fixture).getSiblingFile("test-shifted.sf2").getFullPathName();
        AudioBuffer<float> block(2, 64);
        MidiBuffer midi;
        for (int i = 0; i < 16; ++i) {
            model.requestSoundFont(shifted, {});
            check(wait([&] { return !model.isLoading(); }), "GUI replacement prepares before restore");
            target.processBlock(block, midi); // Start a GUI crossfade.
            restore(target, *original);
            const auto expected = model.getLatestRequestForTests();
            target.processBlock(block, midi);
            check(model.getActiveRequestForTests() == expected && (model.getBankProgram() >> 16) == 0,
                  "host restore adopts its engine on the very next block during a prior crossfade");
        }
        check(model.getAudioReclaimsForTests() == 0,
              "rapid host restores defer all engine destruction off the render stack");
        const auto before = save(target)->toString();
        for (const String malformed : {"8388608.", "-1.", "99999999999999999.", "12.A", "1.!?", "1.AAAA"}) {
            auto state = save(target);
            state->getChildByName("soundFont")->setAttribute("bookmark", malformed);
            state->getChildByName("params")->setAttribute("preset", 1.0);
            restore(target, *state);
            check(save(target)->toString() == before,
                  "malformed bookmark rejects the whole state and preserves the current bank and parameters");
        }
    }
    {
        MemoryBlock bytes;
        for (int i = 0; i < 1024; ++i) {
            const auto value = static_cast<unsigned char>(i % 256);
            bytes.append(&value, 1);
        }
        MemoryBlock decoded;
        check(BookmarkCodec::decode(bytes.toBase64Encoding(), decoded) && decoded == bytes,
              "existing JUCE bookmark byte encodings retain their exact data");
        check(BookmarkCodec::decode("", decoded) && decoded.getSize() == 0
                  && BookmarkCodec::decode("0.", decoded), "both historical empty bookmark encodings remain valid");
        MemoryBlock large(BookmarkCodec::maxBytes, true);
        check(BookmarkCodec::decode(large.toBase64Encoding(), decoded) && decoded == large,
              "valid bookmarks at the documented limit decode successfully");
    }
    {
        juce::TemporaryFile temporary(".xml");
        const auto settings = temporary.getFile();
        SoundFontFolders folders(settings);
        check(folders.add(settings.getParentDirectory(), {}).wasOk(), "bounded-settings fixture is saved");
        MemoryBlock before;
        check(settings.loadFileAsData(before), "initial folder settings bytes read");
        const auto name = "FluidJuicySF2Folders" + String(settings.getFullPathName().hashCode64());
        juce::ChildProcess holder;
        const auto executable = File::getSpecialLocation(File::currentExecutableFile).getFullPathName();
        check(holder.start(StringArray{executable, "--hold-folder-lock", name}), "other process starts for settings contention");
        char reply[5]{};
        check(holder.readProcessOutput(reply, 5) == 5 && String::fromUTF8(reply, 5) == "READY",
              "other process holds the settings lock before the test proceeds");
        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto blocked = folders.read();
        check(blocked.error.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() - start < 1200.0,
              "settings lock contention returns a bounded busy error");
        std::atomic<bool> editStarted{false}, callbackCalled{false};
        SoundFontLibrary scanner(settings);
        scanner.scan([&](auto) { callbackCalled.store(true); }, [&](const auto& cancelled) {
            editStarted.store(true);
            return folders.remove(settings.getParentDirectory(), cancelled);
        });
        check(wait([&] { return editStarted.load(); }), "background edit reaches the held lock");
        const auto cancelStart = juce::Time::getMillisecondCounterHiRes();
        scanner.cancel();
        check(juce::Time::getMillisecondCounterHiRes() - cancelStart < 1000.0 && !callbackCalled.load(),
              "scanner cancellation interrupts lock contention without a late UI callback");
        holder.waitForProcessToFinish(3000);
        MemoryBlock after;
        check(settings.loadFileAsData(after) && after == before && folders.read().error.isEmpty(),
              "failed or cancelled edits preserve settings and later reads recover");
    }
    {
        JuicySFAudioProcessor fresh, legacy;
        check(fresh.getParameters().getLast()->getName(32) == "Output level"
                  && fresh.getParameters().getFirst()->getName(64).contains("bank"),
              "output parameter is appended after all existing parameter indices");
        auto oldState = save(fresh);
        oldState->getChildByName("params")->removeAttribute("outputGain");
        restore(legacy, *oldState);
        for (auto* target : {&fresh, &legacy}) {
            target->prepareToPlay(44100, 1024);
            check(target->getFluidSynthModel().loadSoundFont(fixture, {}), "output-gain fixture loads");
        }
        const auto energy = [](JuicySFAudioProcessor& target) {
            AudioBuffer<float> block(2, 1024);
            MidiBuffer events;
            events.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
            double sum = 0;
            for (int j = 0; j < 8; ++j) {
                target.processBlock(block, events);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block.getNumSamples(); ++i) {
                        const double sample = block.getSample(c, i);
                        sum += sample * sample;
                    }
            }
            return sum;
        };
        const double quieter = energy(fresh), historical = energy(legacy);
        check(historical > 0 && std::abs(10 * std::log10(quieter / historical) + 12.0) < 0.05,
              "new instances leave 12 dB headroom while old project states retain unity output");
        const auto level = save(fresh)->getChildByName("params")->getDoubleAttribute("outputGain");
        restore(legacy, *save(fresh));
        check(std::abs(save(legacy)->getChildByName("params")->getDoubleAttribute("outputGain") - level) < 1.0e-6,
              "explicit output gain round-trips through project state");
    }
    return failures;
}
