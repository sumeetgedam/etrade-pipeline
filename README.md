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


Monorepo for a toy end to end electronic trading pipeline:
- cpp/ : low-latency components ( feed handler, order book, gateway )
- python/ : analytics, FastAPI, notebooks
- docs/ : architecture and milestones
- infra/ : docker-compose and infra artifacts
- data/ : sample feeds and snapshots

This is an initial scaffold. Follow the milestones in docs/milestones.md
