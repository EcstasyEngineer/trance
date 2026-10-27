"""Local, non-playing V4 authoring workbench and authoritative compiler lint.

Run: python tools/authoring_server.py --port 8766
The service never connects to or changes a running Trance player.
"""
from __future__ import annotations

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import re
import subprocess
import tempfile
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "docs" / "exploration" / "authoring"
MAX_BODY = 256 * 1024


def builtins():
    return builtins_v4()


def builtins_v4():
    text = (ROOT / "src/trance/visual/builtin_patterns_v4.cpp").read_text(encoding="utf-8")
    names = ["accelerate", "slow_flash", "sub_text", "flash_text", "parallel", "super_parallel", "animation", "super_fast"]
    out = []
    for index, source in re.findall(r'case\s+(\d+):\s+return\s+R"v4\((.*?)\)v4";', text, re.S):
        n = int(index)
        ast = json.loads(source)
        out.append({"id": n, "key": names[n - 1], "name": names[n - 1], "version": 4,
                    "sourcePatternName": ast["root"].get("id", names[n - 1]),
                    "sourcePath": "src/trance/visual/builtin_patterns_v4.cpp", "source": source,
                    "ast": ast})
    if len(out) != 8:
        raise RuntimeError("Expected exactly eight native V4 builtins")
    return out


def validate(source, executable, locked=0):
    if not isinstance(source, str) or len(source.encode("utf-8")) > MAX_BODY:
        raise ValueError("source must be a string of at most 256 KiB")
    if not isinstance(locked, int) or isinstance(locked, bool) or not 0 <= locked <= 3600:
        raise ValueError("lockedPeriodFrames must be an integer from 0 to 3600")
    if source.lstrip().startswith("{"):
        try:
            json.loads(source)
        except (ValueError, TypeError):
            return {"ok": False, "returncode": None, "stdout": "", "stderr": "",
                    "diagnostics": ["Invalid V4 JSON; source was not sent to the compiler."]}
    else:
        return {"ok": False, "returncode": None, "stdout": "", "stderr": "",
                "diagnostics": ["V3 text grammars are unsupported; provide a V4 JSON AST."]}
    if not executable.is_file():
        return {"ok": False, "returncode": None, "stdout": "", "stderr": "",
                "diagnostics": ["Release trance.exe is missing; build the supported windows-release preset."]}
    with tempfile.TemporaryDirectory(prefix="trance-authoring-") as td:
        directory = Path(td)
        pattern = directory / "authoring.pattern"
        pattern.write_text(source, encoding="utf-8")
        argv = [str(executable), "--lint"]
        if locked:
            # Use an isolated normal session to give --lint the actual Director
            # beat context. No source rewriting or fake acceptance of beat syntax.
            session = {"format": "trance-session", "format_version": 1,
                       "program_map": {"authoring": {
                           "global_fps": 60,
                           "entrainment": {"layer": [{"pulse_hz": 60 / locked, "amplitude_db": 0}]},
                           "custom_visual_pattern": [{"name": "authoring", "file": "authoring.pattern", "enabled": True}]
                       }}}
            path = directory / "authoring.json"
            path.write_text(json.dumps(session), encoding="utf-8")
            argv.append(str(path))
        else:
            argv.append("--pattern=" + str(pattern))
        try:
            result = subprocess.run(argv, cwd=directory, capture_output=True, text=True,
                                    encoding="utf-8", errors="replace", timeout=20,
                                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        except subprocess.TimeoutExpired:
            return {"ok": False, "returncode": None, "stdout": "", "stderr": "",
                    "diagnostics": ["Compiler lint exceeded the 20-second limit."]}
    stdout = result.stdout.replace(str(directory), "<temporary>")[-65536:]
    stderr = result.stderr.replace(str(directory), "<temporary>")[-65536:]
    diagnostics = [line for line in (stdout + "\n" + stderr).splitlines()
                   if re.search(r"FAIL|WARN|error|warning", line, re.I)]
    return {"ok": result.returncode == 0, "returncode": result.returncode,
            "stdout": stdout, "stderr": stderr, "diagnostics": diagnostics,
            "lockedPeriodFrames": locked,
            "coverage": "Product parser, compiler and three no-op render samples; no playback, decoded-media or exhaustive expression validation."}


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, executable=None, **kwargs):
        self.executable = executable
        super().__init__(*args, directory=str(WEB), **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        super().end_headers()

    def reply(self, status, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/api/builtins/v4":
            self.reply(200, {"builtins": builtins_v4()})
        elif path == "/api/builtins":
            self.reply(200, {"builtins": builtins()})
        elif path == "/api/health":
            self.reply(200, {"ok": True, "compilerAvailable": self.executable.is_file()})
        elif path.startswith("/api/"):
            self.reply(404, {"error": "Unknown endpoint"})
        else:
            # SimpleHTTPServer confines .. segments; resolved symlinks get an
            # additional check so arbitrary local files cannot be served.
            resolved = Path(self.translate_path(self.path)).resolve()
            if not resolved.is_relative_to(WEB.resolve()):
                self.reply(403, {"error": "Outside authoring directory"})
                return
            super().do_GET()

    def do_POST(self):
        if urlsplit(self.path).path != "/api/validate":
            self.reply(404, {"error": "Unknown endpoint"})
            return
        # No CORS; reject cross-origin browser submissions and DNS-rebound hosts.
        host = self.headers.get("Host", "")
        if host.split(":")[0] not in {"127.0.0.1", "localhost"}:
            self.reply(403, {"error": "Loopback Host required"})
            return
        origin = self.headers.get("Origin")
        if origin and origin not in {"http://" + host}:
            self.reply(403, {"error": "Same origin required"})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= MAX_BODY:
                self.reply(413, {"error": "Body limit is 256 KiB"})
                return
            payload = json.loads(self.rfile.read(size))
            if not isinstance(payload, dict):
                raise ValueError("Expected a JSON object")
            self.reply(200, validate(payload.get("source"), self.executable,
                                     payload.get("lockedPeriodFrames", 0)))
        except (ValueError, TypeError, UnicodeError) as error:
            self.reply(400, {"error": str(error)})
        except OSError as error:
            self.reply(500, {"error": "Cannot run compiler: " + type(error).__name__})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--exe", type=Path, default=ROOT / "build/windows-msvc/Release/trance.exe")
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, executable=args.exe.resolve()))
    server.daemon_threads = True
    print(f"Authoring workbench: http://127.0.0.1:{server.server_port}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
