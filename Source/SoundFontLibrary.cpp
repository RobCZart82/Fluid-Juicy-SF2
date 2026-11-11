#include "SoundFontLibrary.h"
#include <algorithm>

SoundFontLibrary::SoundFontLibrary() : Thread("SoundFont library scan") {}
SoundFontLibrary::~SoundFontLibrary() { cancel(); }

juce::Array<juce::File> SoundFontLibrary::defaultFolders()
{
#if JUCE_WINDOWS
    return {juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("SoundFonts"),
            juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
                .getChildFile("Fluid Juicy SF2/SoundFonts")};
#else
    return {juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                .getChildFile("Audio/Sounds/Banks"),
            juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
                .getChildFile("Audio/Sounds/Banks")};
#endif
}

SoundFontLibrary::Result SoundFontLibrary::scanFolders(
    const juce::Array<juce::File>& folders, const std::function<bool()>& cancelled)
{
    Result found;
    int entries = 0;
    for (const auto& folder : folders) {
        if (cancelled()) break;
        if (!folder.isDirectory() || folder.isSymbolicLink()) continue;
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFilesAndDirectories)) {
            if (cancelled()) return found;
            if (++entries > 10000 || found.files.size() >= 256) {
                found.limitReached = true;
                break;
            }
            const auto file = entry.getFile();
            if (file.isSymbolicLink() || file.isDirectory() || !file.hasReadAccess()) continue;
            const auto extension = file.getFileExtension();
            if (extension.equalsIgnoreCase(".sf2") || extension.equalsIgnoreCase(".sf3"))
                found.files.addIfNotAlreadyThere(file);
        }
        if (found.limitReached) break;
    }
    std::sort(found.files.begin(), found.files.end(), [](const juce::File& a, const juce::File& b) {
        const int names = a.getFileName().compareNatural(b.getFileName());
        return names != 0 ? names < 0 : a.getFullPathName() < b.getFullPathName();
    });
    return found;
}

void SoundFontLibrary::scan(std::function<void(Result)> callback)
{
    cancel();
    onComplete = std::move(callback);
    startThread();
}

void SoundFontLibrary::cancel()
{
    signalThreadShouldExit();
    stopThread(-1);
    cancelPendingUpdate();
    onComplete = {};
}

void SoundFontLibrary::run()
{
    auto found = scanFolders(defaultFolders(), [this] { return threadShouldExit(); });
    if (threadShouldExit()) return;
    {
        const juce::ScopedLock lock(resultLock);
        result = std::move(found);
    }
    triggerAsyncUpdate();
}

void SoundFontLibrary::handleAsyncUpdate()
{
    waitForThreadToExit(-1);
    Result found;
    {
        const juce::ScopedLock lock(resultLock);
        found = std::move(result);
    }
    if (onComplete) onComplete(std::move(found));
}
