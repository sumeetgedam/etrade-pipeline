// Usage:
    // udp_bench_sender <host> <port> <pps> <payload_size> <count>
// udp_bench_sender 127.0.0.1 9000 10000 200 100000
// Sends <count> UDP packets at ~pps packets/sec, each packet payload begins with :
    // [8 bytes seq (big-endian)] [8 bytes send_ts_ns (big-endian)] [payload bytes ...]

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono;

static inline uint64_t htobe64_u64(uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(v);
#else
    return v;
#endif
}

int main(int argc, char** argv) {
    if(argc < 6) {
        std::cerr << "usage: udp_bench_sender <host> <port> <pps> <payload_size> <count>\n";
        return 2;
    }
    const char* host = argv[1];
    int port = std::stoi(argv[2]);
    double pps = std::stod(argv[3]);
    size_t payload_size = (size_t)std::stoul(argv[4]);
    uint64_t count = (uint64_t)std::stoull(argv[5]);

    if(payload_size < 16) payload_size = 16;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint64_t)port);
    if(inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        std::cerr << "invalid host\n";
        close(sock);
        return 1;
    }

    // optional: set SO_SNDBUF larger
    int sndbuf = 4 * 1024 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    std::vector<uint8_t> buf(payload_size);
    //filler payload
    for(size_t i = 16; i < payload_size; ++i){
        buf[i] = 0xAA;
    }
    double interval_us = 1e6 / pps;
    auto next = steady_clock::now();

    for(uint64_t seq = 1; seq <= count; ++seq) {
        uint64_t ts_ns = (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();

        uint64_t be_seq = htobe64_u64(seq);
        uint64_t be_ns = htobe64_u64(ts_ns);
        std::memcpy(buf.data(), &be_seq, 8);
        std::memcpy(buf.data()+8, &be_ns, 8);

        ssize_t w = sendto(sock, buf.data(), buf.size(), 0, (sockaddr*)&addr, sizeof(addr));
        if(w < 0) {
            if(errno == ENOBUFS) {
                // send buffer overflow, count drops
            }else{
                perror("sendto");
            }
        }
        
        next += microseconds((long long) interval_us);
        std::this_thread::sleep_until(next);
    }

    close(sock);
    std::cout << "[udp_bench_sender] done\n";
    return 0;
}