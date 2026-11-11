#pragma once

#include "JuceHeader.h"
#include <functional>

class SoundFontFolders
{
public:
    struct Folder { juce::File path; juce::MemoryBlock bookmark; };
    struct Snapshot { juce::Array<Folder> folders; juce::String error; };
    explicit SoundFontFolders(juce::File settings = defaultSettingsFile());
    using Cancelled = std::function<bool()>;
    Snapshot read(const Cancelled& cancelled = {}) const;
    juce::Result add(const juce::File&, const juce::MemoryBlock&, const Cancelled& cancelled = {});
    juce::Result remove(const juce::File&, const Cancelled& cancelled = {});
    static juce::File defaultSettingsFile();

private:
    Snapshot readUnlocked() const;
    juce::Result change(const Folder&, bool remove, const Cancelled&);
    juce::File settingsFile;
    juce::String lockName;
};
