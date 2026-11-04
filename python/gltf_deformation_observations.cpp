// SPDX-License-Identifier: Apache-2.0
#include "gltf_deformation_observations.h"

#include <Python.h>
#include <meshvale/interchange/gltf.h>
#include <meshvale/repair/gltf_workflow.h>
#include <nanobind/nanobind.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "gltf_presets.h"

// Detached copies of actual inline facts; no validation or arithmetic is run.
namespace meshvale::repair::python {
namespace {
namespace nb = nanobind;
namespace ic = interchange;
struct GltfDeformationKeys {
  static constexpr char kLimits[] = "limits";
  static constexpr char kWork[] = "work";
  static constexpr char kDeformation[] = "deformation";
  static constexpr char kCaptureOptions[] = "capture_options";
  static constexpr char kReadRequest[] = "read_request";
  static constexpr char kSourceRead[] = "source_read";
  static constexpr char kMaterialization[] = "materialization";
  static constexpr char kWriting[] = "writing";
  static constexpr char kReloadRead[] = "reload_read";
  static constexpr char kReload[] = "reload";
  static constexpr char kVerification[] = "verification";
  static constexpr char kRequest[] = "request";
  static constexpr char kExtraction[] = "extraction";
  static constexpr char kPublication[] = "publication";
  static constexpr char kDeformationPublication[] = "deformation_publication";
  static constexpr char kRead[] = "read";
  static constexpr char kDeformationLimits[] = "deformation_limits";
  static constexpr char kDeformationWork[] = "deformation_work";
};
constexpr std::string_view kWeightSumScope = "asset-core-influence-rows";

nb::dict CheckedDictionary() {
  auto value = nb::steal<nb::dict>(PyDict_New());
  if (!value.is_valid()) throw nb::python_error();
  return value;
}
nb::str CheckedText(std::string_view text) {
  constexpr auto kMaxTextBytes =
      static_cast<std::size_t>((std::numeric_limits<Py_ssize_t>::max)());
  if (text.size() > kMaxTextBytes)
    throw nb::value_error("Python text exceeds its size limit");
  auto value = nb::steal<nb::str>(PyUnicode_FromStringAndSize(
      text.data(), static_cast<Py_ssize_t>(text.size())));
  if (!value.is_valid()) throw nb::python_error();
  return value;
}
nb::object Value(const ic::GltfDeformationLimits& x);
nb::object Value(const ic::GltfDeformationWorkPolicy& x);
nb::object Value(const ic::GltfDeformationLocation& x);
nb::object Value(const ic::GltfDeformationCounts& x);
nb::object Value(const ic::GltfDeformationPredicates& x);
nb::object Value(const ic::GltfDeformationExecution& x);
nb::object Value(const ic::GltfDeformationExceptionMasks& x);
nb::object Value(const ic::GltfWeightSumSample& x);
nb::object Value(const ic::GltfDiagnostic& x);
nb::object Value(const ic::GltfWeightSumSummary& x);
nb::object Value(const ic::GltfQuaternionSourceRefusal& x);
nb::object Value(const ic::GltfQuaternionUnitSummary& x);
nb::object Value(const ic::GltfDeformationNumericRefusal& x);
nb::object Value(const ic::GltfDeformationNumericObservation& x);
nb::object Value(const ic::GltfDeformationObservation& x);
nb::object Value(const ic::GltfNormativeRevision& x);
nb::object Value(const ic::GltfSourceSpan& x);
template <class T>
nb::object Value(const T& x) {
  static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>);
  if constexpr (std::is_enum_v<T>)
    return nb::cast(static_cast<std::uint64_t>(x));
  else
    return nb::cast(x);
}
nb::object Value(std::string_view x) { return CheckedText(x); }
template <class T>
nb::object Value(const std::optional<T>& x) {
  return x ? Value(*x) : nb::none();
}
template <std::size_t N, class... T>
nb::dict Object(const std::array<std::string_view, N>& keys,
                const T&... values) {
  static_assert(N == sizeof...(T));
  nb::dict result = CheckedDictionary();
  std::size_t index = 0;
  const auto field = [&](const auto& value) {
    const auto key = keys[index++];
    result[CheckedText(key)] = Value(value);
  };
  (field(values), ...);
  return result;
}
nb::object Value(const ic::GltfDeformationLimits& x) {
  constexpr std::array<std::string_view, 18> kKeys{
      "max_node_rows",        "max_mesh_users",     "max_skin_rows",
      "max_joint_references", "max_influence_sets", "max_lanes_per_vertex",
      "max_morph_targets",    "max_morph_channels", "max_animation_channels",
      "max_samplers",         "max_keys",           "max_scalar_values",
      "max_sparse_writes",    "max_role_edges",     "max_relationship_steps",
      "max_descriptor_bytes", "max_decoded_bytes",  "max_new_requested_bytes"};
  return Object(
      kKeys, x.max_node_rows, x.max_mesh_users, x.max_skin_rows,
      x.max_joint_references, x.max_influence_sets, x.max_lanes_per_vertex,
      x.max_morph_targets, x.max_morph_channels, x.max_animation_channels,
      x.max_samplers, x.max_keys, x.max_scalar_values, x.max_sparse_writes,
      x.max_role_edges, x.max_relationship_steps, x.max_descriptor_bytes,
      x.max_decoded_bytes, x.max_new_requested_bytes);
}
nb::object Value(const ic::GltfDeformationWorkPolicy& x) {
  constexpr std::array<std::string_view, 8> kKeys{"parallel_min_scalars",
                                                  "partition_scalars",
                                                  "cancellation_block_scalars",
                                                  "parallel_min_bytes",
                                                  "partition_bytes",
                                                  "cancellation_block_bytes",
                                                  "max_jobs",
                                                  "max_relationship_block"};
  return Object(kKeys, x.parallel_min_scalars, x.partition_scalars,
                x.cancellation_block_scalars, x.parallel_min_bytes,
                x.partition_bytes, x.cancellation_block_bytes, x.max_jobs,
                x.max_relationship_block);
}
nb::object Value(const ic::GltfDeformationLocation& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "user",    "skin",    "target", "set",
      "sampler", "channel", "vertex", "component"};
  return Object(kKeys, x.user, x.skin, x.target, x.set, x.sampler, x.channel,
                x.vertex, x.component);
}
nb::object Value(const ic::GltfDeformationCounts& x) {
  constexpr std::array<std::string_view, 15> kKeys{
      "node_rows",         "mesh_users",         "skin_rows",
      "joint_references",  "influence_sets",     "lane_checks",
      "morph_targets",     "morph_channels",     "animation_channels",
      "distinct_samplers", "keys_checked",       "scalar_checks",
      "sparse_writes",     "relationship_steps", "decoded_bytes_checked"};
  return Object(kKeys, x.node_rows, x.mesh_users, x.skin_rows,
                x.joint_references, x.influence_sets, x.lane_checks,
                x.morph_targets, x.morph_channels, x.animation_channels,
                x.distinct_samplers, x.keys_checked, x.scalar_checks,
                x.sparse_writes, x.relationship_steps, x.decoded_bytes_checked);
}
nb::object Value(const ic::GltfDeformationPredicates& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "source_admission", "influences",          "bindings",
      "morphs",           "affecting_animation", "canonical_association",
      "output_reload",    "staged_reload"};
  return Object(kKeys, x.source_admission, x.influences, x.bindings, x.morphs,
                x.affecting_animation, x.canonical_association, x.output_reload,
                x.staged_reload);
}
nb::object Value(const ic::GltfDeformationExecution& x) {
  constexpr std::array<std::string_view, 4> kKeys{
      "planned_jobs", "launched_workers", "joined_workers", "caller_jobs"};
  return Object(kKeys, x.planned_jobs, x.launched_workers, x.joined_workers,
                x.caller_jobs);
}
nb::object Value(const ic::GltfDeformationExceptionMasks& x) {
  constexpr std::array<std::string_view, 5> kKeys{
      "required_units", "sse2_mask", "x87_mask", "sample_partition",
      "sample_caller"};
  return Object(kKeys, x.required_units, x.sse2_mask, x.x87_mask,
                x.sample_partition, x.sample_caller);
}
nb::object Value(const ic::GltfWeightSumSample& x) {
  constexpr std::array<std::string_view, 10> kKeys{
      "mesh",           "primitive",       "vertex",
      "set_count",      "nonzero_weights", "sum_bits",
      "deviation_bits", "epsilon_bits",    "advisory",
      "all_zero"};
  return Object(kKeys, x.mesh, x.primitive, x.vertex, x.set_count,
                x.nonzero_weights, x.sum_bits, x.deviation_bits, x.epsilon_bits,
                x.advisory, x.all_zero);
}
nb::object Value(const ic::GltfNormativeRevision& x) {
  constexpr std::array<std::string_view, 2> kKeys{"commit", "blob"};
  return Object(kKeys, x.commit, x.blob);
}
nb::object Value(const ic::GltfSourceSpan& x) {
  constexpr std::array<std::string_view, 2> kKeys{"offset", "length"};
  return Object(kKeys, x.offset, x.length);
}
nb::object Value(const ic::GltfDiagnostic& x) {
  constexpr std::array<std::string_view, 7> kKeys{
      "status",         "code",          "json_span", "resource_kind",
      "resource_index", "resource_span", "rule_id"};
  return Object(kKeys, static_cast<std::uint64_t>(x.status),
                static_cast<std::uint64_t>(x.code), x.json_span,
                x.resource_kind, x.resource_index, x.resource_span, x.rule_id);
}
nb::object Value(const ic::GltfWeightSumSummary& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "scope",           "complete",      "floating_rows_expected",
      "rows_evaluated",  "advisory_rows", "all_zero_rows",
      "first_evaluated", "first_advisory"};
  return Object(kKeys, kWeightSumScope, x.complete, x.floating_rows_expected,
                x.rows_evaluated, x.advisory_rows, x.all_zero_rows,
                x.first_evaluated, x.first_advisory);
}
constexpr std::array<std::string_view, 3> kQuaternionRoles{
    "node-rotation", "animation-value", "cubic-tangent"};
constexpr std::array<std::string_view, 6> kQuaternionEncodings{
    "json-decimal", "float32", "snorm8", "unorm8", "snorm16", "unorm16"};
constexpr std::array<std::string_view, 4> kQuaternionReasons{
    "range", "nonfinite", "nonunit", "representation"};
constexpr std::array<std::string_view, 5> kNumericReasons{
    "compiled-unsupported", "unqueryable", "rounding", "flush", "exceptions"};
constexpr std::array<std::string_view, 2> kFloatingUnits{"sse2", "x87"};
constexpr std::string_view kNumericalProfile =
    "gltf-deformation-rne-gradual-v1";
constexpr std::string_view kProcessingProfile = "deformation-retained-v1";
constexpr std::string_view kReadAdmissionProfile =
    "retained-core-deformation-v1";
nb::object Value(const ic::GltfQuaternionSourceRefusal& x) {
  constexpr std::array<std::string_view, 9> kKeys{
      "role", "encoding", "node", "animation", "sampler",
      "key",  "accessor", "row",  "reason"};
  return Object(kKeys, kQuaternionRoles[static_cast<std::size_t>(x.role)],
                kQuaternionEncodings[static_cast<std::size_t>(x.encoding)],
                x.node, x.animation, x.sampler, x.key, x.accessor, x.row,
                kQuaternionReasons[static_cast<std::size_t>(x.reason)]);
}
nb::object Value(const ic::GltfQuaternionUnitSummary& x) {
  constexpr std::array<std::string_view, 7> kKeys{"policy",
                                                  "complete",
                                                  "node_values_checked",
                                                  "animation_values_checked",
                                                  "tangent_rows_checked",
                                                  "rows_admitted",
                                                  "first_refusal"};
  return Object(kKeys, x.policy, x.complete, x.node_values_checked,
                x.animation_values_checked, x.tangent_rows_checked,
                x.rows_admitted, x.first_refusal);
}
nb::object Value(const ic::GltfDeformationNumericRefusal& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "reason",       "partition",    "caller", "nearest_even",
      "ftz_disabled", "daz_disabled", "unit",   "exception_masks"};
  return Object(kKeys, kNumericReasons[static_cast<std::size_t>(x.reason)],
                x.partition, x.caller, x.nearest_even, x.ftz_disabled,
                x.daz_disabled,
                x.unit ? std::optional<std::string_view>(
                             kFloatingUnits[static_cast<std::size_t>(*x.unit)])
                       : std::nullopt,
                x.exception_masks);
}
nb::object Value(const ic::GltfDeformationNumericObservation& x) {
  constexpr std::array<std::string_view, 7> kKeys{
      "profile",      "compiled_profile", "guard_calls", "established_calls",
      "exact_blocks", "exception_masks",  "refusal"};
  return Object(kKeys, kNumericalProfile, x.compiled_profile, x.guard_calls,
                x.established_calls, x.exact_blocks, x.exception_masks,
                x.refusal);
}
nb::object Value(const ic::GltfDeformationObservation& x) {
  constexpr std::array<std::string_view, 14> kKeys{"status",
                                                   "diagnostic",
                                                   "phase",
                                                   "complete",
                                                   "location",
                                                   "counts",
                                                   "predicates",
                                                   "new_requested_bytes",
                                                   "effective_request_cap",
                                                   "work",
                                                   "execution",
                                                   "numeric",
                                                   "weight_sums",
                                                   "quaternions"};
  return Object(kKeys, static_cast<std::uint64_t>(x.status), x.diagnostic,
                static_cast<std::uint64_t>(x.phase), x.complete, x.location,
                x.counts, x.predicates, x.new_requested_bytes,
                x.effective_request_cap, x.work, x.execution, x.numeric,
                x.weight_sums, x.quaternions);
}
nb::dict Processing(const GltfWorkflowDeformationFacts& f) {
  constexpr std::array<std::string_view, 15> kKeys{"profile",
                                                   "read_admission_profile",
                                                   "normative_revision",
                                                   "quaternion_policy",
                                                   "capture_read",
                                                   "source_read",
                                                   "source",
                                                   "materialization",
                                                   "writing",
                                                   "reload_read",
                                                   "reload",
                                                   "verification",
                                                   "publication_read",
                                                   "publication_reload",
                                                   "publication_verification"};
  return Object(kKeys, kProcessingProfile, kReadAdmissionProfile,
                f.normative_revision, f.quaternion_policy, f.capture_read,
                f.source_read, f.source, f.materialization, f.writing,
                f.reload_read, f.reload, f.verification, f.publication_read,
                f.publication_reload, f.publication_verification);
}
nb::dict Policy(const ic::GltfDeformationPolicy& policy) {
  nb::dict result = CheckedDictionary();
  result[GltfDeformationKeys::kLimits] = Value(policy.limits);
  result[GltfDeformationKeys::kWork] = Value(policy.work);
  return result;
}
}  // namespace
nb::dict ObserveGltfDeformation(const GltfWorkflowDeformationFacts& facts) {
  return Processing(facts);
}
void AppendGltfDeformationPreset(nb::dict& result, const GltfPreset& preset) {
  nb::dict capture = CheckedDictionary();
  nb::dict read = CheckedDictionary();
  read[GltfDeformationKeys::kDeformation] =
      Policy(preset.capture_options.read_request.deformation);
  capture[GltfDeformationKeys::kReadRequest] = std::move(read);
  result[GltfDeformationKeys::kCaptureOptions] = std::move(capture);
  nb::dict request = CheckedDictionary();
  nb::dict phases = CheckedDictionary();
  nb::dict extraction = CheckedDictionary();
  phases[GltfDeformationKeys::kSourceRead] =
      Policy(preset.deformation.source_read);
  phases[GltfDeformationKeys::kMaterialization] =
      Policy(preset.deformation.materialization);
  phases[GltfDeformationKeys::kWriting] = Policy(preset.deformation.writing);
  phases[GltfDeformationKeys::kReloadRead] =
      Policy(preset.deformation.reload_read);
  phases[GltfDeformationKeys::kReload] = Policy(preset.deformation.reload);
  phases[GltfDeformationKeys::kVerification] =
      Policy(preset.deformation.verification);
  request[GltfDeformationKeys::kDeformation] = std::move(phases);
  extraction[GltfDeformationKeys::kDeformation] =
      Policy(preset.source_deformation);
  request[GltfDeformationKeys::kExtraction] = std::move(extraction);
  result[GltfDeformationKeys::kRequest] = std::move(request);
  nb::dict publication = CheckedDictionary();
  nb::dict policies = CheckedDictionary();
  policies[GltfDeformationKeys::kRead] =
      Policy(preset.deformation_publication.read);
  policies[GltfDeformationKeys::kExtraction] =
      Policy(preset.deformation_publication.extraction);
  policies[GltfDeformationKeys::kVerification] =
      Policy(preset.deformation_publication.verification);
  publication[GltfDeformationKeys::kDeformationPublication] =
      std::move(policies);
  result[GltfDeformationKeys::kPublication] = std::move(publication);
}
nb::dict ObserveGltfFileDeformationOptions(
    const GltfWorkflowDeformationFacts& metadata, const GltfPreset& preset,
    bool publication_requested) {
  constexpr std::array<std::string_view, 5> kMetadataKeys{
      "processing_profile", "read_admission_profile", "normative_revision",
      "numerical_profile", "quaternion_policy"};
  auto result = Object(kMetadataKeys, kProcessingProfile, kReadAdmissionProfile,
                       metadata.normative_revision, kNumericalProfile,
                       metadata.quaternion_policy);
  constexpr std::size_t kPhaseCount = 11;
  constexpr std::array<std::string_view, kPhaseCount> kPhases{
      "capture_read",
      "source_read",
      "source",
      "materialization",
      "writing",
      "reload_read",
      "reload",
      "verification",
      "publication_read",
      "publication_reload",
      "publication_verification"};
  const std::array<const ic::GltfDeformationPolicy*, kPhaseCount> policies{
      &preset.capture_options.read_request.deformation,
      nullptr,  // The file engine retains the actual capture owner.
      &preset.source_deformation,
      &preset.deformation.materialization,
      &preset.deformation.writing,
      &preset.deformation.reload_read,
      &preset.deformation.reload,
      &preset.deformation.verification,
      publication_requested ? &preset.deformation_publication.read : nullptr,
      publication_requested ? &preset.deformation_publication.extraction
                            : nullptr,
      publication_requested ? &preset.deformation_publication.verification
                            : nullptr};
  nb::dict limits = CheckedDictionary();
  nb::dict work = CheckedDictionary();
  for (std::size_t i = 0; i < kPhaseCount; ++i) {
    const auto name = kPhases[i];
    const nb::str key = CheckedText(name);
    limits[key] = policies[i] ? Value(policies[i]->limits) : nb::none();
    work[key] = policies[i] ? Value(policies[i]->work) : nb::none();
  }
  result[GltfDeformationKeys::kDeformationLimits] = std::move(limits);
  result[GltfDeformationKeys::kDeformationWork] = std::move(work);
  return result;
}
}  // namespace meshvale::repair::python
