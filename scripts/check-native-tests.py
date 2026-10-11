# SPDX-License-Identifier: Apache-2.0
"""Check the configured Repair profile and exact native/installed test inventory."""
import argparse
import json
from pathlib import Path
import re
import subprocess

QUALIFIED_PROFILE = "qualified"
UNSUPPORTED_PROFILE = "unsupported"
DISABLED_PROFILE = "off"
PROFILES = (QUALIFIED_PROFILE, UNSUPPORTED_PROFILE, DISABLED_PROFILE)
PROFILE_CACHE_KEY = "MESHVALE_REPAIR_MATERIALIZED_TEST_PROFILE"
MODULE_CACHE_KEY = "MESHVALE_REPAIR_MATERIALIZED_DUPLICATES"
TRUE_CACHE_VALUES = frozenset({"ON", "TRUE", "YES", "1"})
FALSE_CACHE_VALUES = frozenset({"OFF", "FALSE", "NO", "0"})
COMMON_TESTS = frozenset({"targeted_duplicates"})
COMMON_INSTALLED_TESTS = frozenset({"installed_repair_consumer"})
PROFILE_TESTS = {
    QUALIFIED_PROFILE: frozenset({"materialized_duplicate_requests",
        "materialized_duplicates", "materialized_duplicate_semantics"}),
    UNSUPPORTED_PROFILE: frozenset({"materialized_duplicate_requests",
        "materialized_duplicates_unsupported"}),
    DISABLED_PROFILE: frozenset(),
}
PROFILE_INSTALLED_TESTS = {
    QUALIFIED_PROFILE: frozenset({"installed_materialized_duplicates"}),
    UNSUPPORTED_PROFILE: frozenset({"installed_materialized_duplicates_unsupported"}),
    DISABLED_PROFILE: frozenset(),
}


def cache_entries(path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith(("#", "//")):
            continue
        name_and_type, separator, value = line.partition("=")
        name, typed, cache_type = name_and_type.partition(":")
        if separator and typed:
            if name in result:
                raise ValueError("duplicate CMake cache entry: " + name)
            result[name] = (cache_type, value)
    return result


def cache_value(cache, name, expected_type):
    if name not in cache or cache[name][0] != expected_type:
        raise ValueError("missing or incorrectly typed CMake cache entry: " + name)
    return cache[name][1]


def cache_bool(cache, name):
    value = cache_value(cache, name, "BOOL").upper()
    if value in TRUE_CACHE_VALUES:
        return True
    if value in FALSE_CACHE_VALUES:
        return False
    raise ValueError("unrecognized CMake boolean: " + name)


def validate_configuration(cache, configuration):
    if "CMAKE_CONFIGURATION_TYPES" in cache:
        configurations = cache_value(cache, "CMAKE_CONFIGURATION_TYPES", "STRING").split(";")
        if configuration not in configurations:
            raise ValueError("requested configuration is absent from multi-config cache")
    elif cache_value(cache, "CMAKE_BUILD_TYPE", "STRING") != configuration:
        raise ValueError("requested configuration differs from single-config build type")


def validate_cache(cache, profile, configuration, installed=False):
    validate_configuration(cache, configuration)
    if not installed:
        if not cache_bool(cache, "BUILD_TESTING"):
            raise ValueError("native inventory requires BUILD_TESTING=ON")
        if cache_bool(cache, MODULE_CACHE_KEY) != (profile != DISABLED_PROFILE):
            raise ValueError("requested profile differs from configured charged module")
    if profile != DISABLED_PROFILE:
        if cache_value(cache, PROFILE_CACHE_KEY, "STRING") != profile:
            raise ValueError("requested profile differs from configured test profile")


def validate_compiler(build_dir, expected_id, expected_version):
    records = list((build_dir / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    if len(records) != 1:
        raise ValueError("need one fresh CMake C++ compiler identity record")
    text = records[0].read_text(encoding="utf-8")
    actual_id = re.search(r'set\(CMAKE_CXX_COMPILER_ID "([^"\n]+)"\)', text)
    actual_version = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"\n]+)"\)', text)
    if actual_id is None or actual_id.group(1) != expected_id:
        raise ValueError("configured C++ compiler differs from declared cohort")
    if expected_version and (actual_version is None or actual_version.group(1) != expected_version):
        raise ValueError("configured C++ compiler version differs from declared cohort")


def expected_inventory(profile, installed=False):
    common = COMMON_INSTALLED_TESTS if installed else COMMON_TESTS
    specific = PROFILE_INSTALLED_TESTS if installed else PROFILE_TESTS
    return common | specific[profile]


def validate_inventory(build_dir, configuration, expected, ctest):
    tests = json.loads(subprocess.check_output([
        ctest, "--test-dir", str(build_dir), "-C", configuration,
        "--show-only=json-v1"], text=True))["tests"]
    names = [test["name"] for test in tests]
    if len(names) != len(expected) or set(names) != expected:
        raise ValueError(f"expected {sorted(expected)}, found {names}")
    return names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--installed-build-dir", type=Path)
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--compiler-id", choices=("MSVC", "Clang", "GNU", "AppleClang"), required=True)
    parser.add_argument("--compiler-version")
    parser.add_argument("--materialization-profile", choices=PROFILES, required=True)
    parser.add_argument("--ctest", default="ctest")
    args = parser.parse_args()
    try:
        checked = []
        for build_dir, installed in ((args.build_dir, False), (args.installed_build_dir, True)):
            if build_dir is None:
                continue
            validate_cache(cache_entries(build_dir / "CMakeCache.txt"),
                           args.materialization_profile, args.configuration, installed)
            validate_compiler(build_dir, args.compiler_id, args.compiler_version)
            checked.append(("installed" if installed else "native", validate_inventory(
                build_dir, args.configuration,
                expected_inventory(args.materialization_profile, installed), args.ctest)))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    for label, names in checked:
        print(f"{label}: verified {len(names)} test entries; "
              f"{args.materialization_profile}; {args.configuration}; {names}")


if __name__ == "__main__":
    main()
