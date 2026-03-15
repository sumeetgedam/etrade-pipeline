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


#include <arpa/inet.h>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

// stop flag
static volatile std::sig_atomic_t keep_running = 1;

void handle_signal(int) { keep_running = 0; }

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

// Safe JSON string escape for basic control chars and qoutes
// (Not a full JSON-escape implementation but sufficient for our simple symbols/strings)
// static std::string json_escape(const std::string& s){
//     std::ostringstream o;
//     for(char c : s){
//         switch (c)
//         {
//             case '\"': o << "\\\""; break;
//             case '\\': o << "\\\\"; break;
//             case '\b': o << "\\b"; break;
//             case '\f': o << "\\f"; break;
//             case '\n': o << "\\n"; break;
//             case '\r': o << "\\r"; break;
//             case '\t': o << "\\t"; break;
//             default:
//                 if(static_cast<unsigned char>(c) < 0x20){
//                     o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
//                 }else{
//                     o << c;
//                 }
//         }
//     }
//     return o.str();
// }

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
    if(argc >= 2) port = std::stoi(argv[1]);

    // Register SIGINT handler to allow graceful shutdown
    std::signal(SIGINT, handle_signal);

    // Create UDP socket (IPv4)
    int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        std::perror("socket");
        return 1;
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

    std::cout << "[udp_receiver] Listening on 0.0.0.0 : " << port << " (press Ctrl+C to stop)\n";

    // Preallocate buffer to avoid reallocation in loop
    std::vector<uint8_t> buffer(65536);

    // Open output file in append mode. we flush after every write to ensure visibility on host fs
    const std::string out_path = "../../data/events.jsonl";
    std::ofstream outfile(out_path, std::ios::app);
    if(!outfile.is_open()){
        std::cerr << "[error] Cannot open output file : " << out_path << " Reason : ";
        perror("open");
        close(sock);
        return 1;
    }

    // main receive loop: blocking recvfrom for now
    while(keep_running){
        sockaddr_in  src;
        socklen_t src_len = sizeof(src);

        // Record receive timestamp as close to recvfrom return as possible
        ssize_t n = recvfrom(sock, buffer.data(), buffer.size(), 0, 
                            reinterpret_cast<sockaddr*>(&src), &src_len);
        
        auto recv_time = std::chrono::system_clock::now();

        if(n < 0){
            if (errno == EINTR) break; // interrupted by signal
            std::perror("recvfrom");
            continue;
        }

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

        std::string symbol(tokens[2]);
        double price = 0.0;
        if(!to_double(tokens[3], price)){
            std::cerr << "[parse error] invalid price : '" << std::string(tokens[3])
                      << "' from " << src_ip << " : " << src_port << std::endl;
            continue;
        }

        long long size = 0;
        if(!to_ll(tokens[4], size)){
            std::cerr << "[parse error] invalid size : '" << std::string(tokens[4])
                      << "' from " << src_ip << " : " << src_port << std::endl;
            continue;
        }

        // compute latency in milliseconds: recv timestamp - message timestamp
        long long latency_ms = recv_ts_ms - msg_ts;

        // Construct a small JSON string manually
        std::ostringstream js;
        js << std::fixed << std::setprecision(2);
        js << "{";
        js << "\"seq\":" << seq << ",";
        js << "\"msg_ts_ms\":" << msg_ts << ",";
        js << "\"recv_ts_ms\":" << recv_ts_ms << ",";
        js << "\"latency_ms\":" << latency_ms << ",";
        js << "\"symbol\":\"" << symbol << "\",";
        js << "\"price\":" << price << ",";
        js << "\"size\":" << size << ",";
        js << "\"src\":\"" << src_ip << ":" << src_port << "\"";
        js << "}";

        std::string json_line = js.str();
    

        // Append to file and flush to ensure the host sees it quickly
        outfile << json_line << std::endl;
        outfile.flush();

        // Print a short line : timestamp ( ms), source, size, first byets ( hex )
        std::cout << "[ " << recv_ts_ms << " ] seq = " << seq << " " << symbol
                  << " price = " << price
                  << " size = " << size
                  << " latency_ms = " << latency_ms
                  << std::endl;

    }


    std::cout << "[udp_receiver] Shutting down\n";
    close(sock);
    return 0;

}