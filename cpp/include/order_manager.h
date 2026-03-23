#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "order_book.h"
#include "risk.h"

// Order lifeCycle manager :
//  - accepts orders
//  - enforces risk
//  - invokes OrderBook::match_order
//  - tracks remaining size
//  - handles cancel

class OrderManager {

public:
    enum class Status { NEW, OPEN, PARTIAL, FILLED, CANCELED };

    struct State {
        std::string clid;
        OrderBook::Order::Side side;
        std::string symbol;
        double price;
        int64_t original_size;
        int64_t remaining;
        Status status;
    };

    // Construct with pointer to shared orderbook and risk policay
    OrderManager(OrderBook* ob, const RiskPolicy& policy);
    ~OrderManager();

    // result of submission
    struct SubmitResult {
        bool accepted;
        std::string reason;
        std::vector<OrderBook::Fill> fills;

    };

    // submit an order
    // returns SubmitResult
    SubmitResult submit_order(const OrderBook::Order& ord);

    // try to cancel an existing order
    // returns true if cancel succeeded ( remaining > 0)
    // and sets canceled_size to the remaining amoutn that was canceled
    bool cancel_order(const std::string& clid, int64_t &canceled_size);

    // Query current open order count ( for diagnostics)
    size_t open_order_count() const;

private:
    OrderBook* ob_;
    RiskPolicy policy_;
    mutable std::mutex m_;
    std::unordered_map<std::string, State> orders_; // clid -> state

};