#pragma once
#include "JuceHeader.h"

namespace BookmarkCodec {
// Native bookmarks are small. Retain room for older paths/grants while bounding
// the JUCE legacy length-prefixed encoding before its decoder can allocate.
inline constexpr size_t maxBytes = 256 * 1024;

inline bool decode(const String& encoded, MemoryBlock& output, size_t limit = maxBytes) {
    if (encoded.isEmpty()) { output.reset(); return true; } // Historical empty state.
    if (encoded.length() > static_cast<int>((limit * 8 + 5) / 6 + 8)) return false;
    const int dot = encoded.indexOfChar('.');
    if (dot <= 0 || dot > 7) return false;
    size_t size = 0;
    for (int i = 0; i < dot; ++i) {
        const auto digit = encoded[i];
        if (digit < '0' || digit > '9') return false;
        size = size * 10 + static_cast<size_t>(digit - '0');
        if (size > limit) return false;
    }
    const auto payload = encoded.substring(dot + 1);
    if (static_cast<size_t>(payload.length()) != (size * 8 + 5) / 6) return false;
    if (!payload.containsOnly(".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+")) return false;
    MemoryBlock decoded;
    if (!decoded.fromBase64Encoding(encoded) || decoded.getSize() != size) return false;
    output = std::move(decoded);
    return true;
}
}
