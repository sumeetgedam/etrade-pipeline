#pragma once
#include <string>
#include <cstring>

// Event: a small POD-like structure representing a parsed feed message
// Kept intentionally simple for now, can be extended with enums / types

// symbol is now a fixed size NULL terminated buffer to avoid per-messages heap allocations
struct Event
{
    long long seq = 0;
    long long msg_ts_ms = 0;
    long long recv_ts_ms = 0;
    long long latency_ms = 0;
    // Fixed size symbol buffer ( NUL-terminated )
    static constexpr size_t SYMBOL_LEN = 32;
    char symbol[SYMBOL_LEN];
    double price = 0.0;
    long long size = 0;
    // std::string src; // eg, "127.0.0.1:9000" , less frequent

    // Fixed-size src buffer
    static constexpr size_t SRC_LEN = 64;
    char src[SRC_LEN];

    Event(){
        symbol[0] = '\0';
        src[0] = '\0';
    }

    // helper to set symbol safely from a string_view / c-string
    void set_symbol(const char* s, size_t n){
        if(!s) { symbol[0] = '\0'; return; }
        size_t copy_n = (n < SYMBOL_LEN - 1) ? n : (SYMBOL_LEN - 1);
        std::memcpy(symbol, s, copy_n);
        symbol[copy_n] = '\0';

    }

    void set_symbol(const std::string& s) {
        set_symbol(s.c_str(),s.size());
    }

    void set_src(const char* s, size_t n){
        if(!s) { src[0] = '\0'; return; }
        size_t copy_n = (n < SRC_LEN - 1) ? n : (SRC_LEN - 1);
        std::memcpy(src, s, copy_n);
        src[copy_n] = '\0';
    }

    void set_src(const std::string& s) {
        set_src(s.c_str(), s.size());
    }

 };
