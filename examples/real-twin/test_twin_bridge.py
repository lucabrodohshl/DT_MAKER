#!/usr/bin/env python3
"""Self-test of twin_bridge.py against a fake Studio (no product processes needed).

    python3 examples/real-twin/test_twin_bridge.py      (also run by ctest: label "unit")
"""
import io
import json
import os
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import twin_bridge as tb  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


class FakeStudio:
    """Answers the three routes the bridge uses and records every POST."""

    def __init__(self, t0_ticks=5000, reject=()):
        self.posts = []
        self.t0 = t0_ticks
        self.reject = set(reject)
        fake = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):  # quiet
                pass

            def _send(self, status, body):
                data = json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                if self.path == "/api/v1/twins/pump-p102-dt":
                    return self._send(200, {"id": "pump-p102-dt", "ticksPerUnit": 1000})
                if self.path == "/api/v1/twins/pump-p102-dt/runtime/state":
                    return self._send(200, {"locations": ["STOPPED"], "time": {"ticks": fake.t0, "text": "5"}})
                return self._send(404, {"error": {"code": "not_found", "message": "no route", "context": []}})

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                route = self.path.rsplit("/", 1)[-1]
                fake.posts.append((route, body))
                if route == "pt-event":
                    ok = body["label"] not in fake.reject
                    reply = {"accepted": ok, "label": "dt_" + body["label"], "pt_label": body["label"]}
                    if ok:
                        reply.update({"from": "A", "to": "B"})
                    else:
                        reply["error"] = {"code": "incompatible_observation", "message": "not enabled", "context": []}
                    return self._send(200, reply)
                return self._send(200, {"accepted": True})

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def close(self):
        self.server.shutdown()


def config(**overrides):
    with open(os.path.join(HERE, "pump-p102.bridge.json"), encoding="utf-8") as f:
        raw = json.load(f)
    raw.update(overrides)
    path = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False).name
    with open(path, "w", encoding="utf-8") as f:
        json.dump(raw, f)
    return tb.Config.load(path)


class Translate(unittest.TestCase):
    def test_maps_fields_events_and_reports_unknown_codes(self):
        cfg = config()
        clock = tb.Clock(5000, 1000)
        out = tb.translate({"ts": 1000, "TT101_degC": 71.5, "running": True, "event": "START", "other": 1}, cfg, clock)
        self.assertEqual(out.event, "start_cmd!")
        self.assertEqual(out.telemetry, {"bearing_temp": 71.5, "motor_running": True})
        self.assertEqual(out.ticks, 5000)
        unknown = tb.translate({"ts": 2000, "event": "SELF_DESTRUCT"}, cfg, clock)
        self.assertIsNone(unknown.event)
        self.assertEqual(unknown.unmapped_event, "SELF_DESTRUCT")
        self.assertEqual(unknown.ticks, 6000)

    def test_logical_time_follows_source_timestamps_and_never_goes_back(self):
        clock = tb.Clock(5000, 1000)
        self.assertEqual(clock.from_source(100.0), (5000, False))
        self.assertEqual(clock.from_source(102.5), (7500, False))
        self.assertEqual(clock.from_source(101.0), (7500, True))  # late: clamped
        self.assertEqual(clock.from_source(103.0), (8000, False))

    def test_wall_clock_without_timestamps(self):
        now = [10.0]
        clock = tb.Clock(0, 1000, now=lambda: now[0])
        now[0] = 12.25
        self.assertEqual(clock.from_wall(), (2250, False))

    def test_config_rejects_labels_that_are_not_pt_send_labels(self):
        with self.assertRaises(ValueError):
            config(events={"field": "event", "map": {"START": "start_cmd"}})


class BridgeAgainstFakeStudio(unittest.TestCase):
    def setUp(self):
        self.studio = FakeStudio(t0_ticks=5000, reject={"halted!"})
        self.cfg = config(studio=self.studio.url, heartbeatSeconds=0)
        self.lines = []
        self.bridge = tb.Bridge(self.cfg, tb.Studio(self.cfg.studio, self.cfg.instance), log=self.lines.append)
        self.bridge.connect()

    def tearDown(self):
        self.studio.close()

    def test_replays_a_log_relative_to_the_twins_time(self):
        log = io.StringIO("\n".join([
            json.dumps({"ts": 1000, "running": False}),
            json.dumps({"ts": 6000, "running": True, "event": "START"}),
            "this line is not json",
            json.dumps({"ts": 9000, "event": "STOPPED"}),
        ]))
        for message in tb.json_lines(log, self.lines.append):
            self.bridge.handle(message)
        routes = [(r, b) for r, b in self.studio.posts]
        self.assertEqual(routes[0], ("telemetry", {"at": 5000, "motor_running": False}))
        self.assertEqual(routes[1], ("telemetry", {"at": 10000, "motor_running": True}))
        self.assertEqual(routes[2], ("pt-event", {"label": "start_cmd!", "ticks": 10000}))
        self.assertEqual(routes[3], ("pt-event", {"label": "halted!", "ticks": 13000}))
        self.assertEqual(self.bridge.sent_events, 2)
        self.assertEqual(self.bridge.rejected_events, 1)
        self.assertTrue(any("not JSON" in line for line in self.lines))
        self.assertTrue(any("REJECTED" in line for line in self.lines))

    def test_dry_run_sends_nothing(self):
        dry = tb.Bridge(self.cfg, tb.Studio(self.cfg.studio, self.cfg.instance), dry_run=True, log=self.lines.append)
        dry.connect()
        dry.handle({"ts": 1, "event": "START", "TT101_degC": 70})
        self.assertEqual(self.studio.posts, [])
        self.assertTrue(any("would POST pt-event" in line for line in self.lines))


class Heartbeat(unittest.TestCase):
    def test_wall_clock_heartbeats_advance_the_twin(self):
        studio = FakeStudio(t0_ticks=0)
        cfg = config(studio=studio.url, heartbeatSeconds=0.05)
        cfg.timestamp_field = None  # wall-clock mode
        bridge = tb.Bridge(cfg, tb.Studio(cfg.studio, cfg.instance), log=lambda s: None)
        bridge.connect()
        t = threading.Thread(target=bridge.heartbeat_loop, daemon=True)
        t.start()
        threading.Event().wait(0.3)
        bridge.stopped.set()
        t.join(1)
        advances = [b for r, b in studio.posts if r == "advance"]
        studio.close()
        self.assertGreaterEqual(len(advances), 2)
        self.assertEqual(sorted(a["ticks"] for a in advances), [a["ticks"] for a in advances])


if __name__ == "__main__":
    unittest.main(verbosity=1)
