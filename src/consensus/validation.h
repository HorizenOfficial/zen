// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_VALIDATION_H
#define BITCOIN_CONSENSUS_VALIDATION_H

#include <string>

/** Capture information about block/transaction validation */
class CValidationState {
  public:
    CValidationState() : mode(State::VALID), nDoS(0), chRejectCode(Code::OK), corruptionPossible(false) {}
    ~CValidationState(){};

    /** "reject" message codes */
    enum class Code : unsigned char
    {
        OK = 0x00,
        MALFORMED = 0x01,
        INVALID = 0x10,
        OBSOLETE = 0x11,
        DUPLICATED = 0x12,
        NONSTANDARD = 0x40,
        DUST = 0x41,  // unused apparently
        INSUFFICIENT_FEE = 0x42,
        CHECKPOINT = 0x43,  // unused apparently
        CHECKBLOCKATHEIGHT_NOT_FOUND = 0x44,
        SCID_NOT_FOUND = 0x45,
        INSUFFICIENT_SCID_FUNDS = 0x46,
        ABSURDLY_HIGH_FEE = 0x47,
        HAS_CONFLICTS = 0x48,
        NO_COINS_FOR_INPUT = 0x49,
        INVALID_PROOF = 0x4a,
        SC_CUM_COMM_TREE = 0x4b,
        ACTIVE_CERT_DATA_HASH = 0x4c,
        TOO_MANY_CSW_INPUTS_FOR_SC = 0x4d
    };

    // The following makes CValidationState::Code serializable
    static unsigned char CodeToChar(Code code) { return static_cast<unsigned char>(code); }

    bool DoS(int level, bool ret = false, Code chRejectCodeIn = Code::OK, std::string strRejectReasonIn = "",
             bool corruptionIn = false) {
        chRejectCode = chRejectCodeIn;
        strRejectReason = strRejectReasonIn;
        corruptionPossible = corruptionIn;
        if (mode == State::RUNTIME_ERROR) return ret;
        nDoS += level;
        mode = State::INVALID;
        return ret;
    }

    bool Invalid(bool ret = false, Code _chRejectCode = Code::OK, std::string _strRejectReason = "") {
        return DoS(0, ret, _chRejectCode, _strRejectReason);
    }

    bool Error(const std::string& strRejectReasonIn) {
        if (mode == State::VALID) strRejectReason = strRejectReasonIn;
        mode = State::RUNTIME_ERROR;
        return false;
    }

    bool IsValid() const { return mode == State::VALID; }
    bool IsInvalid() const { return mode == State::INVALID; }
    bool IsError() const { return mode == State::RUNTIME_ERROR; }

    int GetDoS() const { return nDoS; }

    bool CorruptionPossible() const { return corruptionPossible; }
    Code GetRejectCode() const { return chRejectCode; }
    std::string GetRejectReason() const { return strRejectReason; }

  private:
    enum class State
    {
        VALID,          //! everything ok
        INVALID,        //! network rule violation (DoS value may be set)
        RUNTIME_ERROR,  //! run-time error
    } mode;

    int nDoS;
    std::string strRejectReason;
    Code chRejectCode;
    bool corruptionPossible;
};

#endif  // BITCOIN_CONSENSUS_VALIDATION_H
