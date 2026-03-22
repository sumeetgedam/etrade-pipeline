#pragma once
#include <atomic>
#include "order_book.h"

// simple global registry for the in-process orderbook
// the engine will call set_global_order_book(...) during startup
inline std::atomic<OrderBook*> g_orderbook_ptr{nullptr};

inline void set_global_order_book(OrderBook* b) {
    g_orderbook_ptr.store(b, std::memory_order_release);
}

inline OrderBook* get_global_order_book() {
    return g_orderbook_ptr.load(std::memory_order_acquire);
}