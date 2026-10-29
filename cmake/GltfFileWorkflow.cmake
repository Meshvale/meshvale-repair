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
# Require the selected result tails as well as the compiled ledger/provider.
# The memory-workflow probe independently checks the compiled translation getters.
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
    meshvale::interchange::GltfFilePublicationResult published;
    published.translation_extraction.reset();
    published.translation_inspection.reset();
    auto read = meshvale::interchange::ReadGltfAssetCapped({}, {}, {}, {}, context);
    return read.asset && !read.asset->SharesExecutionBudget(context) ? 1 : 0;
  }" MESHVALE_INTERCHANGE_FILE_WORKFLOW_PREREQUISITES)
  cmake_pop_check_state()
  if(NOT MESHVALE_INTERCHANGE_FILE_WORKFLOW_PREREQUISITES)
    message(FATAL_ERROR "Reported glTF file workflow requires the Interchange translation publication result, compiled asset ledger query and borrowed verified-files provider; use the documented matching producer revision")
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

function(meshvale_select_deformation_positive_tests target applicability_target)
  set(_meshvale_probed_configuration "")
  set(_meshvale_eligible_configurations "")
  # Generator identity is read-only CMake state, never inferred from a
  # caller/stale CMAKE_CONFIGURATION_TYPES cache field.
  get_property(_meshvale_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(_meshvale_multi_config)
    if(NOT DEFINED CMAKE_CONFIGURATION_TYPES OR CMAKE_CONFIGURATION_TYPES STREQUAL "")
      message(FATAL_ERROR "Deformation format test selection requires a genuine multi-config configuration list")
    endif()
    if("Release" IN_LIST CMAKE_CONFIGURATION_TYPES)
      set(_meshvale_probed_configuration Release)
    endif()
  elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(_meshvale_probed_configuration Release)
  endif()

  if(_meshvale_probed_configuration)
    cmake_push_check_state(RESET)
    set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    # Match the effective standard: the public dependency requires at least20.
    # A caller-selected newer standard remains newer and fails the exact probe.
    get_target_property(_meshvale_standard ${target} CXX_STANDARD)
    if(NOT _meshvale_standard OR _meshvale_standard LESS 20)
      set(_meshvale_standard 20)
    endif()
    set(CMAKE_CXX_STANDARD ${_meshvale_standard})
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    get_target_property(_meshvale_runtime ${target} MSVC_RUNTIME_LIBRARY)
    if(_meshvale_runtime)
      set(CMAKE_MSVC_RUNTIME_LIBRARY "${_meshvale_runtime}")
    endif()
    # Global/toolchain and Release flags are inherited, not replaced. Append the
    # same target warning/FP options; never define capability macros/NDEBUG/CRT.
    if(MSVC)
      set(CMAKE_REQUIRED_FLAGS "/W4 /WX /fp:strict")
    else()
      set(CMAKE_REQUIRED_FLAGS "-Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off")
    endif()
    unset(_MESHVALE_REPAIR_DEFORMATION_FORMAT_NUMERIC_COMPILE CACHE)
    unset(_MESHVALE_REPAIR_DEFORMATION_FORMAT_NUMERIC_COMPILE)
    check_cxx_source_compiles([=[
// Configure-only source, passed to check_cxx_source_compiles as a static library.
#include <array>
#include <cstdint>
#include <limits>

#if defined(_MSC_VER) && defined(_M_X64)
#include <crtversion.h>
#endif

#if defined(_MSC_FULL_VER) && _MSC_FULL_VER == 195136248 && defined(_M_X64) && \\
    _M_X64 == 100 && defined(_M_FP_STRICT) && _M_FP_STRICT == 1 &&             \\
    defined(_MSVC_LANG) && _MSVC_LANG == 202002L &&                            \\
    defined(_MSVC_STL_VERSION) && _MSVC_STL_VERSION == 145 &&                  \\
    defined(_MSVC_STL_UPDATE) && _MSVC_STL_UPDATE == 202604L &&                \\
    defined(_VC_CRT_MAJOR_VERSION) && _VC_CRT_MAJOR_VERSION == 14 &&           \\
    defined(_VC_CRT_MINOR_VERSION) && _VC_CRT_MINOR_VERSION == 51 &&           \\
    defined(_VC_CRT_BUILD_VERSION) && _VC_CRT_BUILD_VERSION == 36244 &&        \\
    defined(_VC_CRT_RBUILD_VERSION) && _VC_CRT_RBUILD_VERSION == 0 &&          \\
    defined(_DLL) && _DLL == 1 && defined(_MT) && _MT == 1 &&                  \\
    defined(NDEBUG) && !defined(_DEBUG) && defined(_ITERATOR_DEBUG_LEVEL) &&   \\
    _ITERATOR_DEBUG_LEVEL == 0
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::digits == 24);
static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<double>::is_iec559);
static_assert(std::numeric_limits<double>::digits == 53);
static_assert(sizeof(double) == sizeof(std::uint64_t));
#else
#error Unsupported deformation-format numerical compile profile
#endif

int main() { return 0; }
    ]=] _MESHVALE_REPAIR_DEFORMATION_FORMAT_NUMERIC_COMPILE)
    if(_MESHVALE_REPAIR_DEFORMATION_FORMAT_NUMERIC_COMPILE)
      set(_meshvale_eligible_configurations Release)
    endif()
    cmake_pop_check_state()
  endif()
  # Derived build evidence, not user options. Reconfiguration overwrites old or
  # command-line values. Internal compile result is also freshly recomputed.
  set(MESHVALE_REPAIR_DEFORMATION_FORMAT_PROBED_CONFIGURATION
    "${_meshvale_probed_configuration}" CACHE INTERNAL "Test-only numerical profile probe configuration" FORCE)
  set(MESHVALE_REPAIR_DEFORMATION_FORMAT_ELIGIBLE_CONFIGURATIONS
    "${_meshvale_eligible_configurations}" CACHE INTERNAL "Test-only derived positive numerical configurations" FORCE)
  if(_meshvale_eligible_configurations)
    if(_meshvale_multi_config)
      add_test(NAME gltf_deformation_policy_applicability
        COMMAND ${applicability_target}
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gltf-deformation-end-to-end/d04-dense-float-0-2-5-stride.gltf"
        CONFIGURATIONS ${_meshvale_eligible_configurations})
      add_test(NAME gltf_deformation_format_preservation
        COMMAND ${target}
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gltf-deformation-format"
        CONFIGURATIONS ${_meshvale_eligible_configurations})
    else()
      # Both targets use the same public dependencies and strict target policy.
      # These single-config binaries are already bound to actual Release.
      add_test(NAME gltf_deformation_policy_applicability
        COMMAND ${applicability_target}
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gltf-deformation-end-to-end/d04-dense-float-0-2-5-stride.gltf")
      add_test(NAME gltf_deformation_format_preservation
        COMMAND ${target}
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/gltf-deformation-format")
    endif()
  endif()
endfunction()

function(meshvale_add_gltf_file_workflow_tests)
  target_sources(meshvale_repair_header_checks PRIVATE
    tests/header_gltf_file_workflow.cpp
    tests/header_gltf_file_workflow_standalone.cpp)
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
    add_executable(meshvale_gltf_deformation_policy_applicability_tests
      tests/gltf_deformation_policy_applicability.cpp)
    target_link_libraries(meshvale_gltf_deformation_policy_applicability_tests
      PRIVATE meshvale::gltf_file_workflow)
    meshvale_warnings(meshvale_gltf_deformation_policy_applicability_tests)
    meshvale_strict_floating_point(meshvale_gltf_deformation_policy_applicability_tests)
    add_executable(meshvale_gltf_deformation_format_preservation_tests
      tests/gltf_deformation_format_preservation.cpp)
    target_link_libraries(meshvale_gltf_deformation_format_preservation_tests
      PRIVATE meshvale::gltf_file_workflow)
    meshvale_warnings(meshvale_gltf_deformation_format_preservation_tests)
    meshvale_strict_floating_point(meshvale_gltf_deformation_format_preservation_tests)
    meshvale_select_deformation_positive_tests(
      meshvale_gltf_deformation_format_preservation_tests
      meshvale_gltf_deformation_policy_applicability_tests)
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
