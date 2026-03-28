// Usage:
    // udp_latency_server  <listen_port> <out_csv>
    // example : udp_latency_server 9001 recv.csv
// Listens on UDP port and writes CSV lines:
    // seq,send_ns,recv_ns,latency_us

#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <vector>

using namespace std::chrono;

static volatile bool g_stop = false;
static void onint(int){ g_stop = true; }

static inline uint64_t be64to_u64(uint64_t v) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap64(v);
#else
    return v;
#endif
}

int main(int argc, char** argv) {
    if(argc < 3) {
        std::cerr << "usage : udp_latency_server <listen_port> <out_csv>\n";
        return 2;
    }
    int port = std::stoi(argv[1]);
    const char* outpath = argv[2];

    std::ofstream out(outpath, std::ios::out | std::ios::trunc);
    if(!out.is_open()) { std::perror("open out"); return 1; }

    out << "seq,send_ns,recv_ns,latency_us\n";

    signal(SIGINT, onint);

    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if(s < 0) {
        perror("socket");
        return 1;
    }
    int one =1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    // increase recv buf
    int rbuf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint64_t)port);

    if(bind(s, (sockaddr*)&addr, sizeof(addr)) < 0 ){
        perror("bind");
        close(s);
        return 1;
    }

    const size_t MAX =  65536;
    std::vector<char> buf(MAX);
    while(!g_stop) {
        ssize_t n = recv(s, buf.data(), MAX, 0);
        if(n <= 0) continue;
        uint64_t be_seq = 0, be_ts = 0;
        if(n >= 16 ){
            std::memcpy(&be_seq, buf.data(), 8);
            std::memcpy(&be_ts, buf.data()+ 8, 8);
            uint64_t seq = be64to_u64(be_seq);
            uint64_t send_ns = be64to_u64(be_ts);
            uint64_t recv_ns = (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
            uint64_t latency_us = (recv_ns > send_ns)?(recv_ns - send_ns)/1000 : 0;
            out << seq << "," << send_ns << "," << recv_ns << "," << latency_us << "\n";

        }
    }
    close(s);
    out.close();
    std::cout << "[udp_latency_server] exiting\n";
    return 0;
}