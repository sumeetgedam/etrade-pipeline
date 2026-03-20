#pragma once
#include <atomic>
#include <cstddef>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>

// Simple SPSC ( single producer single consumer ) ring buffer
// - Capacity must be power of two
// - push/pop are lock-free, no condition variables
// - push returns false if full, pop return returns false if empty
// - Designed for Event (non-trivial) where copies/moves are acceptable
// - Uses memory_order semantics appropriate for SPSC usage

// - push_bulk allows pushing multiple items in one call ( returns number pushed )
//

template<typename T>
class SPSCQueue { 

public:
    // capacity must be power of two and >= 2
    explicit SPSCQueue(size_t capacity = 1024)
        : capacity_(normalize_capacity(capacity)),
        mask_(capacity_ - 1),
        buffer_(capacity_)
    {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        shutdown_.store(false, std::memory_order_relaxed);
        // Optionally reserve string capacity for T if it has symbol string
    }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    // Try to push item, returns true on success, false if queue is full
    bool push(const T& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next = (tail + 1) & mask_;
        if(next == head_.load(std::memory_order_acquire)) {
            // full
            return false;
        }
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool push(T&& item) {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next = (tail + 1) & mask_;
        if(next == head_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[tail] = std::move(item);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    // bulk push : attempt to push up to n items from `items`
    // returns number actually pushed (0..n)
    size_t push_bulk(T* items, size_t n) {
        if (n==0) return 0;
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_relaxed);
        // current size = (tail - head ) & mask_
        size_t size = (tail - head) & mask_;
        size_t free_slots = mask_ - size;
        if(free_slots == 0) return 0;
        size_t to_push = (n <= free_slots) ? n : free_slots;

        // copy / move items into ring slots
        for (size_t i = 0; i < to_push; ++i) { 
            size_t idx = (tail + i) & mask_;
            buffer_[idx] = std::move(items[i]);
        }

        // advance tail
        tail_.store((tail+to_push) & mask_, std::memory_order_release);
        return to_push;
    }

    // try to pop an item, returns trye if an item was popped
    bool pop(T& out) {
        size_t head = head_.load(std::memory_order_relaxed);
        if(head == tail_.load(std::memory_order_relaxed)) {
            // empty
            return false;
        }
        out = std::move(buffer_[head]);
        head_.store((head+1) & mask_, std::memory_order_release);
        return true;
    }

    // check if empty
    bool empty() const {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }

    // signal shutdown to consumer ( non-blocking )
    void notify_shutdown() {
        shutdown_.store(true, std::memory_order_release);
    }

    bool is_shutdown() const {
        return shutdown_.load(std::memory_order_acquire);
    }

    // Approx size
    size_t approx_size() const {
        size_t h = head_.load(std::memory_order_acquire);
        size_t t = tail_.load(std::memory_order_acquire);
        if(t >= h) return t - h;
        return capacity_ - (h - t);
    }

private:
    static size_t normalize_capacity(size_t cap) {
        // Ensure power of two
        if(cap < 2) cap = 2;
        size_t n = 1;
        while(n < cap) n <<= 1;
        return n;
    }

    const size_t capacity_;
    const size_t mask_;
    std::vector<T> buffer_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
    std::atomic<bool> shutdown_;
};