"""Focused independent server review; ephemeral loopback only, no playback.
Run: python docs/exploration/authoring/reviews/18-server.test.py
"""
import concurrent.futures
from functools import partial
import http.client
import importlib.util
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[4]
spec = importlib.util.spec_from_file_location('review18_server', ROOT / 'tools/authoring_server.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
EXE = ROOT / 'build/windows-msvc/Release/trance.exe'

class Review18(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = m.ThreadingHTTPServer(('127.0.0.1', 0), partial(m.Handler, executable=EXE))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.host = '127.0.0.1:' + str(cls.server.server_port)

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(2)

    def request(self, method, path, body=None, headers=None):
        c = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=30)
        try:
            c.request(method, path, body=body, headers=headers or {})
            r = c.getresponse()
            return r.status, r.read()
        finally:
            c.close()

    def post(self, payload, headers=None):
        status, data = self.request('POST', '/api/validate', json.dumps(payload).encode(), headers)
        return status, json.loads(data)

    @unittest.skipUnless(EXE.is_file(), 'Release executable missing')
    def test_all_actual_builtin_sources_compile(self):
        sources = m.builtins()
        self.assertEqual([p['id'] for p in sources], list(range(1, 9)))
        self.assertEqual(sources, json.loads((m.WEB / 'builtins.json').read_text())['builtins'])
        for p in sources:
            with self.subTest(key=p['key']):
                status, result = self.post({'source': p['source']})
                self.assertEqual(status, 200)
                self.assertTrue(result['ok'], result)
                self.assertIn('LINT OK', result['stdout'])

    @unittest.skipUnless(EXE.is_file(), 'Release executable missing')
    def test_exact_beat_frame_counts(self):
        source = 'pattern p for beats 8 { every locked { image primary } }'
        for locked in (1, 7, 17, 59, 3600):
            with self.subTest(locked=locked):
                status, r = self.post({'source': source, 'lockedPeriodFrames': locked})
                self.assertEqual(status, 200)
                self.assertTrue(r['ok'], r)
                self.assertIn("program 'authoring' pattern 'authoring' (" + str(8 * locked) + 'f)', r['stdout'])
        # Exhaustive arithmetic check of production's float pulse field + rounding.
        for locked in range(1, 3601):
            pulse = struct.unpack('f', struct.pack('f', 60 / locked))[0]
            self.assertEqual(int(60 / pulse + 0.5), locked)

    def test_validation_boundaries_and_origins(self):
        for bad in (True, -1, 3601, 1.5, '8', None):
            self.assertEqual(self.post({'source': 'pattern p for 8f {}', 'lockedPeriodFrames': bad})[0], 400)
        for headers in ({'Host': 'example.invalid'}, {'Origin': 'http://example.invalid'},
                        {'Origin': 'http://127.0.0.1:1'}):
            self.assertEqual(self.post({'source': 'pattern p for 8f {}'}, headers)[0], 403)
        self.assertEqual(self.post({'source': 'pattern p for 8f {}'}, {'Origin': 'http://' + self.host})[0], 200)
        self.assertEqual(self.request('POST', '/api/validate', b'x' * (m.MAX_BODY + 1))[0], 413)
        self.assertEqual(self.request('POST', '/api/validate', b'')[0], 413)
        self.assertEqual(self.request('POST', '/api/validate', b'{')[0], 400)
        self.assertEqual(self.post([])[0], 400)
        self.assertEqual(self.request('GET', '/../tools/authoring_server.py')[0], 404)
        self.assertEqual(self.request('GET', '/%2e%2e/%2e%2e/tools/authoring_server.py')[0], 404)

    def test_temp_cleanup_and_no_shell(self):
        seen = []
        def run(argv, **kw):
            td = kw['cwd']
            self.assertTrue(td.is_dir())
            self.assertEqual(argv[:2], [str(EXE), '--lint'])
            self.assertNotIn('shell', kw)
            self.assertEqual(kw['timeout'], 20)
            self.assertEqual((td / 'authoring.pattern').read_text(), 'pattern p for 8f {}')
            seen.append(td)
            return subprocess.CompletedProcess(argv, 0, str(td), '')
        with patch.object(m.subprocess, 'run', side_effect=run):
            for locked in (0, 7):
                r = m.validate('pattern p for 8f {}', EXE, locked)
                self.assertEqual(r['stdout'], '<temporary>')
        self.assertEqual(len(set(seen)), 2)
        self.assertTrue(all(not p.exists() for p in seen))
        for failure in (subprocess.TimeoutExpired('compiler', 20), OSError('synthetic')):
            seen.clear()
            def fail(argv, **kw):
                seen.append(kw['cwd'])
                raise failure
            with patch.object(m.subprocess, 'run', side_effect=fail):
                try:
                    m.validate('pattern p for 8f {}', EXE)
                except OSError:
                    pass
            self.assertTrue(all(not p.exists() for p in seen))

    def test_parallel_body_reads_have_no_timeout(self):
        # Three connections only; observe actual accepted handler socket settings.
        accepted = []
        original = m.Handler.do_POST
        def observe(handler):
            accepted.append(handler.connection.gettimeout())
            original(handler)
        sockets = []
        with patch.object(m.Handler, 'do_POST', observe):
            try:
                for _ in range(3):
                    s = socket.create_connection(('127.0.0.1', self.server.server_port), timeout=2)
                    s.sendall(('POST /api/validate HTTP/1.1\r\nHost: ' + self.host + '\r\nContent-Length: 100\r\n\r\n{').encode())
                    sockets.append(s)
                deadline = time.monotonic() + 2
                while len(accepted) < 3 and time.monotonic() < deadline:
                    time.sleep(.01)
                self.assertEqual(accepted, [None, None, None])
                self.assertEqual(self.request('GET', '/api/health')[0], 200)
            finally:
                # Complete valid JSON so handlers return cleanly before shutdown.
                for s in sockets:
                    s.sendall(b'"source":null}' + b' ' * 85)
                    s.recv(4096)
                    s.close()

    def test_static_symlink_escape(self):
        original_web = m.WEB
        with tempfile.TemporaryDirectory(prefix='trance-review18-') as td:
            root = Path(td)
            web = root / 'web'
            web.mkdir()
            outside = root / 'outside.txt'
            outside.write_text('review18 outside sentinel')
            try:
                (web / 'escape.txt').symlink_to(outside)
            except OSError as error:
                self.skipTest('Local symlink creation unavailable: ' + type(error).__name__)
            try:
                m.WEB = web
                self.assertEqual(self.request('GET', '/escape.txt')[0], 403)
                # Records inherited HEAD behavior: metadata only, no response body.
                self.assertEqual(self.request('HEAD', '/escape.txt')[0], 200)
            finally:
                m.WEB = original_web

if __name__ == '__main__':
    unittest.main(verbosity=2)
