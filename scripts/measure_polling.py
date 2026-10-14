"""Capture only sanitized meter timing/sample lines, and summarize gaps."""
import argparse
import json
import re
import statistics
import time
from collections import defaultdict

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("port")
parser.add_argument("--seconds", type=float, default=240)
parser.add_argument("--warmup-seconds", type=float, default=0,
                    help="Discard this many seconds after opening the port before measuring")
args = parser.parse_args()
if args.seconds <= 0 or args.warmup_seconds < 0:
    parser.error("seconds must be positive and warmup-seconds must be nonnegative")
records = []
samples = []
pending = {}
completed = defaultdict(list)
timeouts = []
fallbacks = []
tx_times = []
with serial.Serial(args.port, 115200, timeout=0.2) as device:
    started = time.monotonic() + args.warmup_seconds
    while time.monotonic() - started < args.seconds:
        text = device.readline().decode("utf-8", errors="replace").strip()
        if not text:
            continue
        at = time.monotonic() - started
        if at < 0:
            continue
        if re.fullmatch(r"METER (request|tx|response|timeout|fallback) [a-zA-Z0-9_= ]+", text):
            parts = text.split()
            fields = dict(item.split("=", 1) for item in parts[2:])
            record = {"event": parts[1], "at": round(at, 3), **fields}
            records.append(record)
            tid = fields["tid"]
            if parts[1] == "request":
                pending[tid] = record
            elif parts[1] == "response":
                key = fields["epc"]
                if tid not in pending:
                    key += " (request_not_captured)"
                elif pending[tid].get("opc") == "2" and key == "E7":
                    key = "E7+E8"
                completed[key].append(int(fields["elapsed"]))
                pending.pop(tid, None)
            elif parts[1] == "timeout":
                timeouts.append(record)
                pending.pop(tid, None)
            elif parts[1] == "tx":
                tx_times.append(int(fields["elapsed"]))
            elif parts[1] == "fallback":
                fallbacks.append(record)
            print(json.dumps(record), flush=True)
        elif re.fullmatch(r"\d{1,7}", text):
            samples.append((at, int(text)))
            print(json.dumps({"event": "sample", "at": round(at, 3), "watts": int(text)}), flush=True)
        elif re.fullmatch(r"DISPLAY ready heap=\d+", text) or text == "DISPLAY framebuffer unavailable":
            print(json.dumps({"event": "display", "at": round(at, 3), "value": text}), flush=True)
        elif text.startswith(("current R/T", "cumulative energy")):
            print(json.dumps({"event": "slow_sample", "at": round(at, 3), "value": text}), flush=True)

def stats(values):
    ordered = sorted(values)
    return {"n": len(values), "min": min(values), "median": statistics.median(values),
            "p95": ordered[max(0, (95 * len(values) + 99) // 100 - 1)], "max": max(values)}

gaps = [round(b[0] - a[0], 3) for a, b in zip(samples, samples[1:])]
summary = {"event": "summary", "responses_ms": {epc: stats(v) for epc, v in completed.items()},
           "duration_s": args.seconds, "warmup_s": args.warmup_seconds,
           "samples": len(samples), "timeouts": len(timeouts), "fallbacks": len(fallbacks),
           "inflight_at_end": len(pending)}
if tx_times:
    summary["tx_ms"] = stats(tx_times)
if gaps:
    summary["sample_gap_s"] = stats(gaps)
power_times = [int(r["t"]) for r in records if r["event"] == "response" and r["epc"] == "E7"]
if len(power_times) > 1:
    summary["device_power_gap_ms"] = stats([(b-a) & 0xFFFFFFFF for a, b in zip(power_times, power_times[1:])])
print(json.dumps(summary), flush=True)
for (a, _), (b, _) in zip(samples, samples[1:]):
    if b - a > 3:
        print(json.dumps({"event": "long_gap", "from_s": round(a, 3), "gap_s": round(b-a, 3),
                          "trace": [r for r in records if a - 0.02 <= r["at"] <= b + 0.02]}), flush=True)
if not samples:
    raise SystemExit("No power samples captured")
