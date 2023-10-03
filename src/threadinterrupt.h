// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_THREADINTERRUPT_H
#define BITCOIN_THREADINTERRUPT_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

/*
    A helper class for interruptible sleeps. Calling operator() will interrupt
    any current sleep, and after that point operator bool() will return true
    until reset.
*/
class CThreadInterrupt
{
public:
    explicit operator bool() const;
    void operator()();
    void reset();

    template <typename chronodurationtype>
    bool sleep_for(chronodurationtype rel_time) {
        std::unique_lock<std::mutex> lock(mut);
        return !cond.wait_for(lock, rel_time,
            [this]() { return flag.load(std::memory_order_acquire); }
        );
    }

private:
    std::condition_variable cond;
    std::mutex mut;
    std::atomic<bool> flag;
};

#endif // BITCOIN_THREADINTERRUPT_H
