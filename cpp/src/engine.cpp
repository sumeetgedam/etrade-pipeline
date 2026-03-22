#include <csignal>
#include <cstring>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "../include/order_book.h"
#include "../include/book_registry.h"


// forward declaration of run functions from the other translation units
int run_udp_receiver(std::atomic<bool> &stop, int port, size_t batch_size);
int run_execution_gateway(std::atomic<bool> &stop, int port, long long max_size);

static std::atomic<bool> g_stop{false};

// poke the listening socket so blocked accept/recv calls wake up
// - send a UDP packet to usp_port
// - connect to gw_port (TCP) and close
// - connect to metrics_port (TCP) and close
static void wake_services(int udp_port, int gw_port, int metrics_port) {
    // UDP_poke
    if(udp_port > 0){
        int s = socket(AF_INET, SOCK_DGRAM, 0);
        if(s >= 0){
            struct sockaddr_in addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<uint16_t>(udp_port));
            inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
            const char *msg = "wake";
            sendto(s, msg, strlen(msg), 0, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
            close(s);
        }
    }

    auto tcp_poke = [](int port) {
        if(port <= 0) return;
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if(s < 0) return;
        struct sockaddr_in serv;
        std::memset(&serv, 0, sizeof(serv));
        serv.sin_family = AF_INET;
        serv.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &serv.sin_addr);
        // set non-blocking connect timeout to avoid hanging
        // try connet and ignore error
        connect(s, reinterpret_cast<struct sockaddr*>(&serv), sizeof(serv));

        const char *msg = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        send(s, msg, strlen(msg), 0);
        close(0);
    };

    tcp_poke(gw_port);
    tcp_poke(metrics_port);
}

void handle_sigint(int) {
    g_stop.store(true , std::memory_order_release);
    std::cerr << "[engine] SIGINT received, shutting down\n";
}

int main(int argc, char* argv[]) {
    // simple arg parsing for engine
    int udp_port = 9000;
    size_t udp_batch = 32;
    int gw_port = 9999;
    long long gw_max_size = 1000000;
    int metrics_port = 9100;

    if(argc >= 2) udp_port = std::stoi(argv[1]);
    if(argc >= 3) udp_batch = static_cast<size_t>(std::stoi(argv[2]));
    if(argc >= 4) gw_port = std::stoi(argv[3]);
    if(argc >= 5) gw_max_size = std::stoll(argv[4]);
    if(argc >= 6) metrics_port = std::stoi(argv[5]);

    // install aigaction for SIGINT that does not set SA_RESTART
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = [](int) { g_stop.store(true, std::memory_order_release); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGINT, &sa, nullptr) < 0) {
        std::perror("sigaction");
    }

    std::cout << "[engine] starting engine : udp_port = " << udp_port
                << " udp_batch = " << udp_batch
                << " gw_port = " << gw_port
                << " gw_max_size = " << gw_max_size 
                << " metrics port = " << metrics_port
                << "\n";

    // construct the shared OrderBook and register globally
    static OrderBook shared_book(10);
    set_global_order_book(&shared_book);

    // start the UDP receiver and gateway in threads
    std::thread udp_thread([&]() {
        try
        {
            run_udp_receiver(g_stop, udp_port, udp_batch);
        }
        catch(const std::exception& e)
        {
            std::cerr << "[engine] udp thread exception : " << e.what() << '\n';
            g_stop.store(true, std::memory_order_release);
        }
        
    });

    std::thread gw_thread([&]() {
        try
        {
            run_execution_gateway(g_stop, gw_port, gw_max_size);
        }
        catch(const std::exception& e)
        {
            std::cerr << "[engine] gateway thread exception : " << e.what() << '\n';
            g_stop.store(true, std::memory_order_release);
        }
        
    });

    // main loop
    while(!g_stop.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cerr << "[engine] shutdown requested, waiting for threads\n";

    wake_services(udp_port, gw_port, metrics_port);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cerr << "[engine] joining threads\n";

    // run_udp_receiver and run_execution_gateway exit when they observe g_stop
    if(udp_thread.joinable()) udp_thread.join();
    if(gw_thread.joinable()) gw_thread.join();

    std::cerr << "[engine] exited\n";
    return 0;
    
}