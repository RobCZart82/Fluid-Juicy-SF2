#include "SoundFontLibrary.h"
#include <algorithm>
#include "SoundFontFolders.h"

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
    if (!startThread()) {
        result.error = "Could not start the SoundFont folder scan.";
        triggerAsyncUpdate();
    }
}

void SoundFontLibrary::cancel()
{
    signalThreadShouldExit();
    stopThread(-1);
    cancelPendingUpdate();
    onComplete = {};
    const juce::ScopedLock lock(resultLock);
    result = {};
}

void SoundFontLibrary::run()
{
    auto folders = defaultFolders();
    const auto saved = SoundFontFolders().read();
    Result savedState;
    savedState.error = saved.error;
    for (const auto& folder : saved.folders) {
        savedState.savedFolders.add(folder.path);
        auto path = folder.path;
#if JUCE_MAC || JUCE_IOS
        if (folder.bookmark.getSize() > 0) {
            JuicyMac::CFPtr<CFDataRef> data{CFDataCreate(nullptr,
                static_cast<const UInt8*>(folder.bookmark.getData()), static_cast<CFIndex>(folder.bookmark.getSize()))};
            CFErrorRef error = nullptr;
            Boolean stale = false;
            auto url = data ? CFURLCreateByResolvingBookmarkData(nullptr, data.get(),
                kCFURLBookmarkResolutionWithSecurityScope, nullptr, nullptr, &stale, &error) : nullptr;
            JuicyMac::CFPtr<CFErrorRef> ownedError{error};
            auto access = std::make_shared<JuicyMac::ScopedFileAccess>(url);
            if (access->url) {
                JuicyMac::CFPtr<CFStringRef> resolved{CFURLCopyFileSystemPath(access->url.get(), kCFURLPOSIXPathStyle)};
                if (resolved) path = juce::File(juce::String::fromCFString(resolved.get()));
                savedState.access.push_back(std::move(access));
            } else savedState.error = "Some folder permissions need to be renewed with Add folder.";
        }
#endif
        folders.addIfNotAlreadyThere(path);
    }
    auto found = scanFolders(folders, [this] { return threadShouldExit(); });
    found.savedFolders = std::move(savedState.savedFolders);
    found.error = std::move(savedState.error);
#if JUCE_MAC || JUCE_IOS
    found.access = std::move(savedState.access);
#endif
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
