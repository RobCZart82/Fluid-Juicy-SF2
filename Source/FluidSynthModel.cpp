// Created by Alex Birch on 10/09/2017.
#include "FluidSynthModel.h"
#include "Util.h"
#include <algorithm>
#include <chrono>
#include <tuple>
#include <stdexcept>
#if JUCE_MAC || JUCE_IOS
using JuicyMac::CFPtr;
#endif
thread_local FluidSynthModel* FluidSynthModel::publishingSelection = nullptr;
thread_local FluidSynthModel* FluidSynthModel::renderingModel = nullptr;
FluidSynthModel::Engine::~Engine() {
#if JUICY_ENABLE_TEST_HOOKS
    if (renderingModel == owner) ++owner->audioReclaims;
#endif
    synth.reset();
    settings.reset();
#if JUCE_MAC || JUCE_IOS
    access.reset();
#endif
    --owner->liveEngines;
    owner->engineAvailable.notify_all();
}
FluidSynthModel::FluidSynthModel(AudioProcessorValueTreeState& state) : valueTreeState(state) {
    for (int channel = 0; channel < partCount; ++channel) {
        auto& part = parts[static_cast<size_t>(channel)];
        for (auto& value : part.controllers) value.store(-1);
        const auto bind = [&](const String& name, int kind) {
            const auto id = PartParameters::id(channel, name);
            auto* parameter = static_cast<AudioParameterInt*>(state.getParameter(id));
            bindings.emplace(id, Binding{channel, kind});
            state.addParameterListener(id, this);
            return parameter;
        };
        part.bank = bind("bank", -1);
        part.preset = bind("preset", -2);
        part.selection.store(packed(part.bank->get(), part.preset->get()));
        for (const auto& control : PartParameters::controls) {
            auto* parameter = bind(control.name, control.cc);
            part.ccParameters[static_cast<size_t>(control.cc)] = parameter;
            part.controllers[static_cast<size_t>(control.cc)].store(parameter->get());
        }
        part.pitchParameter = bind("pitchBend", -3);
        part.pressureParameter = bind("pressure", -4);
        part.rangeParameter = bind("bendRange", -5);
    }
    publishedMetadata = make_shared<Metadata>();
    loader = thread([this] { loaderLoop(); });
}
FluidSynthModel::~FluidSynthModel() {
    {
        lock_guard<mutex> lock(requestMutex);
        stopping = true;
        queued.reset();
        ++latestRequest;
    }
    requestReady.notify_one();
    loader.join();
    for (const auto& binding : bindings)
        valueTreeState.removeParameterListener(binding.first, this);
    delete pending.exchange(nullptr);
    reclaimRetired();
    active.reset();
    fading.reset();
}
void FluidSynthModel::initialise() { active = makeEngine(targetRate.load()); }

unique_ptr<FluidSynthModel::Engine> FluidSynthModel::makeEngine(float rate) {
    // Non-RT producers serialize on buildMutex. Retired engines may be in the
    // middle of destruction on the reclaimer, so wait for their budget credit.
    for (;;) {
        reclaimRetired();
        if (liveEngines.load() < engineBudget) break;
        unique_lock<mutex> lock(engineBudgetMutex);
        engineAvailable.wait_for(lock, chrono::milliseconds(10));
    }
    auto engine = make_unique<Engine>(this);
    engine->rate = rate;
    for (auto& channel : engine->channels) channel.appliedControllers.fill(-1);
    engine->metadata = make_shared<Metadata>();
    auto& settings = engine->settings;
    auto& synth = engine->synth;
    settings.reset(new_fluid_settings());
    if (!settings) throw runtime_error("Could not create FluidSynth settings");
    // https://sourceforge.net/p/fluidsynth/wiki/FluidSettings/
#if JUCE_DEBUG
    fluid_settings_setint(settings.get(), "synth.verbose", 1);
#endif

    fluid_settings_setnum(settings.get(), "synth.sample-rate", rate);
    fluid_settings_setint(settings.get(), "synth.midi-channels", partCount);
    synth.reset(new_fluid_synth(settings.get()));
    if (!synth) throw runtime_error("Could not create FluidSynth engine");

    // I can't hear a damned thing
    fluid_synth_set_gain(synth.get(), 2.0);
    
    // note: fluid_chan.c#fluid_channel_init_ctrl()
    // > Just like panning, a value of 64 indicates no change for sound ctrls
    // --
    // so, advice is to leave everything at 64
    // and yet, I'm finding that default modulators start at MIN,
    // i.e. we are forced to start at 0 and climb from there
    // --
    // let's zero out every audio param that we manage
    for (int channel = 0; channel < partCount; ++channel)
        for (const auto& control : PartParameters::controls)
            fluid_synth_cc(synth.get(), channel, control.cc, control.initial);
    fluid_synth_set_channel_type(synth.get(), 9, CHANNEL_TYPE_DRUM);

    // http://www.synthfont.com/SoundFont_NRPNs.PDF
    float env_amount{20000.0f};
    
    unique_ptr<fluid_mod_t, decltype(&delete_fluid_mod)> mod{new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL2), // MIDI CC 71 Timbre/Harmonic Intensity (filter resonance)
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_CONCAVE
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_FILTERQ);
    fluid_mod_set_amount(mod.get(), FLUID_PEAK_ATTENUATION);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL3), // MIDI CC 72 Release time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVRELEASE);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL4), // MIDI CC 73 Attack time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVATTACK);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    // soundfont spec says that if cutoff is >20kHz and resonance Q is 0, then no filtering occurs
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL5), // MIDI CC 74 Brightness (cutoff frequency, FILTERFC)
                          FLUID_MOD_CC
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_POSITIVE);
        fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_FILTERFC);
    fluid_mod_set_amount(mod.get(), -2400.0f);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL6), // MIDI CC 75 Decay Time
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_LINEAR
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVDECAY);
    fluid_mod_set_amount(mod.get(), env_amount);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);
    
    mod = {new_fluid_mod(), delete_fluid_mod};
    fluid_mod_set_source1(mod.get(),
                          static_cast<int>(SOUND_CTRL10), // MIDI CC 79 undefined
                          FLUID_MOD_CC
                          | FLUID_MOD_UNIPOLAR
                          | FLUID_MOD_CONCAVE
                          | FLUID_MOD_POSITIVE);
    fluid_mod_set_source2(mod.get(), 0, 0);
    fluid_mod_set_dest(mod.get(), GEN_VOLENVSUSTAIN);
    // fluice_voice.c#fluid_voice_update_param()
    // clamps the range to between 0 and 1000, so we'll copy that
    fluid_mod_set_amount(mod.get(), 1000.0f);
    fluid_synth_add_default_mod(synth.get(), mod.get(), FLUID_SYNTH_ADD);

    if (!synth) throw runtime_error("Could not create FluidSynth engine");
    return engine;
}

FluidSynthModel::Request FluidSynthModel::newRequest(const String& path, const MemoryBlock& bookmark) {
    Request request;
    {
        lock_guard<mutex> lock(requestMutex);
        request = {path, bookmark, ++latestRequest};
        queued.reset();
        loading.store(true);
        lock_guard<mutex> stateLock(stateMutex);
        committed.error.clear();
    }
    uiDirty.store(true);
    return request;
}
void FluidSynthModel::requestSoundFont(const String& path, const MemoryBlock& bookmark) {
    const auto previous = getSoundFontState();
    auto request = newRequest(path, bookmark);
    // A failed manual relocation must not replace the project's missing reference.
    request.preserveReferenceOnFailure = previous.path.isNotEmpty();
    request.notifyHost = true;
    {
        lock_guard<mutex> lock(requestMutex);
        if (request.id == latestRequest.load() && !stopping) queued = std::move(request);
    }
    requestReady.notify_one();
    synchroniseUi();
}
bool FluidSynthModel::loadSoundFont(const String& path, const MemoryBlock& bookmark) {
    // Host state restoration is a non-RT operation. Never perform I/O in a
    // reentrant host callback made while our own processBlock is on the stack.
    jassert(renderingModel != this);
    if (renderingModel == this) return false;
    auto request = newRequest(path, bookmark);
    request.crossfade = false; // Host restore must take effect at the next block.
    bool ok;
    {
        lock_guard<mutex> lock(buildMutex);
        ok = performLoad(request);
    }
    synchroniseUi();
    return ok;
}
void FluidSynthModel::loaderLoop() {
    for (;;) {
        reclaimRetired();
        optional<Request> request;
        {
            unique_lock<mutex> lock(requestMutex);
            requestReady.wait_for(lock, chrono::milliseconds(20), [this] { return stopping || queued.has_value(); });
            if (stopping) return;
            request = std::move(queued);
            queued.reset();
        }
        if (request) {
            lock_guard<mutex> lock(buildMutex);
            performLoad(*request);
        }
    }
}
bool FluidSynthModel::performLoad(const Request& request) {
#if JUICY_ENABLE_TEST_HOOKS
    startedRequest.store(request.id);
    {
        unique_lock<mutex> lock(requestMutex);
        requestReady.wait(lock, [this, &request] {
            return !loadPaused.load() || stopping || request.id != latestRequest.load();
        });
        requestReady.wait_for(lock, chrono::milliseconds(loadDelay.load()),
            [this, &request] { return stopping || request.id != latestRequest.load(); });
    }
#endif
    {
        lock_guard<mutex> lock(requestMutex);
        if (stopping || request.id != latestRequest.load()) return false;
    }
    unique_ptr<Engine> candidate;
    try { candidate = prepareFont(request); }
    catch (const exception&) { candidate.reset(); }
    if (candidate) return publish(std::move(candidate), request);
    {
        lock_guard<mutex> lock(requestMutex);
        if (stopping || request.id != latestRequest.load()) return false;
        lock_guard<mutex> stateLock(stateMutex);
        if (!publishedHasFont.load() && !request.preserveReferenceOnFailure) {
            committed.path = request.path;
            committed.bookmark = request.bookmark;
        }
        committed.error = "Could not load SoundFont: " + request.path
            + ". Check that the file exists and is a valid SF2/SF3 bank. The previous bank is kept when available.";
        loading.store(false);
    }
    uiDirty.store(true);
    return false;
}
unique_ptr<FluidSynthModel::Engine> FluidSynthModel::prepareFont(const Request& request) {
    auto engine = makeEngine(targetRate.load());
    engine->request = request.id;
    engine->crossfade = request.crossfade;
    engine->path = request.path;
    engine->bookmark = request.bookmark;
    if (request.path.isEmpty() && request.bookmark.getSize() == 0) return engine;
#if JUCE_MAC || JUCE_IOS
    if (request.bookmark.getSize() > 0) {
        CFPtr<CFDataRef> data{CFDataCreate(nullptr, static_cast<const UInt8*>(request.bookmark.getData()),
            static_cast<CFIndex>(request.bookmark.getSize()))};
        CFErrorRef error = nullptr;
        Boolean stale = false;
        CFURLRef resolved = data ? CFURLCreateByResolvingBookmarkData(nullptr, data.get(),
            kCFURLBookmarkResolutionWithSecurityScope, nullptr, nullptr, &stale, &error) : nullptr;
        CFPtr<CFErrorRef> ownedError{error};
        engine->access = make_unique<JuicyMac::ScopedFileAccess>(resolved);
        if (engine->access->url) {
            CFPtr<CFStringRef> path{CFURLCopyFileSystemPath(engine->access->url.get(), kCFURLPOSIXPathStyle)};
            if (path) engine->path = String::fromCFString(path.get());
        } else { engine->access.reset(); engine->bookmark.reset(); }
    }
#endif
    const auto tryPath = [&] {
        if (engine->path.isEmpty()) return -1;
        return fluid_synth_sfload(engine->synth.get(), engine->path.toRawUTF8(), 0);
    };
    engine->font = tryPath();
    if (engine->font < 0 && engine->path != request.path && request.path.isNotEmpty()) {
        engine->path = request.path;
        engine->bookmark.reset();
#if JUCE_MAC || JUCE_IOS
        engine->access.reset();
#endif
        engine->font = tryPath();
    }
    if (engine->font < 0) return nullptr;
    auto metadata = make_shared<Metadata>();
    auto* font = fluid_synth_get_sfont_by_id(engine->synth.get(), engine->font);
    fluid_sfont_iteration_start(font);
    while (auto* preset = fluid_sfont_iteration_next(font))
        metadata->presets.push_back({fluid_preset_get_banknum(preset), fluid_preset_get_num(preset),
                                    String::fromUTF8(fluid_preset_get_name(preset))});
    if (metadata->presets.empty()) return nullptr;
    sort(metadata->presets.begin(), metadata->presets.end(), [](const Preset& a, const Preset& b) {
        return tie(a.bank, a.program) < tie(b.bank, b.program);
    });
    engine->metadata = metadata;
    for (int channel = 0; channel < partCount; ++channel) {
        auto& state = engine->channels[static_cast<size_t>(channel)];
        state.builtSelectionRequest = parts[static_cast<size_t>(channel)].selection.load();
        applyPart(*engine, channel, false, false);
        int selectedFont = -1, bank = 0, program = 0;
        fluid_synth_get_program(engine->synth.get(), channel, &selectedFont, &bank, &program);
        state.preparedSelection = selectedFont == engine->font ? packed(bank, program) : state.builtSelectionRequest;
    }
    engine->metadata = std::move(metadata);
    return engine;
}
bool FluidSynthModel::publish(unique_ptr<Engine> candidate, const Request& request) {
    Engine* obsolete;
    {
        lock_guard<mutex> lock(requestMutex);
        if (stopping || request.id != latestRequest.load()) return false;
        {
            lock_guard<mutex> stateLock(stateMutex);
            const bool referenceChanged = committed.path != candidate->path || committed.bookmark != candidate->bookmark;
            committed = {candidate->path, candidate->bookmark, {}};
            if (request.notifyHost) {
                if (referenceChanged) hostStateChanged.store(true);
            } else {
                // A successful host recall supersedes any GUI change awaiting delivery.
                hostStateChanged.store(false);
            }
            publishedMetadata = candidate->metadata;
            const bool empty = candidate->font < 0;
            publishedHasFont.store(!empty);
            desiredEmpty.store(empty);
            if (empty) unloadEpoch.store(request.id);
            if (!empty) {
                for (int channel = 0; channel < partCount; ++channel) {
                    auto& state = candidate->channels[static_cast<size_t>(channel)];
                    auto expected = state.builtSelectionRequest;
                    parts[static_cast<size_t>(channel)].selection.compare_exchange_strong(expected, state.preparedSelection);
                }
            }
        }
        obsolete = pending.exchange(candidate.release());
        loading.store(false);
    }
    // Deallocation/bookmark release must not hold even the short request lock.
    delete obsolete;
    uiDirty.store(true);
    return true;
}

void FluidSynthModel::retireEngine(Engine* engine) {
    if (!engine) return;
    auto* head = retired.load();
    do { engine->retiredNext = head; }
    while (!retired.compare_exchange_weak(head, engine));
}
void FluidSynthModel::reclaimRetired() {
    auto* engine = retired.exchange(nullptr);
    while (engine) {
        auto* next = engine->retiredNext;
        delete engine;
        engine = next;
    }
}
void FluidSynthModel::adoptPending() {
    if (auto* next = pending.exchange(nullptr)) {
        if (next->crossfade)
            for (int channel = 0; channel < partCount; ++channel)
                restoreChannel(*next, channel, captureChannel(*active, channel), false);
        retireEngine(fading.release());
        fadeRemaining = 0;
        auto* previous = active.release();
        active.reset(next);
        activeRequest.store(next->request);
        if (next->crossfade && previous->font >= 0 && next->font >= 0 && previous->request >= unloadEpoch.load()) {
            fading.reset(previous); // Empty owner: no destructor on the render stack.
            fadeRemaining = fadeSamples;
        } else {
            retireEngine(previous);
        }
    }
}
void FluidSynthModel::adoptWhileStopped() {
    fading.reset();
    fadeRemaining = 0;
    reclaimRetired();
    if (auto* next = pending.exchange(nullptr)) {
        if (next->crossfade)
            for (int channel = 0; channel < partCount; ++channel)
                restoreChannel(*next, channel, captureChannel(*active, channel), false);
        active.reset(next); // Host has stopped rendering: reclamation is non-RT.
        activeRequest.store(next->request);
    }
}
void FluidSynthModel::parameterChanged(const String& id, float value) {
    if (publishingSelection == this) return;
    const auto binding = bindings.find(id);
    if (binding == bindings.end()) return;
    auto& part = parts[static_cast<size_t>(binding->second.part)];
    const int kind = binding->second.kind;
    if (kind == -1 || kind == -2) {
        auto before = part.selection.load();
        unsigned int after;
        do {
            after = kind == -1 ? packed(static_cast<int>(value), static_cast<int>(before & 0xffffu))
                              : packed(static_cast<int>(before >> 16), static_cast<int>(value));
        } while (!part.selection.compare_exchange_weak(before, after));
    } else if (kind == -3) part.pitch.store(static_cast<int>(value));
    else if (kind == -4) part.pressure.store(static_cast<int>(value));
    else if (kind == -5) part.range.store(static_cast<int>(value));
    else part.controllers[static_cast<size_t>(kind)].store(static_cast<int>(value));
}
void FluidSynthModel::setControllerValue(int controller, int value) {
    if (controller < 0 || controller > 127) return;
    auto* parameter = parts[static_cast<size_t>(getChannel())].ccParameters[static_cast<size_t>(controller)];
    if (parameter) *parameter = juce::jlimit(0, 127, value);
}
void FluidSynthModel::applyControls(Engine& engine) {
    for (int channel = 0; channel < partCount; ++channel) applyPart(engine, channel);
}
void FluidSynthModel::applyPart(Engine& engine, int channel, bool publishSelection, bool applyControllers) {
    auto& part = parts[static_cast<size_t>(channel)];
    auto& state = engine.channels[static_cast<size_t>(channel)];
    if (applyControllers) {
        for (int cc = 0; cc < 120; ++cc) {
            if (PartParameters::isTransaction(cc)) continue;
            const int value = part.controllers[static_cast<size_t>(cc)].load();
            if (value >= 0 && value != state.appliedControllers[static_cast<size_t>(cc)]) {
                fluid_synth_cc(engine.synth.get(), channel, cc, value);
                state.appliedControllers[static_cast<size_t>(cc)] = value;
            }
        }
        const int pitch = part.pitch.load(), pressure = part.pressure.load(), range = part.range.load();
        if (state.appliedPitch != pitch) fluid_synth_pitch_bend(engine.synth.get(), channel, state.appliedPitch = pitch);
        if (state.appliedPressure != pressure) fluid_synth_channel_pressure(engine.synth.get(), channel, state.appliedPressure = pressure);
        if (state.appliedRange != range) fluid_synth_pitch_wheel_sens(engine.synth.get(), channel, state.appliedRange = range);
        state.pressure = pressure;
    }
    const auto requested = part.selection.load();
    if (engine.font >= 0 && state.lastSelection != requested) {
        const auto& presets = engine.metadata->presets;
        const int bank = static_cast<int>(requested >> 16), program = static_cast<int>(requested & 0xffffu);
        auto chosen = find_if(presets.begin(), presets.end(), [=](const Preset& p) { return p.bank == bank && p.program == program; });
        if (chosen == presets.end())
            chosen = find_if(presets.begin(), presets.end(), [=](const Preset& p) { return p.bank == bank; });
        // A missing drum kit must not silently become a melodic piano.
        if (chosen == presets.end() && !(channel == 9 && bank == 128)) chosen = presets.begin();
        if (chosen != presets.end()) {
            fluid_synth_program_select(engine.synth.get(), channel, engine.font, chosen->bank, chosen->program);
            const auto actual = packed(chosen->bank, chosen->program);
            auto expected = requested;
            if (actual != requested && publishSelection && part.selection.compare_exchange_strong(expected, actual)) {
                if (renderingModel == this) publishParameters(channel);
                else uiDirty.store(true);
            }
            state.lastSelection = actual;
        } else {
            fluid_synth_unset_program(engine.synth.get(), channel);
            state.lastSelection = requested;
        }
    }
}
void FluidSynthModel::publishParameters(int channel) {
    const juce::ScopedValueSetter<FluidSynthModel*> guard(publishingSelection, this);
    auto& part = parts[static_cast<size_t>(channel)];
    const auto current = part.selection.load();
    *part.bank = static_cast<int>(current >> 16);
    *part.preset = static_cast<int>(current & 0xffffu);
    for (const auto& control : PartParameters::controls)
        *part.ccParameters[static_cast<size_t>(control.cc)] = part.controllers[static_cast<size_t>(control.cc)].load();
    *part.pitchParameter = part.pitch.load();
    *part.pressureParameter = part.pressure.load();
    *part.rangeParameter = part.range.load();
}
void FluidSynthModel::publishCurrentSelection(Engine& engine, int channel) {
    int font, bank, program;
    if (fluid_synth_get_program(engine.synth.get(), channel, &font, &bank, &program) != FLUID_OK) return;
    auto& part = parts[static_cast<size_t>(channel)];
    auto& state = engine.channels[static_cast<size_t>(channel)];
    part.selection.store(packed(bank, program));
    const bool playable = font == engine.font && any_of(engine.metadata->presets.begin(), engine.metadata->presets.end(),
        [=](const Preset& p) { return p.bank == bank && p.program == program; });
    state.lastSelection = playable ? packed(bank, program) : ~0u;
    if (!playable) applyPart(engine, channel);
    publishParameters(channel);
}
void FluidSynthModel::synchronisePart(Engine& engine, int channel) {
    auto& part = parts[static_cast<size_t>(channel)];
    auto& state = engine.channels[static_cast<size_t>(channel)];
    for (int cc = 0; cc < 120; ++cc) {
        int value = 0;
        fluid_synth_get_cc(engine.synth.get(), channel, cc, &value);
        if (!PartParameters::isTransaction(cc)) part.controllers[static_cast<size_t>(cc)].store(value);
        state.appliedControllers[static_cast<size_t>(cc)] = value;
    }
    int pitch = 8192, range = 2;
    fluid_synth_get_pitch_bend(engine.synth.get(), channel, &pitch);
    fluid_synth_get_pitch_wheel_sens(engine.synth.get(), channel, &range);
    part.pitch.store(pitch); part.range.store(range); part.pressure.store(state.pressure);
    publishCurrentSelection(engine, channel);
}
FluidSynthModel::PartState FluidSynthModel::getPartState(int channel) const {
    PartState result;
    channel = juce::jlimit(0, partCount - 1, channel);
    for (int cc = 0; cc < 120; ++cc)
        result.controllers[static_cast<size_t>(cc)] = parts[static_cast<size_t>(channel)].controllers[static_cast<size_t>(cc)].load();
    return result;
}
void FluidSynthModel::restorePartState(int channel, const PartState& state) {
    if (channel < 0 || channel >= partCount) return;
    for (int cc = 0; cc < 120; ++cc)
        if (!PartParameters::isTransaction(cc) && !parts[static_cast<size_t>(channel)].ccParameters[static_cast<size_t>(cc)])
            parts[static_cast<size_t>(channel)].controllers[static_cast<size_t>(cc)].store(state.controllers[static_cast<size_t>(cc)]);
}
void FluidSynthModel::clearExtraControllers() {
    PartState defaults; defaults.controllers.fill(-1);
    for (int channel = 0; channel < partCount; ++channel) restorePartState(channel, defaults);
}
FluidSynthModel::SoundFontState FluidSynthModel::getSoundFontState() const {
    lock_guard<mutex> lock(stateMutex);
    auto snapshot = committed;
    snapshot.bankProgram = parts[0].selection.load();
    return snapshot;
}
void FluidSynthModel::synchroniseUi() {
    if (auto* manager = juce::MessageManager::getInstanceWithoutCreating())
        if (manager->isThisTheMessageThread() && uiDirty.exchange(false)) publishUi();
}
void FluidSynthModel::publishUi() {
#if JUICY_ENABLE_TEST_HOOKS
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) ++uiViolations;
#endif
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
    SoundFontState state;
    shared_ptr<const Metadata> metadata;
    {
        lock_guard<mutex> lock(stateMutex);
        state = committed;
        metadata = publishedMetadata;
    }
    for (int channel = 0; channel < partCount; ++channel) publishParameters(channel);
    if (metadata != displayedMetadata) {
        ValueTree banks{"banks"};
        ValueTree bank;
        int previous = -1;
        for (const auto& preset : metadata->presets) {
            if (preset.bank != previous) {
                bank = {"bank", {{"num", preset.bank}}};
                banks.appendChild(bank, nullptr);
                previous = preset.bank;
            }
            bank.appendChild({"preset", {{"num", preset.program}, {"name", preset.name}}, {}}, nullptr);
        }
        auto target = valueTreeState.state.getChildWithName("banks");
        target.copyPropertiesAndChildrenFrom(banks, nullptr);
        target.sendPropertyChangeMessage("synthetic");
        displayedMetadata = metadata;
    }
    auto target = valueTreeState.state.getChildWithName("soundFont");
    target.setProperty("path", state.path, nullptr);
    target.setProperty("bookmark", var(state.bookmark), nullptr);
    target.setProperty("loadError", state.error, nullptr);
    target.setProperty("loading", loading.load(), nullptr);
}
int FluidSynthModel::getCurrentProgram() { return static_cast<int>(parts[0].selection.load() & 0xffffu); }
void FluidSynthModel::setCurrentProgram(int index) {
    *static_cast<AudioParameterInt*>(valueTreeState.getParameter("preset")) = juce::jlimit(0, 127, index);
}
const String FluidSynthModel::getProgramName(int index) {
    shared_ptr<const Metadata> metadata;
    {
        lock_guard<mutex> lock(stateMutex);
        metadata = publishedMetadata;
    }
    const auto bank = static_cast<int>(parts[0].selection.load() >> 16);
    for (const auto& preset : metadata->presets)
        if (preset.bank == bank && preset.program == index) return preset.name;
    return "Preset " + String(index);
}
String FluidSynthModel::getPartName(int channel) const {
    channel = juce::jlimit(0, partCount - 1, channel);
    const auto current = parts[static_cast<size_t>(channel)].selection.load();
    lock_guard<mutex> lock(stateMutex);
    for (const auto& preset : publishedMetadata->presets)
        if (preset.bank == static_cast<int>(current >> 16) && preset.program == static_cast<int>(current & 0xffffu))
            return preset.name;
    return publishedHasFont.load() ? "Preset unavailable" : "No SoundFont";
}
void FluidSynthModel::setSampleRate(float rate) {
    // Serialized host lifecycle callback, outside processBlock.
    lock_guard<mutex> lock(buildMutex);
    adoptWhileStopped();
    if (targetRate.load() == rate) return;
    targetRate.store(rate);
    array<ChannelSnapshot, partCount> state;
    for (int channel = 0; channel < partCount; ++channel) state[static_cast<size_t>(channel)] = captureChannel(*active, channel);
    const auto saved = getSoundFontState();
    Request rebuild{saved.path, saved.bookmark, active->request};
    auto replacement = prepareFont(rebuild);
    if (!replacement) {
        replacement = makeEngine(rate);
        replacement->request = rebuild.id;
        {
            lock_guard<mutex> stateLock(stateMutex);
            committed.error = "Could not reload SoundFont after sample-rate change: " + saved.path;
            publishedMetadata = replacement->metadata;
            publishedHasFont.store(false);
            desiredEmpty.store(true);
        }
        uiDirty.store(true);
    }
    if (replacement->font >= 0) {
        lock_guard<mutex> stateLock(stateMutex);
        committed.path = replacement->path;
        committed.bookmark = replacement->bookmark;
        committed.error.clear();
        publishedMetadata = replacement->metadata;
        publishedHasFont.store(true);
        desiredEmpty.store(false);
        for (int channel = 0; channel < partCount; ++channel) {
            auto& part = replacement->channels[static_cast<size_t>(channel)];
            auto expected = part.builtSelectionRequest;
            parts[static_cast<size_t>(channel)].selection.compare_exchange_strong(expected, part.preparedSelection);
        }
        uiDirty.store(true);
    }
    for (int channel = 0; channel < partCount; ++channel)
        restoreChannel(*replacement, channel, state[static_cast<size_t>(channel)], false);
    active = std::move(replacement);
    applyControls(*active);
}
void FluidSynthModel::resetPlayback() {
    if (active) resetEngine(*active);
    // The old bank must not resolve the new bank's selection during reset.
    retireEngine(fading.release());
    fadeRemaining = 0;
}
void FluidSynthModel::blendRetiringEngine(AudioBuffer<float>& buffer, int start, int count, bool silent) {
    if (!fading) return;
    float left[64], right[64];
    for (int offset = 0; offset < count && fading; offset += 64) {
        const int length = juce::jmin(64, count - offset, fadeRemaining);
        std::fill_n(left, length, 0.0f);
        std::fill_n(right, length, 0.0f);
        float* pair[] = {left, right};
        fluid_synth_process(fading->synth.get(), length, 2, pair, 2, pair);
        if (!silent && fading->request >= unloadEpoch.load()) {
            for (int i = 0; i < length; ++i) {
                const float oldGain = static_cast<float>(fadeRemaining - i) / static_cast<float>(fadeSamples);
                const int frame = start + offset + i;
                if (buffer.getNumChannels() == 1) {
                    const float oldSample = 0.5f * (left[i] + right[i]);
                    buffer.setSample(0, frame, oldGain * oldSample + (1.0f - oldGain) * buffer.getSample(0, frame));
                } else {
                    buffer.setSample(0, frame, oldGain * left[i] + (1.0f - oldGain) * buffer.getSample(0, frame));
                    buffer.setSample(1, frame, oldGain * right[i] + (1.0f - oldGain) * buffer.getSample(1, frame));
                }
            }
        }
        fadeRemaining -= length;
        if (fadeRemaining == 0) retireEngine(fading.release());
    }
}
FluidSynthModel::ChannelSnapshot FluidSynthModel::captureChannel(Engine& engine, int channel) {
    auto& synth = engine.synth;
    ChannelSnapshot state;
    state.pressure = engine.channels[static_cast<size_t>(channel)].pressure;
    for (int cc = 0; cc < 120; ++cc)
        fluid_synth_get_cc(synth.get(), channel, cc, &state.controllers[static_cast<size_t>(cc)]);
    for (int gen = 0; gen < GEN_LAST; ++gen)
        state.generators[static_cast<size_t>(gen)] = fluid_synth_get_gen(synth.get(), channel, gen);
    fluid_synth_get_pitch_bend(synth.get(), channel, &state.pitch);
    fluid_synth_get_pitch_wheel_sens(synth.get(), channel, &state.pitchSensitivity);
    return state;
}

void FluidSynthModel::restoreChannel(Engine& engine, int channel, const ChannelSnapshot& state, bool releasePedals) {
    auto& synth = engine.synth;
    // Restore ordinary controllers, never replay channel-mode commands or
    // RPN/NRPN data-entry transactions in numeric CC order.
    for (int cc = 0; cc < 120; ++cc) {
        if (cc == 6 || cc == 38 || (cc >= 96 && cc <= 101)) continue;
        const bool pedal = cc == 64 || cc == 66 || cc == 69;
        const int desired = releasePedals && pedal ? 0 : state.controllers[static_cast<size_t>(cc)];
        int current = 0;
        fluid_synth_get_cc(synth.get(), channel, cc, &current);
        if (current != desired) fluid_synth_cc(synth.get(), channel, cc, desired);
    }
    for (int gen = 0; gen < GEN_LAST; ++gen) {
        const float desired = state.generators[static_cast<size_t>(gen)];
        if (fluid_synth_get_gen(synth.get(), channel, gen) != desired)
            fluid_synth_set_gen(synth.get(), channel, gen, desired);
    }
    fluid_synth_pitch_wheel_sens(synth.get(), channel, state.pitchSensitivity);
    fluid_synth_pitch_bend(synth.get(), channel, state.pitch);
    engine.channels[static_cast<size_t>(channel)].pressure = state.pressure;
    fluid_synth_channel_pressure(synth.get(), channel, state.pressure);
    // Leave data entry deselected. Complete tuning/SysEx and in-progress RPN
    // transactions need the future channel-state model; do not invent them here.
    fluid_synth_cc(synth.get(), channel, 101, 127);
    fluid_synth_cc(synth.get(), channel, 100, 127);
    auto& applied = engine.channels[static_cast<size_t>(channel)];
    for (int cc = 0; cc < 120; ++cc)
        applied.appliedControllers[static_cast<size_t>(cc)] = releasePedals && (cc == 64 || cc == 66 || cc == 69)
            ? 0 : state.controllers[static_cast<size_t>(cc)];
    applied.appliedPitch = state.pitch;
    applied.appliedPressure = state.pressure;
    applied.appliedRange = state.pitchSensitivity;
}

void FluidSynthModel::resetEngine(Engine& engine) {
    auto& synth = engine.synth;
    if (!synth) return;
    array<ChannelSnapshot, partCount> saved;
    array<array<int, 3>, partCount> presets;
    for (int channel = 0; channel < partCount; ++channel) {
        saved[static_cast<size_t>(channel)] = captureChannel(engine, channel);
        auto& preset = presets[static_cast<size_t>(channel)];
        preset = {-1, 0, 0};
        if (engine.font >= 0) fluid_synth_get_program(synth.get(), channel, &preset[0], &preset[1], &preset[2]);
    }
    fluid_synth_system_reset(synth.get());
    // Voice-off ramps may feed the effects during the first render quantum.
    // Finish that ramp (plus any cached fragment), then clear the effects again.
    fluid_synth_process(synth.get(), 128, 0, nullptr, 0, nullptr);
    fluid_synth_system_reset(synth.get());
    for (int channel = 0; channel < partCount; ++channel) {
        restoreChannel(engine, channel, saved[static_cast<size_t>(channel)], true);
        const auto& preset = presets[static_cast<size_t>(channel)];
        if (preset[0] >= 0) fluid_synth_program_select(synth.get(), channel, preset[0], preset[1], preset[2]);
        else fluid_synth_unset_program(synth.get(), channel);
        engine.channels[static_cast<size_t>(channel)].lastSelection = ~0u;
        // Host stop releases pedals without allowing cached GUI intent to re-hold them.
        for (int cc : {64, 66, 69}) parts[static_cast<size_t>(channel)].controllers[static_cast<size_t>(cc)].store(0);
        publishParameters(channel);
    }
    // Control intent may already refer to a pending replacement. Reset this
    // engine's own preset without publishing a fallback into the shared intent.
    // Drain the bounded reset/mixer pipeline without exposing pre-reset audio.
    // No allocation, bank I/O or synth reconstruction; output mixing is skipped.
    fluid_synth_process(synth.get(), 256, 0, nullptr, 0, nullptr);
}

void FluidSynthModel::handleSysEx(Engine& engine, const unsigned char* data, int size) {
    auto& synth = engine.synth;
    int handled = 0;
    const int result = fluid_synth_sysex(
        synth.get(),
        reinterpret_cast<const char*>(data),
        size,
        nullptr, // no response pointer because we have no interest in handling response currently
        nullptr, // no response_len pointer because we have no interest in handling response currently
        &handled,
        static_cast<int>(false));
    if (result == FLUID_OK && handled) {
        const bool gmReset = size == 4 && data[0] == 0x7e && data[2] == 0x09
            && data[3] >= 1 && data[3] <= 3;
        const bool gsReset = size == 9 && data[0] == 0x41 && data[2] == 0x42
            && data[3] == 0x12 && data[4] == 0x40 && data[5] == 0
            && data[6] == 0x7f && data[7] == 0;
        for (int channel = 0; channel < partCount; ++channel) {
            if (gmReset || gsReset) engine.channels[static_cast<size_t>(channel)].pressure = 0;
            synchronisePart(engine, channel);
        }
    }
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    const juce::ScopedValueSetter<FluidSynthModel*> renderGuard(renderingModel, this);
    adoptPending();
    if (routingChanged.exchange(false)) resetPlayback();
    const bool omni = legacyOmni.load();
    auto& engine = *active;
    auto& synth = engine.synth;
    applyControls(engine);
    int rendered = 0;
    const auto renderUntil = [&](int end) {
        const int count = end - rendered;
        if (count <= 0) return;
        if (engine.font < 0 || desiredEmpty.load() || engine.request < unloadEpoch.load()) {
            // Empty projects are silent, including any residual effect output.
            // Advance the engine so stale buffers cannot return on a later load.
            fluid_synth_process(synth.get(), count, 0, nullptr, 0, nullptr);
            blendRetiringEngine(buffer, rendered, count, true);
            rendered = end;
            return;
        }
        if (buffer.getNumChannels() == 1) {
            // FluidSynth requires stereo pairs. A fixed scratch block avoids
            // allocations for mono hosts, including oversized host blocks.
            float left[64], right[64];
            for (int offset = 0; offset < count; offset += 64) {
                const int length = juce::jmin(64, count - offset);
                std::fill_n(left, length, 0.0f);
                std::fill_n(right, length, 0.0f);
                float* pair[] = {left, right};
                // Aliasing wet and dry buffers is supported by FluidSynth;
                // the effects channels wrap into this single stereo pair.
                fluid_synth_process(synth.get(), length, 2, pair, 2, pair);
                auto* mono = buffer.getWritePointer(0, rendered + offset);
                for (int i = 0; i < length; ++i) mono[i] += 0.5f * (left[i] + right[i]);
            }
        } else {
            float* outputs[] = {buffer.getWritePointer(0, rendered),
                                buffer.getWritePointer(1, rendered)};
            fluid_synth_process(synth.get(), count, 2, outputs, 2, outputs);
        }
        blendRetiringEngine(buffer, rendered, count, false);
        rendered = end;
    };
    for (const auto metadata : midiMessages) {
        renderUntil(juce::jlimit(rendered, buffer.getNumSamples(), metadata.samplePosition));
        if (metadata.numBytes > 0 && metadata.data[0] == 0xf0) {
            // Borrow the host buffer for this synchronous call. Constructing a
            // MidiMessage here copies long SysEx data using heap allocation.
            if (metadata.numBytes >= 2 && metadata.data[metadata.numBytes - 1] == 0xf7)
                handleSysEx(engine, metadata.data + 1, metadata.numBytes - 2);
            continue; // Ignore incomplete SysEx rather than stripping a data byte.
        }
        const auto m = metadata.getMessage();
        if (m.getChannel() < 1 || m.getChannel() > partCount) continue;
        const int channel = omni ? 0 : m.getChannel() - 1;
        auto& part = parts[static_cast<size_t>(channel)];
        auto& channelState = engine.channels[static_cast<size_t>(channel)];
        DEBUG_PRINT(m.getDescription());
        
        if (m.isNoteOn()) {
            if (engine.font < 0 || desiredEmpty.load() || engine.request < unloadEpoch.load()) continue;
            fluid_synth_noteon(
                synth.get(),
                channel,
                m.getNoteNumber(),
                m.getVelocity());
        } else if (m.isNoteOff()) {
            fluid_synth_noteoff(
                synth.get(),
                channel,
                m.getNoteNumber());
        } else if (m.isController()) {
            fluid_synth_cc(
                synth.get(),
                channel,
                m.getControllerNumber(),
                m.getControllerValue());

            const int cc = m.getControllerNumber(), value = m.getControllerValue();
            if (cc < 120) {
                part.controllers[static_cast<size_t>(cc)].store(value);
                channelState.appliedControllers[static_cast<size_t>(cc)] = value;
                if (auto* parameter = part.ccParameters[static_cast<size_t>(cc)]) *parameter = value;
            }
            if (cc == 121) {
                channelState.pressure = 0;
                synchronisePart(engine, channel);
            } else if (PartParameters::isTransaction(cc)) {
                int range = 2;
                fluid_synth_get_pitch_wheel_sens(synth.get(), channel, &range);
                part.range.store(range);
                *part.rangeParameter = range;
            }
        } else if (m.isProgramChange()) {
#if JUCE_DEBUG
            String debug{"MIDI program change: "};
            debug << m.getProgramChangeNumber();
            Logger::outputDebugString(debug);
#endif
            int result{fluid_synth_program_change(
                synth.get(),
                channel,
                m.getProgramChangeNumber())};
            if (result == FLUID_OK) publishCurrentSelection(engine, channel);
        } else if (m.isPitchWheel()) {
            part.pitch.store(m.getPitchWheelValue());
            *part.pitchParameter = m.getPitchWheelValue();
            channelState.appliedPitch = m.getPitchWheelValue();
            fluid_synth_pitch_bend(
                synth.get(),
                channel,
                m.getPitchWheelValue());
        } else if (m.isChannelPressure()) {
            channelState.pressure = m.getChannelPressureValue();
            part.pressure.store(m.getChannelPressureValue());
            *part.pressureParameter = m.getChannelPressureValue();
            channelState.appliedPressure = m.getChannelPressureValue();
            fluid_synth_channel_pressure(
                synth.get(),
                channel,
                m.getChannelPressureValue());
        } else if (m.isAftertouch()) {
            fluid_synth_key_pressure(
                synth.get(),
                channel,
                m.getNoteNumber(),
                m.getAfterTouchValue());

        }
    }

    // fluid_synth_get_cc(fluidSynth, 0, 73, &pval);
    // Logger::outputDebugString ( juce::String::formatted("hey: %d\n", pval) );

    renderUntil(buffer.getNumSamples());
}
