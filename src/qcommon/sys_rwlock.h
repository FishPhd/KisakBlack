#pragma once

// Recursive reader/writer lock for CRITSECT_DBHASH (the asset hash table).
//
// A CRITICAL_SECTION serializes every asset lookup against every other one. Lookups are by far the
// most common operation and never modify the table, so they can share the lock; zone loading and
// every other existing call site still take it exclusively.
//
// The exclusive side is recursive, exactly like the CRITICAL_SECTION it replaces: the database
// code enters and leaves it from different functions and re-enters it from inside zone load
// callbacks. A thread that already owns the lock exclusively may also take it shared (that nests
// as another level of exclusive ownership). Shared -> exclusive upgrades are not supported and
// would deadlock, so only code that never writes may use the shared side.
//
// Writers announce themselves so new readers queue behind them (no writer starvation under a
// steady stream of lookups). That is also why a shared section must never take the lock again
// shared: a queued writer would block the nested acquire.

#include <atomic>
#include <cstdint>
#include "sys_futex.h"

class SysRecursiveRWLock
{
public:
    // threadId must be non-zero and unique per thread (Sys_GetCurrentThreadId()).
    void LockExclusive(uint32_t threadId)
    {
        if (m_owner.load(std::memory_order_relaxed) == threadId)
        {
            ++m_recursion;
            return;
        }

        m_waitingWriters.fetch_add(1, std::memory_order_acq_rel);
        unsigned int spins = 0;
        for (;;)
        {
            uint32_t s = m_state.load(std::memory_order_acquire);
            if (s == 0)
            {
                if (m_state.compare_exchange_weak(s, WRITER_BIT, std::memory_order_acq_rel))
                    break;
                continue;
            }
            if (spins < 64)
                Sys_SpinBackoff(spins);
            else
                Sys_FutexWait(&m_state, s, 1);
        }
        m_waitingWriters.fetch_sub(1, std::memory_order_acq_rel);

        m_owner.store(threadId, std::memory_order_relaxed);
        m_recursion = 1;
    }

    bool TryLockExclusive(uint32_t threadId)
    {
        if (m_owner.load(std::memory_order_relaxed) == threadId)
        {
            ++m_recursion;
            return true;
        }

        uint32_t expected = 0;
        if (!m_state.compare_exchange_strong(expected, WRITER_BIT, std::memory_order_acq_rel))
            return false;

        m_owner.store(threadId, std::memory_order_relaxed);
        m_recursion = 1;
        return true;
    }

    void UnlockExclusive()
    {
        if (--m_recursion != 0)
            return;

        m_owner.store(0, std::memory_order_relaxed);
        m_state.store(0, std::memory_order_release);
        Sys_FutexWakeAll(&m_state);
    }

    void LockShared(uint32_t threadId)
    {
        if (m_owner.load(std::memory_order_relaxed) == threadId)
        {
            ++m_recursion; // nested inside our own exclusive section
            return;
        }

        unsigned int spins = 0;
        for (;;)
        {
            uint32_t s = m_state.load(std::memory_order_acquire);
            if (!(s & WRITER_BIT) && m_waitingWriters.load(std::memory_order_acquire) == 0)
            {
                if (m_state.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel))
                    return;
                continue;
            }
            if (spins < 64)
                Sys_SpinBackoff(spins);
            else
                Sys_FutexWait(&m_state, s, 1);
        }
    }

    void UnlockShared(uint32_t threadId)
    {
        if (m_owner.load(std::memory_order_relaxed) == threadId)
        {
            --m_recursion; // matches the nested acquire above; the outer exclusive level remains
            return;
        }

        if (m_state.fetch_sub(1, std::memory_order_acq_rel) == 1)
            Sys_FutexWakeAll(&m_state); // last reader out: wake a waiting writer
    }

private:
    static constexpr uint32_t WRITER_BIT = 0x80000000u;

    std::atomic<uint32_t> m_state{0};          // WRITER_BIT | active reader count
    std::atomic<uint32_t> m_waitingWriters{0};
    std::atomic<uint32_t> m_owner{0};          // thread id of the exclusive owner, 0 if none
    uint32_t m_recursion = 0;                  // only touched by the exclusive owner
};
