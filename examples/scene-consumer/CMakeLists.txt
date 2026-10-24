# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(MeshvaleStaticSceneConsumer LANGUAGES CXX)

find_package(MeshvaleInterchange 0.0.0 EXACT CONFIG REQUIRED)
if(NOT TARGET meshvale::gltf_files)
  message(FATAL_ERROR "Scene inspection requires the installed Interchange gltf_files target")
endif()

add_executable(meshvale_gltf_scene_inspector main.cpp)
target_compile_features(meshvale_gltf_scene_inspector PRIVATE cxx_std_20)
set_target_properties(meshvale_gltf_scene_inspector PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(meshvale_gltf_scene_inspector PRIVATE meshvale::gltf_files)
if(MSVC)
  target_compile_options(meshvale_gltf_scene_inspector PRIVATE /W4 /WX /fp:strict)
else()
  target_compile_options(meshvale_gltf_scene_inspector PRIVATE
    -Wall -Wextra -Wpedantic -Werror -fno-fast-math -ffp-contract=off)
endif()
