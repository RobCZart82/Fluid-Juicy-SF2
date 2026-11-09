#pragma once

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
