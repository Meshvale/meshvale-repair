// SPDX-License-Identifier: Apache-2.0
#ifndef MESHVALE_REPAIR_TESTS_HASH_VECTORS_H_
#define MESHVALE_REPAIR_TESTS_HASH_VECTORS_H_
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
namespace hash_fixture {
inline constexpr std::array<std::byte, 0> kVector0{};
inline constexpr std::array<std::byte, 3> kVector1{
    std::byte{0x61},
    std::byte{0x62},
    std::byte{0x63},
};
inline constexpr std::array<std::byte, 56> kVector2{
    std::byte{0x61}, std::byte{0x62}, std::byte{0x63}, std::byte{0x64},
    std::byte{0x62}, std::byte{0x63}, std::byte{0x64}, std::byte{0x65},
    std::byte{0x63}, std::byte{0x64}, std::byte{0x65}, std::byte{0x66},
    std::byte{0x64}, std::byte{0x65}, std::byte{0x66}, std::byte{0x67},
    std::byte{0x65}, std::byte{0x66}, std::byte{0x67}, std::byte{0x68},
    std::byte{0x66}, std::byte{0x67}, std::byte{0x68}, std::byte{0x69},
    std::byte{0x67}, std::byte{0x68}, std::byte{0x69}, std::byte{0x6a},
    std::byte{0x68}, std::byte{0x69}, std::byte{0x6a}, std::byte{0x6b},
    std::byte{0x69}, std::byte{0x6a}, std::byte{0x6b}, std::byte{0x6c},
    std::byte{0x6a}, std::byte{0x6b}, std::byte{0x6c}, std::byte{0x6d},
    std::byte{0x6b}, std::byte{0x6c}, std::byte{0x6d}, std::byte{0x6e},
    std::byte{0x6c}, std::byte{0x6d}, std::byte{0x6e}, std::byte{0x6f},
    std::byte{0x6d}, std::byte{0x6e}, std::byte{0x6f}, std::byte{0x70},
    std::byte{0x6e}, std::byte{0x6f}, std::byte{0x70}, std::byte{0x71},
};
inline constexpr std::array<std::byte, 256> kVector3{
    std::byte{0x00}, std::byte{0x01}, std::byte{0x02}, std::byte{0x03},
    std::byte{0x04}, std::byte{0x05}, std::byte{0x06}, std::byte{0x07},
    std::byte{0x08}, std::byte{0x09}, std::byte{0x0a}, std::byte{0x0b},
    std::byte{0x0c}, std::byte{0x0d}, std::byte{0x0e}, std::byte{0x0f},
    std::byte{0x10}, std::byte{0x11}, std::byte{0x12}, std::byte{0x13},
    std::byte{0x14}, std::byte{0x15}, std::byte{0x16}, std::byte{0x17},
    std::byte{0x18}, std::byte{0x19}, std::byte{0x1a}, std::byte{0x1b},
    std::byte{0x1c}, std::byte{0x1d}, std::byte{0x1e}, std::byte{0x1f},
    std::byte{0x20}, std::byte{0x21}, std::byte{0x22}, std::byte{0x23},
    std::byte{0x24}, std::byte{0x25}, std::byte{0x26}, std::byte{0x27},
    std::byte{0x28}, std::byte{0x29}, std::byte{0x2a}, std::byte{0x2b},
    std::byte{0x2c}, std::byte{0x2d}, std::byte{0x2e}, std::byte{0x2f},
    std::byte{0x30}, std::byte{0x31}, std::byte{0x32}, std::byte{0x33},
    std::byte{0x34}, std::byte{0x35}, std::byte{0x36}, std::byte{0x37},
    std::byte{0x38}, std::byte{0x39}, std::byte{0x3a}, std::byte{0x3b},
    std::byte{0x3c}, std::byte{0x3d}, std::byte{0x3e}, std::byte{0x3f},
    std::byte{0x40}, std::byte{0x41}, std::byte{0x42}, std::byte{0x43},
    std::byte{0x44}, std::byte{0x45}, std::byte{0x46}, std::byte{0x47},
    std::byte{0x48}, std::byte{0x49}, std::byte{0x4a}, std::byte{0x4b},
    std::byte{0x4c}, std::byte{0x4d}, std::byte{0x4e}, std::byte{0x4f},
    std::byte{0x50}, std::byte{0x51}, std::byte{0x52}, std::byte{0x53},
    std::byte{0x54}, std::byte{0x55}, std::byte{0x56}, std::byte{0x57},
    std::byte{0x58}, std::byte{0x59}, std::byte{0x5a}, std::byte{0x5b},
    std::byte{0x5c}, std::byte{0x5d}, std::byte{0x5e}, std::byte{0x5f},
    std::byte{0x60}, std::byte{0x61}, std::byte{0x62}, std::byte{0x63},
    std::byte{0x64}, std::byte{0x65}, std::byte{0x66}, std::byte{0x67},
    std::byte{0x68}, std::byte{0x69}, std::byte{0x6a}, std::byte{0x6b},
    std::byte{0x6c}, std::byte{0x6d}, std::byte{0x6e}, std::byte{0x6f},
    std::byte{0x70}, std::byte{0x71}, std::byte{0x72}, std::byte{0x73},
    std::byte{0x74}, std::byte{0x75}, std::byte{0x76}, std::byte{0x77},
    std::byte{0x78}, std::byte{0x79}, std::byte{0x7a}, std::byte{0x7b},
    std::byte{0x7c}, std::byte{0x7d}, std::byte{0x7e}, std::byte{0x7f},
    std::byte{0x80}, std::byte{0x81}, std::byte{0x82}, std::byte{0x83},
    std::byte{0x84}, std::byte{0x85}, std::byte{0x86}, std::byte{0x87},
    std::byte{0x88}, std::byte{0x89}, std::byte{0x8a}, std::byte{0x8b},
    std::byte{0x8c}, std::byte{0x8d}, std::byte{0x8e}, std::byte{0x8f},
    std::byte{0x90}, std::byte{0x91}, std::byte{0x92}, std::byte{0x93},
    std::byte{0x94}, std::byte{0x95}, std::byte{0x96}, std::byte{0x97},
    std::byte{0x98}, std::byte{0x99}, std::byte{0x9a}, std::byte{0x9b},
    std::byte{0x9c}, std::byte{0x9d}, std::byte{0x9e}, std::byte{0x9f},
    std::byte{0xa0}, std::byte{0xa1}, std::byte{0xa2}, std::byte{0xa3},
    std::byte{0xa4}, std::byte{0xa5}, std::byte{0xa6}, std::byte{0xa7},
    std::byte{0xa8}, std::byte{0xa9}, std::byte{0xaa}, std::byte{0xab},
    std::byte{0xac}, std::byte{0xad}, std::byte{0xae}, std::byte{0xaf},
    std::byte{0xb0}, std::byte{0xb1}, std::byte{0xb2}, std::byte{0xb3},
    std::byte{0xb4}, std::byte{0xb5}, std::byte{0xb6}, std::byte{0xb7},
    std::byte{0xb8}, std::byte{0xb9}, std::byte{0xba}, std::byte{0xbb},
    std::byte{0xbc}, std::byte{0xbd}, std::byte{0xbe}, std::byte{0xbf},
    std::byte{0xc0}, std::byte{0xc1}, std::byte{0xc2}, std::byte{0xc3},
    std::byte{0xc4}, std::byte{0xc5}, std::byte{0xc6}, std::byte{0xc7},
    std::byte{0xc8}, std::byte{0xc9}, std::byte{0xca}, std::byte{0xcb},
    std::byte{0xcc}, std::byte{0xcd}, std::byte{0xce}, std::byte{0xcf},
    std::byte{0xd0}, std::byte{0xd1}, std::byte{0xd2}, std::byte{0xd3},
    std::byte{0xd4}, std::byte{0xd5}, std::byte{0xd6}, std::byte{0xd7},
    std::byte{0xd8}, std::byte{0xd9}, std::byte{0xda}, std::byte{0xdb},
    std::byte{0xdc}, std::byte{0xdd}, std::byte{0xde}, std::byte{0xdf},
    std::byte{0xe0}, std::byte{0xe1}, std::byte{0xe2}, std::byte{0xe3},
    std::byte{0xe4}, std::byte{0xe5}, std::byte{0xe6}, std::byte{0xe7},
    std::byte{0xe8}, std::byte{0xe9}, std::byte{0xea}, std::byte{0xeb},
    std::byte{0xec}, std::byte{0xed}, std::byte{0xee}, std::byte{0xef},
    std::byte{0xf0}, std::byte{0xf1}, std::byte{0xf2}, std::byte{0xf3},
    std::byte{0xf4}, std::byte{0xf5}, std::byte{0xf6}, std::byte{0xf7},
    std::byte{0xf8}, std::byte{0xf9}, std::byte{0xfa}, std::byte{0xfb},
    std::byte{0xfc}, std::byte{0xfd}, std::byte{0xfe}, std::byte{0xff},
};
inline constexpr std::array<std::byte, 63> kVector4{
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
};
inline constexpr std::array<std::byte, 64> kVector5{
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
};
inline constexpr std::array<std::byte, 65> kVector6{
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61},
};
inline constexpr std::array<std::byte, 56> kVector7{
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
    std::byte{0x61}, std::byte{0x61}, std::byte{0x61}, std::byte{0x61},
};
inline constexpr std::array<std::byte, 174> kVector8{
    std::byte{0x6d}, std::byte{0x65}, std::byte{0x73}, std::byte{0x68},
    std::byte{0x76}, std::byte{0x61}, std::byte{0x6c}, std::byte{0x65},
    std::byte{0x2e}, std::byte{0x67}, std::byte{0x6c}, std::byte{0x74},
    std::byte{0x66}, std::byte{0x2d}, std::byte{0x66}, std::byte{0x69},
    std::byte{0x6c}, std::byte{0x65}, std::byte{0x2d}, std::byte{0x73},
    std::byte{0x6e}, std::byte{0x61}, std::byte{0x70}, std::byte{0x73},
    std::byte{0x68}, std::byte{0x6f}, std::byte{0x74}, std::byte{0x2f},
    std::byte{0x31}, std::byte{0x00}, std::byte{0x09}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x67}, std::byte{0x6c},
    std::byte{0x74}, std::byte{0x66}, std::byte{0x2d}, std::byte{0x6a},
    std::byte{0x73}, std::byte{0x6f}, std::byte{0x6e}, std::byte{0x03},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x05},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x61},
    std::byte{0x2e}, std::byte{0x62}, std::byte{0x69}, std::byte{0x6e},
    std::byte{0x08}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x7f}, std::byte{0x80}, std::byte{0xff},
    std::byte{0x54}, std::byte{0x41}, std::byte{0x49}, std::byte{0x4c},
    std::byte{0x0d}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x6f}, std::byte{0x70}, std::byte{0x61}, std::byte{0x71},
    std::byte{0x75}, std::byte{0x65}, std::byte{0x2f}, std::byte{0xc3},
    std::byte{0xa9}, std::byte{0x2e}, std::byte{0x62}, std::byte{0x69},
    std::byte{0x6e}, std::byte{0x08}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0xff}, std::byte{0x4f},
    std::byte{0x50}, std::byte{0x41}, std::byte{0x51}, std::byte{0x55},
    std::byte{0x45}, std::byte{0x0a}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x73}, std::byte{0x63}, std::byte{0x65},
    std::byte{0x6e}, std::byte{0x65}, std::byte{0x2e}, std::byte{0x67},
    std::byte{0x6c}, std::byte{0x74}, std::byte{0x66}, std::byte{0x1b},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x7b},
    std::byte{0x22}, std::byte{0x61}, std::byte{0x73}, std::byte{0x73},
    std::byte{0x65}, std::byte{0x74}, std::byte{0x22}, std::byte{0x3a},
    std::byte{0x7b}, std::byte{0x22}, std::byte{0x76}, std::byte{0x65},
    std::byte{0x72}, std::byte{0x73}, std::byte{0x69}, std::byte{0x6f},
    std::byte{0x6e}, std::byte{0x22}, std::byte{0x3a}, std::byte{0x22},
    std::byte{0x32}, std::byte{0x2e}, std::byte{0x30}, std::byte{0x22},
    std::byte{0x7d}, std::byte{0x7d},
};
inline constexpr std::array<std::byte, 209> kVector9{
    std::byte{0x6d}, std::byte{0x65}, std::byte{0x73}, std::byte{0x68},
    std::byte{0x76}, std::byte{0x61}, std::byte{0x6c}, std::byte{0x65},
    std::byte{0x2e}, std::byte{0x67}, std::byte{0x6c}, std::byte{0x74},
    std::byte{0x66}, std::byte{0x2d}, std::byte{0x66}, std::byte{0x69},
    std::byte{0x6c}, std::byte{0x65}, std::byte{0x2d}, std::byte{0x73},
    std::byte{0x6e}, std::byte{0x61}, std::byte{0x70}, std::byte{0x73},
    std::byte{0x68}, std::byte{0x6f}, std::byte{0x74}, std::byte{0x2f},
    std::byte{0x31}, std::byte{0x00}, std::byte{0x03}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x67}, std::byte{0x6c},
    std::byte{0x62}, std::byte{0x03}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x05}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x61}, std::byte{0x2e}, std::byte{0x62},
    std::byte{0x69}, std::byte{0x6e}, std::byte{0x05}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0x54},
    std::byte{0x41}, std::byte{0x49}, std::byte{0x4c}, std::byte{0x0d},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x6f},
    std::byte{0x70}, std::byte{0x61}, std::byte{0x71}, std::byte{0x75},
    std::byte{0x65}, std::byte{0x2f}, std::byte{0xc3}, std::byte{0xa9},
    std::byte{0x2e}, std::byte{0x62}, std::byte{0x69}, std::byte{0x6e},
    std::byte{0x08}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0xff}, std::byte{0x4f}, std::byte{0x50},
    std::byte{0x41}, std::byte{0x51}, std::byte{0x55}, std::byte{0x45},
    std::byte{0x09}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x73}, std::byte{0x63}, std::byte{0x65}, std::byte{0x6e},
    std::byte{0x65}, std::byte{0x2e}, std::byte{0x67}, std::byte{0x6c},
    std::byte{0x62}, std::byte{0x48}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x67}, std::byte{0x6c}, std::byte{0x54},
    std::byte{0x46}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x48}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x1c}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x4a}, std::byte{0x53}, std::byte{0x4f},
    std::byte{0x4e}, std::byte{0x7b}, std::byte{0x22}, std::byte{0x61},
    std::byte{0x73}, std::byte{0x73}, std::byte{0x65}, std::byte{0x74},
    std::byte{0x22}, std::byte{0x3a}, std::byte{0x7b}, std::byte{0x22},
    std::byte{0x76}, std::byte{0x65}, std::byte{0x72}, std::byte{0x73},
    std::byte{0x69}, std::byte{0x6f}, std::byte{0x6e}, std::byte{0x22},
    std::byte{0x3a}, std::byte{0x22}, std::byte{0x32}, std::byte{0x2e},
    std::byte{0x30}, std::byte{0x22}, std::byte{0x7d}, std::byte{0x7d},
    std::byte{0x20}, std::byte{0x04}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x42}, std::byte{0x49}, std::byte{0x4e},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x7f}, std::byte{0x80},
    std::byte{0xff}, std::byte{0x04}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x78}, std::byte{0x56}, std::byte{0x34},
    std::byte{0x12}, std::byte{0xde}, std::byte{0xad}, std::byte{0xbe},
    std::byte{0xef},
};
struct Vector {
  std::string_view name;
  std::span<const std::byte> bytes;
  std::string_view expected;
};
inline constexpr std::array<Vector, 10> kVectors{{
    {"empty", kVector0,
     "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc", kVector1,
     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"fips-multiblock", kVector2,
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    {"all-byte-values", kVector3,
     "40aff2e9d2d8922e47afd4648e6967497158785fbd1da870e7110266bf944880"},
    {"block-minus-one", kVector4,
     "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {"one-block", kVector5,
     "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {"block-plus-one", kVector6,
     "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {"length-encoding-boundary", kVector7,
     "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {"json-tail-opaque.framed", kVector8,
     "c463ce11fa3ef2d85e5d04ac56ebc03e7919cf87a0253bb2401b934bb90cd130"},
    {"glb-full-wire.framed", kVector9,
     "d6464b8b230ed9549d1a1d659c3b0467a6b39f111d0eedd94aee98ff46536167"},
}};
struct File {
  std::span<const std::byte> path;
  std::span<const std::byte> bytes;
};
inline constexpr std::array<std::byte, 9> kFormat0{
    std::byte{0x67}, std::byte{0x6c}, std::byte{0x74},
    std::byte{0x66}, std::byte{0x2d}, std::byte{0x6a},
    std::byte{0x73}, std::byte{0x6f}, std::byte{0x6e},
};
inline constexpr std::array<std::byte, 5> kPath0_0{
    std::byte{0x61}, std::byte{0x2e}, std::byte{0x62},
    std::byte{0x69}, std::byte{0x6e},
};
inline constexpr std::array<std::byte, 8> kFile0_0{
    std::byte{0x00}, std::byte{0x7f}, std::byte{0x80}, std::byte{0xff},
    std::byte{0x54}, std::byte{0x41}, std::byte{0x49}, std::byte{0x4c},
};
inline constexpr std::array<std::byte, 13> kPath0_1{
    std::byte{0x6f}, std::byte{0x70}, std::byte{0x61}, std::byte{0x71},
    std::byte{0x75}, std::byte{0x65}, std::byte{0x2f}, std::byte{0xc3},
    std::byte{0xa9}, std::byte{0x2e}, std::byte{0x62}, std::byte{0x69},
    std::byte{0x6e},
};
inline constexpr std::array<std::byte, 8> kFile0_1{
    std::byte{0x00}, std::byte{0xff}, std::byte{0x4f}, std::byte{0x50},
    std::byte{0x41}, std::byte{0x51}, std::byte{0x55}, std::byte{0x45},
};
inline constexpr std::array<std::byte, 10> kPath0_2{
    std::byte{0x73}, std::byte{0x63}, std::byte{0x65}, std::byte{0x6e},
    std::byte{0x65}, std::byte{0x2e}, std::byte{0x67}, std::byte{0x6c},
    std::byte{0x74}, std::byte{0x66},
};
inline constexpr std::array<std::byte, 27> kFile0_2{
    std::byte{0x7b}, std::byte{0x22}, std::byte{0x61}, std::byte{0x73},
    std::byte{0x73}, std::byte{0x65}, std::byte{0x74}, std::byte{0x22},
    std::byte{0x3a}, std::byte{0x7b}, std::byte{0x22}, std::byte{0x76},
    std::byte{0x65}, std::byte{0x72}, std::byte{0x73}, std::byte{0x69},
    std::byte{0x6f}, std::byte{0x6e}, std::byte{0x22}, std::byte{0x3a},
    std::byte{0x22}, std::byte{0x32}, std::byte{0x2e}, std::byte{0x30},
    std::byte{0x22}, std::byte{0x7d}, std::byte{0x7d},
};
inline constexpr std::array<File, 3> kFiles0{{
    {kPath0_0, kFile0_0},
    {kPath0_1, kFile0_1},
    {kPath0_2, kFile0_2},
}};
inline constexpr std::array<std::byte, 3> kFormat1{
    std::byte{0x67},
    std::byte{0x6c},
    std::byte{0x62},
};
inline constexpr std::array<std::byte, 5> kPath1_0{
    std::byte{0x61}, std::byte{0x2e}, std::byte{0x62},
    std::byte{0x69}, std::byte{0x6e},
};
inline constexpr std::array<std::byte, 5> kFile1_0{
    std::byte{0x01}, std::byte{0x54}, std::byte{0x41},
    std::byte{0x49}, std::byte{0x4c},
};
inline constexpr std::array<std::byte, 13> kPath1_1{
    std::byte{0x6f}, std::byte{0x70}, std::byte{0x61}, std::byte{0x71},
    std::byte{0x75}, std::byte{0x65}, std::byte{0x2f}, std::byte{0xc3},
    std::byte{0xa9}, std::byte{0x2e}, std::byte{0x62}, std::byte{0x69},
    std::byte{0x6e},
};
inline constexpr std::array<std::byte, 8> kFile1_1{
    std::byte{0x00}, std::byte{0xff}, std::byte{0x4f}, std::byte{0x50},
    std::byte{0x41}, std::byte{0x51}, std::byte{0x55}, std::byte{0x45},
};
inline constexpr std::array<std::byte, 9> kPath1_2{
    std::byte{0x73}, std::byte{0x63}, std::byte{0x65},
    std::byte{0x6e}, std::byte{0x65}, std::byte{0x2e},
    std::byte{0x67}, std::byte{0x6c}, std::byte{0x62},
};
inline constexpr std::array<std::byte, 72> kFile1_2{
    std::byte{0x67}, std::byte{0x6c}, std::byte{0x54}, std::byte{0x46},
    std::byte{0x02}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x48}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x1c}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x4a}, std::byte{0x53}, std::byte{0x4f}, std::byte{0x4e},
    std::byte{0x7b}, std::byte{0x22}, std::byte{0x61}, std::byte{0x73},
    std::byte{0x73}, std::byte{0x65}, std::byte{0x74}, std::byte{0x22},
    std::byte{0x3a}, std::byte{0x7b}, std::byte{0x22}, std::byte{0x76},
    std::byte{0x65}, std::byte{0x72}, std::byte{0x73}, std::byte{0x69},
    std::byte{0x6f}, std::byte{0x6e}, std::byte{0x22}, std::byte{0x3a},
    std::byte{0x22}, std::byte{0x32}, std::byte{0x2e}, std::byte{0x30},
    std::byte{0x22}, std::byte{0x7d}, std::byte{0x7d}, std::byte{0x20},
    std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x42}, std::byte{0x49}, std::byte{0x4e}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x7f}, std::byte{0x80}, std::byte{0xff},
    std::byte{0x04}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
    std::byte{0xde}, std::byte{0xad}, std::byte{0xbe}, std::byte{0xef},
};
inline constexpr std::array<File, 3> kFiles1{{
    {kPath1_0, kFile1_0},
    {kPath1_1, kFile1_1},
    {kPath1_2, kFile1_2},
}};
struct Framing {
  std::string_view name;
  std::span<const std::byte> format;
  std::span<const File> files;
  std::size_t framed_bytes;
  std::string_view expected;
};
inline constexpr std::array<Framing, 2> kFramings{{
    {"json-tail-opaque", kFormat0, kFiles0, 174,
     "c463ce11fa3ef2d85e5d04ac56ebc03e7919cf87a0253bb2401b934bb90cd130"},
    {"glb-full-wire", kFormat1, kFiles1, 209,
     "d6464b8b230ed9549d1a1d659c3b0467a6b39f111d0eedd94aee98ff46536167"},
}};
}  // namespace hash_fixture
#endif  // MESHVALE_REPAIR_TESTS_HASH_VECTORS_H_
