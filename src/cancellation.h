#pragma once
#include <atomic>
#include <memory>
using Cancellation = std::shared_ptr<std::atomic_bool>;
inline bool isCancelled(const Cancellation &flag) {
    return flag && flag->load(std::memory_order_relaxed);
}
