"""Generate all eight V4 examples afresh, lower them, then run native comparison.

Uses CPU recording draws, not media/GPU rendering or a generic V4 compiler.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from pattern_v4_examples import examples
from pattern_v4_lower import lower


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/windows-msvc/tests/Release/v4_lowering_execution_test.exe")
    args = parser.parse_args()
    if not args.exe.is_file():
        parser.error("Build v4_lowering_execution_test first, or provide --exe")
    with tempfile.TemporaryDirectory(prefix="trance-v4-probe-") as name:
        directory = Path(name)
        for label, document in examples().items():
            (directory / (label + ".pattern")).write_text(lower(document), encoding="utf-8")
        return subprocess.run([str(args.exe.resolve()), str(directory)], check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
