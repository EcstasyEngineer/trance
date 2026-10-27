"""Isolated, hidden, muted MCP integration check for recovered bed presets.

Usage: python tests/qa_bed_presets.py path/to/trance.exe
No user session is loaded and no playback window is shown.
"""
import argparse
import json
import pathlib
import queue
import subprocess
import tempfile
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=pathlib.Path)
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="trance-bed-qa-") as directory:
        root = pathlib.Path(directory)
        session = {
            "format": "trance-session", "format_version": 1,
            "first_playlist_item": "test",
            "playlist": {"test": {"standard": {"program": "test"}}},
            "program_map": {"test": {"global_fps": 60}},
        }
        (root / "session.json").write_text(json.dumps(session), encoding="utf-8")
        (root / "system.json").write_text(json.dumps({
            "format": "trance-system", "format_version": 1, "windowed": True,
        }), encoding="utf-8")
        with (root / "stderr.log").open("w", encoding="utf-8") as error_log:
            process = subprocess.Popen(
                [str(exe), "--mcp", "--hidden", "--muted", str(root / "session.json"),
                 str(root / "system.json")], cwd=root, stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=error_log, text=True,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            replies = queue.Queue()

            def read_replies():
                for line in process.stdout:
                    try:
                        replies.put(json.loads(line))
                    except json.JSONDecodeError:
                        replies.put({"invalid_stdout": line})

            reader = threading.Thread(target=read_replies, daemon=True)
            reader.start()
            identifier = 0

            def request(method, params):
                nonlocal identifier
                identifier += 1
                process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": identifier,
                                                "method": method, "params": params}) + "\n")
                process.stdin.flush()
                reply = replies.get(timeout=15)
                assert reply.get("id") == identifier and "result" in reply, reply
                return reply["result"]

            def call(name, arguments=None, error=False):
                result = request("tools/call", {"name": name, "arguments": arguments or {}})
                assert bool(result.get("isError")) == error, result
                return "\n".join(part.get("text", "") for part in result.get("content", []))

            try:
                request("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                       "clientInfo": {"name": "bed-preset-qa", "version": "1"}})
                tools = request("tools/list", {})
                assert {"bed_presets", "bed_preset"} <= {item["name"] for item in tools["tools"]}
                names = ["bimbo-drone", "reactor", "descent", "dreamy", "celestial", "warm",
                         "floating", "sacred"]
                listing = call("bed_presets")
                assert ",".join(names) in listing, listing
                call("bed_on")
                call("bed_master", {"db": -33})
                for name, count in zip(names, [2, 4, 3, 4, 4, 4, 4, 4]):
                    call("bed_preset", {"name": name})
                    layers = call("bed_layers")
                    assert f"layers={count} master_db=-33" in layers, layers
                assert "[0] carrier=180 binaural=4 pulse=6" in layers, layers
                assert "[3] carrier=450 binaural=2.5 pulse=2.6 level=-10" in layers, layers
                call("bed_preset", {"name": "not-a-preset"}, error=True)
                assert call("bed_layers") == layers, "unknown preset mutated existing bed"
                call("bed_off")
                absent = call("bed_layers")
                call("bed_preset", {"name": "not-a-preset"}, error=True)
                assert call("bed_layers") == absent, "unknown preset mutated absent bed"
                call("bed_preset", {"name": "bimbo-drone"})
                layers = call("bed_layers")
                assert "layers=2 master_db=-28(default)" in layers, layers
                assert "[1] carrier=60 binaural=3 pulse=3.25 level=-6" in layers, layers
            finally:
                process.stdin.close()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    raise AssertionError("MCP EOF shutdown timed out")
                reader.join(timeout=2)
                process.stdout.close()
            assert process.returncode == 0, process.returncode
            assert replies.empty(), "unexpected MCP stdout"
    print("QA OK: all eight presets, MCP discovery, exact readback, master preservation, "
          "unknown-name nonmutation, absent-bed application, hidden/muted clean exit")


if __name__ == "__main__":
    main()
