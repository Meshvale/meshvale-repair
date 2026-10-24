# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_REPAIR_OFF_FILE_WORKFLOW
  "Build the reported retained OFF file repair workflow" OFF)
if(NOT MESHVALE_REPAIR_OFF_FILE_WORKFLOW)
  return()
endif()
if(NOT TARGET meshvale::materialized_duplicates)
  message(FATAL_ERROR "OFF workflow requires MESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON")
endif()
if(NOT TARGET meshvale::off_files)
  find_package(MeshvaleInterchange 0.0.0 EXACT CONFIG REQUIRED)
endif()
if(NOT TARGET meshvale::off_files)
  message(FATAL_ERROR "OFF workflow requires an installed Interchange off_files target")
endif()

add_library(meshvale_repair_off_file_workflow STATIC
  src/off_workflow.cpp src/off_workflow_report.cpp)
add_library(meshvale::off_file_workflow ALIAS meshvale_repair_off_file_workflow)
set_target_properties(meshvale_repair_off_file_workflow PROPERTIES
  EXPORT_NAME off_file_workflow POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_repair_off_file_workflow PUBLIC cxx_std_20)
target_compile_definitions(meshvale_repair_off_file_workflow PRIVATE
  MESHVALE_REPAIR_VERSION="${MESHVALE_REPAIR_VERSION}")
target_link_libraries(meshvale_repair_off_file_workflow PUBLIC
  meshvale::materialized_duplicates meshvale::off_files PRIVATE meshvale::repair)
target_include_directories(meshvale_repair_off_file_workflow PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include> PRIVATE src)
meshvale_warnings(meshvale_repair_off_file_workflow)
meshvale_strict_floating_point(meshvale_repair_off_file_workflow)
install(TARGETS meshvale_repair_off_file_workflow EXPORT MeshvaleRepairTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/repair/off_workflow.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/repair")

function(meshvale_add_off_workflow_tests)
  target_sources(meshvale_repair_header_checks PRIVATE tests/header_off_workflow.cpp)
  target_link_libraries(meshvale_repair_header_checks PRIVATE meshvale::off_file_workflow)
  add_executable(meshvale_off_workflow_tests tests/off_workflow.cpp)
  add_executable(meshvale_off_workflow_request_tests tests/off_workflow_requests.cpp)
  foreach(fixture IN ITEMS meshvale_off_workflow_tests meshvale_off_workflow_request_tests)
    target_include_directories(${fixture} PRIVATE src)
    target_link_libraries(${fixture} PRIVATE meshvale::off_file_workflow)
    meshvale_warnings(${fixture})
    meshvale_strict_floating_point(${fixture})
  endforeach()
  if(NOT MESHVALE_REPAIR_GLTF_FILE_WORKFLOW)
    add_executable(meshvale_off_file_sha256_tests
      tests/gltf_file_sha256.cpp tests/gltf_file_sha256_allocation.cpp)
    target_link_libraries(meshvale_off_file_sha256_tests PRIVATE meshvale::repair)
    meshvale_warnings(meshvale_off_file_sha256_tests)
    meshvale_strict_floating_point(meshvale_off_file_sha256_tests)
    add_test(NAME off_file_sha256 COMMAND meshvale_off_file_sha256_tests)
  endif()
  if(MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE STREQUAL "qualified")
    add_test(NAME off_workflow_semantics COMMAND meshvale_off_workflow_tests)
    add_test(NAME off_workflow_requests COMMAND meshvale_off_workflow_request_tests)
  else()
    add_test(NAME off_workflow_semantics
      COMMAND meshvale_off_workflow_tests --unsupported-profile)
    add_test(NAME off_workflow_requests
      COMMAND meshvale_off_workflow_request_tests --unsupported-profile)
  endif()
  set(_MESHVALE_REPAIR_OFF_REQUEST_TIMEOUT_SECONDS 600)
  set_tests_properties(off_workflow_requests PROPERTIES
    TIMEOUT "${_MESHVALE_REPAIR_OFF_REQUEST_TIMEOUT_SECONDS}" RUN_SERIAL TRUE)
  foreach(header IN ITEMS off_workflow_internal off_workflow_test_internal)
    set(probe "${CMAKE_CURRENT_BINARY_DIR}/${header}_h.cpp")
    file(WRITE "${probe}" "#include \"${header}.h\"\n#include \"${header}.h\"\n")
    target_sources(meshvale_repair_off_file_workflow PRIVATE "${probe}")
  endforeach()
endfunction()
