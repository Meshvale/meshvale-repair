# SPDX-License-Identifier: Apache-2.0
"""Check project C++ formatting with the pinned Google-style formatter."""
import argparse
from pathlib import Path
import re
import subprocess
import sys


FORMATTER_VERSION = "23.1.3"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--formatter", default="clang-format")
    options = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    version = subprocess.run(
        [options.formatter, "--version"], check=True, capture_output=True, text=True
    ).stdout.strip()
    if not re.search(r"\bversion " + re.escape(FORMATTER_VERSION) + r"\b", version):
        parser.error(f"clang-format {FORMATTER_VERSION} required; found {version}")
    inventory = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root, check=True, capture_output=True,
    ).stdout.decode("utf-8").split("\0")
    sources = sorted({name for name in inventory if name and
                      Path(name).suffix in {".h", ".hpp", ".cpp"} and
                      (root / name).is_file()})
    if sources:
        subprocess.run(
            [options.formatter, "--dry-run", "--Werror", *sources],
            cwd=root, check=True,
        )
    print(f"C++ format check passed: {len(sources)} files; {version}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"C++ format check failed: {error}", file=sys.stderr)
        sys.exit(1)
