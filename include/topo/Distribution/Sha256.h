#ifndef TOPO_DISTRIBUTION_SHA256_H
#define TOPO_DISTRIBUTION_SHA256_H

/// Pure-C++ SHA-256 (FIPS 180-4). No external crypto dependency.
///
/// Used by the backend-distribution CLI to compute the canonical payload
/// digest that the package signature is (eventually) computed over, and to
/// verify per-artifact `sha256` fields in the registry index. SHA-256 gives
/// integrity verification with zero added dependencies; the Ed25519
/// *authenticity* layer is a separate, still-open piece (not implemented).

#include <cstddef>
#include <cstdint>
#include <string>

namespace topo::dist {

/// Incremental SHA-256 hasher. Construct, feed bytes with update(), then
/// call hexDigest() once. The object is single-use after finalization.
class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t len);
    void update(const std::string& s) { update(s.data(), s.size()); }

    /// Finalize and return the 64-char lowercase hex digest.
    std::string hexDigest();

private:
    void processBlock(const uint8_t* block);

    uint32_t state_[8];
    uint8_t buffer_[64];
    std::size_t bufferLen_ = 0;
    uint64_t totalBits_ = 0;
    bool finalized_ = false;
};

/// One-shot helpers.
std::string sha256Hex(const void* data, std::size_t len);
std::string sha256Hex(const std::string& s);

/// SHA-256 of a file's contents. Returns an empty string when the file
/// cannot be read.
std::string sha256File(const std::string& path);

} // namespace topo::dist

#endif // TOPO_DISTRIBUTION_SHA256_H
