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
    ("bed layers", True),            # read-back (#60): the R the layers' CRUD was missing
    ("bed layer remove 2", True),    # removes the layer just added (default bed has 2)
    # Content selection (#59). The theme verbs are driven separately below, against a theme
    # name read out of `status` -- there is no name to hardcode that every session has.
    ("visuals", True),
    ("themes", True),
    ("visual accelerate", True),
    ("visual super_fast", True),
    ("load pattern source pattern inline for 128f { every 32f { image primary } }", True),
    ("unload pattern", True),
    ("unload pattern", True),        # idempotent: releasing nothing is an ok no-op
    ("text pin one,two,a whole phrase", True),
    ("text unpin", True),
    ("text unpin", True),            # idempotent
    ("theme unpin", True),           # idempotent with no pin set
    ("", False),                     # empty line -> err, not silence
    ("frobnicate", False),           # unknown verb -> err
    ("overlay opacity potato", False),  # non-numeric -> err
    ("overlay", False),              # wrong arg count -> err
    ("mute", False),                 # missing arg -> err
    ("bed", False),                  # missing subverb -> err
    ("bed master potato", False),    # non-numeric -> err
    ("bed layer 99 carrier 300", False),  # bad index -> err
    ("bed layer 0 bogus 1", False),  # unknown field -> err
    ("visual not_a_visual_zzz", False),   # unknown visual name -> err (lists the valid ones)
    ("visual", False),               # missing arg -> err
    ("theme pin not_a_theme_zzz", False),  # unknown theme -> err
    ("theme", False),                # missing subverb -> err
    ("text", False),                 # missing subverb -> err
    ("unload", False),               # missing subverb -> err
    ("load pattern", False),         # missing path -> err
    ("load pattern source pattern broken for {", False),  # parse error -> err diagnostic
]


XR_STATES = ("off", "unattached", "attached", "attached-idle")


def check_status_xr(reply):
    """`status` must carry xr=<state> (spec-xr-unified.md phase 4).

    It is the only external view of the hot-attach machine, so every QA scenario that
    kills or starts a runtime mid-run is watched through this field; returns a complaint
    string, or None if the field is there and parses.
    """
    fields = dict(part.split("=", 1) for part in reply.split() if "=" in part)
    if "xr" not in fields:
        return "no xr= field in the status reply"
    if fields["xr"] not in XR_STATES:
        return f"xr={fields['xr']!r}, expected one of {XR_STATES}"
    return None


def live_theme_name(status_reply):
    """The primary live theme out of a `status` reply, or None.

    `themes=prev|primary|alternate|next`; slot 1 is the primary lane. Read rather than
    hardcoded because the theme names belong to whatever session is being played -- which is
    exactly the discoverability problem `themes`/`theme pin` exist to solve (#59).
    """
    fields = dict(part.split("=", 1) for part in status_reply.split() if "=" in part)
    slots = fields.get("themes", "").split("|")
    if len(slots) < 2 or slots[1] in ("", "(empty)"):
        return None
    return slots[1]


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
        # One reply inspected for CONTENT rather than framing: status's xr= field. On a
        # machine with no VR software this is "unattached" (or "off" if probing was given
        # up on); with a headset it walks unattached -> attached-idle -> attached without
        # a restart, which is what makes the hot-attach scenarios observable at all.
        sock.sendall(b"status\n")
        status = reader.readline().rstrip("\n")
        problem = check_status_xr(status)
        tag = "  ok " if problem is None else "FAIL "
        print(f"{tag} {'status xr= field':32} -> {problem or status}")
        if problem is not None:
            failures += 1

        # Theme pin round trip, against a name read out of that status line. Content, not
        # framing: a pin that replies ok while `themes` still shows nothing pinned is exactly
        # the failure this verb pair could plausibly have.
        theme = live_theme_name(status)
        if theme is None:
            print(f"SKIP  {'theme pin round trip':32} -> no live theme in the status line")
        else:
            def ask(line):
                sock.sendall((line + "\n").encode("utf-8"))
                return reader.readline().rstrip("\n")

            checks = []
            pin_reply = ask(f"theme pin {theme}")
            checks.append((f"theme pin {theme}", pin_reply.startswith("ok "), pin_reply))
            listing = ask("themes")
            checks.append(("themes marks it pinned", f"{theme}:" in listing and "*" in listing,
                           listing[:120]))
            status_pinned = ask("status")
            checks.append((f"status themepin={theme}", f"themepin={theme}" in status_pinned,
                           status_pinned))
            unpin_reply = ask("theme unpin")
            checks.append(("theme unpin", unpin_reply.startswith("ok "), unpin_reply))
            status_unpinned = ask("status")
            checks.append(("status themepin=-", "themepin=-" in status_unpinned, status_unpinned))
            for label, passed, detail in checks:
                print(f"{'  ok ' if passed else 'FAIL '} {label!r:32} -> {detail!r}")
                if not passed:
                    failures += 1

        # Two simultaneous clients (#29): the reader thread multiplexes with WSAPoll, so both
        # connections must get their own replies rather than one starving or crossing wires.
        second = socket.create_connection(("127.0.0.1", args.port), timeout=5.0)
        try:
            second_reader = second.makefile("r", encoding="utf-8", newline="\n")
            second.sendall(b"status\n")
            sock.sendall(b"status\n")
            reply_b = second_reader.readline().rstrip("\n")
            reply_a = reader.readline().rstrip("\n")
            both = reply_a.startswith("ok ") and reply_b.startswith("ok ")
            print(f"{'  ok ' if both else 'FAIL '} {'two clients both replied':32} -> "
                  f"{reply_a[:40]!r} / {reply_b[:40]!r}")
            if not both:
                failures += 1
        finally:
            second.close()
    finally:
        sock.close()
        # Clean shutdown WITH A CLIENT STILL CONNECTED (#29): `sock` is closed just above but
        # the process must also survive the graceful path, so ask the OS to close the window
        # (WM_CLOSE -- the same event as the close button, i.e. what Escape does) and time it.
        # A hang here is the shutdown deadlock this check exists to catch; SIGTERM/kill is the
        # fallback so the harness never leaves a process behind.
        graceful = False
        if sys.platform == "win32":
            subprocess.run(["taskkill", "/PID", str(proc.pid)],
                           capture_output=True, check=False)
        else:
            proc.terminate()
        started = time.monotonic()
        try:
            proc.wait(timeout=15)
            graceful = True
        except subprocess.TimeoutExpired:
            proc.kill()
        elapsed = time.monotonic() - started
        if graceful:
            print(f"  ok  {'clean exit (window close)':32} -> {elapsed:.1f}s, code {proc.returncode}")
        else:
            print(f"FAIL  {'clean exit (window close)':32} -> still running after 15s; killed")
            failures += 1

    print("QA OK" if not failures else f"QA FAILED ({failures} checks)")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
