#pragma once

#include "JuceHeader.h"
#include <functional>
#include "MacFileAccess.h"
#include <vector>
#include <atomic>
#include <optional>

class SoundFontLibrary final : private juce::Thread, private juce::AsyncUpdater
{
public:
    struct Result {
        juce::Array<juce::File> files;
        bool limitReached = false;
        juce::Array<juce::File> savedFolders;
        juce::String error;
#if JUCE_MAC || JUCE_IOS
        std::vector<std::shared_ptr<JuicyMac::ScopedFileAccess>> access;
#endif
    };

    using Edit = std::function<juce::Result(const std::function<bool()>&)>;
    explicit SoundFontLibrary(juce::File settings = {});
    ~SoundFontLibrary() override;
    // Message-thread API. Owner must outlive editors and join at processor teardown.
    void scan(std::function<void(Result)> callback, Edit edit = {});
    void cancel();
#if JUICY_ENABLE_TEST_HOOKS
    void dispatchForTests() { handleAsyncUpdate(); }
#endif
    static juce::Array<juce::File> defaultFolders();
    static Result scanFolders(const juce::Array<juce::File>&,
                             const std::function<bool()>& cancelled);

private:
    void run() override;
    void handleAsyncUpdate() override;
    juce::CriticalSection resultLock;
    Result result;
    std::function<void(Result)> onComplete;
    struct Job { uint64_t generation; Edit edit; };
    void performScan(const Job&);
    std::optional<Job> queued;
    std::atomic<uint64_t> generation{0};
    uint64_t resultGeneration = 0;
    bool resultReady = false;
    bool workerStarted = false;
    juce::File settingsFile;
};
