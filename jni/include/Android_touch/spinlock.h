#pragma once

#include <atomic>

struct spinlock {
    std::atomic<bool> lock_ = {false};

    void lock() noexcept {
        for (;;) {
            
            if (!lock_.exchange(true, std::memory_order_acquire)) {
                return;
            }
            
            while (lock_.load(std::memory_order_relaxed)) {
                
                
                sched_yield();
            }
        }
    }

    bool try_lock() noexcept {
        
        
        return !lock_.load(std::memory_order_relaxed) &&
               !lock_.exchange(true, std::memory_order_acquire);
    }

    void unlock() noexcept {
        lock_.store(false, std::memory_order_release);
    }
};