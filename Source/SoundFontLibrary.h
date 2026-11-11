#pragma once

#include "JuceHeader.h"
#include <functional>
#include "MacFileAccess.h"
#include <vector>

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
    void scan(std::function<void(Result)> callback, Edit edit = {});
    void cancel();
    static juce::Array<juce::File> defaultFolders();
    static Result scanFolders(const juce::Array<juce::File>&,
                             const std::function<bool()>& cancelled);

private:
    void run() override;
    void handleAsyncUpdate() override;
    juce::CriticalSection resultLock;
    Result result;
    std::function<void(Result)> onComplete;
    Edit pendingEdit;
    juce::File settingsFile;
};
