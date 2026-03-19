#!/usr/bin/env python3
"""
Simlple micro-benchmark sender for udp_receiver + post-run analyzer

Usage:
    python3 python/scripts/benchmark_sender.py --host 127.0.0.1 --port 9000 --count 50000 --rate 20000 --symbol AAPL

What is does :
- sends `--count` UDP messages at approzimately `--rate` messages/sec
- Each messages format; seq|msg_ts_ms|SYMBOL|price|size
- After sending, sleeps briefly then reads data/events.jsonl and parses 
the most recent entries to cmopute parsed_count, inferred drops, and latency stats
- Prints a short summary and latency distribution ( min, mean, median/p50, p95, p99, stddev ).
"""

import argparse
import socket
import time
import math
import json
import statistics
from pathlib import Path

EVENTS_FILE = Path("data/events.jsonl")

def now_ms():
    return int(time.time() * 1000)


def percentile(sorted_vals, p):
    if not sorted_vals:
        return None

    n = len(sorted_vals)
    if p <= 0:
        return float(sorted_vals[0])
    if p >= 100:
        return float(sorted_vals[-1])
    
    k = (n-1) * (p/100.0)
    f = int(math.floor(k))
    c = k - f
    if f + 1 < n:
        return float(sorted_vals[f] + ( sorted_vals[f+1] - sorted_vals[f])) * c
    else:
        return float(sorted_vals[f])
    

def send_messages(host, port, count, rate, symbol, start_seq=1):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    addr = (host, port)
    interval = 1.0 / rate if rate > 0 else 0.0
    seq = start_seq
    sent = 0

    t0 = time.perf_counter()
    next_send = t0
    for i in range(count):
        msg_ts = now_ms()
        price = 100.0 + (i % 100) * 0.01
        size = 100 + (i % 10)
        payload = f"{seq}|{msg_ts}|{symbol}|{price:.2f}|{size}"
        try:
            sock.sendto(payload.encode("utf-8"), addr)
            sent += 1
        except Exception as e:
            print(f"[sender][error] send failed for seq = {seq} err = {e}")
        seq += 1

        if interval > 0:
            next_send += interval
            # busy wait / sleep a bit for fine timing
            now = time.perf_counter()
            sleep_for = next_send - now - 0.0005
            if sleep_for > 0:
                time.sleep(sleep_for)
            #spin until target ( avoid long sleeps losing precision )
            while time.perf_counter() < next_send:
                pass

    elapsed = time.perf_counter() - t0
    achieved_rate = sent / elapsed if elapsed > 0 else 0.0
    return {
        "sent" : sent,
        "elapsed_s" : elapsed,
        "achieved_rate" : achieved_rate,
        "last_seq" : seq - 1
    }


def analyze_events(expect_sent, last_seq):
    # wait to allow receiver to flush to disk

    time.sleep(5)
    if not EVENTS_FILE.exists():
        return {"error": f"{EVENTS_FILE} not found"}
    

    # read whole file
    with open(EVENTS_FILE, "r", encoding="utf-8", errors="replace") as f:
        lines = [ln.strip() for ln in f if ln.strip()]

    # filter lines that look like JSON and parse last expected_sent lines
    parsed = []
    for ln in reversed(lines):
        try:
            obj = json.loads(ln)
            parsed.append(obj)
            if len(parsed) >= expect_sent:
                break
        except Exception:
            continue
    
    parsed = list(reversed(parsed)) # restore chronological order
    parsed_count = len(parsed)

    latencies = []

    seqs = []
    for obj in parsed:
        try:
            if "latency_ms" in obj:
                lat = float(obj["latency_ms"])
            elif "recv_ts_ms" in obj and "msg_ts_ms" in obj:
                lat = float(obj["recv_ts_ms"]) - float(obj["msg_ts_ms"])
            else:
                continue
            latencies.append(lat)

        except Exception:
            continue
        
        if "seq" in obj:
            seqs.append(int(obj["seq"]))
    stats = {}
    if latencies:
        s = sorted(latencies)
        stats["count"] = len(latencies)
        stats["min"] = min(latencies)
        stats["max"] = max(latencies)
        stats["mean"] = statistics.mean(latencies)
        stats["median"] = percentile(s, 50.0)
        stats["p95"] = percentile(s, 95.0)
        stats["p99"] = percentile(s, 99.0)
        stats["stddev"] = statistics.pstdev(latencies) if len(latencies) > 1 else 0.0
    else:
        stats["count"] = 0

    inferred_drops = max(0, expect_sent - parsed_count)
    seq_range = (min(seqs) if seqs else None, max(seqs) if seqs else None)

    return {
        "parsed_count" : parsed_count,
        "inferred_drops" : inferred_drops,
        "seq_range" : seq_range,
        "latency_Stats" : stats
    }



def main():

    parser = argparse.ArgumentParser(description="UDP benchmark sender + analyzer")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--count", type=int, default=10000)
    parser.add_argument("--rate", type=float, default=20000.0, help="messages per send")
    parser.add_argument("--symbol", default="AAPL")
    parser.add_argument("--start-seq", type=int, default=1)
    args = parser.parse_args()

    print(f"[bench] sending {args.count} msgs to {args.host}:{args.port} at target {args.rate:.0f}/s symbol - {args.symbol}")

    res = send_messages(args.host, args.port, args.count, args.rate, args.symbol, args.start_seq)

    print(f"[bench] done sending : sent= {res['sent']} elapsed_s [ {res['elapsed_s']:.3f}]s achieved_rate = {res['achieved_rate']:.1f}/s last_seq = {res['last_seq']}")


    print("[bench] analyzing events.jsonl.....")

    analysis = analyze_events(res['sent'], res['last_seq'])
    print(json.dumps(analysis, indent=2))


if __name__ == "__main__":
    main()
