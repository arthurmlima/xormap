#include "xormap_image/parallel.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

class WorkerFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace

TEST_CASE("the default worker count is always usable", "[parallel]")
{
    REQUIRE(xormap_image::default_worker_count() >= 1U);
}

TEST_CASE("parallel_for does no work for an empty range", "[parallel]")
{
    std::atomic<std::size_t> calls{0};
    xormap_image::parallel_for(0, 8, [&calls](std::size_t) {
        calls.fetch_add(1, std::memory_order_relaxed);
    });
    REQUIRE(calls.load(std::memory_order_relaxed) == 0U);
}

TEST_CASE("parallel_for executes every index exactly once", "[parallel]")
{
    constexpr std::size_t task_count = 257;
    std::vector<std::atomic<unsigned int>> calls(task_count);
    for (auto& count : calls) {
        count.store(0U, std::memory_order_relaxed);
    }

    xormap_image::parallel_for(task_count, 8, [&calls](std::size_t index) {
        calls[index].fetch_add(1U, std::memory_order_relaxed);
    });

    for (const auto& count : calls) {
        REQUIRE(count.load(std::memory_order_relaxed) == 1U);
    }
}

TEST_CASE("parallel_for supports automatic worker selection", "[parallel]")
{
    constexpr std::size_t task_count = 43;
    std::vector<std::atomic<unsigned int>> calls(task_count);
    for (auto& count : calls) {
        count.store(0U, std::memory_order_relaxed);
    }

    xormap_image::parallel_for(task_count, 0, [&calls](std::size_t index) {
        calls[index].fetch_add(1U, std::memory_order_relaxed);
    });

    for (const auto& count : calls) {
        REQUIRE(count.load(std::memory_order_relaxed) == 1U);
    }
}

TEST_CASE("one requested worker is sequential and ordered", "[parallel]")
{
    const std::thread::id caller = std::this_thread::get_id();
    std::vector<std::size_t> visited;
    std::vector<std::thread::id> thread_ids;

    xormap_image::parallel_for(12, 1,
                               [&](std::size_t index) {
                                   visited.push_back(index);
                                   thread_ids.push_back(std::this_thread::get_id());
                               });

    REQUIRE(visited ==
            std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11});
    for (const auto thread_id : thread_ids) {
        REQUIRE(thread_id == caller);
    }
}

TEST_CASE("a single task remains sequential even when more workers are requested",
          "[parallel]")
{
    const std::thread::id caller = std::this_thread::get_id();
    std::thread::id executed_on;
    xormap_image::parallel_for(1, 64, [&](std::size_t index) {
        REQUIRE(index == 0U);
        executed_on = std::this_thread::get_id();
    });
    REQUIRE(executed_on == caller);
}

TEST_CASE("parallel_for propagates worker exceptions after joining",
          "[parallel][errors]")
{
    std::atomic<std::size_t> successful_calls{0};
    std::atomic<std::size_t> active_calls{0};

    try {
        xormap_image::parallel_for(128, 4, [&](std::size_t index) {
            active_calls.fetch_add(1U, std::memory_order_relaxed);
            if (index == 3U) {
                active_calls.fetch_sub(1U, std::memory_order_relaxed);
                throw WorkerFailure("worker 3 failed");
            }
            successful_calls.fetch_add(1U, std::memory_order_relaxed);
            active_calls.fetch_sub(1U, std::memory_order_relaxed);
        });
        FAIL("parallel_for did not propagate the worker exception");
    } catch (const WorkerFailure& error) {
        REQUIRE(std::string(error.what()) == "worker 3 failed");
    }

    REQUIRE(active_calls.load(std::memory_order_relaxed) == 0U);
    REQUIRE(successful_calls.load(std::memory_order_relaxed) < 128U);
}

TEST_CASE("sequential parallel_for propagates exceptions immediately",
          "[parallel][errors]")
{
    std::vector<std::size_t> visited;
    REQUIRE_THROWS_AS(
        xormap_image::parallel_for(8, 1, [&](std::size_t index) {
            visited.push_back(index);
            if (index == 2U) {
                throw WorkerFailure("sequential failure");
            }
        }),
        WorkerFailure);
    REQUIRE(visited == std::vector<std::size_t>{0, 1, 2});
}
