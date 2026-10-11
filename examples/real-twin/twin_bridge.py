#!/usr/bin/env python3
"""twin_bridge — feed a deployed twin instance from a real system.

Reads messages from the plant (JSON objects: one per line on stdin or in a file, or
fetched from a REST endpoint), maps them to the instance's contract and sends them
to the instance's runtime through Verified Twin Studio:

  * PT events  -> POST /api/v1/twins/<instance>/runtime/pt-event  {"label": "<PT label>", "ticks": n}
  * telemetry  -> POST /api/v1/twins/<instance>/runtime/telemetry {"at": n, "<field>": value, ...}
  * heartbeats -> POST /api/v1/twins/<instance>/runtime/advance   {"ticks": n}

The bridge decides nothing about behaviour. It only translates names and time. The
twin's semantic kernel accepts or rejects every event (a rejection is a recorded
conformance violation), and every decision is written to the instance's ledger.

Logical time: the instance runs in logical time, in ticks (ticksPerUnit ticks per
model time unit, normally 1000 per second). At start-up the bridge reads the twin's
current logical time T0. A message stamped `ts` (when the config names a timestamp
field) is sent at T0 + (ts - ts_first); without timestamps, the wall clock since
start-up is used. Time never goes backwards: a late message is sent at the last time
used, and the bridge says so. Heartbeats advance the twin's time while the plant is
quiet, so a deadline of the verified model (an invariant) that the plant misses is
detected and recorded when it is missed, not only at the next event.

Sources (pick one):
  --stdin                 JSON lines on standard input, e.g.
                          mosquitto_sub -h plc-gateway -t 'plant/line4/pump/#' | twin_bridge.py --config c.json --stdin
  --file LOG.jsonl        a recorded log; --speed X replays its timestamps X times faster (0 = no waiting)
  --poll URL              GET a JSON object (or array of objects) every --interval seconds

Config (JSON):
  {
    "studio": "http://127.0.0.1:8080",
    "instance": "pump-p102-dt",
    "timestamp": {"field": "ts", "unit": "ms"},          # optional; unit "ms" or "s" (epoch or any origin)
    "telemetry": {"temp_C": "bearing_temp", "rpm": "shaft_speed"},   # source field -> telemetry field
    "events": {"field": "event", "map": {"START": "start_cmd!", "ALARM": "alarm_raise!"}},
    "heartbeatSeconds": 1.0                                # 0 disables heartbeats
  }

Only the Python standard library is used. Exit with Ctrl-C.
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from typing import Callable, Iterable, Optional


# ----------------------------------------------------------------------------- transport

class Studio:
    """Minimal JSON client for the Studio API (only the twin's runtime routes)."""

    def __init__(self, base: str, instance: str, timeout: float = 10.0):
        self.base = base.rstrip("/") + "/api/v1"
        self.instance = instance
        self.timeout = timeout

    def request(self, method: str, path: str, body: Optional[dict] = None) -> tuple[int, object]:
        data = None if body is None else json.dumps(body).encode()
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json", "X-Twin-Actor": "twin-bridge"})
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as r:
                raw = r.read()
                return r.status, json.loads(raw) if raw else None
        except urllib.error.HTTPError as e:
            raw = e.read()
            try:
                return e.code, json.loads(raw) if raw else None
            except json.JSONDecodeError:
                return e.code, raw.decode(errors="replace")

    def runtime(self, method: str, route: str, body: Optional[dict] = None) -> tuple[int, object]:
        return self.request(method, f"/twins/{self.instance}/runtime/{route}", body)


# ----------------------------------------------------------------------------- mapping

@dataclass
class Config:
    studio: str
    instance: str
    telemetry: dict[str, str] = field(default_factory=dict)
    event_field: Optional[str] = None
    event_map: dict[str, str] = field(default_factory=dict)
    timestamp_field: Optional[str] = None
    timestamp_unit: str = "ms"
    heartbeat: float = 1.0

    @staticmethod
    def load(path: str) -> "Config":
        with open(path, encoding="utf-8") as f:
            raw = json.load(f)
        ev = raw.get("events") or {}
        ts = raw.get("timestamp") or {}
        unit = ts.get("unit", "ms")
        if unit not in ("ms", "s"):
            raise ValueError("timestamp.unit must be \"ms\" or \"s\"")
        for label in (ev.get("map") or {}).values():
            if not isinstance(label, str) or not label.endswith("!"):
                raise ValueError(f"event map values are PT labels such as \"start_cmd!\" (got {label!r})")
        return Config(studio=raw.get("studio", "http://127.0.0.1:8080"), instance=raw["instance"],
                      telemetry=dict(raw.get("telemetry") or {}), event_field=ev.get("field"),
                      event_map=dict(ev.get("map") or {}), timestamp_field=ts.get("field"), timestamp_unit=unit,
                      heartbeat=float(raw.get("heartbeatSeconds", 1.0)))


class Clock:
    """Logical time for the twin: monotonic, in ticks, anchored at the twin's time at start-up."""

    def __init__(self, t0_ticks: int, ticks_per_unit: int, now: Callable[[], float] = time.monotonic):
        self.t0 = t0_ticks
        self.r = ticks_per_unit
        self.now = now
        self.wall0 = now()
        self.source0: Optional[float] = None  # first source timestamp, in seconds
        self.last = t0_ticks

    def from_source(self, seconds: float) -> tuple[int, bool]:
        """Ticks for a source timestamp; second value True if the message was late (clamped)."""
        if self.source0 is None:
            self.source0 = seconds
        return self._monotonic(self.t0 + round((seconds - self.source0) * self.r))

    def from_wall(self) -> tuple[int, bool]:
        return self._monotonic(self.t0 + round((self.now() - self.wall0) * self.r))

    def _monotonic(self, ticks: int) -> tuple[int, bool]:
        if ticks < self.last:
            return self.last, True
        self.last = ticks
        return ticks, False


@dataclass
class Outgoing:
    """What one plant message becomes."""
    ticks: int
    event: Optional[str] = None
    telemetry: dict[str, object] = field(default_factory=dict)
    late: bool = False
    unmapped_event: Optional[str] = None


def translate(message: dict, cfg: Config, clock: Clock) -> Outgoing:
    """Map one plant message to a PT event and/or a telemetry sample (no semantics)."""
    if cfg.timestamp_field and cfg.timestamp_field in message:
        ts = float(message[cfg.timestamp_field])
        ticks, late = clock.from_source(ts / 1000.0 if cfg.timestamp_unit == "ms" else ts)
    else:
        ticks, late = clock.from_wall()
    out = Outgoing(ticks=ticks, late=late)
    if cfg.event_field and cfg.event_field in message and message[cfg.event_field] not in (None, ""):
        code = str(message[cfg.event_field])
        if code in cfg.event_map:
            out.event = cfg.event_map[code]
        else:
            out.unmapped_event = code
    for source_field, telemetry_field in cfg.telemetry.items():
        if source_field in message and message[source_field] is not None:
            out.telemetry[telemetry_field] = message[source_field]
    return out


# ----------------------------------------------------------------------------- bridge

class Bridge:
    def __init__(self, cfg: Config, studio: Studio, dry_run: bool = False, log: Callable[[str], None] = print):
        self.cfg = cfg
        self.studio = studio
        self.dry_run = dry_run
        self.log = log
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.clock: Optional[Clock] = None
        self.sent_events = 0
        self.rejected_events = 0
        self.sent_samples = 0

    def connect(self) -> None:
        """Read the twin's current logical time and time base."""
        if self.dry_run:
            self.clock = Clock(0, 1000)
            self.log(f"dry run: {self.cfg.instance} at t=0 (nothing is sent)")
            return
        status, twin = self.studio.request("GET", f"/twins/{self.cfg.instance}")
        if status != 200 or not isinstance(twin, dict):
            raise SystemExit(f"twin-bridge: instance '{self.cfg.instance}' not found at {self.studio.base} (HTTP {status})")
        r = int(twin.get("ticksPerUnit") or 1000)
        status, state = self.studio.runtime("GET", "state")
        if status != 200 or not isinstance(state, dict):
            raise SystemExit(f"twin-bridge: the runtime of '{self.cfg.instance}' does not answer (HTTP {status}); "
                             "is the instance deployed and running?")
        t0 = int((state.get("time") or {}).get("ticks", 0))
        self.clock = Clock(t0, r)
        self.log(f"connected: {self.cfg.instance} in {', '.join(state.get('locations') or ['?'])} at "
                 f"t={t0 / r:.3f} ({r} ticks per unit)")

    def handle(self, message: dict) -> None:
        assert self.clock is not None
        with self.lock:
            out = translate(message, self.cfg, self.clock)
        t = out.ticks / self.clock.r
        if out.late:
            self.log(f"t={t:.3f} late message (its timestamp is before the last one sent): sent at the last time")
        if out.unmapped_event is not None:
            self.log(f"t={t:.3f} event code {out.unmapped_event!r} is not in the event map: not sent")
        if out.telemetry:
            self._post("telemetry", {"at": out.ticks, **out.telemetry})
            self.sent_samples += 1
        if out.event:
            status, body = self._post("pt-event", {"label": out.event, "ticks": out.ticks})
            self.sent_events += 1
            if status == 200 and isinstance(body, dict):
                accepted = bool(body.get("accepted"))
                dt_label = body.get("label", "")
                if accepted:
                    move = f" ({body['from']} -> {body['to']})" if body.get("from") and body.get("to") else ""
                    self.log(f"t={t:.3f} {out.event} -> {dt_label} accepted{move}")
                else:
                    self.rejected_events += 1
                    self.log(f"t={t:.3f} {out.event} -> {dt_label} REJECTED: {describe(body)} "
                             "(conformance violation recorded in the ledger)")
            elif not self.dry_run:
                self.log(f"t={t:.3f} {out.event} -> HTTP {status}: {describe(body)}")

    def heartbeat_loop(self) -> None:
        if self.cfg.heartbeat <= 0:
            return
        while not self.stopped.wait(self.cfg.heartbeat):
            with self.lock:
                assert self.clock is not None
                ticks, _ = self.clock.from_wall() if self.cfg.timestamp_field is None else (self.clock.last, False)
            if self.cfg.timestamp_field is not None:
                continue  # time follows the source's timestamps; no wall-clock heartbeats
            status, body = self._post("advance", {"ticks": ticks})
            if status == 200 and isinstance(body, dict) and body.get("accepted") is False:
                self.log(f"t={ticks / self.clock.r:.3f} the twin refused to let time pass: {describe(body)} "
                         "(a deadline of the verified model was missed; recorded in the ledger)")

    def _post(self, route: str, body: dict) -> tuple[int, object]:
        if self.dry_run:
            self.log(f"would POST {route} {json.dumps(body)}")
            return 200, {"accepted": True, "label": body.get("label", "")}
        for attempt in range(5):
            try:
                return self.studio.runtime("POST", route, body)
            except (urllib.error.URLError, ConnectionError, TimeoutError) as e:
                self.log(f"Studio unreachable ({e}); retrying")
                time.sleep(min(8, 0.5 * 2 ** attempt))
        return 0, "Studio unreachable"


def describe(body: object) -> str:
    if isinstance(body, dict):
        if "error" in body and isinstance(body["error"], dict):
            return body["error"].get("message", "")
        return body.get("message", json.dumps(body)[:200])
    return str(body)[:200]


# ----------------------------------------------------------------------------- sources

def json_lines(stream: Iterable[str], log: Callable[[str], None]) -> Iterable[dict]:
    for n, line in enumerate(stream, 1):
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            log(f"line {n}: not JSON, skipped")
            continue
        if isinstance(obj, dict):
            yield obj
        else:
            log(f"line {n}: not a JSON object, skipped")


def replay(path: str, cfg: Config, speed: float, log: Callable[[str], None]) -> Iterable[dict]:
    """A recorded log; with timestamps and speed > 0 the original pacing is reproduced."""
    previous: Optional[float] = None
    with open(path, encoding="utf-8") as f:
        for message in json_lines(f, log):
            ts = message.get(cfg.timestamp_field) if cfg.timestamp_field else None
            if speed > 0 and ts is not None:
                seconds = float(ts) / (1000.0 if cfg.timestamp_unit == "ms" else 1.0)
                if previous is not None and seconds > previous:
                    time.sleep((seconds - previous) / speed)
                previous = seconds
            yield message


def poll(url: str, interval: float, log: Callable[[str], None]) -> Iterable[dict]:
    while True:
        try:
            with urllib.request.urlopen(url, timeout=10) as r:
                data = json.loads(r.read() or b"null")
            for obj in data if isinstance(data, list) else [data]:
                if isinstance(obj, dict):
                    yield obj
        except (urllib.error.URLError, json.JSONDecodeError, TimeoutError) as e:
            log(f"poll {url}: {e}")
        time.sleep(interval)


# ----------------------------------------------------------------------------- main

def main(argv: Optional[list[str]] = None) -> int:
    p = argparse.ArgumentParser(description="Feed a deployed twin instance from a real system (see the module docstring).")
    p.add_argument("--config", required=True, help="bridge configuration (JSON)")
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--stdin", action="store_true", help="read JSON lines from standard input")
    src.add_argument("--file", help="replay a JSON-lines log")
    src.add_argument("--poll", metavar="URL", help="poll a REST endpoint returning JSON")
    p.add_argument("--speed", type=float, default=1.0, help="--file: replay speed (0 = as fast as possible)")
    p.add_argument("--interval", type=float, default=1.0, help="--poll: seconds between requests")
    p.add_argument("--studio", help="Studio URL (overrides the config)")
    p.add_argument("--instance", help="instance id (overrides the config)")
    p.add_argument("--dry-run", action="store_true", help="print what would be sent; send nothing")
    a = p.parse_args(argv)

    cfg = Config.load(a.config)
    if a.studio:
        cfg.studio = a.studio
    if a.instance:
        cfg.instance = a.instance
    log = lambda s: print(s, flush=True)  # noqa: E731
    bridge = Bridge(cfg, Studio(cfg.studio, cfg.instance), dry_run=a.dry_run, log=log)
    bridge.connect()
    heart = threading.Thread(target=bridge.heartbeat_loop, daemon=True)
    heart.start()
    if a.stdin:
        messages = json_lines(sys.stdin, log)
    elif a.file:
        messages = replay(a.file, cfg, a.speed, log)
    else:
        messages = poll(a.poll, a.interval, log)
    try:
        for message in messages:
            bridge.handle(message)
    except KeyboardInterrupt:
        pass
    finally:
        bridge.stopped.set()
    log(f"done: {bridge.sent_events} events ({bridge.rejected_events} rejected), {bridge.sent_samples} telemetry samples")
    return 0


if __name__ == "__main__":
    sys.exit(main())
