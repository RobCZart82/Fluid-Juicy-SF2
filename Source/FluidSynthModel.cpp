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
const map<fluid_midi_control_change, String> FluidSynthModel::controllerToParam{
    {SOUND_CTRL2, "filterResonance"}, // MIDI CC 71 Timbre/Harmonic Intensity (filter resonance)
    {SOUND_CTRL3, "release"}, // MIDI CC 72 Release time
    {SOUND_CTRL4, "attack"}, // MIDI CC 73 Attack time
    {SOUND_CTRL5, "filterCutOff"}, // MIDI CC 74 Brightness (cutoff frequency, FILTERFC)
    {SOUND_CTRL6, "decay"}, // MIDI CC 75 Decay Time
    {SOUND_CTRL10, "sustain"}}; // MIDI CC 79 undefined

const map<String, fluid_midi_control_change> FluidSynthModel::paramToController{[]{
    map<String, fluid_midi_control_change> map;
    transform(
        controllerToParam.begin(),
        controllerToParam.end(),
        inserter(map, map.begin()),
        [](const pair<fluid_midi_control_change, String>& pair) {
            return make_pair(pair.second, pair.first);
        });
    return map;
}()};


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
    for (auto& value : requestedControllers) value.store(-1);
    selection.store(packed(static_cast<int>(state.getRawParameterValue("bank")->load()),
                           static_cast<int>(state.getRawParameterValue("preset")->load())));
    for (const auto& entry : controllerToParam) {
        requestedControllers[static_cast<size_t>(entry.first)].store(
            static_cast<int>(state.getRawParameterValue(entry.second)->load()));
        state.addParameterListener(entry.second, this);
    }
    state.addParameterListener("bank", this);
    state.addParameterListener("preset", this);
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
    valueTreeState.removeParameterListener("bank", this);
    valueTreeState.removeParameterListener("preset", this);
    for (const auto& entry : controllerToParam)
        valueTreeState.removeParameterListener(entry.second, this);
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
    engine->appliedControllers.fill(-1);
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
    for (const auto &[controller, param]: controllerToParam) {
        fluid_synth_cc(synth.get(), 0, static_cast<int>(controller), 0);
    }
    
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
    engine->builtSelectionRequest = selection.load();
    const int bank = static_cast<int>(engine->builtSelectionRequest >> 16);
    const int program = static_cast<int>(engine->builtSelectionRequest & 0xffffu);
    if (fluid_synth_program_select(engine->synth.get(), 0, engine->font, bank, program) != FLUID_OK) {
        const auto inBank = find_if(metadata->presets.begin(), metadata->presets.end(),
            [bank](const Preset& preset) { return preset.bank == bank; });
        const auto& first = inBank != metadata->presets.end() ? *inBank : metadata->presets.front();
        if (fluid_synth_program_select(engine->synth.get(), 0, engine->font, first.bank, first.program) != FLUID_OK)
            return nullptr;
    }
    int selectedFont, selectedBank, selectedProgram;
    fluid_synth_get_program(engine->synth.get(), 0, &selectedFont, &selectedBank, &selectedProgram);
    engine->preparedSelection = packed(selectedBank, selectedProgram);
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
                auto expected = candidate->builtSelectionRequest;
                selection.compare_exchange_strong(expected, candidate->preparedSelection);
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
        const auto state = captureChannel(*active);
        restoreChannel(*next, state, false);
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
        const auto state = captureChannel(*active);
        restoreChannel(*next, state, false);
        active.reset(next); // Host has stopped rendering: reclamation is non-RT.
        activeRequest.store(next->request);
    }
}
void FluidSynthModel::parameterChanged(const String& id, float value) {
    if (publishingSelection == this) return;
    if (id == "bank" || id == "preset") {
        auto before = selection.load();
        unsigned int after;
        do {
            after = id == "bank" ? packed(static_cast<int>(value), static_cast<int>(before & 0xffffu))
                                 : packed(static_cast<int>(before >> 16), static_cast<int>(value));
        } while (!selection.compare_exchange_weak(before, after));
    } else if (const auto it = paramToController.find(id); it != paramToController.end()) {
        requestedControllers[static_cast<size_t>(it->second)].store(static_cast<int>(value));
    }
}
void FluidSynthModel::setControllerValue(int controller, int value) {
    const auto it = controllerToParam.find(static_cast<fluid_midi_control_change>(controller));
    if (it != controllerToParam.end())
        *static_cast<AudioParameterInt*>(valueTreeState.getParameter(it->second)) = juce::jlimit(0, 127, value);
}
void FluidSynthModel::applyControls(Engine& engine) {
    for (const auto& entry : controllerToParam) {
        const auto cc = static_cast<size_t>(entry.first);
        const int value = requestedControllers[cc].load();
        if (value != engine.appliedControllers[cc]) {
            fluid_synth_cc(engine.synth.get(), 0, static_cast<int>(cc), value);
            engine.appliedControllers[cc] = value;
        }
    }
    const auto requested = selection.load();
    if (engine.font >= 0 && engine.lastSelection != requested) {
        const auto& presets = engine.metadata->presets;
        const int bank = static_cast<int>(requested >> 16);
        const int program = static_cast<int>(requested & 0xffffu);
        auto chosen = find_if(presets.begin(), presets.end(), [=](const Preset& preset) {
            return preset.bank == bank && preset.program == program;
        });
        if (chosen == presets.end())
            chosen = find_if(presets.begin(), presets.end(), [=](const Preset& preset) { return preset.bank == bank; });
        if (chosen == presets.end()) chosen = presets.begin();
        if (chosen != presets.end()) {
            fluid_synth_program_select(engine.synth.get(), 0, engine.font, chosen->bank, chosen->program);
            const auto actual = packed(chosen->bank, chosen->program);
            auto expected = requested;
            // A control change arriving during adoption must win next block.
            if (actual != requested && selection.compare_exchange_strong(expected, actual)) {
                if (renderingModel == this) publishParameters();
                else uiDirty.store(true); // Never notify a host while holding the builder/lifecycle lock.
            }
            engine.lastSelection = actual;
        }
    }
}
void FluidSynthModel::publishParameters() {
    const juce::ScopedValueSetter<FluidSynthModel*> guard(publishingSelection, this);
    const auto current = selection.load();
    *static_cast<AudioParameterInt*>(valueTreeState.getParameter("bank")) = static_cast<int>(current >> 16);
    *static_cast<AudioParameterInt*>(valueTreeState.getParameter("preset")) = static_cast<int>(current & 0xffffu);
}
void FluidSynthModel::publishCurrentSelection(Engine& engine) {
    int font, bank, program;
    if (fluid_synth_get_program(engine.synth.get(), 0, &font, &bank, &program) != FLUID_OK) return;
    selection.store(packed(bank, program));
    const bool playable = font == engine.font && any_of(engine.metadata->presets.begin(),
        engine.metadata->presets.end(), [=](const Preset& preset) {
            return preset.bank == bank && preset.program == program;
        });
    if (playable) engine.lastSelection = packed(bank, program);
    else {
        // FluidSynth may successfully assign a null preset for an unsupported
        // MIDI bank/program. Resolve against this engine's immutable metadata.
        engine.lastSelection = ~0u;
        applyControls(engine);
    }
    publishParameters();
}
FluidSynthModel::SoundFontState FluidSynthModel::getSoundFontState() const {
    lock_guard<mutex> lock(stateMutex);
    auto snapshot = committed;
    snapshot.bankProgram = selection.load();
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
    const auto before = selection.load();
    publishParameters();
    if (selection.load() != before) uiDirty.store(true);
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
int FluidSynthModel::getCurrentProgram() { return static_cast<int>(selection.load() & 0xffffu); }
void FluidSynthModel::setCurrentProgram(int index) {
    *static_cast<AudioParameterInt*>(valueTreeState.getParameter("preset")) = juce::jlimit(0, 127, index);
}
const String FluidSynthModel::getProgramName(int index) {
    shared_ptr<const Metadata> metadata;
    {
        lock_guard<mutex> lock(stateMutex);
        metadata = publishedMetadata;
    }
    const auto bank = static_cast<int>(selection.load() >> 16);
    for (const auto& preset : metadata->presets)
        if (preset.bank == bank && preset.program == index) return preset.name;
    return "Preset " + String(index);
}
void FluidSynthModel::setSampleRate(float rate) {
    // Serialized host lifecycle callback, outside processBlock.
    lock_guard<mutex> lock(buildMutex);
    adoptWhileStopped();
    if (targetRate.load() == rate) return;
    targetRate.store(rate);
    const auto state = captureChannel(*active);
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
        auto expected = replacement->builtSelectionRequest;
        selection.compare_exchange_strong(expected, replacement->preparedSelection);
        uiDirty.store(true);
    }
    restoreChannel(*replacement, state, false);
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
FluidSynthModel::ChannelSnapshot FluidSynthModel::captureChannel(Engine& engine) {
    auto& synth = engine.synth;
    constexpr int channel = 0;
    ChannelSnapshot state;
    state.pressure = engine.pressure;
    for (int cc = 0; cc < 120; ++cc)
        fluid_synth_get_cc(synth.get(), channel, cc, &state.controllers[static_cast<size_t>(cc)]);
    for (int gen = 0; gen < GEN_LAST; ++gen)
        state.generators[static_cast<size_t>(gen)] = fluid_synth_get_gen(synth.get(), channel, gen);
    fluid_synth_get_pitch_bend(synth.get(), channel, &state.pitch);
    fluid_synth_get_pitch_wheel_sens(synth.get(), channel, &state.pitchSensitivity);
    return state;
}

void FluidSynthModel::restoreChannel(Engine& engine, const ChannelSnapshot& state, bool releasePedals) {
    auto& synth = engine.synth;
    constexpr int channel = 0;
    // Restore ordinary controllers, never replay channel-mode commands or
    // RPN/NRPN data-entry transactions in numeric CC order.
    for (int cc = 0; cc < 120; ++cc) {
        if (cc == 6 || cc == 38 || (cc >= 96 && cc <= 101)) continue;
        const bool pedal = cc == 64 || cc == 66 || cc == 69;
        fluid_synth_cc(synth.get(), channel, cc,
            releasePedals && pedal ? 0 : state.controllers[static_cast<size_t>(cc)]);
    }
    for (int gen = 0; gen < GEN_LAST; ++gen)
        fluid_synth_set_gen(synth.get(), channel, gen, state.generators[static_cast<size_t>(gen)]);
    fluid_synth_pitch_wheel_sens(synth.get(), channel, state.pitchSensitivity);
    fluid_synth_pitch_bend(synth.get(), channel, state.pitch);
    engine.pressure = state.pressure;
    fluid_synth_channel_pressure(synth.get(), channel, state.pressure);
    // Leave data entry deselected. Complete tuning/SysEx and in-progress RPN
    // transactions need the future channel-state model; do not invent them here.
    fluid_synth_cc(synth.get(), channel, 101, 127);
    fluid_synth_cc(synth.get(), channel, 100, 127);
}

void FluidSynthModel::resetEngine(Engine& engine) {
    auto& synth = engine.synth;
    if (!synth) return;
    const auto savedChannel = captureChannel(engine);
    int font = -1, bank = 0, program = 0;
    const bool hadPreset = engine.font >= 0
        && fluid_synth_get_program(synth.get(), 0, &font, &bank, &program) == FLUID_OK;
    fluid_synth_system_reset(synth.get());
    // Voice-off ramps may feed the effects during the first render quantum.
    // Finish that ramp (plus any cached fragment), then clear the effects again.
    fluid_synth_process(synth.get(), 128, 0, nullptr, 0, nullptr);
    fluid_synth_system_reset(synth.get());
    restoreChannel(engine, savedChannel, true);
    if (hadPreset) fluid_synth_program_select(synth.get(), 0, font, bank, program);
    engine.lastSelection = ~0u;
    // Control intent may already refer to a pending replacement. Reset this
    // engine's own preset without publishing a fallback into the shared intent.
    // Drain the bounded reset/mixer pipeline without exposing pre-reset audio.
    // No allocation, bank I/O or synth reconstruction; output mixing is skipped.
    fluid_synth_process(synth.get(), 256, 0, nullptr, 0, nullptr);
}

void FluidSynthModel::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages) {
    const juce::ScopedValueSetter<FluidSynthModel*> renderGuard(renderingModel, this);
    adoptPending();
    auto& engine = *active;
    auto& synth = engine.synth;
    applyControls(engine);
    constexpr int channel = 0;
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
        const auto m = metadata.getMessage();
        renderUntil(juce::jlimit(rendered, buffer.getNumSamples(), metadata.samplePosition));
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

            const int cc = m.getControllerNumber();
            if (cc == 121) engine.pressure = 0;
            if (controllerToParam.find(static_cast<fluid_midi_control_change>(cc)) != controllerToParam.end()) {
                requestedControllers[static_cast<size_t>(cc)].store(m.getControllerValue());
                engine.appliedControllers[static_cast<size_t>(cc)] = m.getControllerValue();
            }
            fluid_midi_control_change controllerNum{static_cast<fluid_midi_control_change>(m.getControllerNumber())};
            if (auto it{controllerToParam.find(controllerNum)};
                it != end(controllerToParam)) {
                String parameterID{it->second};
                RangedAudioParameter *param{valueTreeState.getParameter(parameterID)};
                jassert(dynamic_cast<AudioParameterInt*>(param) != nullptr);
                AudioParameterInt* castParam{dynamic_cast<AudioParameterInt*>(param)};
                *castParam = m.getControllerValue();
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
            if (result == FLUID_OK) publishCurrentSelection(engine);
        } else if (m.isPitchWheel()) {
            fluid_synth_pitch_bend(
                synth.get(),
                channel,
                m.getPitchWheelValue());
        } else if (m.isChannelPressure()) {
            engine.pressure = m.getChannelPressureValue();
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
//        } else if (m.isMetaEvent()) {
//            fluid_midi_event_t *midi_event{new_fluid_midi_event()};
//            fluid_midi_event_set_type(midi_event, static_cast<int>(MIDI_SYSTEM_RESET));
//            fluid_synth_handle_midi_event(synth.get(), midi_event);
//            delete_fluid_midi_event(midi_event);
        } else if (m.isSysEx()) {
            int handled = 0;
            const int result = fluid_synth_sysex(
                synth.get(),
                reinterpret_cast<const char*>(m.getSysExData()),
                m.getSysExDataSize(),
                nullptr, // no response pointer because we have no interest in handling response currently
                nullptr, // no response_len pointer because we have no interest in handling response currently
                &handled,
                static_cast<int>(false));
            if (result == FLUID_OK && handled) {
                const auto* data = m.getSysExData();
                const auto size = m.getSysExDataSize();
                const bool gmReset = size == 4 && data[0] == 0x7e && data[2] == 0x09
                    && data[3] >= 1 && data[3] <= 3;
                const bool gsReset = size == 9 && data[0] == 0x41 && data[2] == 0x42
                    && data[3] == 0x12 && data[4] == 0x40 && data[5] == 0
                    && data[6] == 0x7f && data[7] == 0;
                if (gmReset || gsReset) engine.pressure = 0;
                // A GM/GS reset can change the synth without parameter callbacks.
                // Publish its actual controls before resolving its program so
                // applyControls cannot reapply values from before the reset.
                for (const auto& entry : controllerToParam) {
                    int value = 0;
                    fluid_synth_get_cc(synth.get(), channel, static_cast<int>(entry.first), &value);
                    requestedControllers[static_cast<size_t>(entry.first)].store(value);
                    engine.appliedControllers[static_cast<size_t>(entry.first)] = value;
                    *static_cast<AudioParameterInt*>(valueTreeState.getParameter(entry.second)) = value;
                }
                publishCurrentSelection(engine);
            }
        }
    }

    // fluid_synth_get_cc(fluidSynth, 0, 73, &pval);
    // Logger::outputDebugString ( juce::String::formatted("hey: %d\n", pval) );

    renderUntil(buffer.getNumSamples());
}
