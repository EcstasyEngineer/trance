"""Live regression for nested progress and replacement pattern sources (#68).

Usage: python tests/qa_playback_progress.py path/to/trance.exe

Requires a desktop and GPU. Briefly shows a muted window containing a synthetic
gray image. Session, system settings, media and logs are isolated in a temporary
directory; no existing player or user session is controlled. Not a CTest test.
"""

import argparse
import json
import pathlib
import struct
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=pathlib.Path)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="trance-progress-qa-") as directory:
        root = pathlib.Path(directory)
        # A two-by-two gray BMP, including four-byte row alignment.
        bitmap = (b"BM" + struct.pack("<IHHI", 70, 0, 0, 54)
                  + struct.pack("<IIIHHIIIIII", 40, 2, 2, 1, 24, 0, 16, 2835, 2835, 0, 0)
                  + bytes([64, 64, 64, 64, 64, 64, 0, 0]) * 2)
        (root / "still.bmp").write_bytes(bitmap)
        session = {
            "format": "trance-session", "format_version": 1,
            "theme_scan_root": {"dir": ".", "auto_rescan": False},
            "first_playlist_item": "test",
            "playlist": {"test": {"standard": {"program": "test"}}},
            "program_map": {"test": {
                "enabled_theme": [{"theme_name": "test", "random_weight": 1}],
                "visual_type": [{"type": "parallel", "random_weight": 1}],
                "global_fps": 60}},
            "theme_map": {"test": {"image_path": ["still.bmp"]}},
        }
        (root / "session.json").write_text(json.dumps(session), encoding="utf-8")
        (root / "system.json").write_text(json.dumps({
            "format": "trance-system", "format_version": 1, "windowed": True,
        }), encoding="utf-8")
        stdout_path, stderr_path = root / "stdout.log", root / "stderr.log"
        replies = []
        with stdout_path.open("w", encoding="utf-8") as stdout, \
                stderr_path.open("w", encoding="utf-8") as stderr:
            proc = subprocess.Popen(
                [str(exe), "--mcp", "--hidden", "--muted",
                 str(root / "session.json"), str(root / "system.json")],
                cwd=root, stdin=subprocess.PIPE, stdout=stdout, stderr=stderr,
                text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

            def send(request):
                proc.stdin.write(json.dumps(request) + "\n")
                proc.stdin.flush()

            def call(identifier, name, arguments=None):
                send({"jsonrpc": "2.0", "id": identifier, "method": "tools/call",
                      "params": {"name": name, "arguments": arguments or {}}})

            def wait_for_section(section):
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    log = stderr_path.read_text(encoding="utf-8", errors="replace")
                    if section in log:
                        return
                    if proc.poll() is not None:
                        raise AssertionError(f"player exited before {section}:\n{log}")
                    time.sleep(0.05)
                raise AssertionError(f"missing active section {section}:\n{log}")

            def source(name):
                return (f"pattern {name} for 60f seq {{ "
                        "pattern opening for 30f { every 10f { image primary } } "
                        "pattern closing for 30f { every 4f { image secondary } } }")

            try:
                send({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                    "protocolVersion": "2024-11-05", "capabilities": {},
                    "clientInfo": {"name": "playback-progress-qa", "version": "1"}}})
                call(2, "load_pattern_source", {"source": source("first")})
                call(3, "show")
                wait_for_section("first/opening")
                wait_for_section("first/closing")
                call(4, "load_pattern_source", {"source": source("second")})
                # An ok reply alone missed #68: these prove the second compiled
                # schedule actually became active and traversed its own children.
                wait_for_section("second/opening")
                wait_for_section("second/closing")
                call(5, "hide")
            finally:
                proc.stdin.close()  # MCP EOF requests orderly shutdown.
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
                    raise AssertionError("player did not exit after MCP EOF")
        assert proc.returncode == 0, f"player exit code {proc.returncode}"
        for line in stdout_path.read_text(encoding="utf-8").splitlines():
            replies.append(json.loads(line))  # No console output may leak onto MCP stdout.
        assert {reply["id"] for reply in replies} == {1, 2, 3, 4, 5}, replies
        assert all("result" in reply and "error" not in reply
                   and not reply["result"].get("isError") for reply in replies), replies
        print("QA OK: nested sections, replacement source, clean MCP stdout and orderly exit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
