// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UNDO_H
#define BITCOIN_UNDO_H

#include "compressor.h" 
#include "primitives/transaction.h"
#include "serialize.h"

/** Undo information for a CTxIn
 *
 *  Contains the prevout's CTxOut being spent, and if this was the
 *  last output of the affected transaction, its metadata as well
 *  (coinbase or not, height, transaction version, originScid and isFromBackwardTransfer)
 *  Following the introduction of sidechain certificates and backward transfer, you need to
 *  serialize the whole metadata in case of a backward transfer in order to be able to duly
 *  reconstruct the isFromBackwardTransfer flag
 */
class CTxInUndo
{
public:
    CTxOut txout;         // the txout data before being spent
    bool fCoinBase;       // if the outpoint was the last unspent: whether it belonged to a coinbase
    unsigned int nHeight; // if the outpoint was the last unspent: its height
    int nVersion;         // if the outpoint was the last unspent: its version
    uint256 originScId;   // if the outpoint was the last unspent: its originScId, introduced with certificates

    CTxInUndo() : txout(), fCoinBase(false), nHeight(0), nVersion(0), originScId() {}
    CTxInUndo(const CTxOut &txoutIn, bool fCoinBaseIn = false, unsigned int nHeightIn = 0, int nVersionIn = 0, const uint256 & _scId = uint256() ):
        txout(txoutIn), fCoinBase(fCoinBaseIn), nHeight(nHeightIn), nVersion(nVersionIn), originScId(_scId) { }

    unsigned int GetSerializeSize(int nType, int nVersion) const {
        unsigned int totalSize = 0;
        totalSize = ::GetSerializeSize(VARINT(nHeight*2+(fCoinBase ? 1 : 0)), nType, nVersion) +
               (nHeight > 0 ? ::GetSerializeSize(VARINT(this->nVersion), nType, nVersion) : 0) +
               ::GetSerializeSize(CTxOutCompressor(REF(txout)), nType, nVersion);
        if ((nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f)) {
            totalSize += ::GetSerializeSize(originScId, nType,nVersion);
            totalSize += ::GetSerializeSize(txout.isFromBackwardTransfer, nType,nVersion);
        }
        return totalSize;
    }

    template<typename Stream>
    void Serialize(Stream &s, int nType, int nVersion) const {
        ::Serialize(s, VARINT(nHeight*2+(fCoinBase ? 1 : 0)), nType, nVersion);
        if (nHeight > 0)
            ::Serialize(s, VARINT(this->nVersion), nType, nVersion);
        ::Serialize(s, CTxOutCompressor(REF(txout)), nType, nVersion);

        if ((this->nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f)) {
            ::Serialize(s,originScId, nType, nVersion);
            ::Serialize(s,txout.isFromBackwardTransfer? true: false, nType, nVersion);
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

        if ((this->nVersion & 0x7f) == (SC_CERT_VERSION & 0x7f)) {
            ::Unserialize(s, originScId, nType, nVersion);
            ::Unserialize(s, txout.isFromBackwardTransfer, nType, nVersion);
        }
    }

    std::string ToString() const
    {
        std::string str;
        str += strprintf("txout(%s)\n", txout.ToString());
        str += strprintf("        fCoinBase=%d\n", fCoinBase);
        str += strprintf("        nHeight=%d\n", nHeight);
        str += strprintf("        nVersion=%d\n", nVersion);
        str += strprintf("        originScId=%s\n", originScId.ToString());
        return str;
    }

};

/** Undo information for a CTransaction */
class CTxUndo
{
public:
    // undo information for all txins
    std::vector<CTxInUndo> vprevout;
    uint256 refTx;            //hash of coins from ceased sidechains. It's not needed for ordinary coins and certs
    unsigned int firstBwtPos; //position of the first bwt.

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        CSizeComputer s(nType, nVersion);
        NCONST_PTR(this)->Serialize(s, nType, nVersion);
        return s.size();
    }

    template<typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const
    {
        if (!refTx.IsNull()) {
            WriteCompactSize(s, ceasedCoinsmarker);
            ::Serialize(s, (vprevout), nType, nVersion);
            ::Serialize(s, (refTx), nType, nVersion);
            ::Serialize(s, (firstBwtPos), nType, nVersion);
        } else {
            ::Serialize(s, (vprevout), nType, nVersion);
        }
    }

    template<typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion)
    {
        // reading from data stream to memory
        vprevout.clear();
        refTx.SetNull();

        unsigned int nSize = ReadCompactSize(s);
        if (nSize == ceasedCoinsmarker) {
            ::Unserialize(s, (vprevout), nType, nVersion);
            ::Unserialize(s, refTx, nType, nVersion);
            ::Unserialize(s, (firstBwtPos), nType, nVersion);
        } else {
            ::AddEntriesInVector(s, vprevout, nType, nVersion, nSize);
        }
    };

    std::string ToString() const
    {
        std::string str;
        str += strprintf("    vprevout.size %u\n", vprevout.size());
        for (unsigned int i = 0; i < vprevout.size(); i++)
            str += "        " + vprevout[i].ToString() + "\n";
        str += strprintf("    refTx %s\n", refTx.ToString());
        str += strprintf("    firstBwtPos %u\n", firstBwtPos);
        return str;
    }


private:
    static const uint16_t ceasedCoinsmarker = 0xfec1;
};

struct ScUndoData
{
    CAmount immAmount;
    int certEpoch;
    uint256 lastCertificateHash;
    
    ScUndoData(): immAmount(0), certEpoch(CScCertificate::EPOCH_NOT_INITIALIZED),
                  lastCertificateHash() {}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(immAmount);
        READWRITE(certEpoch);
        READWRITE(lastCertificateHash);
    }

    std::string ToString() const
    {
        std::string str;
        str += strprintf("  immAmount %d.%08d\n", immAmount / COIN, immAmount % COIN);
        str += strprintf("  certEpoch %d\n", certEpoch);
        str += strprintf("  lastCertificateHash %s\n", lastCertificateHash.ToString().substr(0,10));
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
    static const uint16_t _marker = 0xfec1;

    /** memory only */
    bool includesSidechainAttributes;

public:
    std::vector<CTxUndo> vtxundo; // for all but the coinbase
    uint256 old_tree_root;
    std::map<uint256, ScUndoData> msc_iaundo; // key=scid, value=amount matured at block height

    /** create as new */
    CBlockUndo() : includesSidechainAttributes(true) {}

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        CSizeComputer s(nType, nVersion);
        NCONST_PTR(this)->Serialize(s, nType, nVersion);
        return s.size();
    }   

    template<typename Stream> void Serialize(Stream& s, int nType, int nVersion) const
    {
        if (includesSidechainAttributes)
        {
            WriteCompactSize(s, _marker);
            ::Serialize(s, (vtxundo), nType, nVersion);
            ::Serialize(s, (old_tree_root), nType, nVersion);
            ::Serialize(s, (msc_iaundo), nType, nVersion);
        }
        else
        {
            ::Serialize(s, (vtxundo), nType, nVersion);
            ::Serialize(s, (old_tree_root), nType, nVersion);
        }
    }   

    template<typename Stream> void Unserialize(Stream& s, int nType, int nVersion)
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
            ::Unserialize(s, (msc_iaundo), nType, nVersion);
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
        for (unsigned int i = 0; i < vtxundo.size(); i++)
            str += vtxundo[i].ToString() + "\n";
        str += strprintf("old_tree_root %s\n", old_tree_root.ToString().substr(0,10));
        str += strprintf("msc_iaundo.size %u\n", msc_iaundo.size());
        for (auto entry : msc_iaundo)
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
