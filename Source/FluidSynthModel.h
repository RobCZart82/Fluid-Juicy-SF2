// Created by Alex Birch on 10/09/2017.
#pragma once
#include "JuceHeader.h"
#include "MidiConstants.h"
#include "MacFileAccess.h"
#include <fluidsynth.h>
#include <array>
#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace std;

class FluidSynthModel : public AudioProcessorValueTreeState::Listener {
public:
    explicit FluidSynthModel(AudioProcessorValueTreeState&);
    ~FluidSynthModel() override;
    void initialise();
    int getChannel() { return 0; }
    void setControllerValue(int controller, int value);
    void processBlock(AudioBuffer<float>&, MidiBuffer&);
    // Host lifecycle methods: the host must have stopped processBlock.
    void setSampleRate(float);
    void resetPlayback();
    void parameterChanged(const String&, float) override;

    // Non-real-time host restoration remains synchronous for offline readiness.
    // Only the prepared engine is published; a concurrent render keeps its engine.
    bool loadSoundFont(const String&, const MemoryBlock&);
    // GUI browse/drop never waits for disk I/O or a slow preceding request.
    void requestSoundFont(const String&, const MemoryBlock&);
    bool isLoading() const { return loading.load(); }
    bool hasLoadedSoundFont() const { return publishedHasFont.load(); }
    struct SoundFontState { String path; MemoryBlock bookmark; String error; unsigned int bankProgram = 0; };
    SoundFontState getSoundFontState() const;
    String getLoadError() const { return getSoundFontState().error; }
    // Message-thread processor polling: no host callback is made under model locks.
    bool consumeHostStateChange() { return hostStateChanged.exchange(false); }
    void synchroniseUi(); // Message thread only; also used when opening an editor.
    int getNumPrograms() { return 128; }
    int getCurrentProgram();
    void setCurrentProgram(int);
    const String getProgramName(int);
    void changeProgramName(int, const String&) {}
    unsigned int getBankProgram() const { return selection.load(); }
#if JUICY_ENABLE_TEST_HOOKS
    int getPressureForTests() const { return active->pressure; }
    void setLoadDelayForTests(int ms) { loadDelay.store(ms); }
    void pauseLoadsForTests(bool pause) { loadPaused.store(pause); requestReady.notify_all(); }
    unsigned int getUiViolationsForTests() const { return uiViolations.load(); }
    uint64_t getStartedRequestForTests() const { return startedRequest.load(); }
    unsigned int getAudioReclaimsForTests() const { return audioReclaims.load(); }
    uint64_t getActiveRequestForTests() const { return activeRequest.load(); }
    uint64_t getLatestRequestForTests() const { return latestRequest.load(); }
#endif
private:
    struct Preset { int bank, program; String name; };
    struct Metadata { vector<Preset> presets; };
    struct Engine {
        explicit Engine(FluidSynthModel* model) : owner(model) { ++owner->liveEngines; }
        ~Engine();
        FluidSynthModel* owner;
        Engine* retiredNext = nullptr;
        bool crossfade = true;
#if JUCE_MAC || JUCE_IOS
        // Access outlives synth/sample destruction, including a retired engine.
        unique_ptr<JuicyMac::ScopedFileAccess> access;
#endif
        unique_ptr<fluid_settings_t, decltype(&delete_fluid_settings)> settings{nullptr, delete_fluid_settings};
        unique_ptr<fluid_synth_t, decltype(&delete_fluid_synth)> synth{nullptr, delete_fluid_synth};
        int pressure = 0;
        int font = -1;
        float rate = 44100;
        uint64_t request = 0;
        unsigned int lastSelection = ~0u;
        unsigned int builtSelectionRequest = 0, preparedSelection = 0;
        array<int, 128> appliedControllers{};
        shared_ptr<const Metadata> metadata;
        String path;
        MemoryBlock bookmark;
    };
    struct Request {
        String path;
        MemoryBlock bookmark;
        uint64_t id;
        bool crossfade = true;
        bool preserveReferenceOnFailure = false;
        bool notifyHost = false;
    };
    struct ChannelSnapshot {
        array<int, 120> controllers{};
        array<float, GEN_LAST> generators{};
        int pitch = 8192, pitchSensitivity = 2, pressure = 0;
    };
    unique_ptr<Engine> makeEngine(float);
    unique_ptr<Engine> prepareFont(const Request&);
    bool performLoad(const Request&);
    Request newRequest(const String&, const MemoryBlock&);
    bool publish(unique_ptr<Engine>, const Request&);
    void loaderLoop();
    void reclaimRetired();
    void retireEngine(Engine*);
    void adoptPending();
    void adoptWhileStopped();
    void applyControls(Engine&);
    void blendRetiringEngine(AudioBuffer<float>&, int start, int count, bool silent);
    void publishCurrentSelection(Engine&);
    void publishParameters();
    void publishUi();
    ChannelSnapshot captureChannel(Engine&);
    void restoreChannel(Engine&, const ChannelSnapshot&, bool releasePedals);
    void resetEngine(Engine&);
    static unsigned int packed(int bank, int program) { return (static_cast<unsigned int>(bank) << 16) | static_cast<unsigned int>(program); }
    static const map<fluid_midi_control_change, String> controllerToParam;
    static const map<String, fluid_midi_control_change> paramToController;
    static thread_local FluidSynthModel* publishingSelection;
    static thread_local FluidSynthModel* renderingModel;
    AudioProcessorValueTreeState& valueTreeState;
    // Only processBlock / serialized host lifecycle code touches active.
    unique_ptr<Engine> active;
    unique_ptr<Engine> fading;
    int fadeRemaining = 0;
    static constexpr int fadeSamples = 512;
    static_assert(atomic<Engine*>::is_always_lock_free, "Engine handover requires lock-free pointers");
    static_assert(atomic<uint64_t>::is_always_lock_free, "64-bit control state must be lock-free");
    atomic<Engine*> pending{nullptr};
    atomic<Engine*> retired{nullptr};
    atomic<unsigned int> liveEngines{0};
    mutex engineBudgetMutex;
    condition_variable engineAvailable;
    static constexpr unsigned int engineBudget = 4;
    atomic<float> targetRate{44100};
    atomic<unsigned int> selection{0};
    array<atomic<int>, 128> requestedControllers;
    atomic<bool> hostStateChanged{false};
    atomic<bool> desiredEmpty{true}, publishedHasFont{false}, loading{false}, uiDirty{true};
    atomic<uint64_t> latestRequest{0}, activeRequest{0}, unloadEpoch{0};
    mutable mutex stateMutex;
    SoundFontState committed;
    shared_ptr<const Metadata> publishedMetadata;
    shared_ptr<const Metadata> displayedMetadata;
    mutex requestMutex, buildMutex;
    condition_variable requestReady;
    optional<Request> queued;
    bool stopping = false;
    thread loader;
#if JUICY_ENABLE_TEST_HOOKS
    atomic<uint64_t> startedRequest{0};
    atomic<int> loadDelay{0};
    atomic<bool> loadPaused{false};
    atomic<unsigned int> uiViolations{0};
    atomic<unsigned int> audioReclaims{0};
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluidSynthModel)
};
