# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(MeshvaleRepairPlyUnsupportedConsumer LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(MeshvaleRepair 0.0.0 EXACT CONFIG REQUIRED)
if(NOT TARGET meshvale::ply_file_workflow)
  message(FATAL_ERROR "Install Repair with MESHVALE_REPAIR_PLY_FILE_WORKFLOW=ON")
endif()
add_executable(meshvale_ply_workflow_unsupported_consumer main.cpp)
target_link_libraries(meshvale_ply_workflow_unsupported_consumer PRIVATE meshvale::ply_file_workflow)
target_compile_features(meshvale_ply_workflow_unsupported_consumer PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(meshvale_ply_workflow_unsupported_consumer PRIVATE /W4 /WX /fp:strict)
else()
  target_compile_options(meshvale_ply_workflow_unsupported_consumer PRIVATE
    -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off)
endif()
enable_testing()
# Registration selects no producer behavior: nonempty capture must actually
# refuse with a genuine unsupported numerical producer.
add_test(NAME installed_ply_workflow_unsupported
  COMMAND meshvale_ply_workflow_unsupported_consumer)
