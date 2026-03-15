#include "../include/order_book.h"
#include <iostream>
#include <sstream>

OrderBook::OrderBook(size_t max_levels) : max_levels_(max_levels) {}
OrderBook::~OrderBook() = default;

// Apply a simple update: 
// - if price equals the current top price, update size
// - otherwise push the new level as most recent and trim to max_levels_
void OrderBook::apply_event(const Event& ev) {
    std::lock_guard<std::mutex> lk(mu_);
    auto &levels = books_[ev.symbol];

    if(!levels.empty() && levels.front().price == ev.price){
        levels.front().size = ev.size;
    }else {
        Level lvl{ev.price, ev.size};
        levels.insert(levels.begin(), lvl);
        if(levels.size() > max_levels_) levels.resize(max_levels_);
    }

    // print a concise top of book summary 
    std::ostringstream oss;
    oss << "[orderbook] symbol = " << ev.symbol
        << " top_price = " << levels.front().price
        << " seq = " << ev.seq
        << " latency_ms = " << ev.latency_ms;
    
    std::cout << oss.str() << std::endl;

}

std::string OrderBook::snapshot(const std::string& symbol) {
    std::lock_guard<std::mutex> lk(mu_);
    std::ostringstream oss;
    auto it  = books_.find(symbol);
    if (it == books_.end()){
        return "{}";
    }

    oss << "{ \"symbol\" : \"" << symbol << "\", \"levels\" : [";
    bool first = true;
    for(const auto &lvl: it->second){
        if(!first) oss << ", ";
        oss << "{ \"price\" : " << lvl.price << ", \"size\": " << lvl.size << " }";
        first = false;
    
    }
    oss << "] }";
    return oss.str();
}