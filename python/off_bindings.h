// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_OFF_BINDINGS_H_
#define MESHVALE_REPAIR_PYTHON_OFF_BINDINGS_H_

#include <nanobind/nanobind.h>

#include <stop_token>

namespace meshvale::repair::python {
// Shares the existing OffCancellation state; None creates a fresh state.
[[nodiscard]] std::stop_source ShareOffCancellation(nanobind::handle value);
void RegisterOffBindings(nanobind::module_& module);
}  // namespace meshvale::repair::python
#endif  // MESHVALE_REPAIR_PYTHON_OFF_BINDINGS_H_
