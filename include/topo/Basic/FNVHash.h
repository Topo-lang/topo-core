#ifndef TOPO_BASIC_FNVHASH_H
#define TOPO_BASIC_FNVHASH_H

/// FNV-1a 64-bit hash — collision-resistant, deterministic, and portable.
///
/// Used for cache fingerprinting where std::hash is unsuitable because:
/// - std::hash is not required to be deterministic across program invocations
/// - std::hash implementations vary across platforms and standard libraries
/// - std::hash often has poor collision resistance (identity hash for integers,
///   weak mixing for strings on some implementations)
///
/// FNV-1a processes every input byte with XOR + multiply, producing well-
/// distributed 64-bit digests suitable for cache invalidation keys.

#include <cstddef>
#include <cstdint>
#include <string>

namespace topo {

/// FNV-1a constants for 64-bit output.
inline constexpr uint64_t kFNV1aOffsetBasis = 14695981039346656037ULL;
inline constexpr uint64_t kFNV1aPrime = 1099511628211ULL;

/// Hash a raw byte buffer with FNV-1a (64-bit).
inline uint64_t fnv1aHash(const void* data, std::size_t len) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = kFNV1aOffsetBasis;
    for (std::size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(bytes[i]);
        hash *= kFNV1aPrime;
    }
    return hash;
}

/// Hash a std::string with FNV-1a (64-bit).
inline uint64_t fnv1aHash(const std::string& s) noexcept {
    return fnv1aHash(s.data(), s.size());
}

} // namespace topo

#endif // TOPO_BASIC_FNVHASH_H
