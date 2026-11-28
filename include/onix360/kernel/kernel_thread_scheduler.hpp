#pragma once

#include "onix360/cpu/xenon_state.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <optional>

namespace onix360::kernel {

enum class ThreadStatus : std::uint8_t { Ready, Running, Blocked, Terminated };

struct KernelThread {
    std::uint32_t id{};
    std::uint8_t priority{};
    ThreadStatus status{ThreadStatus::Ready};
    cpu::XenonThreadState context{};
};

// Deterministic priority-based cooperative scheduler. Equal-priority threads
// retain FIFO order. Contexts are guest CPU states; this class does not create
// host threads or claim preemptive Xbox scheduling.
class KernelThreadScheduler {
public:
    explicit KernelThreadScheduler(std::size_t capacity = 256) noexcept
        : capacity_(capacity) {}

    [[nodiscard]] std::optional<std::uint32_t> create(
        const cpu::XenonThreadState& initial, std::uint8_t priority = 16) {
        if (threads_.size() >= capacity_ ||
            next_id_ == std::numeric_limits<std::uint32_t>::max())
            return std::nullopt;
        const auto id = next_id_++;
        KernelThread thread{};
        thread.id = id;
        thread.priority = priority;
        thread.status = ThreadStatus::Ready;
        thread.context = initial;
        threads_.emplace(id, thread);
        ready_.push_back(id);
        return id;
    }

    // Save the outgoing guest state before rotating to the next ready thread.
    [[nodiscard]] bool yield(cpu::XenonThreadState& live_context) noexcept {
        if (current_) {
            auto it = threads_.find(*current_);
            if (it == threads_.end() || it->second.status != ThreadStatus::Running)
                return false;
            it->second.context = live_context;
            it->second.status = ThreadStatus::Ready;
            ready_.push_back(it->first);
            current_.reset();
        }
        return schedule(live_context);
    }

    [[nodiscard]] bool schedule(cpu::XenonThreadState& live_context) noexcept {
        if (current_) return false;
        while (!ready_.empty()) {
            auto selected = ready_.end();
            std::uint8_t best_priority = 0;
            for (auto it = ready_.begin(); it != ready_.end(); ++it) {
                const auto thread = threads_.find(*it);
                if (thread == threads_.end() ||
                    thread->second.status != ThreadStatus::Ready) continue;
                if (selected == ready_.end() ||
                    thread->second.priority > best_priority) {
                    selected = it;
                    best_priority = thread->second.priority;
                }
            }
            if (selected == ready_.end()) {
                ready_.clear();
                return false;
            }
            const auto id = *selected;
            ready_.erase(selected);
            auto thread = threads_.find(id);
            thread->second.status = ThreadStatus::Running;
            current_ = id;
            live_context = thread->second.context;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool block_current(cpu::XenonThreadState& live_context) noexcept {
        if (!current_) return false;
        auto it = threads_.find(*current_);
        if (it == threads_.end() || it->second.status != ThreadStatus::Running)
            return false;
        it->second.context = live_context;
        it->second.status = ThreadStatus::Blocked;
        current_.reset();
        (void)schedule(live_context);
        return true;
    }

    [[nodiscard]] bool unblock(std::uint32_t id) noexcept {
        auto it = threads_.find(id);
        if (it == threads_.end() || it->second.status != ThreadStatus::Blocked)
            return false;
        it->second.status = ThreadStatus::Ready;
        ready_.push_back(id);
        return true;
    }

    [[nodiscard]] bool terminate_current(cpu::XenonThreadState& live_context) noexcept {
        (void)live_context;
        if (!current_) return false;
        auto it = threads_.find(*current_);
        if (it == threads_.end() || it->second.status != ThreadStatus::Running)
            return false;
        it->second.status = ThreadStatus::Terminated;
        current_.reset();
        (void)schedule(live_context);
        return true;
    }

    [[nodiscard]] const KernelThread* find(std::uint32_t id) const noexcept {
        const auto it = threads_.find(id);
        return it == threads_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] std::optional<std::uint32_t> current_id() const noexcept {
        return current_;
    }
    [[nodiscard]] std::size_t thread_count() const noexcept { return threads_.size(); }
    [[nodiscard]] std::size_t ready_count() const noexcept { return ready_.size(); }

private:
    std::size_t capacity_{};
    std::uint32_t next_id_{1};
    std::map<std::uint32_t, KernelThread> threads_;
    std::deque<std::uint32_t> ready_;
    std::optional<std::uint32_t> current_;
};

} // namespace onix360::kernel
