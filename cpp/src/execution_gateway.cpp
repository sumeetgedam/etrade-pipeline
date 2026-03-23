// execution_gate with OrderManager + Risj Chescks  _ CANCEL supprt
// Provides run_executoin_gateway(stop, port, max_size) and legacy main for stndalone


#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <atomic>
#include <cmath>
#include <limits>
#include <iomanip>
#include <signal.h>

#include "../include/order_book.h"
#include "../include/book_registry.h"
#include "../include/order_manager.h"
#include "../include/risk.h"

using namespace std::chrono_literals;

// static volatile std::sig_atomic_t keep_running = 1;

// void handle_sigint(int) { 
//     keep_running = 0; 
//     std::cerr << "[signal] SIGINT received, shutting down\n";
// }

// simple counters
static std::atomic<uint64_t> processed_orders{0};
static std::atomic<uint64_t> rejected_orders{0};
static std::atomic<uint64_t> filled_orders{0};

// conigurable limits
static constexpr long long DEFAULT_MAX_SIZE = 1'000'000;

// Helper to trim newline/CR
static inline std::string trim(const std::string &s) {
    size_t start = 0;
    while(start < s.size() && ( s[start] == '\r' || s[start] == '\n')) ++start;
    size_t end = s.size();
    while(end > start && (s[end-1] == '\r' || s[end-1] == '\n')) --end;

    return s.substr(start, end-start);
}

// Read top-of-book snapshot file ./data/book_<SYM>.json
// returns pair(best_bid, best_offer) if missing / parse fail : returns (NaN, NaN)
static std::pair<double, double> read_top_of_book(const std::string &sym) {
    const std::filesystem::path p = std::filesystem::path("./data") / ("book_" + sym + ".json");
    
    if(!std::filesystem::exists(p)) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};

    std::ifstream in(p);
    if(!in.is_open()) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // Very small ad0hoc parse : find first "levels" array, then first two entries to derive bid/ask;
    // our snapshot format stores levels as JSON array of {"price": X, "size": "Y"} with top-of-book first
    double top_price = std::numeric_limits<double>::quiet_NaN();
    // we dont strictly differentiate bid/ask in current snapshot
    // asume top level price the best available side for smplicity
    // to get both bid/ask requires snapshot to include size, 
    // keep simple : return same price for both

    auto pos = content.find("\"levels:\":");
    if(pos == std::string::npos) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};

    auto brace = content.find('[', pos);
    if(brace == std::string::npos) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    // find first price toke after brace
    auto price_pos = content.find("\"price\"", brace);
    if(price_pos == std::string::npos) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    
    auto colon = content.find(':', price_pos);
    if(colon == std::string::npos) return {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};

    size_t i = colon + 1;
    while((i < content.size()) && (content[i] == ' ' || content[i] == '\t')) ++i;
    std::string num;
    while((i < content.size()) && ((content[i] >= '0' && content[i] <= '9') || content[i] =='-' || content[i] == '.')) {
        num.push_back(content[i++]);
    }

    try {
        top_price = std::stod(num);
    }catch(...) {
        top_price = std::numeric_limits<double>::quiet_NaN();
    }

    return {top_price, top_price};
}


// handle a single client cnnection ( line - based protocol )
//  supports ORDER and CANCEL command
// ORDER|<cl_ord_id>|<side>|<symbol>|<price>|<size>\n
// CANCEL|<cl_ord_od>\n
void handle_client(std::atomic<bool> &stop, int fd, long long max_size, OrderManager* mgr) {
    std::cout << "[gateway] client handler started fd = " << fd << "\n";
    constexpr int BUF_SZ = 8192;
    std::string readbuf;
    readbuf.reserve(2048);
    char buf[BUF_SZ];

    struct timeval rto;
    rto.tv_sec = 0;
    rto.tv_usec = 200000; // 200ms
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rto, sizeof(rto));

    while(!stop.load(std::memory_order_acquire)) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        
        if(n == 0) break; // client closed
        if(n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK ){
                std::this_thread::sleep_for(10ms);
                continue;
            }
            break;
        }
        readbuf.append(buf, static_cast<size_t>(n));
        // process fill lines
        // size_t pos;
        while(true) {
            auto pos = readbuf.find('\n');
            if(pos == std::string::npos) break;
            std::string line = trim(readbuf.substr(0, pos + 1));
            readbuf.erase(0, pos + 1);
            if(line.empty()) continue;

            std::cout << "[gateway] received line : '" << line << "'\n";

            // parse parts
            std::vector<std::string> parts;
            std::istringstream iss(line);
            std::string token;
            while(std::getline(iss, token, '|')) parts.push_back(token);
            if(parts.empty()) continue;

            if(parts[0] == "ORDER"){
                if(parts.size() < 6 ) {
                    std::string resp = "REJ||bad_format\n";
                    ::send(fd, resp.data(), resp.size(), 0);
                    rejected_orders.fetch_add(1);
                    continue;
                }
                std::string clid = parts[1];
                std::string side = parts[2];
                std::string sym = parts[3];
                double price = 0.0;
                long long size = 0;
                try { price = std::stod(parts[4]); } catch(...) { price = NAN; }
                try { size = std::stoll(parts[5]); } catch(...) { size = LLONG_MIN; }
                
                // pre-trade risk checks
                if(size <= 0 || size == LLONG_MIN ) {
                    std::string resp = "REJ|" + clid + "|bad_size\n";
                    ::send(fd, resp.data(), resp.size(), 0);
                    rejected_orders.fetch_add(1);
                    continue;
                }

                if(size > max_size) {
                    std::string resp = "REJ|" + clid + "|size_exceeds_limit\n";
                    ::send(fd, resp.data(), resp.size(), 0);
                    rejected_orders.fetch_add(1);
                    continue;
                }

                // accept order
                std::string ack = "ACK|" + clid + "\n";
                ::send(fd, ack.data(), ack.size() , 0);

                // if in-prcess manager avaialble , use it
                // otherwise fallbakc to file sapshot logic
                if(mgr) {
                    OrderBook::Order in;
                    in.clid = clid;
                    in.side = (side=="BUY") ? OrderBook::Order::BUY : OrderBook::Order::SELL;
                    in.symbol = sym;
                    in.price = price;
                    in.size = size;
                    auto r = mgr->submit_order(in);
                    if(!r.accepted) {
                        std::string rej = "REJ|" + clid + "|" + r.reason + "\n";
                        ::send(fd, rej.data(), rej.size(), 0);
                        continue;
                    }

                    for(const auto &f: r.fills) {
                        std::ostringstream os;
                        os << "FILL|" << f.clid << "|" << f.size << "|" << std::fixed << std::setprecision(2) << f.price << "\n";
                        std::string fill = os.str();
                        ::send(fd, fill.data(), fill.size(), 0);
                    }
                }else{
                    // fallback existing file based top-of-book logic unchanged
                    // read top-of-book snapshot to decide an immediate fill
                    auto [best_bid, best_offer] = read_top_of_book(sym);
                    if(!std::isnan(best_bid) && !std::isnan(best_offer)) {
                        if(side == "BUY") {
                            if(!std::isnan(best_offer) && price >= best_offer) {
                                std::ostringstream os;
                                os << "FILL|" << clid << "|" << size << "|" << std::fixed << std::setprecision(2) << best_offer << "\n";
                                std::string fill = os.str();
                                ::send(fd, fill.data(), fill.size(), 0);
                                filled_orders.fetch_add(1);
                            }
                        }else{
                            if(!std::isnan(best_bid) && price <= best_bid) {
                                std::ostringstream os;
                                os << "FILL|" << clid << "|" << size << "|" << std::fixed << std::setprecision(2) << best_bid << "\n";
                                std::string fill = os.str();
                                ::send(fd, fill.data(), fill.size(), 0);
                                filled_orders.fetch_add(1);

                            }
                        }
                    }
                }
            }else if(parts[0] == "CANCEL") {
                // CANCEL|<cl_ord_id>
                if(parts.size() < 2) {
                    std::string resp = "REJ||bad_format\n";
                    ::send(fd, resp.data(), resp.size(), 0);
                    continue;
                }
                std::string clid = parts[1];
                if(!mgr) {
                    std::string resp = "REJ|" + clid + " |no_order_manager\n";
                    ::send(fd, resp.data(), resp.size(), 0);
                    continue;
                }
                int64_t canceled = 0;
                if (mgr->cancel_order(clid, canceled)) {
                    std::ostringstream os;
                    os << "CAL|" << clid << "|" << canceled << "\n";
                    std::string cxl = os.str();
                    ::send(fd, cxl.data(), cxl.size(), 0);

                }else{
                    std::string resp = "REJ|" + clid + "|not_found_or_filled\n";
                    ::send(fd, resp.data(), resp.size(), 0);

                }
            }else {
                std::string resp = "REJ||unknown_cmd\n";
                ::send(fd, resp.data(), resp.size(), 0);
            }
            
        }
    }
    close(fd);
}

// Periodically print counters to stdout  ( helpful while debugging)
void metrics_printer(std::atomic<bool> &stop) {
    while(!stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "[gateway-metrics] processed = " << processed_orders.load()
                << " rejected = " << rejected_orders.load()
                << " filled = " << filled_orders.load() << std::endl;

    }
}



int run_execution_gateway(std::atomic<bool> &stop, int port, long long max_size) {
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if(srv < 0) { 
        std::perror("socket"); 
        return 1; 
    }
    int one =1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint64_t>(port));

    if(bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        close(srv);
        return 1;
    }
    
    if(listen(srv, 16) < 0) {
        std::perror("listen");
        close(srv);
        return 1;
    }

    std::cout << "[execution_gateway] listening on 0.0.0.0:" << port
                << " max_size = " << max_size << " (press Ctrl+C to stop)\n";


    std::thread print_thread([&stop](){ metrics_printer(stop); });

    // If an in-process order book is available
    // create manager with policy
    OrderBook* ob = get_global_order_book();
    std::unique_ptr<OrderManager> mgr;
    if(ob) {
        RiskPolicy policy;
        policy.max_open_orders = 100000;
        policy.max_order_size = static_cast<size_t>(max_size);

        mgr.reset(new OrderManager(ob, policy));
        std::cout << "[gateway] using in-process OrderBook with OrderMAnager\n";
    }else{
        std::cout << "[gateway] no in process orderBook; using snapsht fallback\n";
    }

    while(!stop.load(std::memory_order_acquire)) {
        sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int c = accept(srv, reinterpret_cast<sockaddr*>(&peer), &plen);
        if ( c < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(10ms);
                continue;
            }
            std::perror("accept");
            break;
        }

        char peer_ip[INET_ADDRSTRLEN] = "unknown";
        inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
        uint16_t peer_port = ntohs(peer.sin_port);
        std::cout << "[gateway] accepted connection from " << peer_ip << " : " << peer_port << " fd = " << c << "\n";

        // spawn client handler thread
        std::thread t(handle_client, std::ref(stop), c, max_size, mgr.get());
        t.detach();
    }

    // keep_running = 0;
    close(srv);

    // if(print_thread.joinable()) print_thread.join();
    std::cout << "[execution_gateway] exiting\n";
    return 0;
}

#ifndef ENGINE_BUILD
int main(int argc, char* argv[]){
    int port = 9999;
    long long max_size = DEFAULT_MAX_SIZE;
    if(argc >= 2) port = std::stoi(argv[1]);
    if(argc >= 3) max_size = std::stoll(argv[2]);

    std::atomic<bool> stop(false);
    // std::signal(SIGINT, handle_sigint);
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int){};
    sigemptyset(&sa.sa_mask);
    // Important do not set SA_RESTART so blocking syscalls return EINTR
    sa.sa_flags = 0;
    if(sigaction(SIGINT, &sa, nullptr) < 0) {
        std::perror("sigaction");
    }
    
    return run_execution_gateway(stop, port, max_size);
    
}
#endif