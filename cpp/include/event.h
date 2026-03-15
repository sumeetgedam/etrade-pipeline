#pragma once
#include <string>

// Event: a small POD-like structure representing a parsed feed message
// Kept intentionally simple for now, can be extended with enums / types

struct Event
{
    long long seq = 0;
    long long msg_ts_ms = 0;
    long long recv_ts_ms = 0;
    long long latency_ms = 0;
    std::string symbol;
    double price = 0.0;
    long long size = 0;
    std::string src; // eg, "127.0.0.1:9000"
};
