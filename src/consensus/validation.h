// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_VALIDATION_H
#define BITCOIN_CONSENSUS_VALIDATION_H

#include <string>
#include <iostream>

/** "reject" message codes */
enum class RejectionCode : unsigned char
{
	VALIDATION_OK = 0x00,
	REJECT_MALFORMED = 0x01,
	REJECT_INVALID = 0x10,
	REJECT_OBSOLETE = 0x11,
	REJECT_DUPLICATE = 0x12,
	REJECT_NONSTANDARD = 0x40,
	REJECT_DUST = 0x41,       //unused apparently
	REJECT_INSUFFICIENTFEE = 0x42,
	REJECT_CHECKPOINT = 0x43, //unused apparently
	REJECT_CHECKBLOCKATHEIGHT_NOT_FOUND = 0x44,
	REJECT_SCID_NOT_FOUND = 0x45,
	REJECT_INSUFFICIENT_SCID_FUNDS = 0x46,
	REJECT_ABSURDLY_HIGH_FEE = 0x47,
	REJECT_HAS_CONFLICTS = 0x48,
	REJECT_NO_COINS_FOR_INPUT = 0x49,
	REJECT_PROOF_VER_FAILED = 0x4a,
	REJECT_SC_CUM_COMM_TREE = 0x4b,
	REJECT_ACTIVE_CERT_DATA_HASH = 0x4c
};

// The following makes RejectionCode printable in logs
std::ostream& operator<<(std::ostream& os, const RejectionCode& code)
{
    os << static_cast<unsigned char>(code);
    return os;
}

/** Capture information about block/transaction validation */
class CValidationState
{
private:
    enum class State
	{
        VALID,   //! everything ok
        INVALID, //! network rule violation (DoS value may be set)
        ERROR,   //! run-time error
    } mode;
    int nDoS;
    std::string strRejectReason;
    RejectionCode chRejectCode;
    bool corruptionPossible;
public:
    CValidationState() : mode(State::VALID), nDoS(0), chRejectCode(RejectionCode::VALIDATION_OK), corruptionPossible(false) {}
    virtual ~CValidationState() {};

    virtual bool DoS(int level, bool ret = false,
    		RejectionCode chRejectCodeIn =RejectionCode::VALIDATION_OK,
            std::string strRejectReasonIn="", bool corruptionIn=false)
    {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        corruptionPossible = corruptionIn;
        if (mode == State::ERROR)
            return ret;
        nDoS += level;
        mode = State::INVALID;
        return ret;
    }

    virtual bool Invalid(bool ret = false,
                         RejectionCode _chRejectCode=RejectionCode::VALIDATION_OK,
                         std::string _strRejectReason="")
    {
        return DoS(0, ret, _chRejectCode, _strRejectReason);
    }

    virtual bool Error(const std::string& strRejectReasonIn)
    {
        if (mode == State::VALID)
            strRejectReason = strRejectReasonIn;
        mode = State::ERROR;
        return false;
    }

    virtual bool IsValid()   const { return mode == State::VALID; }
    virtual bool IsInvalid() const { return mode == State::INVALID; }
    virtual bool IsError()   const { return mode == State::ERROR; }

    virtual bool IsInvalid(int &nDoSOut) const {
        if (IsInvalid())
        {
            nDoSOut = nDoS;
            return true;
        }
        return false;
    }
    virtual bool CorruptionPossible()     const { return corruptionPossible; }
    virtual RejectionCode GetRejectCode() const { return chRejectCode; }
    virtual std::string GetRejectReason() const { return strRejectReason; }
};

#endif // BITCOIN_CONSENSUS_VALIDATION_H
