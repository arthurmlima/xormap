#ifndef XORMAP_IMAGE_PARALLEL_HPP
#define XORMAP_IMAGE_PARALLEL_HPP

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace xormap_image {

inline std::size_t default_worker_count()
{
    const auto detected = static_cast<std::size_t>(std::thread::hardware_concurrency());
    return detected == 0 ? 1 : detected;
}

// Executes fn(index) exactly once for every index in [0, task_count). Work is
// dynamically scheduled, which keeps the mixed 256/512/1024-pixel SIPI tasks
// balanced. The first worker exception is rethrown after every thread joins.
template <typename Function>
void parallel_for(std::size_t task_count, std::size_t requested_workers, Function&& fn)
{
    if (task_count == 0) {
        return;
    }

    const std::size_t worker_count =
        std::max<std::size_t>(1, std::min(task_count, requested_workers == 0
                                                          ? default_worker_count()
                                                          : requested_workers));
    if (worker_count == 1) {
        for (std::size_t index = 0; index < task_count; ++index) {
            fn(index);
        }
        return;
    }

    std::atomic<std::size_t> next{0};
    std::atomic<bool> cancelled{false};
    std::exception_ptr first_error;
    std::mutex error_mutex;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    std::exception_ptr launch_error;
    try {
        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.emplace_back([&]() {
                while (!cancelled.load(std::memory_order_relaxed)) {
                    const std::size_t index =
                        next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= task_count) {
                        break;
                    }
                    try {
                        fn(index);
                    } catch (...) {
                        {
                            std::lock_guard<std::mutex> lock(error_mutex);
                            if (!first_error) {
                                first_error = std::current_exception();
                            }
                        }
                        cancelled.store(true, std::memory_order_relaxed);
                        break;
                    }
                }
            });
        }
    } catch (...) {
        // Destroying a joinable std::thread calls std::terminate. Preserve the
        // launch failure, stop new work, and join every thread already made.
        launch_error = std::current_exception();
        cancelled.store(true, std::memory_order_relaxed);
    }

    for (auto& worker : workers) {
        worker.join();
    }
    if (launch_error) {
        std::rethrow_exception(launch_error);
    }
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

}  // namespace xormap_image

#endif
