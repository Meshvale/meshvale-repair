// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_OFF_BINDINGS_H_
#define MESHVALE_REPAIR_PYTHON_OFF_BINDINGS_H_

#include <nanobind/nanobind.h>

namespace meshvale::repair::python {
void RegisterOffBindings(nanobind::module_& module);
}  // namespace meshvale::repair::python
#endif  // MESHVALE_REPAIR_PYTHON_OFF_BINDINGS_H_
