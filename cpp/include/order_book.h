#pragma once
#include "event.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>


// very small L2 like orderbook skeleton
// for each symbol we keep a small vector of recent price levels (most-recent-first)
// This is not a full matching engine, it provides a place to recieve and inspect updates

class OrderBook {

public:
    OrderBook(size_t max_levels = 5);
    ~OrderBook();
    
    // Apply an event to the book ( thread-safe )
    void apply_event(const Event& ev);

    // Get a simple textual snapshot fr debuggin ( thread-safe )
    std::string snapshot(const std::string& symbol);

private:
    struct Level {
        double price;
        long long size;
    };

    // Per symbol levels ( most recent first), protect with mutex for now
    std::map<std::string, std::vector<Level>> books_;
    std::mutex mu_;
    size_t max_levels_;
};