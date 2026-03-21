// Minimal UDP receiver for Milestone 1 ( Equities L2 pipeline )
// - Binds to an IPv4 UDP port
// - Receives datagram ( max 64kb ), timestamps arrival, and prints metadata
// 
// updated :
//     udp_receiver with simple parser + JSON line writer
//     - Receives text messages of the form : <seq>|<iso-ts-ms>|<SYM>|<price>|<size>
//         eg "1|16788800000000|AAPL|173.42|100"
//     - Parses fields, records receive timestamp, and appends a JSON object per message to workspace/data/events.jsonl
//     - latency_ms = recv_ts_ms - msg_ts_ms

// integrated order_book whihc applies upte and prints top of book updates

// Now uses SPSCQueue<Event> instead of TSQueue (mutex+condvar)
// - Producer will drop events if the queue is full ( and lof a short warning )
// - Consumer polls the queue and uses a small backoff when empty

// - now takes optinal recvmmsg bactchin (Linux). Usage ;
//     ./udp_receiver [port] [batch_size]

// - batch_size == 1 (default) : singe recvfrom loop (no recvmmsg)
// - batch_size > 1: attemp recvmmsg batches of up to batch_size packets (Linux)

// + metrics endpoint

#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <queue>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/uio.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <functional>
#include <cstdio>           // FILE*, fopen, snprintf, fwrite, setvbuf
#include <memory>
#include <cerrno>
#include <filesystem>
#include <atomic>
#include <netdb.h>
#include <signal.h>
#include <sys/time.h>

#include "../include/event.h"
#include "../include/order_book.h"
#include "../include/spsc_queue.h"



// stop flag
static volatile std::sig_atomic_t keep_running = 1;

void handle_signal(int) { 
    keep_running = 0;
    std::cerr << "[signal] SIGINT received, shutting down\n";
}


// Metrics ( atomic )
static std::atomic<uint64_t> m_processed_events{0};
static std::atomic<uint64_t> m_dropped_events{0};
static std::atomic<uint64_t> m_batches_received{0};
static std::atomic<uint64_t> m_write_errors{0};
static std::atomic<long long> m_last_latency_ms{-1};

// pointer to queue for runtime gauge sampling (set in main)
static SPSCQueue<Event>* g_queue_ptr = nullptr;

// simple metrics HTTP server 
void metrics_server(unsigned short port) {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if(srv < 0) {
        std::perror("metrics socket");
        return;
    }
    int one =1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("metrics bind");
        close(srv);
        return;
    }

    if(listen(srv, 8) < 0) {
        std::perror("metrics listen");
        close(srv);
        return;
    }

    std::cout << "[metrics] listening on 0.0.0.0 : " << port << std::endl;

    // accept loop
    while(keep_running) {
        sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int c = accept(srv, reinterpret_cast<sockaddr*>(&peer), &plen);
        if(c < 0){
            if (errno == EINTR) continue;
            std::perror("metrics accept");
            break;
        }

        // read a small request (we dont full parse)
        char reqbuf[1024];
        ssize_t r = ::recv(c, reqbuf ,sizeof(reqbuf)-1, 0);
        (void)r;

        // build metrics response
        std::ostringstream out;
        out << "# HELP udp_receiver_processed_events_total number of processed events\n";
        out << "# TYPE udp_receiver_processed_events_total_counter\n";
        out << "udp_receiver_processed_events_total " << m_processed_events.load() << "\n";
        out << "# HELP udp_receiver_dropped_events_total Number of events dropped due to queue full\n";
        out << "# TYPE udp_receiver_dropped_events_total counter \n";
        out << "udp_receiver_dropped_evenet_total " << m_dropped_events.load() << "\n";
        out << "# HELP udp_receiver_batches_received_total Number of recvmmsg batches received\n";
        out << "# TYPE udp_receiver_batches_received_total counter\n";
        out << "# udp_receiver_batches_received " << m_batches_received.load() << "\n";
        out << "# HELP udp_receiver_write_errors_total Number of write errors whe persisting events\n";
        out << "# TYPE udp_receiver_write_errors_total counter\n";
        out << "udp_receiver_write_errors_total " << m_write_errors.load() << "\n";

        long long last_lat = m_last_latency_ms.load();
        out << "# HELP udp_receiver_last_latency_ms last observed latency(ms)\n";
        out << "# TYPE udp_receiver_last_latency_ms gauge\n";
        out << "udp_receiver_last_latency_ms " << (last_lat >= 0 ? last_lat : 0) << "\n";

        // queue size gauge
        size_t qsz = 0;
        if (g_queue_ptr) qsz = g_queue_ptr->approx_size();
        out << "# HELP udp_receiver_queue_size Approximate queue occupany\n";
        out << "# TYPE udp_receiver_queue_size gauge\n";
        out << "udp_receiver_queue_size " << qsz << "\n";

        std::string body = out.str();
        std::ostringstream hdr;
        hdr << "HTTP/1.1 200 OK\r\n"
            << "Content-Type: text/plain; version=0.0.4; charset=utf-8\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Connection: Close\r\n\r\n";
        std::string resp = hdr.str() + body;

        ssize_t to_write = static_cast<ssize_t>(resp.size());
        const char* p = resp.c_str();
        while(to_write > 0) { 
            ssize_t w = ::send(c, p, static_cast<size_t>(to_write), 0);
            if(w < 0) {
                if(errno == EINTR) continue;
                break;
            }
            to_write -= w;
            p += w;
        }
        close(c);
    }

    close(srv);
    std::cout << "[metrics] server exiting\n";
}

// small thread-safe queue unsing mutex + condvar
template <typename T>
class TSQueue {

public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            q_.push(std::move(item));
        }

        cv_.notify_one();
    }

    // Blocks until an item is available or shutdown is signalled
    bool pop(T &out){
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [this]() { return !q_.empty() || shutdown_; });
        if(q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

    void notify_shutdown() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            shutdown_ = true;
        }
        cv_.notify_all();
    }

private:
    std::queue<T> q_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool shutdown_ = false;
    
};


//  split a string_view by a single char delimiter into tokens
static std::vector<std::string_view> split_sv( std::string_view sv, char delim){
    std::vector<std::string_view> out;
    size_t pos = 0;
    while(pos <= sv.size()){
        size_t next = sv.find(delim, pos);
        if(next == std::string_view::npos){
            out.emplace_back(sv.substr(pos));
            break;
        }else{
            out.emplace_back(sv.substr(pos, next - pos));
            pos = next+1;
        }
    }
    
    return out;
}

// Convert string_view to integer with simple error check
bool to_ll(std::string_view sv, long long &out){
    if(sv.empty()) return false;
    long long v = 0;
    bool neg = false;
    size_t i = 0;
    if(sv[0] == '-') { 
        neg = true; 
        ++i; 
        if (i == sv.size()){
            return false;
        }
    }
    for(;i < sv.size(); ++i) {
        char c = sv[i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c-'0');
    }
    out = neg ? -v : v;
    return true;
}

// Convert string_view to double with basic parsing ( use stood fallback if needed )
bool to_double(std::string_view sv, double &out) {
    try{
        std::string tmp(sv);
        out = std::stod(tmp);
        return true;
    }catch(...){
        return false;
    }
}

// Consumer thread : pop events and forward to OrderBook instance
void orderbook_consumer(SPSCQueue<Event>& q, OrderBook& book, size_t empty_spin_limit){
    Event ev;
    size_t spin = 0;
    while(true) {
        while(q.pop(ev)) {
            // apply all available items
            book.apply_event(ev);
            spin = 0;
        }
        // if shutdown requested and queue empty, exit
        if(q.is_shutdown() && q.empty()) break;

        // backoff strategy: spin a few time then sleep to avoi busy loop
        if(spin < empty_spin_limit) {
            ++spin;
            std::this_thread::yield();
        }else{
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }
    
    std::cout << "[orderbook_consumer] shutting down\n";
}

// Robust writev loop : continue until all bytes represented by iovecs are written
// advances through iovecs on partial writes
static bool writev_all(int fd, iovec* iov, int iovcnt) {
    // compute total bytes
    size_t total = 0;
    for(int i = 0; i < iovcnt; ++i) total += iov[i].iov_len;
    size_t written_total = 0;
    int idx = 0;
    size_t offset = 0;

    while(written_total < total) {
        // prepare iovec slice starting at idx / offset
        iovec tmp[64]; // stack buffer for a chunk of iovecs, 64 should be enough for batches, fall if larger
        int tmpcnt = 0;
        for(int i = idx; i < iovcnt && tmpcnt < 64; ++i) {
            const char* base = static_cast<const char*>(iov[i].iov_base);
            size_t len = iov[i].iov_len;
            if(i == idx && offset > 0) {
                base += offset;
                len -= offset;
            }
            tmp[tmpcnt].iov_base = const_cast<char*>(base);
            tmp[tmpcnt].iov_len = len;
            ++tmpcnt;
        }

        ssize_t w = ::writev(fd, tmp, tmpcnt);
        if(w < 0) {
            if (errno == EINTR) continue;
            return false;
        }

        written_total += static_cast<size_t>(w);
        size_t remaining = static_cast<size_t>(w);
        while(remaining > 0 && idx < iovcnt) {
            size_t cur_avail = iov[idx].iov_len - offset;
            if(remaining < cur_avail) {
                offset += remaining;
                remaining = 0;
            }else {
                remaining -= cur_avail;
                ++idx;
                offset = 0;
            }
        }

    }
    return true;
}

// helper: hex print first N bytes
std::string hex_preview(const uint8_t* data, size_t len, size_t preview = 16){
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    size_t show = std::min(len, preview);
    for(size_t i = 0; i < show; ++i){
        oss << std::setw(2) << static_cast<int>(data[i]);
        if (i + 1 < show) oss << ' ';
    }
    if(len > show) oss << " ...";
    return oss.str();
}

int main(int argc, char* argv[]){
    // Usage: usp_receiver [port]
    int port = 9000;
    size_t batch_size = 1;
    if(argc >= 2) port = std::stoi(argv[1]);
    if(argc >= 3) batch_size = static_cast<size_t>(std::stoi(argv[2]));
    if(batch_size < 1) batch_size = 1;

    // Register SIGINT handler to allow graceful shutdown
    // std::signal(SIGINT, handle_signal);
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    sa.sa_flags = 0;
    if(sigaction(SIGINT, &sa, nullptr) < 0){
        std::perror("sigaction");
    }

    // Create UDP socket (IPv4)
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        std::perror("socket");
        return 1;
    }

    struct timeval rto;
    rto.tv_sec = 0;
    rto.tv_usec = 200000; // 200ms
    if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto)) < 0) {
        std::perror("setsockopt SO_RCVTIMEO");
    }


    // Allow quick reuse of address/port 
    int one = 1;
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0){
        std::perror("setsockopt SO_REUSEADDR");
    }

    // Increase receive buffer moderately
    int rcvbuf = 4 * 1024 * 1024; // 4MB
    if(setsockopt( sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0){
        std::perror("setsockopt SO_RCVBUF");
    }

    // Bind to all interfaces on specified port
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if(bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(sock);
        return 1;
    }

    std::cout << "[udp_receiver] Listening on 0.0.0.0 : " << port << " batch_size = " << batch_size << " (press Ctrl+C to stop)\n";

    // Preallocate buffer to avoid reallocation in loop
    std::vector<uint8_t> buffer(65536);

    // Open output file in append mode. we flush after every write to ensure visibility on host fs
    const char* out_path = "../../data/events.jsonl";
    
    // ensure parent directory exists
    try {
        std::filesystem::create_directories("../../data");
        
    }catch(const std::exception &e) {
        std::cerr << "[error] create directories(../../data) failed : " << e.what() << std::endl;
    }


    int out_fd = open(out_path, O_CREAT | O_APPEND | O_WRONLY, 0644);
    if(out_fd < 0){
        int err = errno;
        std::cerr << "[error] open " << out_path << " : " << std::strerror(err) << " (errno = " << err << ")\n"; 
        close(sock);
        return 1;
    }

    // Instantiate SPSC queue and orderbook, start consumer thread
    const size_t queue_capacity = 65536; // larger to avoid drops
    SPSCQueue<Event> queue(queue_capacity);
    OrderBook book(5); // keep 5 levels per symbol for demo
    // std::thread consumer_thread(orderbook_consumer, std::ref(queue), std::ref(book));
    //  error: static assertion failed: std::thread arguments must be invocable after conversion to rvalues
    // cmpiler had triuble instantiating std::thread call, missing <funcational> in toolchain
    // we can replace std::ref wuth a small lambda that captures refernces
    std::thread consumer_thread([&queue, &book]() {
        orderbook_consumer(queue, book, /*empty_sin_limit=*/100);
    });


    // start metrics server thread
    std::thread metrics_thread([](){ metrics_server(9100); });

    // reusable buffer for JSON line
    char tmp_buf[512];

#if defined(__linux__)
    // Prepare recvmmsg structure if batch_size > 1
    const size_t MAX_BATCH = batch_size;
    std::vector<std::vector<char>> batch_bufs;
    std::vector<iovec> iovecs;
    std::vector<mmsghdr> msgs;
    std::vector<sockaddr_in> addrs;
    std::vector<unsigned int> addr_lens;

    if(batch_size > 1){
        batch_bufs.resize(MAX_BATCH);
        iovecs.resize(MAX_BATCH);
        msgs.resize(MAX_BATCH);
        addrs.resize(MAX_BATCH);
        addr_lens.resize(MAX_BATCH);

        for(size_t i = 0; i < MAX_BATCH; ++i) {
            batch_bufs[i].resize(8192); // per packet buffer 
            iovecs[i].iov_base  = batch_bufs[i].data();
            iovecs[i].iov_len = batch_bufs[i].size();
            std::memset(&msgs[i], 0, sizeof(mmsghdr));
            msgs[i].msg_hdr.msg_iov = &iovecs[i];
            msgs[i].msg_hdr.msg_iovlen = 1;
            msgs[i].msg_hdr.msg_name = &addrs[i];
            msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
        }
    }
#endif

    // main receive loop: blocking recvfrom for now
    // eithe batched recvmmsg ( linux ) or recvfrom
    while(keep_running){
#if defined(__linux__)
        if(batch_size > 1) {
            // short timeout so we dont block too long when traffice is low
            struct timespec timeout;
            timeout.tv_sec = 0;
            timeout.tv_nsec = 100000000; // 100ms

            int received = recvmmsg(sock, msgs.data(), static_cast<unsigned int>(MAX_BATCH), 0, &timeout);

            if(received < 0){
                if(errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                // std::perror("recvmmsg");
                std::cerr << "[error] recvmmdg failed errno = " << errno << " (" << std::strerror(errno) << ")\n";

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
                // fallback to single recvfrom once
                // received = 0;

            }

            if(received == 0){
                continue; // timeout or no data
            }
            m_batches_received.fetch_add(1, std::memory_order_relaxed);

            // collects events and JSON lines
            std::vector<Event> events;
            events.reserve(received);
            std::string batched;
            batched.reserve(received * 128);

            // prepare per-line buffers and iovec array for wtitev
            std::vector<std::string> lines;
            lines.reserve(received);
            std::vector<iovec> write_iovs;
            write_iovs.reserve(received);

            for(int i = 0 ; i < received; ++i) {
                ssize_t n = msgs[i].msg_len;
                sockaddr_in *src = reinterpret_cast<sockaddr_in*>(msgs[i].msg_hdr.msg_name);

                auto recv_time = std::chrono::system_clock::now();
                auto ms_tp = std::chrono::time_point_cast<std::chrono::milliseconds>(recv_time);

                long long recv_ts_ms = ms_tp.time_since_epoch().count();

                char src_ip[INET_ADDRSTRLEN] = "";
                inet_ntop(AF_INET, &src->sin_addr, src_ip, sizeof(src_ip));
                uint16_t src_port = ntohs(src->sin_port);

                std::string_view sv(batch_bufs[i].data(), static_cast<size_t>(n));
                auto tokens = split_sv(sv, '|');

                if(tokens.size() != 5) {
                    std::cerr << "[parse error] expected 5 fields but got " << tokens.size()
                            << " from " << src_ip << " : " << src_port << std::endl;
                    continue;
                }

                long long seq = 0;
                if(!to_ll(tokens[0], seq)){
                    std::cerr << "[parse error] invalid seq : '" << std::string(tokens[0])
                            << "' from " << src_ip << " : " << src_port << std::endl;
                    continue;
                }

                long long msg_ts = 0;
                if(!to_ll(tokens[1], msg_ts)){
                    std::cerr << "[parse error] invalid msg_ts : '" << std::string(tokens[1])
                            << "' from " << src_ip << " : " << src_port << std::endl;
                    continue;
                }

                // symbol token -> copy into fized buffer
                std::string_view sym_sv = tokens[2];
                double price = 0.0;
                if(!to_double(tokens[3], price)){
                    std::cerr << "[parse error] invalid price : '" << std::string(tokens[3])
                            << "' from " << src_ip << " : " << src_port << std::endl;
                    continue;
                }

                long long size_v = 0;
                if(!to_ll(tokens[4], size_v)){
                    std::cerr << "[parse error] invalid size : '" << std::string(tokens[4])
                            << "' from " << src_ip << " : " << src_port << std::endl;
                    continue;
                }

                // compute latency in milliseconds: recv timestamp - message timestamp
                long long latency_ms = recv_ts_ms - msg_ts;
                m_last_latency_ms.store(latency_ms, std::memory_order_relaxed);

                // Build Event object
                Event ev;
                ev.seq = seq;
                ev.msg_ts_ms = msg_ts;
                ev.recv_ts_ms = recv_ts_ms;
                ev.latency_ms = latency_ms;
                ev.set_symbol(sym_sv.data(), sym_sv.size());

                // set src into fixed buffer
                char src_merged[64];
                int src_len_written = snprintf(src_merged, sizeof(src_merged), "%s:%u", src_ip, src_port);
                ev.set_src(src_merged, std::min<int>(src_len_written, (int)sizeof(src_merged)-1));


                ev.price = price;
                ev.size = size_v;


                // Assemble Json line into tmp_buf using sprintf
                int wrote = snprintf(tmp_buf, sizeof(tmp_buf),
                    "{\"seq\":%lld,\"msg_ts_ms\":%lld,\"recv_ts_ms\":%lld,\"latency_ms\":%lld,\"symbol\":\"%s\",\"price\":%.2f,\"size\":%lld,\"src\":\"%s\"}\n",
                    ev.seq, ev.msg_ts_ms, ev.recv_ts_ms, ev.latency_ms,
                    ev.symbol, ev.price, ev.size, ev.src);

                if(wrote > 0){
                    lines.emplace_back(tmp_buf, static_cast<size_t>(wrote));
                    iovec iov;
                    iov.iov_base = const_cast<char*>(lines.back().data());
                    iov.iov_len = lines.back().size();
                    write_iovs.push_back(iov);
                }else {
                    std::cerr << "[error] snprintf failed for seq = " << seq << std::endl;
                }
                events.push_back(std::move(ev));

                // // push to SPSC queue for the orderbook, drop if full
                // if(!queue.push(std::move(ev))) {
                //     std::cerr << "[queue][warn] full, dropping seq = " << seq
                //             << " symbol = " << sym_sv << std::endl;
                // }else {
                //     std::cout << "[queue] pushed seq = " << seq << " symbol = " << ev.symbol << std::endl;
                // }

                // std::cout << "[" << recv_ts_ms << "] seq = " << seq << " " << ev.symbol << " latency_ms = " << latency_ms << std::endl;
 
            }
            // write per-line iovecs via writev_all
            if(!write_iovs.empty()) {
                if(!writev_all(out_fd, write_iovs.data(), static_cast<int>(write_iovs.size()))) {
                    std::perror("writev_all");
                    m_write_errors.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // write batched bytes in a single syscall
            // if(!batched.empty()){
            //     const char* ptr = batched.data();
            //     ssize_t to_write = static_cast<ssize_t>(batched.size());
            //     while(to_write > 0) {
            //         ssize_t w = ::write(out_fd, ptr, static_cast<size_t>(to_write));
            //         if(w < 0){
            //             if(errno == EINTR) continue;
            //             std::perror("write");
            //             m_write_errors.fetch_add(1, std::memory_order_relaxed);
            //             break;
            //         }
            //         to_write -= w;
            //         ptr += w;
            //     }
            // }

            // bulk push events into SPSC
            if(!events.empty()){
                // push_bulk moves fomr the events array
                size_t pushed = queue.push_bulk(events.data(), events.size());
                m_processed_events.fetch_add(pushed, std::memory_order_relaxed);
                if(pushed < events.size()) {
                    uint64_t dropped = static_cast<uint64_t>(events.size() - pushed);
                    m_dropped_events.fetch_add(dropped, std::memory_order_relaxed);
                    std::cerr << "[queue][warn] pushed " << pushed << " of " << events.size() << " events , dropping rest\n"; 
                }
            }
            // end batch processing
            continue;
        }
#endif

        // fallback to single recvfrom path ( or non-linux )
        sockaddr_in  src;
        socklen_t src_len = sizeof(src);

        // Record receive timestamp as close to recvfrom return as possible
        ssize_t n = recvfrom(sock, buffer.data(), buffer.size(), 0, 
                            reinterpret_cast<sockaddr*>(&src), &src_len);
        
        // read into a stack buffer
        std::vector<char> buf(8192);
        ssize_t got = recvfrom(sock, buf.data(), buf.size(), 0, reinterpret_cast<sockaddr*>(&src), &src_len);
        if(got < 0) {
            if(errno == EINTR) continue;
            std::perror("recvfrom");
            continue;
        }
        
        auto recv_time = std::chrono::system_clock::now();

        // convert timestamp t human -readable form with milliseconds
        auto ms_tp = std::chrono::time_point_cast<std::chrono::milliseconds>(recv_time);
        // auto epoch  = ms_tp.time_since_epoch();
        long long recv_ts_ms = ms_tp.time_since_epoch().count();

        // source IP & port
        char src_ip[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &src.sin_addr, src_ip, sizeof(src_ip));
        uint16_t src_port = ntohs(src.sin_port);

        // PArse message 
        std::string_view sv(reinterpret_cast<char*>(buffer.data()), static_cast<size_t>(n));
        auto tokens = split_sv(sv, '|');

        // Expect 5 tokens: seq | msg_ts_ms | symbol | price | size
        if(tokens.size() != 5){
            std::cerr << "[parse error] expected 5 fields but got " << tokens.size() 
                      << " from " << src_ip << " : " << src_port
                      << " preview = " << hex_preview(buffer.data(), static_cast<size_t>(n)) << std::endl;
            continue;
        }

        long long seq = 0;
        if(!to_ll(tokens[0], seq)){
            std::cerr << "[parse error] invalid seq : '" << std::string(tokens[0])
                      << "' from " << src_ip << " : " << src_port << std::endl;
            continue;
        }

        long long msg_ts = 0;
        if(!to_ll(tokens[1], msg_ts)){
            std::cerr << "[parse error] invalid msg_ts : '" << std::string(tokens[1])
                      << "' from " << src_ip << " : " << src_port << std::endl;
            continue;
        }

        // symbol token -> copy into fized buffer
        std::string_view sym_sv = tokens[2];
        double price = 0.0;
        if(!to_double(tokens[3], price)){
            std::cerr << "[parse error] invalid price : '" << std::string(tokens[3])
                      << "' from " << src_ip << " : " << src_port << std::endl;
            continue;
        }

        long long size_v = 0;
        if(!to_ll(tokens[4], size_v)){
            std::cerr << "[parse error] invalid size : '" << std::string(tokens[4])
                      << "' from " << src_ip << " : " << src_port << std::endl;
            continue;
        }

        // compute latency in milliseconds: recv timestamp - message timestamp
        long long latency_ms = recv_ts_ms - msg_ts;
        m_last_latency_ms.store(latency_ms, std::memory_order_relaxed);

        // Build Event object
        Event ev;
        ev.seq = seq;
        ev.msg_ts_ms = msg_ts;
        ev.recv_ts_ms = recv_ts_ms;
        ev.latency_ms = latency_ms;
        ev.set_symbol(sym_sv.data(), sym_sv.size());

        // set src into fixed buffer
        char src_merged[64];
        int src_len_written = snprintf(src_merged, sizeof(src_merged), "%s:%u", src_ip, src_port);
        ev.set_src(src_merged, std::min<int>(src_len_written, (int)sizeof(src_merged)-1));


        ev.price = price;
        ev.size = size_v;
        // Assemble Json line into tmp_buf using sprintf
        int wrote = snprintf(tmp_buf, sizeof(tmp_buf),
            "{\"seq\":%lld,\"msg_ts_ms\":%lld,\"recv_ts_ms\":%lld,\"latency_ms\":%lld,\"symbol\":\"%s\",\"price\":%.2f,\"size\":%lld,\"src\":\"%s\"}\n",
            ev.seq, ev.msg_ts_ms, ev.recv_ts_ms, ev.latency_ms,
            ev.symbol, ev.price, ev.size, ev.src);
        
        if(wrote > 0){
            const char* ptr = tmp_buf;
            ssize_t to_write = wrote;
            while(to_write > 0) { 
                ssize_t w = ::write(out_fd, ptr, static_cast<size_t>(to_write));
                if(w < 0) {
                    if(errno == EINTR) continue;
                    std::perror("write\n");
                    m_write_errors.fetch_add(1, std::memory_order_relaxed);
                    break;
                }
                to_write -= w;
                ptr += w;
            }

        }else {
            std::cerr << "[error] json snprintf failed for seq = " << seq << std::endl;
        }

        // // push to SPSC queue for the orderbook, drop if full
        // if(!queue.push(std::move(ev))) {
        //     std::cerr << "[queue][warn] full, dropping seq = " << seq
        //             << " symbol = " << sym_sv << std::endl;
        // }else {
        //     std::cout << "[queue] pushed seq = " << seq << " symbol = " << ev.symbol << std::endl;
        // }

        size_t pushed  = queue.push_bulk(&ev, 1);
        if(pushed > 0) {
            m_processed_events.fetch_add(1, std::memory_order_relaxed); 
            // std::cerr << "[queue][warn] pushed 0 of 1 events, dropping seq = " << ev.seq << "\n";
        }else{
            m_dropped_events.fetch_add(1, std::memory_order_relaxed);
            std::cerr << "[queue][warn] pushed 0 of 1 events dropping seq = " << ev.seq << "\n";
        }

        // Print a short line : timestamp ( ms), source, size, first byets ( hex )
        std::cout << "[ " << recv_ts_ms << " ] seq = " << seq << " " << ev.symbol
                  << " price = " << price
                  << " size = " << size_v
                  << " latency_ms = " << latency_ms
                  << std::endl;

    }

    queue.notify_shutdown();
    if(consumer_thread.joinable()) consumer_thread.join();

    // stop metrucs
    keep_running = 0;
    int wake = ::socket(AF_INET, SOCK_STREAM, 0);
    if(wake >= 0) {
        sockaddr_in maddr;
        std::memset(&maddr, 0, sizeof(maddr));
        maddr.sin_family = AF_INET;
        maddr.sin_port = htons(9100);
        inet_pton(AF_INET, "127.0.0.1", &maddr.sin_addr);
        connect(wake, reinterpret_cast<sockaddr*>(&maddr), sizeof(maddr));
        close(wake);
    }

    if(metrics_thread.joinable()) metrics_thread.join();

    if(out_fd >= 0){
        close(out_fd);
    }

    std::cout << "[udp_receiver] Shutting down\n";
    close(sock);
    return 0;

}