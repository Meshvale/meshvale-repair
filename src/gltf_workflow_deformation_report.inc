// SPDX-License-Identifier: Apache-2.0
// Fixed-size, nonallocating projection into the existing two-pass report sink.
// Included only in gltf_workflow_report.cpp after its diagnostic/span helpers.
namespace deformation_report {
namespace ic = interchange;
void Value(Sink& out, const ic::GltfDeformationLimits& x);
void Value(Sink& out, const ic::GltfDeformationWorkPolicy& x);
void Value(Sink& out, const ic::GltfDeformationLocation& x);
void Value(Sink& out, const ic::GltfDeformationCounts& x);
void Value(Sink& out, const ic::GltfDeformationPredicates& x);
void Value(Sink& out, const ic::GltfDeformationExecution& x);
void Value(Sink& out, const ic::GltfDeformationExceptionMasks& x);
void Value(Sink& out, const ic::GltfWeightSumSample& x);
void Value(Sink& out, const ic::GltfDiagnostic& x);
void Value(Sink& out, const ic::GltfWeightSumSummary& x);
void Value(Sink& out, const ic::GltfQuaternionSourceRefusal& x);
void Value(Sink& out, const ic::GltfQuaternionUnitSummary& x);
void Value(Sink& out, const ic::GltfDeformationNumericRefusal& x);
void Value(Sink& out, const ic::GltfDeformationNumericObservation& x);
void Value(Sink& out, const ic::GltfDeformationObservation& x);
void Value(Sink& out, const ic::GltfNormativeRevision& x);
void Value(Sink& out, std::uint64_t x) { out.Integer(x); }
void Value(Sink& out, std::uint32_t x) { out.Integer(x); }
void Value(Sink& out, bool x) { out.Boolean(x); }
void Value(Sink& out, std::string_view x) { out.String(x); }
template <class T>
void Value(Sink& out, const std::optional<T>& x) {
  if (x)
    Value(out, *x);
  else
    out.Raw("null");
}
template <std::size_t N, class... T>
void Object(Sink& out, const std::array<std::string_view, N>& keys,
            const T&... values) {
  static_assert(N == sizeof...(T));
  out.Raw("{");
  std::size_t index = 0;
  const auto field = [&](const auto& value) {
    if (index) out.Raw(",");
    out.String(keys[index++]);
    out.Raw(":");
    Value(out, value);
  };
  (field(values), ...);
  out.Raw("}");
}
void Value(Sink& out, const ic::GltfDeformationLimits& x) {
  constexpr std::array<std::string_view, 18> kKeys{
      "max_node_rows",        "max_mesh_users",     "max_skin_rows",
      "max_joint_references", "max_influence_sets", "max_lanes_per_vertex",
      "max_morph_targets",    "max_morph_channels", "max_animation_channels",
      "max_samplers",         "max_keys",           "max_scalar_values",
      "max_sparse_writes",    "max_role_edges",     "max_relationship_steps",
      "max_descriptor_bytes", "max_decoded_bytes",  "max_new_requested_bytes"};
  Object(out, kKeys, x.max_node_rows, x.max_mesh_users, x.max_skin_rows,
         x.max_joint_references, x.max_influence_sets, x.max_lanes_per_vertex,
         x.max_morph_targets, x.max_morph_channels, x.max_animation_channels,
         x.max_samplers, x.max_keys, x.max_scalar_values, x.max_sparse_writes,
         x.max_role_edges, x.max_relationship_steps, x.max_descriptor_bytes,
         x.max_decoded_bytes, x.max_new_requested_bytes);
}
void Value(Sink& out, const ic::GltfDeformationWorkPolicy& x) {
  constexpr std::array<std::string_view, 8> kKeys{"parallel_min_scalars",
                                                  "partition_scalars",
                                                  "cancellation_block_scalars",
                                                  "parallel_min_bytes",
                                                  "partition_bytes",
                                                  "cancellation_block_bytes",
                                                  "max_jobs",
                                                  "max_relationship_block"};
  Object(out, kKeys, x.parallel_min_scalars, x.partition_scalars,
         x.cancellation_block_scalars, x.parallel_min_bytes, x.partition_bytes,
         x.cancellation_block_bytes, x.max_jobs, x.max_relationship_block);
}
void Value(Sink& out, const ic::GltfDeformationLocation& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "user",    "skin",    "target", "set",
      "sampler", "channel", "vertex", "component"};
  Object(out, kKeys, x.user, x.skin, x.target, x.set, x.sampler, x.channel,
         x.vertex, x.component);
}
void Value(Sink& out, const ic::GltfDeformationCounts& x) {
  constexpr std::array<std::string_view, 15> kKeys{
      "node_rows",         "mesh_users",         "skin_rows",
      "joint_references",  "influence_sets",     "lane_checks",
      "morph_targets",     "morph_channels",     "animation_channels",
      "distinct_samplers", "keys_checked",       "scalar_checks",
      "sparse_writes",     "relationship_steps", "decoded_bytes_checked"};
  Object(out, kKeys, x.node_rows, x.mesh_users, x.skin_rows, x.joint_references,
         x.influence_sets, x.lane_checks, x.morph_targets, x.morph_channels,
         x.animation_channels, x.distinct_samplers, x.keys_checked,
         x.scalar_checks, x.sparse_writes, x.relationship_steps,
         x.decoded_bytes_checked);
}
void Value(Sink& out, const ic::GltfDeformationPredicates& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "source_admission", "influences",          "bindings",
      "morphs",           "affecting_animation", "canonical_association",
      "output_reload",    "staged_reload"};
  Object(out, kKeys, x.source_admission, x.influences, x.bindings, x.morphs,
         x.affecting_animation, x.canonical_association, x.output_reload,
         x.staged_reload);
}
void Value(Sink& out, const ic::GltfDeformationExecution& x) {
  constexpr std::array<std::string_view, 4> kKeys{
      "planned_jobs", "launched_workers", "joined_workers", "caller_jobs"};
  Object(out, kKeys, x.planned_jobs, x.launched_workers, x.joined_workers,
         x.caller_jobs);
}
void Value(Sink& out, const ic::GltfDeformationExceptionMasks& x) {
  constexpr std::array<std::string_view, 5> kKeys{
      "required_units", "sse2_mask", "x87_mask", "sample_partition",
      "sample_caller"};
  Object(out, kKeys, x.required_units, x.sse2_mask, x.x87_mask,
         x.sample_partition, x.sample_caller);
}
void Value(Sink& out, const ic::GltfWeightSumSample& x) {
  constexpr std::array<std::string_view, 10> kKeys{
      "mesh",           "primitive",       "vertex",
      "set_count",      "nonzero_weights", "sum_bits",
      "deviation_bits", "epsilon_bits",    "advisory",
      "all_zero"};
  Object(out, kKeys, x.mesh, x.primitive, x.vertex, x.set_count,
         x.nonzero_weights, x.sum_bits, x.deviation_bits, x.epsilon_bits,
         x.advisory, x.all_zero);
}
void Value(Sink& out, const ic::GltfNormativeRevision& x) {
  constexpr std::array<std::string_view, 2> kKeys{"commit", "blob"};
  Object(out, kKeys, x.commit, x.blob);
}
void Value(Sink& out, const ic::GltfDiagnostic& x) {
  constexpr std::array<std::string_view, 7> kKeys{
      "status",         "code",          "json_span", "resource_kind",
      "resource_index", "resource_span", "rule_id"};
  out.Raw("{");
  for (std::size_t i = 0; i < kKeys.size(); ++i) {
    if (i) out.Raw(",");
    out.String(kKeys[i]);
    out.Raw(":");
    switch (i) {
      case 0:
        out.Integer(static_cast<std::uint64_t>(x.status));
        break;
      case 1:
        out.Integer(static_cast<std::uint64_t>(x.code));
        break;
      case 2:
        SpanReceipt(out, x.json_span);
        break;
      case 3:
        out.OptionalInteger(x.resource_kind);
        break;
      case 4:
        out.OptionalInteger(x.resource_index);
        break;
      case 5:
        SpanReceipt(out, x.resource_span);
        break;
      case 6:
        out.Integer(x.rule_id);
        break;
    }
  }
  out.Raw("}");
}
void Value(Sink& out, const ic::GltfWeightSumSummary& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "scope",           "complete",      "floating_rows_expected",
      "rows_evaluated",  "advisory_rows", "all_zero_rows",
      "first_evaluated", "first_advisory"};
  Object(out, kKeys, std::string_view("asset-core-influence-rows"), x.complete,
         x.floating_rows_expected, x.rows_evaluated, x.advisory_rows,
         x.all_zero_rows, x.first_evaluated, x.first_advisory);
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
void Value(Sink& out, const ic::GltfQuaternionSourceRefusal& x) {
  constexpr std::array<std::string_view, 9> kKeys{
      "role", "encoding", "node", "animation", "sampler",
      "key",  "accessor", "row",  "reason"};
  Object(out, kKeys, kQuaternionRoles[static_cast<std::size_t>(x.role)],
         kQuaternionEncodings[static_cast<std::size_t>(x.encoding)], x.node,
         x.animation, x.sampler, x.key, x.accessor, x.row,
         kQuaternionReasons[static_cast<std::size_t>(x.reason)]);
}
void Value(Sink& out, const ic::GltfQuaternionUnitSummary& x) {
  constexpr std::array<std::string_view, 7> kKeys{"policy",
                                                  "complete",
                                                  "node_values_checked",
                                                  "animation_values_checked",
                                                  "tangent_rows_checked",
                                                  "rows_admitted",
                                                  "first_refusal"};
  Object(out, kKeys, x.policy, x.complete, x.node_values_checked,
         x.animation_values_checked, x.tangent_rows_checked, x.rows_admitted,
         x.first_refusal);
}
void Value(Sink& out, const ic::GltfDeformationNumericRefusal& x) {
  constexpr std::array<std::string_view, 8> kKeys{
      "reason",       "partition",    "caller", "nearest_even",
      "ftz_disabled", "daz_disabled", "unit",   "exception_masks"};
  Object(out, kKeys, kNumericReasons[static_cast<std::size_t>(x.reason)],
         x.partition, x.caller, x.nearest_even, x.ftz_disabled, x.daz_disabled,
         x.unit ? std::optional<std::string_view>(
                      kFloatingUnits[static_cast<std::size_t>(*x.unit)])
                : std::nullopt,
         x.exception_masks);
}
void Value(Sink& out, const ic::GltfDeformationNumericObservation& x) {
  constexpr std::array<std::string_view, 7> kKeys{
      "profile",      "compiled_profile", "guard_calls", "established_calls",
      "exact_blocks", "exception_masks",  "refusal"};
  Object(out, kKeys, kNumericalProfile, x.compiled_profile, x.guard_calls,
         x.established_calls, x.exact_blocks, x.exception_masks, x.refusal);
}
void Value(Sink& out, const ic::GltfDeformationObservation& x) {
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
  Object(out, kKeys, static_cast<std::uint64_t>(x.status), x.diagnostic,
         static_cast<std::uint64_t>(x.phase), x.complete, x.location, x.counts,
         x.predicates, x.new_requested_bytes, x.effective_request_cap, x.work,
         x.execution, x.numeric, x.weight_sums, x.quaternions);
}
constexpr std::array<std::string_view, kDeformationPhaseCount> kPhases{
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
void Processing(Sink& out, const GltfWorkflowDeformationFacts& f) {
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
  Object(out, kKeys, kProcessingProfile, kReadAdmissionProfile,
         f.normative_revision, f.quaternion_policy, f.capture_read,
         f.source_read, f.source, f.materialization, f.writing, f.reload_read,
         f.reload, f.verification, f.publication_read, f.publication_reload,
         f.publication_verification);
}
void Options(Sink& out, const GltfWorkflowDeformationFacts& f,
             const std::array<std::optional<ic::GltfDeformationPolicy>,
                              kDeformationPhaseCount>& policies) {
  constexpr std::array<std::string_view, 7> kKeys{
      "processing_profile", "read_admission_profile", "normative_revision",
      "numerical_profile",  "quaternion_policy",      "deformation_limits",
      "deformation_work"};
  const auto scalar = [&](std::size_t index, const auto& value) {
    out.Raw(",");
    out.String(kKeys[index]);
    out.Raw(":");
    Value(out, value);
  };
  scalar(0, kProcessingProfile);
  scalar(1, kReadAdmissionProfile);
  scalar(2, f.normative_revision);
  scalar(3, kNumericalProfile);
  scalar(4, f.quaternion_policy);
  for (std::size_t table = 0; table < 2; ++table) {
    out.Raw(",");
    out.String(kKeys[5 + table]);
    out.Raw(":{");
    for (std::size_t i = 0; i < kPhases.size(); ++i) {
      if (i) out.Raw(",");
      out.String(kPhases[i]);
      out.Raw(":");
      if (!policies[i])
        out.Raw("null");
      else if (table == 0)
        Value(out, policies[i]->limits);
      else
        Value(out, policies[i]->work);
    }
    out.Raw("}");
  }
}
}  // namespace deformation_report
