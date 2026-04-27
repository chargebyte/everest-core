// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace everest::lib::util::perf {

using Clock = std::chrono::steady_clock;

struct Metric {
    explicit Metric(const char* metric_name) : name(metric_name) {
        register_metric(this);
    }

    void add(const std::uint64_t ns) {
        this->calls.fetch_add(1, std::memory_order_relaxed);
        this->total_ns.fetch_add(ns, std::memory_order_relaxed);
    }

    const char* name;
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> total_ns{0};

private:
    static void register_metric(Metric* metric) {
        std::lock_guard<std::mutex> lock(registry_mutex());
        registry().push_back(metric);
    }

    static std::vector<Metric*>& registry() {
        static std::vector<Metric*> metrics;
        return metrics;
    }

    static std::mutex& registry_mutex() {
        static std::mutex mutex;
        return mutex;
    }

    friend void dump(FILE* out);
};

class ScopeTimer {
public:
    explicit ScopeTimer(Metric& metric) : metric(metric), start_time(Clock::now()) {
    }

    ~ScopeTimer() {
        const auto end_time = Clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - this->start_time).count();
        this->metric.add(static_cast<std::uint64_t>(ns));
    }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;
    ScopeTimer(ScopeTimer&&) = delete;
    ScopeTimer& operator=(ScopeTimer&&) = delete;

private:
    Metric& metric;
    Clock::time_point start_time;
};

inline void dump(FILE* out = stderr) {
    std::lock_guard<std::mutex> lock(Metric::registry_mutex());

    std::fprintf(out, "\n--- EVerest performance counters ---\n");

    for (Metric* metric : Metric::registry()) {
        const auto calls = metric->calls.load(std::memory_order_relaxed);
        const auto total_ns = metric->total_ns.load(std::memory_order_relaxed);

        if (calls == 0) {
            continue;
        }

        const double total_ms = static_cast<double>(total_ns) / 1000000.0;
        const double avg_ns = static_cast<double>(total_ns) / static_cast<double>(calls);

        std::fprintf(out, "%-48s calls=%llu total=%.3f ms avg=%.1f ns\n", metric->name,
                     static_cast<unsigned long long>(calls), total_ms, avg_ns);
    }

    std::fprintf(out, "------------------------------------\n");
}

inline void dump_at_exit() {
    std::atexit([] { dump(stderr); });
}

inline std::atomic<bool>& dump_requested() {
    static std::atomic<bool> requested{false};
    return requested;
}

inline void signal_handler(int) {
    dump_requested().store(true, std::memory_order_relaxed);
}

inline void install_signal_dump(const int signal_number = SIGUSR1) {
    std::signal(signal_number, signal_handler);
}

inline void dump_if_requested(FILE* out = stderr) {
    if (dump_requested().exchange(false, std::memory_order_relaxed)) {
        dump(out);
    }
}

} // namespace everest::lib::util::perf

#define EVEREST_UTIL_PERF_CONCAT_IMPL(a, b) a##b
#define EVEREST_UTIL_PERF_CONCAT(a, b) EVEREST_UTIL_PERF_CONCAT_IMPL(a, b)

#define EVEREST_UTIL_PERF_SCOPE(name)                                                                                 \
    static everest::lib::util::perf::Metric EVEREST_UTIL_PERF_CONCAT(_everest_util_perf_metric_, __LINE__){name};     \
    everest::lib::util::perf::ScopeTimer EVEREST_UTIL_PERF_CONCAT(_everest_util_perf_timer_, __LINE__){               \
        EVEREST_UTIL_PERF_CONCAT(_everest_util_perf_metric_, __LINE__)}
