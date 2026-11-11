#pragma once

#include "JuceHeader.h"
#include <functional>

class SoundFontLibrary final : private juce::Thread, private juce::AsyncUpdater
{
public:
    struct Result {
        juce::Array<juce::File> files;
        bool limitReached = false;
    };

    SoundFontLibrary();
    ~SoundFontLibrary() override;
    void scan(std::function<void(Result)> callback);
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
};
