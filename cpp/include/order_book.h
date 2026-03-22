#pragma once
#include "event.h"
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <utility>


// very small L2 like orderbook skeleton
// for each symbol we keep a small vector of recent price levels (most-recent-first)
// This is not a full matching engine, it provides a place to recieve and inspect updates

class OrderBook {

public:
    OrderBook(int depth = 5);
    ~OrderBook();
    
    // Apply an event to the book ( thread-safe )
    // void apply_event(const Event& ev);

    // Get a simple textual snapshot fr debuggin ( thread-safe )
    // std::string snapshot(const std::string& symbol);

    struct Fill {
        std::string clid;
        int64_t size;
        double price;
    };

    struct Order { 
        std::string clid;
        enum Side {BUY = 0, SELL = 1} side;
        std::string symbol;
        double price;
        int64_t size;
    };

    // Apply a parsed market Event to th book (feed-drivem)
    // keep as public so receiver /engine can call it
    // struct Event;
    void apply_event(const Event& ev);

    // query top-of-book (best_bid, best_offer )
    // returns pair(best_bud, best+offer), NANs if not available
    std::pair<double, double> top_of_book(const std::string& symbol) const;

    // match incoming order against the book and return fills  ( may be empty )
    // This mutates the book state ( removes / shrinks level ) as part of the match
    // the returned fills use Order.clid to let the gateway correlate fills
    std::vector<Fill> match_order(const Order& order);

    // Helper used for testing , set a level for a symbol/side/price->size
    // side true = BUY (bid), false=SELL(ask)
    void set_level(const std::string& symbol, bool side_buy, double price, int64_t size);

    // optional, snapshot writers
    // to obtain book levels for persistence
    // std::vector<std::pair<double, int64_t>> get_bids*const std::string& symbol) const;
    // std::vector<std::pair<double, int64_t>> get_asks(const std::string& symbol) const;


private:

    struct Impl;
    Impl* impl_;
    // struct Level {
    //     double price;
    //     long long size;
    // };

    // // Per symbol levels ( most recent first), protect with mutex for now
    // std::map<std::string, std::vector<Level>> books_;
    // std::mutex mu_;
    // size_t max_levels_;
};