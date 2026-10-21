// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_OBSERVER_H_
#define MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_OBSERVER_H_

#include <nanobind/nanobind.h>

namespace meshvale::repair::gltf_file_workflow_detail {
struct FileWorkflowTestOptions;
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
void AfterNativeReturn() noexcept;
void BeforeReturn(nanobind::handle prepared) noexcept;
void Restore() noexcept;
void RegisterObserverBindings(nanobind::module_& module);
}  // namespace meshvale::repair::python::binding_test
#endif  // MESHVALE_REPAIR_TESTS_PYTHON_NATIVE_GLTF_BINDING_OBSERVER_H_
