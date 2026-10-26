// SPDX-License-Identifier: Apache-2.0
#include "gltf_bindings.h"

#include <Python.h>
#include <meshvale/geometry/attributes.h>
#include <meshvale/geometry/editable_mesh.h>
#include <meshvale/geometry/execution.h>
#include <meshvale/geometry/materialization.h>
#include <meshvale/interchange/glb.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/interchange/gltf_files.h>
#include <meshvale/interchange/gltf_materialization.h>
#include <meshvale/interchange/gltf_topology_write.h>
#include <meshvale/interchange/gltf_workflow_support.h>
#include <meshvale/interchange/gltf_write.h>
#include <meshvale/repair/duplicates.h>
#include <meshvale/repair/gltf_file_workflow.h>
#include <meshvale/repair/gltf_workflow.h>
#include <meshvale/repair/materialized_duplicates.h>
#include <nanobind/stl/string_view.h>

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

#include "gltf_presets.h"

#if defined(MESHVALE_REPAIR_GLTF_BINDING_TESTS)
#include "gltf_binding_observer.h"
#include "gltf_file_workflow_test_internal.h"
#endif

namespace meshvale::repair::python {
namespace {
namespace nb = nanobind;
namespace ic = interchange;
namespace geo = geometry;
constexpr std::string_view kVertexDomainName = "vertex";
constexpr std::string_view kFaceDomainName = "face";
constexpr std::string_view kCornerDomainName = "corner";
constexpr std::size_t kPositionComponents = 3;
// Closed processing-object and additive API/property names.
struct GltfTranslationFields {
  static constexpr char kDirectTranslation[] = "direct_translation";
  static constexpr char kProfile[] = "profile";
  static constexpr char kSource[] = "source";
  static constexpr char kWriting[] = "writing";
  static constexpr char kReload[] = "reload";
  static constexpr char kVerification[] = "verification";
  static constexpr char kPublicationReload[] = "publication_reload";
  static constexpr char kPublicationVerification[] = "publication_verification";
  static constexpr char kOffset[] = "offset";
  static constexpr char kLength[] = "length";
  static constexpr char kStatus[] = "status";
  static constexpr char kCode[] = "code";
  static constexpr char kJsonSpan[] = "json_span";
  static constexpr char kResourceKind[] = "resource_kind";
  static constexpr char kResourceIndex[] = "resource_index";
  static constexpr char kResourceSpan[] = "resource_span";
  static constexpr char kRuleId[] = "rule_id";
  static constexpr char kDiagnostic[] = "diagnostic";
  static constexpr char kComplete[] = "complete";
  static constexpr char kNodeRows[] = "node_rows";
  static constexpr char kMeshUsers[] = "mesh_users";
  static constexpr char kScannedChannels[] = "scanned_channels";
  static constexpr char kAffectedChannels[] = "affected_channels";
  static constexpr char kDistinctSamplers[] = "distinct_samplers";
  static constexpr char kKeysChecked[] = "keys_checked";
  static constexpr char kDecodedBytesChecked[] = "decoded_bytes_checked";
  static constexpr char kRelationshipSteps[] = "relationship_steps";
  static constexpr char kNewRequestedBytes[] = "new_requested_bytes";
  static constexpr char kEffectiveRequestCap[] = "effective_request_cap";
  static constexpr char kWork[] = "work";
  static constexpr char kExecution[] = "execution";
  static constexpr char kPlannedJobs[] = "planned_jobs";
  static constexpr char kLaunchedWorkers[] = "launched_workers";
  static constexpr char kJoinedWorkers[] = "joined_workers";
  static constexpr char kCallerJobs[] = "caller_jobs";
  static constexpr char kMaxNodeRows[] = "max_node_rows";
  static constexpr char kMaxMeshUsers[] = "max_mesh_users";
  static constexpr char kMaxChannelReferences[] = "max_channel_references";
  static constexpr char kMaxDistinctSamplers[] = "max_distinct_samplers";
  static constexpr char kMaxKeysPerSampler[] = "max_keys_per_sampler";
  static constexpr char kMaxTotalKeys[] = "max_total_keys";
  static constexpr char kMaxRelationshipSteps[] = "max_relationship_steps";
  static constexpr char kMaxMetadataBytes[] = "max_metadata_bytes";
  static constexpr char kMaxDecodedBytes[] = "max_decoded_bytes";
  static constexpr char kMaxNewRequestedBytes[] = "max_new_requested_bytes";
  static constexpr char kParallelMinKeys[] = "parallel_min_keys";
  static constexpr char kPartitionKeys[] = "partition_keys";
  static constexpr char kCancellationBlockKeys[] = "cancellation_block_keys";
  static constexpr char kMaxJobs[] = "max_jobs";
  static constexpr char kProcessingProfile[] = "processing_profile";
  static constexpr char kTranslationFacts[] = "translation_facts";
  static constexpr char kDirectTranslationPolicy[] =
      "_direct_translation_policy";
};
constexpr std::uint64_t kMaxGltfProcessingProfileBytes = 32;

template <class T>
struct Observation {
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
    return nb::cast(Observation<T>{value});
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
  return nb::cast(Observation<std::error_code>{value});
}

class GltfExecution {
 public:
  explicit GltfExecution(std::string_view name)
      : name_(name),
        preset_(MakeGltfPreset(name)),
        context_(preset_.execution) {}
  // A policy handle over the existing module context, without a new ledger.
  GltfExecution DirectTranslationPolicy() const {
    if (name_ != kBalancedGltfPreset)
      throw nb::value_error(
          "default policy requires the balanced static session");
    GltfExecution policy(*this);
    policy.name_ = kBalancedTranslationGltfPreset;
    policy.preset_ = MakeGltfPreset(policy.name_);
    return policy;
  }
  bool SupportsDirectTranslation() const noexcept {
    return name_ == kBalancedTranslationGltfPreset ||
           name_ == kSerialTranslationGltfPreset;
  }
  const std::string& Name() const noexcept { return name_; }
  const GltfPreset& Preset() const noexcept { return preset_; }
  const geo::ExecutionContext& Context() const noexcept { return context_; }

 private:
  std::string name_;
  GltfPreset preset_;
  geo::ExecutionContext context_;
};
class GltfCancellation {
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
struct GltfFileWorkflowResult {
  explicit GltfFileWorkflowResult(const GltfExecution& session,
                                  std::stop_source cancellation)
      : preset(session.Preset()),
        context(session.Context()),
        stop(std::move(cancellation)) {}
  GltfPreset preset;
  geo::ExecutionContext context;
  std::stop_source stop;
  std::filesystem::path source;
  std::optional<std::filesystem::path> destination;
  ic::GltfFileReadOptions read_options;
  std::string input_id;
  std::string report_path;
  std::uint64_t primitive_ordinal = 0;
  ic::GltfExtractionRequest extraction;
  std::vector<DuplicateTarget> targets;
  std::vector<OwnedDeclaration> declarations;
  std::vector<RowLocalAttributeDeclaration> declaration_views;
  ic::GltfFileReadResult capture;
  GltfDuplicateFileWorkflowResult workflow;
  InvocationState state = InvocationState::kPrepared;
  bool workflow_started = false;
  GltfReportFailureRetention report_failure_retention =
      GltfReportFailureRetention::kRelease;
  void RequireComplete() const {
    if (state != InvocationState::kComplete)
      throw nb::value_error("glTF invocation has not completed");
  }
  const MaterializedDuplicateRepair* Repair() const noexcept {
    return workflow_started && workflow.record ? workflow.record->RepairFacts()
                                               : nullptr;
  }
};
static_assert(std::is_nothrow_move_constructible_v<ic::GltfFileReadResult>);
static_assert(std::is_nothrow_move_assignable_v<ic::GltfFileReadResult>);
static_assert(
    std::is_nothrow_move_constructible_v<GltfDuplicateFileWorkflowResult>);
static_assert(
    std::is_nothrow_move_assignable_v<GltfDuplicateFileWorkflowResult>);
static_assert(std::is_nothrow_move_constructible_v<GltfFileWorkflowResult>);
static_assert(std::is_nothrow_move_assignable_v<GltfFileWorkflowResult>);

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
nb::object Prepare(nb::handle source, nb::handle destination,
                   nb::handle targets, nb::handle primitive_ordinal,
                   nb::handle triangles, nb::handle values,
                   nb::handle processing_profile, nb::handle execution,
                   nb::handle input_id, nb::handle resource_root,
                   nb::handle row_local_attributes, nb::handle report_path,
                   nb::handle cancellation, nb::handle retain_failure_facts) {
  if (!PyBool_Check(retain_failure_facts.ptr()))
    throw nb::type_error("retain_failure_facts must be a bool");
  if (!nb::isinstance<GltfExecution>(execution))
    throw nb::type_error("execution must be a resolved GltfExecution session");
  const auto& session = nb::cast<const GltfExecution&>(execution);
  const auto profile = Text(processing_profile, kMaxGltfProcessingProfileBytes);
  ic::GltfProcessingProfile selected_profile;
  if (profile == kStaticGltfProcessingProfile)
    selected_profile = ic::GltfProcessingProfile::kStaticLocalTrianglesV1;
  else if (profile == kDirectTranslationGltfProcessingProfile)
    selected_profile = ic::GltfProcessingProfile::kDirectTranslationTrianglesV1;
  else
    throw nb::value_error("unknown glTF processing profile");
  if (selected_profile ==
          ic::GltfProcessingProfile::kDirectTranslationTrianglesV1 &&
      !session.SupportsDirectTranslation())
    throw nb::value_error(
        "execution preset does not support direct translation");
  std::stop_source stop;
  if (!cancellation.is_none()) {
    if (!nb::isinstance<GltfCancellation>(cancellation))
      throw nb::type_error("cancellation must be a GltfCancellation");
    stop = nb::cast<const GltfCancellation&>(cancellation).Source();
  }
  const auto result_type = nb::type<GltfFileWorkflowResult>();
#if defined(MESHVALE_REPAIR_GLTF_BINDING_TESTS)
  auto instance_scope = binding_test::BeforeInstanceAllocation();
#endif
  nb::object object = nb::inst_alloc(result_type);
  auto* result = new (nb::inst_ptr<GltfFileWorkflowResult>(object))
      GltfFileWorkflowResult(session, std::move(stop));
  nb::inst_mark_ready(object);
  result->extraction.processing_profile = selected_profile;
  if (retain_failure_facts.ptr() == Py_True)
    result->report_failure_retention =
        GltfReportFailureRetention::kRetainCompletedRepair;
  const auto& limits = result->preset;
  const auto path_limit =
      limits.capture_limits.files.max_native_path_code_units;
  result->source = Path(source, path_limit);
  if (!destination.is_none())
    result->destination = Path(destination, path_limit);
  if (!resource_root.is_none())
    result->read_options.resource_root = Path(resource_root, path_limit);
  result->primitive_ordinal = Index(primitive_ordinal);
  const auto triangle_policy =
      Text(triangles, limits.workflow_limits.max_report_path_bytes);
  if (triangle_policy == kRequireTrianglesPolicy)
    result->extraction.triangles = ic::GltfTriangleRequest::kRequireTriangles;
  else if (triangle_policy == kConvertStripOrFanPolicy)
    result->extraction.triangles = ic::GltfTriangleRequest::kConvertStripOrFan;
  else
    throw nb::value_error("unknown triangle policy");
  const auto value_policy =
      Text(values, limits.workflow_limits.max_report_path_bytes);
  if (value_policy == kRawValuesPolicy)
    result->extraction.values = ic::GltfValueRequest::kRaw;
  else if (value_policy == kRawAndNormalizedValuesPolicy)
    result->extraction.values = ic::GltfValueRequest::kRawAndNormalized;
  else
    throw nb::value_error("unknown value policy");
  result->input_id =
      Text(input_id, limits.workflow_limits.memory.max_input_id_bytes);
  CheckInputId(result->input_id);
  result->report_path =
      Text(report_path, limits.workflow_limits.max_report_path_bytes);
  BoundedItems(targets, limits.workflow_limits.memory.repair.max_targets,
               [&](nb::handle item) {
                 const auto [keep, remove] = Pair(item);
                 result->targets.push_back({Index(keep), Index(remove)});
               });
  std::uint64_t declaration_name_bytes = 0;
  BoundedItems(
      row_local_attributes,
      limits.workflow_limits.memory.repair.max_row_local_declarations,
      [&](nb::handle item) {
        const auto [domain, name] = Pair(item);
        const auto text = Text(
            domain,
            limits.workflow_limits.memory.repair.max_declaration_name_bytes);
        geo::AttributeDomain kind;
        if (text == kVertexDomainName)
          kind = geo::AttributeDomain::vertex;
        else if (text == kFaceDomainName)
          kind = geo::AttributeDomain::face;
        else if (text == kCornerDomainName)
          kind = geo::AttributeDomain::corner;
        else
          throw nb::value_error(
              "row-local domain must be vertex, face or corner");
        auto owned_name = Text(
            name,
            limits.workflow_limits.memory.repair.max_declaration_name_bytes -
                declaration_name_bytes);
        declaration_name_bytes += owned_name.size();
        if (owned_name.empty())
          throw nb::value_error("row-local name must not be empty");
        result->declarations.push_back({kind, std::move(owned_name)});
      });
  result->declaration_views.reserve(result->declarations.size());
  for (const auto& declaration : result->declarations)
    result->declaration_views.push_back({declaration.domain, declaration.name});
  return object;
}
nb::object Run(nb::handle prepared) {
  if (!nb::isinstance<GltfFileWorkflowResult>(prepared))
    throw nb::type_error("prepared glTF result required");
  auto* result = nb::inst_ptr<GltfFileWorkflowResult>(prepared);
  if (result->state != InvocationState::kPrepared)
    throw nb::value_error("prepared glTF invocation is single-use");
  nb::object returned = nb::borrow(prepared);
  result->state = InvocationState::kRunning;
  try {
#if defined(MESHVALE_REPAIR_GLTF_BINDING_TESTS)
    binding_test::ObserveStopSource(result->stop);
#endif
    {
      nb::gil_scoped_release release;
      result->capture = ic::ReadGltfFileAsset(
          result->source, result->read_options, result->preset.capture_limits,
          result->preset.capture_work, result->context,
          result->stop.get_token());
      if (result->capture.asset) {
        std::optional<GltfDuplicateFilePublication> publication;
        if (result->destination)
          publication.emplace(*result->destination, result->report_path,
                              std::span<const ic::GltfFileView>{});
        const GltfDuplicateFileWorkflowRequest request{
            *result->capture.asset,
            result->input_id,
            result->primitive_ordinal,
            result->extraction,
            result->targets,
            {result->declaration_views},
            publication ? &*publication : nullptr,
            result->report_failure_retention};
        result->workflow_started = true;
#if defined(MESHVALE_REPAIR_GLTF_BINDING_TESTS)
        auto native_result =
            gltf_file_workflow_detail::RunGltfDuplicateFileWorkflowForTest(
                request, result->preset.workflow_limits,
                result->preset.workflow_work, result->context,
                result->stop.get_token(), binding_test::WorkflowOptions());
        binding_test::AfterNativeReturn();
#else
        auto native_result = RunGltfDuplicateFileWorkflow(
            request, result->preset.workflow_limits,
            result->preset.workflow_work, result->context,
            result->stop.get_token());
#endif
        result->workflow = std::move(native_result);
      }
    }
#if defined(MESHVALE_REPAIR_GLTF_BINDING_TESTS)
    binding_test::BeforeReturn(prepared);
#endif
    result->state = InvocationState::kComplete;
    return returned;
  } catch (...) {
#if defined(MESHVALE_REPAIR_GLTF_BINDING_TESTS)
    binding_test::Restore();
#endif
    result->state = InvocationState::kFailed;
    throw;
  }
}
nb::object Map(const GltfFileWorkflowResult& result,
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
    const GltfFileWorkflowResult& result,
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
nb::dict PresetObservation(const ic::GltfDirectTranslationLimits& value);
nb::dict PresetObservation(const ic::GltfDirectTranslationWorkPolicy& value);
nb::dict PresetObservation(const geo::ExecutionOptions& value);
nb::dict PresetObservation(const ic::GltfFileLimits& value);
nb::dict PresetObservation(const ic::GltfReadLimits& value);
nb::dict PresetObservation(const ic::GltfReadOperationLimits& value);
nb::dict PresetObservation(const ic::GlbLimits& value);
nb::dict PresetObservation(const ic::GltfFileReadLimits& value);
nb::dict PresetObservation(const ic::GltfExtractionLimits& value);
nb::dict PresetObservation(const geo::MeshMaterializationLimits& value);
nb::dict PresetObservation(const geo::MeshInspectionLimits& value);
nb::dict PresetObservation(const ic::GltfMaterializationLimits& value);
nb::dict PresetObservation(const MaterializedDuplicateLimits& value);
nb::dict PresetObservation(const ic::GltfWriteLimits& value);
nb::dict PresetObservation(const ic::GltfTopologyWriteLimits& value);
nb::dict PresetObservation(const ic::GltfTopologyOutputInspectionLimits& value);
nb::dict PresetObservation(const GltfDuplicateFileMemoryLimits& value);
nb::dict PresetObservation(const ic::GltfFilePublicationLimits& value);
nb::dict PresetObservation(const GltfDuplicateFileWorkflowLimits& value);
nb::dict PresetObservation(const ic::GltfFileWorkPolicy& value);
nb::dict PresetObservation(const ic::GltfWorkPolicy& value);
nb::dict PresetObservation(const ic::GlbWorkPolicy& value);
nb::dict PresetObservation(const ic::GltfFileReadWorkPolicy& value);
nb::dict PresetObservation(const geo::MeshMaterializationWorkPolicy& value);
nb::dict PresetObservation(const ic::GltfTopologyWriteWorkPolicy& value);
nb::dict PresetObservation(
    const ic::GltfTopologyOutputInspectionWorkPolicy& value);
nb::dict PresetObservation(const GltfWorkflowByteWorkPolicy& value);
nb::dict PresetObservation(const GltfDuplicateWorkflowWorkPolicy& value);
nb::dict PresetObservation(const ic::GltfFilePublicationWorkPolicy& value);
nb::dict PresetObservation(const GltfDuplicateFileWorkflowWorkPolicy& value);
nb::dict PresetObservation(const GltfPreset& value);
template <class T>
nb::object PresetValue(const T& value) {
  if constexpr (std::is_arithmetic_v<T>)
    return nb::cast(value);
  else
    return PresetObservation(value);
}
nb::dict PresetObservation(const ic::GltfDirectTranslationLimits& value) {
  nb::dict result;
  result[GltfTranslationFields::kMaxNodeRows] =
      PresetValue(value.max_node_rows);
  result[GltfTranslationFields::kMaxMeshUsers] =
      PresetValue(value.max_mesh_users);
  result[GltfTranslationFields::kMaxChannelReferences] =
      PresetValue(value.max_channel_references);
  result[GltfTranslationFields::kMaxDistinctSamplers] =
      PresetValue(value.max_distinct_samplers);
  result[GltfTranslationFields::kMaxKeysPerSampler] =
      PresetValue(value.max_keys_per_sampler);
  result[GltfTranslationFields::kMaxTotalKeys] =
      PresetValue(value.max_total_keys);
  result[GltfTranslationFields::kMaxRelationshipSteps] =
      PresetValue(value.max_relationship_steps);
  result[GltfTranslationFields::kMaxMetadataBytes] =
      PresetValue(value.max_metadata_bytes);
  result[GltfTranslationFields::kMaxDecodedBytes] =
      PresetValue(value.max_decoded_bytes);
  result[GltfTranslationFields::kMaxNewRequestedBytes] =
      PresetValue(value.max_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfDirectTranslationWorkPolicy& value) {
  nb::dict result;
  result[GltfTranslationFields::kParallelMinKeys] =
      PresetValue(value.parallel_min_keys);
  result[GltfTranslationFields::kPartitionKeys] =
      PresetValue(value.partition_keys);
  result[GltfTranslationFields::kCancellationBlockKeys] =
      PresetValue(value.cancellation_block_keys);
  result[GltfTranslationFields::kMaxJobs] = PresetValue(value.max_jobs);
  return result;
}
nb::dict PresetObservation(const geo::ExecutionOptions& value) {
  nb::dict result;
  result["worker_budget"] = PresetValue(value.worker_budget);
  result["minimum_parallel_vertices"] =
      PresetValue(value.minimum_parallel_vertices);
  result["tracked_payload_budget_bytes"] =
      PresetValue(value.tracked_payload_budget_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfFileLimits& value) {
  nb::dict result;
  result["max_files"] = PresetValue(value.max_files);
  result["max_resource_bindings"] = PresetValue(value.max_resource_bindings);
  result["max_file_bytes"] = PresetValue(value.max_file_bytes);
  result["max_total_file_bytes"] = PresetValue(value.max_total_file_bytes);
  result["max_path_bytes"] = PresetValue(value.max_path_bytes);
  result["max_total_path_bytes"] = PresetValue(value.max_total_path_bytes);
  result["max_native_path_code_units"] =
      PresetValue(value.max_native_path_code_units);
  result["max_new_requested_bytes"] =
      PresetValue(value.max_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfReadLimits& value) {
  nb::dict result;
  result["max_json_bytes"] = PresetValue(value.max_json_bytes);
  result["max_resource_bytes"] = PresetValue(value.max_resource_bytes);
  result["max_owned_source_resource_bytes"] =
      PresetValue(value.max_owned_source_resource_bytes);
  result["max_document_nodes"] = PresetValue(value.max_document_nodes);
  result["max_document_depth"] = PresetValue(value.max_document_depth);
  result["max_decoded_string_bytes"] =
      PresetValue(value.max_decoded_string_bytes);
  result["max_numeric_token_bytes"] =
      PresetValue(value.max_numeric_token_bytes);
  result["max_decimal_coefficient_digits"] =
      PresetValue(value.max_decimal_coefficient_digits);
  result["max_decimal_exponent_digits"] =
      PresetValue(value.max_decimal_exponent_digits);
  result["max_table_rows"] = PresetValue(value.max_table_rows);
  result["max_graph_edges"] = PresetValue(value.max_graph_edges);
  result["max_instances"] = PresetValue(value.max_instances);
  result["max_accessor_elements"] = PresetValue(value.max_accessor_elements);
  result["max_projection_bytes"] = PresetValue(value.max_projection_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfReadOperationLimits& value) {
  nb::dict result;
  result["source"] = PresetValue(value.source);
  result["max_new_requested_bytes"] =
      PresetValue(value.max_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GlbLimits& value) {
  nb::dict result;
  result["max_container_bytes"] = PresetValue(value.max_container_bytes);
  result["max_chunks"] = PresetValue(value.max_chunks);
  result["max_requested_bytes"] = PresetValue(value.max_requested_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfFileReadLimits& value) {
  nb::dict result;
  result["files"] = PresetValue(value.files);
  result["asset"] = PresetValue(value.asset);
  result["container"] = PresetValue(value.container);
  return result;
}
nb::dict PresetObservation(const ic::GltfExtractionLimits& value) {
  nb::dict result;
  result["max_components"] = PresetValue(value.max_components);
  result["max_triangle_corners"] = PresetValue(value.max_triangle_corners);
  result["max_output_requested_bytes"] =
      PresetValue(value.max_output_requested_bytes);
  if (value.direct_translation.max_new_requested_bytes != 0)
    result[GltfTranslationFields::kDirectTranslation] =
        PresetValue(value.direct_translation);
  return result;
}
nb::dict PresetObservation(const geo::MeshMaterializationLimits& value) {
  nb::dict result;
  result["max_vertices"] = PresetValue(value.max_vertices);
  result["max_faces"] = PresetValue(value.max_faces);
  result["max_corners"] = PresetValue(value.max_corners);
  result["max_face_offsets"] = PresetValue(value.max_face_offsets);
  result["max_attributes"] = PresetValue(value.max_attributes);
  result["max_attribute_scalars"] = PresetValue(value.max_attribute_scalars);
  result["max_attribute_offsets"] = PresetValue(value.max_attribute_offsets);
  result["max_presence_bytes"] = PresetValue(value.max_presence_bytes);
  result["max_metadata_entries"] = PresetValue(value.max_metadata_entries);
  result["max_descriptor_bytes"] = PresetValue(value.max_descriptor_bytes);
  result["max_new_requested_bytes"] =
      PresetValue(value.max_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const geo::MeshInspectionLimits& value) {
  nb::dict result;
  result["max_issues"] = PresetValue(value.max_issues);
  result["max_new_requested_bytes"] =
      PresetValue(value.max_new_requested_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfMaterializationLimits& value) {
  nb::dict result;
  result["max_channels"] = PresetValue(value.max_channels);
  result["max_bridge_new_requested_bytes"] =
      PresetValue(value.max_bridge_new_requested_bytes);
  result["materialization"] = PresetValue(value.materialization);
  result["inspection"] = PresetValue(value.inspection);
  return result;
}
nb::dict PresetObservation(const MaterializedDuplicateLimits& value) {
  nb::dict result;
  result["max_targets"] = PresetValue(value.max_targets);
  result["max_row_local_declarations"] =
      PresetValue(value.max_row_local_declarations);
  result["max_declaration_name_bytes"] =
      PresetValue(value.max_declaration_name_bytes);
  result["max_comparison_steps_per_target"] =
      PresetValue(value.max_comparison_steps_per_target);
  result["max_new_requested_bytes"] =
      PresetValue(value.max_new_requested_bytes);
  result["inspection"] = PresetValue(value.inspection);
  result["selection"] = PresetValue(value.selection);
  return result;
}
nb::dict PresetObservation(const ic::GltfWriteLimits& value) {
  nb::dict result;
  result["max_json_bytes"] = PresetValue(value.max_json_bytes);
  result["max_resources"] = PresetValue(value.max_resources);
  result["max_resource_bytes"] = PresetValue(value.max_resource_bytes);
  result["max_total_resource_bytes"] =
      PresetValue(value.max_total_resource_bytes);
  result["max_output_requested_bytes"] =
      PresetValue(value.max_output_requested_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfTopologyWriteLimits& value) {
  nb::dict result;
  result["max_source_json_bytes"] = PresetValue(value.max_source_json_bytes);
  result["max_source_json_nodes"] = PresetValue(value.max_source_json_nodes);
  result["max_source_json_depth"] = PresetValue(value.max_source_json_depth);
  result["max_vertices"] = PresetValue(value.max_vertices);
  result["max_channels"] = PresetValue(value.max_channels);
  result["max_attribute_scalars"] = PresetValue(value.max_attribute_scalars);
  result["max_descriptor_bytes"] = PresetValue(value.max_descriptor_bytes);
  result["max_candidate_faces"] = PresetValue(value.max_candidate_faces);
  result["max_candidate_corners"] = PresetValue(value.max_candidate_corners);
  result["max_index_bytes"] = PresetValue(value.max_index_bytes);
  result["output"] = PresetValue(value.output);
  if (value.direct_translation.max_new_requested_bytes != 0)
    result[GltfTranslationFields::kDirectTranslation] =
        PresetValue(value.direct_translation);
  return result;
}
nb::dict PresetObservation(
    const ic::GltfTopologyOutputInspectionLimits& value) {
  nb::dict result;
  result["max_json_bytes"] = PresetValue(value.max_json_bytes);
  result["max_json_nodes"] = PresetValue(value.max_json_nodes);
  result["max_json_depth"] = PresetValue(value.max_json_depth);
  result["max_resources"] = PresetValue(value.max_resources);
  result["max_resource_bytes"] = PresetValue(value.max_resource_bytes);
  result["max_vertices"] = PresetValue(value.max_vertices);
  result["max_channels"] = PresetValue(value.max_channels);
  result["max_attribute_scalars"] = PresetValue(value.max_attribute_scalars);
  result["max_descriptor_bytes"] = PresetValue(value.max_descriptor_bytes);
  result["max_candidate_faces"] = PresetValue(value.max_candidate_faces);
  result["max_candidate_corners"] = PresetValue(value.max_candidate_corners);
  result["max_index_bytes"] = PresetValue(value.max_index_bytes);
  result["max_new_requested_bytes"] =
      PresetValue(value.max_new_requested_bytes);
  if (value.direct_translation.max_new_requested_bytes != 0)
    result[GltfTranslationFields::kDirectTranslation] =
        PresetValue(value.direct_translation);
  return result;
}
nb::dict PresetObservation(const GltfDuplicateFileMemoryLimits& value) {
  nb::dict result;
  result["max_new_requested_bytes"] =
      PresetValue(value.max_new_requested_bytes);
  result["max_input_id_bytes"] = PresetValue(value.max_input_id_bytes);
  result["max_source_protection_bytes"] =
      PresetValue(value.max_source_protection_bytes);
  result["max_source_binding_records"] =
      PresetValue(value.max_source_binding_records);
  result["max_reload_bindings"] = PresetValue(value.max_reload_bindings);
  result["max_report_bytes"] = PresetValue(value.max_report_bytes);
  result["extraction"] = PresetValue(value.extraction);
  result["materialization"] = PresetValue(value.materialization);
  result["repair"] = PresetValue(value.repair);
  result["writing"] = PresetValue(value.writing);
  result["reload"] = PresetValue(value.reload);
  result["reload_extraction"] = PresetValue(value.reload_extraction);
  result["verification"] = PresetValue(value.verification);
  return result;
}
nb::dict PresetObservation(const ic::GltfFilePublicationLimits& value) {
  nb::dict result;
  result["files"] = PresetValue(value.files);
  result["reload"] = PresetValue(value.reload);
  result["extraction"] = PresetValue(value.extraction);
  result["inspection"] = PresetValue(value.inspection);
  result["container"] = PresetValue(value.container);
  return result;
}
nb::dict PresetObservation(const GltfDuplicateFileWorkflowLimits& value) {
  nb::dict result;
  result["memory"] = PresetValue(value.memory);
  result["max_wrapper_new_requested_bytes"] =
      PresetValue(value.max_wrapper_new_requested_bytes);
  result["max_source_files"] = PresetValue(value.max_source_files);
  result["max_supplemental_files"] = PresetValue(value.max_supplemental_files);
  result["max_report_path_bytes"] = PresetValue(value.max_report_path_bytes);
  result["max_report_bytes"] = PresetValue(value.max_report_bytes);
  result["max_hash_input_bytes"] = PresetValue(value.max_hash_input_bytes);
  result["publication"] = PresetValue(value.publication);
  return result;
}
nb::dict PresetObservation(const ic::GltfFileWorkPolicy& value) {
  nb::dict result;
  result["io_block_bytes"] = PresetValue(value.io_block_bytes);
  result["parallel_min_bytes"] = PresetValue(value.parallel_min_bytes);
  result["partition_bytes"] = PresetValue(value.partition_bytes);
  result["cancellation_block_bytes"] =
      PresetValue(value.cancellation_block_bytes);
  result["max_jobs"] = PresetValue(value.max_jobs);
  return result;
}
nb::dict PresetObservation(const ic::GltfWorkPolicy& value) {
  nb::dict result;
  result["parallel_min_resource_bytes"] =
      PresetValue(value.parallel_min_resource_bytes);
  result["resource_partition_bytes"] =
      PresetValue(value.resource_partition_bytes);
  result["parallel_min_channel_components"] =
      PresetValue(value.parallel_min_channel_components);
  result["channel_partition_elements"] =
      PresetValue(value.channel_partition_elements);
  if (value.direct_translation.max_jobs != 0)
    result[GltfTranslationFields::kDirectTranslation] =
        PresetValue(value.direct_translation);
  return result;
}
nb::dict PresetObservation(const ic::GlbWorkPolicy& value) {
  nb::dict result;
  result["parallel_min_bytes"] = PresetValue(value.parallel_min_bytes);
  result["partition_bytes"] = PresetValue(value.partition_bytes);
  return result;
}
nb::dict PresetObservation(const ic::GltfFileReadWorkPolicy& value) {
  nb::dict result;
  result["files"] = PresetValue(value.files);
  result["asset"] = PresetValue(value.asset);
  result["container"] = PresetValue(value.container);
  return result;
}
nb::dict PresetObservation(const geo::MeshMaterializationWorkPolicy& value) {
  nb::dict result;
  result["parallel_min_bytes"] = PresetValue(value.parallel_min_bytes);
  result["partition_bytes"] = PresetValue(value.partition_bytes);
  result["cancellation_block_scalars"] =
      PresetValue(value.cancellation_block_scalars);
  result["max_jobs"] = PresetValue(value.max_jobs);
  return result;
}
nb::dict PresetObservation(const ic::GltfTopologyWriteWorkPolicy& value) {
  nb::dict result;
  result["parallel_min_bytes"] = PresetValue(value.parallel_min_bytes);
  result["partition_bytes"] = PresetValue(value.partition_bytes);
  result["cancellation_block_bytes"] =
      PresetValue(value.cancellation_block_bytes);
  result["max_jobs"] = PresetValue(value.max_jobs);
  if (value.direct_translation.max_jobs != 0)
    result[GltfTranslationFields::kDirectTranslation] =
        PresetValue(value.direct_translation);
  return result;
}
nb::dict PresetObservation(
    const ic::GltfTopologyOutputInspectionWorkPolicy& value) {
  nb::dict result;
  result["parallel_min_bytes"] = PresetValue(value.parallel_min_bytes);
  result["partition_bytes"] = PresetValue(value.partition_bytes);
  result["cancellation_block_bytes"] =
      PresetValue(value.cancellation_block_bytes);
  result["max_jobs"] = PresetValue(value.max_jobs);
  if (value.direct_translation.max_jobs != 0)
    result[GltfTranslationFields::kDirectTranslation] =
        PresetValue(value.direct_translation);
  return result;
}
nb::dict PresetObservation(const GltfWorkflowByteWorkPolicy& value) {
  nb::dict result;
  result["parallel_min_bytes"] = PresetValue(value.parallel_min_bytes);
  result["partition_bytes"] = PresetValue(value.partition_bytes);
  result["cancellation_block_bytes"] =
      PresetValue(value.cancellation_block_bytes);
  result["max_jobs"] = PresetValue(value.max_jobs);
  return result;
}
nb::dict PresetObservation(const GltfDuplicateWorkflowWorkPolicy& value) {
  nb::dict result;
  result["interchange"] = PresetValue(value.interchange);
  result["geometry"] = PresetValue(value.geometry);
  result["writing"] = PresetValue(value.writing);
  result["verification"] = PresetValue(value.verification);
  result["report"] = PresetValue(value.report);
  return result;
}
nb::dict PresetObservation(const ic::GltfFilePublicationWorkPolicy& value) {
  nb::dict result;
  result["files"] = PresetValue(value.files);
  result["reload"] = PresetValue(value.reload);
  result["inspection"] = PresetValue(value.inspection);
  result["container"] = PresetValue(value.container);
  return result;
}
nb::dict PresetObservation(const GltfDuplicateFileWorkflowWorkPolicy& value) {
  nb::dict result;
  result["memory"] = PresetValue(value.memory);
  result["hashing"] = PresetValue(value.hashing);
  result["report"] = PresetValue(value.report);
  result["publication"] = PresetValue(value.publication);
  return result;
}
nb::dict PresetObservation(const GltfPreset& value) {
  nb::dict result;
  result["execution"] = PresetObservation(value.execution);
  result["capture_limits"] = PresetObservation(value.capture_limits);
  result["capture_work"] = PresetObservation(value.capture_work);
  result["workflow_limits"] = PresetObservation(value.workflow_limits);
  result["workflow_work"] = PresetObservation(value.workflow_work);
  return result;
}
nb::object TranslationSpan(const std::optional<ic::GltfSourceSpan>& value) {
  if (!value) return nb::none();
  nb::dict result;
  result[GltfTranslationFields::kOffset] = value->offset;
  result[GltfTranslationFields::kLength] = value->length;
  return result;
}
nb::dict TranslationDiagnostic(const ic::GltfDiagnostic& value) {
  nb::dict result;
  result[GltfTranslationFields::kStatus] =
      static_cast<std::uint32_t>(value.status);
  result[GltfTranslationFields::kCode] = static_cast<std::uint32_t>(value.code);
  result[GltfTranslationFields::kJsonSpan] = TranslationSpan(value.json_span);
  result[GltfTranslationFields::kResourceKind] =
      value.resource_kind
          ? nb::cast(static_cast<std::uint32_t>(*value.resource_kind))
          : nb::none();
  result[GltfTranslationFields::kResourceIndex] =
      value.resource_index ? nb::cast(*value.resource_index) : nb::none();
  result[GltfTranslationFields::kResourceSpan] =
      TranslationSpan(value.resource_span);
  result[GltfTranslationFields::kRuleId] = value.rule_id;
  return result;
}
nb::object TranslationObservation(
    const std::optional<ic::GltfDirectTranslationObservation>& value) {
  if (!value) return nb::none();
  nb::dict result;
  result[GltfTranslationFields::kStatus] =
      static_cast<std::uint32_t>(value->status);
  result[GltfTranslationFields::kDiagnostic] =
      TranslationDiagnostic(value->diagnostic);
  result[GltfTranslationFields::kComplete] = value->complete;
  result[GltfTranslationFields::kNodeRows] = value->node_rows;
  result[GltfTranslationFields::kMeshUsers] = value->mesh_users;
  result[GltfTranslationFields::kScannedChannels] = value->scanned_channels;
  result[GltfTranslationFields::kAffectedChannels] = value->affected_channels;
  result[GltfTranslationFields::kDistinctSamplers] = value->distinct_samplers;
  result[GltfTranslationFields::kKeysChecked] = value->keys_checked;
  result[GltfTranslationFields::kDecodedBytesChecked] =
      value->decoded_bytes_checked;
  result[GltfTranslationFields::kRelationshipSteps] = value->relationship_steps;
  result[GltfTranslationFields::kNewRequestedBytes] =
      value->new_requested_bytes;
  result[GltfTranslationFields::kEffectiveRequestCap] =
      value->effective_request_cap;
  result[GltfTranslationFields::kWork] = PresetObservation(value->work);
  nb::dict execution;
  execution[GltfTranslationFields::kPlannedJobs] =
      value->execution.planned_jobs;
  execution[GltfTranslationFields::kLaunchedWorkers] =
      value->execution.launched_workers;
  execution[GltfTranslationFields::kJoinedWorkers] =
      value->execution.joined_workers;
  execution[GltfTranslationFields::kCallerJobs] = value->execution.caller_jobs;
  result[GltfTranslationFields::kExecution] = std::move(execution);
  return result;
}
nb::object TranslationFacts(const GltfFileWorkflowResult& value) {
  value.RequireComplete();
  if (value.extraction.processing_profile ==
      ic::GltfProcessingProfile::kStaticLocalTrianglesV1)
    return nb::none();
  const auto& facts = value.workflow.translation;
  nb::dict result;
  result[GltfTranslationFields::kProfile] =
      nb::str(kDirectTranslationGltfProcessingProfile.data());
  result[GltfTranslationFields::kSource] = TranslationObservation(facts.source);
  result[GltfTranslationFields::kWriting] =
      TranslationObservation(facts.writing);
  result[GltfTranslationFields::kReload] = TranslationObservation(facts.reload);
  result[GltfTranslationFields::kVerification] =
      TranslationObservation(facts.verification);
  result[GltfTranslationFields::kPublicationReload] =
      TranslationObservation(facts.publication_reload);
  result[GltfTranslationFields::kPublicationVerification] =
      TranslationObservation(facts.publication_verification);
  return result;
}
}  // namespace
void RegisterGltfBindings(nb::module_& module) {
  nb::enum_<CandidateOutcome>(module, "CandidateOutcome")
      .value("accepted", CandidateOutcome::accepted)
      .value("unchanged", CandidateOutcome::unchanged)
      .value("rejected", CandidateOutcome::rejected);
  nb::enum_<GltfContainerCounterAvailability>(
      module, "GltfContainerCounterAvailability")
      .value("kNotApplicable", GltfContainerCounterAvailability::kNotApplicable)
      .value("kUnavailable", GltfContainerCounterAvailability::kUnavailable);
  nb::enum_<GltfFileWorkflowDiagnosticCode>(module,
                                            "GltfFileWorkflowDiagnosticCode")
      .value("kNone", GltfFileWorkflowDiagnosticCode::kNone)
      .value("kRequiredFieldZero",
             GltfFileWorkflowDiagnosticCode::kRequiredFieldZero)
      .value("kConfigurationExtent",
             GltfFileWorkflowDiagnosticCode::kConfigurationExtent)
      .value("kSourceLedger", GltfFileWorkflowDiagnosticCode::kSourceLedger)
      .value("kSourceAccounting",
             GltfFileWorkflowDiagnosticCode::kSourceAccounting)
      .value("kCountLimit", GltfFileWorkflowDiagnosticCode::kCountLimit)
      .value("kRequestBudget", GltfFileWorkflowDiagnosticCode::kRequestBudget)
      .value("kStopRequested", GltfFileWorkflowDiagnosticCode::kStopRequested)
      .value("kMemory", GltfFileWorkflowDiagnosticCode::kMemory)
      .value("kHashInputExtent",
             GltfFileWorkflowDiagnosticCode::kHashInputExtent)
      .value("kReportPreparation",
             GltfFileWorkflowDiagnosticCode::kReportPreparation)
      .value("kPublication", GltfFileWorkflowDiagnosticCode::kPublication);
  nb::enum_<GltfFileWorkflowPublicationOutcome>(
      module, "GltfFileWorkflowPublicationOutcome")
      .value("kNotRequested", GltfFileWorkflowPublicationOutcome::kNotRequested)
      .value("kNotAttempted", GltfFileWorkflowPublicationOutcome::kNotAttempted)
      .value("kFailed", GltfFileWorkflowPublicationOutcome::kFailed)
      .value("kCanceled", GltfFileWorkflowPublicationOutcome::kCanceled)
      .value("kPublished", GltfFileWorkflowPublicationOutcome::kPublished);
  nb::enum_<GltfFileWorkflowStage>(module, "GltfFileWorkflowStage")
      .value("kConfiguration", GltfFileWorkflowStage::kConfiguration)
      .value("kSource", GltfFileWorkflowStage::kSource)
      .value("kMemory", GltfFileWorkflowStage::kMemory)
      .value("kHash", GltfFileWorkflowStage::kHash)
      .value("kReport", GltfFileWorkflowStage::kReport)
      .value("kPublication", GltfFileWorkflowStage::kPublication)
      .value("kComplete", GltfFileWorkflowStage::kComplete);
  nb::enum_<GltfWorkflowDiagnosticCode>(module, "GltfWorkflowDiagnosticCode")
      .value("kNone", GltfWorkflowDiagnosticCode::kNone)
      .value("kRequiredFieldZero",
             GltfWorkflowDiagnosticCode::kRequiredFieldZero)
      .value("kConfigurationExtent",
             GltfWorkflowDiagnosticCode::kConfigurationExtent)
      .value("kInputIdentity", GltfWorkflowDiagnosticCode::kInputIdentity)
      .value("kCountLimit", GltfWorkflowDiagnosticCode::kCountLimit)
      .value("kRequestBudget", GltfWorkflowDiagnosticCode::kRequestBudget)
      .value("kStopRequested", GltfWorkflowDiagnosticCode::kStopRequested)
      .value("kPrimitiveSelection",
             GltfWorkflowDiagnosticCode::kPrimitiveSelection)
      .value("kSourceAccounting", GltfWorkflowDiagnosticCode::kSourceAccounting)
      .value("kMaterializationAllowance",
             GltfWorkflowDiagnosticCode::kMaterializationAllowance)
      .value("kRead", GltfWorkflowDiagnosticCode::kRead)
      .value("kExtract", GltfWorkflowDiagnosticCode::kExtract)
      .value("kMaterialize", GltfWorkflowDiagnosticCode::kMaterialize)
      .value("kRepair", GltfWorkflowDiagnosticCode::kRepair)
      .value("kWrite", GltfWorkflowDiagnosticCode::kWrite)
      .value("kReload", GltfWorkflowDiagnosticCode::kReload)
      .value("kReloadExtract", GltfWorkflowDiagnosticCode::kReloadExtract)
      .value("kSourceChanged", GltfWorkflowDiagnosticCode::kSourceChanged)
      .value("kReloadMismatch", GltfWorkflowDiagnosticCode::kReloadMismatch)
      .value("kPrimitiveIdentityMismatch",
             GltfWorkflowDiagnosticCode::kPrimitiveIdentityMismatch)
      .value("kTopologyMismatch", GltfWorkflowDiagnosticCode::kTopologyMismatch)
      .value("kChannelDescriptorMismatch",
             GltfWorkflowDiagnosticCode::kChannelDescriptorMismatch)
      .value("kChannelBitsMismatch",
             GltfWorkflowDiagnosticCode::kChannelBitsMismatch)
      .value("kResourceBytesMismatch",
             GltfWorkflowDiagnosticCode::kResourceBytesMismatch)
      .value("kRetainedJsonMismatch",
             GltfWorkflowDiagnosticCode::kRetainedJsonMismatch)
      .value("kBlockedByEarlierStage",
             GltfWorkflowDiagnosticCode::kBlockedByEarlierStage)
      .value("kSolidCheckUnavailable",
             GltfWorkflowDiagnosticCode::kSolidCheckUnavailable)
      .value("kReportExtent", GltfWorkflowDiagnosticCode::kReportExtent)
      .value("kReportStringEncoding",
             GltfWorkflowDiagnosticCode::kReportStringEncoding);
  nb::enum_<GltfWorkflowExecutionOutcome>(module,
                                          "GltfWorkflowExecutionOutcome")
      .value("kCompleted", GltfWorkflowExecutionOutcome::kCompleted)
      .value("kFailed", GltfWorkflowExecutionOutcome::kFailed)
      .value("kCanceled", GltfWorkflowExecutionOutcome::kCanceled);
  nb::enum_<GltfWorkflowExitMeaning>(module, "GltfWorkflowExitMeaning")
      .value("kSuccess", GltfWorkflowExitMeaning::kSuccess)
      .value("kRejectedOrIncomplete",
             GltfWorkflowExitMeaning::kRejectedOrIncomplete)
      .value("kExecutionFailure", GltfWorkflowExitMeaning::kExecutionFailure)
      .value("kCanceled", GltfWorkflowExitMeaning::kCanceled);
  nb::enum_<GltfWorkflowProfileOutcome>(module, "GltfWorkflowProfileOutcome")
      .value("kPassed", GltfWorkflowProfileOutcome::kPassed)
      .value("kFailed", GltfWorkflowProfileOutcome::kFailed)
      .value("kIncomplete", GltfWorkflowProfileOutcome::kIncomplete);
  nb::enum_<GltfWorkflowStage>(module, "GltfWorkflowStage")
      .value("kConfiguration", GltfWorkflowStage::kConfiguration)
      .value("kRead", GltfWorkflowStage::kRead)
      .value("kExtract", GltfWorkflowStage::kExtract)
      .value("kMaterialize", GltfWorkflowStage::kMaterialize)
      .value("kRepair", GltfWorkflowStage::kRepair)
      .value("kWrite", GltfWorkflowStage::kWrite)
      .value("kReload", GltfWorkflowStage::kReload)
      .value("kReloadExtract", GltfWorkflowStage::kReloadExtract)
      .value("kVerify", GltfWorkflowStage::kVerify)
      .value("kReport", GltfWorkflowStage::kReport)
      .value("kComplete", GltfWorkflowStage::kComplete);
  nb::enum_<GltfWorkflowStatus>(module, "GltfWorkflowStatus")
      .value("kSuccess", GltfWorkflowStatus::kSuccess)
      .value("kInvalidConfiguration", GltfWorkflowStatus::kInvalidConfiguration)
      .value("kLimitExceeded", GltfWorkflowStatus::kLimitExceeded)
      .value("kUnsupported", GltfWorkflowStatus::kUnsupported)
      .value("kBudgetExceeded", GltfWorkflowStatus::kBudgetExceeded)
      .value("kCanceled", GltfWorkflowStatus::kCanceled)
      .value("kRejected", GltfWorkflowStatus::kRejected)
      .value("kDelegatedFailure", GltfWorkflowStatus::kDelegatedFailure)
      .value("kSourceContractFailure",
             GltfWorkflowStatus::kSourceContractFailure)
      .value("kVerificationFailure", GltfWorkflowStatus::kVerificationFailure);
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
      .value("kTargetLimit", MaterializedDuplicateDiagnosticCode::kTargetLimit)
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
      .value("kTargetRange", MaterializedDuplicateDiagnosticCode::kTargetRange)
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
  nb::enum_<MaterializedDuplicateStage>(module, "MaterializedDuplicateStage")
      .value("kConfiguration", MaterializedDuplicateStage::kConfiguration)
      .value("kSourceInspection", MaterializedDuplicateStage::kSourceInspection)
      .value("kEligibility", MaterializedDuplicateStage::kEligibility)
      .value("kTargets", MaterializedDuplicateStage::kTargets)
      .value("kComparison", MaterializedDuplicateStage::kComparison)
      .value("kSelection", MaterializedDuplicateStage::kSelection)
      .value("kCorrespondence", MaterializedDuplicateStage::kCorrespondence)
      .value("kCandidateInspection",
             MaterializedDuplicateStage::kCandidateInspection)
      .value("kVerification", MaterializedDuplicateStage::kVerification)
      .value("kPublication", MaterializedDuplicateStage::kPublication);
  nb::enum_<MaterializedDuplicateStatus>(module, "MaterializedDuplicateStatus")
      .value("kSuccess", MaterializedDuplicateStatus::kSuccess)
      .value("kInvalidConfiguration",
             MaterializedDuplicateStatus::kInvalidConfiguration)
      .value("kLimitExceeded", MaterializedDuplicateStatus::kLimitExceeded)
      .value("kBudgetExceeded", MaterializedDuplicateStatus::kBudgetExceeded)
      .value("kCanceled", MaterializedDuplicateStatus::kCanceled)
      .value("kInvalidSourceStorage",
             MaterializedDuplicateStatus::kInvalidSourceStorage)
      .value("kRejected", MaterializedDuplicateStatus::kRejected)
      .value("kGeometryFailure", MaterializedDuplicateStatus::kGeometryFailure)
      .value("kVerificationFailure",
             MaterializedDuplicateStatus::kVerificationFailure);
  nb::enum_<geo::MaterializationDiagnosticCode>(module,
                                                "MaterializationDiagnosticCode")
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
  nb::enum_<geo::MaterializationStatus>(module, "MaterializationStatus")
      .value("kSuccess", geo::MaterializationStatus::kSuccess)
      .value("kInvalidConfiguration",
             geo::MaterializationStatus::kInvalidConfiguration)
      .value("kInvalidTransport", geo::MaterializationStatus::kInvalidTransport)
      .value("kLimitExceeded", geo::MaterializationStatus::kLimitExceeded)
      .value("kUnsupportedAllocationProfile",
             geo::MaterializationStatus::kUnsupportedAllocationProfile)
      .value("kBudgetExceeded", geo::MaterializationStatus::kBudgetExceeded)
      .value("kCanceled", geo::MaterializationStatus::kCanceled)
      .value("kInvalidSourceStorage",
             geo::MaterializationStatus::kInvalidSourceStorage)
      .value("kInvalidSelection",
             geo::MaterializationStatus::kInvalidSelection);
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
      .value("kAttributeNameEmpty", geo::StorageIssueCode::kAttributeNameEmpty)
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
  nb::enum_<ic::GlbStatus>(module, "GlbStatus")
      .value("kSuccess", ic::GlbStatus::kSuccess)
      .value("kInvalidOptions", ic::GlbStatus::kInvalidOptions)
      .value("kMalformed", ic::GlbStatus::kMalformed)
      .value("kUnsupportedVersion", ic::GlbStatus::kUnsupportedVersion)
      .value("kLimitExceeded", ic::GlbStatus::kLimitExceeded)
      .value("kBudgetExceeded", ic::GlbStatus::kBudgetExceeded)
      .value("kCanceled", ic::GlbStatus::kCanceled);
  nb::enum_<ic::GltfDiagnosticCode>(module, "GltfDiagnosticCode")
      .value("kNone", ic::GltfDiagnosticCode::kNone)
      .value("kZeroLimit", ic::GltfDiagnosticCode::kZeroLimit)
      .value("kLimitNotRepresentable",
             ic::GltfDiagnosticCode::kLimitNotRepresentable)
      .value("kInvalidRequest", ic::GltfDiagnosticCode::kInvalidRequest)
      .value("kInvalidBindingKind", ic::GltfDiagnosticCode::kInvalidBindingKind)
      .value("kUnsupportedResourceRepresentation",
             ic::GltfDiagnosticCode::kUnsupportedResourceRepresentation)
      .value("kEmbeddedUriSyntax", ic::GltfDiagnosticCode::kEmbeddedUriSyntax)
      .value("kEmbeddedMediaMismatch",
             ic::GltfDiagnosticCode::kEmbeddedMediaMismatch)
      .value("kLimitExceeded", ic::GltfDiagnosticCode::kLimitExceeded)
      .value("kSizeOverflow", ic::GltfDiagnosticCode::kSizeOverflow)
      .value("kJsonSyntax", ic::GltfDiagnosticCode::kJsonSyntax)
      .value("kUnicodeScalar", ic::GltfDiagnosticCode::kUnicodeScalar)
      .value("kDecodedDuplicateKey",
             ic::GltfDiagnosticCode::kDecodedDuplicateKey)
      .value("kCoreFieldType", ic::GltfDiagnosticCode::kCoreFieldType)
      .value("kCoreFieldRequired", ic::GltfDiagnosticCode::kCoreFieldRequired)
      .value("kExactInteger", ic::GltfDiagnosticCode::kExactInteger)
      .value("kCoreRange", ic::GltfDiagnosticCode::kCoreRange)
      .value("kCoreArrayLength", ic::GltfDiagnosticCode::kCoreArrayLength)
      .value("kCoreReference", ic::GltfDiagnosticCode::kCoreReference)
      .value("kGraphRelationship", ic::GltfDiagnosticCode::kGraphRelationship)
      .value("kBinaryLayout", ic::GltfDiagnosticCode::kBinaryLayout)
      .value("kBinaryValue", ic::GltfDiagnosticCode::kBinaryValue)
      .value("kIndexRole", ic::GltfDiagnosticCode::kIndexRole)
      .value("kUnsupportedEnum", ic::GltfDiagnosticCode::kUnsupportedEnum)
      .value("kUnsupportedNumberRepresentation",
             ic::GltfDiagnosticCode::kUnsupportedNumberRepresentation)
      .value("kUnsupportedCoreString",
             ic::GltfDiagnosticCode::kUnsupportedCoreString)
      .value("kRequiredExtension", ic::GltfDiagnosticCode::kRequiredExtension)
      .value("kMissingResource", ic::GltfDiagnosticCode::kMissingResource)
      .value("kDuplicateResource", ic::GltfDiagnosticCode::kDuplicateResource)
      .value("kExtraResource", ic::GltfDiagnosticCode::kExtraResource)
      .value("kResourceExtent", ic::GltfDiagnosticCode::kResourceExtent)
      .value("kCanceled", ic::GltfDiagnosticCode::kCanceled)
      .value("kPayloadBudget", ic::GltfDiagnosticCode::kPayloadBudget)
      .value("kInvalidSelection", ic::GltfDiagnosticCode::kInvalidSelection)
      .value("kForeignOwner", ic::GltfDiagnosticCode::kForeignOwner);
  nb::enum_<ic::GltfFileCleanupOutcome>(module, "GltfFileCleanupOutcome")
      .value("kNotRequired", ic::GltfFileCleanupOutcome::kNotRequired)
      .value("kRemoved", ic::GltfFileCleanupOutcome::kRemoved)
      .value("kFailed", ic::GltfFileCleanupOutcome::kFailed);
  nb::enum_<ic::GltfFileDiagnosticCode>(module, "GltfFileDiagnosticCode")
      .value("kNone", ic::GltfFileDiagnosticCode::kNone)
      .value("kRequiredFieldZero",
             ic::GltfFileDiagnosticCode::kRequiredFieldZero)
      .value("kConfigurationExtent",
             ic::GltfFileDiagnosticCode::kConfigurationExtent)
      .value("kInvalidRequest", ic::GltfFileDiagnosticCode::kInvalidRequest)
      .value("kCountLimit", ic::GltfFileDiagnosticCode::kCountLimit)
      .value("kFileRequestLimit", ic::GltfFileDiagnosticCode::kFileRequestLimit)
      .value("kRequestBudget", ic::GltfFileDiagnosticCode::kRequestBudget)
      .value("kStopRequested", ic::GltfFileDiagnosticCode::kStopRequested)
      .value("kSourceIdentity", ic::GltfFileDiagnosticCode::kSourceIdentity)
      .value("kForeignBudget", ic::GltfFileDiagnosticCode::kForeignBudget)
      .value("kUriMalformed", ic::GltfFileDiagnosticCode::kUriMalformed)
      .value("kUriUnsupported", ic::GltfFileDiagnosticCode::kUriUnsupported)
      .value("kPathUnsupported", ic::GltfFileDiagnosticCode::kPathUnsupported)
      .value("kPathOutsideRoot", ic::GltfFileDiagnosticCode::kPathOutsideRoot)
      .value("kPathCollision", ic::GltfFileDiagnosticCode::kPathCollision)
      .value("kResourceMissing", ic::GltfFileDiagnosticCode::kResourceMissing)
      .value("kResourceNotRegularFile",
             ic::GltfFileDiagnosticCode::kResourceNotRegularFile)
      .value("kFileChanged", ic::GltfFileDiagnosticCode::kFileChanged)
      .value("kFileRead", ic::GltfFileDiagnosticCode::kFileRead)
      .value("kFileWrite", ic::GltfFileDiagnosticCode::kFileWrite)
      .value("kOpaqueInventoryRequired",
             ic::GltfFileDiagnosticCode::kOpaqueInventoryRequired)
      .value("kContainerBinding", ic::GltfFileDiagnosticCode::kContainerBinding)
      .value("kContainerPadding", ic::GltfFileDiagnosticCode::kContainerPadding)
      .value("kContainerPreservation",
             ic::GltfFileDiagnosticCode::kContainerPreservation)
      .value("kEntryBytes", ic::GltfFileDiagnosticCode::kEntryBytes)
      .value("kResourceBytes", ic::GltfFileDiagnosticCode::kResourceBytes)
      .value("kStagedInventory", ic::GltfFileDiagnosticCode::kStagedInventory)
      .value("kDelegatedContainer",
             ic::GltfFileDiagnosticCode::kDelegatedContainer)
      .value("kDelegatedRead", ic::GltfFileDiagnosticCode::kDelegatedRead)
      .value("kDelegatedExtraction",
             ic::GltfFileDiagnosticCode::kDelegatedExtraction)
      .value("kDelegatedInspection",
             ic::GltfFileDiagnosticCode::kDelegatedInspection)
      .value("kProgressCallback", ic::GltfFileDiagnosticCode::kProgressCallback)
      .value("kVerifiedCallback", ic::GltfFileDiagnosticCode::kVerifiedCallback)
      .value("kDestinationPath", ic::GltfFileDiagnosticCode::kDestinationPath)
      .value("kDestinationParent",
             ic::GltfFileDiagnosticCode::kDestinationParent)
      .value("kPublicationUnsupported",
             ic::GltfFileDiagnosticCode::kPublicationUnsupported)
      .value("kPublicationFailed",
             ic::GltfFileDiagnosticCode::kPublicationFailed)
      .value("kDestinationExists",
             ic::GltfFileDiagnosticCode::kDestinationExists);
  nb::enum_<ic::GltfFileFormat>(module, "GltfFileFormat")
      .value("kGltfJson", ic::GltfFileFormat::kGltfJson)
      .value("kGlb", ic::GltfFileFormat::kGlb);
  nb::enum_<ic::GltfFilePublicationOutcome>(module,
                                            "GltfFilePublicationOutcome")
      .value("kFailed", ic::GltfFilePublicationOutcome::kFailed)
      .value("kCanceled", ic::GltfFilePublicationOutcome::kCanceled)
      .value("kPublished", ic::GltfFilePublicationOutcome::kPublished);
  nb::enum_<ic::GltfFilePublicationPhase>(module, "GltfFilePublicationPhase")
      .value("kPreflight", ic::GltfFilePublicationPhase::kPreflight)
      .value("kStaging", ic::GltfFilePublicationPhase::kStaging)
      .value("kVerification", ic::GltfFilePublicationPhase::kVerification)
      .value("kPublication", ic::GltfFilePublicationPhase::kPublication);
  nb::enum_<ic::GltfFileStage>(module, "GltfFileStage")
      .value("kConfiguration", ic::GltfFileStage::kConfiguration)
      .value("kPathPlanning", ic::GltfFileStage::kPathPlanning)
      .value("kCapture", ic::GltfFileStage::kCapture)
      .value("kContainer", ic::GltfFileStage::kContainer)
      .value("kAssetRead", ic::GltfFileStage::kAssetRead)
      .value("kStaging", ic::GltfFileStage::kStaging)
      .value("kExtraction", ic::GltfFileStage::kExtraction)
      .value("kInspection", ic::GltfFileStage::kInspection)
      .value("kSupplemental", ic::GltfFileStage::kSupplemental)
      .value("kInventory", ic::GltfFileStage::kInventory)
      .value("kPublication", ic::GltfFileStage::kPublication)
      .value("kComplete", ic::GltfFileStage::kComplete);
  nb::enum_<ic::GltfFileStatus>(module, "GltfFileStatus")
      .value("kSuccess", ic::GltfFileStatus::kSuccess)
      .value("kInvalidConfiguration", ic::GltfFileStatus::kInvalidConfiguration)
      .value("kInvalidRequest", ic::GltfFileStatus::kInvalidRequest)
      .value("kMalformed", ic::GltfFileStatus::kMalformed)
      .value("kUnsupportedProfile", ic::GltfFileStatus::kUnsupportedProfile)
      .value("kLimitExceeded", ic::GltfFileStatus::kLimitExceeded)
      .value("kForeignBudget", ic::GltfFileStatus::kForeignBudget)
      .value("kSourceContractFailure",
             ic::GltfFileStatus::kSourceContractFailure)
      .value("kBudgetExceeded", ic::GltfFileStatus::kBudgetExceeded)
      .value("kIoFailure", ic::GltfFileStatus::kIoFailure)
      .value("kPreservationMismatch", ic::GltfFileStatus::kPreservationMismatch)
      .value("kCallbackFailure", ic::GltfFileStatus::kCallbackFailure)
      .value("kDestinationExists", ic::GltfFileStatus::kDestinationExists)
      .value("kPublicationFailed", ic::GltfFileStatus::kPublicationFailed)
      .value("kCanceled", ic::GltfFileStatus::kCanceled);
  nb::enum_<ic::GltfMaterializationDiagnosticCode>(
      module, "GltfMaterializationDiagnosticCode")
      .value("kNone", ic::GltfMaterializationDiagnosticCode::kNone)
      .value("kRequiredFieldZero",
             ic::GltfMaterializationDiagnosticCode::kRequiredFieldZero)
      .value("kConfigurationExtent",
             ic::GltfMaterializationDiagnosticCode::kConfigurationExtent)
      .value("kChannelLimit",
             ic::GltfMaterializationDiagnosticCode::kChannelLimit)
      .value("kBridgeRequestLimit",
             ic::GltfMaterializationDiagnosticCode::kBridgeRequestLimit)
      .value("kSetIndexRepresentation",
             ic::GltfMaterializationDiagnosticCode::kSetIndexRepresentation)
      .value("kSourceDescriptor",
             ic::GltfMaterializationDiagnosticCode::kSourceDescriptor)
      .value("kRequestBudget",
             ic::GltfMaterializationDiagnosticCode::kRequestBudget)
      .value("kStopRequested",
             ic::GltfMaterializationDiagnosticCode::kStopRequested)
      .value("kGeometry", ic::GltfMaterializationDiagnosticCode::kGeometry)
      .value("kStorageIssues",
             ic::GltfMaterializationDiagnosticCode::kStorageIssues);
  nb::enum_<ic::GltfMaterializationStage>(module, "GltfMaterializationStage")
      .value("kBridge", ic::GltfMaterializationStage::kBridge)
      .value("kCapture", ic::GltfMaterializationStage::kCapture)
      .value("kInspection", ic::GltfMaterializationStage::kInspection)
      .value("kPublication", ic::GltfMaterializationStage::kPublication);
  nb::enum_<ic::GltfMaterializationStatus>(module, "GltfMaterializationStatus")
      .value("kSuccess", ic::GltfMaterializationStatus::kSuccess)
      .value("kInvalidConfiguration",
             ic::GltfMaterializationStatus::kInvalidConfiguration)
      .value("kLimitExceeded", ic::GltfMaterializationStatus::kLimitExceeded)
      .value("kUnsupportedRepresentation",
             ic::GltfMaterializationStatus::kUnsupportedRepresentation)
      .value("kBudgetExceeded", ic::GltfMaterializationStatus::kBudgetExceeded)
      .value("kCanceled", ic::GltfMaterializationStatus::kCanceled)
      .value("kSourceContractFailure",
             ic::GltfMaterializationStatus::kSourceContractFailure)
      .value("kGeometryFailure",
             ic::GltfMaterializationStatus::kGeometryFailure)
      .value("kInvalidStorage", ic::GltfMaterializationStatus::kInvalidStorage);
  nb::enum_<ic::GltfResourceKind>(module, "GltfResourceKind")
      .value("kInvalid", ic::GltfResourceKind::kInvalid)
      .value("kBuffer", ic::GltfResourceKind::kBuffer)
      .value("kImage", ic::GltfResourceKind::kImage);
  nb::enum_<ic::GltfStatus>(module, "GltfStatus")
      .value("kAccepted", ic::GltfStatus::kAccepted)
      .value("kInvalidLimits", ic::GltfStatus::kInvalidLimits)
      .value("kInvalidRequest", ic::GltfStatus::kInvalidRequest)
      .value("kMalformed", ic::GltfStatus::kMalformed)
      .value("kUnsupportedProfile", ic::GltfStatus::kUnsupportedProfile)
      .value("kResourceBindingMismatch",
             ic::GltfStatus::kResourceBindingMismatch)
      .value("kCanceled", ic::GltfStatus::kCanceled)
      .value("kBudgetExceeded", ic::GltfStatus::kBudgetExceeded)
      .value("kInvalidSelection", ic::GltfStatus::kInvalidSelection)
      .value("kForeignOwner", ic::GltfStatus::kForeignOwner);
  nb::enum_<ic::GltfTopologyOutputInspectionDiagnosticCode>(
      module, "GltfTopologyOutputInspectionDiagnosticCode")
      .value("kNone", ic::GltfTopologyOutputInspectionDiagnosticCode::kNone)
      .value("kRequiredFieldZero",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kRequiredFieldZero)
      .value(
          "kConfigurationExtent",
          ic::GltfTopologyOutputInspectionDiagnosticCode::kConfigurationExtent)
      .value("kCountLimit",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kCountLimit)
      .value("kInspectionRequestLimit",
             ic::GltfTopologyOutputInspectionDiagnosticCode::
                 kInspectionRequestLimit)
      .value("kForeignBudget",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kForeignBudget)
      .value("kCandidateStorage",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kCandidateStorage)
      .value("kSourceIdentity",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kSourceIdentity)
      .value("kReloadIdentity",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kReloadIdentity)
      .value("kFaceMapShape",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kFaceMapShape)
      .value("kFaceMapOrder",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kFaceMapOrder)
      .value("kFaceMapRange",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kFaceMapRange)
      .value("kCornerMapShape",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kCornerMapShape)
      .value("kCornerMapValue",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kCornerMapValue)
      .value("kTriangleShape",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kTriangleShape)
      .value("kTriangleLoop",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kTriangleLoop)
      .value("kPositionBits",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kPositionBits)
      .value("kChannelCount",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kChannelCount)
      .value(
          "kAttributeDescriptor",
          ic::GltfTopologyOutputInspectionDiagnosticCode::kAttributeDescriptor)
      .value("kAttributeBits",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kAttributeBits)
      .value("kEmptyChangedSelection",
             ic::GltfTopologyOutputInspectionDiagnosticCode::
                 kEmptyChangedSelection)
      .value("kOutputJson",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kOutputJson)
      .value(
          "kLexicalPreservation",
          ic::GltfTopologyOutputInspectionDiagnosticCode::kLexicalPreservation)
      .value("kAppendedBinding",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kAppendedBinding)
      .value("kResourceIdentity",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kResourceIdentity)
      .value("kResourceBytes",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kResourceBytes)
      .value("kChannelDescriptor",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kChannelDescriptor)
      .value("kChannelBits",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kChannelBits)
      .value("kSelectedTopology",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kSelectedTopology)
      .value("kSourceProvenance",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kSourceProvenance)
      .value("kRequestBudget",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kRequestBudget)
      .value("kStopRequested",
             ic::GltfTopologyOutputInspectionDiagnosticCode::kStopRequested);
  nb::enum_<ic::GltfTopologyOutputInspectionStage>(
      module, "GltfTopologyOutputInspectionStage")
      .value("kAdmission", ic::GltfTopologyOutputInspectionStage::kAdmission)
      .value("kSourceCandidate",
             ic::GltfTopologyOutputInspectionStage::kSourceCandidate)
      .value("kCorrespondence",
             ic::GltfTopologyOutputInspectionStage::kCorrespondence)
      .value("kLexical", ic::GltfTopologyOutputInspectionStage::kLexical)
      .value("kResources", ic::GltfTopologyOutputInspectionStage::kResources)
      .value("kReloadedPrimitive",
             ic::GltfTopologyOutputInspectionStage::kReloadedPrimitive)
      .value("kProvenance", ic::GltfTopologyOutputInspectionStage::kProvenance)
      .value("kPublication",
             ic::GltfTopologyOutputInspectionStage::kPublication);
  nb::enum_<ic::GltfTopologyOutputInspectionStatus>(
      module, "GltfTopologyOutputInspectionStatus")
      .value("kSuccess", ic::GltfTopologyOutputInspectionStatus::kSuccess)
      .value("kInvalidConfiguration",
             ic::GltfTopologyOutputInspectionStatus::kInvalidConfiguration)
      .value("kLimitExceeded",
             ic::GltfTopologyOutputInspectionStatus::kLimitExceeded)
      .value("kForeignBudget",
             ic::GltfTopologyOutputInspectionStatus::kForeignBudget)
      .value("kInvalidCandidateStorage",
             ic::GltfTopologyOutputInspectionStatus::kInvalidCandidateStorage)
      .value("kInvalidCorrespondence",
             ic::GltfTopologyOutputInspectionStatus::kInvalidCorrespondence)
      .value("kSourceContractFailure",
             ic::GltfTopologyOutputInspectionStatus::kSourceContractFailure)
      .value("kOutputMismatch",
             ic::GltfTopologyOutputInspectionStatus::kOutputMismatch)
      .value("kUnsupportedSelection",
             ic::GltfTopologyOutputInspectionStatus::kUnsupportedSelection)
      .value("kBudgetExceeded",
             ic::GltfTopologyOutputInspectionStatus::kBudgetExceeded)
      .value("kCanceled", ic::GltfTopologyOutputInspectionStatus::kCanceled);
  nb::enum_<ic::GltfTopologyWriteDiagnosticCode>(
      module, "GltfTopologyWriteDiagnosticCode")
      .value("kNone", ic::GltfTopologyWriteDiagnosticCode::kNone)
      .value("kRequiredFieldZero",
             ic::GltfTopologyWriteDiagnosticCode::kRequiredFieldZero)
      .value("kConfigurationExtent",
             ic::GltfTopologyWriteDiagnosticCode::kConfigurationExtent)
      .value("kCountLimit", ic::GltfTopologyWriteDiagnosticCode::kCountLimit)
      .value("kWriterRequestLimit",
             ic::GltfTopologyWriteDiagnosticCode::kWriterRequestLimit)
      .value("kForeignBudget",
             ic::GltfTopologyWriteDiagnosticCode::kForeignBudget)
      .value("kCandidateStorage",
             ic::GltfTopologyWriteDiagnosticCode::kCandidateStorage)
      .value("kSourceCapability",
             ic::GltfTopologyWriteDiagnosticCode::kSourceCapability)
      .value("kEmptyChangedSelection",
             ic::GltfTopologyWriteDiagnosticCode::kEmptyChangedSelection)
      .value("kFaceMapShape",
             ic::GltfTopologyWriteDiagnosticCode::kFaceMapShape)
      .value("kFaceMapOrder",
             ic::GltfTopologyWriteDiagnosticCode::kFaceMapOrder)
      .value("kFaceMapRange",
             ic::GltfTopologyWriteDiagnosticCode::kFaceMapRange)
      .value("kCornerMapShape",
             ic::GltfTopologyWriteDiagnosticCode::kCornerMapShape)
      .value("kCornerMapValue",
             ic::GltfTopologyWriteDiagnosticCode::kCornerMapValue)
      .value("kTriangleShape",
             ic::GltfTopologyWriteDiagnosticCode::kTriangleShape)
      .value("kTriangleLoop",
             ic::GltfTopologyWriteDiagnosticCode::kTriangleLoop)
      .value("kPositionBits",
             ic::GltfTopologyWriteDiagnosticCode::kPositionBits)
      .value("kChannelCount",
             ic::GltfTopologyWriteDiagnosticCode::kChannelCount)
      .value("kAttributeDescriptor",
             ic::GltfTopologyWriteDiagnosticCode::kAttributeDescriptor)
      .value("kAttributeBits",
             ic::GltfTopologyWriteDiagnosticCode::kAttributeBits)
      .value("kSourceDescriptor",
             ic::GltfTopologyWriteDiagnosticCode::kSourceDescriptor)
      .value("kSourceProvenance",
             ic::GltfTopologyWriteDiagnosticCode::kSourceProvenance)
      .value("kLexicalSource",
             ic::GltfTopologyWriteDiagnosticCode::kLexicalSource)
      .value("kIndexRepresentation",
             ic::GltfTopologyWriteDiagnosticCode::kIndexRepresentation)
      .value("kOutputExtent",
             ic::GltfTopologyWriteDiagnosticCode::kOutputExtent)
      .value("kRequestBudget",
             ic::GltfTopologyWriteDiagnosticCode::kRequestBudget)
      .value("kStopRequested",
             ic::GltfTopologyWriteDiagnosticCode::kStopRequested);
  nb::enum_<ic::GltfTopologyWriteStage>(module, "GltfTopologyWriteStage")
      .value("kAdmission", ic::GltfTopologyWriteStage::kAdmission)
      .value("kVerification", ic::GltfTopologyWriteStage::kVerification)
      .value("kLexicalPlanning", ic::GltfTopologyWriteStage::kLexicalPlanning)
      .value("kIndexEncoding", ic::GltfTopologyWriteStage::kIndexEncoding)
      .value("kBase64Encoding", ic::GltfTopologyWriteStage::kBase64Encoding)
      .value("kOutputCopy", ic::GltfTopologyWriteStage::kOutputCopy)
      .value("kPublication", ic::GltfTopologyWriteStage::kPublication);
  nb::enum_<ic::GltfTopologyWriteStatus>(module, "GltfTopologyWriteStatus")
      .value("kSuccess", ic::GltfTopologyWriteStatus::kSuccess)
      .value("kInvalidConfiguration",
             ic::GltfTopologyWriteStatus::kInvalidConfiguration)
      .value("kLimitExceeded", ic::GltfTopologyWriteStatus::kLimitExceeded)
      .value("kForeignBudget", ic::GltfTopologyWriteStatus::kForeignBudget)
      .value("kInvalidCandidateStorage",
             ic::GltfTopologyWriteStatus::kInvalidCandidateStorage)
      .value("kInvalidCorrespondence",
             ic::GltfTopologyWriteStatus::kInvalidCorrespondence)
      .value("kPreservationMismatch",
             ic::GltfTopologyWriteStatus::kPreservationMismatch)
      .value("kUnsupportedProfile",
             ic::GltfTopologyWriteStatus::kUnsupportedProfile)
      .value("kUnsupportedSelection",
             ic::GltfTopologyWriteStatus::kUnsupportedSelection)
      .value("kSourceContractFailure",
             ic::GltfTopologyWriteStatus::kSourceContractFailure)
      .value("kBudgetExceeded", ic::GltfTopologyWriteStatus::kBudgetExceeded)
      .value("kCanceled", ic::GltfTopologyWriteStatus::kCanceled);
  nb::class_<Observation<GltfFileWorkflowByteExecution>>(
      module, "GltfFileWorkflowByteExecution")
      .def_prop_ro("hashing",
                   [](const Observation<GltfFileWorkflowByteExecution>& self) {
                     return Observe(self.value.hashing);
                   })
      .def_prop_ro("report_copy",
                   [](const Observation<GltfFileWorkflowByteExecution>& self) {
                     return Observe(self.value.report_copy);
                   });
  nb::class_<Observation<GltfFileWorkflowDiagnostic>>(
      module, "GltfFileWorkflowDiagnostic")
      .def_prop_ro("stage",
                   [](const Observation<GltfFileWorkflowDiagnostic>& self) {
                     return Observe(self.value.stage);
                   })
      .def_prop_ro("code",
                   [](const Observation<GltfFileWorkflowDiagnostic>& self) {
                     return Observe(self.value.code);
                   })
      .def_prop_ro("memory",
                   [](const Observation<GltfFileWorkflowDiagnostic>& self) {
                     return Observe(self.value.memory);
                   })
      .def_prop_ro("publisher",
                   [](const Observation<GltfFileWorkflowDiagnostic>& self) {
                     return Observe(self.value.publisher);
                   });
  nb::class_<Observation<GltfFileWorkflowOutcomes>>(module,
                                                    "GltfFileWorkflowOutcomes")
      .def_prop_ro("execution",
                   [](const Observation<GltfFileWorkflowOutcomes>& self) {
                     return Observe(self.value.execution);
                   })
      .def_prop_ro("candidate",
                   [](const Observation<GltfFileWorkflowOutcomes>& self) {
                     return Observe(self.value.candidate);
                   })
      .def_prop_ro("profile",
                   [](const Observation<GltfFileWorkflowOutcomes>& self) {
                     return Observe(self.value.profile);
                   })
      .def_prop_ro("publication",
                   [](const Observation<GltfFileWorkflowOutcomes>& self) {
                     return Observe(self.value.publication);
                   })
      .def_prop_ro("stage",
                   [](const Observation<GltfFileWorkflowOutcomes>& self) {
                     return Observe(self.value.stage);
                   })
      .def_prop_ro("exit",
                   [](const Observation<GltfFileWorkflowOutcomes>& self) {
                     return Observe(self.value.exit);
                   });
  nb::class_<Observation<GltfFileWorkflowRequestedBytes>>(
      module, "GltfFileWorkflowRequestedBytes")
      .def_prop_ro("retained_source",
                   [](const Observation<GltfFileWorkflowRequestedBytes>& self) {
                     return Observe(self.value.retained_source);
                   })
      .def_prop_ro("wrapper",
                   [](const Observation<GltfFileWorkflowRequestedBytes>& self) {
                     return Observe(self.value.wrapper);
                   })
      .def_prop_ro("memory",
                   [](const Observation<GltfFileWorkflowRequestedBytes>& self) {
                     return Observe(self.value.memory);
                   })
      .def_prop_ro("publication_files",
                   [](const Observation<GltfFileWorkflowRequestedBytes>& self) {
                     return Observe(self.value.publication_files);
                   })
      .def_prop_ro("retained_repair",
                   [](const Observation<GltfFileWorkflowRequestedBytes>& self) {
                     return Observe(self.value.retained_repair);
                   });
  nb::class_<Observation<GltfRetainedRepairRequestedBytes>>(
      module, "GltfRetainedRepairRequestedBytes")
      .def_prop_ro(
          "source_geometry",
          [](const Observation<GltfRetainedRepairRequestedBytes>& self) {
            return Observe(self.value.source_geometry);
          })
      .def_prop_ro(
          "repair",
          [](const Observation<GltfRetainedRepairRequestedBytes>& self) {
            return Observe(self.value.repair);
          })
      .def_prop_ro(
          "total",
          [](const Observation<GltfRetainedRepairRequestedBytes>& self) {
            return Observe(self.value.total);
          });
  nb::class_<Observation<MaterializedDuplicateRequestedBytes>>(
      module, "MaterializedDuplicateRequestedBytes")
      .def_prop_ro(
          "repair",
          [](const Observation<MaterializedDuplicateRequestedBytes>& self) {
            return Observe(self.value.repair);
          })
      .def_prop_ro(
          "source_inspection",
          [](const Observation<MaterializedDuplicateRequestedBytes>& self) {
            return Observe(self.value.source_inspection);
          })
      .def_prop_ro(
          "selection",
          [](const Observation<MaterializedDuplicateRequestedBytes>& self) {
            return Observe(self.value.selection);
          })
      .def_prop_ro(
          "candidate_inspection",
          [](const Observation<MaterializedDuplicateRequestedBytes>& self) {
            return Observe(self.value.candidate_inspection);
          })
      .def_prop_ro(
          "total",
          [](const Observation<MaterializedDuplicateRequestedBytes>& self) {
            return Observe(self.value.total);
          });
  nb::class_<Observation<GltfWorkflowOutcomes>>(module, "GltfWorkflowOutcomes")
      .def_prop_ro("execution",
                   [](const Observation<GltfWorkflowOutcomes>& self) {
                     return Observe(self.value.execution);
                   })
      .def_prop_ro("candidate",
                   [](const Observation<GltfWorkflowOutcomes>& self) {
                     return Observe(self.value.candidate);
                   })
      .def_prop_ro("profile",
                   [](const Observation<GltfWorkflowOutcomes>& self) {
                     return Observe(self.value.profile);
                   })
      .def_prop_ro("stage",
                   [](const Observation<GltfWorkflowOutcomes>& self) {
                     return Observe(self.value.stage);
                   })
      .def_prop_ro("exit", [](const Observation<GltfWorkflowOutcomes>& self) {
        return Observe(self.value.exit);
      });
  nb::class_<Observation<GltfRetainedFileSourceRequestedBytes>>(
      module, "GltfRetainedFileSourceRequestedBytes")
      .def_prop_ro(
          "files",
          [](const Observation<GltfRetainedFileSourceRequestedBytes>& self) {
            return Observe(self.value.files);
          })
      .def_prop_ro(
          "capped_reader",
          [](const Observation<GltfRetainedFileSourceRequestedBytes>& self) {
            return Observe(self.value.capped_reader);
          })
      .def_prop_ro(
          "container",
          [](const Observation<GltfRetainedFileSourceRequestedBytes>& self) {
            return Observe(self.value.container);
          });
  nb::class_<Observation<GltfWorkflowByteExecutionStats>>(
      module, "GltfWorkflowByteExecutionStats")
      .def_prop_ro("planned_jobs",
                   [](const Observation<GltfWorkflowByteExecutionStats>& self) {
                     return Observe(self.value.planned_jobs);
                   })
      .def_prop_ro("launched_workers",
                   [](const Observation<GltfWorkflowByteExecutionStats>& self) {
                     return Observe(self.value.launched_workers);
                   })
      .def_prop_ro("joined_workers",
                   [](const Observation<GltfWorkflowByteExecutionStats>& self) {
                     return Observe(self.value.joined_workers);
                   })
      .def_prop_ro("caller_jobs",
                   [](const Observation<GltfWorkflowByteExecutionStats>& self) {
                     return Observe(self.value.caller_jobs);
                   });
  nb::class_<Observation<GltfWorkflowDiagnostic>>(module,
                                                  "GltfWorkflowDiagnostic")
      .def_prop_ro("stage",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.stage);
                   })
      .def_prop_ro("code",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.code);
                   })
      .def_prop_ro("attribute",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.attribute);
                   })
      .def_prop_ro("element",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.element);
                   })
      .def_prop_ro("resource_kind",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.resource_kind);
                   })
      .def_prop_ro("resource_index",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.resource_index);
                   })
      .def_prop_ro("json_span",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.json_span);
                   })
      .def_prop_ro("delegated",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.delegated);
                   })
      .def_prop_ro("delegated_status",
                   [](const Observation<GltfWorkflowDiagnostic>& self) {
                     return Observe(self.value.delegated_status);
                   });
  nb::class_<Observation<GltfWorkflowRequestedBytes>>(
      module, "GltfWorkflowRequestedBytes")
      .def_prop_ro("workflow",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.workflow);
                   })
      .def_prop_ro("read",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.read);
                   })
      .def_prop_ro("extraction",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.extraction);
                   })
      .def_prop_ro("materialization",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.materialization);
                   })
      .def_prop_ro("repair",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.repair);
                   })
      .def_prop_ro("writing",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.writing);
                   })
      .def_prop_ro("reload",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.reload);
                   })
      .def_prop_ro("reload_extraction",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.reload_extraction);
                   })
      .def_prop_ro("verification",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.verification);
                   })
      .def_prop_ro("total",
                   [](const Observation<GltfWorkflowRequestedBytes>& self) {
                     return Observe(self.value.total);
                   });
  nb::class_<Observation<MaterializedDuplicateDiagnostic>>(
      module, "MaterializedDuplicateDiagnostic")
      .def_prop_ro(
          "stage",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.stage);
          })
      .def_prop_ro(
          "code",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.code);
          })
      .def_prop_ro(
          "target",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.target);
          })
      .def_prop_ro(
          "attribute",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.attribute);
          })
      .def_prop_ro(
          "element",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.element);
          })
      .def_prop_ro(
          "geometry_status",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.geometry_status);
          })
      .def_prop_ro(
          "geometry_diagnostic",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.geometry_diagnostic);
          })
      .def_prop_ro(
          "storage_issue",
          [](const Observation<MaterializedDuplicateDiagnostic>& self) {
            return Observe(self.value.storage_issue);
          });
  nb::class_<Observation<geo::MaterializationDiagnostic>>(
      module, "MaterializationDiagnostic")
      .def_prop_ro("code",
                   [](const Observation<geo::MaterializationDiagnostic>& self) {
                     return Observe(self.value.code);
                   })
      .def_prop_ro("attribute",
                   [](const Observation<geo::MaterializationDiagnostic>& self) {
                     return Observe(self.value.attribute);
                   })
      .def_prop_ro("element",
                   [](const Observation<geo::MaterializationDiagnostic>& self) {
                     return Observe(self.value.element);
                   })
      .def_prop_ro("storage_issue",
                   [](const Observation<geo::MaterializationDiagnostic>& self) {
                     return Observe(self.value.storage_issue);
                   });
  nb::class_<Observation<geo::StorageIssue>>(module, "StorageIssue")
      .def_prop_ro("code",
                   [](const Observation<geo::StorageIssue>& self) {
                     return Observe(self.value.code);
                   })
      .def_prop_ro("attribute",
                   [](const Observation<geo::StorageIssue>& self) {
                     return Observe(self.value.attribute);
                   })
      .def_prop_ro("element", [](const Observation<geo::StorageIssue>& self) {
        return Observe(self.value.element);
      });
  nb::class_<Observation<ic::GltfDiagnostic>>(module, "GltfDiagnostic")
      .def_prop_ro("status",
                   [](const Observation<ic::GltfDiagnostic>& self) {
                     return Observe(self.value.status);
                   })
      .def_prop_ro("code",
                   [](const Observation<ic::GltfDiagnostic>& self) {
                     return Observe(self.value.code);
                   })
      .def_prop_ro("json_span",
                   [](const Observation<ic::GltfDiagnostic>& self) {
                     return Observe(self.value.json_span);
                   })
      .def_prop_ro("resource_kind",
                   [](const Observation<ic::GltfDiagnostic>& self) {
                     return Observe(self.value.resource_kind);
                   })
      .def_prop_ro("resource_index",
                   [](const Observation<ic::GltfDiagnostic>& self) {
                     return Observe(self.value.resource_index);
                   })
      .def_prop_ro("resource_span",
                   [](const Observation<ic::GltfDiagnostic>& self) {
                     return Observe(self.value.resource_span);
                   })
      .def_prop_ro("rule_id", [](const Observation<ic::GltfDiagnostic>& self) {
        return Observe(self.value.rule_id);
      });
  nb::class_<Observation<ic::GltfFileDiagnostic>>(module, "GltfFileDiagnostic")
      .def_prop_ro("status",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.status);
                   })
      .def_prop_ro("stage",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.stage);
                   })
      .def_prop_ro("code",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.code);
                   })
      .def_prop_ro("file_index",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.file_index);
                   })
      .def_prop_ro("byte_offset",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.byte_offset);
                   })
      .def_prop_ro("resource_kind",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.resource_kind);
                   })
      .def_prop_ro("resource_index",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.resource_index);
                   })
      .def_prop_ro("system_error",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.system_error);
                   })
      .def_prop_ro("gltf_diagnostic",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.gltf_diagnostic);
                   })
      .def_prop_ro("container_status",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.container_status);
                   })
      .def_prop_ro("container_byte_offset",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.container_byte_offset);
                   })
      .def_prop_ro("inspection_status",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.inspection_status);
                   })
      .def_prop_ro("inspection_diagnostic",
                   [](const Observation<ic::GltfFileDiagnostic>& self) {
                     return Observe(self.value.inspection_diagnostic);
                   });
  nb::class_<Observation<ic::GltfFileExecution>>(module, "GltfFileExecution")
      .def_prop_ro("capture",
                   [](const Observation<ic::GltfFileExecution>& self) {
                     return Observe(self.value.capture);
                   })
      .def_prop_ro("byte_verification",
                   [](const Observation<ic::GltfFileExecution>& self) {
                     return Observe(self.value.byte_verification);
                   });
  nb::class_<Observation<ic::GltfFileExecutionStats>>(module,
                                                      "GltfFileExecutionStats")
      .def_prop_ro("planned_jobs",
                   [](const Observation<ic::GltfFileExecutionStats>& self) {
                     return Observe(self.value.planned_jobs);
                   })
      .def_prop_ro("launched_workers",
                   [](const Observation<ic::GltfFileExecutionStats>& self) {
                     return Observe(self.value.launched_workers);
                   })
      .def_prop_ro("joined_workers",
                   [](const Observation<ic::GltfFileExecutionStats>& self) {
                     return Observe(self.value.joined_workers);
                   })
      .def_prop_ro("caller_jobs",
                   [](const Observation<ic::GltfFileExecutionStats>& self) {
                     return Observe(self.value.caller_jobs);
                   });
  nb::class_<Observation<ic::GltfMaterializationDiagnostic>>(
      module, "GltfMaterializationDiagnostic")
      .def_prop_ro(
          "stage",
          [](const Observation<ic::GltfMaterializationDiagnostic>& self) {
            return Observe(self.value.stage);
          })
      .def_prop_ro(
          "code",
          [](const Observation<ic::GltfMaterializationDiagnostic>& self) {
            return Observe(self.value.code);
          })
      .def_prop_ro(
          "source_channel",
          [](const Observation<ic::GltfMaterializationDiagnostic>& self) {
            return Observe(self.value.source_channel);
          })
      .def_prop_ro(
          "geometry_status",
          [](const Observation<ic::GltfMaterializationDiagnostic>& self) {
            return Observe(self.value.geometry_status);
          })
      .def_prop_ro(
          "geometry_diagnostic",
          [](const Observation<ic::GltfMaterializationDiagnostic>& self) {
            return Observe(self.value.geometry_diagnostic);
          })
      .def_prop_ro(
          "storage_issue",
          [](const Observation<ic::GltfMaterializationDiagnostic>& self) {
            return Observe(self.value.storage_issue);
          });
  nb::class_<Observation<ic::GltfMaterializationRequestedBytes>>(
      module, "GltfMaterializationRequestedBytes")
      .def_prop_ro(
          "bridge",
          [](const Observation<ic::GltfMaterializationRequestedBytes>& self) {
            return Observe(self.value.bridge);
          })
      .def_prop_ro(
          "materialization",
          [](const Observation<ic::GltfMaterializationRequestedBytes>& self) {
            return Observe(self.value.materialization);
          })
      .def_prop_ro(
          "inspection",
          [](const Observation<ic::GltfMaterializationRequestedBytes>& self) {
            return Observe(self.value.inspection);
          });
  nb::class_<Observation<ic::GltfSourceSpan>>(module, "GltfSourceSpan")
      .def_prop_ro("offset",
                   [](const Observation<ic::GltfSourceSpan>& self) {
                     return Observe(self.value.offset);
                   })
      .def_prop_ro("length", [](const Observation<ic::GltfSourceSpan>& self) {
        return Observe(self.value.length);
      });
  nb::class_<Observation<ic::GltfTopologyOutputInspectionDiagnostic>>(
      module, "GltfTopologyOutputInspectionDiagnostic")
      .def_prop_ro(
          "stage",
          [](const Observation<ic::GltfTopologyOutputInspectionDiagnostic>&
                 self) { return Observe(self.value.stage); })
      .def_prop_ro(
          "code",
          [](const Observation<ic::GltfTopologyOutputInspectionDiagnostic>&
                 self) { return Observe(self.value.code); })
      .def_prop_ro(
          "attribute",
          [](const Observation<ic::GltfTopologyOutputInspectionDiagnostic>&
                 self) { return Observe(self.value.attribute); })
      .def_prop_ro(
          "element",
          [](const Observation<ic::GltfTopologyOutputInspectionDiagnostic>&
                 self) { return Observe(self.value.element); })
      .def_prop_ro(
          "resource",
          [](const Observation<ic::GltfTopologyOutputInspectionDiagnostic>&
                 self) { return Observe(self.value.resource); })
      .def_prop_ro(
          "json_span",
          [](const Observation<ic::GltfTopologyOutputInspectionDiagnostic>&
                 self) { return Observe(self.value.json_span); })
      .def_prop_ro(
          "storage_issue",
          [](const Observation<ic::GltfTopologyOutputInspectionDiagnostic>&
                 self) { return Observe(self.value.storage_issue); });
  nb::class_<Observation<ic::GltfTopologyWriteDiagnostic>>(
      module, "GltfTopologyWriteDiagnostic")
      .def_prop_ro(
          "stage",
          [](const Observation<ic::GltfTopologyWriteDiagnostic>& self) {
            return Observe(self.value.stage);
          })
      .def_prop_ro(
          "code",
          [](const Observation<ic::GltfTopologyWriteDiagnostic>& self) {
            return Observe(self.value.code);
          })
      .def_prop_ro(
          "attribute",
          [](const Observation<ic::GltfTopologyWriteDiagnostic>& self) {
            return Observe(self.value.attribute);
          })
      .def_prop_ro(
          "element",
          [](const Observation<ic::GltfTopologyWriteDiagnostic>& self) {
            return Observe(self.value.element);
          })
      .def_prop_ro(
          "json_span",
          [](const Observation<ic::GltfTopologyWriteDiagnostic>& self) {
            return Observe(self.value.json_span);
          })
      .def_prop_ro(
          "storage_issue",
          [](const Observation<ic::GltfTopologyWriteDiagnostic>& self) {
            return Observe(self.value.storage_issue);
          });
  nb::class_<Observation<std::error_code>>(module, "SystemErrorCode")
      .def_prop_ro("category",
                   [](const Observation<std::error_code>& self) {
                     return nb::str(self.value.category().name());
                   })
      .def_prop_ro("value", [](const Observation<std::error_code>& self) {
        return self.value.value();
      });
  nb::class_<Observation<ic::GltfFilePublicationResult>>(
      module, "GltfFilePublicationObservation")
      .def_prop_ro("outcome",
                   [](const Observation<ic::GltfFilePublicationResult>& self) {
                     return Observe(self.value.outcome);
                   })
      .def_prop_ro("phase",
                   [](const Observation<ic::GltfFilePublicationResult>& self) {
                     return Observe(self.value.phase);
                   })
      .def_prop_ro("diagnostic",
                   [](const Observation<ic::GltfFilePublicationResult>& self) {
                     return Observe(self.value.diagnostic);
                   })
      .def_prop_ro("cleanup",
                   [](const Observation<ic::GltfFilePublicationResult>& self) {
                     return Observe(self.value.cleanup);
                   })
      .def_prop_ro("cleanup_error",
                   [](const Observation<ic::GltfFilePublicationResult>& self) {
                     return Observe(self.value.cleanup_error);
                   })
      .def_prop_ro("execution",
                   [](const Observation<ic::GltfFilePublicationResult>& self) {
                     return Observe(self.value.execution);
                   });
  nb::class_<GltfExecution>(module, "GltfExecution")
      .def(nb::init<std::string_view>(), nb::kw_only(),
           nb::arg("preset") = kBalancedGltfPreset)
      .def(GltfTranslationFields::kDirectTranslationPolicy,
           &GltfExecution::DirectTranslationPolicy)
      .def_prop_ro("preset",
                   [](const GltfExecution& self) {
                     return nb::str(self.Name().c_str());
                   })
      .def_prop_ro("resolved_preset",
                   [](const GltfExecution& self) {
                     return PresetObservation(self.Preset());
                   })
      .def_prop_ro("worker_budget",
                   [](const GltfExecution& self) {
                     return self.Context().WorkerBudget();
                   })
      .def_prop_ro("active_workers",
                   [](const GltfExecution& self) {
                     return self.Context().ActiveWorkers();
                   })
      .def_prop_ro("active_tracked_payload", [](const GltfExecution& self) {
        return self.Context().ActiveTrackedPayload();
      });
  nb::class_<GltfCancellation>(module, "GltfCancellation")
      .def(nb::init<>())
      .def("cancel", &GltfCancellation::Cancel)
      .def_prop_ro("cancelled", &GltfCancellation::Cancelled);
  nb::class_<GltfFileWorkflowResult>(module, "GltfFileWorkflowResult")
      .def_prop_ro(
          GltfTranslationFields::kProcessingProfile,
          [](const GltfFileWorkflowResult& self) {
            self.RequireComplete();
            return self.extraction.processing_profile ==
                           ic::GltfProcessingProfile::
                               kDirectTranslationTrianglesV1
                       ? nb::str(kDirectTranslationGltfProcessingProfile.data())
                       : nb::str(kStaticGltfProcessingProfile.data());
          })
      .def_prop_ro(GltfTranslationFields::kTranslationFacts, &TranslationFacts)
      .def_prop_ro("workflow_started",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return self.workflow_started;
                   })
      .def_prop_ro("capture_status",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return Observe(self.capture.diagnostic.status);
                   })
      .def_prop_ro("capture_diagnostic",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return Observe(self.capture.diagnostic);
                   })
      .def_prop_ro("capture_execution",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return Observe(self.capture.execution);
                   })
      .def_prop_ro("status",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return self.workflow_started
                                ? Observe(self.workflow.status)
                                : nb::none();
                   })
      .def_prop_ro("outcomes",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return self.workflow_started
                                ? Observe(self.workflow.outcomes)
                                : nb::none();
                   })
      .def_prop_ro("diagnostic",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return self.workflow_started
                                ? Observe(self.workflow.diagnostic)
                                : nb::none();
                   })
      .def_prop_ro(
          "memory_outcomes",
          [](const GltfFileWorkflowResult& self) {
            self.RequireComplete();
            return self.workflow_started && self.workflow.record
                       ? Observe(self.workflow.record->MemoryOutcomes())
                       : nb::none();
          })
      .def_prop_ro("execution",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return self.workflow_started
                                ? Observe(self.workflow.execution)
                                : nb::none();
                   })
      .def_prop_ro("publication",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return self.workflow_started
                                ? Observe(self.workflow.publication)
                                : nb::none();
                   })
      .def_prop_ro(
          "requested_bytes",
          [](const GltfFileWorkflowResult& self) {
            self.RequireComplete();
            return self.workflow_started && self.workflow.record
                       ? Observe(self.workflow.record->NewRequestedBytes())
                       : nb::none();
          })
      .def_prop_ro("source_format",
                   [](const GltfFileWorkflowResult& self) {
                     self.RequireComplete();
                     return self.capture.asset
                                ? Observe(self.capture.asset->Format())
                                : nb::none();
                   })
      .def_prop_ro("source_entry_path",
                   [](const GltfFileWorkflowResult& self) -> nb::object {
                     self.RequireComplete();
                     if (!self.capture.asset) return nb::none();
                     const auto path = self.capture.asset->EntryPath();
                     return nb::str(path.data(), path.size());
                   })
      .def_prop_ro("published_entry_path",
                   [](const GltfFileWorkflowResult& self) -> nb::object {
                     self.RequireComplete();
                     if (!self.workflow_started || !self.workflow.record ||
                         !self.workflow.record->Published())
                       return nb::none();
                     const auto path =
                         self.workflow.record->Published()->EntryPath();
                     return nb::str(path.data(), path.size());
                   })
      .def_prop_ro(
          "published_report_path",
          [](const GltfFileWorkflowResult& self) -> nb::object {
            self.RequireComplete();
            if (!self.workflow_started || !self.workflow.record)
              return nb::none();
            const auto path = self.workflow.record->PublishedReportPath();
            return path ? nb::object(nb::str(path->data(), path->size()))
                        : nb::none();
          })
      .def_prop_ro("report_json",
                   [](const GltfFileWorkflowResult& self) -> nb::object {
                     self.RequireComplete();
                     if (!self.workflow_started || !self.workflow.record)
                       return nb::none();
                     const auto bytes = self.workflow.record->ReportJson();
#if defined(MESHVALE_REPAIR_GLTF_BINDING_TESTS)
                     auto report_scope = binding_test::BeforeReportBytes();
#endif
                     return bytes.empty() ? nb::none()
                                          : nb::object(nb::bytes(bytes.data(),
                                                                 bytes.size()));
                   })
      .def_prop_ro("input_snapshot_counts",
                   [](const GltfFileWorkflowResult& self) {
                     return SnapshotCounts(
                         self, &MaterializedDuplicateRepair::Source);
                   })
      .def_prop_ro("candidate_snapshot_counts",
                   [](const GltfFileWorkflowResult& self) {
                     return SnapshotCounts(
                         self, &MaterializedDuplicateRepair::Candidate);
                   })
      .def_prop_ro("face_map",
                   [](const GltfFileWorkflowResult& self) {
                     return Map(self, &MaterializedDuplicateRepair::FaceMap);
                   })
      .def_prop_ro("corner_map",
                   [](const GltfFileWorkflowResult& self) {
                     return Map(self, &MaterializedDuplicateRepair::CornerMap);
                   })
      .def_prop_ro("source_faces",
                   [](const GltfFileWorkflowResult& self) {
                     return Map(self,
                                &MaterializedDuplicateRepair::SourceFaces);
                   })
      .def_prop_ro("source_corners", [](const GltfFileWorkflowResult& self) {
        return Map(self, &MaterializedDuplicateRepair::SourceCorners);
      });
  module.def("_prepare_gltf_file", &Prepare, nb::arg("source"),
             nb::arg("destination").none(), nb::arg("targets"), nb::kw_only(),
             nb::arg("primitive_ordinal"), nb::arg("triangles"),
             nb::arg("values"),
             nb::arg(GltfTranslationFields::kProcessingProfile) =
                 kStaticGltfProcessingProfile,
             nb::arg("execution"), nb::arg("input_id") = kDefaultGltfInputId,
             nb::arg("resource_root") = nb::none(),
             nb::arg("row_local_attributes") = nb::tuple(),
             nb::arg("report_path") = kDefaultGltfReportPath,
             nb::arg("cancellation") = nb::none(),
             nb::arg("retain_failure_facts") = false);
  // One borrowed handle, no conversion owners, postcall hooks or keepalive.
  module.def("_run_gltf_file", &Run);
}
}  // namespace meshvale::repair::python
