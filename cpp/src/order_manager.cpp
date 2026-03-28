#include <algorithm>

#include "../include/order_manager.h"
#include "../include/metrics.h"

OrderManager::OrderManager(OrderBook* ob, const RiskPolicy& policy) : ob_(ob), policy_(policy) {}


OrderManager::~OrderManager() {}

OrderManager::SubmitResult OrderManager::submit_order(const OrderBook::Order& ord) {
    SubmitResult res;
    res.accepted = false;

    // basic sanity
    if(ord.size <= 0) {
        res.reason = "bas_size";
        metrics::inc_orders_rejected();
        return res;
    }

    // Risk checks
    if(policy_.max_order_size > 0 && static_cast<int64_t>(policy_.max_order_size) < ord.size) {
        res.reason = "size_exceeds_limit";
        metrics::inc_orders_rejected();
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
            metrics::inc_orders_rejected();
            return res;
        }
    }

    // if we dont have an orderbook, reject ( dont expect this as engine creates one )
    if(!ob_) {
        res.reason = "no_orderbook";
        metrics::inc_orders_rejected();
        return res;

    }

    // accept
    {
        std::lock_guard<std::mutex> lk(m_);
        State st;
        st.clid = ord.clid;
        st.side = static_cast<OrderBook::Order::Side>(ord.side);
        st.symbol = ord.symbol;
        st.price = ord.price;
        st.original_size = ord.size;
        st.remaining = ord.size;
        st.status = Status::NEW;
        orders_.emplace(st.clid, st);

        // update open orders gauge
        size_t open_cnt = 0;
        for(const auto &kv: orders_) {
            if(kv.second.status == Status::OPEN || kv.second.status == Status::PARTIAL || kv.second.status == Status::NEW) ++open_cnt;

        }
        metrics::set_open_orders(open_cnt);

    }

    // perform matching mgainst the order book ( may generate imeediate fills)
    auto fills = ob_->match_order(ord);

    if(!fills.empty()) {
        // update internal state based on fills

        int64_t consumed = 0;
        for(const auto &f : fills) consumed += f.size;

        {
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
            // update open irders gauge
            size_t open_cnt = 0;
            for(const auto &kv : orders_) {
                if(kv.second.status == Status::OPEN || kv.second.status == Status::PARTIAL || kv.second.status == Status::NEW) ++open_cnt;

            }
            metrics::set_open_orders(open_cnt);
        }
        // report fills to metrics: number of fill records and total filled volume
        metrics::inc_orders_filled_count(fills.size());
        uint64_t vol = 0;
        for ( const auto &f: fills) vol += static_cast<uint64_t>(f.size);
        metrics::add_filled_volume(vol);
        
    }else{
        // no immediate fills  , mark as open
        {
            std::lock_guard<std::mutex> lk(m_);
            auto it = orders_.find(ord.clid);
            if (it != orders_.end()) it->second.status = Status::OPEN;

            // update open orders gauge
            size_t open_cnt = 0;
            for(const auto &kv: orders_) {
                if(kv.second.status == Status::OPEN || kv.second.status == Status::PARTIAL || kv.second.status == Status::NEW) ++open_cnt;
            }
            metrics::set_open_orders(open_cnt);
        }
        
    }

    res.accepted = true;
    metrics::inc_order_accepted();
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

    // update open orders gauge
    size_t open_cnt = 0;
    for(const auto &kv: orders_) {
        if(kv.second.status == Status::OPEN || kv.second.status == Status::PARTIAL || kv.second.status == Status::NEW) ++open_cnt;
    }
    metrics::set_open_orders(open_cnt);

    metrics::inc_orders_canceled(1);
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