#pragma once
#include "JuceHeader.h"

#if JUCE_MAC || JUCE_IOS
#include <CoreFoundation/CoreFoundation.h>
#include <memory>
#include <type_traits>

// Own the public CoreFoundation references without depending on JUCE internals.
namespace JuicyMac {
struct Release {
    void operator()(CFTypeRef object) const noexcept {
        if (object != nullptr) CFRelease(object);
    }
};
template <typename Ref>
using CFPtr = std::unique_ptr<std::remove_pointer_t<Ref>, Release>;

inline juce::MemoryBlock createReadOnlyBookmark(const juce::File& file) {
    CFPtr<CFStringRef> path{file.getFullPathName().toCFString()};
    CFPtr<CFURLRef> url{CFURLCreateWithFileSystemPath(nullptr, path.get(), kCFURLPOSIXPathStyle, file.isDirectory())};
    CFErrorRef error = nullptr;
    CFPtr<CFDataRef> data{url ? CFURLCreateBookmarkData(nullptr, url.get(),
        kCFURLBookmarkCreationWithSecurityScope | kCFURLBookmarkCreationSecurityScopeAllowOnlyReadAccess,
        nullptr, nullptr, &error) : nullptr};
    CFPtr<CFErrorRef> ownedError{error};
    juce::MemoryBlock bookmark;
    if (data) bookmark.append(CFDataGetBytePtr(data.get()), static_cast<size_t>(CFDataGetLength(data.get())));
    return bookmark;
}

struct ScopedFileAccess {
    explicit ScopedFileAccess(CFURLRef value) : url(value),
        started(value != nullptr && CFURLStartAccessingSecurityScopedResource(value)) {}
    ~ScopedFileAccess() {
        if (started) CFURLStopAccessingSecurityScopedResource(url.get());
    }
    CFPtr<CFURLRef> url;
    bool started;
};
}
#endif
