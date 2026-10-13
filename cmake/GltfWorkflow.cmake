# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_REPAIR_GLTF_WORKFLOW
  "Build the native reported supplied-memory glTF repair workflow" OFF)
if(NOT MESHVALE_REPAIR_GLTF_WORKFLOW)
  return()
endif()
if(NOT TARGET meshvale::materialized_duplicates)
  message(FATAL_ERROR "Reported glTF workflow requires MESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON")
endif()
find_package(MeshvaleInterchange 0.0.0 EXACT CONFIG REQUIRED)
if(NOT TARGET meshvale::gltf_workflow_support)
  message(FATAL_ERROR "Reported glTF workflow requires the installed Interchange gltf_workflow_support target")
endif()

add_library(meshvale_repair_gltf_workflow STATIC
  src/gltf_workflow.cpp src/gltf_workflow_report.cpp)
add_library(meshvale::gltf_workflow ALIAS meshvale_repair_gltf_workflow)
set_target_properties(meshvale_repair_gltf_workflow PROPERTIES
  EXPORT_NAME gltf_workflow POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_repair_gltf_workflow PUBLIC cxx_std_20)
target_compile_definitions(meshvale_repair_gltf_workflow PRIVATE
  MESHVALE_REPAIR_VERSION="${MESHVALE_REPAIR_VERSION}")
target_link_libraries(meshvale_repair_gltf_workflow PUBLIC
  meshvale::materialized_duplicates meshvale::gltf_workflow_support)
target_include_directories(meshvale_repair_gltf_workflow PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
meshvale_warnings(meshvale_repair_gltf_workflow)
meshvale_strict_floating_point(meshvale_repair_gltf_workflow)
install(TARGETS meshvale_repair_gltf_workflow EXPORT MeshvaleRepairTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/repair/gltf_workflow.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/repair")

function(meshvale_add_gltf_workflow_tests)
  if(NOT MESHVALE_REPAIR_GLTF_WORKFLOW)
    return()
  endif()
  target_sources(meshvale_repair_header_checks PRIVATE tests/header_gltf_workflow.cpp)
  target_link_libraries(meshvale_repair_header_checks PRIVATE meshvale::gltf_workflow)
  add_executable(meshvale_gltf_workflow_tests tests/gltf_workflow.cpp)
  target_link_libraries(meshvale_gltf_workflow_tests PRIVATE meshvale::gltf_workflow)
  meshvale_warnings(meshvale_gltf_workflow_tests)
  meshvale_strict_floating_point(meshvale_gltf_workflow_tests)
  if(MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE STREQUAL "qualified")
    add_test(NAME gltf_workflow COMMAND meshvale_gltf_workflow_tests)
    add_executable(meshvale_gltf_workflow_request_tests tests/gltf_workflow_requests.cpp)
    target_include_directories(meshvale_gltf_workflow_request_tests PRIVATE src)
    target_link_libraries(meshvale_gltf_workflow_request_tests PRIVATE meshvale::gltf_workflow)
    meshvale_warnings(meshvale_gltf_workflow_request_tests)
    meshvale_strict_floating_point(meshvale_gltf_workflow_request_tests)
    add_test(NAME gltf_workflow_requests COMMAND meshvale_gltf_workflow_request_tests)
  else()
    add_test(NAME gltf_workflow_unsupported
      COMMAND meshvale_gltf_workflow_tests --unsupported-profile)
  endif()
endfunction()
