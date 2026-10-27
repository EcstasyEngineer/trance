"""Run: python tests/authoring_server_test.py (no playback or live user state)."""
import importlib.util
import json
from pathlib import Path
import threading
import unittest
from functools import partial
from urllib.error import HTTPError
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("authoring_server", ROOT / "tools/authoring_server.py")
server_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server_module)
EXE = ROOT / "build/windows-msvc/Release/trance.exe"


class AuthoringServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = server_module.ThreadingHTTPServer(("127.0.0.1", 0), partial(server_module.Handler, executable=EXE))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = "http://127.0.0.1:" + str(cls.server.server_port)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(2)

    def get(self, endpoint):
        with urlopen(self.base + endpoint, timeout=30) as response:
            return json.load(response)

    def post(self, payload, headers=None):
        request = Request(self.base + "/api/validate", data=json.dumps(payload).encode(),
                          headers={"Content-Type": "application/json", **(headers or {})})
        with urlopen(request, timeout=30) as response:
            return json.load(response)

    def test_builtins_match_production(self):
        actual = self.get("/api/builtins")["builtins"]
        self.assertEqual(len(actual), 8)
        self.assertEqual(actual[4]["key"], "parallel")
        self.assertEqual(actual[4]["sourcePatternName"], "parallel")
        cached = json.loads((server_module.WEB / "builtins-v4.json").read_text(encoding="utf-8"))["builtins"]
        self.assertEqual(actual, cached)

    def test_native_builtins_match_production(self):
        actual = self.get("/api/builtins/v4")["builtins"]
        self.assertEqual(len(actual), 8)
        cached = json.loads((server_module.WEB / "builtins-v4.json").read_text(encoding="utf-8"))["builtins"]
        self.assertEqual(actual, cached)
        for builtin in actual:
            self.assertEqual(builtin["version"], 4)
            self.assertEqual(json.loads(builtin["source"]), builtin["ast"])
            self.assertEqual(builtin["ast"]["version"], 4)

    @unittest.skipUnless(EXE.is_file(), "Release executable missing")
    def test_native_v4_compiler(self):
        source = json.dumps({"version": 4, "root": {"kind": "parallel", "duration": 16,
            "children": [{"node": {"op": "image", "source": "either", "animation": {"every": 1}}}]}})
        result = self.post({"source": source})
        self.assertTrue(result["ok"], result)
        self.assertIn("LINT OK", result["stdout"])
        self.assertFalse(self.post({"source": '{"version":4,"root":'})["ok"])

    def test_health(self):
        self.assertTrue(self.get("/api/health")["ok"])

    def test_reject_cross_origin(self):
        with self.assertRaises(HTTPError) as caught:
            self.post({"source": "pattern p for 8f {}"}, {"Origin": "https://example.com"})
        self.assertEqual(caught.exception.code, 403)

    def test_invalid_json_payload(self):
        with self.assertRaises(HTTPError) as caught:
            self.post({"source": 42})
        self.assertEqual(caught.exception.code, 400)

    def test_extra_root_rejected(self):
        result = self.post({"source": "pattern p for 8f {} pattern q for 8f {}"})
        self.assertFalse(result["ok"])
        self.assertIn("V3", result["diagnostics"][0])

    @unittest.skipUnless(EXE.is_file(), "Release executable missing")
    def test_real_lint(self):
        result = self.post({"source": json.dumps({"version":4,"root":{"kind":"parallel","duration":64,"cadence":8,"children":[{"node":{"op":"image","source":"primary"}}]}})})
        self.assertTrue(result["ok"], result)
        self.assertIn("LINT OK", result["stdout"])
        result = self.post({"source": "pattern p for 8f { made_up 42 }"})
        self.assertFalse(result["ok"])

    @unittest.skipUnless(EXE.is_file(), "Release executable missing")
    def test_beat_context(self):
        source = "pattern p for beats 8 { every locked { image primary } }"
        self.assertFalse(self.post({"source": source})["ok"])
        self.assertFalse(self.post({"source": source, "lockedPeriodFrames": 8})["ok"])


if __name__ == "__main__":
    unittest.main()
