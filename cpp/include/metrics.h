#pragma once
#include <cstdint>

namespace metrics {

    // Order/gateway metrics
    void inc_order_accepted();
    void inc_orders_rejected();
    void inc_orders_filled_count(uint64_t n = 1);
    void add_filled_volume(uint64_t v);
    void inc_orders_canceled(uint64_t n = 1);

    // Gauge: open orders
    void set_open_orders(uint64_t n);

    // Getters for metrics_server
    uint64_t get_order_accepted();
    uint64_t get_order_rejected();
    uint64_t get_orders_filled_count();
    uint64_t get_filled_volume();
    uint64_t get_orders_canceled();
    uint64_t get_open_orders();

}