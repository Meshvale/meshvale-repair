// SPDX-License-Identifier: Apache-2.0
#include "gltf_binding_observer.h"

#include <Python.h>
#include <nanobind/stl/string_view.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>

#include "gltf_binding_allocation.h"
#include "gltf_file_workflow_test_internal.h"
#include "materialized_duplicate_allocation.h"
#include "materialized_duplicate_workers.h"

namespace meshvale::repair::python::binding_test {
namespace {
namespace nb = nanobind;
namespace allocation = binding_test_allocation;
namespace own = materialized_duplicates_detail;
enum class Mode {
  kIdle,
  kNormal,
  kPositive,
  kPositiveRefused,
  kReportCanceled,
  kInstance,
  kReport
};
enum class ProbeOutcome {
  kNotAttempted,
  kSucceeded,
  kPythonFailure,
  kCppFailure,
  kUnexpectedFailure
};
constexpr std::size_t kDomainCount = 3;
constexpr std::array<PyMemAllocatorDomain, kDomainCount> kDomains{
    PYMEM_DOMAIN_RAW, PYMEM_DOMAIN_MEM, PYMEM_DOMAIN_OBJ};
constexpr std::string_view kNormalMode = "normal";
constexpr std::string_view kPositiveMode = "positive-wrapper";
constexpr std::string_view kPositiveRefusedMode = "positive-wrapper-refused";
constexpr std::string_view kReportCanceledMode = "report-canceled";
constexpr std::string_view kInstanceMode = "instance-refused";
constexpr std::string_view kReportMode = "report-refused";
struct Domain {
  PyMemAllocatorEx original{};
  std::atomic<std::size_t> requests{0};
};
static_assert(std::atomic<std::size_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
std::array<Domain, kDomainCount> domains;
constinit std::atomic<bool> counting{false};
constinit std::atomic<bool> fail_next{false};
Mode mode = Mode::kIdle;
ProbeOutcome probe = ProbeOutcome::kNotAttempted;
bool installed = false;
bool return_armed = false;
std::size_t cpp_before = 0;
std::size_t cpp_delta = 0;
std::array<std::size_t, kDomainCount> python_delta{};
std::size_t pre_probe_cpp = 0;
std::size_t pre_probe_python = 0;
std::size_t refusal_attempts = 0;
constinit std::atomic<std::size_t> failures{0};
struct OriginalRequests {
  std::atomic<std::size_t> calls{0};
  std::atomic<std::size_t> live{0};
  std::atomic<std::size_t> after_freeze{0};
  std::atomic<bool> frozen{false};
  static void* Allocate(std::size_t bytes, std::size_t alignment,
                        void* opaque) {
    auto& self = *static_cast<OriginalRequests*>(opaque);
    self.calls.fetch_add(1, std::memory_order_relaxed);
    if (self.frozen.load(std::memory_order_relaxed)) {
      self.after_freeze.fetch_add(1, std::memory_order_relaxed);
      throw std::bad_alloc();
    }
    const auto original = own::DefaultAllocationBackend();
    auto* pointer = original.allocate(bytes, alignment, original.state);
    self.live.fetch_add(1, std::memory_order_relaxed);
    return pointer;
  }
  static void Deallocate(void* pointer, std::size_t bytes,
                         std::size_t alignment, void* opaque) noexcept {
    auto& self = *static_cast<OriginalRequests*>(opaque);
    const auto original = own::DefaultAllocationBackend();
    original.deallocate(pointer, bytes, alignment, original.state);
    self.live.fetch_sub(1, std::memory_order_relaxed);
  }
  own::AllocationBackend Backend() noexcept {
    return {Allocate, Deallocate, this};
  }
};
OriginalRequests wrapper;
OriginalRequests memory;
std::size_t prepared_reports = 0;
std::optional<std::stop_source> observed_stop;
constexpr std::size_t kFaultWorkerCapacity = 64;
struct ReportWorkers {
  struct Slot {
    own::NativeWorkerRecord wrapped;
    own::NativeWorkerRecord* original = nullptr;
    ReportWorkers* owner = nullptr;
  };
  std::array<Slot, kFaultWorkerCapacity> slots{};
  std::size_t launched = 0;
  std::size_t joined = 0;
  std::atomic<std::size_t> entered{0};
  std::atomic<bool> stop_requested{false};
  std::atomic<bool> stop_accepted{false};
  static void Entry(own::NativeWorkerRecord& record) noexcept {
    auto& slot = *static_cast<Slot*>(record.state);
    auto& self = *slot.owner;
    if (!self.stop_requested.exchange(true)) {
      self.stop_accepted = observed_stop && observed_stop->request_stop();
      // The real report worker records are already admitted. Freeze only the
      // named Repair backends; the publisher is never replaced or invoked.
      wrapper.frozen = memory.frozen = true;
    }
    ++self.entered;
    slot.original->entry(*slot.original);
  }
  static std::uintptr_t Launch(own::NativeWorkerRecord* record, void* opaque) {
    auto& self = *static_cast<ReportWorkers*>(opaque);
    if (self.launched == self.slots.size())
      throw nb::value_error("report worker observer capacity exhausted");
    auto& slot = self.slots[self.launched];
    slot.original = record;
    slot.owner = &self;
    slot.wrapped = {Entry, &slot, record->ordinal};
    const auto original = own::DefaultNativeWorkerBackend();
    const auto handle = original.launch(&slot.wrapped, original.state);
    ++self.launched;
    return handle;
  }
  static void Join(std::uintptr_t handle, void* opaque) noexcept {
    auto& self = *static_cast<ReportWorkers*>(opaque);
    const auto original = own::DefaultNativeWorkerBackend();
    original.join(handle, original.state);
    ++self.joined;
  }
  own::NativeWorkerBackend Backend() noexcept { return {Launch, Join, this}; }
};
ReportWorkers report_workers;
bool Request(Domain& domain) noexcept {
  if (!counting.load(std::memory_order_relaxed)) return false;
  domain.requests.fetch_add(1, std::memory_order_relaxed);
  if (!fail_next.exchange(false, std::memory_order_relaxed)) return false;
  failures.fetch_add(1, std::memory_order_relaxed);
  return true;
}
void* Malloc(void* context, std::size_t size) noexcept {
  auto& domain = *static_cast<Domain*>(context);
  if (Request(domain)) return nullptr;
  return domain.original.malloc(domain.original.ctx, size);
}
void* Calloc(void* context, std::size_t count, std::size_t size) noexcept {
  auto& domain = *static_cast<Domain*>(context);
  if (Request(domain)) return nullptr;
  return domain.original.calloc(domain.original.ctx, count, size);
}
void* Realloc(void* context, void* pointer, std::size_t size) noexcept {
  auto& domain = *static_cast<Domain*>(context);
  if (Request(domain)) return nullptr;
  return domain.original.realloc(domain.original.ctx, pointer, size);
}
void Free(void* context, void* pointer) noexcept {
  auto& domain = *static_cast<Domain*>(context);
  domain.original.free(domain.original.ctx, pointer);
}
void Install() {
  if (installed || !PyGILState_Check())
    throw nb::value_error(
        "observer installation requires one GIL-owned session");
  for (std::size_t i = 0; i < domains.size(); ++i) {
    auto& domain = domains[i];
    PyMem_GetAllocator(kDomains[i], &domain.original);
    domain.requests = 0;
    PyMemAllocatorEx hooks{&domain, Malloc, Calloc, Realloc, Free};
    PyMem_SetAllocator(kDomains[i], &hooks);
  }
  installed = true;
}
std::size_t PythonRequests() noexcept {
  std::size_t total = 0;
  for (const auto& domain : domains)
    total += domain.requests.load(std::memory_order_relaxed);
  return total;
}
void Require(bool value, const char* message) {
  if (!value) throw nb::value_error(message);
}
void Controls() {
  constexpr std::size_t kAlignment = 64;
  const auto before = allocation::Calls();
  allocation::Refuse(true);
  bool ordinary_refused = false;
  bool aligned_refused = false;
  try {
    auto* pointer = ::operator new(1);
    ::operator delete(pointer);
  } catch (const std::bad_alloc&) {
    ordinary_refused = true;
  }
  try {
    auto* pointer = ::operator new(kAlignment, std::align_val_t{kAlignment});
    ::operator delete(pointer, std::align_val_t{kAlignment});
  } catch (const std::bad_alloc&) {
    aligned_refused = true;
  }
  allocation::Refuse(false);
  Require(
      ordinary_refused && aligned_refused && allocation::Calls() == before + 2,
      "actual ordinary/aligned original allocation refusal controls");
}
void Setup(std::string_view selected) {
  if (installed) {
    Restore();
    throw nb::value_error("observer session already active");
  }
  Controls();
  if (selected == kNormalMode)
    mode = Mode::kNormal;
  else if (selected == kPositiveMode)
    mode = Mode::kPositive;
  else if (selected == kPositiveRefusedMode)
    mode = Mode::kPositiveRefused;
  else if (selected == kReportCanceledMode)
    mode = Mode::kReportCanceled;
  else if (selected == kInstanceMode)
    mode = Mode::kInstance;
  else if (selected == kReportMode)
    mode = Mode::kReport;
  else
    throw nb::value_error("unknown test observer mode");
  probe = ProbeOutcome::kNotAttempted;
  return_armed = false;
  cpp_delta = pre_probe_cpp = pre_probe_python = refusal_attempts = 0;
  python_delta.fill(0);
  failures = 0;
  wrapper.calls = memory.calls = 0;
  wrapper.after_freeze = memory.after_freeze = 0;
  wrapper.frozen = memory.frozen = false;
  prepared_reports = 0;
  observed_stop.reset();
  report_workers.launched = report_workers.joined = 0;
  report_workers.entered = 0;
  report_workers.stop_requested = report_workers.stop_accepted = false;
  counting = false;
  fail_next = false;
  if (mode == Mode::kNormal || mode == Mode::kPositive ||
      mode == Mode::kPositiveRefused || mode == Mode::kReportCanceled)
    Install();
}
void Prepared(std::span<const std::byte>, void*) {
  ++prepared_reports;
  wrapper.frozen = true;
  memory.frozen = true;
}
nb::dict Finish(nb::handle) {
  Restore();
  Controls();
  nb::dict result;
  result["cpp_requests"] = cpp_delta;
  nb::tuple python =
      nb::make_tuple(python_delta[0], python_delta[1], python_delta[2]);
  result["python_requests"] = python;
  result["prepared_reports"] = prepared_reports;
  result["wrapper_after_freeze"] = wrapper.after_freeze.load();
  result["memory_after_freeze"] = memory.after_freeze.load();
  result["wrapper_requests"] = wrapper.calls.load();
  result["memory_requests"] = memory.calls.load();
  result["return_armed"] = return_armed;
  result["pre_probe_cpp_requests"] = pre_probe_cpp;
  result["pre_probe_python_requests"] = pre_probe_python;
  result["refusal_attempts"] = refusal_attempts;
  result["probe"] = static_cast<int>(probe);
  result["restored"] = !installed;
  result["report_workers_launched"] = report_workers.launched;
  result["report_workers_joined"] = report_workers.joined;
  result["report_workers_entered"] = report_workers.entered.load();
  result["report_stop_requested"] = report_workers.stop_requested.load();
  result["report_stop_accepted"] = report_workers.stop_accepted.load();
  return result;
}
nb::object RefuseProperty(nb::handle value, nb::handle name) {
  if (!PyUnicode_Check(name.ptr()))
    throw nb::type_error("property name must be Unicode");
  Install();
  mode = Mode::kIdle;
  cpp_before = allocation::Calls();
  counting = true;
  fail_next = true;
  // Name and call arguments are already owned before arming. This exercises
  // real lazy property delivery without inserting a production getter hook.
  nb::object result = nb::steal(PyObject_GetAttr(value.ptr(), name.ptr()));
  Restore();
  if (!result.is_valid()) throw nb::python_error();
  return result;
}
}  // namespace
void Restore() noexcept {
  allocation::Refuse(false);
  counting = false;
  fail_next = false;
  if (!installed) {
    observed_stop.reset();
    return;
  }
  if (!PyGILState_Check()) std::terminate();
  cpp_delta = allocation::Calls() - cpp_before;
  for (std::size_t i = 0; i < domains.size(); ++i)
    python_delta[i] = domains[i].requests.load(std::memory_order_relaxed);
  refusal_attempts = failures.load(std::memory_order_relaxed);
  for (std::size_t i = domains.size(); i > 0; --i)
    PyMem_SetAllocator(kDomains[i - 1], &domains[i - 1].original);
  installed = false;
  observed_stop.reset();
}
AllocationScope::~AllocationScope() noexcept {
  if (active_) Restore();
}
AllocationScope BeforeInstanceAllocation() {
  const bool active = mode == Mode::kInstance;
  if (active) {
    Install();
    mode = Mode::kIdle;
    cpp_before = allocation::Calls();
    counting = true;
    fail_next = true;
  }
  return AllocationScope(active);
}
AllocationScope BeforeReportBytes() {
  const bool active = mode == Mode::kReport;
  if (active) {
    Install();
    mode = Mode::kIdle;
    cpp_before = allocation::Calls();
    counting = true;
    fail_next = true;
  }
  return AllocationScope(active);
}
gltf_file_workflow_detail::FileWorkflowTestOptions WorkflowOptions() {
  gltf_file_workflow_detail::FileWorkflowTestOptions options;
  if (mode == Mode::kNormal || mode == Mode::kPositive ||
      mode == Mode::kPositiveRefused || mode == Mode::kReportCanceled) {
    options.allocation_backend = wrapper.Backend();
    options.memory.allocation_backend = memory.Backend();
    options.prepared_report = Prepared;
    if (mode == Mode::kReportCanceled)
      options.memory.report_worker_backend = report_workers.Backend();
  }
  return options;
}
void ObserveStopSource(const std::stop_source& source) noexcept {
  if (mode == Mode::kReportCanceled) observed_stop = source;
}
void AfterNativeReturn() noexcept {
  if (!installed) return;
  for (auto& domain : domains) domain.requests = 0;
  cpp_before = allocation::Calls();
  return_armed = true;
  counting = true;
  allocation::Refuse(mode == Mode::kNormal || mode == Mode::kReportCanceled);
}
void BeforeReturn(nb::handle prepared) noexcept {
  if (mode != Mode::kPositive && mode != Mode::kPositiveRefused) return;
  pre_probe_cpp = allocation::Calls() - cpp_before;
  pre_probe_python = PythonRequests();
  allocation::Refuse(
      false);  // Explicit positive phase counts, never claims zero.
  if (mode == Mode::kPositiveRefused) fail_next = true;
  // This catch encloses only the deliberately allocating test probe. The real
  // native result has been assigned and its normal exception path is unchanged.
  try {
    nb::object temporary = nb::inst_alloc(
        nb::handle(reinterpret_cast<PyObject*>(Py_TYPE(prepared.ptr()))));
    Restore();
    probe = ProbeOutcome::kSucceeded;
  } catch (nb::python_error&) {
    Restore();
    probe = ProbeOutcome::kPythonFailure;
  } catch (const std::bad_alloc&) {
    Restore();
    probe = ProbeOutcome::kCppFailure;
  } catch (...) {
    Restore();
    probe = ProbeOutcome::kUnexpectedFailure;
  }
}
void RegisterObserverBindings(nb::module_& module) {
  module.def("_observer_setup", &Setup);
  module.def("_observer_finish", &Finish);
  module.def("_observer_restore", &Restore);
  module.def("_observer_refuse_property", &RefuseProperty);
  module.def("_observer_live_blocks",
             [] { return wrapper.live.load() + memory.live.load(); });
  module.attr("_probe_not_attempted") =
      static_cast<int>(ProbeOutcome::kNotAttempted);
  module.attr("_probe_succeeded") = static_cast<int>(ProbeOutcome::kSucceeded);
  module.attr("_probe_python_failure") =
      static_cast<int>(ProbeOutcome::kPythonFailure);
}
}  // namespace meshvale::repair::python::binding_test
