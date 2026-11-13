#pragma once
#include <algorithm>
#include <memory>

// Opt-in real-bank acceptance. Files remain external; CI uses only our original fixture.
inline int runBankAcceptance(const String& bankPath, const String& reportPath, bool requireGM) {
    auto* object = new juce::DynamicObject();
    juce::var report(object);
    object->setProperty("format", 1);
    object->setProperty("bank", bankPath);
    object->setProperty("bankBytes", static_cast<juce::int64>(File(bankPath).getSize()));
    object->setProperty("sampleRate", 44100);
    object->setProperty("blockFrames", 64);
    object->setProperty("requiredGM", requireGM);
    juce::Array<juce::var> errors;
    const auto fail = [&](const String& text) { errors.add(text); std::cerr << text << std::endl; };
    auto processor = std::make_unique<JuicySFAudioProcessor>();
    const auto set = [](JuicySFAudioProcessor& target, int part, const String& name, float value) {
        const auto id = PartParameters::id(part, name);
        for (auto* parameter : target.getParameters())
            if (auto* ranged = dynamic_cast<RangedAudioParameter*>(parameter))
                if (ranged->getParameterID() == id) { ranged->setValueNotifyingHost(ranged->convertTo0to1(value)); return; }
    };
    bool finite = true;
    const auto measure = [&](const AudioBuffer<float>& block, double& peak, double& squareSum, juce::int64& overs) {
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            for (int frame = 0; frame < block.getNumSamples(); ++frame) {
                const double value = block.getSample(channel, frame);
                finite &= std::isfinite(value);
                peak = std::max(peak, std::abs(value)); squareSum += value * value;
                if (std::abs(value) > 1.0) ++overs;
            }
    };
    processor->prepareToPlay(44100, 64);
    const auto loadStart = juce::Time::getMillisecondCounterHiRes();
    const bool loaded = processor->getFluidSynthModel().loadSoundFont(bankPath, {});
    object->setProperty("loadMs", juce::Time::getMillisecondCounterHiRes() - loadStart);
    if (!loaded) fail("Bank could not be loaded");
    int presentGM = 0, audibleGM = 0, audibleDrums = 0;
    double auditionPeak = 0;
    juce::Array<juce::var> missing, silent;
    if (loaded) {
        AudioBuffer<float> adopt(2, 64); MidiBuffer noEvents;
        processor->processBlock(adopt, noEvents);
        // Three key positions reduce false silence on range-limited presets.
        for (int program = 0; program < 128; ++program) {
            processor->reset();
            set(*processor, 0, "bank", 0); set(*processor, 0, "preset", static_cast<float>(program));
            bool sounded = false, exists = false;
            for (int key : {60, 48, 72}) {
                processor->reset();
                MidiBuffer events;
                events.addEvent(MidiMessage::noteOn(1, key, static_cast<juce::uint8>(100)), 0);
                double energy = 0; juce::int64 overs = 0;
                for (int blockIndex = 0; blockIndex < 70; ++blockIndex) {
                    AudioBuffer<float> block(2, 64);
                    processor->processBlock(block, events);
                    measure(block, auditionPeak, energy, overs);
                }
                exists = processor->getFluidSynthModel().getBankProgram(0) == static_cast<unsigned int>(program);
                if (!exists) break;
                if (energy > 1.0e-9) { sounded = true; break; }
            }
            if (exists) ++presentGM; else missing.add(program);
            if (exists && sounded) ++audibleGM;
            else if (exists) silent.add(program);
        }
        set(*processor, 9, "bank", 128); set(*processor, 9, "preset", 0);
        for (int key = 35; key <= 81; ++key) {
            processor->reset();
            MidiBuffer events;
            events.addEvent(MidiMessage::noteOn(10, key, static_cast<juce::uint8>(100)), 0);
            double energy = 0; juce::int64 overs = 0;
            for (int blockIndex = 0; blockIndex < 70; ++blockIndex) {
                AudioBuffer<float> block(2, 64);
                processor->processBlock(block, events);
                measure(block, auditionPeak, energy, overs);
            }
            if (processor->getFluidSynthModel().getBankProgram(9) == (128u << 16) && energy > 1.0e-9) ++audibleDrums;
        }
        if (requireGM && (presentGM != 128 || audibleGM != 128 || audibleDrums != 47))
            fail("GM address/audibility spot checks are incomplete; inspect missing/silent programs");
        if (!requireGM && (presentGM < 1 || audibleGM < 1 || audibleDrums < 1)) fail("Synthetic harness fixture did not sound");
        processor->reset();
        const int programs[] = {0,25,33,48,56,73,80,89,11,0,4,19,24,65,81,91};
        MidiBuffer ensemble;
        for (int part = 0; part < PartParameters::count; ++part) {
            set(*processor, part, "bank", part == 9 ? 128.0f : 0.0f);
            set(*processor, part, "preset", requireGM ? static_cast<float>(programs[part]) : 0.0f);
            set(*processor, part, "volume", 100); set(*processor, part, "expression", 127);
            ensemble.addEvent(MidiMessage::noteOn(part + 1, part == 9 ? 36 : 48 + part % 12,
                static_cast<juce::uint8>(100)), 0);
        }
        processor->consumeOutputPeak(); processor->clearOutputOver();
        double peak = 0, squares = 0; juce::int64 overs = 0;
        std::vector<double> times; times.reserve(2000);
        for (int i = 0; i < 2100; ++i) {
            AudioBuffer<float> block(2, 64);
            const auto start = juce::Time::getMillisecondCounterHiRes();
            processor->processBlock(block, ensemble);
            const double elapsed = (juce::Time::getMillisecondCounterHiRes() - start) * 1000.0;
            if (i >= 100) times.push_back(elapsed);
            measure(block, peak, squares, overs);
        }
        std::sort(times.begin(), times.end());
        const bool peakMatches = std::abs(processor->consumeOutputPeak() - peak) < 1.0e-6;
        const bool overMatches = processor->hasOutputOver() == (overs > 0);
        object->setProperty("meterMatchesRenderedAudio", peakMatches && overMatches);
        if (!peakMatches || !overMatches) fail("Post-Output meter disagrees with rendered samples");
        object->setProperty("ensemblePeak", peak);
        object->setProperty("ensembleSamplesAboveFullScale", overs);
        object->setProperty("ensembleRms", std::sqrt(squares / (2100.0 * 128.0)));
        object->setProperty("renderMedianUs", times[1000]); object->setProperty("renderP99Us", times[1980]);
        object->setProperty("renderMaxUs", times.back()); object->setProperty("blockBudgetUs", 64.0 / 44100.0 * 1.0e6);
        object->setProperty("suggestedAdditionalAttenuationDb", peak > 0 ? std::max(0.0, 20.0 * std::log10(peak) + 1.0) : 0.0);
        if (squares <= 1.0e-9) fail("Sixteen-part ensemble was silent");
        for (int part = 0; part < PartParameters::count; ++part) {
            set(*processor, part, "volume", static_cast<float>(64 + part));
            set(*processor, part, "pan", static_cast<float>(part * 8));
        }
        MidiBuffer empty; AudioBuffer<float> block(2, 64); processor->processBlock(block, empty);
        MemoryBlock state; processor->getStateInformation(state);
        const auto expected = AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()))->toString();
        if (processor->getFluidSynthModel().getAudioReclaimsForTests() != 0) fail("Engine reclaimed during rendering");
        processor.reset(); // Do not keep two giant sample banks in RAM during recall testing.
        processor = std::make_unique<JuicySFAudioProcessor>();
        processor->prepareToPlay(44100, 64);
        processor->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        processor->processBlock(block, empty);
        MemoryBlock recalled; processor->getStateInformation(recalled);
        const auto actual = AudioProcessor::getXmlFromBinary(recalled.getData(), static_cast<int>(recalled.getSize()))->toString();
        const bool recalledOk = actual == expected && processor->getFluidSynthModel().hasLoadedSoundFont();
        object->setProperty("allPartStateRoundTrip", recalledOk);
        if (!recalledOk) fail("All-part state did not round-trip exactly");
    }
    if (!finite) fail("Non-finite audio samples rendered");
    object->setProperty("gmPresent", presentGM); object->setProperty("gmAudible", audibleGM);
    object->setProperty("standardDrumKeysAudible", audibleDrums);
    object->setProperty("missingGMPrograms", juce::var(missing)); object->setProperty("silentGMPrograms", juce::var(silent));
    object->setProperty("auditionPeak", auditionPeak); object->setProperty("finiteAudio", finite);
    object->setProperty("errors", juce::var(errors)); object->setProperty("passed", errors.isEmpty());
    const File destination(reportPath);
    destination.getParentDirectory().createDirectory();
    if (!destination.replaceWithText(juce::JSON::toString(report))) { std::cerr << "Could not write acceptance report" << std::endl; return 2; }
    std::cout << juce::JSON::toString(report) << std::endl;
    return errors.isEmpty() ? 0 : 1;
}
