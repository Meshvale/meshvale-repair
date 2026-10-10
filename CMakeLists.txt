# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(MeshvaleRepair LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MeshvaleGeometry 0.0.0 EXACT CONFIG REQUIRED)
find_package(Git QUIET)
set(MESHVALE_REPAIR_VERSION "0.0.0")
if(Git_FOUND)
  execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match --dirty
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE meshvale_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(meshvale_tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)$")
    set(MESHVALE_REPAIR_VERSION "${CMAKE_MATCH_1}")
  endif()
endif()

add_library(meshvale_repair STATIC src/duplicates.cpp)
add_library(meshvale::repair ALIAS meshvale_repair)
set_target_properties(meshvale_repair PROPERTIES EXPORT_NAME repair POSITION_INDEPENDENT_CODE ON)
target_compile_features(meshvale_repair PUBLIC cxx_std_20)
target_link_libraries(meshvale_repair PUBLIC meshvale::geometry)
target_include_directories(meshvale_repair PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
function(meshvale_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()
meshvale_warnings(meshvale_repair)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS meshvale_repair EXPORT MeshvaleRepairTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT MeshvaleRepairTargets NAMESPACE meshvale::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleRepair")
configure_package_config_file(cmake/MeshvaleRepairConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleRepairConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleRepair")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleRepairConfigVersion.cmake"
  VERSION "${MESHVALE_REPAIR_VERSION}" COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleRepairConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/MeshvaleRepairConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/MeshvaleRepair")
install(FILES LICENSE NOTICE DESTINATION "${CMAKE_INSTALL_DATADIR}/MeshvaleRepair")

option(MESHVALE_REPAIR_PYTHON "Build the optional Python duplicate interface" OFF)
if(MESHVALE_REPAIR_PYTHON)
  find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
  execute_process(COMMAND "${Python_EXECUTABLE}" -c
    "import sysconfig; print(int(bool(sysconfig.get_config_var('Py_GIL_DISABLED'))))"
    OUTPUT_VARIABLE meshvale_python_free_threaded OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  if(meshvale_python_free_threaded STREQUAL "1")
    message(FATAL_ERROR "Free-threaded Python is not supported by this binding")
  endif()
  execute_process(COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
    OUTPUT_VARIABLE meshvale_nanobind_dir OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
  find_package(nanobind 3.1 CONFIG REQUIRED HINTS "${meshvale_nanobind_dir}")
  nanobind_add_module(_repair NB_STATIC NB_DOMAIN meshvale_repair python/bindings.cpp)
  meshvale_geometry_add_python_record(meshvale_repair_record DOMAIN meshvale_repair)
  target_link_libraries(_repair PRIVATE meshvale_repair_record meshvale::repair)
  meshvale_warnings(_repair)
  install(TARGETS _repair LIBRARY DESTINATION meshvale_repair COMPONENT python)
endif()

include(CTest)
if(BUILD_TESTING)
  add_library(meshvale_repair_header_checks OBJECT
    tests/header_canonical.cpp)
  target_link_libraries(meshvale_repair_header_checks PRIVATE meshvale::repair)
  meshvale_warnings(meshvale_repair_header_checks)
  add_executable(meshvale_duplicate_tests tests/duplicates.cpp)
  target_link_libraries(meshvale_duplicate_tests PRIVATE meshvale::repair)
  meshvale_warnings(meshvale_duplicate_tests)
  add_test(NAME targeted_duplicates COMMAND meshvale_duplicate_tests)
endif()
