# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_REPAIR_GLTF_FILE_WORKFLOW
  "Build the native reported retained glTF file repair workflow" OFF)
if(NOT MESHVALE_REPAIR_GLTF_FILE_WORKFLOW)
  return()
endif()
if(NOT TARGET meshvale::gltf_workflow)
  message(FATAL_ERROR "Reported glTF file workflow requires MESHVALE_REPAIR_GLTF_WORKFLOW=ON")
endif()
if(NOT TARGET meshvale::gltf_files)
  message(FATAL_ERROR "Reported glTF file workflow requires the installed Interchange gltf_files target")
endif()
# Development packages share 0.0.0; require the compiled ledger query and provider.
function(meshvale_require_gltf_file_prerequisites)
  cmake_push_check_state(RESET)
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${CMAKE_BUILD_TYPE}")
  else()
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
  endif()
  unset(MESHVALE_INTERCHANGE_FILE_WORKFLOW_PREREQUISITES CACHE)
  set(CMAKE_REQUIRED_LIBRARIES meshvale::gltf_files)
  check_cxx_source_compiles("#include <meshvale/geometry/execution.h>
  #include <meshvale/interchange/gltf.h>
  #include <meshvale/interchange/gltf_files.h>
  #include <span>
  std::span<const meshvale::interchange::GltfFileView> Provide(
      const meshvale::interchange::GltfFileVerifiedContent&, void*) { return {}; }
  int main() {
    meshvale::geometry::ExecutionContext context;
    meshvale::interchange::GltfFilePublicationOptions options;
    options.verified_files_provider = {&Provide, nullptr};
    auto read = meshvale::interchange::ReadGltfAssetCapped({}, {}, {}, {}, context);
    return read.asset && !read.asset->SharesExecutionBudget(context) ? 1 : 0;
  }" MESHVALE_INTERCHANGE_FILE_WORKFLOW_PREREQUISITES)
  cmake_pop_check_state()
  if(NOT MESHVALE_INTERCHANGE_FILE_WORKFLOW_PREREQUISITES)
    message(FATAL_ERROR "Reported glTF file workflow requires the compiled Interchange asset ledger query and borrowed verified-files provider; use the documented producer revision")
  endif()
endfunction()
meshvale_require_gltf_file_prerequisites()

add_library(meshvale_repair_gltf_file_workflow STATIC
  src/gltf_file_workflow.cpp src/gltf_file_workflow_report.cpp)
add_library(meshvale::gltf_file_workflow ALIAS meshvale_repair_gltf_file_workflow)
set_target_properties(meshvale_repair_gltf_file_workflow PROPERTIES
  EXPORT_NAME gltf_file_workflow POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_repair_gltf_file_workflow PUBLIC cxx_std_20)
target_compile_definitions(meshvale_repair_gltf_file_workflow PRIVATE
  MESHVALE_REPAIR_VERSION="${MESHVALE_REPAIR_VERSION}")
target_link_libraries(meshvale_repair_gltf_file_workflow PUBLIC
  meshvale::gltf_workflow meshvale::gltf_files PRIVATE meshvale::repair)
target_include_directories(meshvale_repair_gltf_file_workflow PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include> PRIVATE src)
meshvale_warnings(meshvale_repair_gltf_file_workflow)
meshvale_strict_floating_point(meshvale_repair_gltf_file_workflow)
install(TARGETS meshvale_repair_gltf_file_workflow EXPORT MeshvaleRepairTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/repair/gltf_file_workflow.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/repair")

function(meshvale_add_gltf_file_workflow_tests)
  target_sources(meshvale_repair_header_checks PRIVATE tests/header_gltf_file_workflow.cpp)
  target_link_libraries(meshvale_repair_header_checks PRIVATE meshvale::gltf_file_workflow)
  add_executable(meshvale_gltf_file_sha256_tests
    tests/gltf_file_sha256.cpp tests/gltf_file_sha256_allocation.cpp)
  target_link_libraries(meshvale_gltf_file_sha256_tests PRIVATE meshvale::gltf_file_workflow)
  meshvale_warnings(meshvale_gltf_file_sha256_tests)
  meshvale_strict_floating_point(meshvale_gltf_file_sha256_tests)
  add_test(NAME gltf_file_sha256 COMMAND meshvale_gltf_file_sha256_tests)
  add_executable(meshvale_gltf_file_workflow_tests tests/gltf_file_workflow.cpp)
  target_link_libraries(meshvale_gltf_file_workflow_tests PRIVATE meshvale::gltf_file_workflow)
  meshvale_warnings(meshvale_gltf_file_workflow_tests)
  meshvale_strict_floating_point(meshvale_gltf_file_workflow_tests)
  if(MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE STREQUAL "qualified")
    add_test(NAME gltf_file_workflow_semantics COMMAND meshvale_gltf_file_workflow_tests)
    add_executable(meshvale_gltf_file_workflow_request_tests tests/gltf_file_workflow_requests.cpp)
    target_include_directories(meshvale_gltf_file_workflow_request_tests PRIVATE src)
    target_link_libraries(meshvale_gltf_file_workflow_request_tests PRIVATE meshvale::gltf_file_workflow)
    meshvale_warnings(meshvale_gltf_file_workflow_request_tests)
    meshvale_strict_floating_point(meshvale_gltf_file_workflow_request_tests)
    add_test(NAME gltf_file_workflow_requests COMMAND meshvale_gltf_file_workflow_request_tests)
    add_executable(meshvale_gltf_retained_facts_tests tests/gltf_retained_facts.cpp)
    target_include_directories(meshvale_gltf_retained_facts_tests PRIVATE src)
    target_link_libraries(meshvale_gltf_retained_facts_tests PRIVATE meshvale::gltf_file_workflow)
    meshvale_warnings(meshvale_gltf_retained_facts_tests)
    meshvale_strict_floating_point(meshvale_gltf_retained_facts_tests)
    add_test(NAME gltf_retained_facts COMMAND meshvale_gltf_retained_facts_tests)
    # Bound complete original wrapper/report/hash allocation sweeps separately.
    set(_MESHVALE_REPAIR_GLTF_FILE_REQUEST_TIMEOUT_SECONDS 600)
    set_tests_properties(gltf_file_workflow_requests PROPERTIES
      TIMEOUT "${_MESHVALE_REPAIR_GLTF_FILE_REQUEST_TIMEOUT_SECONDS}" RUN_SERIAL TRUE)
  else()
    add_test(NAME gltf_file_workflow_unsupported
      COMMAND meshvale_gltf_file_workflow_tests --unsupported-profile)
  endif()
  foreach(meshvale_file_header IN ITEMS gltf_file_workflow_internal gltf_file_sha256_internal
                                       gltf_workflow_report_internal gltf_file_workflow_test_internal)
    set(meshvale_file_probe "${CMAKE_CURRENT_BINARY_DIR}/${meshvale_file_header}_h.cpp")
    file(WRITE "${meshvale_file_probe}"
      "#include \"${meshvale_file_header}.h\"\n#include \"${meshvale_file_header}.h\"\n")
    target_sources(meshvale_repair_gltf_file_workflow PRIVATE "${meshvale_file_probe}")
  endforeach()
endfunction()
