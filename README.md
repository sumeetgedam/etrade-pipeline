# E-Trade Pipeline - Equities L2 ( Toy ) Monorepo

TL;DR
- End-to-end toy equities L2 pipeline focused on low-latency feed ahndling, simple in-memory orderbook, and observability
- C++20 feed handler (UDP) + orderbook core, Python FastAPI for event access and metrics, Prometheus exposition + simple UI scaffold
-Designe as a lerning and experimentaion platform for ultra low latency techniques ( zero-copy, hw timestaping, lock-free handoffs planned)

Tech stack
- C++20 (gcc) - feed handler, parser, minimal L2 orderbook
- CMake + Ninja - build
- Python 3 - FastAPI app, test sender, metrics endpoint
- prometheus_client - scrapeable metrics endpoint
- Docker / docker-compose - optional local Prometheus + Grafana integration

Repository layout ( top-level )
- cpp/ - C++ sources, includes, CMakeLists
- python/ - FastAPI app, test sender, scripts
- infra/ - prometheus.yml & docker-compose for local Prometheus/Grafana
- scripts/ - dev helpers(scripts/dev-set-up.sh)
- data/ - events.jsonl and snapshots
- docs/ - architecture notes, milestones

What this project implementts (current)
- Minimal UDP receiver (cpp/src/udp_receiver.cpp) that:
    - binds to UDP port, receives datagrams, timestamps arrival
    - parses simple text feed messages : seq|msg_ts_ms|SYM|price|size
    - appends parsed JSONL lines to /data/events.jsonl
    - computes latency_ms = recv_ts_ms - msg_ts_ms
    - forward parsed Event objects to an in-process Orderbook consumer via a TSQueue
- Simple L2 orderbook skeleton( cpp/src/order_book.cpp) that maintains top levels per symbol and prints updates
- Python FastAPI service (python/app/main.py) exposing :
    - GET /events?tail=N -> last N events (JSON)
    - GET /metrics?tail=N -> JSO/n latency stats (p50/p95/p99, hidtogram)
    - GET /metrics_prometheus?tail=N -> Prometheus exposition (histogram + counters + percentile gauges)
- A small python/scripts/send_udp.py to generate test messages
- scripts/dev-setup.sh for venv creation and dependency installation

Monorepo for a toy end to end electronic trading pipeline:
- cpp/ : low-latency components ( feed handler, order book, gateway )
- python/ : analytics, FastAPI, notebooks
- docs/ : architecture and milestones
- infra/ : docker-compose and infra artifacts
- data/ : sample feeds and snapshots

This is an initial scaffold. Follow the milestones in docs/milestones.md
