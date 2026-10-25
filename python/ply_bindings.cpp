// SPDX-License-Identifier: Apache-2.0
#include "ply_bindings.h"

#include <Python.h>
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/ply.h>
#include <meshvale/interchange/ply_files.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <meshvale/repair/ply_workflow.h>
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

#include "ply_presets.h"

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
constexpr Py_ssize_t kTargetPairSize = 2;
constexpr Py_ssize_t kDeclarationFieldCount = 2;
constexpr std::string_view kDeclarationDomain = "domain";
constexpr std::string_view kDeclarationName = "name";
constexpr std::string_view kVertexDomainName = "vertex";
constexpr std::string_view kFaceDomainName = "face";
constexpr std::string_view kCornerDomainName = "corner";
constexpr std::string_view kAsciiEncodingName = "ascii";
constexpr std::string_view kLittleEndianEncodingName = "binary_little_endian";
constexpr std::string_view kBigEndianEncodingName = "binary_big_endian";
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
struct PlyObservation {
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
    return nb::cast(PlyObservation<T>{value});
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
  return nb::cast(PlyObservation<std::error_code>{value});
}

template <class T, class Member>
void Property(nb::class_<PlyObservation<T>>& type, const char* name,
              Member T::* member) {
  type.def_prop_ro(name, [member](const PlyObservation<T>& self) {
    return Observe(self.value.*member);
  });
}
class PlyExecution {
 public:
  explicit PlyExecution(std::string_view name)
      : name_(name),
        preset_(MakePlyPreset(name)),
        context_(preset_.execution) {}
  const std::string& Name() const noexcept { return name_; }
  const PlyPreset& Preset() const noexcept { return preset_; }
  const geo::ExecutionContext& Context() const noexcept { return context_; }

 private:
  std::string name_;
  PlyPreset preset_;
  geo::ExecutionContext context_;
};
class PlyCancellation {
 public:
  bool Cancel() noexcept { return source_.request_stop(); }
  bool Cancelled() const noexcept { return source_.stop_requested(); }
  std::stop_source Source() const noexcept { return source_; }

 private:
  std::stop_source source_;
};
enum class InvocationState { kPrepared, kRunning, kComplete, kFailed };
struct OwnedDeclaration {
  geo::AttributeDomain domain;
  std::string name;
};
struct CapturedRequestedBytes {
  std::uint64_t files;
  ic::PlyRequestedBytes ply;
};
struct PlyFileWorkflowResult {
  explicit PlyFileWorkflowResult(const PlyExecution& session,
                                 std::stop_source cancellation)
      : preset(session.Preset()),
        context(session.Context()),
        stop(std::move(cancellation)) {}
  PlyPreset preset;
  geo::ExecutionContext context;
  std::stop_source stop;
  std::filesystem::path source;
  std::optional<std::filesystem::path> destination;
  std::string input_id;
  std::string report_path;
  std::vector<DuplicateTarget> targets;
  ic::PlyWriteOptions writing;
  std::vector<OwnedDeclaration> declarations;
  // Built only after all owned names are final; no later mutation through
  // joins.
  std::vector<RowLocalAttributeDeclaration> declaration_views;
  ic::PlyFileReadResult capture;
  PlyDuplicateFileWorkflowResult workflow;
  InvocationState state = InvocationState::kPrepared;
  bool workflow_started = false;
  void RequireComplete() const {
    if (state != InvocationState::kComplete)
      throw nb::value_error("PLY invocation has not completed");
  }
  const MaterializedDuplicateRepair* Repair() const noexcept {
    return workflow_started && workflow.record ? workflow.record->RepairFacts()
                                               : nullptr;
  }
};
static_assert(std::is_nothrow_move_constructible_v<ic::PlyFileReadResult>);
static_assert(std::is_nothrow_move_assignable_v<ic::PlyFileReadResult>);
static_assert(
    std::is_nothrow_move_constructible_v<PlyDuplicateFileWorkflowResult>);
static_assert(
    std::is_nothrow_move_assignable_v<PlyDuplicateFileWorkflowResult>);
static_assert(std::is_nothrow_move_constructible_v<PlyFileWorkflowResult>);
static_assert(std::is_nothrow_move_assignable_v<PlyFileWorkflowResult>);
std::uint64_t Index(nb::handle value) {
  if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
    throw nb::type_error("indices must be integers, not booleans or None");
  const auto result = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  return static_cast<std::uint64_t>(result);
}
std::pair<nb::handle, nb::handle> Pair(nb::handle value) {
  if (PyTuple_Check(value.ptr()) &&
      PyTuple_GET_SIZE(value.ptr()) == kTargetPairSize)
    return {PyTuple_GET_ITEM(value.ptr(), 0), PyTuple_GET_ITEM(value.ptr(), 1)};
  if (PyList_Check(value.ptr()) &&
      PyList_GET_SIZE(value.ptr()) == kTargetPairSize)
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
  if (static_cast<std::uint64_t>(utf8_length) > kPlyFrontendPathUtf8Bytes)
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
void InitExecution(PlyExecution* self, nb::handle name) {
  constexpr auto kPresetNameBytes =
      std::max(kBalancedPlyPreset.size(), kSerialPlyPreset.size());
  const auto owned = Text(name, kPresetNameBytes);
  new (self) PlyExecution(owned);
}
void CheckDeclarationName(std::string_view name) {
  if (name.empty() ||
      !(name.front() == '_' || (name.front() >= 'a' && name.front() <= 'z') ||
        (name.front() >= 'A' && name.front() <= 'Z')))
    throw nb::value_error("row-local name must be an ASCII identifier");
  for (char c : name)
    if (!AsciiAlphaNumeric(c) && c != '_')
      throw nb::value_error("row-local name must be an ASCII identifier");
}
ic::PlyEncoding Encoding(nb::handle value) {
  constexpr auto kEncodingNameBytes =
      std::max({kAsciiEncodingName.size(), kLittleEndianEncodingName.size(),
                kBigEndianEncodingName.size()});
  const auto text = Text(value, kEncodingNameBytes);
  if (text == kAsciiEncodingName) return ic::PlyEncoding::kAscii;
  if (text == kLittleEndianEncodingName)
    return ic::PlyEncoding::kBinaryLittleEndian;
  if (text == kBigEndianEncodingName) return ic::PlyEncoding::kBinaryBigEndian;
  throw nb::value_error(
      "encoding must be ascii, binary_little_endian or binary_big_endian");
}
void Declarations(PlyFileWorkflowResult& result, nb::handle values) {
  const auto& limits = result.preset.workflow_limits.repair;
  std::uint64_t name_bytes = 0;
  BoundedItems(values, limits.max_row_local_declarations, [&](nb::handle item) {
    if (!PyDict_Check(item.ptr()))
      throw nb::type_error(
          "row-local declarations must be domain/name dictionaries");
    if (PyDict_Size(item.ptr()) != kDeclarationFieldCount)
      throw nb::value_error(
          "row-local declaration requires exactly domain and name");
    nb::object domain;
    nb::object name;
    Py_ssize_t position = 0;
    PyObject* key = nullptr;
    PyObject* value = nullptr;
    while (PyDict_Next(item.ptr(), &position, &key, &value)) {
      if (!PyUnicode_Check(key))
        throw nb::type_error(
            "row-local declaration keys must be Unicode strings");
      const auto length = PyUnicode_GET_LENGTH(key);
      if (length == static_cast<Py_ssize_t>(kDeclarationDomain.size()) &&
          PyUnicode_CompareWithASCIIString(key, kDeclarationDomain.data()) == 0)
        domain = nb::borrow(nb::handle(value));
      else if (length == static_cast<Py_ssize_t>(kDeclarationName.size()) &&
               PyUnicode_CompareWithASCIIString(key, kDeclarationName.data()) ==
                   0)
        name = nb::borrow(nb::handle(value));
      else
        throw nb::value_error(
            "row-local declaration requires exactly domain and name");
    }
    if (!domain.is_valid() || !name.is_valid())
      throw nb::value_error(
          "row-local declaration requires exactly domain and name");
    constexpr auto kDomainNameBytes =
        std::max({kVertexDomainName.size(), kFaceDomainName.size(),
                  kCornerDomainName.size()});
    const auto text = Text(domain, kDomainNameBytes);
    geo::AttributeDomain kind;
    if (text == kVertexDomainName)
      kind = geo::AttributeDomain::vertex;
    else if (text == kFaceDomainName)
      kind = geo::AttributeDomain::face;
    else if (text == kCornerDomainName)
      kind = geo::AttributeDomain::corner;
    else
      throw nb::value_error("row-local domain must be vertex, face or corner");
    auto owned_name =
        Text(name, limits.max_declaration_name_bytes - name_bytes);
    CheckDeclarationName(owned_name);
    for (const auto& previous : result.declarations)
      if (previous.domain == kind && previous.name == owned_name)
        throw nb::value_error("duplicate row-local domain/name declaration");
    name_bytes += owned_name.size();
    result.declarations.push_back({kind, std::move(owned_name)});
  });
  result.declaration_views.reserve(result.declarations.size());
  for (const auto& value : result.declarations)
    result.declaration_views.push_back({value.domain, value.name});
}
nb::object Prepare(nb::handle source, nb::handle destination,
                   nb::handle targets, nb::handle encoding,
                   nb::handle row_local_attributes, nb::handle execution,
                   nb::handle input_id, nb::handle report_path,
                   nb::handle cancellation) {
  if (!nb::isinstance<PlyExecution>(execution))
    throw nb::type_error("execution must be a resolved PlyExecution session");
  const auto& session = nb::cast<const PlyExecution&>(execution);
  std::stop_source stop;
  if (!cancellation.is_none()) {
    if (!nb::isinstance<PlyCancellation>(cancellation))
      throw nb::type_error("cancellation must be a PlyCancellation");
    stop = nb::cast<const PlyCancellation&>(cancellation).Source();
  }
  nb::object object = nb::inst_alloc(nb::type<PlyFileWorkflowResult>());
  auto* result = new (nb::inst_ptr<PlyFileWorkflowResult>(object))
      PlyFileWorkflowResult(session, std::move(stop));
  nb::inst_mark_ready(object);
  const auto& limits = result->preset;
  const auto path_limit =
      limits.capture_limits.files.max_native_path_code_units;
  result->source = Path(source, path_limit);
  if (!destination.is_none())
    result->destination = Path(destination, path_limit);
  result->writing = {Encoding(encoding), ic::PlyRewritePolicy::kCanonical};
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
  Declarations(*result, row_local_attributes);
  Isolate(result->source, result->destination);
  return object;
}
nb::object Run(nb::handle prepared) {
  if (!nb::isinstance<PlyFileWorkflowResult>(prepared))
    throw nb::type_error("prepared PLY result required");
  auto* result = nb::inst_ptr<PlyFileWorkflowResult>(prepared);
  if (result->state != InvocationState::kPrepared)
    throw nb::value_error("prepared PLY invocation is single-use");
  nb::object returned = nb::borrow(prepared);
  result->state = InvocationState::kRunning;
  try {
    {
      nb::gil_scoped_release release;
      result->capture =
          ic::ReadPlyFileAsset(result->source, result->preset.capture_limits,
                               result->preset.capture_work, result->context,
                               result->stop.get_token());
      if (result->capture.asset) {
        std::optional<PlyWorkflowPublication> publication;
        if (result->destination)
          publication.emplace(*result->destination, result->report_path);
        const PlyDuplicateFileWorkflowRequest request{
            *result->capture.asset, result->input_id,          result->targets,
            result->writing,        result->declaration_views, publication};
        result->workflow_started = true;
        auto native = RunPlyDuplicateFileWorkflow(
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
nb::object Map(const PlyFileWorkflowResult& result,
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
    const PlyFileWorkflowResult& result,
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
constexpr std::string_view kCancellationBlockValues =
    "cancellation_block_values";
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
constexpr std::string_view kMaxHeaderBytes = "max_header_bytes";
constexpr std::string_view kMaxHeaderLines = "max_header_lines";
constexpr std::string_view kMaxInputIdBytes = "max_input_id_bytes";
constexpr std::string_view kMaxIssues = "max_issues";
constexpr std::string_view kMaxJobs = "max_jobs";
constexpr std::string_view kMaxLineBytes = "max_line_bytes";
constexpr std::string_view kMaxListItemsPerRow = "max_list_items_per_row";
constexpr std::string_view kMaxListRows = "max_list_rows";
constexpr std::string_view kMaxMetadataEntries = "max_metadata_entries";
constexpr std::string_view kMaxNameBytes = "max_name_bytes";
constexpr std::string_view kMaxNativePathCodeUnits =
    "max_native_path_code_units";
constexpr std::string_view kMaxNewRequestedBytes = "max_new_requested_bytes";
constexpr std::string_view kMaxOutputBytes = "max_output_bytes";
constexpr std::string_view kMaxPresenceBytes = "max_presence_bytes";
constexpr std::string_view kMaxProperties = "max_properties";
constexpr std::string_view kMaxPropertyValues = "max_property_values";
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
constexpr std::string_view kMaxTotalNameBytes = "max_total_name_bytes";
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
constexpr std::string_view kParallelMinBytes = "parallel_min_bytes";
constexpr std::string_view kPartitionBytes = "partition_bytes";
constexpr std::string_view kPly = "ply";
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
nb::dict PresetObservation(const ic::PlyFileReadLimits& value);
nb::dict PresetObservation(const ic::PlyFileReadWorkPolicy& value);
nb::dict PresetObservation(const PlyDuplicateFileWorkflowLimits& value);
nb::dict PresetObservation(const PlyDuplicateFileWorkflowWorkPolicy& value);
nb::dict PresetObservation(const ic::PlyFileLimits& value);
nb::dict PresetObservation(const ic::PlyReadLimits& value);
nb::dict PresetObservation(const ic::PlyFileWorkPolicy& value);
nb::dict PresetObservation(const ic::PlyWorkPolicy& value);
nb::dict PresetObservation(const MaterializedDuplicateLimits& value);
nb::dict PresetObservation(const ic::PlyWriteLimits& value);
nb::dict PresetObservation(const ic::PlyFilePublicationLimits& value);
nb::dict PresetObservation(const geo::MeshMaterializationWorkPolicy& value);
nb::dict PresetObservation(const PlyByteWorkPolicy& value);
nb::dict PresetObservation(const ic::PlyFilePublicationWorkPolicy& value);
nb::dict PresetObservation(const geo::MeshMaterializationLimits& value);
nb::dict PresetObservation(const geo::MeshInspectionLimits& value);
nb::dict PresetObservation(const PlyPreset& value);
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
nb::dict PresetObservation(const ic::PlyFileReadLimits& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kPly.data()] = PresetValue(value.ply);
  return result;
}
nb::dict PresetObservation(const ic::PlyFileReadWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kPly.data()] = PresetValue(value.ply);
  return result;
}
nb::dict PresetObservation(const PlyDuplicateFileWorkflowLimits& value) {
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
nb::dict PresetObservation(const PlyDuplicateFileWorkflowWorkPolicy& value) {
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
nb::dict PresetObservation(const ic::PlyFileLimits& value) {
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
nb::dict PresetObservation(const ic::PlyReadLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxSourceBytes.data()] =
      PresetValue(value.max_source_bytes);
  result[preset_keys::kMaxHeaderBytes.data()] =
      PresetValue(value.max_header_bytes);
  result[preset_keys::kMaxHeaderLines.data()] =
      PresetValue(value.max_header_lines);
  result[preset_keys::kMaxLineBytes.data()] = PresetValue(value.max_line_bytes);
  result[preset_keys::kMaxTokenBytes.data()] =
      PresetValue(value.max_token_bytes);
  result[preset_keys::kMaxCommentBytes.data()] =
      PresetValue(value.max_comment_bytes);
  result[preset_keys::kMaxNameBytes.data()] = PresetValue(value.max_name_bytes);
  result[preset_keys::kMaxTotalNameBytes.data()] =
      PresetValue(value.max_total_name_bytes);
  result[preset_keys::kMaxProperties.data()] =
      PresetValue(value.max_properties);
  result[preset_keys::kMaxVertices.data()] = PresetValue(value.max_vertices);
  result[preset_keys::kMaxFaces.data()] = PresetValue(value.max_faces);
  result[preset_keys::kMaxCorners.data()] = PresetValue(value.max_corners);
  result[preset_keys::kMaxCornersPerFace.data()] =
      PresetValue(value.max_corners_per_face);
  result[preset_keys::kMaxListRows.data()] = PresetValue(value.max_list_rows);
  result[preset_keys::kMaxListItemsPerRow.data()] =
      PresetValue(value.max_list_items_per_row);
  result[preset_keys::kMaxPropertyValues.data()] =
      PresetValue(value.max_property_values);
  result[preset_keys::kMaxCodecNewRequestedBytes.data()] =
      PresetValue(value.max_codec_new_requested_bytes);
  result[preset_keys::kMaxTotalNewRequestedBytes.data()] =
      PresetValue(value.max_total_new_requested_bytes);
  result[preset_keys::kMaterialization.data()] =
      PresetValue(value.materialization);
  result[preset_keys::kInspection.data()] = PresetValue(value.inspection);
  return result;
}
nb::dict PresetObservation(const ic::PlyFileWorkPolicy& value) {
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
nb::dict PresetObservation(const ic::PlyWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kCancellationBlockBytes.data()] =
      PresetValue(value.cancellation_block_bytes);
  result[preset_keys::kCancellationBlockValues.data()] =
      PresetValue(value.cancellation_block_values);
  result[preset_keys::kCancellationBlockRecords.data()] =
      PresetValue(value.cancellation_block_records);
  result[preset_keys::kGeometry.data()] = PresetValue(value.geometry);
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
nb::dict PresetObservation(const ic::PlyWriteLimits& value) {
  nb::dict result;
  result[preset_keys::kMaxHeaderBytes.data()] =
      PresetValue(value.max_header_bytes);
  result[preset_keys::kMaxHeaderLines.data()] =
      PresetValue(value.max_header_lines);
  result[preset_keys::kMaxLineBytes.data()] = PresetValue(value.max_line_bytes);
  result[preset_keys::kMaxTokenBytes.data()] =
      PresetValue(value.max_token_bytes);
  result[preset_keys::kMaxNameBytes.data()] = PresetValue(value.max_name_bytes);
  result[preset_keys::kMaxTotalNameBytes.data()] =
      PresetValue(value.max_total_name_bytes);
  result[preset_keys::kMaxProperties.data()] =
      PresetValue(value.max_properties);
  result[preset_keys::kMaxVertices.data()] = PresetValue(value.max_vertices);
  result[preset_keys::kMaxFaces.data()] = PresetValue(value.max_faces);
  result[preset_keys::kMaxCorners.data()] = PresetValue(value.max_corners);
  result[preset_keys::kMaxCornersPerFace.data()] =
      PresetValue(value.max_corners_per_face);
  result[preset_keys::kMaxListRows.data()] = PresetValue(value.max_list_rows);
  result[preset_keys::kMaxListItemsPerRow.data()] =
      PresetValue(value.max_list_items_per_row);
  result[preset_keys::kMaxPropertyValues.data()] =
      PresetValue(value.max_property_values);
  result[preset_keys::kMaxCodecNewRequestedBytes.data()] =
      PresetValue(value.max_codec_new_requested_bytes);
  result[preset_keys::kMaxTotalNewRequestedBytes.data()] =
      PresetValue(value.max_total_new_requested_bytes);
  result[preset_keys::kMaxOutputBytes.data()] =
      PresetValue(value.max_output_bytes);
  result[preset_keys::kInspection.data()] = PresetValue(value.inspection);
  return result;
}
nb::dict PresetObservation(const ic::PlyFilePublicationLimits& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kExpectedInspection.data()] =
      PresetValue(value.expected_inspection);
  result[preset_keys::kReload.data()] = PresetValue(value.reload);
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
nb::dict PresetObservation(const PlyByteWorkPolicy& value) {
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
nb::dict PresetObservation(const ic::PlyFilePublicationWorkPolicy& value) {
  nb::dict result;
  result[preset_keys::kFiles.data()] = PresetValue(value.files);
  result[preset_keys::kExpectedInspection.data()] =
      PresetValue(value.expected_inspection);
  result[preset_keys::kReload.data()] = PresetValue(value.reload);
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
nb::dict PresetObservation(const PlyPreset& value) {
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
struct SchemaElementObservation {
  ic::PlyDocument document;
  std::size_t element;
};
struct SchemaPropertyObservation {
  ic::PlyDocument document;
  std::size_t element;
  std::size_t property;
};
nb::tuple Schema(const ic::PlyDocument& document) {
  const auto values = document.Schema();
  nb::tuple output =
      nb::steal<nb::tuple>(PyTuple_New(static_cast<Py_ssize_t>(values.size())));
  if (!output.is_valid()) throw nb::python_error();
  for (std::size_t i = 0; i < values.size(); ++i)
    PyTuple_SET_ITEM(
        output.ptr(), static_cast<Py_ssize_t>(i),
        nb::cast(SchemaElementObservation{document, i}).release().ptr());
  return output;
}
}  // namespace
void RegisterPlyBindings(nb::module_& module) {
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
  if (!nb::type<PlyWorkflowStage>().is_valid()) {
    nb::enum_<PlyWorkflowStage>(module, "PlyWorkflowStage")
        .value("kConfiguration", PlyWorkflowStage::kConfiguration)
        .value("kSourceProtection", PlyWorkflowStage::kSourceProtection)
        .value("kRepair", PlyWorkflowStage::kRepair)
        .value("kWrite", PlyWorkflowStage::kWrite)
        .value("kReload", PlyWorkflowStage::kReload)
        .value("kVerify", PlyWorkflowStage::kVerify)
        .value("kHash", PlyWorkflowStage::kHash)
        .value("kReport", PlyWorkflowStage::kReport)
        .value("kPublication", PlyWorkflowStage::kPublication)
        .value("kComplete", PlyWorkflowStage::kComplete);
  }
  if (!nb::type<PlyWorkflowStatus>().is_valid()) {
    nb::enum_<PlyWorkflowStatus>(module, "PlyWorkflowStatus")
        .value("kSuccess", PlyWorkflowStatus::kSuccess)
        .value("kInvalidConfiguration",
               PlyWorkflowStatus::kInvalidConfiguration)
        .value("kInvalidRequest", PlyWorkflowStatus::kInvalidRequest)
        .value("kLimitExceeded", PlyWorkflowStatus::kLimitExceeded)
        .value("kBudgetExceeded", PlyWorkflowStatus::kBudgetExceeded)
        .value("kCanceled", PlyWorkflowStatus::kCanceled)
        .value("kRejected", PlyWorkflowStatus::kRejected)
        .value("kDelegatedFailure", PlyWorkflowStatus::kDelegatedFailure)
        .value("kVerificationFailure", PlyWorkflowStatus::kVerificationFailure);
  }
  if (!nb::type<PlyWorkflowDiagnosticCode>().is_valid()) {
    nb::enum_<PlyWorkflowDiagnosticCode>(module, "PlyWorkflowDiagnosticCode")
        .value("kNone", PlyWorkflowDiagnosticCode::kNone)
        .value("kRequiredFieldZero",
               PlyWorkflowDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               PlyWorkflowDiagnosticCode::kConfigurationExtent)
        .value("kInputIdentity", PlyWorkflowDiagnosticCode::kInputIdentity)
        .value("kPolicy", PlyWorkflowDiagnosticCode::kPolicy)
        .value("kNameUnsupported", PlyWorkflowDiagnosticCode::kNameUnsupported)
        .value("kNameCollision", PlyWorkflowDiagnosticCode::kNameCollision)
        .value("kCountLimit", PlyWorkflowDiagnosticCode::kCountLimit)
        .value("kRequestBudget", PlyWorkflowDiagnosticCode::kRequestBudget)
        .value("kStopRequested", PlyWorkflowDiagnosticCode::kStopRequested)
        .value("kRepair", PlyWorkflowDiagnosticCode::kRepair)
        .value("kWrite", PlyWorkflowDiagnosticCode::kWrite)
        .value("kReload", PlyWorkflowDiagnosticCode::kReload)
        .value("kCorrespondence", PlyWorkflowDiagnosticCode::kCorrespondence)
        .value("kCanonicalPreservation",
               PlyWorkflowDiagnosticCode::kCanonicalPreservation)
        .value("kReloadMismatch", PlyWorkflowDiagnosticCode::kReloadMismatch)
        .value("kSourceChanged", PlyWorkflowDiagnosticCode::kSourceChanged)
        .value("kHashExtent", PlyWorkflowDiagnosticCode::kHashExtent)
        .value("kReportExtent", PlyWorkflowDiagnosticCode::kReportExtent)
        .value("kReportStringEncoding",
               PlyWorkflowDiagnosticCode::kReportStringEncoding)
        .value("kPublication", PlyWorkflowDiagnosticCode::kPublication)
        .value("kBlocked", PlyWorkflowDiagnosticCode::kBlocked)
        .value("kSolidUnavailable",
               PlyWorkflowDiagnosticCode::kSolidUnavailable)
        .value("kDeclaration", PlyWorkflowDiagnosticCode::kDeclaration)
        .value("kTypedProperties", PlyWorkflowDiagnosticCode::kTypedProperties)
        .value("kWireSchema", PlyWorkflowDiagnosticCode::kWireSchema);
  }
  if (!nb::type<PlyWorkflowExecutionOutcome>().is_valid()) {
    nb::enum_<PlyWorkflowExecutionOutcome>(module,
                                           "PlyWorkflowExecutionOutcome")
        .value("kCompleted", PlyWorkflowExecutionOutcome::kCompleted)
        .value("kFailed", PlyWorkflowExecutionOutcome::kFailed)
        .value("kCanceled", PlyWorkflowExecutionOutcome::kCanceled);
  }
  if (!nb::type<PlyWorkflowProfileOutcome>().is_valid()) {
    nb::enum_<PlyWorkflowProfileOutcome>(module, "PlyWorkflowProfileOutcome")
        .value("kPassed", PlyWorkflowProfileOutcome::kPassed)
        .value("kFailed", PlyWorkflowProfileOutcome::kFailed)
        .value("kIncomplete", PlyWorkflowProfileOutcome::kIncomplete);
  }
  if (!nb::type<PlyWorkflowPublicationOutcome>().is_valid()) {
    nb::enum_<PlyWorkflowPublicationOutcome>(module,
                                             "PlyWorkflowPublicationOutcome")
        .value("kNotRequested", PlyWorkflowPublicationOutcome::kNotRequested)
        .value("kNotAttempted", PlyWorkflowPublicationOutcome::kNotAttempted)
        .value("kFailed", PlyWorkflowPublicationOutcome::kFailed)
        .value("kCanceled", PlyWorkflowPublicationOutcome::kCanceled)
        .value("kPublished", PlyWorkflowPublicationOutcome::kPublished);
  }
  if (!nb::type<PlyWorkflowExitMeaning>().is_valid()) {
    nb::enum_<PlyWorkflowExitMeaning>(module, "PlyWorkflowExitMeaning")
        .value("kSuccess", PlyWorkflowExitMeaning::kSuccess)
        .value("kRejectedOrIncomplete",
               PlyWorkflowExitMeaning::kRejectedOrIncomplete)
        .value("kExecutionFailure", PlyWorkflowExitMeaning::kExecutionFailure)
        .value("kCanceled", PlyWorkflowExitMeaning::kCanceled);
  }
  if (!nb::type<PlyWorkflowCheck>().is_valid()) {
    nb::enum_<PlyWorkflowCheck>(module, "PlyWorkflowCheck")
        .value("kSourceStorage", PlyWorkflowCheck::kSourceStorage)
        .value("kTargetPolicy", PlyWorkflowCheck::kTargetPolicy)
        .value("kCandidateStorage", PlyWorkflowCheck::kCandidateStorage)
        .value("kRepairCorrespondence", PlyWorkflowCheck::kRepairCorrespondence)
        .value("kCanonicalPreservation",
               PlyWorkflowCheck::kCanonicalPreservation)
        .value("kReloadCandidate", PlyWorkflowCheck::kReloadCandidate)
        .value("kSourceUnchanged", PlyWorkflowCheck::kSourceUnchanged)
        .value("kTypedProperties", PlyWorkflowCheck::kTypedProperties)
        .value("kWireSchema", PlyWorkflowCheck::kWireSchema)
        .value("kStagedCandidate", PlyWorkflowCheck::kStagedCandidate)
        .value("kStagedPhysicalMembers",
               PlyWorkflowCheck::kStagedPhysicalMembers)
        .value("kSolidValidity", PlyWorkflowCheck::kSolidValidity);
  }
  if (!nb::type<PlyWorkflowCoverageState>().is_valid()) {
    nb::enum_<PlyWorkflowCoverageState>(module, "PlyWorkflowCoverageState")
        .value("kPerformed", PlyWorkflowCoverageState::kPerformed)
        .value("kSkipped", PlyWorkflowCoverageState::kSkipped)
        .value("kUnsupported", PlyWorkflowCoverageState::kUnsupported)
        .value("kFailed", PlyWorkflowCoverageState::kFailed);
  }
  if (!nb::type<ic::PlyEncoding>().is_valid()) {
    nb::enum_<ic::PlyEncoding>(module, "PlyEncoding")
        .value("kInvalid", ic::PlyEncoding::kInvalid)
        .value("kAscii", ic::PlyEncoding::kAscii)
        .value("kBinaryLittleEndian", ic::PlyEncoding::kBinaryLittleEndian)
        .value("kBinaryBigEndian", ic::PlyEncoding::kBinaryBigEndian);
  }
  if (!nb::type<ic::PlyScalarType>().is_valid()) {
    nb::enum_<ic::PlyScalarType>(module, "PlyScalarType")
        .value("kInvalid", ic::PlyScalarType::kInvalid)
        .value("kChar", ic::PlyScalarType::kChar)
        .value("kUchar", ic::PlyScalarType::kUchar)
        .value("kShort", ic::PlyScalarType::kShort)
        .value("kUshort", ic::PlyScalarType::kUshort)
        .value("kInt", ic::PlyScalarType::kInt)
        .value("kUint", ic::PlyScalarType::kUint)
        .value("kFloat", ic::PlyScalarType::kFloat)
        .value("kDouble", ic::PlyScalarType::kDouble);
  }
  if (!nb::type<ic::PlyElementKind>().is_valid()) {
    nb::enum_<ic::PlyElementKind>(module, "PlyElementKind")
        .value("kVertex", ic::PlyElementKind::kVertex)
        .value("kFace", ic::PlyElementKind::kFace);
  }
  if (!nb::type<ic::PlyPropertyKind>().is_valid()) {
    nb::enum_<ic::PlyPropertyKind>(module, "PlyPropertyKind")
        .value("kScalar", ic::PlyPropertyKind::kScalar)
        .value("kList", ic::PlyPropertyKind::kList);
  }
  if (!nb::type<ic::PlyBinding>().is_valid()) {
    nb::enum_<ic::PlyBinding>(module, "PlyBinding")
        .value("kX", ic::PlyBinding::kX)
        .value("kY", ic::PlyBinding::kY)
        .value("kZ", ic::PlyBinding::kZ)
        .value("kVertexIndices", ic::PlyBinding::kVertexIndices)
        .value("kAttribute", ic::PlyBinding::kAttribute);
  }
  if (!nb::type<ic::PlyStatus>().is_valid()) {
    nb::enum_<ic::PlyStatus>(module, "PlyStatus")
        .value("kSuccess", ic::PlyStatus::kSuccess)
        .value("kInvalidConfiguration", ic::PlyStatus::kInvalidConfiguration)
        .value("kMalformed", ic::PlyStatus::kMalformed)
        .value("kUnsupportedVariant", ic::PlyStatus::kUnsupportedVariant)
        .value("kUnsupportedBinding", ic::PlyStatus::kUnsupportedBinding)
        .value("kUnsupportedNumericConversion",
               ic::PlyStatus::kUnsupportedNumericConversion)
        .value("kLimitExceeded", ic::PlyStatus::kLimitExceeded)
        .value("kBudgetExceeded", ic::PlyStatus::kBudgetExceeded)
        .value("kCanceled", ic::PlyStatus::kCanceled)
        .value("kInvalidStorage", ic::PlyStatus::kInvalidStorage)
        .value("kGeometryFailure", ic::PlyStatus::kGeometryFailure);
  }
  if (!nb::type<ic::PlyStage>().is_valid()) {
    nb::enum_<ic::PlyStage>(module, "PlyStage")
        .value("kConfiguration", ic::PlyStage::kConfiguration)
        .value("kScan", ic::PlyStage::kScan)
        .value("kDecode", ic::PlyStage::kDecode)
        .value("kMaterialization", ic::PlyStage::kMaterialization)
        .value("kInspection", ic::PlyStage::kInspection)
        .value("kValidation", ic::PlyStage::kValidation)
        .value("kEncode", ic::PlyStage::kEncode)
        .value("kPublication", ic::PlyStage::kPublication);
  }
  if (!nb::type<ic::PlyDiagnosticCode>().is_valid()) {
    nb::enum_<ic::PlyDiagnosticCode>(module, "PlyDiagnosticCode")
        .value("kNone", ic::PlyDiagnosticCode::kNone)
        .value("kRequiredFieldZero", ic::PlyDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               ic::PlyDiagnosticCode::kConfigurationExtent)
        .value("kSourceLimit", ic::PlyDiagnosticCode::kSourceLimit)
        .value("kHeaderLimit", ic::PlyDiagnosticCode::kHeaderLimit)
        .value("kHeaderLineLimit", ic::PlyDiagnosticCode::kHeaderLineLimit)
        .value("kLineLimit", ic::PlyDiagnosticCode::kLineLimit)
        .value("kTokenLimit", ic::PlyDiagnosticCode::kTokenLimit)
        .value("kCommentLimit", ic::PlyDiagnosticCode::kCommentLimit)
        .value("kNameLimit", ic::PlyDiagnosticCode::kNameLimit)
        .value("kTotalNameLimit", ic::PlyDiagnosticCode::kTotalNameLimit)
        .value("kPropertyLimit", ic::PlyDiagnosticCode::kPropertyLimit)
        .value("kVertexLimit", ic::PlyDiagnosticCode::kVertexLimit)
        .value("kFaceLimit", ic::PlyDiagnosticCode::kFaceLimit)
        .value("kCornerLimit", ic::PlyDiagnosticCode::kCornerLimit)
        .value("kPerFaceCornerLimit",
               ic::PlyDiagnosticCode::kPerFaceCornerLimit)
        .value("kListRowLimit", ic::PlyDiagnosticCode::kListRowLimit)
        .value("kListItemLimit", ic::PlyDiagnosticCode::kListItemLimit)
        .value("kPropertyValueLimit",
               ic::PlyDiagnosticCode::kPropertyValueLimit)
        .value("kOutputLimit", ic::PlyDiagnosticCode::kOutputLimit)
        .value("kSyntax", ic::PlyDiagnosticCode::kSyntax)
        .value("kIntegerSyntax", ic::PlyDiagnosticCode::kIntegerSyntax)
        .value("kIntegerRange", ic::PlyDiagnosticCode::kIntegerRange)
        .value("kNumericSyntax", ic::PlyDiagnosticCode::kNumericSyntax)
        .value("kNumericRange", ic::PlyDiagnosticCode::kNumericRange)
        .value("kNonfinite", ic::PlyDiagnosticCode::kNonfinite)
        .value("kVariant", ic::PlyDiagnosticCode::kVariant)
        .value("kDuplicateName", ic::PlyDiagnosticCode::kDuplicateName)
        .value("kMissingCoreProperty",
               ic::PlyDiagnosticCode::kMissingCoreProperty)
        .value("kTrailingData", ic::PlyDiagnosticCode::kTrailingData)
        .value("kVertexRange", ic::PlyDiagnosticCode::kVertexRange)
        .value("kFaceTooShort", ic::PlyDiagnosticCode::kFaceTooShort)
        .value("kSchemaBinding", ic::PlyDiagnosticCode::kSchemaBinding)
        .value("kNumericConversion", ic::PlyDiagnosticCode::kNumericConversion)
        .value("kRewritePolicy", ic::PlyDiagnosticCode::kRewritePolicy)
        .value("kEncoding", ic::PlyDiagnosticCode::kEncoding)
        .value("kRequestBudget", ic::PlyDiagnosticCode::kRequestBudget)
        .value("kStopRequested", ic::PlyDiagnosticCode::kStopRequested)
        .value("kGeometry", ic::PlyDiagnosticCode::kGeometry)
        .value("kStorageIssues", ic::PlyDiagnosticCode::kStorageIssues);
  }
  if (!nb::type<ic::PlyRewritePolicy>().is_valid()) {
    nb::enum_<ic::PlyRewritePolicy>(module, "PlyRewritePolicy")
        .value("kInvalid", ic::PlyRewritePolicy::kInvalid)
        .value("kCanonical", ic::PlyRewritePolicy::kCanonical);
  }
  if (!nb::type<ic::PlyCorrespondence>().is_valid()) {
    nb::enum_<ic::PlyCorrespondence>(module, "PlyCorrespondence")
        .value("kInvalid", ic::PlyCorrespondence::kInvalid)
        .value("kIdentity", ic::PlyCorrespondence::kIdentity);
  }
  if (!nb::type<ic::PlyFileStatus>().is_valid()) {
    nb::enum_<ic::PlyFileStatus>(module, "PlyFileStatus")
        .value("kSuccess", ic::PlyFileStatus::kSuccess)
        .value("kInvalidConfiguration",
               ic::PlyFileStatus::kInvalidConfiguration)
        .value("kInvalidRequest", ic::PlyFileStatus::kInvalidRequest)
        .value("kUnsupportedProfile", ic::PlyFileStatus::kUnsupportedProfile)
        .value("kLimitExceeded", ic::PlyFileStatus::kLimitExceeded)
        .value("kBudgetExceeded", ic::PlyFileStatus::kBudgetExceeded)
        .value("kForeignBudget", ic::PlyFileStatus::kForeignBudget)
        .value("kIoFailure", ic::PlyFileStatus::kIoFailure)
        .value("kFileChanged", ic::PlyFileStatus::kFileChanged)
        .value("kPlyFailure", ic::PlyFileStatus::kPlyFailure)
        .value("kGeometryFailure", ic::PlyFileStatus::kGeometryFailure)
        .value("kPreservationMismatch",
               ic::PlyFileStatus::kPreservationMismatch)
        .value("kProviderFailure", ic::PlyFileStatus::kProviderFailure)
        .value("kDestinationExists", ic::PlyFileStatus::kDestinationExists)
        .value("kPublicationFailed", ic::PlyFileStatus::kPublicationFailed)
        .value("kCanceled", ic::PlyFileStatus::kCanceled);
  }
  if (!nb::type<ic::PlyFileStage>().is_valid()) {
    nb::enum_<ic::PlyFileStage>(module, "PlyFileStage")
        .value("kConfiguration", ic::PlyFileStage::kConfiguration)
        .value("kPathPlanning", ic::PlyFileStage::kPathPlanning)
        .value("kCapture", ic::PlyFileStage::kCapture)
        .value("kPlyRead", ic::PlyFileStage::kPlyRead)
        .value("kExpectedInspection", ic::PlyFileStage::kExpectedInspection)
        .value("kStaging", ic::PlyFileStage::kStaging)
        .value("kReload", ic::PlyFileStage::kReload)
        .value("kVerification", ic::PlyFileStage::kVerification)
        .value("kProvider", ic::PlyFileStage::kProvider)
        .value("kInventory", ic::PlyFileStage::kInventory)
        .value("kPublication", ic::PlyFileStage::kPublication)
        .value("kComplete", ic::PlyFileStage::kComplete);
  }
  if (!nb::type<ic::PlyFileDiagnosticCode>().is_valid()) {
    nb::enum_<ic::PlyFileDiagnosticCode>(module, "PlyFileDiagnosticCode")
        .value("kNone", ic::PlyFileDiagnosticCode::kNone)
        .value("kRequiredFieldZero",
               ic::PlyFileDiagnosticCode::kRequiredFieldZero)
        .value("kConfigurationExtent",
               ic::PlyFileDiagnosticCode::kConfigurationExtent)
        .value("kCountLimit", ic::PlyFileDiagnosticCode::kCountLimit)
        .value("kPathUnsupported", ic::PlyFileDiagnosticCode::kPathUnsupported)
        .value("kNameUnsupported", ic::PlyFileDiagnosticCode::kNameUnsupported)
        .value("kNameCollision", ic::PlyFileDiagnosticCode::kNameCollision)
        .value("kNotRegular", ic::PlyFileDiagnosticCode::kNotRegular)
        .value("kOpen", ic::PlyFileDiagnosticCode::kOpen)
        .value("kRead", ic::PlyFileDiagnosticCode::kRead)
        .value("kWrite", ic::PlyFileDiagnosticCode::kWrite)
        .value("kClose", ic::PlyFileDiagnosticCode::kClose)
        .value("kChanged", ic::PlyFileDiagnosticCode::kChanged)
        .value("kRequestBudget", ic::PlyFileDiagnosticCode::kRequestBudget)
        .value("kSharedBudget", ic::PlyFileDiagnosticCode::kSharedBudget)
        .value("kStopRequested", ic::PlyFileDiagnosticCode::kStopRequested)
        .value("kLedgerMismatch", ic::PlyFileDiagnosticCode::kLedgerMismatch)
        .value("kDelegatedPly", ic::PlyFileDiagnosticCode::kDelegatedPly)
        .value("kDelegatedInspection",
               ic::PlyFileDiagnosticCode::kDelegatedInspection)
        .value("kAttributeMismatch",
               ic::PlyFileDiagnosticCode::kAttributeMismatch)
        .value("kOutputFacts", ic::PlyFileDiagnosticCode::kOutputFacts)
        .value("kByteMismatch", ic::PlyFileDiagnosticCode::kByteMismatch)
        .value("kMeshMismatch", ic::PlyFileDiagnosticCode::kMeshMismatch)
        .value("kProvider", ic::PlyFileDiagnosticCode::kProvider)
        .value("kStagedInventory", ic::PlyFileDiagnosticCode::kStagedInventory)
        .value("kDestinationExists",
               ic::PlyFileDiagnosticCode::kDestinationExists)
        .value("kCommit", ic::PlyFileDiagnosticCode::kCommit)
        .value("kCommitUnsupported",
               ic::PlyFileDiagnosticCode::kCommitUnsupported)
        .value("kSchemaMismatch", ic::PlyFileDiagnosticCode::kSchemaMismatch);
  }
  if (!nb::type<ic::PlyFilePublicationOutcome>().is_valid()) {
    nb::enum_<ic::PlyFilePublicationOutcome>(module,
                                             "PlyFilePublicationOutcome")
        .value("kFailed", ic::PlyFilePublicationOutcome::kFailed)
        .value("kCanceled", ic::PlyFilePublicationOutcome::kCanceled)
        .value("kPublished", ic::PlyFilePublicationOutcome::kPublished);
  }
  if (!nb::type<ic::PlyFileCleanupOutcome>().is_valid()) {
    nb::enum_<ic::PlyFileCleanupOutcome>(module, "PlyFileCleanupOutcome")
        .value("kNotRequired", ic::PlyFileCleanupOutcome::kNotRequired)
        .value("kRemoved", ic::PlyFileCleanupOutcome::kRemoved)
        .value("kFailed", ic::PlyFileCleanupOutcome::kFailed);
  }
  {
    auto type = nb::class_<PlyObservation<PlyWorkflowDiagnostic>>(
        module, "PlyWorkflowDiagnostic");
    Property(type, "stage", &PlyWorkflowDiagnostic::stage);
    Property(type, "code", &PlyWorkflowDiagnostic::code);
    Property(type, "element", &PlyWorkflowDiagnostic::element);
    Property(type, "delegated_status",
             &PlyWorkflowDiagnostic::delegated_status);
    Property(type, "delegated", &PlyWorkflowDiagnostic::delegated);
  }
  {
    auto type = nb::class_<PlyObservation<PlyWorkflowOutcomes>>(
        module, "PlyWorkflowOutcomes");
    Property(type, "execution", &PlyWorkflowOutcomes::execution);
    Property(type, "candidate", &PlyWorkflowOutcomes::candidate);
    Property(type, "profile", &PlyWorkflowOutcomes::profile);
    Property(type, "publication", &PlyWorkflowOutcomes::publication);
    Property(type, "stage", &PlyWorkflowOutcomes::stage);
    Property(type, "exit", &PlyWorkflowOutcomes::exit);
  }
  {
    auto type = nb::class_<PlyObservation<PlyWorkflowCoverage>>(
        module, "PlyWorkflowCoverage");
    Property(type, "check", &PlyWorkflowCoverage::check);
    Property(type, "state", &PlyWorkflowCoverage::state);
    Property(type, "passed", &PlyWorkflowCoverage::passed);
    Property(type, "reason", &PlyWorkflowCoverage::reason);
  }
  {
    auto type = nb::class_<PlyObservation<PlyWorkflowExecution>>(
        module, "PlyWorkflowExecution");
    Property(type, "repair", &PlyWorkflowExecution::repair);
    Property(type, "writing", &PlyWorkflowExecution::writing);
    Property(type, "reload", &PlyWorkflowExecution::reload);
    Property(type, "source_protection",
             &PlyWorkflowExecution::source_protection);
    Property(type, "repair_correspondence",
             &PlyWorkflowExecution::repair_correspondence);
    Property(type, "canonical_preservation",
             &PlyWorkflowExecution::canonical_preservation);
    Property(type, "typed_properties", &PlyWorkflowExecution::typed_properties);
    Property(type, "wire_schema", &PlyWorkflowExecution::wire_schema);
    Property(type, "reload_verification",
             &PlyWorkflowExecution::reload_verification);
    Property(type, "source_verification",
             &PlyWorkflowExecution::source_verification);
    Property(type, "staged_candidate", &PlyWorkflowExecution::staged_candidate);
    Property(type, "hashing", &PlyWorkflowExecution::hashing);
    Property(type, "report_copy", &PlyWorkflowExecution::report_copy);
  }
  {
    auto type = nb::class_<PlyObservation<PlyWorkflowRequestedBytes>>(
        module, "PlyWorkflowRequestedBytes");
    Property(type, "wrapper", &PlyWorkflowRequestedBytes::wrapper);
    Property(type, "source_files", &PlyWorkflowRequestedBytes::source_files);
    Property(type, "source_ply", &PlyWorkflowRequestedBytes::source_ply);
    Property(type, "repair", &PlyWorkflowRequestedBytes::repair);
    Property(type, "writing", &PlyWorkflowRequestedBytes::writing);
    Property(type, "reload", &PlyWorkflowRequestedBytes::reload);
    Property(type, "publication_files",
             &PlyWorkflowRequestedBytes::publication_files);
    Property(type, "publication_ply",
             &PlyWorkflowRequestedBytes::publication_ply);
  }
  {
    auto type = nb::class_<PlyObservation<ic::PlyFileDiagnostic>>(
        module, "PlyFileDiagnostic");
    Property(type, "status", &ic::PlyFileDiagnostic::status);
    Property(type, "stage", &ic::PlyFileDiagnostic::stage);
    Property(type, "code", &ic::PlyFileDiagnostic::code);
    Property(type, "file_index", &ic::PlyFileDiagnostic::file_index);
    Property(type, "byte_offset", &ic::PlyFileDiagnostic::byte_offset);
    Property(type, "system_error", &ic::PlyFileDiagnostic::system_error);
    Property(type, "ply_status", &ic::PlyFileDiagnostic::ply_status);
    Property(type, "ply_diagnostic", &ic::PlyFileDiagnostic::ply_diagnostic);
    Property(type, "geometry_status", &ic::PlyFileDiagnostic::geometry_status);
    Property(type, "geometry_diagnostic",
             &ic::PlyFileDiagnostic::geometry_diagnostic);
  }
  {
    auto type = nb::class_<PlyObservation<ic::PlyFileExecution>>(
        module, "PlyFileExecution");
    Property(type, "capture", &ic::PlyFileExecution::capture);
    Property(type, "copying", &ic::PlyFileExecution::copying);
    Property(type, "byte_verification",
             &ic::PlyFileExecution::byte_verification);
    Property(type, "mesh_verification",
             &ic::PlyFileExecution::mesh_verification);
    Property(type, "expected_inspection",
             &ic::PlyFileExecution::expected_inspection);
    Property(type, "ply", &ic::PlyFileExecution::ply);
  }
  {
    auto type = nb::class_<PlyObservation<ic::PlyFilePublicationResult>>(
        module, "PlyFilePublicationObservation");
    Property(type, "outcome", &ic::PlyFilePublicationResult::outcome);
    Property(type, "phase", &ic::PlyFilePublicationResult::phase);
    Property(type, "diagnostic", &ic::PlyFilePublicationResult::diagnostic);
    Property(type, "cleanup", &ic::PlyFilePublicationResult::cleanup);
    Property(type, "cleanup_error",
             &ic::PlyFilePublicationResult::cleanup_error);
    Property(type, "execution", &ic::PlyFilePublicationResult::execution);
  }
  {
    auto type =
        nb::class_<PlyObservation<ic::PlyDiagnostic>>(module, "PlyDiagnostic");
    Property(type, "stage", &ic::PlyDiagnostic::stage);
    Property(type, "code", &ic::PlyDiagnostic::code);
    Property(type, "byte_offset", &ic::PlyDiagnostic::byte_offset);
    Property(type, "element", &ic::PlyDiagnostic::element);
    Property(type, "property", &ic::PlyDiagnostic::property);
    Property(type, "row", &ic::PlyDiagnostic::row);
    Property(type, "item", &ic::PlyDiagnostic::item);
    Property(type, "attribute", &ic::PlyDiagnostic::attribute);
    Property(type, "geometry_status", &ic::PlyDiagnostic::geometry_status);
    Property(type, "geometry_diagnostic",
             &ic::PlyDiagnostic::geometry_diagnostic);
    Property(type, "storage_issue", &ic::PlyDiagnostic::storage_issue);
  }
  {
    auto type = nb::class_<PlyObservation<ic::PlyExecution>>(
        module, "PlyCodecExecution");
    Property(type, "codec", &ic::PlyExecution::codec);
    Property(type, "materialization", &ic::PlyExecution::materialization);
    Property(type, "inspection", &ic::PlyExecution::inspection);
  }
  {
    auto type = nb::class_<PlyObservation<ic::PlyRequestedBytes>>(
        module, "PlyRequestedBytes");
    Property(type, "codec", &ic::PlyRequestedBytes::codec);
    Property(type, "materialization", &ic::PlyRequestedBytes::materialization);
    Property(type, "inspection", &ic::PlyRequestedBytes::inspection);
    Property(type, "total", &ic::PlyRequestedBytes::total);
  }
  {
    auto type =
        nb::class_<PlyObservation<ic::PlyWriteFacts>>(module, "PlyWriteFacts");
    Property(type, "source_encoding", &ic::PlyWriteFacts::source_encoding);
    Property(type, "source_vertex_count",
             &ic::PlyWriteFacts::source_vertex_count);
    Property(type, "source_face_count", &ic::PlyWriteFacts::source_face_count);
    Property(type, "encoding", &ic::PlyWriteFacts::encoding);
    Property(type, "rewrite_policy", &ic::PlyWriteFacts::rewrite_policy);
    Property(type, "vertex_count", &ic::PlyWriteFacts::vertex_count);
    Property(type, "face_count", &ic::PlyWriteFacts::face_count);
    Property(type, "corner_count", &ic::PlyWriteFacts::corner_count);
    Property(type, "property_count", &ic::PlyWriteFacts::property_count);
    Property(type, "correspondence", &ic::PlyWriteFacts::correspondence);
  }
  {
    auto type = nb::class_<PlyObservation<MaterializedDuplicateDiagnostic>>(
        module, "PlyMaterializedDuplicateDiagnostic");
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
    auto type = nb::class_<PlyObservation<MaterializedDuplicateExecution>>(
        module, "PlyMaterializedDuplicateExecution");
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
    auto type = nb::class_<PlyObservation<MaterializedDuplicateRequestedBytes>>(
        module, "PlyMaterializedDuplicateRequestedBytes");
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
    auto type = nb::class_<PlyObservation<geo::MaterializationDiagnostic>>(
        module, "PlyMaterializationDiagnostic");
    Property(type, "code", &geo::MaterializationDiagnostic::code);
    Property(type, "attribute", &geo::MaterializationDiagnostic::attribute);
    Property(type, "element", &geo::MaterializationDiagnostic::element);
    Property(type, "storage_issue",
             &geo::MaterializationDiagnostic::storage_issue);
  }
  {
    auto type = nb::class_<PlyObservation<geo::StorageIssue>>(
        module, "PlyStorageIssue");
    Property(type, "code", &geo::StorageIssue::code);
    Property(type, "attribute", &geo::StorageIssue::attribute);
    Property(type, "element", &geo::StorageIssue::element);
  }
  {
    auto type = nb::class_<PlyObservation<geo::MaterializationExecutionStats>>(
        module, "PlyMaterializationExecutionStats");
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
    auto type = nb::class_<PlyObservation<CapturedRequestedBytes>>(
        module, "PlyCapturedRequestedBytes");
    Property(type, "files", &CapturedRequestedBytes::files);
    Property(type, "ply", &CapturedRequestedBytes::ply);
  }
  nb::class_<PlyObservation<std::error_code>>(module, "PlySystemErrorCode")
      .def_prop_ro("category",
                   [](const PlyObservation<std::error_code>& self) {
                     return nb::str(self.value.category().name());
                   })
      .def_prop_ro("value", [](const PlyObservation<std::error_code>& self) {
        return self.value.value();
      });
  nb::class_<SchemaElementObservation>(module, "PlyElementView")
      .def_prop_ro("kind",
                   [](const SchemaElementObservation& self) {
                     return Observe(self.document.Schema()[self.element].kind);
                   })
      .def_prop_ro("original_count",
                   [](const SchemaElementObservation& self) {
                     return self.document.Schema()[self.element].original_count;
                   })
      .def_prop_ro("properties", [](const SchemaElementObservation& self) {
        const auto values = self.document.Schema()[self.element].properties;
        nb::tuple output = nb::steal<nb::tuple>(
            PyTuple_New(static_cast<Py_ssize_t>(values.size())));
        if (!output.is_valid()) throw nb::python_error();
        for (std::size_t i = 0; i < values.size(); ++i)
          PyTuple_SET_ITEM(output.ptr(), static_cast<Py_ssize_t>(i),
                           nb::cast(SchemaPropertyObservation{self.document,
                                                              self.element, i})
                               .release()
                               .ptr());
        return output;
      });
  auto property =
      nb::class_<SchemaPropertyObservation>(module, "PlyPropertyView");
  property.def_prop_ro("name", [](const SchemaPropertyObservation& self) {
    const auto name =
        self.document.Schema()[self.element].properties[self.property].name;
    return nb::str(name.data(), name.size());
  });
  property.def_prop_ro("kind", [](const SchemaPropertyObservation& self) {
    return Observe(
        self.document.Schema()[self.element].properties[self.property].kind);
  });
  property.def_prop_ro("value_type", [](const SchemaPropertyObservation& self) {
    return Observe(self.document.Schema()[self.element]
                       .properties[self.property]
                       .value_type);
  });
  property.def_prop_ro("count_type", [](const SchemaPropertyObservation& self) {
    return Observe(self.document.Schema()[self.element]
                       .properties[self.property]
                       .count_type);
  });
  property.def_prop_ro("binding", [](const SchemaPropertyObservation& self) {
    return Observe(
        self.document.Schema()[self.element].properties[self.property].binding);
  });
  property.def_prop_ro("attribute", [](const SchemaPropertyObservation& self) {
    return Observe(self.document.Schema()[self.element]
                       .properties[self.property]
                       .attribute);
  });
  nb::class_<PlyExecution>(module, "PlyExecution")
      .def("__init__", &InitExecution, nb::kw_only(),
           nb::arg("preset") = kBalancedPlyPreset)
      .def_prop_ro(
          "preset",
          [](const PlyExecution& self) { return nb::str(self.Name().c_str()); })
      .def_prop_ro("resolved_preset",
                   [](const PlyExecution& self) {
                     return PresetObservation(self.Preset());
                   })
      .def_prop_ro("worker_budget",
                   [](const PlyExecution& self) {
                     return self.Context().WorkerBudget();
                   })
      .def_prop_ro("active_workers",
                   [](const PlyExecution& self) {
                     return self.Context().ActiveWorkers();
                   })
      .def_prop_ro("active_tracked_payload", [](const PlyExecution& self) {
        return self.Context().ActiveTrackedPayload();
      });
  nb::class_<PlyCancellation>(module, "PlyCancellation")
      .def(nb::init<>())
      .def("cancel", &PlyCancellation::Cancel)
      .def_prop_ro("cancelled", &PlyCancellation::Cancelled);
  auto result =
      nb::class_<PlyFileWorkflowResult>(module, "PlyFileWorkflowResult");
  result.def_prop_ro("workflow_started", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started;
  });
  result.def_prop_ro(
      "exit_code", [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        return self.workflow_started
                   ? nb::cast(static_cast<int>(self.workflow.outcomes.exit))
                   : nb::none();
      });
  result.def_prop_ro("memory_output_verified",
                     [](const PlyFileWorkflowResult& self) {
                       self.RequireComplete();
                       return self.workflow.record &&
                              self.workflow.record->Output().has_value() &&
                              self.workflow.record->Reload().has_value();
                     });
  result.def_prop_ro("capture_status", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return Observe(self.capture.status);
  });
  result.def_prop_ro("capture_diagnostic",
                     [](const PlyFileWorkflowResult& self) {
                       self.RequireComplete();
                       return Observe(self.capture.diagnostic);
                     });
  result.def_prop_ro("capture_execution",
                     [](const PlyFileWorkflowResult& self) {
                       self.RequireComplete();
                       return Observe(self.capture.execution);
                     });
  result.def_prop_ro(
      "capture_requested_bytes", [](const PlyFileWorkflowResult& self) {
        self.RequireComplete();
        return self.capture.asset
                   ? Observe(CapturedRequestedBytes{
                         self.capture.asset->NewRequestedBytes(),
                         self.capture.asset->Document().NewRequestedBytes()})
                   : nb::none();
      });
  result.def_prop_ro("status", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.status) : nb::none();
  });
  result.def_prop_ro("outcomes", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.outcomes) : nb::none();
  });
  result.def_prop_ro("diagnostic", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.diagnostic)
                                 : nb::none();
  });
  result.def_prop_ro(
      "report_diagnostic", [](const PlyFileWorkflowResult& self) {
        self.RequireComplete();
        return self.workflow_started ? Observe(self.workflow.report_diagnostic)
                                     : nb::none();
      });
  result.def_prop_ro("execution", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.execution)
                                 : nb::none();
  });
  result.def_prop_ro("publication", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow_started ? Observe(self.workflow.publication)
                                 : nb::none();
  });
  result.def_prop_ro("requested_bytes", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow.record
               ? Observe(self.workflow.record->NewRequestedBytes())
               : nb::none();
  });
  result.def_prop_ro(
      "coverage", [](const PlyFileWorkflowResult& self) -> nb::object {
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
  result.def_prop_ro("source_encoding", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.capture.asset
               ? Observe(self.capture.asset->Document().Encoding())
               : nb::none();
  });
  result.def_prop_ro("source_schema",
                     [](const PlyFileWorkflowResult& self) -> nb::object {
                       self.RequireComplete();
                       if (!(self.capture.asset)) return nb::none();
                       return Schema(self.capture.asset->Document());
                     });
  result.def_prop_ro("reload_encoding", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    return self.workflow.record && self.workflow.record->Reload()
               ? Observe(self.workflow.record->Reload()->Encoding())
               : nb::none();
  });
  result.def_prop_ro(
      "reload_schema", [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!(self.workflow.record && self.workflow.record->Reload()))
          return nb::none();
        return Schema(*self.workflow.record->Reload());
      });
  result.def_prop_ro(
      "published_encoding", [](const PlyFileWorkflowResult& self) {
        self.RequireComplete();
        return self.workflow.record && self.workflow.record->Published()
                   ? Observe(self.workflow.record->Published()
                                 ->Document()
                                 .Encoding())
                   : nb::none();
      });
  result.def_prop_ro(
      "published_schema", [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!(self.workflow.record && self.workflow.record->Published()))
          return nb::none();
        return Schema(self.workflow.record->Published()->Document());
      });
  result.def_prop_ro("original_bytes",
                     [](const PlyFileWorkflowResult& self) -> nb::object {
                       self.RequireComplete();
                       if (!self.capture.asset) return nb::none();
                       const auto bytes = self.capture.asset->EntryBytes();
                       return nb::bytes(bytes.data(), bytes.size());
                     });
  result.def_prop_ro("source_entry_path",
                     [](const PlyFileWorkflowResult& self) -> nb::object {
                       self.RequireComplete();
                       if (!self.capture.asset) return nb::none();
                       const auto path = self.capture.asset->EntryPath();
                       return nb::str(path.data(), path.size());
                     });
  result.def_prop_ro(
      "published_entry_path",
      [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record || !self.workflow.record->Published())
          return nb::none();
        const auto path = self.workflow.record->Published()->EntryPath();
        return nb::str(path.data(), path.size());
      });
  result.def_prop_ro(
      "published_report_path",
      [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record) return nb::none();
        const auto path = self.workflow.record->PublishedReportPath();
        return path ? nb::object(nb::str(path->data(), path->size()))
                    : nb::none();
      });
  result.def_prop_ro(
      "input_sha256", [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record) return nb::none();
        const auto hash = self.workflow.record->InputSha256();
        return hash ? nb::object(nb::str(hash->data(), hash->size()))
                    : nb::none();
      });
  result.def_prop_ro(
      "report_json", [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record) return nb::none();
        const auto bytes = self.workflow.record->ReportJson();
        return bytes.empty()
                   ? nb::none()
                   : nb::object(nb::bytes(bytes.data(), bytes.size()));
      });
  result.def_prop_ro(
      "output_bytes", [](const PlyFileWorkflowResult& self) -> nb::object {
        self.RequireComplete();
        if (!self.workflow.record || !self.workflow.record->Output())
          return nb::none();
        const auto bytes = self.workflow.record->Output()->Bytes();
        return nb::bytes(bytes.data(), bytes.size());
      });
  result.def_prop_ro("write_facts", [](const PlyFileWorkflowResult& self) {
    self.RequireComplete();
    const auto* facts =
        self.workflow.record ? self.workflow.record->WriteFacts() : nullptr;
    return facts ? Observe(*facts) : nb::none();
  });
  result.def_prop_ro(
      "input_snapshot_counts", [](const PlyFileWorkflowResult& self) {
        return SnapshotCounts(self, &MaterializedDuplicateRepair::Source);
      });
  result.def_prop_ro(
      "candidate_snapshot_counts", [](const PlyFileWorkflowResult& self) {
        return SnapshotCounts(self, &MaterializedDuplicateRepair::Candidate);
      });
  result.def_prop_ro("face_map", [](const PlyFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::FaceMap);
  });
  result.def_prop_ro("corner_map", [](const PlyFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::CornerMap);
  });
  result.def_prop_ro("source_faces", [](const PlyFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::SourceFaces);
  });
  result.def_prop_ro("source_corners", [](const PlyFileWorkflowResult& self) {
    return Map(self, &MaterializedDuplicateRepair::SourceCorners);
  });
  module.def("_prepare_ply_file", &Prepare, nb::arg("source"),
             nb::arg("destination").none(), nb::arg("targets"), nb::kw_only(),
             nb::arg("encoding"), nb::arg("row_local_attributes") = nb::tuple(),
             nb::arg("execution"), nb::arg("input_id") = kDefaultPlyInputId,
             nb::arg("report_path") = kDefaultPlyReportPath,
             nb::arg("cancellation") = nb::none());
  // Borrowed preallocated handle; no post-native conversion or keepalive owner.
  module.def("_run_ply_file", &Run);
}
}  // namespace meshvale::repair::python
