"""Run direct native V4 parser/compiler execution behavior checks.

No V4-to-V3 adapter is imported or invoked. Recording renderer avoids real media,
GPU pixels, displays, and wall-clock timing.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/windows-msvc/tests/Release/v4_native_execution_test.exe")
    args = parser.parse_args()
    if not args.exe.is_file():
        parser.error("Build v4_native_execution_test first, or provide --exe")
    return subprocess.run([str(args.exe.resolve())], check=False).returncode

if __name__ == "__main__":
    raise SystemExit(main())
