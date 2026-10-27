"""Package the Windows v4.0 executable using an explicit content allowlist."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    stage = ROOT / "dist" / "v4.0" / "package"
    if stage.exists():
        assert stage.resolve() == ROOT / "dist/v4.0/package"
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    executable = ROOT / "build/windows-msvc/Release/trance.exe"
    shutil.copy2(executable, stage / "trance.exe")
    shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")
    (stage / "README.txt").write_text(
        "trance v4.0 for Windows x64\n\n"
        "Run trance.exe with a session JSON file, or with media in the working folder.\n"
        "F2: settings; M: mute; Escape: exit; Shift+F11: hide/pause/mute.\n"
        "Custom visual patterns require V4 JSON ASTs. V3 text is unsupported.\n"
        "Composer: https://ecstasyengineer.github.io/trance/\n"
        "Docs: https://github.com/EcstasyEngineer/trance/tree/v4.0/docs\n"
        "The executable is unsigned. GPU/headset results require device testing.\n"
        "Third-party licenses are included in third-party/.\n", encoding="utf-8")
    (stage / "BUILDINFO.txt").write_text(
        "Version: 4.0.0\nTag: v4.0\nSource: " + args.commit + "\n"
        "Build: supported windows-release CMake preset; Visual Studio 2026\n"
        "Tests: Release CTest and native/static composer checks\n", encoding="utf-8")
    notices = ROOT / "build/windows-msvc/vcpkg_installed/x64-windows-static-md/share"
    for license_file in sorted(notices.glob("*/copyright")):
        target = stage / "third-party" / license_file.parent.name / "copyright"
        target.parent.mkdir(parents=True)
        shutil.copy2(license_file, target)
    if not list((stage / "third-party").glob("*/copyright")):
        raise RuntimeError("Dependency license notices are missing")
    output = stage.parent
    shutil.copy2(executable, output / "trance.exe")
    archive = output / "trance-v4.0-windows-x64.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zipped:
        for item in sorted(stage.rglob("*")):
            if item.is_file():
                zipped.write(item, item.relative_to(stage).as_posix())
    assets = [output / "trance.exe", archive]
    (output / "SHA256SUMS.txt").write_text("".join(
        hashlib.sha256(item.read_bytes()).hexdigest() + "  " + item.name + "\n"
        for item in assets), encoding="utf-8")
    print("Packaged executable, archive, checksums and dependency notices")

if __name__ == "__main__":
    main()
