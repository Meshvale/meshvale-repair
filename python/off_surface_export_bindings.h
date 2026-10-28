// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_PYTHON_OFF_SURFACE_EXPORT_BINDINGS_H_
#define MESHVALE_REPAIR_PYTHON_OFF_SURFACE_EXPORT_BINDINGS_H_
#include <nanobind/nanobind.h>
namespace meshvale::repair::python {
void RegisterOffSurfaceExportBindings(nanobind::module_& module);
}
#endif  // MESHVALE_REPAIR_PYTHON_OFF_SURFACE_EXPORT_BINDINGS_H_
