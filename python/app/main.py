# Small FastAPI app to serve the latest parsed events from events.jsonl
# - Endpoint: GET /events?tail=10 -> returns last `tail` JSON objects ( default 10 )
# - Uses an efficient backward-read helper to avoid loading the whole file 
# - Designed for development/testing (not optimized for production or huge files)
# 
# - GET /metrics?tail=N -> returns latency statistics computed from last N events
# - GET /metrics_prometheus>tail=N -> returns Prometheus text exposition for scraping


from fastapi import FastAPI, HTTPException, Query
from typing import List, Any, Optional, Dict
from pathlib import Path
import json
import os
import statistics
import time

# Prometheus client imports
from prometheus_client import CollectorRegistry, Counter, Gauge, Histogram, generate_latest, CONTENT_TYPE_LATEST


DATA_FILE = Path("data/events.jsonl")
app = FastAPI(title="etrade-pipeline events API (with prometheus metrics)")


def read_last_lines(path: Path, num_lines: int) -> List[str]:
    """
    Read the last `num_lines` lines from the file at `path` efficiently.
    Returns list of decoded text lines ( no trailing newline )
    Notes:
        - Opens file in binary mode and reads from the end in chunks
        - works well for reasonably-size tail windows ( tens/hundreds of lines)
        - simpler than mmap for now and avoids loading entire file when it grows

    """
    if not path.exists():
        print("path does not exists")
        return []
    
    bufsize = 8192
    lines = []
    with open(path, "rb") as f:
        f.seek(0, os.SEEK_END)
        file_size = f.tell()
        if file_size == 0:
            return []
        # Start form EOF and read backwards until we have enough lines
        block_end = file_size
        data = bytearray()
        while block_end > 0 and len(lines) <= num_lines:
            block_start = max(0, block_end - bufsize)
            f.seek(block_start)
            block = f.read(block_end - block_start)
            data[:0] = block # prepend
            # split into lines
            all_lines = data.split(b'\n')
            # keep complete lines except possibly the first partial one
            # convert to text and store
            if len(all_lines) > 1:
                # all_lines[-1] may be empty if file ends with newline
                lines = [ln.decode('utf-8', errors='replace') for ln in all_lines if ln != b'']
            
            block_end = block_start
            if block_start == 0:
                break
        
        
        # Return the last num_lines entries ( as text )
        return lines[-num_lines:]
    

@app.get("/health")
def health():
    return {"status": "ok"}

@app.get("/events", response_model=List[Any])
def get_events(tail: int = Query(10, ge=1, le=1000)):
    """
    Return the last tail events from the events from the events.jsonl file as JSON objects
    - tail : number of most recent events to return ( default 10 )
    - If parsing of a JSON line fails , that line will be skipped and an error logged
    """
    raw_lines = read_last_lines(DATA_FILE, tail)
    if not raw_lines:
        
        return []
    
    out = []
    for idx, line in enumerate(raw_lines):
        line = line.strip()
        print(line)
        if not line:
            continue
        try:
            obj = json.loads(line)
            out.append(obj)
        except Exception as e:
            # Return parse errors as part of the HTTP response with context if nothin parse
            # for now, skip bad lines but include an internal error note if nothing valid

            continue
    return out


def percentile(sorted_vals: List[float], p: float) -> Optional[float]:
    """
    Compute percentile p (0-100) using linear interpolcation between closet ranks
    Assumes sorted_vals is non-emtpy and sorted ascending
    """
    n = len(sorted_vals)
    if n == 0:
        return None
    if p <= 0:
        return float(sorted_vals[0])
    if p >= 100:
        return float(sorted_vals[-1])
    
    # compute rank index
    k = (n-1) * (p/100.0)
    f = int(k)
    c = k-f
    if f + 1 < n:
        return float(sorted_vals[f]) + (sorted_vals[f+1]-sorted_vals[f])*c
    else:
        return float(sorted_vals[f])
    

def compute_latnecy_stats(latencies: List[float]) ->Dict[str, Any]:
    """
    Return summary and a simple bucket histogram for the provided latencies
    """
    if not latencies:
        return {"count": 0, "error": "no latency data"}
    
    n = len(latencies)
    lat_sorted = sorted(latencies)
    minimum = float(lat_sorted[0])
    maximum = float(lat_sorted[-1])
    mean_v = float(statistics.mean(lat_sorted))
    median_v = percentile(lat_sorted, 50.0)
    p95 = percentile(lat_sorted, 95.0)
    p99 = percentile(lat_sorted, 99.0)
    p999 = percentile(lat_sorted, 99.9)

    # population stddev
    try:
        stddev_v = float(statistics.pstdev(lat_sorted))
    except Exception:
        stddev_v = 0.0


    # simple histogram buckets ( ms ): counts for  <= bucket
    buckets = [0.0, 0.5, 1.0, 2.0, 5.0, 10.0, 50.0, 100.0, 500.0, 1000.0]
    hist = {}
    for b in buckets:
        hist[str(b)] = sum(1 for x in lat_sorted if x <= b)

    return {
        "count": n,
        "min": minimum,
        "max": maximum,
        "mean": mean_v,
        "median": median_v,
        "p95": p95,
        "p99": p99,
        "p99.9": p999,
        "stddev": stddev_v,
        "histogram_counts_le": hist,
    }


@app.get("/metrics")
def get_metrics(tail: int = Query(1000, ge=1, le=100000)):
    """
    Compute and return latency statistics (ms) over the last `tail` events
    - tail: how many most-recent events to scan (default 1000)
    """
    raw_lines = read_last_lines(DATA_FILE, tail)
    if not raw_lines:
        return {"count": 0, "error": f"No event found in {DATA_FILE}"}
    
    latencies: List[float] = []
    parsed = 0
    skipped = 0
    for line in raw_lines:
        line = line.strip()
        if not line:
            continue
        try:
            obj = json.loads(line)
        except Exception:
            skipped += 1
            continue

        # accept numeric latency from either "latecy_ms" or compute from recv/msg timestamps
        lat = None
        if "latency_ms" in obj:
            try:
                lat = float(obj["latency_ms"])
            except Exception:
                lat = None
        else:
            try:
                if "recv_ts_ms" in obj and "msg_ts_ms" in obj:
                    lat = float(obj["recv_ts_ms"]) - float(obj["msg_ts_ms"])
            except Exception:
                lat = None

        if lat is None:
            skipped += 1
            continue

        parsed +=1
        latencies.append(lat)

    if not latencies:
        return {"count":0, "error": "no vaid latency values parsed", "parsed_lines":parsed, "skipped_lines":skipped}

    stats = compute_latnecy_stats(latencies)

    # add parsed / skipped metadata
    stats["parsed_lines"] = parsed
    stats["skipped_lines"] = skipped

    return stats    


@app.get("/metrics_prometheus")
def get_metrics_prometheus(tail: int = Query(1000, ge=1, le=100000)):
    """
    Return Prometheus exposition text for the last `tail` events
    Use you Prmetheus scrape config to target /metrics_prometheus
    """
    raw_lines = read_last_lines(DATA_FILE, tail)

    # Create a per-request CollectorRegistry to avoid global state
    registry = CollectorRegistry()

    # Define metrics in registry
    scanned_counter = Counter("etrade_events_scanned_total", "Total events scanned for metrics", registry=registry)
    parsed_counter = Counter("etrade_events_parsed_total", "Total events successfully parsed", registry=registry)
    skipped_counter = Counter("etrade_events_skipped_total", "Total events skipped/malformed", registry=registry)

    # Histogram for latency in milliseconds, Bucket are in ms ( Prometheus can aggregate them)
    # Prometheus client histogram buckets expect increasing float values
    hist_buckets = (0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 50.0, 100.0, 500.0, 1000.0)
    latency_hist = Histogram("etrade_latency_ms", "Ingress latency in milliseconds", buckets=hist_buckets, registry=registry)

    # Gauges for pecentiles ( helpful direct values )
    p50_g = Gauge("etrade_latency_p50_ms", "Latency p50 (ms)", registry=registry)
    p95_g = Gauge("etrade_latency_p95_ms", "Latency p95 (ms)", registry=registry)
    p99_g = Gauge("etrade_latency_p99_ms", "Latency p99 (ms)", registry=registry)

    latencies: List[float] = []
    parsed = 0
    skipped = 0
    scanned = 0
    for line in raw_lines:
        line = line.strip()
        if not line:
            continue
        scanned += 1
        try:
            obj = json.loads(line)
        except Exception:
            skipped +=1
            continue
        
        lat = None
        if "latency_ms" in obj:
            try:
                lat = float(obj["latency_ms"])
            except Exception:
                lat = None
        else:
            try:
                if "recv_ts_ms" in obj and "msg_ts_ms" in obj:
                    lat = float(obj["recv_ts_ms"]) - float(obj["msg_ts_ms"])
            except Exception:
                lat = None

        if lat is None:
            skipped += 1
            continue

        parsed +=1
        latencies.append(lat)
        # observer each laetncy into histogram
        latency_hist.observe(lat)

    # update counters
    scanned_counter.inc(scanned)
    parsed_counter.inc(parsed)
    skipped_counter.inc(skipped)

    # set percentile gauges if we have data
    if latencies:
        s = sorted(latencies)
        p50 = percentile(s, 50.0)
        p95 = percentile(s, 95.0)
        p99 = percentile(s, 99.0)
        if p50 is not None:
            p50_g.set(p50)
        if p95_g is not None:
            p95_g.set(p95)
        if p99_g is not None:
            p99_g.set(p99)
    
    # return Prometheus exposition format
    return generate_latest(registry), 200, {"Content-Type": CONTENT_TYPE_LATEST}


@app.get("/book")
def get_book(symbol: str = Query(..., min_length = 1), max_age_ms: int = Query(5000, ge=0)):
    """
    Return the latest per-symbol orderbook snapshot written by the C++ OrderBook
    snapshot path : data/book_<SYMBOL>/json

    Parameters:
        - symbol: required symbol string
        - max_age_ms: maximum allowed snapshot age in milliseconds ( default 5000)
                        if snapshot's last updatedms is older tan this, returns 404
                        Use max_age_ms = 0 to diable TTL check (accept any age)
    """
    safe_symbol = symbol.strip()
    if not safe_symbol:
        raise HTTPException(status_code=400, detail="symbol is required")
    
    path = Path(f"data/book_{safe_symbol}.json")
    if not path.exists():
        print(path)
        raise HTTPException(status_code=400, detail=f"snapshot not found for symbol {safe_symbol}")
    
    try:
        with open(path, "r", encoding="utf-8") as f:
            content = json.load(f)
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"failed to read snapshot: {str(e)}")

    # TTL check 
    if 'last_updated_ms' not in content:
        raise HTTPException(status_code=404, detail="snapshot missing last_updated_ms, cannot verify freshness")

    try:
        last_updated_ms = int(content["last_updated_ms"])
    except Exception:
        raise HTTPException(status_code=500, detail="invalid last_updated_ms in snapshot")
    
    now_ms = int(time.time() * 1000)

    age_ms = now_ms - last_updated_ms
    if max_age_ms > 0 and age_ms > max_age_ms:
        raise HTTPException(
            status_code = 404,
            detail=f"snapshot stale: age_ms = {age_ms} exceed max_age_ms = {max_age_ms}"
        )
    
    return content
