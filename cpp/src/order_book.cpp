#include "../include/order_book.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

OrderBook::OrderBook(size_t max_levels) : max_levels_(max_levels) {
    // ensure data directory exists so snapshot writes succeed
    try{
        std::filesystem::create_directories("../../data");
    }catch(...) {}
}
OrderBook::~OrderBook() = default;

// Apply a simple update: 
// - if price equals the current top price, update size
// - otherwise push the new level as most recent and trim to max_levels_
void OrderBook::apply_event(const Event& ev) {
    // Debug: confirm entry
    std::cout << "[orderbook] apply_event called seq= " << ev.seq
              << " symbol = " << ev.symbol 
              << " price = " << ev.price
              << " size = " << ev.size 
              << " latency_ms = " << ev.latency_ms
              << std::endl;

    std::lock_guard<std::mutex> lk(mu_);
    auto &levels = books_[ev.symbol];

    if(!levels.empty() && levels.front().price == ev.price){
        // updating existing top level size
        levels.front().size = ev.size;
    }else {
        // push new top level
        Level lvl{ev.price, ev.size};
        levels.insert(levels.begin(), lvl);
        if(levels.size() > max_levels_) levels.resize(max_levels_);
    }

    // print a concise top of book summary 
    std::ostringstream oss;
    oss << "[orderbook] symbol = " << ev.symbol;

    if(!levels.empty()){
        oss << " top_price = " << levels.front().price
            << " top_size = " << levels.front().size;
    }
        
    oss << " seq = " << ev.seq
        << " latency_ms = " << ev.latency_ms;
    
    std::cout << oss.str() << std::endl;

    // write per-symbol snapshot file ( overwrite )
    try{
        std::ostringstream js;
        js << "{ \"symbol\": \"" << ev.symbol << "\", \"levels\" : [ ";
        bool first = true;
        for(const auto &lvl : levels) {
            if (!first) js << ", ";
            js << "{ \"price\" : " << lvl.price << ", \"size\": " << lvl.size << " }";
            first = false;
        }
        js << "] }";
        std::string path = "../../data/book_" + ev.symbol + ".json";
        std::ofstream ofs(path, std::ios::trunc);
        if(!ofs.is_open()){
            std::cerr << "[orderbook][error] cannot wrtie snapshot to " << path << std::endl;
            
        }else{
            ofs << js.str();
            ofs.flush();
            // success (optional debug)
            // std::cout << "[orderbook] snapshot written to " << path << std::endl;
        }

    }catch(const std::exception &e) {
        std::cerr << "[order][error] snapshot write exception : " << e.what() << std::endl;
    }catch(...){
        std::cerr << "[orderbook][error] unknown snapshot write failure\n";
    }

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