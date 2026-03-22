#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <unistd.h> // tfor getpid()
#include <limits>
#include <cmath>
#include <map>
#include <unordered_map>
#include <mutex>
#include <algorithm>
#include <memory>

#include "../include/order_book.h"
#include "../include/event.h"

// struct OrderBook::Event {
//     // Minimal Evet stub for apply event signature
//     std::string symbol;
//     double price;
//     int64_t size;
//     long latency_ms;
// };

struct OrderBook::Impl {
    // per symbol L2 bids ( price -> size) sorted descending, ask scending
    struct SideBook {
        std::map<double, int64_t, std::greater<double>> bids;
        std::map<double, int64_t> asks;
    };

    std::unordered_map<std::string, SideBook> books;
    mutable std::mutex m;
    int depth = 5;
};

OrderBook::OrderBook(int depth): impl_(new Impl()) { impl_->depth = depth; }
OrderBook::~OrderBook() { delete impl_; }


void OrderBook::apply_event(const Event& ev) {
    // Basic behavior: treat event as a snashot update for the price level
    // this routine is intentionally consersative and only sets an ask level
    // if price > 0.0 and size >= 0 Real feed sematics may differ
    if(ev.symbol[0] == '\0') return;
    std::lock_guard<std::mutex> lk(impl_->m);
    auto &sidebook = impl_->books[ev.symbol];

    // Heuristic: if price is integer0like or positive, insert/update bid or ask
    // this is placeholder , adapt to your feed ( if feed includes side flag )
    // we ill put levels on both sides only for test convenience when size > 0
    if(ev.size <= 0){
        // if size zero or negative, remoce the level if exists
        sidebook.bids.erase(ev.price);
        sidebook.asks.erase(ev.price);
        return;
    }

    // simpe update: if price is less than current median, treat as bid, else ask
    // for realistic feed this must be replaced by real side info
    // use a simple rule, put into asks if ther's a better higher bid already
    if( !sidebook.bids.empty()) {
        double best_bid = sidebook.bids.begin()->first;
        if(ev.price <= best_bid) {
            sidebook.bids[ev.price] = ev.size;
            return;
        }
    }
    // otherwise insert into asks
    sidebook.asks[ev.price] = ev.size;
}

std::pair<double, double> OrderBook::top_of_book(const std::string& symbol) const {
    std::lock_guard<std::mutex> lk(impl_->m);
    auto it = impl_->books.find(symbol);
    if(it == impl_->books.end()) {
        double qnan = std::numeric_limits<double>::quiet_NaN();
        return {qnan, qnan};
    }

    const auto &sb = it->second;
    double best_bid = std::numeric_limits<double>::quiet_NaN();
    double best_ask = std::numeric_limits<double>::quiet_NaN();
    if(!sb.bids.empty()) best_bid = sb.bids.begin()->first;
    if(!sb.asks.empty()) best_ask = sb.asks.begin()->first;
    return {best_bid, best_ask};
}

std::vector<OrderBook::Fill> OrderBook::match_order(const OrderBook::Order& order) {
    std::vector<Fill> fills;
    std::lock_guard<std::mutex> lk(impl_->m);
    auto it = impl_->books.find(order.symbol);
    if(it == impl_->books.end()) return fills;

    auto &sb = it->second;

    int64_t remaining = order.size;
    if(remaining <= 0) return fills;

    if(order.side == Order::BUY) {
        // match against asks (lowest first)
        auto ait = sb.asks.begin();
        while(ait != sb.asks.end() && remaining > 0) {
            double price = ait->first;
            int64_t avail = ait->second;
            if ( avail <= 0){
                ait = sb.asks.erase(ait);
                continue;
            }
            int64_t take = std::min<int64_t>(avail, remaining);
            fills.push_back(Fill{order.clid, take, price});
            remaining -= take;
            avail -= take;
            if (avail == 0) {
                ait = sb.asks.erase(ait);
            }else{
                ait->second = avail;
                ++ait;
            }
        }
    }else { 
        // match against bids
        auto bit = sb.bids.begin();
        while(bit != sb.bids.end() && remaining > 0){
            double price = bit->first;
            int64_t avail = bit->second;
            if(avail <= 0) {
                bit = sb.bids.erase(bit);
                continue;
            }
            int64_t take = std::min<int64_t>(avail, remaining);
            fills.push_back(Fill{order.clid, take, price});
            remaining -= take;
            avail -= take;
            if(avail == 0){
                bit = sb.bids.erase(bit);
            }else{
                bit->second = avail;
                ++bit;
            }
        }
    }
    return fills;
}

void OrderBook::set_level(const std::string& symbol, bool side_buy, double price, int64_t size) {
    std::lock_guard<std::mutex> lk(impl_->m);
    auto &sb = impl_->books[symbol];
    if(side_buy) {
        if(size <= 0) sb.bids.erase(price);
        else sb.bids[price] = size;
    }else{
        if(size<= 0) sb.asks.erase(price);
        else sb.asks[price] = size;
    }
}

// helper to formatrecv_ts_ms ( milliseconds since epoch ) as ISO8601 UTC string with ms
// static std::string iso8601_from_ms(long long ms_since_epoch) {
//     using namespace std::chrono;
//     system_clock::time_point tp = system_clock::time_point(milliseconds(ms_since_epoch));
//     std::time_t t = system_clock::to_time_t(tp);
//     std::tm tm{};
// #if defined(_WIN32)
//     gmtime_s(&tm, &t);
// #else
//     gmtime_r(&t, &tm);
// #endif
//     auto ms_part = ms_since_epoch % 1000;
//     std::ostringstream oss;
//     oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
//     oss << '.' << std::setw(3) << std::setfill('0') << ms_part << 'Z';
//     return oss.str();

// }

// Apply a simple update: 
// - if price equals the current top price, update size
// - otherwise push the new level as most recent and trim to max_levels_
// void OrderBook::apply_event(const Event& ev) {
    // Debug: confirm entry
    // std::cout << "[orderbook] apply_event called seq= " << ev.seq
    //           << " symbol = " << ev.symbol 
    //           << " price = " << ev.price
    //           << " size = " << ev.size 
    //         //   << " latency_ms = " << ev.latency_ms
    //           << std::endl;

    // std::lock_guard<std::mutex> lk(impl_->m);
    // auto &levels = books_[std::string(ev.symbol)];

    // if(!levels.empty() && levels.front().price == ev.price){
    //     // updating existing top level size
    //     levels.front().size = ev.size;
    // }else {
    //     // push new top level
    //     Level lvl{ev.price, ev.size};
    //     levels.insert(levels.begin(), lvl);
    //     if(levels.size() > max_levels_) levels.resize(max_levels_);
    // }

    // // print a concise top of book summary 
    // std::ostringstream oss;
    // oss << "[orderbook] symbol = " << ev.symbol;

    // if(!levels.empty()){
    //     oss << " top_price = " << levels.front().price
    //         << " top_size = " << levels.front().size;
    // }else {
    //     oss << " (empty)";
    // }
        
    // oss << " seq = " << ev.seq
    //     << " latency_ms = " << ev.latency_ms;
    
    // std::cout << oss.str() << std::endl;

    // // write per-symbol snapshot file ( overwrite )
    // // try{
    //     std::ostringstream js;
    //     js << "{ \"symbol\": \"" << ev.symbol << "\",";
    //     js << "\"last_updated_ms\":" << ev.recv_ts_ms << ",";
    //     js << "\"last_updated_iso\":\"" << iso8601_from_ms(ev.recv_ts_ms) << "\",";
    //     js << "\"levels\" : [ ";
    //     bool first = true;
    //     for(const auto &lvl : levels) {
    //         if (!first) js << ", ";
    //         js << "{ \"price\" : " << lvl.price << ", \"size\": " << lvl.size << " }";
    //         first = false;
    //     }
    //     js << "] }";
    //     std::string json_content = js.str();

    //     // Atomic write to tmp file then rename
    //     try{
    //         std::string base_path = "../../data/book_" + std::string(ev.symbol) + ".json";
    //         std::ostringstream tmposs;
    //         tmposs << base_path << ".tmp." << getpid();
    //         std::string tmp_path = tmposs.str();

    //         {
    //             std::ofstream ofs(tmp_path, std::ios::binary | std::ios::trunc);
    //             if(!ofs.is_open()){
    //                 std::cerr << "[orderbook][error] cannot open tmp snapshot " << tmp_path << std::endl;
    //             }else {
    //                 ofs << json_content;
    //                 ofs.flush();
    //                 ofs.close();
    //                 // rename is atomic on POSIX when replacing within same filesystem
    //                 std::filesystem::rename(tmp_path, base_path);
    //             }
    //         }
        
    //     }catch (const std::exception &e) {
    //         std::cerr << "[orderbook][error] snapshot write exception : " << e.what() << std::endl;
    //     }catch(...){
    //         std::cerr << "[orderbook][error] unknown snapshot write failure\n";
    //     }
    //     // std::string path = "../../data/book_" + ev.symbol + ".json";
    //     // std::ofstream ofs(path, std::ios::trunc);
    //     // if(!ofs.is_open()){
    //     //     std::cerr << "[orderbook][error] cannot wrtie snapshot to " << path << std::endl;
            
    //     // }else{
    //     //     ofs << js.str();
    //     //     ofs.flush();
    //     //     // success (optional debug)
    //     //     // std::cout << "[orderbook] snapshot written to " << path << std::endl;
    //     // }

    // // }catch(const std::exception &e) {
    // //     std::cerr << "[order][error] snapshot write exception : " << e.what() << std::endl;
    // // }catch(...){
    // //     std::cerr << "[orderbook][error] unknown snapshot write failure\n";
    // // }

// }

// std::string OrderBook::snapshot(const std::string& symbol) {
//     std::lock_guard<std::mutex> lk(mu_);
//     std::ostringstream oss;
//     auto it  = books_.find(symbol);
//     if (it == books_.end()){
//         return "{}";
//     }

//     oss << "{ \"symbol\" : \"" << symbol << "\", \"levels\" : [";
//     bool first = true;
//     for(const auto &lvl: it->second){
//         if(!first) oss << ", ";
//         oss << "{ \"price\" : " << lvl.price << ", \"size\": " << lvl.size << " }";
//         first = false;
    
//     }
//     oss << "] }";
//     return oss.str();
// }