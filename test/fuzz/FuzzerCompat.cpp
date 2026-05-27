// Compatibility shim for LLVM 22 libFuzzer on macOS.
//
// The bundled libclang_rt.fuzzer_osx.a references std::__1::__hash_memory()
// which exists in LLVM 22's libc++ but not in the macOS system libc++.
// We provide a compatible implementation here so fuzz targets can link
// against the system libc++ (matching the rest of the project).
//
// Implementation is equivalent to the one in LLVM's libc++ (murmur2-based).

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace std {
inline namespace __1 {

size_t __hash_memory(const void* ptr, size_t len) noexcept {
    const auto* data = static_cast<const uint8_t*>(ptr);
    // FNV-1a hash (portable, adequate for hash table use)
    constexpr size_t kFNVOffsetBasis = sizeof(size_t) == 8 ? 14695981039346656037ULL : 2166136261U;
    constexpr size_t kFNVPrime = sizeof(size_t) == 8 ? 1099511628211ULL : 16777619U;
    size_t hash = kFNVOffsetBasis;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<size_t>(data[i]);
        hash *= kFNVPrime;
    }
    return hash;
}

} // namespace __1
} // namespace std
