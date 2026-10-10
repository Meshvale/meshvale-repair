// SPDX-License-Identifier: Apache-2.0
#include <Python.h>
#include <nanobind/nanobind.h>

#include <utility>
#include <vector>

#include "meshvale/geometry/python/record.h"
#include "meshvale/repair/duplicates.h"

namespace nb = nanobind;
namespace geo = meshvale::geometry;
namespace repair = meshvale::repair;

namespace {
std::pair<nb::handle, nb::handle> pair(nb::handle value) {
  if (PyTuple_Check(value.ptr()) && PyTuple_GET_SIZE(value.ptr()) == 2)
    return {PyTuple_GET_ITEM(value.ptr(), 0), PyTuple_GET_ITEM(value.ptr(), 1)};
  if (PyList_Check(value.ptr()) && PyList_GET_SIZE(value.ptr()) == 2)
    return {PyList_GET_ITEM(value.ptr(), 0), PyList_GET_ITEM(value.ptr(), 1)};
  throw nb::value_error("request items must be pairs");
}
geo::index_t index(nb::handle value) {
  if (!PyLong_Check(value.ptr()) || PyBool_Check(value.ptr()))
    throw nb::type_error("face indices must be integers, not booleans");
  const auto result = PyLong_AsUnsignedLongLong(value.ptr());
  if (PyErr_Occurred()) throw nb::python_error();
  return static_cast<geo::index_t>(result);
}
nb::dict remove_duplicates(nb::handle record, const nb::list& targets,
                           const nb::list& row_local) {
  const auto source = geo::python::from_record(record);
  std::vector<repair::DuplicateTarget> request;
  for (auto item : targets) {
    const auto [keep, discard] = pair(item);
    request.push_back({index(keep), index(discard)});
  }
  repair::DuplicateOptions options;
  for (auto item : row_local) {
    const auto [domain, name] = pair(item);
    const auto text = geo::python::text(domain);
    geo::AttributeDomain kind;
    if (text == "vertex")
      kind = geo::AttributeDomain::vertex;
    else if (text == "face")
      kind = geo::AttributeDomain::face;
    else if (text == "corner")
      kind = geo::AttributeDomain::corner;
    else
      throw nb::value_error("row-local domain must be vertex, face or corner");
    options.row_local_attributes.push_back({kind, geo::python::text(name)});
  }
  repair::DuplicateResult result;
  {
    nb::gil_scoped_release release;
    result = repair::remove_duplicate_faces(source, request, options);
  }
  nb::dict output;
  output["outcome"] =
      result.outcome == repair::CandidateOutcome::accepted    ? "accepted"
      : result.outcome == repair::CandidateOutcome::unchanged ? "unchanged"
                                                              : "rejected";
  output["candidate_record"] = nb::none();
  if (result.candidate)
    output["candidate_record"] = geo::python::to_record(*result.candidate);
  output["face_map"] = geo::python::write_buffer(result.face_map, "Q");
  output["corner_map"] = geo::python::write_buffer(result.corner_map, "Q");
  nb::list diagnostics;
  for (const auto& diagnostic : result.diagnostics) {
    nb::dict item;
    item["code"] = nb::cast(diagnostic.code);
    item["subject"] = nb::cast(diagnostic.subject);
    item["element"] =
        diagnostic.element ? nb::cast(*diagnostic.element) : nb::none();
    diagnostics.append(item);
  }
  output["diagnostics"] = diagnostics;
  return output;
}
}  // namespace

NB_MODULE(_repair, module) {
  module.def("remove_duplicates", &remove_duplicates, nb::arg("record"),
             nb::arg("targets"), nb::arg("row_local"));
}
