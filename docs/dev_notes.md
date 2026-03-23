cmake -G Ninja ..
ninja


root@47e9d6fa4e5b:/worksapce/cpp/build# ./udp_receiver 9000
[udp_receiver] Listening on 0.0.0.0 : 9000 (press Ctrl+C to stop)
[ 1773560694618 ms] from 127.0.0.1 : 49162 size = 6 preview = 54 45 53 54 2d 30
[ 1773560694918 ms] from 127.0.0.1 : 49162 size = 6 preview = 54 45 53 54 2d 31
[ 1773560695219 ms] from 127.0.0.1 : 49162 size = 6 preview = 54 45 53 54 2d 32
[ 1773560695521 ms] from 127.0.0.1 : 49162 size = 6 preview = 54 45 53 54 2d 33
[ 1773560695822 ms] from 127.0.0.1 : 49162 size = 6 preview = 54 45 53 54 2d 34
^C
^C^C
 github  docker exec -it 47e9d6fa4e5b bash


root@47e9d6fa4e5b:/worksapce/python/scripts# ps aux | grep udp_receiver
root       282  0.0  0.0   6200  3584 pts/1    S+   08:22   0:00 ./udp_receiver 9000
root       319  0.0  0.0   6200  3584 pts/3    S+   08:57   0:00 ./udp_receiver 9000
root       414  0.0  0.0   6204  3456 pts/4    S+   10:15   0:00 ./udp_receiver 9000
root       417 30.0  0.0   3472  1664 pts/2    S+   10:17   0:00 grep --color=auto udp_receiver
root@47e9d6fa4e5b:/worksapce/python/scripts# ps aux | grep udp_receiver | grep -v grep
root       282  0.0  0.0   6200  3584 pts/1    S+   08:22   0:00 ./udp_receiver 9000
root       319  0.0  0.0   6200  3584 pts/3    S+   08:57   0:00 ./udp_receiver 9000
root       414  0.0  0.0   6204  3456 pts/4    S+   10:15   0:00 ./udp_receiver 9000
root@47e9d6fa4e5b:/worksapce/python/scripts# pkill -TERM udp_receiver || kill -TERM $(pgrep udp_receiver || true)
root@47e9d6fa4e5b:/worksapce/python/scripts# ps aux | grep udp_receiver | grep -v grep
root@47e9d6fa4e5b:/worksapce/python/scripts#


curl "http://127.0.0.1:8000/metrics_prometheus" | head
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100  2124  100  2124    0     0   9267      0 --:--:-- --:--:-- --:--:-- 11005
["# HELP etrade_events_scanned_total Total events scanned for metrics\n# TYPE etrade_events_scanned_total counter\netrade_events_scanned_total 20.0\n# HELP etrade_events_scanned_created Total events scanned for metrics\n# TYPE etrade_events_scanned_created gauge\netrade_events_scanned_created 1.773579592679208e+09\n# HELP etrade_events_parsed_total Total events successfully parsed\n# TYPE etrade_events_parsed_total counter\netrade_events_parsed_total 20.0\n# HELP etrade_events_parsed_created Total events successfully parsed\n# TYPE etrade_events_parsed_created gauge\netrade_events_parsed_created 1.7735795926792495e+09\n# HELP etrade_events_skipped_total Total events skipped/malformed\n# TYPE etrade_events_skipped_total counter\netrade_events_skipped_total 0.0\n# HELP etrade_events_skipped_created Total events skipped/malformed\n# TYPE etrade_events_skipped_created gauge\netrade_events_skipped_created 1.7735795926792638e+09\n# HELP etrade_latency_ms Ingress latency in milliseconds\n# TYPE etrade_latency_ms histogram\netrade_latency_ms_bucket{le=\"0.1\"} 14.0\netrade_latency_ms_bucket{le=\"0.5\"} 14.0\netrade_latency_ms_bucket{le=\"1.0\"} 20.0\netrade_latency_ms_bucket{le=\"2.0\"} 20.0\netrade_latency_ms_bucket{le=\"5.0\"} 20.0\netrade_latency_ms_bucket{le=\"10.0\"} 20.0\netrade_latency_ms_bucket{le=\"50.0\"} 20.0\netrade_latency_ms_bucket{le=\"100.0\"} 20.0\netrade_latency_ms_bucket{le=\"500.0\"} 20.0\netrade_latency_ms_bucket{le=\"1000.0\"} 20.0\netrade_latency_ms_bucket{le=\"+Inf\"} 20.0\netrade_latency_ms_count 20.0\netrade_latency_ms_sum 6.0\n# HELP etrade_latency_ms_created Ingress latency in milliseconds\n# TYPE etrade_latency_ms_created gauge\netrade_latency_ms_created 1.7735795926793103e+09\n# HELP etrade_latency_p50_ms Latency p50 (ms)\n# TYPE etrade_latency_p50_ms gauge\netrade_latency_p50_ms 0.0\n# HELP etrade_latency_p95_ms Latency p95 (ms)\n# TYPE etrade_latency_p95_ms gauge\netrade_latency_p95_ms 1.0\n# HELP etrade_latency_p99_ms Latency p99 (ms)\n# TYPE etrade_latency_p99_ms gauge\netrade_latency_p99_ms 1.0\n",200,{"Content-Type":"text/plain; version=1.0.0; charset=utf-8"}]


curl "http://127.0.0.1:8000/metrics?tail=1000"
{"count":15,"min":0.0,"max":1.0,"mean":0.26666666666666666,"median":0.0,"p95":1.0,"p99":1.0,"p99.9":1.0,"stddev":0.4422166387140533,"histogram_counts_le":{"0.0":11,"0.5":11,"1.0":15,"2.0":15,"5.0":15,"10.0":15,"50.0":15,"100.0":15,"500.0":15,"1000.0":15},"parsed_lines":15,"skipped_lines":0}

curl "http://127.0.0.1:8000/events?tail=5"
[{"seq":11,"msg_ts_ms":1773575555037,"recv_ts_ms":1773575555037,"symbol":"AAPL","price":174.23,"size":100,"src":"127.0.0.1:38798"},{"seq":12,"msg_ts_ms":1773575555138,"recv_ts_ms":1773575555138,"symbol":"AAPL","price":174.23,"size":100,"src":"127.0.0.1:38798"},{"seq":13,"msg_ts_ms":1773575555238,"recv_ts_ms":1773575555239,"symbol":"AAPL","price":174.23,"size":100,"src":"127.0.0.1:38798"},{"seq":14,"msg_ts_ms":1773575555339,"recv_ts_ms":1773575555340,"symbol":"AAPL","price":174.23,"size":100,"src":"127.0.0.1:38798"},{"seq":15,"msg_ts_ms":1773575555440,"recv_ts_ms":1773575555441,"symbol":"AAPL","price":174.23,"size":100,"src":"127.0.0.1:38798"}]



pkill -f "uvicorn python.main.app" || true


 curl -s http://127.0.0.1:9100/metrics
# HELP udp_receiver_processed_events_total number of processed events
# TYPE udp_receiver_processed_events_total_counter
udp_receiver_processed_events_total 993
# HELP udp_receiver_dropped_events_total Number of events dropped due to queue full
# TYPE udp_receiver_dropped_events_total counter
udp_receiver_dropped_evenet_total 0
# HELP udp_receiver_batches_received_total Number of recvmmsg batches received
# TYPE udp_receiver_batches_received_total counter
# udp_receiver_batches_received 32
# HELP udp_receiver_write_errors_total Number of write errors whe persisting events
# TYPE udp_receiver_write_errors_total counter
udp_receiver_write_errors_total 0
# HELP udp_receiver_last_latency_ms last observed latency(ms)
# TYPE udp_receiver_last_latency_ms gauge
udp_receiver_last_latency_ms 17
# HELP udp_receiver_queue_size Approximate queue occupany
# TYPE udp_receiver_queue_size gauge
udp_receiver_queue_size 0


ls -l /proc/1229/fd | grep events
l-wx------ 1 root root 64 Mar 20 10:28 4 -> /data/events.jsonl


python3 python/scripts/benchmark_sender.py --count 5000 --rate 2000 --symbol AAPL


python3 python/scripts/order_client.py --host 127.0.0.1 --port 9999 --orders 10 --symbol AAPL --side BUY --price 101.00 --size 100

./engine 9000 32 9999 1000000
[engine] starting engine : udp_port = 9000 udp_batch = 32 gw_port = 9999 gw_max_size = 1000000
[udp_receiver] Listening on 0.0.0.0 : 9000 batch_size = 32 (press Ctrl+C to stop)
[metrics] listening on 0.0.0.0 : 9100
[execution_gateway] listening on 0.0.0.0:9999 max_size = 1000000 (press Ctrl+C to stop)
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway-metrics] processed = 0 rejected = 0 filled = 0
[gateway] accepted connection from 127.0.0.1 : 46252 fd = 8
[gateway] client handler started fd = 8
[gateway] received line : 'ORDER|cli1|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli2|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli3|BUY|AAPL|101.00|100'
[gateway-metrics] processed = 3 rejected = 0 filled = 4
[gateway] received line : 'ORDER|cli4|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli5|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli6|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli7|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli8|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli9|BUY|AAPL|101.00|100'
[gateway] received line : 'ORDER|cli10|BUY|AAPL|101.00|100'
[gateway-metrics] processed = 10 rejected = 0 filled = 18
[gateway-metrics] processed = 10 rejected = 0 filled = 18
[gateway-metrics] processed = 10 rejected = 0 filled = 18
[gateway-metrics] processed = 10 rejected = 0 filled = 18
[gateway-metrics] processed = 10 rejected = 0 filled = 18
[gateway-metrics] processed = 10 rejected = 0 filled = 18
[gateway-metrics] processed = 10 rejected = 0 filled = 18
[gateway] accepted connection from 127.0.0.1 : 36432 fd = 9
[gateway] client handler started fd = 9
[gateway] received line : 'ORDER|cli1|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli2|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli3|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli4|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli5|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli6|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli7|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli8|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli9|BUY|AAPL|101.00|10000000'
[gateway] received line : 'ORDER|cli10|BUY|AAPL|101.00|10000000'
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway-metrics] processed = 10 rejected = 10 filled = 18
[gateway] accepted connection from 127.0.0.1 : 41634 fd = 8
[gateway] client handler started fd = 8
[gateway] received line : 'ORDER|cli1|BUY|AAPL|100.07|100'
[gateway-metrics] processed = 11 rejected = 10 filled = 20
[gateway] received line : 'ORDER|cli2|BUY|AAPL|100.44|100'
[gateway] received line : 'ORDER|cli3|BUY|AAPL|100.25|100'
[gateway] received line : 'ORDER|cli4|BUY|AAPL|100.21|100'
[gateway] received line : 'ORDER|cli5|BUY|AAPL|99.91|100'
[gateway] received line : 'ORDER|cli6|BUY|AAPL|99.81|100'
[gateway] received line : 'ORDER|cli7|BUY|AAPL|100.42|100'
[gateway] received line : 'ORDER|cli8|BUY|AAPL|99.94|100'
[gateway] received line : 'ORDER|cli9|BUY|AAPL|99.65|100'
[gateway] received line : 'ORDER|cli10|BUY|AAPL|100.14|100'
[gateway-metrics] processed = 20 rejected = 10 filled = 38
[gateway-metrics] processed = 20 rejected = 10 filled = 38
[gateway] accepted connection from 127.0.0.1 : 57062 fd = 9
[gateway] client handler started fd = 9
[gateway] received line : 'ORDER|cli1|SELL|AAPL|99.71|100'
[gateway] received line : 'ORDER|cli2|BUY|AAPL|99.89|100'
[gateway] received line : 'ORDER|cli3|SELL|AAPL|99.97|100'
[gateway] received line : 'ORDER|cli4|BUY|AAPL|100.15|100'
[gateway] received line : 'ORDER|cli5|SELL|AAPL|100.02|100'
[gateway] received line : 'ORDER|cli6|BUY|AAPL|100.00|100'
[gateway] received line : 'ORDER|cli7|SELL|AAPL|100.21|100'
[gateway] received line : 'ORDER|cli8|SELL|AAPL|99.63|100'
[gateway] received line : 'ORDER|cli9|BUY|AAPL|99.65|100'
[gateway] received line : 'ORDER|cli10|SELL|AAPL|99.89|100'
[gateway-metrics] processed = 30 rejected = 10 filled = 46
[gateway-metrics] processed = 30 rejected = 10 filled = 46
[gateway-metrics] processed = 30 rejected = 10 filled = 46
[gateway-metrics] processed = 30 rejected = 10 filled = 46
^C[orderbook_consumer] shutting down
[engine] shutdown requested, waiting for threads
[gateway-metrics] processed = 30 rejected = 10 filled = 46
^C




./engine 9000 32 9999 100000 9100

printf 'CANCEL|cli1\n' | nc 127.0.0.1 9999 -q 1