"""Live pixel regression for the Director's final-frame/reset boundary.

Requires a desktop/GPU and Pillow. Uses an isolated muted player, generated opaque
media, temporary settings and MCP; it does not control an existing player.
The one-frame pattern must stay drawable on every traversal, rather than resetting
to an inactive tree before the render. Flash Text is sampled separately with the
same ready media; this is not exhaustive media/headset acceptance coverage.
"""
import argparse
import json
from pathlib import Path
from queue import Queue, Empty
import struct
import subprocess
import tempfile
from threading import Thread
import time

from PIL import Image, ImageStat


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="trance-boundary-qa-") as directory:
        root = Path(directory)
        bitmap = (b"BM" + struct.pack("<IHHI", 70, 0, 0, 54)
                  + struct.pack("<IIIHHIIIIII", 40, 2, 2, 1, 24, 0, 16, 2835, 2835, 0, 0)
                  + bytes([192, 192, 192, 192, 192, 192, 0, 0]) * 2)
        (root / "still.bmp").write_bytes(bitmap)
        source = {"version": 4, "root": {"kind": "parallel", "duration": 1,
                  "children": [{"node": {"op": "image", "source": "primary"}}]}}
        for name in ("first", "second"):
            (root / f"{name}.pattern").write_text(json.dumps(source), encoding="utf-8")
        session = {
            "format": "trance-session", "format_version": 1,
            "theme_scan_root": {"dir": ".", "auto_rescan": False},
            "first_playlist_item": "test",
            "playlist": {"test": {"standard": {"program": "test"}}},
            "program_map": {"test": {
                "enabled_theme": [{"theme_name": "test", "random_weight": 1}],
                "visual_type": [{"type": "flash_text", "random_weight": 1}],
                "custom_visual_pattern": [{"name": name, "file": f"{name}.pattern",
                                           "random_weight": 1, "enabled": True,
                                           "pinned": True} for name in ("first", "second")],
                "global_fps": 60}},
            "theme_map": {"test": {"image_path": ["still.bmp"]}},
        }
        (root / "session.json").write_text(json.dumps(session), encoding="utf-8")
        (root / "system.json").write_text(json.dumps({
            "format": "trance-system", "format_version": 1, "windowed": True,
        }), encoding="utf-8")
        replies = Queue()
        with (root / "stderr.log").open("w", encoding="utf-8") as stderr:
            proc = subprocess.Popen(
                [str(exe), "--mcp", "--hidden", "--muted",
                 str(root / "session.json"), str(root / "system.json")],
                cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr,
                text=True, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))

            def reader():
                for line in proc.stdout:
                    replies.put(json.loads(line))

            Thread(target=reader, daemon=True).start()
            identifier = 0

            def request(method, params):
                nonlocal identifier
                identifier += 1
                proc.stdin.write(json.dumps({"jsonrpc": "2.0", "id": identifier,
                                             "method": method, "params": params}) + "\n")
                proc.stdin.flush()
                try:
                    reply = replies.get(timeout=20)
                except Empty:
                    raise AssertionError("MCP response timed out") from None
                assert reply.get("id") == identifier and "error" not in reply, reply
                assert not reply.get("result", {}).get("isError"), reply
                return reply

            def call(name, arguments=None):
                return request("tools/call", {"name": name, "arguments": arguments or {}})

            def sample(label, count):
                means = []
                for index in range(count):
                    target = root / f"{label}-{index}.png"
                    call("screenshot", {"file": str(target)})
                    deadline = time.monotonic() + 10
                    while True:
                        try:
                            with Image.open(target) as image:
                                image.load()
                                width, height = image.size
                                patch = image.convert("RGB").crop((width//3, height//3,
                                                                   width*2//3, height*2//3))
                                means.append(min(ImageStat.Stat(patch).mean))
                            break
                        except OSError:
                            assert time.monotonic() < deadline, "complete screenshot was not produced"
                            time.sleep(.02)
                    time.sleep(.07)
                assert min(means) > 150, f"{label}: dark frame; means={means}"
                return means

            try:
                request("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                       "clientInfo": {"name": "boundary-qa", "version": "1"}})
                call("load_pattern_source", {"source": json.dumps(source)})
                call("show")
                time.sleep(1)
                short = sample("one-frame", 6)
                # Restore the sole configured built-in through the normal selector.
                call("unload_pattern")
                time.sleep(.2)
                switched = sample("one-frame-lottery", 16)
                call("visual", {"name": "flash_text"})
                time.sleep(.2)
                flash = sample("flash-text", 12)
                print(f"QA OK: same-root repeat, root changes and Flash Text remain opaque; "
                      f"minimum channel means {min(short):.1f}, {min(switched):.1f}, {min(flash):.1f}")
            finally:
                proc.stdin.close()
                try:
                    proc.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=5)
                proc.stdout.close()
        assert proc.returncode == 0, f"player exit code {proc.returncode}"
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
