// SPDX-License-Identifier: Apache-2.0
#include <nanobind/nanobind.h>

#include "gltf_binding_observer.h"
#include "gltf_bindings.h"
NB_MODULE(_repair_gltf_return_test, module) {
  meshvale::repair::python::RegisterGltfBindings(module);
  meshvale::repair::python::binding_test::RegisterObserverBindings(module);
}
