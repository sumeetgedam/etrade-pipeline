#include <atomic>

#include "../include/metrics.h"


namespace metrics {

    static std::atomic<uint64_t> g_order_rejected{0};
    static std::atomic<uint64_t> g_order_accepted{0};
    static std::atomic<uint64_t> g_orders_filled_count{0};
    static std::atomic<uint64_t> g_filled_volume{0};
    static std::atomic<uint64_t> g_orders_canceled{0};
    static std::atomic<uint64_t> g_open_orders{0};

    void inc_order_accepted() {
        g_order_accepted.fetch_add(1, std::memory_order_relaxed);

    }

    void inc_orders_rejected() {
        g_order_rejected.fetch_add(1, std::memory_order_relaxed);
    }

    void inc_orders_filled_count(uint64_t n) {
        g_orders_filled_count.fetch_add(n, std::memory_order_relaxed);
    }

    void add_filled_volume(uint64_t v) { g_filled_volume.fetch_add(v, std::memory_order_relaxed); }

    void inc_orders_canceled(uint64_t n) { g_orders_canceled.fetch_add(n, std::memory_order_relaxed); }

    void set_open_orders(uint64_t n){ g_open_orders.store(n, std::memory_order_relaxed); }

    uint64_t get_order_accepted() { return g_order_accepted.load(std::memory_order_relaxed); }
    uint64_t get_order_rejected() { return g_order_rejected.load(std::memory_order_relaxed); }
    uint64_t get_orders_filled_count() { return g_orders_filled_count.load(std::memory_order_relaxed); }
    uint64_t get_filled_volume() { return g_filled_volume.load(std::memory_order_relaxed); }
    uint64_t get_orders_canceled() { return g_orders_canceled.load(std::memory_order_relaxed); }
    uint64_t get_open_orders() { return g_open_orders.load(std::memory_order_relaxed); }

}