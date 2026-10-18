// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_SRC_GLTF_FILE_SHA256_INTERNAL_H_
#define MESHVALE_REPAIR_SRC_GLTF_FILE_SHA256_INTERNAL_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace meshvale::repair::gltf_file_detail {
inline constexpr std::size_t kSha256DigestBytes = 32;
inline constexpr std::size_t kSha256HexCharacters = 2 * kSha256DigestBytes;
inline constexpr std::size_t kSha256BlockBytes = 64;
inline constexpr std::size_t kSha256StateWords = 8;
inline constexpr std::size_t kSha256BitsPerByte = 8;
inline constexpr std::uint64_t kSha256MaxInputBytes =
    std::numeric_limits<std::uint64_t>::max() / kSha256BitsPerByte;
using Sha256Digest = std::array<std::byte, kSha256DigestBytes>;
using Sha256Hex = std::array<char, kSha256HexCharacters>;

// Byte-aligned SHA-256 messages must have a representable u64 bit length.
// Also refuses an already-invalid accumulated count; subtraction never wraps.
[[nodiscard]] bool CanAppendSha256Bytes(std::uint64_t current_bytes,
                                        std::uint64_t added_bytes) noexcept;

// Fixed-state integrity digest, not an authenticity or FIPS-validation claim.
// Each instance owns one serial stream. Concurrent mutation requires external
// synchronization; independent instances share no mutable state. Calls allocate
// nothing and retain no input span. Callers own framing, work
// limits/checkpoints, worker scheduling and all metadata/storage accounting.
class Sha256 {
 public:
  Sha256() noexcept;
  // Checks the complete append before any mutation. False leaves the stream
  // unchanged. Empty input succeeds for every valid stream state.
  [[nodiscard]] bool Update(std::span<const std::byte> bytes) noexcept;
  // Finishes a fixed-state copy: repeated snapshots and subsequent Update are
  // allowed. Padding/length/digest words use SHA-256 big-endian byte order,
  // independent of a caller's source-framing byte order.
  [[nodiscard]] Sha256Digest Digest() const noexcept;
  [[nodiscard]] std::uint64_t BytesProcessed() const noexcept;

 private:
  void Compress(std::span<const std::byte, kSha256BlockBytes> block) noexcept;
  std::array<std::uint32_t, kSha256StateWords> state_;
  std::array<std::byte, kSha256BlockBytes> pending_{};
  std::size_t pending_bytes_ = 0;
  std::uint64_t total_bytes_ = 0;
};

// Exactly 64 lowercase hexadecimal bytes; no terminator or allocation.
[[nodiscard]] Sha256Hex LowercaseSha256Hex(const Sha256Digest& digest) noexcept;
}  // namespace meshvale::repair::gltf_file_detail
#endif  // MESHVALE_REPAIR_SRC_GLTF_FILE_SHA256_INTERNAL_H_
