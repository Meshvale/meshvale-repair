// SPDX-License-Identifier: Apache-2.0
#include "off_bindings.h"

#include <Python.h>
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/off.h>
#include <meshvale/interchange/off_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/off_workflow.h>
#include <nanobind/stl/string_view.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "off_presets.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace meshvale::repair::python {
namespace {
namespace nb = nanobind;
namespace ic = interchange;
namespace geo = geometry;
constexpr std::size_t kPositionComponents = 3;
constexpr std::string_view kProhibitedName = "/\\:<>\"|?*";
constexpr std::uint32_t kControlEnd = 0x20, kDelete = 0x7f,
                        kLatinControlEnd = 0xa0;
constexpr std::array<std::string_view, 4> kDeviceNames{"con", "prn", "aux",
                                                       "nul"};
constexpr std::size_t kNumberedDeviceLength = 4, kDevicePrefixLength = 3;
constexpr char kFirstDeviceNumber = '1', kLastDeviceNumber = '9';
constexpr std::string_view kComDevicePrefix = "com";
constexpr std::string_view kLptDevicePrefix = "lpt";
template <class T>
struct OffObservation {
  T value;
};
template <class T>
nb::object Observe(const T& value);
template <class T>
nb::object Observe(const std::optional<T>& value);
template <class... T>
nb::object Observe(const std::variant<T...>& value);
nb::object Observe(const std::error_code& value);
template <class T>
nb::object Observe(const T& value) {
  if constexpr (std::is_same_v<T, std::monostate>)
    return nb::none();
  else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
    return nb::cast(value);
  else
    return nb::cast(OffObservation<T>{value});
}
template <class T>
nb::object Observe(const std::optional<T>& value) {
  return value ? Observe(*value) : nb::none();
}
template <class... T>
nb::object Observe(const std::variant<T...>& value) {
  return std::visit([](const auto& active) { return Observe(active); }, value);
}
nb::object Observe(const std::error_code& value) {
  return nb::cast(OffObservation<std::error_code>{value});
}

template <class T, class Member>
void Property(nb::class_<OffObservation<T>>& type, const char* name,
              Member T::* member) {
  type.def_prop_ro(name, [member](const OffObservation<T>& self) {
    return Observe(self.value.*member);
  });
}
class OffExecution {
 public:
  explicit OffExecution(std::string_view name)
      : name_(name),
        preset_(MakeOffPreset(name)),
        context_(preset_.execution) {}
  const std::string& Name() const noexcept { return name_; }
  const OffPreset& Preset() const noexcept { return preset_; }
  const geo::ExecutionContext& Context() const noexcept { return context_; }

 private:
  std::string name_;
  OffPreset preset_;
  geo::ExecutionContext context_;
};
class OffCancellation {
 public:
  bool Cancel() noexcept { return source_.request_stop(); }
  bool Cancelled() const noexcept { return source_.stop_requested(); }
  std::stop_source Source() const noexcept { return source_; }

 private:
  std::stop_source source_;
};
enum class InvocationState { kPrepared, kRunning, kComplete, kFailed };
struct CapturedRequestedBytes {
  std::uint64_t files;
  ic::OffRequestedBytes off;
};
struct OffFileWorkflowResult {
  explicit OffFileWorkflowResult(const OffExecution& session,
                                 std::stop_source cancellation)
      : preset(session.Preset()),
        context(session.Context()),
        stop(std::move(cancellation)) {}
  OffPreset preset;
  geo::ExecutionContext context;
  std::stop_source stop;
  std::filesystem::path source;
  std::optional<std::filesystem::path> destination;
  std::string input_id;
  std::string report_path;
  std::vector<DuplicateTarget> targets;
  ic::OffTextPolicy text = ic::OffTextPolicy::kInvalid;
  ic::OffEdgeCountPolicy edge_count = ic::OffEdgeCountPolicy::kInvalid;
  ic::OffFileReadResult capture;
  OffDuplicateFileWorkflowResult workflow;
  InvocationState state = InvocationState::kPrepared;
  bool workflow_started = false;
  void RequireComplete() const {
    if (state != InvocationState::kComplete)
      throw nb::value_error("OFF invocation has not completed");
  }
  const MaterializedDuplicateRepair* Repair() const noexcept {
    return workflow_started && workflow.record ? workflow.record->RepairFacts()
                                               : nullptr;
  }
};
static_assert(std::is_nothrow_move_constructible_v<ic::OffFileReadResult>);
static_assert(std::is_nothrow_move_assignable_v<ic::OffFileReadResult>);
static_assert(
    std::is_nothrow_move_constructible_v<OffDuplicateFileWorkflowResult>);
static_assert(
    std::is_nothrow_move_assignable_v<OffDuplicateFileWorkflowResult>);
static_assert(std::is_nothrow_move_constructible_v<OffFileWorkflowResult>);
static_assert(std::is_nothrow_move_assignable_v<OffFileWorkflowResult>);
std::uint64_t Index(nb::handle value) {
  if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
    throw nb::type_error("indices must be integers, not booleans or None");
  const auto result = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  return static_cast<std::uint64_t>(result);
}
std::pair<nb::handle, nb::handle> Pair(nb::handle value) {
  if (PyTuple_Check(value.ptr()) && PyTuple_GET_SIZE(value.ptr()) == 2)
    return {PyTuple_GET_ITEM(value.ptr(), 0), PyTuple_GET_ITEM(value.ptr(), 1)};
  if (PyList_Check(value.ptr()) && PyList_GET_SIZE(value.ptr()) == 2)
    return {PyList_GET_ITEM(value.ptr(), 0), PyList_GET_ITEM(value.ptr(), 1)};
  throw nb::value_error("request items must be two-element tuple/list pairs");
}
std::string Text(nb::handle value, std::uint64_t max_bytes) {
  if (!PyUnicode_Check(value.ptr()))
    throw nb::type_error("text arguments must be Unicode strings");
  if (static_cast<std::uint64_t>(PyUnicode_GET_LENGTH(value.ptr())) > max_bytes)
    throw nb::value_error("text exceeds the application preset limit");
  Py_ssize_t count = 0;
  const char* bytes = PyUnicode_AsUTF8AndSize(value.ptr(), &count);
  if (!bytes) throw nb::python_error();
  if (static_cast<std::uint64_t>(count) > max_bytes ||
      std::memchr(bytes, 0, static_cast<std::size_t>(count)))
    throw nb::value_error("text exceeds its UTF-8 limit or contains NUL");
  return {bytes, static_cast<std::size_t>(count)};
}
bool AsciiAlphaNumeric(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9');
}
void CheckInputId(std::string_view value) {
  if (value.empty() || !AsciiAlphaNumeric(value.front()))
    throw nb::value_error("input_id must start with an ASCII letter or digit");
  for (char c : value)
    if (!AsciiAlphaNumeric(c) && c != '_' && c != '.' && c != ':' && c != '-')
      throw nb::value_error("input_id is not an opaque ASCII identifier");
}
struct PythonMemoryDeleter {
  void operator()(wchar_t* value) const noexcept { PyMem_Free(value); }
};
std::filesystem::path Path(nb::handle value, std::uint64_t max_units) {
  nb::object text = nb::steal(PyOS_FSPath(value.ptr()));
  if (!text.is_valid()) throw nb::python_error();
  if (!PyUnicode_Check(text.ptr()))
    throw nb::type_error("filesystem paths must be Unicode, not bytes");
  const auto length = PyUnicode_GET_LENGTH(text.ptr());
  if (static_cast<std::uint64_t>(length) >= max_units)
    throw nb::value_error("path exceeds the application native-path limit");
  const auto nul = PyUnicode_FindChar(text.ptr(), 0, 0, length, 1);
  if (nul == -2) throw nb::python_error();
  if (nul >= 0) throw nb::value_error("filesystem path contains NUL");
  Py_ssize_t utf8_length = 0;
  if (!PyUnicode_AsUTF8AndSize(text.ptr(), &utf8_length))
    throw nb::python_error();
  if (static_cast<std::uint64_t>(utf8_length) > kOffFrontendPathUtf8Bytes)
    throw nb::value_error("path exceeds the application UTF-8 byte limit");
#if defined(_WIN32)
  Py_ssize_t wide_length = 0;
  std::unique_ptr<wchar_t, PythonMemoryDeleter> wide(
      PyUnicode_AsWideCharString(text.ptr(), &wide_length));
  if (!wide) throw nb::python_error();
  if (static_cast<std::uint64_t>(wide_length) >= max_units)
    throw nb::value_error("path exceeds the application native-path limit");
  return std::filesystem::path(wide.get(), wide.get() + wide_length);
#else
  nb::object bytes = nb::steal(PyUnicode_EncodeFSDefault(text.ptr()));
  if (!bytes.is_valid()) throw nb::python_error();
  const auto count = PyBytes_GET_SIZE(bytes.ptr());
  if (static_cast<std::uint64_t>(count) >= max_units)
    throw nb::value_error("path exceeds the application native-path limit");
  return std::filesystem::path(PyBytes_AS_STRING(bytes.ptr()),
                               PyBytes_AS_STRING(bytes.ptr()) + count);
#endif
}
template <class Function>
void BoundedItems(nb::handle value, std::uint64_t limit, Function&& function) {
  if ((PyTuple_CheckExact(value.ptr()) || PyList_CheckExact(value.ptr())) &&
      static_cast<std::uint64_t>(Py_SIZE(value.ptr())) > limit)
    throw nb::value_error("request count exceeds the application preset");
  nb::object iterator = nb::steal(PyObject_GetIter(value.ptr()));
  if (!iterator.is_valid()) throw nb::python_error();
  std::uint64_t count = 0;
  while (true) {
    nb::object item = nb::steal(PyIter_Next(iterator.ptr()));
    if (!item.is_valid()) {
      if (PyErr_Occurred()) throw nb::python_error();
      return;
    }
    if (count == limit)
      throw nb::value_error("request count exceeds the application preset");
    function(item);
    ++count;
  }
}
void CheckReceipt(nb::handle original, std::string_view name) {
  if (name.empty() || name == "." || name == ".." || name.back() == '.' ||
      name.back() == ' ')
    throw nb::value_error("report_path must be a portable flat basename");
  const auto length = PyUnicode_GET_LENGTH(original.ptr());
  for (Py_ssize_t i = 0; i < length; ++i) {
    const auto scalar = PyUnicode_ReadChar(original.ptr(), i);
    if (scalar == static_cast<Py_UCS4>(-1)) throw nb::python_error();
    if (scalar < kControlEnd ||
        (scalar >= kDelete && scalar < kLatinControlEnd) ||
        (scalar < kDelete && kProhibitedName.find(static_cast<char>(scalar)) !=
                                 kProhibitedName.npos))
      throw nb::value_error(
          "report_path contains a prohibited portable-name character");
  }
  const auto dot = name.find('.');
  const auto stem = name.substr(0, dot);
  const auto equal = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
      const auto folded =
          a[i] >= 'A' && a[i] <= 'Z' ? a[i] + ('a' - 'A') : a[i];
      if (folded != b[i]) return false;
    }
    return true;
  };
  for (auto device : kDeviceNames)
    if (equal(stem, device))
      throw nb::value_error("report_path is a reserved device name");
  if (stem.size() == kNumberedDeviceLength &&
      (equal(stem.substr(0, kDevicePrefixLength), kComDevicePrefix) ||
       equal(stem.substr(0, kDevicePrefixLength), kLptDevicePrefix)) &&
      stem.back() >= kFirstDeviceNumber && stem.back() <= kLastDeviceNumber)
    throw nb::value_error("report_path is a reserved device name");
}
bool SameComponent(const std::filesystem::path& a,
                   const std::filesystem::path& b) {
#if defined(_WIN32)
  const auto& first = a.native();
  const auto& second = b.native();
  // Inputs are admitted bounded paths. Native ordinal comparison also handles
  // non-ASCII Windows path casing without changing caller-owned arguments.
  return CompareStringOrdinal(first.data(), static_cast<int>(first.size()),
                              second.data(), static_cast<int>(second.size()),
                              TRUE) == CSTR_EQUAL;
#else
  return a == b;
#endif
}
bool Contains(const std::filesystem::path& parent,
              const std::filesystem::path& child) {
  auto p = parent.begin();
  auto c = child.begin();
  for (; p != parent.end() && c != child.end(); ++p, ++c)
    if (!SameComponent(*p, *c)) return false;
  return p == parent.end();
}
void Isolate(const std::filesystem::path& source,
             const std::optional<std::filesystem::path>& destination) {
  if (!destination) return;
  std::error_code error;
  const auto input = std::filesystem::weakly_canonical(source, error);
  // A failed comparison must not turn an actual native path/IO refusal into
  // guessed success or canonicalize the logical IO argument.
  if (error) return;
  const auto output = std::filesystem::weakly_canonical(*destination, error);
  if (error) return;
  if (Contains(input, output) || Contains(output, input))
    throw nb::value_error("source entry and destination must not overlap");
}
void InitExecution(OffExecution* self, nb::handle name) {
  constexpr auto kPresetNameBytes =
      std::max(kBalancedOffPreset.size(), kSerialOffPreset.size());
  const auto owned = Text(name, kPresetNameBytes);
  new (self) OffExecution(owned);
}
nb::object Prepare(nb::handle source, nb::handle destination,
                   nb::handle targets, nb::handle text, nb::handle edge_count,
                   nb::handle execution, nb::handle input_id,
                   nb::handle report_path, nb::handle cancellation) {
  if (!nb::isinstance<OffExecution>(execution))
    throw nb::type_error("execution must be a resolved OffExecution session");
  const auto& session = nb::cast<const OffExecution&>(execution);
  std::stop_source stop;
  if (!cancellation.is_none()) {
    if (!nb::isinstance<OffCancellation>(cancellation))
      throw nb::type_error("cancellation must be an OffCancellation");
    stop = nb::cast<const OffCancellation&>(cancellation).Source();
  }
  nb::object object = nb::inst_alloc(nb::type<OffFileWorkflowResult>());
  auto* result = new (nb::inst_ptr<OffFileWorkflowResult>(object))
      OffFileWorkflowResult(session, std::move(stop));
  nb::inst_mark_ready(object);
  const auto& limits = result->preset;
  const auto path_limit =
      limits.capture_limits.files.max_native_path_code_units;
  result->source = Path(source, path_limit);
  if (!destination.is_none())
    result->destination = Path(destination, path_limit);
  if (Text(text, kCanonicalOffTextPolicy.size()) != kCanonicalOffTextPolicy)
    throw nb::value_error("text must be canonical-ascii");
  if (Text(edge_count, kUnknownZeroOffEdgePolicy.size()) !=
      kUnknownZeroOffEdgePolicy)
    throw nb::value_error("edge_count must be unknown-zero");
  result->text = ic::OffTextPolicy::kCanonicalAscii;
  result->edge_count = ic::OffEdgeCountPolicy::kUnknownZero;
  result->input_id = Text(input_id, limits.workflow_limits.max_input_id_bytes);
  CheckInputId(result->input_id);
  result->report_path =
      Text(report_path,
           limits.workflow_limits.publication.files.max_receipt_name_bytes);
  CheckReceipt(report_path, result->report_path);
  const auto entry = result->source.filename().u8string();
  const std::string_view entry_name(reinterpret_cast<const char*>(entry.data()),
                                    entry.size());
  if (entry_name.size() > limits.capture_limits.files.max_entry_name_bytes)
    throw nb::value_error("source basename exceeds its UTF-8 limit");
  bool equal = entry_name.size() == result->report_path.size();
  for (std::size_t i = 0; equal && i < entry_name.size(); ++i) {
    const auto fold = [](char c) {
      return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
    };
    equal = fold(entry_name[i]) == fold(result->report_path[i]);
  }
  if (equal)
    throw nb::value_error("report_path collides with the source basename");
  BoundedItems(targets, limits.workflow_limits.repair.max_targets,
               [&](nb::handle item) {
                 const auto [keep, remove] = Pair(item);
                 result->targets.push_back({Index(keep), Index(remove)});
               });
  Isolate(result->source, result->destination);
  return object;
}
nb::object Run(nb::handle prepared) {
  if (!nb::isinstance<OffFileWorkflowResult>(prepared))
    throw nb::type_error("prepared OFF result required");
  auto* result = nb::inst_ptr<OffFileWorkflowResult>(prepared);
  if (result->state != InvocationState::kPrepared)
    throw nb::value_error("prepared OFF invocation is single-use");
  nb::object returned = nb::borrow(prepared);
  result->state = InvocationState::kRunning;
  try {
    {
      nb::gil_scoped_release release;
      result->capture =
          ic::ReadOffFileAsset(result->source, result->preset.capture_limits,
                               result->preset.capture_work, result->context,
                               result->stop.get_token());
      if (result->capture.asset) {
        std::optional<OffWorkflowPublication> publication;
        if (result->destination)
          publication.emplace(*result->destination, result->report_path);
        const OffDuplicateFileWorkflowRequest request{
            *result->capture.asset, result->input_id,   result->targets,
            result->text,           result->edge_count, publication};
        result->workflow_started = true;
        auto native = RunOffDuplicateFileWorkflow(
            request, result->preset.workflow_limits,
            result->preset.workflow_work, result->stop.get_token());
        result->workflow = std::move(native);
      }
    }
    result->state = InvocationState::kComplete;
    return returned;
  } catch (...) {
    result->state = InvocationState::kFailed;
    throw;
  }
}
nb::object Map(const OffFileWorkflowResult& result,
               std::span<const geo::index_t> (
                   MaterializedDuplicateRepair::*map)() const noexcept) {
  result.RequireComplete();
  const auto* repair = result.Repair();
  if (!repair) return nb::none();
  const auto values = (repair->*map)();
  nb::tuple output =
      nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
  if (!output.is_valid()) throw nb::python_error();
  for (std::size_t i = 0; i < values.size(); ++i) {
    PyObject* value = PyLong_FromUnsignedLongLong(values[i]);
    if (!value) throw nb::python_error();
    PyTuple_SET_ITEM(output.ptr(), static_cast<Py_ssize_t>(i), value);
  }
  return output;
}
nb::object SnapshotCounts(
    const OffFileWorkflowResult& result,
    const geo::MaterializedMesh& (MaterializedDuplicateRepair::*mesh)()
        const noexcept) {
  result.RequireComplete();
  const auto* repair = result.Repair();
  if (!repair) return nb::none();
  const auto view = (repair->*mesh)().View();
  return nb::make_tuple(view.positions.size() / kPositionComponents,
                        view.face_offsets.size() - 1,
                        view.corner_vertices.size());
}
namespace preset_keys {
constexpr std::string_view kCancellationBlockBytes = "cancellation_block_bytes";
constexpr std::string_view kCancellationBlockRecords =
    "cancellation_block_records";
constexpr std::string_view kCancellationBlockScalars =
    "cancellation_block_scalars";
constexpr std::string_view kCancellationBlockTokens =
    "cancellation_block_tokens";
constexpr std::string_view kCaptureLimits = "capture_limits";
constexpr std::string_view kCaptureWork = "capture_work";
constexpr std::string_view kExecution = "execution";
constexpr std::string_view kExpectedInspection = "expected_inspection";
constexpr std::string_view kFiles = "files";
constexpr std::string_view kGeometry = "geometry";
constexpr std::string_view kHashing = "hashing";
constexpr std::string_view kInspection = "inspection";
constexpr std::string_view kIoBlockBytes = "io_block_bytes";
constexpr std::string_view kMaterialization = "materialization";
constexpr std::string_view kMaxAttributeOffsets = "max_attribute_offsets";
constexpr std::string_view kMaxAttributeScalars = "max_attribute_scalars";
constexpr std::string_view kMaxAttributes = "max_attributes";
constexpr std::string_view kMaxCodecNewRequestedBytes =
    "max_codec_new_requested_bytes";
constexpr std::string_view kMaxCommentBytes = "max_comment_bytes";
constexpr std::string_view kMaxComparisonStepsPerTarget =
    "max_comparison_steps_per_target";
constexpr std::string_view kMaxCorners = "max_corners";
constexpr std::string_view kMaxCornersPerFace = "max_corners_per_face";
constexpr std::string_view kMaxDeclarationNameBytes =
    "max_declaration_name_bytes";
constexpr std::string_view kMaxDescriptorBytes = "max_descriptor_bytes";
constexpr std::string_view kMaxEntryBytes = "max_entry_bytes";
constexpr std::string_view kMaxEntryNameBytes = "max_entry_name_bytes";
constexpr std::string_view kMaxFaceOffsets = "max_face_offsets";
constexpr std::string_view kMaxFaces = "max_faces";
constexpr std::string_view kMaxFileNewRequestedBytes =
    "max_file_new_requested_bytes";
constexpr std::string_view kMaxHashInputBytes = "max_hash_input_bytes";
constexpr std::string_view kMaxInputIdBytes = "max_input_id_bytes";
constexpr std::string_view kMaxIssues = "max_issues";
constexpr std::string_view kMaxJobs = "max_jobs";
constexpr std::string_view kMaxLineBytes = "max_line_bytes";
constexpr std::string_view kMaxMetadataEntries = "max_metadata_entries";
constexpr std::string_view kMaxNativePathCodeUnits =
    "max_native_path_code_units";
constexpr std::string_view kMaxNewRequestedBytes = "max_new_requested_bytes";
constexpr std::string_view kMaxOutputBytes = "max_output_bytes";
constexpr std::string_view kMaxPresenceBytes = "max_presence_bytes";
constexpr std::string_view kMaxReceiptBytes = "max_receipt_bytes";
constexpr std::string_view kMaxReceiptNameBytes = "max_receipt_name_bytes";
constexpr std::string_view kMaxReportBytes = "max_report_bytes";
constexpr std::string_view kMaxRowLocalDeclarations =
    "max_row_local_declarations";
constexpr std::string_view kMaxSourceBytes = "max_source_bytes";
constexpr std::string_view kMaxSourceProtectionBytes =
    "max_source_protection_bytes";
constexpr std::string_view kMaxTargets = "max_targets";
constexpr std::string_view kMaxTokenBytes = "max_token_bytes";
constexpr std::string_view kMaxTotalFileBytes = "max_total_file_bytes";
constexpr std::string_view kMaxTotalNewRequestedBytes =
    "max_total_new_requested_bytes";
constexpr std::string_view kMaxVerificationCorners = "max_verification_corners";
constexpr std::string_view kMaxVerificationFaces = "max_verification_faces";
constexpr std::string_view kMaxVerificationVertices =
    "max_verification_vertices";
constexpr std::string_view kMaxVertices = "max_vertices";
constexpr std::string_view kMaxWrapperNewRequestedBytes =
    "max_wrapper_new_requested_bytes";
constexpr std::string_view kMinimumParallelVertices =
    "minimum_parallel_vertices";
constexpr std::string_view kOff = "off";
constexpr std::string_view kParallelMinBytes = "parallel_min_bytes";
constexpr std::string_view kPartitionBytes = "partition_bytes";
constexpr std::string_view kPublication = "publication";
constexpr std::string_view kReload = "reload";
constexpr std::string_view kRepair = "repair";
constexpr std::string_view kReport = "report";
constexpr std::string_view kSelection = "selection";
constexpr std::string_view kTrackedPayloadBudgetBytes =
    "tracked_payload_budget_bytes";
constexpr std::string_view kVerification = "verification";
constexpr std::string_view kWorkerBudget = "worker_budget";
constexpr std::string_view kWorkflowLimits = "workflow_limits";
constexpr std::string_view kWorkflowWork = "workflow_work";
constexpr std::string_view kWriting = "writing";
}  // namespace preset_keys
nb::dict PresetObservation(const geo::ExecutionOptions& value);
nb::dict PresetObservation(const ic::OffFileLimits& value);
nb::dict PresetObservation(const ic::OffFileWorkPolicy& value);
nb::dict PresetObservation(const geo::MeshMaterializationLimits& value);
nb::dict PresetObservation(const geo::MeshInspectionLimits& value);
nb::dict PresetObservation(const geo::MeshMaterializationWorkPolicy& value);
nb::dict PresetObservation(const ic::OffReadLimits& value);
nb::dict PresetObservation(const ic::OffWriteLimits& value);
nb::dict PresetObservation(const ic::OffWorkPolicy& value);
nb::dict PresetObservation(const ic::OffFileReadLimits& value);
nb::dict PresetObservation(const ic::OffFileReadWorkPolicy& value);
nb::dict PresetObservation(const ic::OffFilePublicationLimits& value);
nb::dict PresetObservation(const ic::OffFilePublicationWorkPolicy& value);
nb::dict PresetObservation(const MaterializedDuplicateLimits& value);
nb::dict PresetObservation(const OffDuplicateFileWorkflowLimits& value);
nb::dict PresetObservation(const OffByteWorkPolicy& value);
nb::dict PresetObservation(const OffDuplicateFileWorkflowWorkPolicy& value);
nb::dict PresetObservation(const OffPreset& value);
template <class T>
nb::object PresetValue(const T& value) {
  if constexpr (std::is_arithmetic_v<T>)
    return nb::cast(value);
  else
    return PresetObservation(value);
}
nb::dict PresetObservation(const geo::ExecutionOptions& value) {
  nb::dict result;
  result[preset_keys::kWorkerBudget.data()] = PresetValue(value.worker_budget);
  result[preset_keys::kMinimumParallelVertices.data()] =
      PresetValue(value.minimum_parallel_vertices);
  result[preset_keys::kTrackedPayloadBudgetBytes.data()] =
      PresetValue(value.tracked_payload_budget_bytes);
  return result;
}
nb::dict PresetObservation(const ic::OffFileLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxEntryBytes.data()] =
      PresetValue(value.max_entry_bytes);
  result[preset_keys::kMaxReceiptBytes.data()] =
      PresetValue(value.max_receipt_bytes);
  result[preset_keys::kMaxTotalFileBytes.data()] =
      PresetValue(value.max_total_file_bytes);
  result[preset_keys::kMaxEntryNameBytes.data()] =
      PresetValue(value.max_entry_name_bytes);
  result[preset_keys::kMaxReceiptNameBytes.data()] =
      PresetValue(value.max_receipt_name_bytes);
  result[preset_keys::kMaxNativePathCodeUnits.data()] =
      PresetValue(value.max_native_path_code_units);
  result[preset_keys::kMaxFileNewRequestedBytes.data()] =
      PresetValue(value.max_file_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const ic::OffFileWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kIoBlockBytes.data()] = PresetValue(value.io_block_bytes);
  result[preset_keys::kParallelMinBytes.data()] =
      PresetValue(value.parallel_min_bytes);
  result[preset_keys::kPartitionBytes.data()] =
      PresetValue(value.partition_bytes);
  result[preset_keys::kCancellationBlockBytes.data()] =
      PresetValue(value.cancellation_block_bytes);
  result[preset_keys::kMaxJobs.data()] = PresetValue(value.max_jobs);
  return result;
}
nb::dict PresetObservation(const geo::MeshMaterializationLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxVertices.data()] = PresetValue(value.max_vertices);
  result[preset_keys::kMaxFaces.data()] = PresetValue(value.max_faces);
  result[preset_keys::kMaxCorners.data()] = PresetValue(value.max_corners);
  result[preset_keys::kMaxFaceOffsets.data()] =
      PresetValue(value.max_face_offsets);
  result[preset_keys::kMaxAttributes.data()] =
      PresetValue(value.max_attributes);
  result[preset_keys::kMaxAttributeScalars.data()] =
      PresetValue(value.max_attribute_scalars);
  result[preset_keys::kMaxAttributeOffsets.data()] =
      PresetValue(value.max_attribute_offsets);
  result[preset_keys::kMaxPresenceBytes.data()] =
      PresetValue(value.max_presence_bytes);
  result[preset_keys::kMaxMetadataEntries.data()] =
      PresetValue(value.max_metadata_entries);
  result[preset_keys::kMaxDescriptorBytes.data()] =
      PresetValue(value.max_descriptor_bytes);
  result[preset_keys::kMaxNewRequestedBytes.data()] =
      PresetValue(value.max_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const geo::MeshInspectionLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxIssues.data()] = PresetValue(value.max_issues);
  result[preset_keys::kMaxNewRequestedBytes.data()] =
      PresetValue(value.max_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const geo::MeshMaterializationWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kParallelMinBytes.data()] =
      PresetValue(value.parallel_min_bytes);
  result[preset_keys::kPartitionBytes.data()] =
      PresetValue(value.partition_bytes);
  result[preset_keys::kCancellationBlockScalars.data()] =
      PresetValue(value.cancellation_block_scalars);
  result[preset_keys::kMaxJobs.data()] = PresetValue(value.max_jobs);
  return result;
}
nb::dict PresetObservation(const ic::OffReadLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxSourceBytes.data()] =
      PresetValue(value.max_source_bytes);
  result[preset_keys::kMaxVertices.data()] = PresetValue(value.max_vertices);
  result[preset_keys::kMaxFaces.data()] = PresetValue(value.max_faces);
  result[preset_keys::kMaxCorners.data()] = PresetValue(value.max_corners);
  result[preset_keys::kMaxCornersPerFace.data()] =
      PresetValue(value.max_corners_per_face);
  result[preset_keys::kMaxLineBytes.data()] = PresetValue(value.max_line_bytes);
  result[preset_keys::kMaxTokenBytes.data()] =
      PresetValue(value.max_token_bytes);
  result[preset_keys::kMaxCommentBytes.data()] =
      PresetValue(value.max_comment_bytes);
  result[preset_keys::kMaxCodecNewRequestedBytes.data()] =
      PresetValue(value.max_codec_new_requested_bytes);
  result[preset_keys::kMaxTotalNewRequestedBytes.data()] =
      PresetValue(value.max_total_new_requested_bytes);
  result[preset_keys::kMaterialization.data()] =
      PresetValue(value.materialization);
  result[preset_keys::kInspection.data()] = PresetValue(value.inspection);
  return result;
}
nb::dict PresetObservation(const ic::OffWriteLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxOutputBytes.data()] =
      PresetValue(value.max_output_bytes);
  result[preset_keys::kMaxVertices.data()] = PresetValue(value.max_vertices);
  result[preset_keys::kMaxFaces.data()] = PresetValue(value.max_faces);
  result[preset_keys::kMaxCorners.data()] = PresetValue(value.max_corners);
  result[preset_keys::kMaxCornersPerFace.data()] =
      PresetValue(value.max_corners_per_face);
  result[preset_keys::kMaxLineBytes.data()] = PresetValue(value.max_line_bytes);
  result[preset_keys::kMaxTokenBytes.data()] =
      PresetValue(value.max_token_bytes);
  result[preset_keys::kMaxCodecNewRequestedBytes.data()] =
      PresetValue(value.max_codec_new_requested_bytes);
  result[preset_keys::kMaxTotalNewRequestedBytes.data()] =
      PresetValue(value.max_total_new_requested_bytes);
  result[preset_keys::kInspection.data()] = PresetValue(value.inspection);
  return result;
}
nb::dict PresetObservation(const ic::OffWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kCancellationBlockBytes.data()] =
      PresetValue(value.cancellation_block_bytes);
  result[preset_keys::kCancellationBlockTokens.data()] =
      PresetValue(value.cancellation_block_tokens);
  result[preset_keys::kCancellationBlockRecords.data()] =
      PresetValue(value.cancellation_block_records);
  result[preset_keys::kGeometry.data()] = PresetValue(value.geometry);
  return result;
}
nb::dict PresetObservation(const ic::OffFileReadLimits& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kOff.data()] = PresetValue(value.off);
  return result;
}
nb::dict PresetObservation(const ic::OffFileReadWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kOff.data()] = PresetValue(value.off);
  return result;
}
nb::dict PresetObservation(const ic::OffFilePublicationLimits& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kExpectedInspection.data()] =
      PresetValue(value.expected_inspection);
  result[preset_keys::kReload.data()] = PresetValue(value.reload);
  return result;
}
nb::dict PresetObservation(const ic::OffFilePublicationWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kExpectedInspection.data()] =
      PresetValue(value.expected_inspection);
  result[preset_keys::kReload.data()] = PresetValue(value.reload);
  return result;
}
nb::dict PresetObservation(const MaterializedDuplicateLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxTargets.data()] = PresetValue(value.max_targets);
  result[preset_keys::kMaxRowLocalDeclarations.data()] =
      PresetValue(value.max_row_local_declarations);
  result[preset_keys::kMaxDeclarationNameBytes.data()] =
      PresetValue(value.max_declaration_name_bytes);
  result[preset_keys::kMaxComparisonStepsPerTarget.data()] =
      PresetValue(value.max_comparison_steps_per_target);
  result[preset_keys::kMaxNewRequestedBytes.data()] =
      PresetValue(value.max_new_requested_bytes);
  result[preset_keys::kInspection.data()] = PresetValue(value.inspection);
  result[preset_keys::kSelection.data()] = PresetValue(value.selection);
  return result;
}
nb::dict PresetObservation(const OffDuplicateFileWorkflowLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxWrapperNewRequestedBytes.data()] =
      PresetValue(value.max_wrapper_new_requested_bytes);
  result[preset_keys::kMaxInputIdBytes.data()] =
      PresetValue(value.max_input_id_bytes);
  result[preset_keys::kMaxSourceProtectionBytes.data()] =
      PresetValue(value.max_source_protection_bytes);
  result[preset_keys::kMaxReportBytes.data()] =
      PresetValue(value.max_report_bytes);
  result[preset_keys::kMaxHashInputBytes.data()] =
      PresetValue(value.max_hash_input_bytes);
  result[preset_keys::kMaxVerificationVertices.data()] =
      PresetValue(value.max_verification_vertices);
  result[preset_keys::kMaxVerificationFaces.data()] =
      PresetValue(value.max_verification_faces);
  result[preset_keys::kMaxVerificationCorners.data()] =
      PresetValue(value.max_verification_corners);
  result[preset_keys::kRepair.data()] = PresetValue(value.repair);
  result[preset_keys::kWriting.data()] = PresetValue(value.writing);
  result[preset_keys::kReload.data()] = PresetValue(value.reload);
  result[preset_keys::kPublication.data()] = PresetValue(value.publication);
  return result;
}
nb::dict PresetObservation(const OffByteWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kParallelMinBytes.data()] =
      PresetValue(value.parallel_min_bytes);
  result[preset_keys::kPartitionBytes.data()] =
      PresetValue(value.partition_bytes);
  result[preset_keys::kCancellationBlockBytes.data()] =
      PresetValue(value.cancellation_block_bytes);
  result[preset_keys::kMaxJobs.data()] = PresetValue(value.max_jobs);
  return result;
}
nb::dict PresetObservation(const OffDuplicateFileWorkflowWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kRepair.data()] = PresetValue(value.repair);
  result[preset_keys::kWriting.data()] = PresetValue(value.writing);
  result[preset_keys::kReload.data()] = PresetValue(value.reload);
  result[preset_keys::kVerification.data()] = PresetValue(value.verification);
  result[preset_keys::kHashing.data()] = PresetValue(value.hashing);
  result[preset_keys::kReport.data()] = PresetValue(value.report);
  result[preset_keys::kPublication.data()] = PresetValue(value.publication);
  return result;
}
nb::dict PresetObservation(const OffPreset& value) {
  nb::dict result;
  result[preset_keys::kExecution.data()] = PresetValue(value.execution);
  result[preset_keys::kCaptureLimits.data()] =
      PresetValue(value.capture_limits);
  result[preset_keys::kCaptureWork.data()] = PresetValue(value.capture_work);
  result[preset_keys::kWorkflowLimits.data()] =
      PresetValue(value.workflow_limits);
  result[preset_keys::kWorkflowWork.data()] = PresetValue(value.workflow_work);
  return result;
}
}  // namespace
std::stop_source ShareOffCancellation(nb::handle value) {
  if (value.is_none()) return {};
  if (!nb::isinstance<OffCancellation>(value))
    throw nb::type_error("cancellation must be an OffCancellation");
  return nb::cast<const OffCancellation&>(value).Source();
}

void RegisterOffBindings(nb::module_& module) {
  if (!nb::type<CandidateOutcome>().is_valid()) {
    nb::enum_<CandidateOutcome>(module, "CandidateOutcome")
        .value("accepted", CandidateOutcome::accepted)
        .value("unchanged", CandidateOutcome::unchanged)
        .value("rejected", CandidateOutcome::rejected);
  }
  if (!nb::type<MaterializedDuplicateDiagnosticCode>().is_valid()) {
    nb::enum_<MaterializedDuplicateDiagnosticCode>(
        module, "MaterializedDuplicateDiagnosticCode")
        .value("kNone", MaterializedDuplicateDiagnosticCode::kNone)
        .value("kRequiredFieldZero",
               MaterializedDuplicateDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               MaterializedDuplicateDiagnosticCode::kConfigurationExtent)
        .value("kDeclarationDomain",
               MaterializedDuplicateDiagnosticCode::kDeclarationDomain)
        .value("kDeclarationNameEmpty",
               MaterializedDuplicateDiagnosticCode::kDeclarationNameEmpty)
        .value("kDeclarationDuplicate",
               MaterializedDuplicateDiagnosticCode::kDeclarationDuplicate)
        .value("kTargetLimit",
               MaterializedDuplicateDiagnosticCode::kTargetLimit)
        .value("kDeclarationLimit",
               MaterializedDuplicateDiagnosticCode::kDeclarationLimit)
        .value("kDeclarationNameByteLimit",
               MaterializedDuplicateDiagnosticCode::kDeclarationNameByteLimit)
        .value("kComparisonLimit",
               MaterializedDuplicateDiagnosticCode::kComparisonLimit)
        .value("kRequestBudget",
               MaterializedDuplicateDiagnosticCode::kRequestBudget)
        .value("kStopRequested",
               MaterializedDuplicateDiagnosticCode::kStopRequested)
        .value("kStorageIssues",
               MaterializedDuplicateDiagnosticCode::kStorageIssues)
        .value("kNonfiniteAttribute",
               MaterializedDuplicateDiagnosticCode::kNonfiniteAttribute)
        .value("kUnsupportedAttribute",
               MaterializedDuplicateDiagnosticCode::kUnsupportedAttribute)
        .value("kTargetRange",
               MaterializedDuplicateDiagnosticCode::kTargetRange)
        .value("kSelfTarget", MaterializedDuplicateDiagnosticCode::kSelfTarget)
        .value("kRepeatedTarget",
               MaterializedDuplicateDiagnosticCode::kRepeatedTarget)
        .value("kRemovedRepresentative",
               MaterializedDuplicateDiagnosticCode::kRemovedRepresentative)
        .value("kNotEquivalent",
               MaterializedDuplicateDiagnosticCode::kNotEquivalent)
        .value("kGeometry", MaterializedDuplicateDiagnosticCode::kGeometry)
        .value("kPostcondition",
               MaterializedDuplicateDiagnosticCode::kPostcondition);
  }
  if (!nb::type<MaterializedDuplicateStage>().is_valid()) {
    nb::enum_<MaterializedDuplicateStage>(module, "MaterializedDuplicateStage")
        .value("kConfiguration", MaterializedDuplicateStage::kConfiguration)
        .value("kSourceInspection",
               MaterializedDuplicateStage::kSourceInspection)
        .value("kEligibility", MaterializedDuplicateStage::kEligibility)
        .value("kTargets", MaterializedDuplicateStage::kTargets)
        .value("kComparison", MaterializedDuplicateStage::kComparison)
        .value("kSelection", MaterializedDuplicateStage::kSelection)
        .value("kCorrespondence", MaterializedDuplicateStage::kCorrespondence)
        .value("kCandidateInspection",
               MaterializedDuplicateStage::kCandidateInspection)
        .value("kVerification", MaterializedDuplicateStage::kVerification)
        .value("kPublication", MaterializedDuplicateStage::kPublication);
  }
  if (!nb::type<MaterializedDuplicateStatus>().is_valid()) {
    nb::enum_<MaterializedDuplicateStatus>(module,
                                           "MaterializedDuplicateStatus")
        .value("kSuccess", MaterializedDuplicateStatus::kSuccess)
        .value("kInvalidConfiguration",
               MaterializedDuplicateStatus::kInvalidConfiguration)
        .value("kLimitExceeded", MaterializedDuplicateStatus::kLimitExceeded)
        .value("kBudgetExceeded", MaterializedDuplicateStatus::kBudgetExceeded)
        .value("kCanceled", MaterializedDuplicateStatus::kCanceled)
        .value("kInvalidSourceStorage",
               MaterializedDuplicateStatus::kInvalidSourceStorage)
        .value("kRejected", MaterializedDuplicateStatus::kRejected)
        .value("kGeometryFailure",
               MaterializedDuplicateStatus::kGeometryFailure)
        .value("kVerificationFailure",
               MaterializedDuplicateStatus::kVerificationFailure);
  }
  if (!nb::type<geo::MaterializationDiagnosticCode>().is_valid()) {
    nb::enum_<geo::MaterializationDiagnosticCode>(
        module, "MaterializationDiagnosticCode")
        .value("kNone", geo::MaterializationDiagnosticCode::kNone)
        .value("kRequiredFieldZero",
               geo::MaterializationDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               geo::MaterializationDiagnosticCode::kConfigurationExtent)
        .value("kTransportSelector",
               geo::MaterializationDiagnosticCode::kTransportSelector)
        .value("kPositionExtent",
               geo::MaterializationDiagnosticCode::kPositionExtent)
        .value("kTriangleExtent",
               geo::MaterializationDiagnosticCode::kTriangleExtent)
        .value("kStorageExtent",
               geo::MaterializationDiagnosticCode::kStorageExtent)
        .value("kCountLimit", geo::MaterializationDiagnosticCode::kCountLimit)
        .value("kIssueLimit", geo::MaterializationDiagnosticCode::kIssueLimit)
        .value("kAllocationProfile",
               geo::MaterializationDiagnosticCode::kAllocationProfile)
        .value("kRequestBudget",
               geo::MaterializationDiagnosticCode::kRequestBudget)
        .value("kStopRequested",
               geo::MaterializationDiagnosticCode::kStopRequested)
        .value("kSourceStorage",
               geo::MaterializationDiagnosticCode::kSourceStorage)
        .value("kSelectionOrder",
               geo::MaterializationDiagnosticCode::kSelectionOrder)
        .value("kSelectionRange",
               geo::MaterializationDiagnosticCode::kSelectionRange);
  }
  if (!nb::type<geo::MaterializationStatus>().is_valid()) {
    nb::enum_<geo::MaterializationStatus>(module, "MaterializationStatus")
        .value("kSuccess", geo::MaterializationStatus::kSuccess)
        .value("kInvalidConfiguration",
               geo::MaterializationStatus::kInvalidConfiguration)
        .value("kInvalidTransport",
               geo::MaterializationStatus::kInvalidTransport)
        .value("kLimitExceeded", geo::MaterializationStatus::kLimitExceeded)
        .value("kUnsupportedAllocationProfile",
               geo::MaterializationStatus::kUnsupportedAllocationProfile)
        .value("kBudgetExceeded", geo::MaterializationStatus::kBudgetExceeded)
        .value("kCanceled", geo::MaterializationStatus::kCanceled)
        .value("kInvalidSourceStorage",
               geo::MaterializationStatus::kInvalidSourceStorage)
        .value("kInvalidSelection",
               geo::MaterializationStatus::kInvalidSelection);
  }
  if (!nb::type<geo::StorageIssueCode>().is_valid()) {
    nb::enum_<geo::StorageIssueCode>(module, "StorageIssueCode")
        .value("kPositionNonfinite", geo::StorageIssueCode::kPositionNonfinite)
        .value("kFaceOffsetsEmpty", geo::StorageIssueCode::kFaceOffsetsEmpty)
        .value("kFaceOffsetsStart", geo::StorageIssueCode::kFaceOffsetsStart)
        .value("kFaceOffsetsEnd", geo::StorageIssueCode::kFaceOffsetsEnd)
        .value("kFaceOffsetRange", geo::StorageIssueCode::kFaceOffsetRange)
        .value("kFaceOffsetOrder", geo::StorageIssueCode::kFaceOffsetOrder)
        .value("kFaceTooShort", geo::StorageIssueCode::kFaceTooShort)
        .value("kCornerVertexRange", geo::StorageIssueCode::kCornerVertexRange)
        .value("kAttributeDuplicateName",
               geo::StorageIssueCode::kAttributeDuplicateName)
        .value("kAttributeDomain", geo::StorageIssueCode::kAttributeDomain)
        .value("kAttributeNameEmpty",
               geo::StorageIssueCode::kAttributeNameEmpty)
        .value("kAttributeComponentsZero",
               geo::StorageIssueCode::kAttributeComponentsZero)
        .value("kAttributeDenseShape",
               geo::StorageIssueCode::kAttributeDenseShape)
        .value("kAttributeOffsetsEmpty",
               geo::StorageIssueCode::kAttributeOffsetsEmpty)
        .value("kAttributeOffsetsCount",
               geo::StorageIssueCode::kAttributeOffsetsCount)
        .value("kAttributeOffsetsStart",
               geo::StorageIssueCode::kAttributeOffsetsStart)
        .value("kAttributeOffsetsEnd",
               geo::StorageIssueCode::kAttributeOffsetsEnd)
        .value("kAttributeOffsetRange",
               geo::StorageIssueCode::kAttributeOffsetRange)
        .value("kAttributeOffsetOrder",
               geo::StorageIssueCode::kAttributeOffsetOrder)
        .value("kAttributeOffsetAlignment",
               geo::StorageIssueCode::kAttributeOffsetAlignment)
        .value("kAttributePresenceCount",
               geo::StorageIssueCode::kAttributePresenceCount)
        .value("kAttributePresenceValue",
               geo::StorageIssueCode::kAttributePresenceValue)
        .value("kMetadataDuplicateKey",
               geo::StorageIssueCode::kMetadataDuplicateKey);
  }
  if (!nb::type<OffWorkflowStage>().is_valid()) {
    nb::enum_<OffWorkflowStage>(module, "OffWorkflowStage")
        .value("kConfiguration", OffWorkflowStage::kConfiguration)
        .value("kSourceProtection", OffWorkflowStage::kSourceProtection)
        .value("kRepair", OffWorkflowStage::kRepair)
        .value("kWrite", OffWorkflowStage::kWrite)
        .value("kReload", OffWorkflowStage::kReload)
        .value("kVerify", OffWorkflowStage::kVerify)
        .value("kHash", OffWorkflowStage::kHash)
        .value("kReport", OffWorkflowStage::kReport)
        .value("kPublication", OffWorkflowStage::kPublication)
        .value("kComplete", OffWorkflowStage::kComplete);
  }
  if (!nb::type<OffWorkflowStatus>().is_valid()) {
    nb::enum_<OffWorkflowStatus>(module, "OffWorkflowStatus")
        .value("kSuccess", OffWorkflowStatus::kSuccess)
        .value("kInvalidConfiguration",
               OffWorkflowStatus::kInvalidConfiguration)
        .value("kInvalidRequest", OffWorkflowStatus::kInvalidRequest)
        .value("kLimitExceeded", OffWorkflowStatus::kLimitExceeded)
        .value("kBudgetExceeded", OffWorkflowStatus::kBudgetExceeded)
        .value("kCanceled", OffWorkflowStatus::kCanceled)
        .value("kRejected", OffWorkflowStatus::kRejected)
        .value("kDelegatedFailure", OffWorkflowStatus::kDelegatedFailure)
        .value("kVerificationFailure", OffWorkflowStatus::kVerificationFailure);
  }
  if (!nb::type<OffWorkflowDiagnosticCode>().is_valid()) {
    nb::enum_<OffWorkflowDiagnosticCode>(module, "OffWorkflowDiagnosticCode")
        .value("kNone", OffWorkflowDiagnosticCode::kNone)
        .value("kRequiredFieldZero",
               OffWorkflowDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               OffWorkflowDiagnosticCode::kConfigurationExtent)
        .value("kInputIdentity", OffWorkflowDiagnosticCode::kInputIdentity)
        .value("kPolicy", OffWorkflowDiagnosticCode::kPolicy)
        .value("kNameUnsupported", OffWorkflowDiagnosticCode::kNameUnsupported)
        .value("kNameCollision", OffWorkflowDiagnosticCode::kNameCollision)
        .value("kCountLimit", OffWorkflowDiagnosticCode::kCountLimit)
        .value("kRequestBudget", OffWorkflowDiagnosticCode::kRequestBudget)
        .value("kStopRequested", OffWorkflowDiagnosticCode::kStopRequested)
        .value("kRepair", OffWorkflowDiagnosticCode::kRepair)
        .value("kWrite", OffWorkflowDiagnosticCode::kWrite)
        .value("kReload", OffWorkflowDiagnosticCode::kReload)
        .value("kCorrespondence", OffWorkflowDiagnosticCode::kCorrespondence)
        .value("kCanonicalPreservation",
               OffWorkflowDiagnosticCode::kCanonicalPreservation)
        .value("kReloadMismatch", OffWorkflowDiagnosticCode::kReloadMismatch)
        .value("kSourceChanged", OffWorkflowDiagnosticCode::kSourceChanged)
        .value("kHashExtent", OffWorkflowDiagnosticCode::kHashExtent)
        .value("kReportExtent", OffWorkflowDiagnosticCode::kReportExtent)
        .value("kReportStringEncoding",
               OffWorkflowDiagnosticCode::kReportStringEncoding)
        .value("kPublication", OffWorkflowDiagnosticCode::kPublication)
        .value("kBlocked", OffWorkflowDiagnosticCode::kBlocked)
        .value("kSolidUnavailable",
               OffWorkflowDiagnosticCode::kSolidUnavailable);
  }
  if (!nb::type<OffWorkflowExecutionOutcome>().is_valid()) {
    nb::enum_<OffWorkflowExecutionOutcome>(module,
                                           "OffWorkflowExecutionOutcome")
        .value("kCompleted", OffWorkflowExecutionOutcome::kCompleted)
        .value("kFailed", OffWorkflowExecutionOutcome::kFailed)
        .value("kCanceled", OffWorkflowExecutionOutcome::kCanceled);
  }
  if (!nb::type<OffWorkflowProfileOutcome>().is_valid()) {
    nb::enum_<OffWorkflowProfileOutcome>(module, "OffWorkflowProfileOutcome")
        .value("kPassed", OffWorkflowProfileOutcome::kPassed)
        .value("kFailed", OffWorkflowProfileOutcome::kFailed)
        .value("kIncomplete", OffWorkflowProfileOutcome::kIncomplete);
  }
  if (!nb::type<OffWorkflowPublicationOutcome>().is_valid()) {
    nb::enum_<OffWorkflowPublicationOutcome>(module,
                                             "OffWorkflowPublicationOutcome")
        .value("kNotRequested", OffWorkflowPublicationOutcome::kNotRequested)
        .value("kNotAttempted", OffWorkflowPublicationOutcome::kNotAttempted)
        .value("kFailed", OffWorkflowPublicationOutcome::kFailed)
        .value("kCanceled", OffWorkflowPublicationOutcome::kCanceled)
        .value("kPublished", OffWorkflowPublicationOutcome::kPublished);
  }
  if (!nb::type<OffWorkflowExitMeaning>().is_valid()) {
    nb::enum_<OffWorkflowExitMeaning>(module, "OffWorkflowExitMeaning")
        .value("kSuccess", OffWorkflowExitMeaning::kSuccess)
        .value("kRejectedOrIncomplete",
               OffWorkflowExitMeaning::kRejectedOrIncomplete)
        .value("kExecutionFailure", OffWorkflowExitMeaning::kExecutionFailure)
        .value("kCanceled", OffWorkflowExitMeaning::kCanceled);
  }
  if (!nb::type<OffWorkflowCheck>().is_valid()) {
    nb::enum_<OffWorkflowCheck>(module, "OffWorkflowCheck")
        .value("kSourceStorage", OffWorkflowCheck::kSourceStorage)
        .value("kTargetPolicy", OffWorkflowCheck::kTargetPolicy)
        .value("kCandidateStorage", OffWorkflowCheck::kCandidateStorage)
        .value("kRepairCorrespondence", OffWorkflowCheck::kRepairCorrespondence)
        .value("kCanonicalPreservation",
               OffWorkflowCheck::kCanonicalPreservation)
        .value("kReloadCandidate", OffWorkflowCheck::kReloadCandidate)
        .value("kSourceUnchanged", OffWorkflowCheck::kSourceUnchanged)
        .value("kStagedCandidate", OffWorkflowCheck::kStagedCandidate)
        .value("kStagedPhysicalMembers",
               OffWorkflowCheck::kStagedPhysicalMembers)
        .value("kSolidValidity", OffWorkflowCheck::kSolidValidity);
  }
  if (!nb::type<OffWorkflowCoverageState>().is_valid()) {
    nb::enum_<OffWorkflowCoverageState>(module, "OffWorkflowCoverageState")
        .value("kPerformed", OffWorkflowCoverageState::kPerformed)
        .value("kSkipped", OffWorkflowCoverageState::kSkipped)
        .value("kUnsupported", OffWorkflowCoverageState::kUnsupported)
        .value("kFailed", OffWorkflowCoverageState::kFailed);
  }
  if (!nb::type<ic::OffProfile>().is_valid()) {
    nb::enum_<ic::OffProfile>(module, "OffProfile")
        .value("kInvalid", ic::OffProfile::kInvalid)
        .value("kAsciiRecords", ic::OffProfile::kAsciiRecords);
  }
  if (!nb::type<ic::OffStatus>().is_valid()) {
    nb::enum_<ic::OffStatus>(module, "OffStatus")
        .value("kSuccess", ic::OffStatus::kSuccess)
        .value("kInvalidConfiguration", ic::OffStatus::kInvalidConfiguration)
        .value("kMalformed", ic::OffStatus::kMalformed)
        .value("kUnsupportedVariant", ic::OffStatus::kUnsupportedVariant)
        .value("kLimitExceeded", ic::OffStatus::kLimitExceeded)
        .value("kBudgetExceeded", ic::OffStatus::kBudgetExceeded)
        .value("kCanceled", ic::OffStatus::kCanceled)
        .value("kInvalidStorage", ic::OffStatus::kInvalidStorage)
        .value("kGeometryFailure", ic::OffStatus::kGeometryFailure);
  }
  if (!nb::type<ic::OffStage>().is_valid()) {
    nb::enum_<ic::OffStage>(module, "OffStage")
        .value("kConfiguration", ic::OffStage::kConfiguration)
        .value("kScan", ic::OffStage::kScan)
        .value("kDecode", ic::OffStage::kDecode)
        .value("kMaterialization", ic::OffStage::kMaterialization)
        .value("kInspection", ic::OffStage::kInspection)
        .value("kValidation", ic::OffStage::kValidation)
        .value("kEncode", ic::OffStage::kEncode)
        .value("kPublication", ic::OffStage::kPublication);
  }
  if (!nb::type<ic::OffDiagnosticCode>().is_valid()) {
    nb::enum_<ic::OffDiagnosticCode>(module, "OffDiagnosticCode")
        .value("kNone", ic::OffDiagnosticCode::kNone)
        .value("kRequiredFieldZero", ic::OffDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               ic::OffDiagnosticCode::kConfigurationExtent)
        .value("kSourceLimit", ic::OffDiagnosticCode::kSourceLimit)
        .value("kVertexLimit", ic::OffDiagnosticCode::kVertexLimit)
        .value("kFaceLimit", ic::OffDiagnosticCode::kFaceLimit)
        .value("kCornerLimit", ic::OffDiagnosticCode::kCornerLimit)
        .value("kPerFaceCornerLimit",
               ic::OffDiagnosticCode::kPerFaceCornerLimit)
        .value("kLineLimit", ic::OffDiagnosticCode::kLineLimit)
        .value("kTokenLimit", ic::OffDiagnosticCode::kTokenLimit)
        .value("kCommentLimit", ic::OffDiagnosticCode::kCommentLimit)
        .value("kOutputLimit", ic::OffDiagnosticCode::kOutputLimit)
        .value("kSyntax", ic::OffDiagnosticCode::kSyntax)
        .value("kIntegerSyntax", ic::OffDiagnosticCode::kIntegerSyntax)
        .value("kIntegerRange", ic::OffDiagnosticCode::kIntegerRange)
        .value("kNumericSyntax", ic::OffDiagnosticCode::kNumericSyntax)
        .value("kNumericRange", ic::OffDiagnosticCode::kNumericRange)
        .value("kNonfinite", ic::OffDiagnosticCode::kNonfinite)
        .value("kVariant", ic::OffDiagnosticCode::kVariant)
        .value("kUnsupportedPayload",
               ic::OffDiagnosticCode::kUnsupportedPayload)
        .value("kTrailingData", ic::OffDiagnosticCode::kTrailingData)
        .value("kVertexRange", ic::OffDiagnosticCode::kVertexRange)
        .value("kFaceTooShort", ic::OffDiagnosticCode::kFaceTooShort)
        .value("kUnsupportedAttribute",
               ic::OffDiagnosticCode::kUnsupportedAttribute)
        .value("kTextPolicy", ic::OffDiagnosticCode::kTextPolicy)
        .value("kEdgeCountPolicy", ic::OffDiagnosticCode::kEdgeCountPolicy)
        .value("kRequestBudget", ic::OffDiagnosticCode::kRequestBudget)
        .value("kStopRequested", ic::OffDiagnosticCode::kStopRequested)
        .value("kGeometry", ic::OffDiagnosticCode::kGeometry)
        .value("kStorageIssues", ic::OffDiagnosticCode::kStorageIssues);
  }
  if (!nb::type<ic::OffTextPolicy>().is_valid()) {
    nb::enum_<ic::OffTextPolicy>(module, "OffTextPolicy")
        .value("kInvalid", ic::OffTextPolicy::kInvalid)
        .value("kCanonicalAscii", ic::OffTextPolicy::kCanonicalAscii);
  }
  if (!nb::type<ic::OffEdgeCountPolicy>().is_valid()) {
    nb::enum_<ic::OffEdgeCountPolicy>(module, "OffEdgeCountPolicy")
        .value("kInvalid", ic::OffEdgeCountPolicy::kInvalid)
        .value("kUnknownZero", ic::OffEdgeCountPolicy::kUnknownZero);
  }
  if (!nb::type<ic::OffCorrespondence>().is_valid()) {
    nb::enum_<ic::OffCorrespondence>(module, "OffCorrespondence")
        .value("kInvalid", ic::OffCorrespondence::kInvalid)
        .value("kIdentity", ic::OffCorrespondence::kIdentity);
  }
  if (!nb::type<ic::OffFileStatus>().is_valid()) {
    nb::enum_<ic::OffFileStatus>(module, "OffFileStatus")
        .value("kSuccess", ic::OffFileStatus::kSuccess)
        .value("kInvalidConfiguration",
               ic::OffFileStatus::kInvalidConfiguration)
        .value("kInvalidRequest", ic::OffFileStatus::kInvalidRequest)
        .value("kUnsupportedProfile", ic::OffFileStatus::kUnsupportedProfile)
        .value("kLimitExceeded", ic::OffFileStatus::kLimitExceeded)
        .value("kBudgetExceeded", ic::OffFileStatus::kBudgetExceeded)
        .value("kForeignBudget", ic::OffFileStatus::kForeignBudget)
        .value("kIoFailure", ic::OffFileStatus::kIoFailure)
        .value("kFileChanged", ic::OffFileStatus::kFileChanged)
        .value("kOffFailure", ic::OffFileStatus::kOffFailure)
        .value("kGeometryFailure", ic::OffFileStatus::kGeometryFailure)
        .value("kPreservationMismatch",
               ic::OffFileStatus::kPreservationMismatch)
        .value("kProviderFailure", ic::OffFileStatus::kProviderFailure)
        .value("kDestinationExists", ic::OffFileStatus::kDestinationExists)
        .value("kPublicationFailed", ic::OffFileStatus::kPublicationFailed)
        .value("kCanceled", ic::OffFileStatus::kCanceled);
  }
  if (!nb::type<ic::OffFileStage>().is_valid()) {
    nb::enum_<ic::OffFileStage>(module, "OffFileStage")
        .value("kConfiguration", ic::OffFileStage::kConfiguration)
        .value("kPathPlanning", ic::OffFileStage::kPathPlanning)
        .value("kCapture", ic::OffFileStage::kCapture)
        .value("kOffRead", ic::OffFileStage::kOffRead)
        .value("kExpectedInspection", ic::OffFileStage::kExpectedInspection)
        .value("kStaging", ic::OffFileStage::kStaging)
        .value("kReload", ic::OffFileStage::kReload)
        .value("kVerification", ic::OffFileStage::kVerification)
        .value("kProvider", ic::OffFileStage::kProvider)
        .value("kInventory", ic::OffFileStage::kInventory)
        .value("kPublication", ic::OffFileStage::kPublication)
        .value("kComplete", ic::OffFileStage::kComplete);
  }
  if (!nb::type<ic::OffFileDiagnosticCode>().is_valid()) {
    nb::enum_<ic::OffFileDiagnosticCode>(module, "OffFileDiagnosticCode")
        .value("kNone", ic::OffFileDiagnosticCode::kNone)
        .value("kRequiredFieldZero",
               ic::OffFileDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               ic::OffFileDiagnosticCode::kConfigurationExtent)
        .value("kCountLimit", ic::OffFileDiagnosticCode::kCountLimit)
        .value("kPathUnsupported", ic::OffFileDiagnosticCode::kPathUnsupported)
        .value("kNameUnsupported", ic::OffFileDiagnosticCode::kNameUnsupported)
        .value("kNameCollision", ic::OffFileDiagnosticCode::kNameCollision)
        .value("kNotRegular", ic::OffFileDiagnosticCode::kNotRegular)
        .value("kOpen", ic::OffFileDiagnosticCode::kOpen)
        .value("kRead", ic::OffFileDiagnosticCode::kRead)
        .value("kWrite", ic::OffFileDiagnosticCode::kWrite)
        .value("kClose", ic::OffFileDiagnosticCode::kClose)
        .value("kChanged", ic::OffFileDiagnosticCode::kChanged)
        .value("kRequestBudget", ic::OffFileDiagnosticCode::kRequestBudget)
        .value("kSharedBudget", ic::OffFileDiagnosticCode::kSharedBudget)
        .value("kStopRequested", ic::OffFileDiagnosticCode::kStopRequested)
        .value("kLedgerMismatch", ic::OffFileDiagnosticCode::kLedgerMismatch)
        .value("kDelegatedOff", ic::OffFileDiagnosticCode::kDelegatedOff)
        .value("kDelegatedInspection",
               ic::OffFileDiagnosticCode::kDelegatedInspection)
        .value("kAttributeUnsupported",
               ic::OffFileDiagnosticCode::kAttributeUnsupported)
        .value("kOutputFacts", ic::OffFileDiagnosticCode::kOutputFacts)
        .value("kByteMismatch", ic::OffFileDiagnosticCode::kByteMismatch)
        .value("kMeshMismatch", ic::OffFileDiagnosticCode::kMeshMismatch)
        .value("kProvider", ic::OffFileDiagnosticCode::kProvider)
        .value("kStagedInventory", ic::OffFileDiagnosticCode::kStagedInventory)
        .value("kDestinationExists",
               ic::OffFileDiagnosticCode::kDestinationExists)
        .value("kCommit", ic::OffFileDiagnosticCode::kCommit)
        .value("kCommitUnsupported",
               ic::OffFileDiagnosticCode::kCommitUnsupported);
  }
  if (!nb::type<ic::OffFilePublicationOutcome>().is_valid()) {
    nb::enum_<ic::OffFilePublicationOutcome>(module,
                                             "OffFilePublicationOutcome")
        .value("kFailed", ic::OffFilePublicationOutcome::kFailed)
        .value("kCanceled", ic::OffFilePublicationOutcome::kCanceled)
        .value("kPublished", ic::OffFilePublicationOutcome::kPublished);
  }
  if (!nb::type<ic::OffFileCleanupOutcome>().is_valid()) {
    nb::enum_<ic::OffFileCleanupOutcome>(module, "OffFileCleanupOutcome")
        .value("kNotRequired", ic::OffFileCleanupOutcome::kNotRequired)
        .value("kRemoved", ic::OffFileCleanupOutcome::kRemoved)
        .value("kFailed", ic::OffFileCleanupOutcome::kFailed);
  }
  {
    auto type = nb::class_<OffObservation<OffWorkflowDiagnostic>>(
        module, "OffWorkflowDiagnostic");
    Property(type, "stage", &OffWorkflowDiagnostic::stage);
    Property(type, "code", &OffWorkflowDiagnostic::code);
    Property(type, "element", &OffWorkflowDiagnostic::element);
    Property(type, "delegated_status",
             &OffWorkflowDiagnostic::delegated_status);
    Property(type, "delegated", &OffWorkflowDiagnostic::delegated);
  }
  {
    auto type = nb::class_<OffObservation<OffWorkflowOutcomes>>(
        module, "OffWorkflowOutcomes");
    Property(type, "execution", &OffWorkflowOutcomes::execution);
    Property(type, "candidate", &OffWorkflowOutcomes::candidate);
    Property(type, "profile", &OffWorkflowOutcomes::profile);
    Property(type, "publication", &OffWorkflowOutcomes::publication);
    Property(type, "stage", &OffWorkflowOutcomes::stage);
    Property(type, "exit", &OffWorkflowOutcomes::exit);
  }
  {
    auto type = nb::class_<OffObservation<OffWorkflowCoverage>>(
        module, "OffWorkflowCoverage");
    Property(type, "check", &OffWorkflowCoverage::check);
    Property(type, "state", &OffWorkflowCoverage::state);
    Property(type, "passed", &OffWorkflowCoverage::passed);
    Property(type, "reason", &OffWorkflowCoverage::reason);
  }
  {
    auto type = nb::class_<OffObservation<OffWorkflowExecution>>(
        module, "OffWorkflowExecution");
    Property(type, "repair", &OffWorkflowExecution::repair);
    Property(type, "writing", &OffWorkflowExecution::writing);
    Property(type, "reload", &OffWorkflowExecution::reload);
    Property(type, "source_protection",
             &OffWorkflowExecution::source_protection);
    Property(type, "repair_correspondence",
             &OffWorkflowExecution::repair_correspondence);
    Property(type, "canonical_preservation",
             &OffWorkflowExecution::canonical_preservation);
    Property(type, "reload_verification",
             &OffWorkflowExecution::reload_verification);
    Property(type, "source_verification",
             &OffWorkflowExecution::source_verification);
    Property(type, "staged_candidate", &OffWorkflowExecution::staged_candidate);
    Property(type, "hashing", &OffWorkflowExecution::hashing);
    Property(type, "report_copy", &OffWorkflowExecution::report_copy);
  }
  {
    auto type = nb::class_<OffObservation<OffWorkflowRequestedBytes>>(
        module, "OffWorkflowRequestedBytes");
    Property(type, "wrapper", &OffWorkflowRequestedBytes::wrapper);
    Property(type, "source_files", &OffWorkflowRequestedBytes::source_files);
    Property(type, "source_off", &OffWorkflowRequestedBytes::source_off);
    Property(type, "repair", &OffWorkflowRequestedBytes::repair);
    Property(type, "writing", &OffWorkflowRequestedBytes::writing);
    Property(type, "reload", &OffWorkflowRequestedBytes::reload);
    Property(type, "publication_files",
             &OffWorkflowRequestedBytes::publication_files);
    Property(type, "publication_off",
             &OffWorkflowRequestedBytes::publication_off);
  }
  {
    auto type = nb::class_<OffObservation<ic::OffFileDiagnostic>>(
        module, "OffFileDiagnostic");
    Property(type, "status", &ic::OffFileDiagnostic::status);
    Property(type, "stage", &ic::OffFileDiagnostic::stage);
    Property(type, "code", &ic::OffFileDiagnostic::code);
    Property(type, "file_index", &ic::OffFileDiagnostic::file_index);
    Property(type, "byte_offset", &ic::OffFileDiagnostic::byte_offset);
    Property(type, "system_error", &ic::OffFileDiagnostic::system_error);
    Property(type, "off_status", &ic::OffFileDiagnostic::off_status);
    Property(type, "off_diagnostic", &ic::OffFileDiagnostic::off_diagnostic);
    Property(type, "geometry_status", &ic::OffFileDiagnostic::geometry_status);
    Property(type, "geometry_diagnostic",
             &ic::OffFileDiagnostic::geometry_diagnostic);
  }
  {
    auto type = nb::class_<OffObservation<ic::OffFileExecution>>(
        module, "OffFileExecution");
    Property(type, "capture", &ic::OffFileExecution::capture);
    Property(type, "copying", &ic::OffFileExecution::copying);
    Property(type, "byte_verification",
             &ic::OffFileExecution::byte_verification);
    Property(type, "mesh_verification",
             &ic::OffFileExecution::mesh_verification);
    Property(type, "expected_inspection",
             &ic::OffFileExecution::expected_inspection);
    Property(type, "off", &ic::OffFileExecution::off);
  }
  {
    auto type = nb::class_<OffObservation<ic::OffFilePublicationResult>>(
        module, "OffFilePublicationObservation");
    Property(type, "outcome", &ic::OffFilePublicationResult::outcome);
    Property(type, "phase", &ic::OffFilePublicationResult::phase);
    Property(type, "diagnostic", &ic::OffFilePublicationResult::diagnostic);
    Property(type, "cleanup", &ic::OffFilePublicationResult::cleanup);
    Property(type, "cleanup_error",
             &ic::OffFilePublicationResult::cleanup_error);
    Property(type, "execution", &ic::OffFilePublicationResult::execution);
  }
  {
    auto type =
        nb::class_<OffObservation<ic::OffDiagnostic>>(module, "OffDiagnostic");
    Property(type, "stage", &ic::OffDiagnostic::stage);
    Property(type, "code", &ic::OffDiagnostic::code);
    Property(type, "byte_offset", &ic::OffDiagnostic::byte_offset);
    Property(type, "vertex", &ic::OffDiagnostic::vertex);
    Property(type, "face", &ic::OffDiagnostic::face);
    Property(type, "geometry_status", &ic::OffDiagnostic::geometry_status);
    Property(type, "geometry_diagnostic",
             &ic::OffDiagnostic::geometry_diagnostic);
    Property(type, "storage_issue", &ic::OffDiagnostic::storage_issue);
  }
  {
    auto type = nb::class_<OffObservation<ic::OffExecution>>(
        module, "OffCodecExecution");
    Property(type, "codec", &ic::OffExecution::codec);
    Property(type, "materialization", &ic::OffExecution::materialization);
    Property(type, "inspection", &ic::OffExecution::inspection);
  }
  {
    auto type = nb::class_<OffObservation<ic::OffRequestedBytes>>(
        module, "OffRequestedBytes");
    Property(type, "codec", &ic::OffRequestedBytes::codec);
    Property(type, "materialization", &ic::OffRequestedBytes::materialization);
    Property(type, "inspection", &ic::OffRequestedBytes::inspection);
    Property(type, "total", &ic::OffRequestedBytes::total);
  }
  {
    auto type =
        nb::class_<OffObservation<ic::OffWriteFacts>>(module, "OffWriteFacts");
    Property(type, "profile", &ic::OffWriteFacts::profile);
    Property(type, "vertex_count", &ic::OffWriteFacts::vertex_count);
    Property(type, "face_count", &ic::OffWriteFacts::face_count);
    Property(type, "corner_count", &ic::OffWriteFacts::corner_count);
    Property(type, "text_policy", &ic::OffWriteFacts::text_policy);
    Property(type, "edge_count_policy", &ic::OffWriteFacts::edge_count_policy);
    Property(type, "emitted_edge_count",
             &ic::OffWriteFacts::emitted_edge_count);
    Property(type, "correspondence", &ic::OffWriteFacts::correspondence);
    Property(type, "source_declared_edge_count",
             &ic::OffWriteFacts::source_declared_edge_count);
  }
  {
    auto type = nb::class_<OffObservation<MaterializedDuplicateDiagnostic>>(
        module, "OffMaterializedDuplicateDiagnostic");
    Property(type, "stage", &MaterializedDuplicateDiagnostic::stage);
    Property(type, "code", &MaterializedDuplicateDiagnostic::code);
    Property(type, "target", &MaterializedDuplicateDiagnostic::target);
    Property(type, "attribute", &MaterializedDuplicateDiagnostic::attribute);
    Property(type, "element", &MaterializedDuplicateDiagnostic::element);
    Property(type, "geometry_status",
             &MaterializedDuplicateDiagnostic::geometry_status);
    Property(type, "geometry_diagnostic",
             &MaterializedDuplicateDiagnostic::geometry_diagnostic);
    Property(type, "storage_issue",
             &MaterializedDuplicateDiagnostic::storage_issue);
  }
  {
    auto type = nb::class_<OffObservation<MaterializedDuplicateExecution>>(
        module, "OffMaterializedDuplicateExecution");
    Property(type, "source_inspection",
             &MaterializedDuplicateExecution::source_inspection);
    Property(type, "eligibility", &MaterializedDuplicateExecution::eligibility);
    Property(type, "comparison", &MaterializedDuplicateExecution::comparison);
    Property(type, "selection", &MaterializedDuplicateExecution::selection);
    Property(type, "correspondence",
             &MaterializedDuplicateExecution::correspondence);
    Property(type, "candidate_inspection",
             &MaterializedDuplicateExecution::candidate_inspection);
    Property(type, "verification",
             &MaterializedDuplicateExecution::verification);
    Property(type, "directed_verification",
             &MaterializedDuplicateExecution::directed_verification);
  }
  {
    auto type = nb::class_<OffObservation<MaterializedDuplicateRequestedBytes>>(
        module, "OffMaterializedDuplicateRequestedBytes");
    Property(type, "repair", &MaterializedDuplicateRequestedBytes::repair);
    Property(type, "source_inspection",
             &MaterializedDuplicateRequestedBytes::source_inspection);
    Property(type, "selection",
             &MaterializedDuplicateRequestedBytes::selection);
    Property(type, "candidate_inspection",
             &MaterializedDuplicateRequestedBytes::candidate_inspection);
    Property(type, "total", &MaterializedDuplicateRequestedBytes::total);
  }
  {
    auto type = nb::class_<OffObservation<geo::MaterializationDiagnostic>>(
        module, "OffMaterializationDiagnostic");
    Property(type, "code", &geo::MaterializationDiagnostic::code);
    Property(type, "attribute", &geo::MaterializationDiagnostic::attribute);
    Property(type, "element", &geo::MaterializationDiagnostic::element);
    Property(type, "storage_issue",
             &geo::MaterializationDiagnostic::storage_issue);
  }
  {
    auto type = nb::class_<OffObservation<geo::StorageIssue>>(
        module, "OffStorageIssue");
    Property(type, "code", &geo::StorageIssue::code);
    Property(type, "attribute", &geo::StorageIssue::attribute);
    Property(type, "element", &geo::StorageIssue::element);
  }
  {
    auto type = nb::class_<OffObservation<geo::MaterializationExecutionStats>>(
        module, "OffMaterializationExecutionStats");
    Property(type, "planned_jobs",
             &geo::MaterializationExecutionStats::planned_jobs);
    Property(type, "launched_workers",
             &geo::MaterializationExecutionStats::launched_workers);
    Property(type, "joined_workers",
             &geo::MaterializationExecutionStats::joined_workers);
    Property(type, "caller_jobs",
             &geo::MaterializationExecutionStats::caller_jobs);
  }
  {
    auto type = nb::class_<OffObservation<CapturedRequestedBytes>>(
        module, "OffCapturedRequestedBytes");
    Property(type, "files", &CapturedRequestedBytes::files);
    Property(type, "off", &CapturedRequestedBytes::off);
  }
  nb::class_<OffObservation<std::error_code>>(module, "OffSystemErrorCode")
      .def_prop_ro("category",
                   [](const OffObservation<std::error_code>& self) {
                     return nb::str(self.value.category().name());
                   })
      .def_prop_ro("value", [](const OffObservation<std::error_code>& self) {
        return self.value.value();
      });
  nb::class_<OffExecution>(module, "OffExecution")
      .def("__init__", &InitExecution, nb::kw_only(),
           nb::arg("preset") = kBalancedOffPreset)
      .def_prop_ro(
          "preset",
          [](const OffExecution& self) { return nb::str(self.Name().c_str()); })
      .def_prop_ro("resolved_preset",
                   [](const OffExecution& self) {
                     return PresetObservation(self.Preset());
                   })
      .def_prop_ro("worker_budget",
                   [](const OffExecution& self) {
                     return self.Context().WorkerBudget();
                   })
      .def_prop_ro("active_workers",
                   [](const OffExecution& self) {
                     return self.Context().ActiveWorkers();
                   })
      .def_prop_ro("active_tracked_payload", [](const OffExecution& self) {
        return self.Context().ActiveTrackedPayload();
      });
  nb::class_<OffCancellation>(module, "OffCancellation")
      .def(nb::init<>())
      .def("cancel", &OffCancellation::Cancel)
      .def_prop_ro("cancelled", &OffCancellation::Cancelled);
  auto result =
      nb::class_<OffFileWorkflowResult>(module, "OffFileWorkflowResult");
  result.def_prop_ro("workflow_started", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started;
  });
  result.def_prop_ro("capture_status", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return Observe(self.capture.status);
  });
  result.def_prop_ro("capture_diagnostic",
                     [](const OffFileWorkflowResult& self) {
                       self.RequireComplete();
                       return Observe(self.capture.diagnostic);
                     });
  result.def_prop_ro("capture_execution",
                     [](const OffFileWorkflowResult& self) {
                       self.RequireComplete();
                       return Observe(self.capture.execution);
                     });
  result.def_prop_ro(
      "capture_requested_bytes", [](const OffFileWorkflowResult& self) {
        self.RequireComplete();
        return self.capture.asset
                   ? Observe(CapturedRequestedBytes{
                         self.capture.asset->NewRequestedBytes(),
                         self.capture.asset->Document().NewRequestedBytes()})
                   : nb::none();
      });
  result.def_prop_ro("status", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.status) : nb::none();
  });
  result.def_prop_ro("outcomes", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.outcomes) : nb::none();
  });
  result.def_prop_ro("diagnostic", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.diagnostic)
                                 : nb::none();
  });
  result.def_prop_ro(
      "report_diagnostic", [](const OffFileWorkflowResult& self) {
        self.RequireComplete();
        return self.workflow_started ? Observe(self.workflow.report_diagnostic)
                                     : nb::none();
      });
  result.def_prop_ro("execution", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.execution)
                                 : nb::none();
  });
  result.def_prop_ro("publication", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.publication)
                                 : nb::none();
  });
  result.def_prop_ro("requested_bytes", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow.record
               ? Observe(self.workflow.record->NewRequestedBytes())
               : nb::none();
  });
  result.def_prop_ro(
      "coverage", [](const OffFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record) return nb::none();
        const auto coverage = self.workflow.record->Coverage();
        nb::tuple output = nb::steal<nb::tuple>(
            PyTuple_New(static_cast<Py_ssize_t>(coverage.size())));
        if (!output.is_valid()) throw nb::python_error();
        for (std::size_t i = 0; i < coverage.size(); ++i)
          PyTuple_SET_ITEM(output.ptr(), static_cast<Py_ssize_t>(i),
                           Observe(coverage[i]).release().ptr());
        return output;
      });
  result.def_prop_ro("source_profile", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.capture.asset
               ? Observe(self.capture.asset->Document().Profile())
               : nb::none();
  });
  result.def_prop_ro("source_entry_path",
                     [](const OffFileWorkflowResult& self) -> nb::object {
                       self.RequireComplete();
                       if (!self.capture.asset) return nb::none();
                       const auto path = self.capture.asset->EntryPath();
                       return nb::str(path.data(), path.size());
                     });
  result.def_prop_ro(
      "published_entry_path",
      [](const OffFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record || !self.workflow.record->Published())
          return nb::none();
        const auto path = self.workflow.record->Published()->EntryPath();
        return nb::str(path.data(), path.size());
      });
  result.def_prop_ro(
      "published_report_path",
      [](const OffFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record) return nb::none();
        const auto path = self.workflow.record->PublishedReportPath();
        return path ? nb::object(nb::str(path->data(), path->size()))
                    : nb::none();
      });
  result.def_prop_ro(
      "input_sha256", [](const OffFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record) return nb::none();
        const auto hash = self.workflow.record->InputSha256();
        return hash ? nb::object(nb::str(hash->data(), hash->size()))
                    : nb::none();
      });
  result.def_prop_ro(
      "report_json", [](const OffFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record) return nb::none();
        const auto bytes = self.workflow.record->ReportJson();
        return bytes.empty()
                   ? nb::none()
                   : nb::object(nb::bytes(bytes.data(), bytes.size()));
      });
  result.def_prop_ro(
      "output_bytes", [](const OffFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record || !self.workflow.record->Output())
          return nb::none();
        const auto bytes = self.workflow.record->Output()->Bytes();
        return nb::bytes(bytes.data(), bytes.size());
      });
  result.def_prop_ro("write_facts", [](const OffFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow.record && self.workflow.record->Output()
               ? Observe(self.workflow.record->Output()->Facts())
               : nb::none();
  });
  result.def_prop_ro(
      "input_snapshot_counts", [](const OffFileWorkflowResult& self) {
        return SnapshotCounts(self, &MaterializedDuplicateRepair::Source);
      });
  result.def_prop_ro(
      "candidate_snapshot_counts", [](const OffFileWorkflowResult& self) {
        return SnapshotCounts(self, &MaterializedDuplicateRepair::Candidate);
      });
  result.def_prop_ro("face_map", [](const OffFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::FaceMap);
  });
  result.def_prop_ro("corner_map", [](const OffFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::CornerMap);
  });
  result.def_prop_ro("source_faces", [](const OffFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::SourceFaces);
  });
  result.def_prop_ro("source_corners", [](const OffFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::SourceCorners);
  });
  module.def("_prepare_off_file", &Prepare, nb::arg("source"),
             nb::arg("destination").none(), nb::arg("targets"), nb::kw_only(),
             nb::arg("text"), nb::arg("edge_count"), nb::arg("execution"),
             nb::arg("input_id") = kDefaultOffInputId,
             nb::arg("report_path") = kDefaultOffReportPath,
             nb::arg("cancellation") = nb::none());
  // Borrowed preallocated handle; no post-native conversion or keepalive owner.
  module.def("_run_off_file", &Run);
}
}  // namespace meshvale::repair::python
