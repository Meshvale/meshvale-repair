# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.24)
project(MeshvaleRepairInstalledConsumer LANGUAGES CXX)
find_package(MeshvaleRepair CONFIG REQUIRED)
add_executable(meshvale_repair_consumer main.cpp)
target_link_libraries(meshvale_repair_consumer PRIVATE meshvale::repair)
enable_testing()
add_test(NAME installed_repair_consumer COMMAND meshvale_repair_consumer)
