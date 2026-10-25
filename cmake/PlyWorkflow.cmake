# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_REPAIR_PLY_FILE_WORKFLOW
  "Build the reported retained typed PLY file repair workflow" OFF)
if(NOT MESHVALE_REPAIR_PLY_FILE_WORKFLOW)
  return()
endif()
if(NOT TARGET meshvale::materialized_duplicates)
  message(FATAL_ERROR "PLY workflow requires MESHVALE_REPAIR_MATERIALIZED_DUPLICATES=ON")
endif()
if(NOT TARGET meshvale::ply_files)
  find_package(MeshvaleInterchange 0.0.0 EXACT CONFIG REQUIRED)
endif()
if(NOT TARGET meshvale::ply_files)
  message(FATAL_ERROR "PLY workflow requires an installed Interchange ply_files target")
endif()

add_library(meshvale_repair_ply_file_workflow STATIC
  src/ply_workflow.cpp src/ply_workflow_report.cpp)
add_library(meshvale::ply_file_workflow ALIAS meshvale_repair_ply_file_workflow)
set_target_properties(meshvale_repair_ply_file_workflow PROPERTIES
  EXPORT_NAME ply_file_workflow POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_repair_ply_file_workflow PUBLIC cxx_std_20)
target_compile_definitions(meshvale_repair_ply_file_workflow PRIVATE
  MESHVALE_REPAIR_VERSION="${MESHVALE_REPAIR_VERSION}")
target_link_libraries(meshvale_repair_ply_file_workflow PUBLIC
  meshvale::materialized_duplicates meshvale::ply_files PRIVATE meshvale::repair)
target_include_directories(meshvale_repair_ply_file_workflow PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include> PRIVATE src)
meshvale_warnings(meshvale_repair_ply_file_workflow)
meshvale_strict_floating_point(meshvale_repair_ply_file_workflow)
install(TARGETS meshvale_repair_ply_file_workflow EXPORT MeshvaleRepairTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/repair/ply_workflow.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/repair")

function(meshvale_add_ply_workflow_tests)
  target_sources(meshvale_repair_header_checks PRIVATE tests/header_ply_workflow.cpp)
  target_link_libraries(meshvale_repair_header_checks PRIVATE meshvale::ply_file_workflow)
  add_executable(meshvale_ply_workflow_tests
    tests/ply_workflow.cpp tests/header_ply_workflow_observations.cpp)
  target_link_libraries(meshvale_ply_workflow_tests PRIVATE meshvale::ply_file_workflow)
  target_include_directories(meshvale_ply_workflow_tests PRIVATE tests)
  meshvale_warnings(meshvale_ply_workflow_tests)
  meshvale_strict_floating_point(meshvale_ply_workflow_tests)
  # Generated outside the formatter's source inventory: retain both inclusions.
  set(observer_probe "${CMAKE_CURRENT_BINARY_DIR}/ply_workflow_observations_twice.cpp")
  file(WRITE "${observer_probe}"
    "#include \"ply_workflow_observations.h\"\n#include \"ply_workflow_observations.h\"\n")
  target_sources(meshvale_ply_workflow_tests PRIVATE "${observer_probe}")
  if(MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE STREQUAL "qualified")
    add_test(NAME ply_workflow_semantics COMMAND meshvale_ply_workflow_tests)
    add_executable(meshvale_ply_workflow_request_tests tests/ply_workflow_requests.cpp)
    target_link_libraries(meshvale_ply_workflow_request_tests PRIVATE meshvale::ply_file_workflow)
    target_include_directories(meshvale_ply_workflow_request_tests PRIVATE src tests)
    meshvale_warnings(meshvale_ply_workflow_request_tests)
    meshvale_strict_floating_point(meshvale_ply_workflow_request_tests)
    add_test(NAME ply_workflow_requests COMMAND meshvale_ply_workflow_request_tests)
  endif()
  foreach(header IN ITEMS ply_workflow_internal ply_workflow_test_internal)
    set(probe "${CMAKE_CURRENT_BINARY_DIR}/${header}_h.cpp")
    file(WRITE "${probe}" "#include \"${header}.h\"\n#include \"${header}.h\"\n")
    target_sources(meshvale_repair_ply_file_workflow PRIVATE "${probe}")
  endforeach()
endfunction()
