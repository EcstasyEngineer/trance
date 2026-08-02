"""Command-channel end-to-end QA against a LIVE trance.exe (issue #29).

Launches trance.exe windowed with --command_port open, drives every protocol verb
over a real TCP connection (docs/spec-mcp-ambient-daemon.md sec 3/4), checks each
reply's ok/err framing, and shuts the process down. This replaces the deleted C++
command_protocol_test at the only level that matters: the real socket, the real
dispatch on the render thread, the real replies.

Needs a desktop session and a real GPU (the app renders while being driven), so it
is a hands-on QA harness -- deliberately NOT registered with ctest and never run by
CI. Run it on a machine you are physically at; the window will appear and steal
focus while it runs.

Usage:
  python tests/qa_command_channel.py build/windows-msvc/Release/trance.exe \
      [--session path/to/some.session.json] [--port 47737]

Exit code 0 iff every verb produced a well-formed expected reply.
"""

import argparse
import socket
import subprocess
import sys
import time

# (line to send, expect_ok) -- expect_ok=False marks lines that MUST come back "err ...";
# the malformed cases are part of the contract (spec sec 3: exactly one reply per line,
# unknown/malformed lines reply err, never silence).
VERBS = [
    ("status", True),
    ("pause", True),
    ("resume", True),
    ("overlay on", True),
    ("overlay opacity 0.5", True),
    ("overlay opacity 2.0", True),   # clamped to 1, still ok (spec sec 4)
    ("overlay off", True),
    ("ui on", True),
    ("ui off", True),
    ("hide", True),
    ("hide", True),                  # idempotent: hide-while-hidden is an ok no-op
    ("show", True),
    ("show", True),                  # idempotent: show-while-shown is an ok no-op
    # Audio surface (spec sec 4 "Audio"). This instance plays a throwaway session, so
    # bed edits are fair game; they only ever touch the in-memory program.
    ("mute on", True),
    ("mute off", True),
    ("bed off", True),
    ("bed off", True),               # idempotent
    ("bed on", True),                # restores the default bed
    ("bed on", True),                # idempotent
    ("bed master -30", True),
    ("bed master -100", True),       # clamped to -60, still ok
    ("bed layer 0 carrier 300", True),
    ("bed layer 0 binaural 4", True),
    ("bed layer 0 pulse 6", True),
    ("bed layer 0 level -3", True),
    ("bed layer add", True),
    ("bed layer remove 2", True),    # removes the layer just added (default bed has 2)
    ("", False),                     # empty line -> err, not silence
    ("frobnicate", False),           # unknown verb -> err
    ("overlay opacity potato", False),  # non-numeric -> err
    ("overlay", False),              # wrong arg count -> err
    ("mute", False),                 # missing arg -> err
    ("bed", False),                  # missing subverb -> err
    ("bed master potato", False),    # non-numeric -> err
    ("bed layer 99 carrier 300", False),  # bad index -> err
    ("bed layer 0 bogus 1", False),  # unknown field -> err
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", help="path to trance.exe")
    parser.add_argument("--session", default=None, help="session to play (default: app default)")
    parser.add_argument("--port", type=int, default=47737)
    parser.add_argument("--startup-wait", type=float, default=15.0,
                        help="seconds to keep retrying the first connect")
    args = parser.parse_args()

    cmd = [args.exe, f"--command_port={args.port}"]
    if args.session:
        cmd.append(args.session)
    print(f"launching: {' '.join(cmd)}")
    proc = subprocess.Popen(cmd)

    sock = None
    deadline = time.monotonic() + args.startup_wait
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            print(f"FAIL  app exited during startup (code {proc.returncode})")
            return 1
        try:
            sock = socket.create_connection(("127.0.0.1", args.port), timeout=2.0)
            break
        except OSError:
            time.sleep(0.5)
    if sock is None:
        print(f"FAIL  could not connect to 127.0.0.1:{args.port} within {args.startup_wait}s")
        proc.terminate()
        return 1

    failures = 0
    try:
        reader = sock.makefile("r", encoding="utf-8", newline="\n")
        sock.settimeout(5.0)
        for line, expect_ok in VERBS:
            sock.sendall((line + "\n").encode("utf-8"))
            reply = reader.readline().rstrip("\n")
            ok = reply == "ok" or reply.startswith("ok ")
            err = reply.startswith("err ")
            well_formed = ok or err
            passed = well_formed and (ok == expect_ok)
            tag = "  ok " if passed else "FAIL "
            print(f"{tag} {line!r:32} -> {reply!r}")
            if not passed:
                failures += 1
    finally:
        sock.close()
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("QA OK" if not failures else f"QA FAILED ({failures} verbs)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
