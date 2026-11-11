#pragma once

#include "JuceHeader.h"

class SoundFontFolders
{
public:
    struct Folder { juce::File path; juce::MemoryBlock bookmark; };
    struct Snapshot { juce::Array<Folder> folders; juce::String error; };
    explicit SoundFontFolders(juce::File settings = defaultSettingsFile());
    Snapshot read() const;
    juce::Result add(const juce::File&, const juce::MemoryBlock&);
    juce::Result remove(const juce::File&);
    static juce::File defaultSettingsFile();

private:
    Snapshot readUnlocked() const;
    juce::Result change(const Folder&, bool remove);
    juce::File settingsFile;
    juce::String lockName;
};
