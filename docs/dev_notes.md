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