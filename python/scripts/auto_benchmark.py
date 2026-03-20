#!usr/bin/env python3
"""
Automated benchmark runner

Usage (inside container):
    python3 python/scripts/auto_benchmark.py \
    --batch-sizes 1,8,16,32,64 \
    --count 5000 --rate 5000 \
    --port 9000 \
    --receiver ./cpp/build/udp_receiver

What it does:
- For each batch_size:
    - removes data/events.jsonl
    - starts the receiver binary with args: <port> <batch_size>
    - runs python/scripts/benchmark_sender.py with provided count/rate
    - stops the receiver with SIGINT, waits for graceful exit
    - collects benchmark JSON output and receiver stdout/stderr
    - writes results to results.csv and stores logs under artifacts

    
"""

import argparse
import csv
import json
import os
import shutil
import signal
import subprocess
import time
from pathlib import Path
import re


HERE = Path("./")
EVENTS_FILE = HERE / "data" / "events.jsonl"
ARTIFACTS = HERE / "artifacts"
RESULTS_CSV = ARTIFACTS / "results.csv"
BENCH_SCRIPT = HERE / "python" / "scripts" / "benchmark_sender.py"

def run_cmds(cmd, timeout=None):
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        out, err = p.communicate(timeout=timeout)
        return p.returncode, out, err
    except subprocess.TimeoutExpired:
        p.kill()
        out, err = p.communicate()
        return -1, out, err

def parse_bench_output(output):
    # benchmark_sender prints JSON at the end, parse the JSON blob
    # find the first '{' that starts the JSON object for analysis
    m = re.search(r'(\{[\s\S]*\})\s*$', output.strip())
    if not m:
        return None, output
    try:
        data = json.loads(m.group(1))
        return data, output
    except Exception:
        return None, output

def ensure_artifacts():
    ARTIFACTS.mkdir(parents=True, exist_ok=True)

def clear_events():
    # ensure directory exs=ists and is writable
    EVENTS_FILE.parent.mkdir(parents=True, exist_ok=True)

    # remove old file if present
    try:
        if EVENTS_FILE.exists():
            EVENTS_FILE.unlink()
    except Exception as e:
        print(f"[runner][warn] failed to unlink {EVENTS_FILE} : {e}")
    
    try:
        with open(EVENTS_FILE, "a") as f:
            f.flush()
        try:
            os.chmod(EVENTS_FILE, 0o777)
        except Exception:
            pass
    except Exception as e:
        print(f"[runner][error] failed to create {EVENTS_FILE} before starting receiver: {e}")

def write_file(path: Path, content: str):
    try:
        path.write_text(content)
    except Exception as e:
        print(f"[runner][warn] failed to write {path} : {e}")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--batch-sizes", default="1,8,16,32,64", help="comma separated batch sizes to test")
    parser.add_argument("--count", type=int, default=5000)
    parser.add_argument("--rate", type=float, default=5000.0)
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--receiver", default="./cpp/build/udp_receiver", help="path to udp_receiver binary")
    parser.add_argument("--wait-before-run", type=float, default=0.5, help="seconds to wait after starting receiver")
    parser.add_argument("--per-run-timeout", type=int, default=300, help="timeout seconds for each benchmark run")

    args = parser.parse_args()

    batch_sizes = [int(x) for x in args.batch_sizes.split(",") if x.strip()]
    print(batch_sizes)
    receiver_path = Path(args.receiver)
    print(receiver_path)
    if not receiver_path.exists():
        print(f"[error] receiver not found at {receiver_path}")
        return
    ensure_artifacts()

    header = ["batch_size", "sent", "elapsed_s", "achieved_rate", "last_seq", "parsed_count",
              "inferred_drops", "seq_min", "seq_max", "lat_count", "lat_min", "lat_mean", "lat_median", "lat_p95",
              "lat_p99", "lat_stddev", "recevier_rc", "bench_rc", "receiver_stdout_file", "receiver_stderr_file",
              "bemch_stdout_file"]
    
    with open(RESULTS_CSV, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(header)

    for bs in batch_sizes:
        print(f"\n---------> RUN batch_size = {bs} <-------------")
        clear_events()

        #start receiver
        recv_cmd = [str(receiver_path), str(args.port), str(bs)]
        recv_proc = subprocess.Popen(recv_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        print(f"[runner] started receiver pid = {recv_proc.pid}, waiting {args.wait_before_run}s")
        time.sleep(args.wait_before_run)

        # check if receiver exited early
        if recv_proc.poll() is not None:
            # receiver exited, captuer output and skip this run
            try:
                r_stdout, r_stderr = recv_proc.communicate(timeout=1)
            except Exception:
                r_stdout, r_stderr = "", ""
            recv_stdout_file = ARTIFACTS / f"recevier_bs{bs}.stdout.txt"
            recv_stderr_file = ARTIFACTS / f"receiver_bs{bs}.stderr.txt"
            write_file(recv_stdout_file, r_stdout or "")
            write_file(recv_stderr_file, r_stderr or "")
            print(f"[runner][error] reciever exited early (rc={recv_proc.returncode})")
            print(f"skipping bench for batch_size = {bs}")
            #record a row incdicating reciever failure
            row = [bs] + [""] * 15 + [recv_proc.returncode if recv_proc.returncode is not None else -1, "N/A", str(recv_stdout_file), str(recv_stderr_file), ""]
            with open(RESULTS_CSV, "a", newline="") as f:
                writer = csv.writer(f)
                writer.writerow(row)
            continue
        
        # run benchmark sender
        bench_cmd = ["python3", str(BENCH_SCRIPT),
                     "--host", "127.0.0.1",
                     "--port", str(args.port),
                     "--count", str(args.count),
                     "--rate", str(int(args.rate)),
                     "--symbol", "AAPL"]
        print(f"[runner] running bench : {' '.join(bench_cmd)}")
        bench_rc, bench_out, bench_err = run_cmds(bench_cmd, timeout= args.per_run_timeout)

        bench_log_file = ARTIFACTS / f"bench_bs{bs}.stdout.txt"
        # bench_log_file.write_text(bench_out + "\n\nSTDERR:\n" + bench_err)
        write_file(bench_log_file, (bench_out or "") + "\n\nSTDERR:\n" + (bench_err or ""))

        print(subprocess.check_output(["ls", "-ltrha", "./data"], text=True))
        # parse bench output JSON only if bench_rc == 0
        analysis = {}
        if bench_rc == 0:

            analysis, _ = parse_bench_output(bench_out)
            if analysis is None:
                print("[runner][warn] failed to parse bench JSON output, storing raw output")
                analysis = {}
        else:
            print(f"[runner][warn] benchmark script exited with rc = {bench_rc}")
            print("storing output......")
        # gracefully stop
        print(f"[runner] stopping receiver pid = {recv_proc.pid}")
        try:
            recv_proc.send_signal(signal.SIGINT)
            try:
                r_stdout, r_stderr = recv_proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                recv_proc.kill()
                r_stdout, r_stderr = recv_proc.communicate(timeout=5)
        except Exception as e:
            print(f"[runner][error] stopping receiver : {e}")
            recv_proc.kill()
            r_stdout, r_stderr = recv_proc.communicate()

        recv_stdout_file = ARTIFACTS / f"receiver_bs{bs}.stdout.txt"
        recv_stderr_file = ARTIFACTS / f"receiver_bs{bs}.stderr.txt"
        # recv_stdout_file.write_text(r_stdout or "")
        # recv_stderr_file.write_text(r_stderr or "")
        write_file(recv_stdout_file, r_stdout or "")
        write_file(recv_stderr_file, r_stdout or "")

        # read last events.jsonl lines for (sanity) 
        parsed_count = analysis.get("parsed_count", "")
        inferred_drops = analysis.get("inferred_drops", "")
        seq_min, seq_max = (analysis.get("seq_range") or (None, None))
        lat_stats = analysis.get("latency_stats", {}) or {}

        row = [
            bs,
            analysis.get("sent", ""),
            analysis.get("elapsed_s", ""),
            analysis.get("achieved_rate", ""),
            analysis.get("last_seq", ""),
            parsed_count,
            inferred_drops,
            seq_min,
            seq_max,
            lat_stats.get("count", ""),
            lat_stats.get("min", ""),
            lat_stats.get("mean", ""),
            lat_stats.get("median", ""),
            lat_stats.get("p95", ""),
            lat_stats.get("p99", ""),
            lat_stats.get("stddev", ""),
            recv_proc.returncode if recv_proc.returncode is not None else 0,
            bench_rc,
            str(recv_stdout_file),
            str(recv_stderr_file),
            str(bench_log_file)
            
        ]

        with open(RESULTS_CSV, "a", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(row)

        print(f"[runner] finished batch_size = {bs} -> parsedCount = {parsed_count} inferred drops = {inferred_drops}")
        print(f"recevier_rc = {recv_proc.returncode} bench_rc = {bench_rc}")

    print(f"\n All runs completed, Results : {RESULTS_CSV}")
    print(f"Artifacts: {ARTIFACTS}")

if __name__ == "__main__":
    main()