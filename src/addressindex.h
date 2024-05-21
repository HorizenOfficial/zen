// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2021-2023 Zen Blockchain Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ADDRESSINDEX_H
#define BITCOIN_ADDRESSINDEX_H

#include "uint256.h"
#include "amount.h"
#include "script/script.h"

enum class AddressType {
    UNKNOWN = 0,
    PUBKEY = 1,
    SCRIPT = 2
};

struct CAddressUnspentKey {
    AddressType type;
    uint160 hashBytes;
    uint256 txhash;
    size_t index;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 57;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, static_cast<uint8_t>(type)); //uint8_t is used for backward compatibility
        hashBytes.Serialize(s, nType, nVersion);
        txhash.Serialize(s, nType, nVersion);
        ser_writedata32(s, index);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = static_cast<AddressType>(ser_readdata8(s)); //uint8_t is used for backward compatibility
        hashBytes.Unserialize(s, nType, nVersion);
        txhash.Unserialize(s, nType, nVersion);
        index = ser_readdata32(s);
    }

    CAddressUnspentKey(AddressType addressType, uint160 addressHash, uint256 txid, size_t indexValue) {
        type = addressType;
        hashBytes = addressHash;
        txhash = txid;
        index = indexValue;
    }

    CAddressUnspentKey() {
        SetNull();
    }

    void SetNull() {
        type = AddressType::UNKNOWN;
        hashBytes.SetNull();
        txhash.SetNull();
        index = 0;
    }
};

struct CAddressUnspentValue {
    CAmount satoshis;
    CScript script;
    int blockHeight;
    int maturityHeight;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(satoshis);
        READWRITE(script);
        READWRITE(blockHeight);
        
        // Since the maturity can be negative, we have to manipulate it to store the sign bit in a VARINT
        READWRITE_VARINT_WITH_SIGN(maturityHeight);
    }

    CAddressUnspentValue(CAmount sats, CScript scriptPubKey, int height, int maturity) {
        satoshis = sats;
        script = scriptPubKey;
        blockHeight = height;
        maturityHeight = maturity;
    }

    CAddressUnspentValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        script.clear();
        blockHeight = 0;
        maturityHeight = 0;
    }

    bool IsNull() const {
        return (satoshis == -1);
    }
};

struct CAddressIndexKey {
    AddressType type;
    uint160 hashBytes;
    int blockHeight;
    unsigned int txindex;
    uint256 txhash;
    size_t index;
    bool spending;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 66;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, static_cast<uint8_t>(type)); //uint8_t is used for backward compatibility
        hashBytes.Serialize(s, nType, nVersion);
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        ser_writedata32be(s, txindex);
        txhash.Serialize(s, nType, nVersion);
        ser_writedata32(s, index);
        char f = spending;
        ser_writedata8(s, f);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = static_cast<AddressType>(ser_readdata8(s)); //uint8_t is used for backward compatibility
        hashBytes.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
        txindex = ser_readdata32be(s);
        txhash.Unserialize(s, nType, nVersion);
        index = ser_readdata32(s);
        char f = ser_readdata8(s);
        spending = f;
    }

    CAddressIndexKey(AddressType addressType, uint160 addressHash, int height, int blockindex,
                     uint256 txid, size_t indexValue, bool isSpending) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
        txindex = blockindex;
        txhash = txid;
        index = indexValue;
        spending = isSpending;
    }

    CAddressIndexKey() {
        SetNull();
    }

    void SetNull() {
        type = AddressType::UNKNOWN;
        hashBytes.SetNull();
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        index = 0;
        spending = false;
    }

};

struct CAddressIndexValue {
    CAmount satoshis;
    int maturityHeight;     // It can contain negative numbers

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(satoshis);

        // Since the maturity can be negative, we have to manipulate it to store the sign bit in a VARINT
        READWRITE_VARINT_WITH_SIGN(maturityHeight);
    }

    CAddressIndexValue(CAmount sats, int height) {
        satoshis = sats;
        maturityHeight = height;
    }

    CAddressIndexValue() {
        SetNull();
    }

    void SetNull() {
        satoshis = -1;
        maturityHeight = 0;
    }

    bool IsNull() const {
        return satoshis == -1 && maturityHeight == 0;
    }
};

struct CAddressIndexIteratorKey {
    AddressType type;
    uint160 hashBytes;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 21;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, static_cast<uint8_t>(type)); //uint8_t is used for backward compatibility
        hashBytes.Serialize(s, nType, nVersion);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = static_cast<AddressType>(ser_readdata8(s)); //uint8_t is used for backward compatibility
        hashBytes.Unserialize(s, nType, nVersion);
    }

    CAddressIndexIteratorKey(AddressType addressType, uint160 addressHash) {
        type = addressType;
        hashBytes = addressHash;
    }

    CAddressIndexIteratorKey() {
        SetNull();
    }

    void SetNull() {
        type = AddressType::UNKNOWN;
        hashBytes.SetNull();
    }
};

struct CAddressIndexIteratorHeightKey {
    AddressType type;
    uint160 hashBytes;
    int blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const {
        return 25;
    }
    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        ser_writedata8(s, static_cast<uint8_t>(type)); //uint8_t is used for backward compatibility
        hashBytes.Serialize(s, nType, nVersion);
        ser_writedata32be(s, blockHeight);
    }
    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        type = static_cast<AddressType>(ser_readdata8(s)); //uint8_t is used for backward compatibility
        hashBytes.Unserialize(s, nType, nVersion);
        blockHeight = ser_readdata32be(s);
    }

    CAddressIndexIteratorHeightKey(AddressType addressType, uint160 addressHash, int height) {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
    }

    CAddressIndexIteratorHeightKey() {
        SetNull();
    }

    void SetNull() {
        type = AddressType::UNKNOWN;
        hashBytes.SetNull();
        blockHeight = 0;
    }
};

struct CMempoolAddressDelta
{
    enum OutputStatus
    {
        // do not change order or values, rpc clients might relay on that 
        // --
        ORDINARY_OUTPUT = 0,                    /**< the output of an ordnary tx or a non-bwt output of a certificate (e.g. change) */ 
        TOP_QUALITY_CERT_BACKWARD_TRANSFER = 1, /**< top quality certificate, it has a possibility to reach maturity one day*/
        LOW_QUALITY_CERT_BACKWARD_TRANSFER = 2, /**< low quality compared to another cert for the same scid in the mempool */
        NOT_APPLICABLE = 0xFF                   /**< not an output: the mempool map refer to both inputs and outputs */
    };

    static std::string OutputStatusToString(enum OutputStatus s)
    {
        switch(s) {
            case ORDINARY_OUTPUT:                    return "ORDINARY";
            case TOP_QUALITY_CERT_BACKWARD_TRANSFER: return "TOP_QUALITY_MEMPOOL";
            case LOW_QUALITY_CERT_BACKWARD_TRANSFER: return "LOW_QUALITY_MEMPOOL";
            default: break;
        }
        return "UNKNOWN";
    };

    int64_t time;
    CAmount amount;
    uint256 prevhash;
    unsigned int prevout;
    OutputStatus outStatus;

    // used for inputs
    CMempoolAddressDelta(int64_t t, CAmount a, uint256 hash, unsigned int out) {
        time = t;
        amount = a;
        prevhash = hash;
        prevout = out;
        outStatus = NOT_APPLICABLE;
    }

    // used for outputs
    CMempoolAddressDelta(int64_t t, CAmount a, OutputStatus status = ORDINARY_OUTPUT) {
        time = t;
        amount = a;
        prevhash.SetNull();
        prevout = 0;
        outStatus = status;
    }

    // default ctor
    CMempoolAddressDelta() {
        time = 0;
        amount = 0;
        prevhash.SetNull();
        prevout = 0;
        outStatus = NOT_APPLICABLE;
    }

};

struct CMempoolAddressDeltaKey
{
    AddressType type;
    uint160 addressBytes;
    uint256 txhash;
    unsigned int index;
    int spending;

    CMempoolAddressDeltaKey(AddressType addressType, uint160 addressHash, uint256 hash, unsigned int i, int s) {
        type = addressType;
        addressBytes = addressHash;
        txhash = hash;
        index = i;
        spending = s;
    }

    CMempoolAddressDeltaKey(AddressType addressType, uint160 addressHash) {
        type = addressType;
        addressBytes = addressHash;
        txhash.SetNull();
        index = 0;
        spending = 0;
    }
};

struct CMempoolAddressDeltaKeyCompare
{
    bool operator()(const CMempoolAddressDeltaKey& a, const CMempoolAddressDeltaKey& b) const {
        if (a.type == b.type) {
            if (a.addressBytes == b.addressBytes) {
                if (a.txhash == b.txhash) {
                    if (a.index == b.index) {
                        return a.spending < b.spending;
                    } else {
                        return a.index < b.index;
                    }
                } else {
                    return a.txhash < b.txhash;
                }
            } else {
                return a.addressBytes < b.addressBytes;
            }
        } else {
            return a.type < b.type;
        }
    }
};

//! Retrieves from script type the associated address type
/*!
  \param scriptType the script type used to determine address type
  \return the associated address type
*/
inline AddressType fromScriptTypeToAddressType(CScript::ScriptType scriptType)
{
    AddressType addressType = AddressType::UNKNOWN;
    if (scriptType == CScript::ScriptType::P2PKH || scriptType == CScript::ScriptType::P2PK)
        addressType = AddressType::PUBKEY;
    else if (scriptType == CScript::ScriptType::P2SH)
        addressType = AddressType::SCRIPT;
    return addressType;
}

#endif // BITCOIN_ADDRESSINDEX_H
