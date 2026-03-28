// simple replay sender : reads events.jsonl lines and re-sends UDP market messages
// usage:
    // replay_sender <events.jsonl> <host> <port> [--fast] [--scale N]
// if --fast is provided, events are emitted without wwaiting
// otherwise by default inter-event waits follow the msg_ts_ms deltas
// use --scale to speed up ( scale factor > 1).

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

// very small helpers to extract fields ( no JSON dependency )
static bool extract_lonlong_field(const std::string &s, const std::string &key, long long &out) {
    auto p = s.find(key);
    if(p == std::string::npos) return false;
    p += key.size();
    // skip whitespace and colon
    while(p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == ':')) ++p;
    bool neg = false;
    if(p < s.size() && s[p] == '-') { neg = true; ++p; }
    long long v = 0;
    bool got = false;
    while(p < s.size()) {
        char c = s[p++];
        if(c >= '0' && c <= '9') {
            v = v * 10 + (c-'0');
            got = true;
        }else break;
    }
    if(!got) return false;
    out = neg ? -v : v;
    return true;
}

static bool extract_double_field(const std::string &s, const std::string &key, double &out) {
    auto p = s.find(key);
    if (p == std::string::npos) return false;
    p += key.size();
    while(p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == ':')) ++p;
    // read until we hit a non-number char (allow '.'. 'e', '-')
    std::string tmp;
    bool got = false;
    while(p < s.size()) {
        char c = s[p];
        if((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            tmp.push_back(c);
            got = true;
            ++p;
        }else  break;
    }
    if(!got) return false;
    try {
        out = std::stod(tmp);
        return true;
    }catch(...){
        return false;
    }
}


static bool extract_string_field(const std::string &s, const std::string &key, std::string &out) {
    auto p = s.find(key);
    if(p == std::string::npos) return false;
    p += key.size();
    // find first qoute
    auto q = s.find('"', p);
    if(q == std::string::npos) return false;
    ++q;
    auto r = s.find('"', q);
    if (r == std::string::npos) return false;
    out = s.substr(q, r - q);
    return true;
}

int main(int argc, char** argv) {
    if(argc < 4) {
        std::cerr << "Usage: replay_sender <events.jsonl> <host> <port> [--fast] [--scale N]\n";
        return 2;
    }

    std::string path = argv[1];
    std::string host = argv[2];
    int port = std::stoi(argv[3]);
    bool fast = false;
    double scale = 1.0;
    for(int i = 4; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--fast") fast = true;
        else if (a == "--scale" && i + 1 < argc) {
            scale = std::stod(argv[++i]);
            if(scale <= 0) scale = 1.0;
        }else {
            std::cerr << "Unknown arg: " << a << "\n";
        }
    }

    std::ifstream in(path);
    if(!in.is_open()) {
        std::cerr << "failed to open " << path  << "\n";
        return 1;
    }

    int sock = ::socket(AF_INET,SOCK_DGRAM, 0);
    if(sock < 0) {
        std::perror("socket");
        return 1;
    } 
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if(inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        std::cerr << "invalid host : " << host << "\n";
        close(sock);
        return 1;
    }

    std::string line;
    long long prev_ts = -1;
    size_t sent = 0;
    while(std::getline(in, line)) {
        if(line.empty()) continue;
        long long seq = 0;
        long long msg_ts = 0;
        double price = 0.0;
        long long size = 0;
        std::string sym;
        // try to extract fields , tolerant parsing
        bool ok = true;
        ok &= extract_lonlong_field(line, "\"seq\"", seq);
        ok &= extract_lonlong_field(line, "\"msg_ts_ms\"", msg_ts);
        ok &= extract_double_field(line, "\"price\"", price);
        ok &= extract_lonlong_field(line, "\"size\"", size);
        ok &= extract_string_field(line, "\"symbol\"", sym);
        if(!ok) {
            std::cerr << "[warn] skipping unparsable line : " << line << "\n";
            continue;
        }

        // Format the UDP message as the receiver expects: seq|msg_ts|symbol|price|size
        char buf[256];
        int n = snprintf(buf, sizeof(buf), "%lld|%lld|%s|%.2f|%lld", (long long)seq, (long long)msg_ts, sym.c_str(), price, (long long)size);
        if(n <= 0) continue;

        // if not fast: compute sleep based on msg_ts delta(ms)
        if(!fast && prev_ts >= 0) {
            long long delta = msg_ts - prev_ts;
            if(delta > 0) {
                long long sleep_ms = static_cast<long long>(delta/scale);
                if(sleep_ms > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
                }
            }
        }

        ssize_t w = sendto(sock, buf, static_cast<size_t>(n), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if(w < 0) {
            std::perror("sendto");
        }else{
            ++sent;
        }

        prev_ts = msg_ts;

    }

    close(sock);
    std::cout << "[replay_sender] sent " << sent << " messages to " << host << ":" << port << "\n";
    return 0;

}