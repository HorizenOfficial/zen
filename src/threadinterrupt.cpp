// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2023 The Horizen Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "threadinterrupt.h"

CThreadInterrupt::operator bool() const
{
    return flag.load(std::memory_order_acquire);
}

void CThreadInterrupt::reset()
{
    flag.store(false, std::memory_order_release);
}

void CThreadInterrupt::operator()()
{
    {
        std::unique_lock<std::mutex> lock(mut);
        flag.store(true, std::memory_order_release);
    }
    cond.notify_all();
}

