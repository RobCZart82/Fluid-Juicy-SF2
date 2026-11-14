#pragma once

inline int runSF3Tests(const String& fixture) {
    int failures = runPartTests(fixture) + runOutputMeterTests(fixture);
    const auto check = [&](bool ok, const char* text) {
        std::cout << (ok ? "PASS " : "FAIL ") << text << std::endl;
        if (!ok) ++failures;
    };
    juce::TemporaryFile truncated(".sf3");
    MemoryBlock original;
    check(File(fixture).loadFileAsData(original) && original.getSize() > 64
              && truncated.getFile().replaceWithData(original.getData(), 64),
          "truncated SF3 fixture is created from the compressed bank");
    JuicySFAudioProcessor processor;
    processor.prepareToPlay(44100, 1024);
    auto& model = processor.getFluidSynthModel();
    check(model.loadSoundFont(fixture, {}), "SF3 failure test starts with a working bank");
    AudioBuffer<float> block(2, 1024);
    MidiBuffer notes;
    notes.addEvent(MidiMessage::noteOn(1, 69, static_cast<juce::uint8>(100)), 0);
    processor.processBlock(block, notes);
    const auto active = model.getActiveRequestForTests();
    model.requestSoundFont(truncated.getFile().getFullPathName(), {});
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 5000.0;
    while (model.isLoading() && juce::Time::getMillisecondCounterHiRes() < deadline) juce::Thread::sleep(1);
    check(!model.isLoading() && model.getSoundFontState().error.isNotEmpty(),
          "truncated SF3 background load finishes with a visible error");
    MidiBuffer empty;
    processor.processBlock(block, empty);
    check(model.getSoundFontState().path == fixture && model.getActiveRequestForTests() == active
              && block.getRMSLevel(0, 0, block.getNumSamples()) > 0.001f,
          "failed SF3 replacement preserves the committed bank and its playing voice");
    return failures;
}
