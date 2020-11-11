// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UNDO_H
#define BITCOIN_UNDO_H

#include "compressor.h" 
#include "primitives/transaction.h"
#include "serialize.h"
#include "coins.h"

/** Undo information for a CTxIn
 *
 *  Contains the prevout's CTxOut being spent, and if this was the
 *  last output of the affected transaction, its metadata as well
 *  (coinbase or not, height, transaction version, originScid)
 *  Following the introduction of sidechain certificates and backward transfer,
 *  nFirstBwtPos is serialized for certificates.
 */
class CTxInUndo
{
public:
    CTxOut txout;           // the txout data before being spent
    bool fCoinBase;         // if the outpoint was the last unspent: whether it belonged to a coinbase
    unsigned int nHeight;   // if the outpoint was the last unspent: its height
    int nVersion;           // if the outpoint was the last unspent: its version
    int nFirstBwtPos;       // if the outpoint was the last unspent: its nFirstBwtPos, serialized only for certificates
    int nBwtMaturityHeight; // if the outpoint was the last unspent: its nBwtMaturityHeight, introduced with certificates

    CTxInUndo() : txout(), fCoinBase(false), nHeight(0), nVersion(0), nFirstBwtPos(BWT_POS_UNSET), nBwtMaturityHeight(0) {}
    CTxInUndo(const CTxOut &txoutIn, bool fCoinBaseIn = false,
              unsigned int nHeightIn = 0, int nVersionIn = 0,
              int firstBwtPos = BWT_POS_UNSET, int bwtMaturityHeight = 0):
        txout(txoutIn), fCoinBase(fCoinBaseIn), nHeight(nHeightIn), nVersion(nVersionIn),
        nFirstBwtPos(firstBwtPos), nBwtMaturityHeight(bwtMaturityHeight) {}

    unsigned int GetSerializeSize(int nType, int nVersion) const {
        unsigned int totalSize = 0;
        totalSize = ::GetSerializeSize(VARINT(nHeight*2+(fCoinBase ? 1 : 0)), nType, nVersion) +
               (nHeight > 0 ? ::GetSerializeSize(VARINT(this->nVersion), nType, nVersion) : 0) +
               ::GetSerializeSize(CTxOutCompressor(REF(txout)), nType, nVersion);
        if ((nHeight > 0) && ((this->nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))) {
            totalSize += ::GetSerializeSize(nFirstBwtPos, nType,nVersion);
            totalSize += ::GetSerializeSize(nBwtMaturityHeight, nType,nVersion);
        }
        return totalSize;
    }

    template<typename Stream>
    void Serialize(Stream &s, int nType, int nVersion) const {
        ::Serialize(s, VARINT(nHeight*2+(fCoinBase ? 1 : 0)), nType, nVersion);
        if (nHeight > 0)
            ::Serialize(s, VARINT(this->nVersion), nType, nVersion);
        ::Serialize(s, CTxOutCompressor(REF(txout)), nType, nVersion);

        if ((nHeight > 0) && ((this->nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))) {
            ::Serialize(s, nFirstBwtPos, nType, nVersion);
            ::Serialize(s, nBwtMaturityHeight, nType, nVersion);
        }
    }

    template<typename Stream>
    void Unserialize(Stream &s, int nType, int nVersion) {
        unsigned int nCode = 0;
        ::Unserialize(s, VARINT(nCode), nType, nVersion);
        nHeight = nCode / 2;
        fCoinBase = nCode & 1;
        if (nHeight > 0)
            ::Unserialize(s, VARINT(this->nVersion), nType, nVersion);
        ::Unserialize(s, REF(CTxOutCompressor(REF(txout))), nType, nVersion);

        if ((nHeight > 0) && ((this->nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f))) {
            ::Unserialize(s, nFirstBwtPos, nType, nVersion);
            ::Unserialize(s, nBwtMaturityHeight, nType, nVersion);
        }
    }

    std::string ToString() const
    {
        std::string str;
        str += strprintf("txout(%s)\n", txout.ToString());
        str += strprintf("        fCoinBase         = %d\n", fCoinBase);
        str += strprintf("        nHeight           = %d\n", nHeight);
        str += strprintf("        nVersion          = %x\n", nVersion);
        str += strprintf("        nFirstBwtPos      = %d\n", nFirstBwtPos);
        str += strprintf("        nBwtMaturityHeight= %d\n", nBwtMaturityHeight);
        return str;
    }

};

class CVoidedCertUndo {
public:
    std::vector<CTxInUndo> voidedOuts;;
    uint256 voidedCertScId;

    CVoidedCertUndo(): voidedOuts(),voidedCertScId() {};

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(voidedOuts);
        READWRITE(voidedCertScId);
    }

    std::string ToString() const
    {
        std::string str;
        str += strprintf("    voidedOuts.size %u\n", voidedOuts.size());
        for (unsigned int i = 0; i < voidedOuts.size(); i++)
            str += "        " + voidedOuts[i].ToString() + "\n";
        str += strprintf("    voidedCertScId       %s\n", voidedCertScId.ToString());
        return str;
    }
};

/** Undo information for a CTransaction */
class CTxUndo
{
public:
    std::vector<CTxInUndo> vprevout; // undo information for all txins

    //for cert only, to restore ScInfo
    int replacedLastCertEpoch;
    uint256 replacedLastCertHash;
    int64_t replacedLastCertQuality;

    CTxUndo(): vprevout(), replacedLastCertEpoch(CScCertificate::EPOCH_NOT_INITIALIZED),
            replacedLastCertHash(), replacedLastCertQuality(CScCertificate::QUALITY_NOT_INITIALIZED) {}

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        CSizeComputer s(nType, nVersion);
        NCONST_PTR(this)->Serialize(s, nType, nVersion);
        return s.size();
    };

    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const
    {
        if (replacedLastCertEpoch != CScCertificate::EPOCH_NOT_INITIALIZED) {
            WriteCompactSize(s, certAttributesMarker);
            ::Serialize(s, vprevout,                nType, nVersion);
            ::Serialize(s, replacedLastCertEpoch,   nType, nVersion);
            ::Serialize(s, replacedLastCertHash,    nType, nVersion);
            ::Serialize(s, replacedLastCertQuality, nType, nVersion);
        }
        else {
            ::Serialize(s, vprevout, nType, nVersion);
        }
    };

    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion)
    {
        // reading from data stream to memory
        vprevout.clear();
        replacedLastCertEpoch = CScCertificate::EPOCH_NOT_INITIALIZED;
        replacedLastCertHash.SetNull();
        replacedLastCertQuality = CScCertificate::QUALITY_NOT_INITIALIZED;

        unsigned int nSize = ReadCompactSize(s);
        if (nSize == certAttributesMarker)
        {
            ::Unserialize(s, vprevout, nType, nVersion);
            ::Unserialize(s, replacedLastCertEpoch,   nType, nVersion);
            ::Unserialize(s, replacedLastCertHash,    nType, nVersion);
            ::Unserialize(s, replacedLastCertQuality, nType, nVersion);
        }
        else
            ::AddEntriesInVector(s, vprevout, nType, nVersion, nSize);
    };

    std::string ToString() const
    {
        std::string str;
        str += strprintf("vprevout.size %u\n", vprevout.size());
        for(const CTxInUndo& in: vprevout)
            str += strprintf("\n  [%s]\n", in.ToString());
        str += strprintf("replacedLastCertEpoch   %d\n", replacedLastCertEpoch);
        str += strprintf("replacedLastCertHash    %s\n", replacedLastCertHash.ToString());
        str += strprintf("replacedLastCertQuality %d\n", replacedLastCertQuality);
        return str;
    }

private:
    static const uint16_t certAttributesMarker = 0xffff;
};

struct ScUndoData
{
    CAmount appliedMaturedAmount;
    ScUndoData(): appliedMaturedAmount(0) {}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(appliedMaturedAmount);
    }

    std::string ToString() const
    {
        std::string str;
        str += strprintf("  immAmount %d.%08d\n", appliedMaturedAmount / COIN, appliedMaturedAmount % COIN);
        return str;
    }
};

/** Undo information for a CBlock */
class CBlockUndo
{
    /** Magic number read from the value expressing the size of vtxundo vector.
     *  It is used for distinguish new version of CBlockUndo instance from old ones.
     *  The maximum number of tx in a block is roughly MAX_BLOCK_SIZE / MIN_TX_SIZE, which is:
     *   2M / 61bytes =~ 33K = 0x8012
     * Therefore the magic number must be a number greater than this limit. */
    static const uint16_t _marker = 0xffff;

    static_assert(_marker > (MAX_BLOCK_SIZE / MIN_TX_SIZE),
        "CBlockUndo::_marker must be greater than max number of tx in a block!");

    /** memory only */
    bool includesSidechainAttributes;

public:
    std::vector<CTxUndo> vtxundo; // for all txs and certs but the coinbase
    uint256 old_tree_root;
    std::map<uint256, ScUndoData> scUndoMap;       // key=scid, value=amount matured at block height
    std::vector<CVoidedCertUndo>  vVoidedCertUndo; // for voided backward transfers

    /** create as new */
    CBlockUndo() : includesSidechainAttributes(true) {}

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        CSizeComputer s(nType, nVersion);
        NCONST_PTR(this)->Serialize(s, nType, nVersion);
        return s.size();
    }   

    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const
    {
        if (includesSidechainAttributes)
        {
            WriteCompactSize(s, _marker);
            ::Serialize(s, (vtxundo), nType, nVersion);
            ::Serialize(s, (old_tree_root), nType, nVersion);
            ::Serialize(s, (scUndoMap), nType, nVersion);
            ::Serialize(s, (vVoidedCertUndo), nType, nVersion);
        }
        else
        {
            ::Serialize(s, (vtxundo), nType, nVersion);
            ::Serialize(s, (old_tree_root), nType, nVersion);
        }
    }   

    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion)
    {
        // reading from data stream to memory
        vtxundo.clear();
        includesSidechainAttributes = false;

        unsigned int nSize = ReadCompactSize(s);
        if (nSize == _marker)
        {
            // this is a new version of blockundo
            ::Unserialize(s, (vtxundo), nType, nVersion);
            ::Unserialize(s, (old_tree_root), nType, nVersion);
            ::Unserialize(s, (scUndoMap), nType, nVersion);
            ::Unserialize(s, (vVoidedCertUndo), nType, nVersion);
            includesSidechainAttributes = true;
        }
        else
        {
            // vtxundo size has been already consumed in stream, add its entries
            ::AddEntriesInVector(s, vtxundo, nType, nVersion, nSize);
            ::Unserialize(s, (old_tree_root), nType, nVersion);
        }
    };

    std::string ToString() const
    {
        std::string str = "\n=== CBlockUndo START ===========================================================================\n";
        str += strprintf("includesSidechainAttributes=%u (mem only)\n", includesSidechainAttributes);
        str += strprintf("vtxundo.size %u\n", vtxundo.size());
        for(const CTxUndo& txUndo: vtxundo)
            str += txUndo.ToString() + "\n";

        str += strprintf("scUndoMap.size %u\n", scUndoMap.size());
        for(const auto& pair: scUndoMap)
            str += strprintf("%s --> %s\n", pair.first.ToString().substr(0,10), pair.second.ToString());

        str += strprintf("vVoidedCertUndo.size %u\n", vVoidedCertUndo.size());
        for(const CVoidedCertUndo& voidCertUndo: vVoidedCertUndo)
            str += voidCertUndo.ToString() + "\n";
        str += strprintf("old_tree_root %s\n", old_tree_root.ToString().substr(0,10));
        str += strprintf("msc_iaundo.size %u\n", scUndoMap.size());
        for (auto entry : scUndoMap)
            str += strprintf("%s --> %s\n", entry.first.ToString().substr(0,10), entry.second.ToString());
        str += strprintf(" ---> obj size %u\n", GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION));
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << *this;
        str += strprintf("      obj hash [%s]\n", hasher.GetHash().ToString());
        str += "=== CBlockUndo END =============================================================================\n";
        return str;
    }

    /** for testing */
    bool IncludesSidechainAttributes() const  { return includesSidechainAttributes; }

};
#endif // BITCOIN_UNDO_H
