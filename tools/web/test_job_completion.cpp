#include <tl/jobqueue/jobqueue_all.h>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

jqBatchGroup::jqBatchGroup() { BatchCount = 0; }

int main() {
    jqBatchGroup group;
    group.ExecutingBatchCount = 1;
    // Deterministically reproduce the split-read race during requeue.
    const auto oldQueued = group.QueuedBatchCount;
    __atomic_fetch_add(&group.QueuedBatchCount, 1u, __ATOMIC_SEQ_CST);
    __atomic_fetch_sub(&group.ExecutingBatchCount, 1u, __ATOMIC_SEQ_CST);
    assert(oldQueued + group.ExecutingBatchCount == 0);
    assert(jqReadBatchCount(&group) != 0);

    std::atomic<bool> started{false}, finished{false};
    std::thread worker([&] {
        started.store(true, std::memory_order_release);
        for (unsigned i = 0; i < 100000; ++i) {
            __atomic_fetch_add(&group.ExecutingBatchCount, 1u, __ATOMIC_SEQ_CST);
            __atomic_fetch_sub(&group.QueuedBatchCount, 1u, __ATOMIC_SEQ_CST);
            __atomic_fetch_add(&group.QueuedBatchCount, 1u, __ATOMIC_SEQ_CST);
            __atomic_fetch_sub(&group.ExecutingBatchCount, 1u, __ATOMIC_SEQ_CST);
        }
        finished.store(true, std::memory_order_release);
    });
    while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
    do { assert(jqReadBatchCount(&group) != 0); } while (!finished.load(std::memory_order_acquire));
    worker.join();
    group.QueuedBatchCount = 0;
    assert(jqReadBatchCount(&group) == 0);
    std::puts("Atomic job completion checks passed");
}
