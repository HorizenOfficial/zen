// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"

#include "tinyformat.h"

const std::string CURRENCY_UNIT = "ZEN";

CFeeRate::CFeeRate(const CAmount& nFeePaid, size_t nSize)
{
    if (nSize > 0)
        nSatoshisPerK = nFeePaid*1000/nSize;
    else
        nSatoshisPerK = 0;
}

CAmount CFeeRate::GetFee(size_t nSize) const
{
    CAmount nFee = nSatoshisPerK*nSize / 1000;

    if (nFee == 0 && nSatoshisPerK > 0)
        nFee = nSatoshisPerK;

    return nFee;
}

std::string CFeeRate::ToString() const
{
    return strprintf("%d.%08d %s/kB", nSatoshisPerK / COIN, nSatoshisPerK % COIN, CURRENCY_UNIT);
}

void CRawFeeRate::operator+=(const CRawFeeRate& rhs) {
    if (isMax() || rhs.isMax()) {
        fee   = MAX_FEE;
        bytes = 1;
    }
    else {
        fee += rhs.fee;
        bytes += rhs.bytes;
    }
    SetSatoshisPerK();
}

void CRawFeeRate::SetSatoshisPerK() {
    if (isMax()) {
        nSatoshisPerK = MAX_FEE;
    }
    else {
        nSatoshisPerK = bytes ? ((1000 * fee) / bytes) : 0;
    }
}
