// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#include "../src/gltf_file_sha256_internal.h"

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "hash_prefix_answers.h"
#include "hash_vectors.h"

namespace {
std::atomic<std::size_t> allocation_calls{0};
std::atomic<bool> disallow_allocation{false};
std::atomic<void*> allocation_control_pointer{nullptr};
void CountAllocation() {
  ++allocation_calls;
  if (disallow_allocation.load()) throw std::bad_alloc();
}
}  // namespace
// Test-executable-only original new observer. No production hook or allocator.
void* operator new(std::size_t bytes) {
  CountAllocation();
  if (auto* pointer = std::malloc(std::max<std::size_t>(bytes, 1)))
    return pointer;
  throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
  std::free(pointer);
}
void* operator new(std::size_t bytes, std::align_val_t alignment) {
  CountAllocation();
  const auto size = std::max<std::size_t>(bytes, 1);
  const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
  if (auto* pointer = _aligned_malloc(size, align)) return pointer;
#else
  if (size > std::numeric_limits<std::size_t>::max() - (align - 1))
    throw std::bad_alloc();
  const auto padded = (size + align - 1) / align * align;
  if (auto* pointer = std::aligned_alloc(align, padded)) return pointer;
#endif
  throw std::bad_alloc();
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
  return ::operator new(bytes, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
#if defined(_WIN32)
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}
void operator delete(void* pointer, std::size_t,
                     std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}
void operator delete[](void* pointer, std::size_t,
                       std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}

namespace {
namespace hash = meshvale::repair::gltf_file_detail;
constexpr std::size_t kChunkCeiling = 65;
constexpr std::size_t kExpectedHexCharacters = 64;
void Require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void ObserveOriginalAllocationRefusal() {
  constexpr std::size_t kAlignedControlBytes = 64;
  struct alignas(kAlignedControlBytes) AlignedControl {
    std::array<std::byte, kAlignedControlBytes> value{};
  };
  const auto before = allocation_calls.load();
  disallow_allocation = true;
  bool ordinary_refused = false;
  bool aligned_refused = false;
  try {
    auto* pointer = new std::byte{};
    // An observable escape keeps the control allocation from being elided.
    allocation_control_pointer.store(pointer);
    delete pointer;
    allocation_control_pointer.store(nullptr);
  } catch (const std::bad_alloc&) {
    ordinary_refused = true;
  }
  try {
    auto* pointer = new AlignedControl;
    allocation_control_pointer.store(pointer);
    delete pointer;
    allocation_control_pointer.store(nullptr);
  } catch (const std::bad_alloc&) {
    aligned_refused = true;
  }
  disallow_allocation = false;
  Require(ordinary_refused && aligned_refused &&
              allocation_calls.load() == before + 2,
          "original observer refuses actual ordinary and aligned new calls");
}
void CheckDigest(const hash::Sha256& stream, std::string_view expected) {
  const auto digest = stream.Digest();
  const auto repeated = stream.Digest();
  Require(digest == repeated, "repeatable immutable digest snapshot");
  const auto hex = hash::LowercaseSha256Hex(digest);
  Require(hex.size() == kExpectedHexCharacters &&
              std::string_view(hex.data(), hex.size()) == expected,
          "independent literal digest and exactly 64 lowercase hex bytes");
}
void KnownAnswersAndSplits() {
  for (const auto& vector : hash_fixture::kVectors) {
    hash::Sha256 one;
    Require(
        one.Update(vector.bytes) && one.BytesProcessed() == vector.bytes.size(),
        "one-shot update owns only a byte count");
    CheckDigest(one, vector.expected);
    for (std::size_t split = 0; split <= vector.bytes.size(); ++split) {
      hash::Sha256 stream;
      Require(stream.Update(vector.bytes.first(split)) && stream.Update({}) &&
                  stream.Update(vector.bytes.subspan(split)),
              "every two-way split including empty and exact block edges");
      CheckDigest(stream, vector.expected);
      Require(stream.BytesProcessed() == vector.bytes.size(),
              "padding and repeated digest do not mutate accumulated length");
    }
    for (std::size_t stride = 1; stride <= kChunkCeiling; ++stride) {
      hash::Sha256 stream;
      auto remaining = vector.bytes;
      while (!remaining.empty()) {
        const auto count = std::min(stride, remaining.size());
        Require(stream.Update(remaining.first(count)),
                "incremental fixed chunks");
        remaining = remaining.subspan(count);
      }
      CheckDigest(stream, vector.expected);
    }
  }
}
void SnapshotsAndCopies() {
  const auto& abc = hash_fixture::kVectors[1];
  hash::Sha256 stream;
  CheckDigest(stream, hash_fixture::kVectors[0].expected);
  Require(stream.Update(abc.bytes.first(1)), "first incremental byte");
  const auto first_snapshot = stream.Digest();
  auto copy = stream;
  Require(copy.Update(abc.bytes.subspan(1)) && stream.BytesProcessed() == 1 &&
              stream.Digest() == first_snapshot,
          "copy owns its independent stream and preserves original snapshot");
  CheckDigest(copy, abc.expected);
  Require(stream.Update(abc.bytes.subspan(1)), "append after digest snapshot");
  CheckDigest(stream, abc.expected);
  const auto before_empty = stream.Digest();
  Require(stream.Update({}) && stream.Digest() == before_empty &&
              stream.BytesProcessed() == abc.bytes.size(),
          "empty append leaves nonempty stream unchanged");
}
void EveryPaddingPositionAndContinuation() {
  const auto& all_bytes = hash_fixture::kVectors[3];
  Require(hash_fixture::kPrefixAnswers.size() == all_bytes.bytes.size() + 1,
          "independent prefix answers cover every literal input extent");
  for (std::size_t split = 0; split <= all_bytes.bytes.size(); ++split) {
    hash::Sha256 stream;
    Require(stream.Update(all_bytes.bytes.first(split)),
            "every possible block/padding remainder prefix");
    CheckDigest(stream, hash_fixture::kPrefixAnswers[split]);
    Require(stream.Update(all_bytes.bytes.subspan(split)),
            "continue every partial stream after repeated digest snapshots");
    CheckDigest(stream, all_bytes.expected);
  }
}
void LengthAdmission() {
  // Literal maximum byte-aligned length; no fabricated huge spans or private
  // state mutation. These exercise the exact predicate used before Update.
  constexpr std::uint64_t kLiteralMaximumBytes = 0x1fffffffffffffff;
  constexpr std::uint64_t kLiteralMaximumWord = 0xffffffffffffffff;
  Require(hash::kSha256MaxInputBytes == kLiteralMaximumBytes,
          "byte limit is floor u64 maximum divided by eight bits");
  Require(hash::CanAppendSha256Bytes(0, 0) &&
              hash::CanAppendSha256Bytes(0, kLiteralMaximumBytes) &&
              hash::CanAppendSha256Bytes(kLiteralMaximumBytes, 0) &&
              hash::CanAppendSha256Bytes(kLiteralMaximumBytes - 1, 1) &&
              hash::CanAppendSha256Bytes(1, kLiteralMaximumBytes - 1),
          "zero, exact maximum and inclusive addition are admitted");
  Require(!hash::CanAppendSha256Bytes(kLiteralMaximumBytes, 1) &&
              !hash::CanAppendSha256Bytes(kLiteralMaximumBytes - 1, 2) &&
              !hash::CanAppendSha256Bytes(kLiteralMaximumBytes + 1, 0) &&
              !hash::CanAppendSha256Bytes(0, kLiteralMaximumWord) &&
              !hash::CanAppendSha256Bytes(kLiteralMaximumWord, 1) &&
              !hash::CanAppendSha256Bytes(1, kLiteralMaximumBytes),
          "invalid total, byte-to-bit and addition overflow are refused");
}
void HexEndianAndMillionBytes() {
  hash::Sha256Digest bytes{};
  for (std::size_t byte = 0; byte < bytes.size(); ++byte)
    bytes[byte] = static_cast<std::byte>(byte);
  const auto hex = hash::LowercaseSha256Hex(bytes);
  Require(
      std::string_view(hex.data(), hex.size()) ==
          "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
      "literal high/low nibble order, lowercase and full digest extent");
  constexpr std::size_t kMillionChunkBytes = 1000;
  constexpr std::size_t kMillionChunks = 1000;
  std::array<std::byte, kMillionChunkBytes> block{};
  block.fill(std::byte{'a'});
  hash::Sha256 stream;
  for (std::size_t chunk = 0; chunk < kMillionChunks; ++chunk)
    Require(stream.Update(block), "fixed-block million-byte stream");
  CheckDigest(
      stream,
      "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
void LittleEndianFrameLength(hash::Sha256& stream, std::uint64_t length) {
  // Independent literal file-convention framing, not the SHA length encoding.
  constexpr std::size_t kFrameLengthBytes = 8;
  constexpr std::size_t kFrameBitsPerByte = 8;
  constexpr std::uint64_t kFrameByteMask = 0xff;
  std::array<std::byte, kFrameLengthBytes> bytes{};
  for (std::size_t byte = 0; byte < bytes.size(); ++byte)
    bytes[byte] = static_cast<std::byte>(
        (length >> (byte * kFrameBitsPerByte)) & kFrameByteMask);
  Require(stream.Update(bytes), "little-endian caller framing word");
}
void Frame(hash::Sha256& stream, std::span<const std::byte> bytes) {
  LittleEndianFrameLength(stream, static_cast<std::uint64_t>(bytes.size()));
  // Stream full physical bytes one at a time without a framed payload clone.
  for (const auto& byte : bytes)
    Require(stream.Update(std::span(&byte, 1)),
            "one-byte framed physical input");
}
void PhysicalFramingAndArtifacts() {
  constexpr std::string_view kLiteralMarker = "meshvale.gltf-file-snapshot/1";
  constexpr std::array<std::byte, 1> kLiteralSeparator{std::byte{0}};
  constexpr std::array<std::array<std::string_view, 3>, 2> kArtifactAnswers{
      {{"ec01fba3a4f3f2af509d13b18e6c53de24fd3be51bb6581077544e1b5e147f17",
        "3d82c0adf4bce598ecf6aa4a38a963d78742b77246896e1b5a55336d3febee5a",
        "f74cabc3532658af0aa3e7abdd6939d22aa5cd78849dc06e3817248fe1c3788d"},
       {"5bb9db4a07d267b435d3ef1426f48044dd6238f3d3c48191a598be58a743c7b6",
        "3d82c0adf4bce598ecf6aa4a38a963d78742b77246896e1b5a55336d3febee5a",
        "ea626a42b6fa3687b330df462d84302b6e04c8f483fcff82ad0717e71ed54e18"}}};
  for (std::size_t input = 0; input < hash_fixture::kFramings.size(); ++input) {
    const auto& source = hash_fixture::kFramings[input];
    hash::Sha256 stream;
    Require(stream.Update(std::as_bytes(std::span(kLiteralMarker))) &&
                stream.Update(kLiteralSeparator),
            "literal domain and NUL separator");
    Frame(stream, source.format);
    LittleEndianFrameLength(stream,
                            static_cast<std::uint64_t>(source.files.size()));
    for (std::size_t file = 0; file < source.files.size(); ++file) {
      Frame(stream, source.files[file].path);
      Frame(stream, source.files[file].bytes);
      hash::Sha256 artifact;
      Require(artifact.Update(source.files[file].bytes),
              "complete physical artifact");
      CheckDigest(artifact, kArtifactAnswers[input][file]);
    }
    Require(stream.BytesProcessed() == source.framed_bytes,
            "independent exact framing work count");
    CheckDigest(stream, source.expected);
  }
}
}  // namespace
int main() {
  static_assert(std::is_nothrow_copy_constructible_v<hash::Sha256>);
  static_assert(noexcept(hash::Sha256{}.Digest()));
  static_assert(noexcept(hash::Sha256{}.Update({})));
  try {
    ObserveOriginalAllocationRefusal();
    const auto before = allocation_calls.load();
    disallow_allocation = true;
    KnownAnswersAndSplits();
    SnapshotsAndCopies();
    EveryPaddingPositionAndContinuation();
    LengthAdmission();
    HexEndianAndMillionBytes();
    PhysicalFramingAndArtifacts();
    disallow_allocation = false;
    Require(
        allocation_calls.load() == before,
        "all actual update/digest/hex/length/framing calls allocate nothing");
    std::cout << "SHA256 literal/Python vectors, all splits, strides1..65, "
                 "snapshots, endian/framing/artifacts, million bytes and "
                 "u64 admission passed; zero new requests; state_bytes="
              << sizeof(hash::Sha256) << '\n';
    return 0;
  } catch (const std::exception& error) {
    disallow_allocation = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}
