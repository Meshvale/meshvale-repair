# SPDX-License-Identifier: Apache-2.0
option(MESHVALE_REPAIR_OFF_SURFACE_EXPORT
  "Build the retained OFF repair to annotated GLB workflow" OFF)
set(MESHVALE_REPAIR_INTERCHANGE_TEST_SOURCE "" CACHE PATH
  "Matching Interchange source for non-installed composed publication fault tests")
if(NOT MESHVALE_REPAIR_OFF_SURFACE_EXPORT)
  return()
endif()
if(NOT TARGET meshvale::off_file_workflow)
  message(FATAL_ERROR "OFF surface export requires MESHVALE_REPAIR_OFF_FILE_WORKFLOW=ON")
endif()
if(NOT TARGET meshvale::charged_triangulation)
  message(FATAL_ERROR "OFF surface export requires an installed Geometry charged_triangulation target")
endif()
foreach(dependency IN ITEMS surface_output surface_files)
  if(NOT TARGET meshvale::${dependency})
    message(FATAL_ERROR "OFF surface export requires an installed Interchange ${dependency} target")
  endif()
endforeach()

add_library(meshvale_repair_off_surface_export STATIC src/off_surface_export.cpp)
add_library(meshvale::repair_off_surface_export ALIAS meshvale_repair_off_surface_export)
set_target_properties(meshvale_repair_off_surface_export PROPERTIES
  EXPORT_NAME repair_off_surface_export POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_repair_off_surface_export PUBLIC cxx_std_20)
target_link_libraries(meshvale_repair_off_surface_export PUBLIC
  meshvale::off_file_workflow meshvale::charged_triangulation
  meshvale::surface_output meshvale::surface_files)
target_include_directories(meshvale_repair_off_surface_export PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include> PRIVATE src)
meshvale_warnings(meshvale_repair_off_surface_export)
meshvale_strict_floating_point(meshvale_repair_off_surface_export)
install(TARGETS meshvale_repair_off_surface_export EXPORT MeshvaleRepairTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/meshvale/repair/off_surface_export.h
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/meshvale/repair")

function(meshvale_add_off_surface_export_tests)
  target_sources(meshvale_repair_header_checks PRIVATE
    tests/headers/off_surface_export_h.cpp)
  target_link_libraries(meshvale_repair_header_checks PRIVATE
    meshvale::repair_off_surface_export)
  foreach(kind IN ITEMS semantics requests consumer)
    set(fixture meshvale_off_surface_export_${kind}_tests)
    add_executable(${fixture} tests/off_surface_export_${kind}.cpp)
    target_link_libraries(${fixture} PRIVATE meshvale::repair_off_surface_export)
    target_include_directories(${fixture} PRIVATE tests)
    if(NOT kind STREQUAL "consumer")
      target_include_directories(${fixture} PRIVATE src)
    endif()
    meshvale_warnings(${fixture})
    meshvale_strict_floating_point(${fixture})
    if(MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE STREQUAL "qualified")
      add_test(NAME off_surface_export_${kind} COMMAND ${fixture})
    else()
      add_test(NAME off_surface_export_${kind}
        COMMAND ${fixture} --unsupported-profile)
    endif()
  endforeach()
  set(_MESHVALE_REPAIR_OFF_SURFACE_REQUEST_TIMEOUT_SECONDS 600)
  set_tests_properties(off_surface_export_requests PROPERTIES
    TIMEOUT "${_MESHVALE_REPAIR_OFF_SURFACE_REQUEST_TIMEOUT_SECONDS}" RUN_SERIAL TRUE)
  if(MESHVALE_REPAIR_INTERCHANGE_TEST_SOURCE)
    if(NOT MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE STREQUAL "qualified")
      message(FATAL_ERROR "Composed publication faults require the qualified test profile")
    endif()
    foreach(required IN ITEMS src/surface_files_test_internal.h
        src/surface_file_publish.cpp include/meshvale/interchange/surface_files.h)
      if(NOT EXISTS "${MESHVALE_REPAIR_INTERCHANGE_TEST_SOURCE}/${required}")
        message(FATAL_ERROR "Composed publication faults need matching Interchange source: ${required}")
      endif()
    endforeach()
    # This source-coupled target uses genuine producer hooks. Private producer
    # headers never enter an installed consumer or the exported Repair SDK.
    add_executable(meshvale_off_surface_export_publication_faults_tests
      tests/off_surface_export_publication_faults.cpp)
    target_link_libraries(meshvale_off_surface_export_publication_faults_tests
      PRIVATE meshvale::repair_off_surface_export)
    target_include_directories(meshvale_off_surface_export_publication_faults_tests
      PRIVATE src tests "${MESHVALE_REPAIR_INTERCHANGE_TEST_SOURCE}/src")
    meshvale_warnings(meshvale_off_surface_export_publication_faults_tests)
    meshvale_strict_floating_point(meshvale_off_surface_export_publication_faults_tests)
    add_test(NAME off_surface_export_publication_faults
      COMMAND meshvale_off_surface_export_publication_faults_tests)
    set_tests_properties(off_surface_export_publication_faults PROPERTIES
      TIMEOUT "${_MESHVALE_REPAIR_OFF_SURFACE_REQUEST_TIMEOUT_SECONDS}" RUN_SERIAL TRUE)
  endif()
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/off_surface_export_internal.h")
    set(probe "${CMAKE_CURRENT_BINARY_DIR}/off_surface_export_internal_twice.cpp")
    file(WRITE "${probe}"
      "#include \"off_surface_export_internal.h\"\n#include \"off_surface_export_internal.h\"\n")
    target_sources(meshvale_repair_off_surface_export PRIVATE "${probe}")
  endif()
endfunction()
