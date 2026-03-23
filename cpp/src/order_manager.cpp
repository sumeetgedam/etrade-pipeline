#include <algorithm>

#include "../include/order_manager.h"

OrderManager::OrderManager(OrderBook* ob, const RiskPolicy& policy) : ob_(ob), policy_(policy) {}


OrderManager::~OrderManager() {}

OrderManager::SubmitResult OrderManager::submit_order(const OrderBook::Order& ord) {
    SubmitResult res;
    res.accepted = false;

    // basic sanity
    if(ord.size <= 0) {
        res.reason = "bas_size";
        return res;
    }

    // Risk checks
    if(policy_.max_order_size > 0 && static_cast<int64_t>(policy_.max_order_size) < ord.size) {
        res.reason = "size_exceeds_limit";
        return res;
    }

    {
        std::lock_guard<std::mutex> lk(m_);
        size_t open_cnt = 0;
        for(const auto &kv : orders_) {
            if(kv.second.status == Status::OPEN || kv.second.status == Status::PARTIAL) ++open_cnt;

        }
        if(policy_.max_open_orders > 0 && open_cnt >= policy_.max_open_orders) {
            res.reason = "max_open_orders_exceeded";
            return res;
        }
    }

    // if we dont have an orderbook, reject ( dont expect this as engine creates one )
    if(!ob_) {
        res.reason = "no_orderbook";
        return res;

    }

    // accept
    {
        std::lock_guard<std::mutex> lk(m_);
        State st;
        st.clid = ord.clid;
        st.side = ord.side;
        st.symbol = ord.symbol;
        st.price = ord.price;
        st.original_size = ord.size;
        st.remaining = ord.size;
        st.status = Status::NEW;
        orders_.emplace(st.clid, st);


    }

    // perform matching mgainst the order book ( may generate imeediate fills)
    auto fills = ob_->match_order(ord);

    if(!fills.empty()) {
        // update internal state based on fills
        std::lock_guard<std::mutex> lk(m_);
        auto it = orders_.find(ord.clid);
        if (it != orders_.end()) {
            int64_t consumed = 0;
            for(const auto &f : fills) consumed += f.size;
            it->second.remaining -= consumed;
            if(it->second.remaining <= 0){
                it->second.remaining = 0;
                it->second.status = Status::FILLED;
            }else{
                it->second.status = Status::PARTIAL;
            }
        }
    }else{
        // no immediate fills  , mark as open
        std::lock_guard<std::mutex> lk(m_);
        auto it = orders_.find(ord.clid);
        if (it != orders_.end()) it->second.status = Status::OPEN;
    }

    res.accepted = true;
    res.fills = std::move(fills);
    return res;
    
}


bool OrderManager::cancel_order(const std::string& clid, int64_t &canceled_size) {
    std::lock_guard<std::mutex> lk(m_);
    auto it = orders_.find(clid);
    if(it == orders_.end()) return false;
    if(it->second.status == Status::FILLED || it->second.status == Status::CANCELED) return false;
    canceled_size = it->second.remaining;
    it->second.remaining = 0;
    it->second.status = Status::CANCELED;
    return true;
}

size_t OrderManager::open_order_count() const {
    std::lock_guard<std::mutex> lk(m_);
    size_t cnt = 0;
    for(const auto &kv: orders_) {
        if(kv.second.status == Status::OPEN || kv.second.status == Status::PARTIAL) ++cnt;

    }
    return cnt;
}