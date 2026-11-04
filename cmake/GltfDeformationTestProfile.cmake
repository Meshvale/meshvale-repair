# SPDX-License-Identifier: Apache-2.0
# Test evidence hook, selected explicitly with CMAKE_PROJECT_INCLUDE. It records
# the actual project generator; it does not change production configuration.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  get_property(_meshvale_domain_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
  if(_meshvale_domain_multi)
    set(_meshvale_domain_kind "multi")
  else()
    set(_meshvale_domain_kind "single")
  endif()
  foreach(_meshvale_domain_value IN ITEMS CMAKE_GENERATOR CMAKE_BUILD_TYPE
                                           CMAKE_CONFIGURATION_TYPES)
    string(REPLACE "\\" "\\\\" _meshvale_domain_${_meshvale_domain_value}
           "${${_meshvale_domain_value}}")
    string(REPLACE "\"" "\\\"" _meshvale_domain_${_meshvale_domain_value}
           "${_meshvale_domain_${_meshvale_domain_value}}")
  endforeach()
  file(WRITE "${CMAKE_BINARY_DIR}/gltf-domain-generator.json"
    "{\"schema\":\"meshvale.gltf-deformation-generator/1\","
    "\"generator\":\"${_meshvale_domain_CMAKE_GENERATOR}\","
    "\"kind\":\"${_meshvale_domain_kind}\","
    "\"build_type\":\"${_meshvale_domain_CMAKE_BUILD_TYPE}\","
    "\"configuration_types\":\"${_meshvale_domain_CMAKE_CONFIGURATION_TYPES}\"}\n")
  function(_meshvale_record_domain_inputs)
    set(_roots "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/src"
               "${CMAKE_SOURCE_DIR}/python" "${CMAKE_BINARY_DIR}/_deps")
    foreach(_package IN ITEMS MeshvaleGeometry MeshvaleInterchange)
      if(DEFINED ${_package}_DIR)
        get_filename_component(_prefix "${${_package}_DIR}/../../.." ABSOLUTE)
        list(APPEND _roots "${_prefix}/include")
      endif()
    endforeach()
    if(DEFINED tinyobjloader_DIR)
      get_filename_component(_prefix "${tinyobjloader_DIR}/../.." ABSOLUTE)
      list(APPEND _roots "${_prefix}/include")
    endif()
    set(_inputs "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
    if(DEFINED MESHVALE_DEFORMATION_READ_SOURCE AND
       EXISTS "${MESHVALE_DEFORMATION_READ_SOURCE}")
      list(APPEND _inputs "${MESHVALE_DEFORMATION_READ_SOURCE}")
    endif()
    foreach(_root IN LISTS _roots)
      if(IS_DIRECTORY "${_root}")
        file(GLOB_RECURSE _files LIST_DIRECTORIES FALSE
             "${_root}/*.h" "${_root}/*.hpp" "${_root}/*.inc"
             "${_root}/*.cpp" "${_root}/*.cc" "${_root}/*.cxx" "${_root}/*.c")
        list(APPEND _inputs ${_files})
      endif()
    endforeach()
    list(REMOVE_DUPLICATES _inputs)
    list(SORT _inputs)
    set(_records "")
    foreach(_input IN LISTS _inputs)
      file(REAL_PATH "${_input}" _real)
      file(SHA256 "${_real}" _sha)
      file(SIZE "${_real}" _bytes)
      string(REPLACE "\\" "/" _real "${_real}")
      string(REPLACE "\"" "\\\"" _real "${_real}")
      if(NOT _records STREQUAL "")
        string(APPEND _records ",")
      endif()
      string(APPEND _records "{\"path\":\"${_real}\",\"bytes\":${_bytes},\"sha256\":\"${_sha}\"}")
    endforeach()
    file(WRITE "${CMAKE_BINARY_DIR}/gltf-domain-configure-inputs.json"
      "{\"schema\":\"meshvale.gltf-deformation-configure-inputs/1\","
      "\"timing\":\"end-of-configure-before-build\",\"files\":[${_records}]}\n")
  endfunction()
  # FetchContent/configure_file inputs now exist. This does not invent hashes
  # for future file(GENERATE) outputs or compiler-produced files.
  cmake_language(DEFER CALL _meshvale_record_domain_inputs)
endif()
