// SPDX-License-Identifier: Apache-2.0
#include "gltf_file_sha256_internal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace meshvale::repair::gltf_file_detail {
namespace {
// Algorithm parameters and numeric constants: NIST FIPS180-4 sections 4.1.2,
// 4.2.2, 5.1.1, 5.3.3 and 6.2. No third-party implementation is incorporated.
constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
constexpr std::size_t kLengthBytes = sizeof(std::uint64_t);
constexpr std::size_t kLengthOffset = kSha256BlockBytes - kLengthBytes;
constexpr std::size_t kRounds = 64;
constexpr std::size_t kBlockWords = kSha256BlockBytes / kWordBytes;
constexpr std::uint32_t kByteMask = 0xff;
constexpr std::byte kPaddingStart{0x80};
static_assert(std::numeric_limits<unsigned char>::digits == kSha256BitsPerByte);
constexpr std::array<std::uint32_t, kSha256StateWords> kInitialState{
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
constexpr std::array<std::uint32_t, kRounds> kRoundConstants{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
std::uint32_t ScheduleSigma0(std::uint32_t word) noexcept {
  constexpr int kFirstRotation = 7;
  constexpr int kSecondRotation = 18;
  constexpr int kShift = 3;
  return std::rotr(word, kFirstRotation) ^ std::rotr(word, kSecondRotation) ^
         (word >> kShift);
}
std::uint32_t ScheduleSigma1(std::uint32_t word) noexcept {
  constexpr int kFirstRotation = 17;
  constexpr int kSecondRotation = 19;
  constexpr int kShift = 10;
  return std::rotr(word, kFirstRotation) ^ std::rotr(word, kSecondRotation) ^
         (word >> kShift);
}
std::uint32_t RoundSigma0(std::uint32_t word) noexcept {
  constexpr int kFirstRotation = 2;
  constexpr int kSecondRotation = 13;
  constexpr int kThirdRotation = 22;
  return std::rotr(word, kFirstRotation) ^ std::rotr(word, kSecondRotation) ^
         std::rotr(word, kThirdRotation);
}
std::uint32_t RoundSigma1(std::uint32_t word) noexcept {
  constexpr int kFirstRotation = 6;
  constexpr int kSecondRotation = 11;
  constexpr int kThirdRotation = 25;
  return std::rotr(word, kFirstRotation) ^ std::rotr(word, kSecondRotation) ^
         std::rotr(word, kThirdRotation);
}
}  // namespace

bool CanAppendSha256Bytes(std::uint64_t current_bytes,
                          std::uint64_t added_bytes) noexcept {
  return current_bytes <= kSha256MaxInputBytes &&
         added_bytes <= kSha256MaxInputBytes - current_bytes;
}
Sha256::Sha256() noexcept : state_(kInitialState) {}
bool Sha256::Update(std::span<const std::byte> bytes) noexcept {
  static_assert(std::numeric_limits<std::size_t>::max() <=
                std::numeric_limits<std::uint64_t>::max());
  const auto added = static_cast<std::uint64_t>(bytes.size());
  if (!CanAppendSha256Bytes(total_bytes_, added)) return false;
  total_bytes_ += added;
  while (!bytes.empty()) {
    if (pending_bytes_ == 0 && bytes.size() >= kSha256BlockBytes) {
      Compress(bytes.first<kSha256BlockBytes>());
      bytes = bytes.subspan(kSha256BlockBytes);
      continue;
    }
    const auto take =
        std::min(kSha256BlockBytes - pending_bytes_, bytes.size());
    std::copy_n(bytes.begin(), take,
                pending_.begin() + static_cast<std::ptrdiff_t>(pending_bytes_));
    pending_bytes_ += take;
    bytes = bytes.subspan(take);
    if (pending_bytes_ == kSha256BlockBytes) {
      Compress(pending_);
      pending_bytes_ = 0;
    }
  }
  return true;
}
void Sha256::Compress(
    std::span<const std::byte, kSha256BlockBytes> block) noexcept {
  constexpr std::size_t kPreviousSigma1 = 2;
  constexpr std::size_t kPreviousWord = 7;
  constexpr std::size_t kPreviousSigma0 = 15;
  constexpr std::size_t kPreviousBlock = 16;
  std::array<std::uint32_t, kRounds> schedule{};
  for (std::size_t word = 0; word < kBlockWords; ++word)
    for (std::size_t byte = 0; byte < kWordBytes; ++byte)
      schedule[word] =
          (schedule[word] << kSha256BitsPerByte) |
          std::to_integer<std::uint32_t>(block[word * kWordBytes + byte]);
  for (std::size_t round = kBlockWords; round < kRounds; ++round)
    schedule[round] = ScheduleSigma1(schedule[round - kPreviousSigma1]) +
                      schedule[round - kPreviousWord] +
                      ScheduleSigma0(schedule[round - kPreviousSigma0]) +
                      schedule[round - kPreviousBlock];
  auto [a, b, c, d, e, f, g, h] = state_;
  for (std::size_t round = 0; round < kRounds; ++round) {
    const auto choose = (e & f) ^ (~e & g);
    const auto majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t first =
        h + RoundSigma1(e) + choose + kRoundConstants[round] + schedule[round];
    const std::uint32_t second = RoundSigma0(a) + majority;
    h = g;
    g = f;
    f = e;
    e = d + first;
    d = c;
    c = b;
    b = a;
    a = first + second;
  }
  const std::array<std::uint32_t, kSha256StateWords> working{a, b, c, d,
                                                             e, f, g, h};
  for (std::size_t word = 0; word < state_.size(); ++word)
    state_[word] += working[word];
}
Sha256Digest Sha256::Digest() const noexcept {
  auto copy = *this;
  copy.pending_[copy.pending_bytes_++] = kPaddingStart;
  if (copy.pending_bytes_ > kLengthOffset) {
    std::fill(copy.pending_.begin() +
                  static_cast<std::ptrdiff_t>(copy.pending_bytes_),
              copy.pending_.end(), std::byte{0});
    copy.Compress(copy.pending_);
    copy.pending_bytes_ = 0;
  }
  std::fill(
      copy.pending_.begin() + static_cast<std::ptrdiff_t>(copy.pending_bytes_),
      copy.pending_.begin() + kLengthOffset, std::byte{0});
  const auto bits = copy.total_bytes_ * kSha256BitsPerByte;
  for (std::size_t byte = 0; byte < kLengthBytes; ++byte)
    copy.pending_[kLengthOffset + byte] = static_cast<std::byte>(
        (bits >> ((kLengthBytes - 1 - byte) * kSha256BitsPerByte)) & kByteMask);
  copy.Compress(copy.pending_);
  Sha256Digest digest{};
  for (std::size_t word = 0; word < copy.state_.size(); ++word)
    for (std::size_t byte = 0; byte < kWordBytes; ++byte)
      digest[word * kWordBytes + byte] = static_cast<std::byte>(
          (copy.state_[word] >>
           ((kWordBytes - 1 - byte) * kSha256BitsPerByte)) &
          kByteMask);
  return digest;
}
std::uint64_t Sha256::BytesProcessed() const noexcept { return total_bytes_; }
Sha256Hex LowercaseSha256Hex(const Sha256Digest& digest) noexcept {
  constexpr std::string_view kHexDigits = "0123456789abcdef";
  constexpr std::size_t kCharactersPerByte = 2;
  constexpr unsigned kNibbleBits = 4;
  constexpr unsigned kNibbleMask = 0xf;
  Sha256Hex result{};
  for (std::size_t byte = 0; byte < digest.size(); ++byte) {
    const auto value = std::to_integer<unsigned>(digest[byte]);
    result[byte * kCharactersPerByte] = kHexDigits[value >> kNibbleBits];
    result[byte * kCharactersPerByte + 1] = kHexDigits[value & kNibbleMask];
  }
  return result;
}
}  // namespace meshvale::repair::gltf_file_detail
