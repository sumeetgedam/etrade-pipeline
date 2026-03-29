# Low Ltanecy Market Engine (ULL Engine) - Current State

#### Status snapshot:
- Completed (core): high-throughput UDP market feed receiver, L2 OrderBook application, in-process OrderManager, TCP execution gateway (simple ACK/REJ/FILL/CXL protocol), basic pre-trade risk checks, atomic Prometheus metrics + Grafana dashboard, replay sender and micro-benchmark latency harness
- In progress / partial : deterministic replay integration and test harness , write ahead event JSONL export, latency measurement tools


#### What this repo provides now
- Market data reciever ( udp_receiver ) with recvmmsg batching and queueing
- Order book (L2) builder and a shared global book registry used by the in-process matching engine
- OrderManager : simple in-process order handling with basic risk checks ( max order size, max open orders ).
- Execution gateway ( execution_gateway ) : TCP server that accepts `ORDER|...` and `CANCEL|...` plain messages and responds with `ACK|`, `REJ|`, `FILL|`, `CXL|`.
- Metrics : atomics exported by a small HTTP metrics server at `/metrics` ( Prometheus text format ).
- Monitoring stack: docker-compose config for Prometheus + Grafana and a provisioned "ULL Engine Overview dashboard
- Replay tools: `replay_sender` to replay JSONL event files to a target UDP endpoint, `test_replay.py` integration test
- Latency benchmark harness: `udp_bench_sender`, `udp_latency_server`, and `compute_latency_stats.py`
- Helpful scripts for quick integration tests and experiments

#### High-level architecture
- UDP market feed -> udp_receiver (recvmmsg batch) -> SPSC queue -> consumer thread -> OrderBook.apply_event -> (top-of-book snapshot written if configured)
- Trading client -> TCP gateway -> OrderManager -> OrderBook matching -> fills reeported back via gateway
- Metrics exposed by udp_receiver + OrderManager -> scraped by Prometheus -> Grafana dashboard


#### Build ( Linux )
1. Ensure build dependencies (C/C++ toolchain, cmake, ninja). Example on Ubuntu: sudo apt update && sudo apt install -y build-essential cmake ninja-build libssl-dev

2. Build:  
    cd cpp  
    rm -rf build && mkdir -p build && cd build  
    cmake -G Ninja ..  
    ninja -v

#### Key binaries
- ./cpp/build/engine - integrated engine ( udp receiver + orderbook + gateway )
- ./cpp/build/execution_gateway - standalone gateway ( if built separately )
- ./cpp/build/replay_sender - replay JSONL -> UDP sender
- ./cpp/build/udp_bench_sender - latency send generator
- ./cpp/build/udp_latency_server - latency recorder

#### Quick run (local integrated engine)
- Start engine (example ports):
    ./cpp/build/engine 9000 32 9999 100000 9100
    Args: <udp_port> <udp_batch_size> <gateway_port> <max_order_size> <metrics_port>

- Send a simple ORDER (netcat|telnet):
    printf 'ORDER|cli1|BUY|AAPL|101.00|10\n' | nc 127.0.0.1 9999

- Query metrics:
curl --noproxy '*' http://127.0.0.1:9100/metrics

#### Monitoring (Prometheus + Grafana)
- Files added: infra/monitoring/docker-compose.yml, prometheus/prometheus.yml, grafana provisioning + dashboard JSON
- Start :  
    cd infra/monitoring  
    docker-compose up -d
- Prometheus UI; http://localhost:9090
- Grafana UI: http://localhost:3000
- Prometheus targets: check `ull_engine` target is UP and scraping the engine metrics endpoint

#### Replay & integration test
- Build the replay_sender and engine as above.
- To replay an events JSONL :  
    ./cpp/build/replay_sender events.jsonl <target_ip> <target_udp_port> [--fast] [--scale N]
- Quick integration test (sends a couple of UDP market messages + order):  
    ./python/scripts/test_replay.py  
    (ensure ./cpp/build/engine exists or set env ENGINE_BIN to binary path.)

#### Latency micro-benchmark
- Start server:  
    ./cpp/build/udp_latency_server 9001  /tmp/recv.csv &
- Run sender:  
    ./cpp/build/udp_bench_sender 127.0.0.1 9001 20000 200 50000
- Compute stats:  
    ./python/scripts/compute_latency_stats.py /tmp/recv.csv


#### Where state and snapshot are written
- Top-of-Book snapshot (fallback snapshots) used gateway fallback are placed under:  
    data/book_<SYMBOL>.json
- UDP receiver optionall writes event JSONL for replay (see udp_reciever code).

#### Observability & metrics exported
- udp_receiver_* (processed_events, dropped_events, batches, queue_size, last_latency_ms)
- orders_* (orders_accepted_totalm orders_rejected_total, orders_fills_total, orders_filled_volume_total, orders_canceled_total, orders_open)
- Grafana dashboard configured to show these metrics with increase()/rate() panels

#### Dev notes & recommendations
- When running monitoring inside Docker Desktop (Windows)ensure container network can reach engine

#### Testing
- Unit tests: add tests for OrderBook level application and OrderManager, Integration test: python/scripts/test_replay.py
- Benchmarks: use udp_bench_sender ->up_latency_server