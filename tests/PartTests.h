#pragma once
#include "../Source/PluginEditor.h"

inline int runPartTests(const String& fixture) {
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::cout << (ok ? "PASS " : "FAIL ") << text << std::endl;
        if (!ok) ++failures;
    };
    const auto parameter = [](JuicySFAudioProcessor& processor, int part, const String& name) -> RangedAudioParameter* {
        for (auto* item : processor.getParameters())
            if (auto* value = dynamic_cast<RangedAudioParameter*>(item))
                if (value->getParameterID() == PartParameters::id(part, name)) return value;
        return nullptr;
    };
    const auto set = [&](JuicySFAudioProcessor& p, int part, const String& name, float value) {
        auto* item = parameter(p, part, name);
        if (item) item->setValueNotifyingHost(item->convertTo0to1(value));
        else check(false, "part parameter exists");
    };
    const auto get = [&](JuicySFAudioProcessor& p, int part, const String& name) {
        auto* item = parameter(p, part, name);
        return item ? item->convertFrom0to1(item->getValue()) : -1.0f;
    };
    const auto render = [](JuicySFAudioProcessor& p, MidiBuffer events) {
        AudioBuffer<float> warm(2, 1024), block(2, 8192);
        p.processBlock(warm, events);
        MidiBuffer empty;
        p.processBlock(block, empty);
        return block;
    };
    const auto amplitude = [](const AudioBuffer<float>& block, double frequency) {
        double re = 0, im = 0;
        for (int i = 0; i < block.getNumSamples(); ++i) {
            const double phase = juce::MathConstants<double>::twoPi * frequency * i / 44100.0;
            const double value = (block.getSample(0, i) + block.getSample(1, i)) * 0.5;
            re += value * std::cos(phase); im += value * std::sin(phase);
        }
        return 2 * std::sqrt(re * re + im * im) / block.getNumSamples();
    };
    const auto energy = [](const AudioBuffer<float>& block, int channel) {
        double result = 0;
        for (int i = 0; i < block.getNumSamples(); ++i) {
            const double value = block.getSample(channel, i); result += value * value;
        }
        return result;
    };
    const auto save = [](JuicySFAudioProcessor& p) {
        MemoryBlock state; p.getStateInformation(state);
        return AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
    };
    const auto restore = [](JuicySFAudioProcessor& p, const XmlElement& xml) {
        MemoryBlock state; AudioProcessor::copyXmlToBinary(xml, state);
        p.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    };
    const auto wait = [](FluidSynthModel& model) {
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
        while (model.isLoading() && juce::Time::getMillisecondCounterHiRes() < deadline) juce::Thread::sleep(1);
        return !model.isLoading();
    };
    JuicySFAudioProcessor p;
    p.prepareToPlay(44100, 8192);
    auto& model = p.getFluidSynthModel();
    check(!model.isLegacyOmni() && model.loadSoundFont(fixture, {}), "new instances use one shared bank in 16-part mode");
    MidiBuffer notes;
    notes.addEvent(MidiMessage::controllerEvent(2, 0, 8), 0);
    notes.addEvent(MidiMessage::programChange(2, 1), 0);
    notes.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
    notes.addEvent(MidiMessage::noteOn(2, 69, static_cast<juce::uint8>(100)), 0);
    const auto both = render(p, notes);
    const double low = amplitude(both, 440), high = amplitude(both, 880);
    check(low > 0.001 && high > 0.001 && model.getBankProgram(0) == 0 && model.getBankProgram(1) == ((8u << 16) | 1u),
          "simultaneous MIDI channels render distinct bank/program fingerprints");
    MidiBuffer off;
    off.addEvent(MidiMessage::noteOff(1, 69), 0);
    const auto onlySecond = render(p, off);
    check(amplitude(onlySecond, 440) < low * 0.02 && amplitude(onlySecond, 880) > high * 0.7,
          "note-off on channel 1 leaves the same held note on channel 2 playing");
    p.reset();
    MidiBuffer volume;
    volume.addEvent(MidiMessage::controllerEvent(1, 7, 0), 0);
    volume.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
    volume.addEvent(MidiMessage::noteOn(2, 69, static_cast<juce::uint8>(100)), 0);
    const auto mixed = render(p, volume);
    check(amplitude(mixed, 440) < low * 0.02 && amplitude(mixed, 880) > high * 0.7
              && get(p, 0, "volume") < 0.1f && get(p, 1, "volume") > 99.0f,
          "CC7 changes one part's level without velocity edits or affecting another part");
    p.reset(); set(p, 0, "volume", 100); set(p, 1, "expression", 0);
    const auto expression = render(p, notes);
    check(amplitude(expression, 440) > low * 0.7 && amplitude(expression, 880) < high * 0.02,
          "automated expression is independent for each part");
    p.reset(); set(p, 1, "expression", 127); set(p, 0, "pan", 0);
    MidiBuffer left;
    left.addEvent(MidiMessage::controllerEvent(1, 91, 0), 0);
    left.addEvent(MidiMessage::controllerEvent(1, 93, 0), 0);
    left.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
    const auto panned = render(p, left);
    check(energy(panned, 0) > 0.01 && energy(panned, 1) < energy(panned, 0) * 0.01,
          "per-part pan controls the stereo position using CC10");
    p.reset(); set(p, 0, "pan", 64); set(p, 0, "pitchBend", 16383);
    const auto bent = render(p, notes);
    check(amplitude(bent, 493.88) > low * 0.5 && amplitude(bent, 880) > high * 0.7,
          "pitch bend on part 1 does not retune part 2");
    p.reset(); set(p, 0, "pitchBend", 8192);
    MidiBuffer pressure = notes;
    pressure.addEvent(MidiMessage::channelPressureChange(1, 127), 0);
    const auto pressured = render(p, pressure);
    check(amplitude(pressured, 440) < low * 0.02 && amplitude(pressured, 880) > high * 0.7,
          "channel pressure modulation remains local to the addressed MIDI part");
    p.reset(); set(p, 0, "pressure", 0);
    MidiBuffer sustain = notes;
    sustain.addEvent(MidiMessage::controllerEvent(1, 64, 127), 0);
    render(p, sustain);
    MidiBuffer releases;
    releases.addEvent(MidiMessage::noteOff(1, 69), 0);
    releases.addEvent(MidiMessage::noteOff(2, 69), 0);
    const auto held = render(p, releases);
    check(amplitude(held, 440) > low * 0.7 && amplitude(held, 880) < high * 0.02,
          "sustain pedal on part 1 does not hold released notes on part 2");
    p.reset();
    MidiBuffer drums;
    drums.addEvent(MidiMessage::noteOn(10, 69, static_cast<juce::uint8>(100)), 0);
    const auto drumAudio = render(p, drums);
    check(model.getBankProgram(9) == (128u << 16) && energy(drumAudio, 0) > 0.01,
          "MIDI channel 10 defaults to a playable SF2 percussion-bank preset");
    MidiBuffer stopOther;
    stopOther.addEvent(MidiMessage::controllerEvent(1, 120, 0), 0);
    check(energy(render(p, stopOther), 0) > 0.01, "all-sound-off on part 1 does not stop part 10");
    MidiBuffer stopDrums;
    stopDrums.addEvent(MidiMessage::controllerEvent(10, 120, 0), 0);
    check(energy(render(p, stopDrums), 0) < 0.00001, "all-sound-off reaches the addressed drum part");
    p.reset();
    MidiBuffer largeRange;
    largeRange.addEvent(MidiMessage::controllerEvent(3, 101, 0), 0);
    largeRange.addEvent(MidiMessage::controllerEvent(3, 100, 0), 0);
    largeRange.addEvent(MidiMessage::controllerEvent(3, 6, 127), 0);
    render(p, largeRange);
    check(get(p, 2, "bendRange") <= 72.0f, "RPN bend sensitivity respects FluidSynth's supported 72-semitone API limit");
    bool distinct = true;
    for (int part = 0; part < PartParameters::count; ++part) {
        set(p, part, "volume", static_cast<float>(60 + part));
        set(p, part, "pan", static_cast<float>(part * 8));
        set(p, part, "expression", static_cast<float>(100 + part));
        set(p, part, "modulation", static_cast<float>(part));
        set(p, part, "bendRange", static_cast<float>(part + 1));
        distinct &= parameter(p, part, "volume") != nullptr;
    }
    MidiBuffer extra;
    extra.addEvent(MidiMessage::controllerEvent(16, 91, 33), 0);
    extra.addEvent(MidiMessage::controllerEvent(2, 101, 0), 0);
    extra.addEvent(MidiMessage::controllerEvent(2, 100, 0), 0);
    extra.addEvent(MidiMessage::controllerEvent(2, 6, 12), 0);
    render(p, extra);
    model.setSelectedPart(15);
    const auto saved = save(p);
    JuicySFAudioProcessor reopened;
    reopened.prepareToPlay(44100, 8192);
    restore(reopened, *saved);
    MidiBuffer empty; render(reopened, empty);
    for (int part = 0; part < PartParameters::count; ++part) {
        distinct &= std::abs(get(reopened, part, "volume") - (60 + part)) < 0.01f;
        distinct &= std::abs(get(reopened, part, "pan") - part * 8) < 0.01f;
        distinct &= model.getBankProgram(part) == reopened.getFluidSynthModel().getBankProgram(part);
    }
    check(distinct && reopened.getFluidSynthModel().getChannel() == 15 && !reopened.getFluidSynthModel().isLegacyOmni(),
          "all 16 parts retain independent mix, bank/program and selected-part state on project recall");
    check(reopened.getFluidSynthModel().getPartState(15).controllers[91] == 33
              && get(reopened, 1, "bendRange") > 11.9f && get(reopened, 0, "bendRange") < 1.1f,
          "effect-send CC and channel-local RPN bend sensitivity survive state recall");
    p.prepareToPlay(48000, 8192);
    bool preserved = true;
    for (int part = 0; part < PartParameters::count; ++part)
        preserved &= model.getBankProgram(part) == reopened.getFluidSynthModel().getBankProgram(part);
    check(preserved && model.getPartState(15).controllers[91] == 33, "rate rebuild preserves all part selections and live controllers");
    model.requestSoundFont(fixture, {});
    check(wait(model), "shared-bank replacement prepares for all parts");
    render(p, empty);
    p.reset();
    check(model.getBankProgram(1) == ((8u << 16) | 1u) && model.getAudioReclaimsForTests() == 0,
          "bank handover and reset retain part selections without render-thread reclamation");
    {
        std::unique_ptr<AudioProcessorEditor> editor(reopened.createEditor());
        std::function<Component*(Component&, const String&)> find = [&](Component& root, const String& id) -> Component* {
            if (root.getComponentID() == id) return &root;
            for (auto* child : root.getChildren()) if (auto* result = find(*child, id)) return result;
            return nullptr;
        };
        auto* selector = dynamic_cast<juce::ComboBox*>(find(*editor, "partSelector"));
        check(selector && selector->getNumItems() == 16 && selector->getSelectedId() == 16,
              "editor restores its 16-part selector without changing any part's preset");
        if (selector) selector->setSelectedId(2, juce::sendNotificationSync);
        auto* slider = dynamic_cast<Slider*>(find(*editor, "part2.volume"));
        check(reopened.getFluidSynthModel().getChannel() == 1 && slider != nullptr,
              "part selector and 16-row MIDI mixer bind independent part controls");
        if (slider) slider->setValue(42, juce::sendNotificationSync);
        check(std::abs(get(reopened, 1, "volume") - 42) < 0.01f && std::abs(get(reopened, 0, "volume") - 60) < 0.01f,
              "editing a mixer row updates only that part's automatable volume");
    }
    p.prepareToPlay(44100, 8192);
    bool everyChannel = true;
    for (int part = 0; part < PartParameters::count; ++part) {
        p.reset();
        set(p, part, "bank", part == 9 ? 128 : (part % 2) * 8);
        set(p, part, "preset", 0); set(p, part, "volume", 100); set(p, part, "expression", 127);
        set(p, part, "pan", 64); set(p, part, "pressure", 0); set(p, part, "pitchBend", 8192);
        MidiBuffer one;
        one.addEvent(MidiMessage::controllerEvent(part + 1, 91, 0), 0);
        one.addEvent(MidiMessage::controllerEvent(part + 1, 93, 0), 0);
        one.addEvent(MidiMessage::noteOn(part + 1, 69, static_cast<juce::uint8>(100)), 0);
        const double frequency = part != 9 && part % 2 ? 880.0 : 440.0;
        const double before = amplitude(render(p, one), frequency);
        MidiBuffer quiet;
        quiet.addEvent(MidiMessage::controllerEvent(part + 1, 7, 0), 0);
        const double after = amplitude(render(p, quiet), frequency);
        everyChannel &= before > 0.001 && after < before * 0.02;
    }
    check(everyChannel, "all 16 MIDI channel numbers reach their own preset and volume controller");
    p.reset();
    MidiBuffer orchestra;
    for (int part = 0; part < PartParameters::count; ++part) {
        set(p, part, "volume", 60);
        orchestra.addEvent(MidiMessage::noteOn(part + 1, 60 + part, static_cast<juce::uint8>(90)), 0);
    }
    const auto ensemble = render(p, orchestra);
    bool finite = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < ensemble.getNumSamples(); ++frame)
            finite &= std::isfinite(ensemble.getSample(channel, frame));
    check(finite && energy(ensemble, 0) > 0.01, "one instance renders a finite simultaneous 16-part ensemble");
    {
        JuicySFAudioProcessor clean;
        clean.prepareToPlay(44100, 8192);
        clean.getFluidSynthModel().loadSoundFont(fixture, {});
        render(clean, empty);
        const int defaultSend = clean.getFluidSynthModel().getControllerForTests(15, 91);
        auto withoutExtras = save(clean);
        withoutExtras->removeChildElement(withoutExtras->getChildByName("partControllers"), true);
        restore(p, *withoutExtras);
        render(p, empty);
        check(p.getFluidSynthModel().getControllerForTests(15, 91) == defaultSend,
              "recall without extra controllers resets old live CC state instead of inheriting it");
    }
    {
        JuicySFAudioProcessor bankRecall;
        bankRecall.prepareToPlay(44100, 8192);
        bankRecall.getFluidSynthModel().loadSoundFont(fixture, {});
        MidiBuffer change;
        change.addEvent(MidiMessage::controllerEvent(2, 0, 8), 0);
        change.addEvent(MidiMessage::programChange(2, 1), 0);
        render(bankRecall, change);
        set(bankRecall, 1, "bank", 0);
        set(bankRecall, 1, "preset", 0);
        render(bankRecall, empty); // GUI bank change overrides the previous MIDI bank selection.
        const auto selected = save(bankRecall);
        MidiBuffer bankOnly;
        bankOnly.addEvent(MidiMessage::controllerEvent(2, 0, 8), 0);
        render(bankRecall, bankOnly);
        MidiBuffer laterProgram;
        laterProgram.addEvent(MidiMessage::programChange(2, 1), 0);
        laterProgram.addEvent(MidiMessage::noteOn(2, 69, static_cast<juce::uint8>(100)), 0);
        const auto normalMIDI = render(bankRecall, laterProgram);
        check(bankRecall.getFluidSynthModel().getBankProgram(1) == ((8u << 16) | 1u)
                  && amplitude(normalMIDI, 880) > 0.001,
              "ordinary MIDI Bank Select can still precede Program Change in a later block");
        JuicySFAudioProcessor afterRecall;
        afterRecall.prepareToPlay(44100, 8192);
        restore(afterRecall, *selected);
        MidiBuffer programOnly;
        programOnly.addEvent(MidiMessage::programChange(2, 0), 0);
        programOnly.addEvent(MidiMessage::noteOn(2, 69, static_cast<juce::uint8>(100)), 0);
        const auto actual = render(afterRecall, programOnly);
        check(afterRecall.getFluidSynthModel().getBankProgram(1) == 0
                  && amplitude(actual, 440) > 0.001 && amplitude(actual, 880) < 0.001,
              "recall keeps a GUI-selected bank authoritative over older MIDI bank-select CC history");
    }
    {
        auto legacy = save(reopened);
        legacy->removeAttribute("routing"); legacy->removeAttribute("partFormat");
        legacy->removeChildElement(legacy->getChildByName("partControllers"), true);
        auto* params = legacy->getChildByName("params");
        for (int i = params->getNumAttributes(); --i >= 0;)
            if (params->getAttributeName(i).startsWith("part")) params->removeAttribute(params->getAttributeName(i));
        params->setAttribute("bank", 8.0 / 128.0); params->setAttribute("preset", 1.0 / 127.0);
        JuicySFAudioProcessor old;
        old.prepareToPlay(44100, 8192); restore(old, *legacy);
        MidiBuffer channelTwo;
        channelTwo.addEvent(MidiMessage::noteOn(2, 69, static_cast<juce::uint8>(100)), 0);
        const auto oldAudio = render(old, channelTwo);
        check(old.getFluidSynthModel().isLegacyOmni() && amplitude(oldAudio, 880) > 0.001,
              "historical single-part projects preserve all-channel-to-part-1 MIDI routing");
        old.getFluidSynthModel().setLegacyOmni(false, true);
        const auto silence = render(old, empty);
        check(energy(silence, 0) < 0.00001, "explicit routing-mode conversion clears old held voices");
        const auto converted = render(old, channelTwo);
        check(amplitude(converted, 440) > 0.001 && amplitude(converted, 880) < 0.001,
              "legacy projects can explicitly enable independent MIDI-channel routing");
    }
    return failures;
}
