#pragma once

inline int runOutputMeterTests(const String& fixture) {
    int failures = 0;
    const auto check = [&](bool ok, const char* text) {
        std::cout << (ok ? "PASS " : "FAIL ") << text << std::endl;
        if (!ok) ++failures;
    };
    JuicySFAudioProcessor p;
    for (auto* parameter : p.getParameters()) {
        if (auto* ranged = dynamic_cast<RangedAudioParameter*>(parameter)) {
            if (ranged->getParameterID() == "outputGain") ranged->setValueNotifyingHost(ranged->convertTo0to1(6.0f));
            for (int part = 0; part < PartParameters::count; ++part)
                if (ranged->getParameterID() == PartParameters::id(part, "volume"))
                    ranged->setValueNotifyingHost(ranged->convertTo0to1(127.0f));
        }
    }
    p.prepareToPlay(44100, 512);
    check(p.getFluidSynthModel().loadSoundFont(fixture, {}), "output-meter fixture loads");
    MemoryBlock before; p.getStateInformation(before);
    MidiBuffer notes;
    for (int part = 0; part < PartParameters::count; ++part)
        notes.addEvent(MidiMessage::noteOn(part + 1, 69, static_cast<juce::uint8>(127)), 0);
    AudioBuffer<float> block(2, 512);
    float observed = 0;
    for (int i = 0; i < 8; ++i) {
        p.processBlock(block, notes);
        for (int channel = 0; channel < 2; ++channel)
            observed = juce::jmax(observed, block.getMagnitude(channel, 0, block.getNumSamples()));
    }
    check(observed > 1.0f && std::abs(p.consumeOutputPeak() - observed) < 1.0e-6f && p.hasOutputOver(),
          "meter reports actual post-gain peaks and holds OVER without limiting the audio");
    check(p.consumeOutputPeak() < 1.0e-12f && p.hasOutputOver(), "reading the peak drains only the peak accumulator, not the OVER latch");
    p.clearOutputOver();
    check(!p.hasOutputOver(), "OVER can be explicitly cleared");
    MemoryBlock after; p.getStateInformation(after);
    check(before == after, "meter observations and reset do not change serialized project state");
    p.getFluidSynthModel().loadSoundFont("", {}); // Unload guarantees silence, unlike effect-tail reset ramps.
    MidiBuffer empty;
    p.processBlock(block, empty);
    check(p.consumeOutputPeak() < 1.0e-6f && !p.hasOutputOver(), "an unloaded silent instrument does not resurrect a cleared OVER indication");
    return failures;
}
