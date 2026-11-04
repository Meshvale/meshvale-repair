// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_OBSERVER_H_
#define MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_OBSERVER_H_

#include <nanobind/nanobind.h>

#include <stop_token>

namespace meshvale::repair::gltf_file_workflow_detail {
struct FileWorkflowTestOptions;
}
namespace meshvale::interchange {
struct GltfExtractionRequest;
}
namespace meshvale::repair::python {
struct GltfPreset;
}
namespace meshvale::repair::python::binding_test {
class AllocationScope {
 public:
  explicit AllocationScope(bool active) noexcept : active_(active) {}
  ~AllocationScope() noexcept;
  AllocationScope(const AllocationScope&) = delete;
  AllocationScope& operator=(const AllocationScope&) = delete;

 private:
  bool active_;
};
[[nodiscard]] AllocationScope BeforeInstanceAllocation();
[[nodiscard]] AllocationScope BeforeReportBytes();
[[nodiscard]] gltf_file_workflow_detail::FileWorkflowTestOptions
WorkflowOptions();
// Test-only shared token capture before native work, never a Python callback.
void ObserveStopSource(const std::stop_source& source) noexcept;
// Consumes one test-only override before native request construction. Only
// this invocation's owned preset/extraction changes; the public table is fixed.
void ApplyStagedWork(GltfPreset& preset,
                     interchange::GltfExtractionRequest& extraction);
void FinishStagedWork() noexcept;
void AfterNativeReturn() noexcept;
void BeforeReturn(nanobind::handle prepared) noexcept;
void Restore() noexcept;
void RegisterObserverBindings(nanobind::module_& module);
}  // namespace meshvale::repair::python::binding_test
#endif  // MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_OBSERVER_H_
