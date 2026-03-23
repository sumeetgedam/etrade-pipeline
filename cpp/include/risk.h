#pragma once
#include <cstddef>
#include <string>


// Simple risk policy holder and checks
struct RiskPolicy {
    size_t max_order_size = 0;// 0 means no per-order size limit
    size_t max_open_orders = 0;// 0 means no limit
};