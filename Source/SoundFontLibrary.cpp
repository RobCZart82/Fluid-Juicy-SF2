#include "SoundFontLibrary.h"
#include <algorithm>
#include "SoundFontFolders.h"
#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace {
bool isLibraryLink(const juce::File& file)
{
    if (!file.isSymbolicLink()) return false;
#if JUCE_WINDOWS
    // JUCE treats every reparse point as a link, including OneDrive placeholders.
    // Inspect the tag without reading or downloading the sample data.
    const auto handle = CreateFileW(file.getFullPathName().toWideCharPointer(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return true;
    FILE_ATTRIBUTE_TAG_INFO info{};
    const bool read = GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) != 0;
    CloseHandle(handle);
    return !read || IsReparseTagNameSurrogate(info.ReparseTag);
#else
    return true;
#endif
}
}

SoundFontLibrary::SoundFontLibrary(juce::File settings)
    : Thread("SoundFont library scan"),
      settingsFile(settings == juce::File() ? SoundFontFolders::defaultSettingsFile() : settings) {}
SoundFontLibrary::~SoundFontLibrary() {
    cancel();
    signalThreadShouldExit();
    notify();
    stopThread(-1); // Processor teardown joins: never leave plugin code detached.
}

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
        if (!folder.isDirectory() || isLibraryLink(folder)) continue;
        for (const auto& entry : juce::RangedDirectoryIterator(folder, false, "*", juce::File::findFilesAndDirectories)) {
            if (cancelled()) return found;
            if (++entries > 10000 || found.files.size() >= 256) {
                found.limitReached = true;
                break;
            }
            const auto file = entry.getFile();
            if (isLibraryLink(file) || file.isDirectory() || !file.hasReadAccess()) continue;
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

void SoundFontLibrary::scan(std::function<void(Result)> callback, Edit edit)
{
    cancel();
    onComplete = std::move(callback);
    {
        const juce::ScopedLock lock(resultLock);
        queued = Job{generation.load(), std::move(edit)};
    }
    if (!workerStarted) workerStarted = startThread();
    if (!workerStarted) {
        const juce::ScopedLock lock(resultLock);
        queued.reset();
        result.error = "Could not start the SoundFont folder scan.";
        resultGeneration = generation.load();
        resultReady = true;
        triggerAsyncUpdate();
    }
    notify();
}

void SoundFontLibrary::cancel()
{
    ++generation;
    cancelPendingUpdate();
    onComplete = {};
    const juce::ScopedLock lock(resultLock);
    queued.reset();
    result = {};
    resultReady = false;
}

void SoundFontLibrary::run()
{
    while (!threadShouldExit()) {
        std::optional<Job> job;
        {
            const juce::ScopedLock lock(resultLock);
            job = std::move(queued);
            queued.reset();
        }
        if (!job) { wait(100); continue; }
        performScan(*job);
    }
}

void SoundFontLibrary::performScan(const Job& job)
{
    auto folders = defaultFolders();
    const std::function<bool()> cancelled = [this, id = job.generation] {
        return threadShouldExit() || generation.load() != id;
    };
    Result savedState;
    if (job.edit) {
        const auto edited = job.edit(cancelled);
        if (edited.failed()) savedState.error = edited.getErrorMessage();
    }
    if (cancelled()) return;
    const auto saved = SoundFontFolders(settingsFile).read(cancelled);
    if (saved.error.isNotEmpty()) savedState.error = saved.error;
    if (cancelled()) return;
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
    auto found = scanFolders(folders, cancelled);
    found.savedFolders = std::move(savedState.savedFolders);
    found.error = std::move(savedState.error);
#if JUCE_MAC || JUCE_IOS
    found.access = std::move(savedState.access);
#endif
    if (cancelled()) return;
    {
        const juce::ScopedLock lock(resultLock);
        if (cancelled()) return;
        resultGeneration = job.generation;
        resultReady = true;
        result = std::move(found);
    }
    triggerAsyncUpdate();
}

void SoundFontLibrary::handleAsyncUpdate()
{
    Result found;
    {
        const juce::ScopedLock lock(resultLock);
        if (!resultReady || resultGeneration != generation.load()) return;
        resultReady = false;
        found = std::move(result);
    }
    auto callback = onComplete;
    if (callback) callback(std::move(found));
}
