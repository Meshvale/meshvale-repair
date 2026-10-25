// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_PLY_WORKFLOW_OBSERVATIONS_H_
#define MESHVALE_REPAIR_TESTS_PLY_WORKFLOW_OBSERVATIONS_H_

#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/ply_workflow.h>

#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>

// Test-only public-getter observations after synchronous native return.
// No expected data, report parsing, delegate substitution or private state
// access.
namespace meshvale::repair::test_ply_observations {
namespace detail {
namespace geo = meshvale::geometry;
namespace ic = meshvale::interchange;
namespace fields {
inline constexpr std::string_view kStatus = "status";
inline constexpr std::string_view kStage = "stage";
inline constexpr std::string_view kExecutionOutcome = "execution_outcome";
inline constexpr std::string_view kCandidateOutcome = "candidate_outcome";
inline constexpr std::string_view kProfileOutcome = "profile_outcome";
inline constexpr std::string_view kPublicationOutcome = "publication_outcome";
inline constexpr std::string_view kExitCode = "exit_code";
inline constexpr std::string_view kRepairCompleted = "repair_completed";
inline constexpr std::string_view kMemoryOutputVerified =
    "memory_output_verified";
inline constexpr std::string_view kInputSha256 = "input_sha256";
inline constexpr std::string_view kFaceMap = "face_map";
inline constexpr std::string_view kCornerMap = "corner_map";
inline constexpr std::string_view kSourceFaces = "source_faces";
inline constexpr std::string_view kSourceCorners = "source_corners";
inline constexpr std::string_view kSourceMesh = "source_mesh";
inline constexpr std::string_view kSourceSchema = "source_schema";
inline constexpr std::string_view kCandidateMesh = "candidate_mesh";
inline constexpr std::string_view kReloadMesh = "reload_mesh";
inline constexpr std::string_view kPublishedMesh = "published_mesh";
inline constexpr std::string_view kReloadSchema = "reload_schema";
inline constexpr std::string_view kPublishedSchema = "published_schema";
inline constexpr std::string_view kExecution = "execution";
inline constexpr std::string_view kDiagnostic = "diagnostic";
inline constexpr std::string_view kPublisher = "publisher";
inline constexpr std::string_view kSourceEncoding = "source_encoding";
inline constexpr std::string_view kReloadEncoding = "reload_encoding";
inline constexpr std::string_view kPublishedEncoding = "published_encoding";
inline constexpr std::string_view kCounts = "counts";
inline constexpr std::string_view kPositionsF64Hex = "positions_f64_hex";
inline constexpr std::string_view kFaceOffsets = "face_offsets";
inline constexpr std::string_view kCornerVertices = "corner_vertices";
inline constexpr std::string_view kAttributes = "attributes";
inline constexpr std::string_view kVertices = "vertices";
inline constexpr std::string_view kFaces = "faces";
inline constexpr std::string_view kCorners = "corners";
inline constexpr std::string_view kDomain = "domain";
inline constexpr std::string_view kName = "name";
inline constexpr std::string_view kComponents = "components";
inline constexpr std::string_view kSemantic = "semantic";
inline constexpr std::string_view kSetIndex = "set_index";
inline constexpr std::string_view kPresent = "present";
inline constexpr std::string_view kMetadata = "metadata";
inline constexpr std::string_view kCanonicalType = "canonical_type";
inline constexpr std::string_view kDataKind = "data_kind";
inline constexpr std::string_view kValues = "values";
inline constexpr std::string_view kOffsets = "offsets";
inline constexpr std::string_view kKey = "key";
inline constexpr std::string_view kValue = "value";
inline constexpr std::string_view kKind = "kind";
inline constexpr std::string_view kOriginalCount = "original_count";
inline constexpr std::string_view kProperties = "properties";
inline constexpr std::string_view kValueType = "value_type";
inline constexpr std::string_view kCountType = "count_type";
inline constexpr std::string_view kBinding = "binding";
inline constexpr std::string_view kAttribute = "attribute";
inline constexpr std::string_view kCode = "code";
inline constexpr std::string_view kTarget = "target";
inline constexpr std::string_view kElement = "element";
inline constexpr std::string_view kGeometryStatus = "geometry_status";
inline constexpr std::string_view kGeometryDiagnostic = "geometry_diagnostic";
inline constexpr std::string_view kStorageIssue = "storage_issue";
inline constexpr std::string_view kByteOffset = "byte_offset";
inline constexpr std::string_view kProperty = "property";
inline constexpr std::string_view kRow = "row";
inline constexpr std::string_view kItem = "item";
inline constexpr std::string_view kFileIndex = "file_index";
inline constexpr std::string_view kSystemError = "system_error";
inline constexpr std::string_view kPlyStatus = "ply_status";
inline constexpr std::string_view kPlyDiagnostic = "ply_diagnostic";
inline constexpr std::string_view kOutcome = "outcome";
inline constexpr std::string_view kCleanup = "cleanup";
inline constexpr std::string_view kCleanupError = "cleanup_error";
inline constexpr std::string_view kCategory = "category";
inline constexpr std::string_view kPlannedJobs = "planned_jobs";
inline constexpr std::string_view kLaunchedWorkers = "launched_workers";
inline constexpr std::string_view kJoinedWorkers = "joined_workers";
inline constexpr std::string_view kCallerJobs = "caller_jobs";
inline constexpr std::string_view kCodec = "codec";
inline constexpr std::string_view kMaterialization = "materialization";
inline constexpr std::string_view kInspection = "inspection";
inline constexpr std::string_view kRepair = "repair";
inline constexpr std::string_view kSourceInspection = "source_inspection";
inline constexpr std::string_view kEligibility = "eligibility";
inline constexpr std::string_view kComparison = "comparison";
inline constexpr std::string_view kSelection = "selection";
inline constexpr std::string_view kCorrespondence = "correspondence";
inline constexpr std::string_view kCandidateInspection = "candidate_inspection";
inline constexpr std::string_view kVerification = "verification";
inline constexpr std::string_view kDirectedVerification =
    "directed_verification";
inline constexpr std::string_view kWriting = "writing";
inline constexpr std::string_view kReload = "reload";
inline constexpr std::string_view kSourceProtection = "source_protection";
inline constexpr std::string_view kRepairCorrespondence =
    "repair_correspondence";
inline constexpr std::string_view kCanonicalPreservation =
    "canonical_preservation";
inline constexpr std::string_view kTypedProperties = "typed_properties";
inline constexpr std::string_view kWireSchema = "wire_schema";
inline constexpr std::string_view kReloadVerification = "reload_verification";
inline constexpr std::string_view kSourceVerification = "source_verification";
inline constexpr std::string_view kStagedCandidate = "staged_candidate";
inline constexpr std::string_view kHashing = "hashing";
inline constexpr std::string_view kReportCopy = "report_copy";
inline constexpr std::string_view kPublication = "publication";
inline constexpr std::string_view kDelegated = "delegated";
inline constexpr std::string_view kCapture = "capture";
inline constexpr std::string_view kCopying = "copying";
inline constexpr std::string_view kByteVerification = "byte_verification";
inline constexpr std::string_view kMeshVerification = "mesh_verification";
inline constexpr std::string_view kExpectedInspection = "expected_inspection";
inline constexpr std::string_view kPly = "ply";
}  // namespace fields
namespace words {
inline constexpr std::string_view kAscii = "ascii";
inline constexpr std::string_view kBinaryLittleEndian = "binary_little_endian";
inline constexpr std::string_view kBinaryBigEndian = "binary_big_endian";
inline constexpr std::string_view kVertex = "vertex";
inline constexpr std::string_view kFace = "face";
inline constexpr std::string_view kCorner = "corner";
inline constexpr std::string_view kScalar = "scalar";
inline constexpr std::string_view kList = "list";
inline constexpr std::string_view kChar = "char";
inline constexpr std::string_view kUchar = "uchar";
inline constexpr std::string_view kShort = "short";
inline constexpr std::string_view kUshort = "ushort";
inline constexpr std::string_view kInt = "int";
inline constexpr std::string_view kUint = "uint";
inline constexpr std::string_view kFloat = "float";
inline constexpr std::string_view kDouble = "double";
inline constexpr std::string_view kX = "x";
inline constexpr std::string_view kY = "y";
inline constexpr std::string_view kZ = "z";
inline constexpr std::string_view kAttribute = "attribute";
inline constexpr std::string_view kAccepted = "accepted";
inline constexpr std::string_view kUnchanged = "unchanged";
inline constexpr std::string_view kRejected = "rejected";
inline constexpr std::string_view kCompleted = "completed";
inline constexpr std::string_view kFailed = "failed";
inline constexpr std::string_view kCancelled = "cancelled";
inline constexpr std::string_view kPassed = "passed";
inline constexpr std::string_view kIncomplete = "incomplete";
inline constexpr std::string_view kConfiguration = "configuration";
inline constexpr std::string_view kRepair = "repair";
inline constexpr std::string_view kWrite = "write";
inline constexpr std::string_view kReload = "reload";
inline constexpr std::string_view kVerify = "verify";
inline constexpr std::string_view kHash = "hash";
inline constexpr std::string_view kReport = "report";
inline constexpr std::string_view kPublication = "publication";
inline constexpr std::string_view kComplete = "complete";
inline constexpr std::string_view kFloat32 = "float32";
inline constexpr std::string_view kFloat64 = "float64";
inline constexpr std::string_view kInt32 = "int32";
inline constexpr std::string_view kUint8 = "uint8";
inline constexpr std::string_view kUint16 = "uint16";
inline constexpr std::string_view kUint32 = "uint32";
inline constexpr std::string_view kUint64 = "uint64";
inline constexpr std::string_view kBits32 = "bits32";
inline constexpr std::string_view kBits64 = "bits64";
inline constexpr std::string_view kInteger = "integer";
inline constexpr std::string_view kVertexIndices = "vertex-indices";
inline constexpr std::string_view kSourceProtection = "source-protection";
inline constexpr std::string_view kNotRequested = "not-requested";
inline constexpr std::string_view kNotAttempted = "not-attempted";
inline constexpr std::string_view kPublished = "published";
inline constexpr std::string_view kPly = "ply";
}  // namespace words
inline constexpr std::string_view kJsonNull = "null";
inline constexpr std::string_view kJsonTrue = "true";
inline constexpr std::string_view kJsonFalse = "false";
inline constexpr std::string_view kHexDigits = "0123456789abcdef";
inline constexpr std::size_t kMaxObservationBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kPositionComponents = 3;
inline constexpr std::size_t kFloatHexDigits = 8;
inline constexpr std::size_t kDoubleHexDigits = 16;
inline constexpr std::size_t kUnicodeHexDigits = 4;
inline constexpr unsigned kNibbleBits = 4;
inline constexpr std::uint64_t kNibbleMask = 0xf;
inline constexpr std::uint32_t kAsciiEnd = 0x80;
inline constexpr std::uint32_t kControlEnd = 0x20;
inline constexpr std::uint32_t kUnicodeEnd = 0x10ffff;
inline constexpr std::uint32_t kSupplementaryStart = 0x10000;
inline constexpr std::uint32_t kHighSurrogateStart = 0xd800;
inline constexpr std::uint32_t kLowSurrogateStart = 0xdc00;
inline constexpr std::uint32_t kSurrogateEnd = 0xdfff;
inline constexpr unsigned kSurrogateBits = 10;
inline constexpr std::uint32_t kSurrogateMask = 0x3ff;
inline constexpr unsigned char kUtf8TailMask = 0xc0;
inline constexpr unsigned char kUtf8TailTag = 0x80;
inline constexpr unsigned char kUtf8TailPayload = 0x3f;
inline constexpr unsigned kUtf8TailBits = 6;

inline void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
class Json {
 public:
  explicit Json(const std::filesystem::path& path) {
    Require(!std::filesystem::exists(path), "observation path must be new");
    stream_.open(path, std::ios::binary | std::ios::out);
    Require(stream_.good(), "observation file open failed");
  }
  void Raw(std::string_view text) {
    Require(text.size() <= kMaxObservationBytes - size_,
            "observation byte bound exceeded");
    Require(text.size() <= static_cast<std::uint64_t>(
                               std::numeric_limits<std::streamsize>::max()),
            "observation stream extent exceeded");
    stream_.write(text.data(), static_cast<std::streamsize>(text.size()));
    Require(stream_.good(), "observation write failed");
    size_ += text.size();
  }
  void Null() { Raw(kJsonNull); }
  void Boolean(bool value) { Raw(value ? kJsonTrue : kJsonFalse); }
  template <class T>
  void Integer(T value) {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    constexpr std::size_t kIntegerChars =
        std::numeric_limits<std::uint64_t>::digits10 + 2;
    std::array<char, kIntegerChars> text;
    const auto result = [&] {
      if constexpr (std::is_signed_v<T>)
        return std::to_chars(text.data(), text.data() + text.size(),
                             static_cast<std::int64_t>(value));
      else
        return std::to_chars(text.data(), text.data() + text.size(),
                             static_cast<std::uint64_t>(value));
    }();
    Require(result.ec == std::errc{}, "observation integer conversion failed");
    Raw({text.data(), static_cast<std::size_t>(result.ptr - text.data())});
  }
  void Hex(std::uint64_t value, std::size_t digits) {
    Require(digits <= kDoubleHexDigits, "observation hex width invalid");
    std::array<char, kDoubleHexDigits> text{};
    for (std::size_t i = 0; i < digits; ++i) {
      text[digits - i - 1] = kHexDigits[value & kNibbleMask];
      value >>= kNibbleBits;
    }
    Raw({text.data(), digits});
  }
  void BitWord(std::uint64_t value, std::size_t digits) {
    Raw("\"");
    Hex(value, digits);
    Raw("\"");
  }
  void String(std::string_view value) {
    Raw("\"");
    for (std::size_t i = 0; i < value.size();) {
      auto scalar =
          static_cast<std::uint32_t>(static_cast<unsigned char>(value[i++]));
      if (scalar < kAsciiEnd) {
        if (scalar == '"')
          Raw("\\\"");
        else if (scalar == '\\')
          Raw("\\\\");
        else if (scalar < kControlEnd)
          UnicodeWord(scalar);
        else {
          const char byte = static_cast<char>(scalar);
          Raw({&byte, 1});
        }
        continue;
      }
      struct Lead {
        std::uint32_t first, last, payload, minimum;
        std::size_t tails;
      };
      constexpr std::array kLeads{Lead{0xc2, 0xdf, 0x1f, 0x80, 1},
                                  Lead{0xe0, 0xef, 0x0f, 0x800, 2},
                                  Lead{0xf0, 0xf4, 0x07, 0x10000, 3}};
      const Lead* selected = nullptr;
      for (const auto& lead : kLeads)
        if (scalar >= lead.first && scalar <= lead.last) selected = &lead;
      Require(selected != nullptr, "observation string is not UTF-8");
      Require(selected->tails <= value.size() - i,
              "observation UTF-8 sequence is truncated");
      scalar &= selected->payload;
      for (std::size_t j = 0; j < selected->tails; ++j) {
        const auto byte = static_cast<unsigned char>(value[i++]);
        Require((byte & kUtf8TailMask) == kUtf8TailTag,
                "observation UTF-8 continuation invalid");
        scalar = (scalar << kUtf8TailBits) | (byte & kUtf8TailPayload);
      }
      Require(scalar >= selected->minimum && scalar <= kUnicodeEnd &&
                  !(scalar >= kHighSurrogateStart && scalar <= kSurrogateEnd),
              "observation UTF-8 scalar invalid");
      if (scalar < kSupplementaryStart)
        UnicodeWord(scalar);
      else {
        scalar -= kSupplementaryStart;
        UnicodeWord(kHighSurrogateStart + (scalar >> kSurrogateBits));
        UnicodeWord(kLowSurrogateStart + (scalar & kSurrogateMask));
      }
    }
    Raw("\"");
  }
  void Finish() {
    Raw("\n");
    stream_.flush();
    Require(stream_.good(), "observation flush failed");
    stream_.close();
    Require(!stream_.fail(), "observation close failed");
  }

 private:
  void UnicodeWord(std::uint32_t value) {
    Raw("\\u");
    Hex(value, kUnicodeHexDigits);
  }
  std::ofstream stream_;
  std::size_t size_ = 0;
};
class Object {
 public:
  explicit Object(Json& out) : out_(out) { out_.Raw("{"); }
  template <class F>
  void Field(std::string_view key, F write) {
    if (!first_) out_.Raw(",");
    first_ = false;
    out_.String(key);
    out_.Raw(":");
    write();
  }
  template <class T>
  void Number(std::string_view key, T value) {
    Field(key, [&] { out_.Integer(value); });
  }
  void Text(std::string_view key, std::string_view value) {
    Field(key, [&] { out_.String(value); });
  }
  void Boolean(std::string_view key, bool value) {
    Field(key, [&] { out_.Boolean(value); });
  }
  void End() { out_.Raw("}"); }

 private:
  Json& out_;
  bool first_ = true;
};
template <class Range, class F>
inline void Array(Json& out, const Range& values, F write) {
  out.Raw("[");
  bool first = true;
  for (const auto& value : values) {
    if (!first) out.Raw(",");
    first = false;
    write(value);
  }
  out.Raw("]");
}
template <class T, class F>
inline void Optional(Json& out, const std::optional<T>& value, F write) {
  if (value)
    write(*value);
  else
    out.Null();
}
template <class Range>
inline void Integers(Json& out, const Range& values) {
  Array(out, values, [&](auto value) { out.Integer(value); });
}
inline std::string_view Encoding(ic::PlyEncoding value) {
  switch (value) {
    case ic::PlyEncoding::kAscii:
      return words::kAscii;
    case ic::PlyEncoding::kBinaryLittleEndian:
      return words::kBinaryLittleEndian;
    case ic::PlyEncoding::kBinaryBigEndian:
      return words::kBinaryBigEndian;
    case ic::PlyEncoding::kInvalid:
      break;
  }
  throw std::runtime_error("observation encoding invalid");
}
inline std::string_view Domain(geo::AttributeDomain value) {
  switch (value) {
    case geo::AttributeDomain::vertex:
      return words::kVertex;
    case geo::AttributeDomain::face:
      return words::kFace;
    case geo::AttributeDomain::corner:
      return words::kCorner;
  }
  throw std::runtime_error("observation domain invalid");
}
inline std::string_view ScalarType(ic::PlyScalarType value) {
  switch (value) {
    case ic::PlyScalarType::kChar:
      return words::kChar;
    case ic::PlyScalarType::kUchar:
      return words::kUchar;
    case ic::PlyScalarType::kShort:
      return words::kShort;
    case ic::PlyScalarType::kUshort:
      return words::kUshort;
    case ic::PlyScalarType::kInt:
      return words::kInt;
    case ic::PlyScalarType::kUint:
      return words::kUint;
    case ic::PlyScalarType::kFloat:
      return words::kFloat;
    case ic::PlyScalarType::kDouble:
      return words::kDouble;
    case ic::PlyScalarType::kInvalid:
      break;
  }
  throw std::runtime_error("observation scalar type invalid");
}
inline std::string_view Binding(ic::PlyBinding value) {
  switch (value) {
    case ic::PlyBinding::kX:
      return words::kX;
    case ic::PlyBinding::kY:
      return words::kY;
    case ic::PlyBinding::kZ:
      return words::kZ;
    case ic::PlyBinding::kVertexIndices:
      return words::kVertexIndices;
    case ic::PlyBinding::kAttribute:
      return words::kAttribute;
  }
  throw std::runtime_error("observation binding invalid");
}
inline std::string_view Stage(PlyWorkflowStage value) {
  switch (value) {
    case PlyWorkflowStage::kConfiguration:
      return words::kConfiguration;
    case PlyWorkflowStage::kSourceProtection:
      return words::kSourceProtection;
    case PlyWorkflowStage::kRepair:
      return words::kRepair;
    case PlyWorkflowStage::kWrite:
      return words::kWrite;
    case PlyWorkflowStage::kReload:
      return words::kReload;
    case PlyWorkflowStage::kVerify:
      return words::kVerify;
    case PlyWorkflowStage::kHash:
      return words::kHash;
    case PlyWorkflowStage::kReport:
      return words::kReport;
    case PlyWorkflowStage::kPublication:
      return words::kPublication;
    case PlyWorkflowStage::kComplete:
      return words::kComplete;
  }
  throw std::runtime_error("observation workflow stage invalid");
}
inline std::string_view Candidate(CandidateOutcome value) {
  switch (value) {
    case CandidateOutcome::accepted:
      return words::kAccepted;
    case CandidateOutcome::unchanged:
      return words::kUnchanged;
    case CandidateOutcome::rejected:
      return words::kRejected;
  }
  throw std::runtime_error("observation candidate outcome invalid");
}
inline std::string_view Execution(PlyWorkflowExecutionOutcome value) {
  switch (value) {
    case PlyWorkflowExecutionOutcome::kCompleted:
      return words::kCompleted;
    case PlyWorkflowExecutionOutcome::kFailed:
      return words::kFailed;
    case PlyWorkflowExecutionOutcome::kCanceled:
      return words::kCancelled;
  }
  throw std::runtime_error("observation execution outcome invalid");
}
inline std::string_view Profile(PlyWorkflowProfileOutcome value) {
  switch (value) {
    case PlyWorkflowProfileOutcome::kPassed:
      return words::kPassed;
    case PlyWorkflowProfileOutcome::kFailed:
      return words::kFailed;
    case PlyWorkflowProfileOutcome::kIncomplete:
      return words::kIncomplete;
  }
  throw std::runtime_error("observation profile outcome invalid");
}
inline std::string_view Publication(PlyWorkflowPublicationOutcome value) {
  switch (value) {
    case PlyWorkflowPublicationOutcome::kNotRequested:
      return words::kNotRequested;
    case PlyWorkflowPublicationOutcome::kNotAttempted:
      return words::kNotAttempted;
    case PlyWorkflowPublicationOutcome::kFailed:
      return words::kFailed;
    case PlyWorkflowPublicationOutcome::kCanceled:
      return words::kCancelled;
    case PlyWorkflowPublicationOutcome::kPublished:
      return words::kPublished;
  }
  throw std::runtime_error("observation publication outcome invalid");
}
inline void Stats(Json& out, const geo::MaterializationExecutionStats& value) {
  Object object(out);
  object.Number(fields::kPlannedJobs, value.planned_jobs);
  object.Number(fields::kLaunchedWorkers, value.launched_workers);
  object.Number(fields::kJoinedWorkers, value.joined_workers);
  object.Number(fields::kCallerJobs, value.caller_jobs);
  object.End();
}
inline void CodecStats(Json& out, const ic::PlyExecution& value) {
  Object object(out);
  object.Field(fields::kCodec, [&] { Stats(out, value.codec); });
  object.Field(fields::kMaterialization,
               [&] { Stats(out, value.materialization); });
  object.Field(fields::kInspection, [&] { Stats(out, value.inspection); });
  object.End();
}
inline void FileStats(Json& out, const ic::PlyFileExecution& value) {
  Object object(out);
  object.Field(fields::kCapture, [&] { Stats(out, value.capture); });
  object.Field(fields::kCopying, [&] { Stats(out, value.copying); });
  object.Field(fields::kByteVerification,
               [&] { Stats(out, value.byte_verification); });
  object.Field(fields::kMeshVerification,
               [&] { Stats(out, value.mesh_verification); });
  object.Field(fields::kExpectedInspection,
               [&] { Stats(out, value.expected_inspection); });
  object.Field(fields::kPly, [&] { CodecStats(out, value.ply); });
  object.End();
}
inline void Phases(Json& out, const PlyDuplicateFileWorkflowResult& result) {
  const auto& value = result.execution;
  Object object(out);
  object.Field(fields::kRepair, [&] {
    Object repair(out);
    repair.Field(fields::kSourceInspection,
                 [&] { Stats(out, value.repair.source_inspection); });
    repair.Field(fields::kEligibility,
                 [&] { Stats(out, value.repair.eligibility); });
    repair.Field(fields::kComparison,
                 [&] { Stats(out, value.repair.comparison); });
    repair.Field(fields::kSelection,
                 [&] { Stats(out, value.repair.selection); });
    repair.Field(fields::kCorrespondence,
                 [&] { Stats(out, value.repair.correspondence); });
    repair.Field(fields::kCandidateInspection,
                 [&] { Stats(out, value.repair.candidate_inspection); });
    repair.Field(fields::kVerification,
                 [&] { Stats(out, value.repair.verification); });
    repair.Field(fields::kDirectedVerification,
                 [&] { Stats(out, value.repair.directed_verification); });
    repair.End();
  });
  object.Field(fields::kWriting, [&] { CodecStats(out, value.writing); });
  object.Field(fields::kReload, [&] { CodecStats(out, value.reload); });
  object.Field(fields::kSourceProtection,
               [&] { Stats(out, value.source_protection); });
  object.Field(fields::kRepairCorrespondence,
               [&] { Stats(out, value.repair_correspondence); });
  object.Field(fields::kCanonicalPreservation,
               [&] { Stats(out, value.canonical_preservation); });
  object.Field(fields::kTypedProperties,
               [&] { Stats(out, value.typed_properties); });
  object.Field(fields::kWireSchema, [&] { Stats(out, value.wire_schema); });
  object.Field(fields::kReloadVerification,
               [&] { Stats(out, value.reload_verification); });
  object.Field(fields::kSourceVerification,
               [&] { Stats(out, value.source_verification); });
  object.Field(fields::kStagedCandidate,
               [&] { Stats(out, value.staged_candidate); });
  object.Field(fields::kHashing, [&] { Stats(out, value.hashing); });
  object.Field(fields::kReportCopy, [&] { Stats(out, value.report_copy); });
  object.Field(fields::kPublication, [&] {
    if (result.publication)
      FileStats(out, result.publication->execution);
    else
      out.Null();
  });
  object.End();
}
template <class T>
inline void Diagnostic(Json& out, const T& value) {
  Object object(out);
  object.Number(fields::kCode, value.code);
  if constexpr (requires { value.stage; })
    object.Number(fields::kStage, value.stage);
  if constexpr (requires { value.target; })
    object.Field(fields::kTarget, [&] {
      Optional(out, value.target, [&](auto v) { out.Integer(v); });
    });
  if constexpr (requires { value.attribute; })
    object.Field(fields::kAttribute, [&] {
      Optional(out, value.attribute, [&](auto v) { out.Integer(v); });
    });
  if constexpr (requires { value.element; })
    object.Field(fields::kElement, [&] {
      Optional(out, value.element, [&](auto v) { out.Integer(v); });
    });
  if constexpr (requires { value.byte_offset; })
    object.Field(fields::kByteOffset, [&] {
      Optional(out, value.byte_offset, [&](auto v) { out.Integer(v); });
    });
  if constexpr (requires { value.property; }) {
    object.Field(fields::kProperty, [&] {
      Optional(out, value.property, [&](auto v) { out.Integer(v); });
    });
    object.Field(fields::kRow, [&] {
      Optional(out, value.row, [&](auto v) { out.Integer(v); });
    });
    object.Field(fields::kItem, [&] {
      Optional(out, value.item, [&](auto v) { out.Integer(v); });
    });
  }
  if constexpr (requires { value.geometry_status; }) {
    object.Field(fields::kGeometryStatus, [&] {
      Optional(out, value.geometry_status, [&](auto v) { out.Integer(v); });
    });
    object.Field(fields::kGeometryDiagnostic, [&] {
      Optional(out, value.geometry_diagnostic,
               [&](const auto& v) { Diagnostic(out, v); });
    });
  }
  if constexpr (requires { value.storage_issue; })
    object.Field(fields::kStorageIssue, [&] {
      Optional(out, value.storage_issue,
               [&](const auto& v) { Diagnostic(out, v); });
    });
  object.End();
}
inline void WorkflowDiagnostic(Json& out, const PlyWorkflowDiagnostic& value) {
  Object object(out);
  object.Number(fields::kStage, value.stage);
  object.Number(fields::kCode, value.code);
  object.Field(fields::kElement, [&] {
    Optional(out, value.element, [&](auto v) { out.Integer(v); });
  });
  object.Field(fields::kDelegated, [&] {
    if (std::holds_alternative<std::monostate>(value.delegated_status)) {
      Require(std::holds_alternative<std::monostate>(value.delegated),
              "observation delegate mismatch");
      out.Null();
    } else if (const auto* repair_status =
                   std::get_if<MaterializedDuplicateStatus>(
                       &value.delegated_status)) {
      const auto& diagnostic =
          std::get<MaterializedDuplicateDiagnostic>(value.delegated);
      Object delegated(out);
      delegated.Text(fields::kKind, words::kRepair);
      delegated.Number(fields::kStatus, *repair_status);
      delegated.Field(fields::kDiagnostic,
                      [&] { Diagnostic(out, diagnostic); });
      delegated.End();
    } else {
      const auto& ply_status = std::get<ic::PlyStatus>(value.delegated_status);
      const auto& diagnostic = std::get<ic::PlyDiagnostic>(value.delegated);
      Object delegated(out);
      delegated.Text(fields::kKind, words::kPly);
      delegated.Number(fields::kStatus, ply_status);
      delegated.Field(fields::kDiagnostic,
                      [&] { Diagnostic(out, diagnostic); });
      delegated.End();
    }
  });
  object.End();
}
inline void SystemError(Json& out, const std::error_code& value) {
  Object object(out);
  object.Text(fields::kCategory, value.category().name());
  object.Number(fields::kValue, value.value());
  object.End();
}
inline void FileDiagnostic(Json& out, const ic::PlyFileDiagnostic& value) {
  Object object(out);
  object.Number(fields::kStatus, value.status);
  object.Number(fields::kStage, value.stage);
  object.Number(fields::kCode, value.code);
  object.Field(fields::kFileIndex, [&] {
    Optional(out, value.file_index, [&](auto v) { out.Integer(v); });
  });
  object.Field(fields::kByteOffset, [&] {
    Optional(out, value.byte_offset, [&](auto v) { out.Integer(v); });
  });
  object.Field(fields::kSystemError, [&] {
    Optional(out, value.system_error,
             [&](const auto& v) { SystemError(out, v); });
  });
  object.Field(fields::kPlyStatus, [&] {
    Optional(out, value.ply_status, [&](auto v) { out.Integer(v); });
  });
  object.Field(fields::kPlyDiagnostic, [&] {
    Optional(out, value.ply_diagnostic,
             [&](const auto& v) { Diagnostic(out, v); });
  });
  object.Field(fields::kGeometryStatus, [&] {
    Optional(out, value.geometry_status, [&](auto v) { out.Integer(v); });
  });
  object.Field(fields::kGeometryDiagnostic, [&] {
    Optional(out, value.geometry_diagnostic,
             [&](const auto& v) { Diagnostic(out, v); });
  });
  object.End();
}
inline void Publisher(Json& out, const ic::PlyFilePublicationResult& value) {
  Object object(out);
  object.Number(fields::kOutcome, value.outcome);
  object.Field(fields::kDiagnostic,
               [&] { FileDiagnostic(out, value.diagnostic); });
  object.Number(fields::kCleanup, value.cleanup);
  object.Field(fields::kCleanupError, [&] {
    Optional(out, value.cleanup_error,
             [&](const auto& v) { SystemError(out, v); });
  });
  object.End();
}
template <class T>
inline std::string_view CanonicalType() {
  if constexpr (std::is_same_v<T, float>)
    return words::kFloat32;
  else if constexpr (std::is_same_v<T, double>)
    return words::kFloat64;
  else if constexpr (std::is_same_v<T, std::int32_t>)
    return words::kInt32;
  else if constexpr (std::is_same_v<T, std::uint8_t>)
    return words::kUint8;
  else if constexpr (std::is_same_v<T, std::uint16_t>)
    return words::kUint16;
  else if constexpr (std::is_same_v<T, std::uint32_t>)
    return words::kUint32;
  else if constexpr (std::is_same_v<T, std::uint64_t>)
    return words::kUint64;
  else
    static_assert(std::is_same_v<T, void>, "unsupported observed scalar type");
}
inline void Attribute(Json& out, const geo::AttributeReadView& value) {
  Object object(out);
  object.Text(fields::kDomain, Domain(value.domain));
  object.Text(fields::kName, value.name);
  object.Number(fields::kComponents, value.components);
  object.Text(fields::kSemantic, value.semantic);
  object.Field(fields::kSetIndex, [&] {
    Optional(out, value.set_index, [&](auto v) { out.Integer(v); });
  });
  object.Field(fields::kPresent, [&] {
    Optional(out, value.present, [&](const auto& v) { Integers(out, v); });
  });
  object.Field(fields::kMetadata, [&] {
    Array(out, value.metadata, [&](const auto& v) {
      Object item(out);
      item.Text(fields::kKey, v.key);
      item.Text(fields::kValue, v.value);
      item.End();
    });
  });
  std::visit(
      [&](auto values) {
        using T = std::remove_const_t<typename decltype(values)::element_type>;
        object.Text(fields::kCanonicalType, CanonicalType<T>());
        object.Text(fields::kDataKind, std::is_same_v<T, float> ? words::kBits32
                                       : std::is_same_v<T, double>
                                           ? words::kBits64
                                           : words::kInteger);
        object.Field(fields::kValues, [&] {
          Array(out, values, [&](auto v) {
            if constexpr (std::is_same_v<T, float>)
              out.BitWord(std::bit_cast<std::uint32_t>(v), kFloatHexDigits);
            else if constexpr (std::is_same_v<T, double>)
              out.BitWord(std::bit_cast<std::uint64_t>(v), kDoubleHexDigits);
            else
              out.Integer(v);
          });
        });
      },
      value.values);
  object.Field(fields::kOffsets, [&] {
    Optional(out, value.offsets, [&](const auto& v) { Integers(out, v); });
  });
  object.End();
}
inline void Mesh(Json& out, const geo::MaterializedMesh& mesh) {
  const auto view = mesh.View();
  Require(view.positions.size() % kPositionComponents == 0 &&
              !view.face_offsets.empty(),
          "observed mesh extent invalid");
  Object object(out);
  object.Field(fields::kCounts, [&] {
    Object counts(out);
    counts.Number(fields::kVertices,
                  view.positions.size() / kPositionComponents);
    counts.Number(fields::kFaces, view.face_offsets.size() - 1);
    counts.Number(fields::kCorners, view.corner_vertices.size());
    counts.End();
  });
  object.Field(fields::kPositionsF64Hex, [&] {
    Array(out, view.positions, [&](double v) {
      out.BitWord(std::bit_cast<std::uint64_t>(v), kDoubleHexDigits);
    });
  });
  object.Field(fields::kFaceOffsets, [&] { Integers(out, view.face_offsets); });
  object.Field(fields::kCornerVertices,
               [&] { Integers(out, view.corner_vertices); });
  object.Field(fields::kAttributes, [&] {
    Array(out, view.attributes, [&](const auto& v) { Attribute(out, v); });
  });
  object.End();
}
inline void Schema(Json& out, const ic::PlyDocument& document) {
  Array(out, document.Schema(), [&](const auto& element) {
    Object object(out);
    object.Text(fields::kKind, element.kind == ic::PlyElementKind::kVertex
                                   ? words::kVertex
                                   : words::kFace);
    object.Number(fields::kOriginalCount, element.original_count);
    object.Field(fields::kProperties, [&] {
      Array(out, element.properties, [&](const auto& value) {
        Object property(out);
        property.Text(fields::kName, value.name);
        property.Text(fields::kKind, value.kind == ic::PlyPropertyKind::kScalar
                                         ? words::kScalar
                                         : words::kList);
        property.Text(fields::kValueType, ScalarType(value.value_type));
        property.Field(fields::kCountType, [&] {
          Optional(out, value.count_type,
                   [&](auto v) { out.String(ScalarType(v)); });
        });
        property.Text(fields::kBinding, Binding(value.binding));
        property.Field(fields::kAttribute, [&] {
          Optional(out, value.attribute, [&](auto v) { out.Integer(v); });
        });
        property.End();
      });
    });
    object.End();
  });
}
}  // namespace detail

// Fallible post-return fixture IO. The caller retains source/result through
// this call; it must not run inside a native commit-success noexcept callback.
// Failure preserves the already returned native outcome and may leave partial
// evidence.
inline void WriteNative(const std::filesystem::path& path,
                        const interchange::PlyFileAsset& source,
                        const PlyDuplicateFileWorkflowResult& result) {
  namespace d = detail;
  d::Json out(path);
  d::Object object(out);
  const auto* record = result.record ? &*result.record : nullptr;
  const auto* repair = record ? record->RepairFacts() : nullptr;
  const auto* reload =
      record && record->Reload() ? &*record->Reload() : nullptr;
  const auto* published =
      record && record->Published() ? &*record->Published() : nullptr;
  object.Number(d::fields::kStatus, result.status);
  object.Text(d::fields::kStage, d::Stage(result.outcomes.stage));
  object.Text(d::fields::kExecutionOutcome,
              d::Execution(result.outcomes.execution));
  object.Field(d::fields::kCandidateOutcome, [&] {
    d::Optional(out, result.outcomes.candidate,
                [&](auto v) { out.String(d::Candidate(v)); });
  });
  object.Text(d::fields::kProfileOutcome, d::Profile(result.outcomes.profile));
  object.Text(d::fields::kPublicationOutcome,
              d::Publication(result.outcomes.publication));
  object.Number(d::fields::kExitCode, result.outcomes.exit);
  object.Boolean(d::fields::kRepairCompleted, repair != nullptr);
  object.Boolean(d::fields::kMemoryOutputVerified,
                 record && record->Output() && reload);
  object.Field(d::fields::kInputSha256, [&] {
    if (record)
      d::Optional(out, record->InputSha256(), [&](auto v) { out.String(v); });
    else
      out.Null();
  });
  object.Field(d::fields::kFaceMap, [&] {
    if (repair)
      d::Integers(out, repair->FaceMap());
    else
      out.Null();
  });
  object.Field(d::fields::kCornerMap, [&] {
    if (repair)
      d::Integers(out, repair->CornerMap());
    else
      out.Null();
  });
  object.Field(d::fields::kSourceFaces, [&] {
    if (repair)
      d::Integers(out, repair->SourceFaces());
    else
      out.Null();
  });
  object.Field(d::fields::kSourceCorners, [&] {
    if (repair)
      d::Integers(out, repair->SourceCorners());
    else
      out.Null();
  });
  object.Field(d::fields::kSourceMesh,
               [&] { d::Mesh(out, source.Document().Mesh()); });
  object.Field(d::fields::kSourceSchema,
               [&] { d::Schema(out, source.Document()); });
  object.Field(d::fields::kCandidateMesh, [&] {
    if (repair)
      d::Mesh(out, repair->Candidate());
    else
      out.Null();
  });
  object.Field(d::fields::kReloadMesh, [&] {
    if (reload)
      d::Mesh(out, reload->Mesh());
    else
      out.Null();
  });
  object.Field(d::fields::kPublishedMesh, [&] {
    if (published)
      d::Mesh(out, published->Document().Mesh());
    else
      out.Null();
  });
  object.Field(d::fields::kReloadSchema, [&] {
    if (reload)
      d::Schema(out, *reload);
    else
      out.Null();
  });
  object.Field(d::fields::kPublishedSchema, [&] {
    if (published)
      d::Schema(out, published->Document());
    else
      out.Null();
  });
  object.Text(d::fields::kSourceEncoding,
              d::Encoding(source.Document().Encoding()));
  object.Field(d::fields::kReloadEncoding, [&] {
    if (reload)
      out.String(d::Encoding(reload->Encoding()));
    else
      out.Null();
  });
  object.Field(d::fields::kPublishedEncoding, [&] {
    if (published)
      out.String(d::Encoding(published->Document().Encoding()));
    else
      out.Null();
  });
  object.Field(d::fields::kExecution, [&] { d::Phases(out, result); });
  object.Field(d::fields::kDiagnostic,
               [&] { d::WorkflowDiagnostic(out, result.diagnostic); });
  object.Field(d::fields::kPublisher, [&] {
    if (result.publication)
      d::Publisher(out, *result.publication);
    else
      out.Null();
  });
  object.End();
  out.Finish();
}
}  // namespace meshvale::repair::test_ply_observations
#endif  // MESHVALE_REPAIR_TESTS_PLY_WORKFLOW_OBSERVATIONS_H_
