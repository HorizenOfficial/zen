// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "coins.h"
#include "leveldbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockFileInfo;
class CBlockIndex;
struct CDiskTxPos;
class uint256;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 100;
//! max. -dbcache in (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;

/** CCoinsView backed by the LevelDB coin database (chainstate/) */
class CCoinsViewDB : public CCoinsView
{
protected:
    CLevelDBWrapper db;
    CCoinsViewDB(std::string dbName, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
public:
    CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const;
    bool GetNullifier(const uint256 &nf) const;
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;
    bool GetSidechain(const uint256& scId, CSidechain& info) const;
    bool HaveSidechain(const uint256& scId) const;
    void queryScIds(std::set<uint256>& scIdsList) const;
    uint256 GetBestBlock() const;
    uint256 GetBestAnchor() const;
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashAnchor,
                    CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers,
                    CSidechainsMap& mapSidechains);
    bool GetStats(CCoinsStats &stats) const;
    void Dump_info() const;

private:
    /**
     * Serialized format:
     * - VARINT(nVersion)
     * - VARINT(nCode)
     * - unspentness bitvector, for vout[2] and further; least significant byte first
     * - the non-spent CTxOuts (via CTxOutCompressor)
     * - VARINT(nHeight)
     *
     * The nCode value consists of:
     * - bit 1: IsCoinBase()
     * - bit 2: vout[0] is not spent
     * - bit 4: vout[1] is not spent
     * - The higher bits encode N, the number of non-zero bytes in the following bitvector.
     *   - In case both bit 2 and bit 4 are unset, they encode N-1, as there must be at
     *     least one non-spent output).
     *
     * Example: 0104835800816115944e077fe7c803cfa57f29b36bf87c1d358bb85e
     *          <><><--------------------------------------------><---->
     *          |  \                  |                             /
     *    version   code             vout[1]                  height
     *
     *    - version = 1
     *    - code = 4 (vout[1] is not spent, and 0 non-zero bytes of bitvector follow)
     *    - unspentness bitvector: as 0 non-zero bytes follow, it has length 0
     *    - vout[1]: 835800816115944e077fe7c803cfa57f29b36bf87c1d35
     *               * 8358: compact amount representation for 60000000000 (600 BTC)
     *               * 00: special txout type pay-to-pubkey-hash
     *               * 816115944e077fe7c803cfa57f29b36bf87c1d35: address uint160
     *    - height = 203998
     *
     *
     * Example: 0109044086ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4eebbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa486af3b
     *          <><><--><--------------------------------------------------><----------------------------------------------><---->
     *         /  \   \                     |                                                           |                     /
     *  version  code  unspentness       vout[4]                                                     vout[16]           height
     *
     *  - version = 1
     *  - code = 9 (coinbase, neither vout[0] or vout[1] are unspent,
     *                2 (1, +1 because both bit 2 and bit 4 are unset) non-zero bitvector bytes follow)
     *  - unspentness bitvector: bits 2 (0x04) and 14 (0x4000) are set, so vout[2+2] and vout[14+2] are unspent
     *  - vout[4]: 86ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4ee
     *             * 86ef97d579: compact amount representation for 234925952 (2.35 BTC)
     *             * 00: special txout type pay-to-pubkey-hash
     *             * 61b01caab50f1b8e9c50a5057eb43c2d9563a4ee: address uint160
     *  - vout[16]: bbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa4
     *              * bbd123: compact amount representation for 110397 (0.001 BTC)
     *              * 00: special txout type pay-to-pubkey-hash
     *              * 8c988f1a4a4de2161e0f50aac7f17e7f9555caa4: address uint160
     *  - height = 120891
     */
    template<typename Stream>
    static void CCoinsCoreSerialize(const CCoins & coin, Stream &s, int nType, int nVersion) {
        unsigned int nMaskSize = 0, nMaskCode = 0;
        CalcMaskSize(coin, nMaskSize, nMaskCode);
        bool fFirst = coin.vout.size() > 0 && !coin.vout[0].IsNull();
        bool fSecond = coin.vout.size() > 1 && !coin.vout[1].IsNull();
        assert(fFirst || fSecond || nMaskCode);
        unsigned int nCode = 8*(nMaskCode - (fFirst || fSecond ? 0 : 1)) + (coin.fCoinBase ? 1 : 0) + (fFirst ? 2 : 0) + (fSecond ? 4 : 0);
        // version
        ::Serialize(s, VARINT(coin.nVersion), nType, nVersion);
        // header code
        ::Serialize(s, VARINT(nCode), nType, nVersion);
        // spentness bitmask
        for (unsigned int b = 0; b<nMaskSize; b++) {
            unsigned char chAvail = 0;
            for (unsigned int i = 0; i < 8 && 2+b*8+i < coin.vout.size(); i++)
                if (!coin.vout[2+b*8+i].IsNull())
                    chAvail |= (1 << i);
            ::Serialize(s, chAvail, nType, nVersion);
        }
        // txouts themself
        for (unsigned int i = 0; i < coin.vout.size(); i++) {
            if (!coin.vout[i].IsNull())
                ::Serialize(s, CTxOutCompressor(REF(coin.vout[i])), nType, nVersion);
        }
        // coinbase height
        ::Serialize(s, VARINT(coin.nHeight), nType, nVersion);

        // originScId is not serialized in CCoins
    }

    template<typename Stream>
    static void CCoinsCoreUnserialize(CCoins & coin, Stream &s, int nType, int nVersion) {
        unsigned int nCode = 0;
        // version
        ::Unserialize(s, VARINT(coin.nVersion), nType, nVersion);
        // header code
        ::Unserialize(s, VARINT(nCode), nType, nVersion);
        coin.fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0) {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail, nType, nVersion);
            for (unsigned int p = 0; p < 8; p++) {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        coin.vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++) {
            if (vAvail[i])
                ::Unserialize(s, REF(CTxOutCompressor(coin.vout[i])), nType, nVersion);
        }
        // coinbase height
        ::Unserialize(s, VARINT(coin.nHeight), nType, nVersion);
        coin.Cleanup();

        // originScId is set to null when CCoins is unserialized
        coin.originScId.SetNull();
    }

    static unsigned int CCoinsCoreSerializedSize(const CCoins & coin, int nType, int nVersion) {
        unsigned int nSize = 0;
        unsigned int nMaskSize = 0, nMaskCode = 0;
        CalcMaskSize(coin, nMaskSize, nMaskCode);
        bool fFirst = coin.vout.size() > 0 && !coin.vout[0].IsNull();
        bool fSecond = coin.vout.size() > 1 && !coin.vout[1].IsNull();
        assert(fFirst || fSecond || nMaskCode);
        unsigned int nCode = 8*(nMaskCode - (fFirst || fSecond ? 0 : 1)) + (coin.fCoinBase ? 1 : 0) + (fFirst ? 2 : 0) + (fSecond ? 4 : 0);
        // version
        nSize += ::GetSerializeSize(VARINT(coin.nVersion), nType, nVersion);
        // size of header code
        nSize += ::GetSerializeSize(VARINT(nCode), nType, nVersion);
        // spentness bitmask
        nSize += nMaskSize;
        // txouts themself
        for (unsigned int i = 0; i < coin.vout.size(); i++)
            if (!coin.vout[i].IsNull())
                nSize += ::GetSerializeSize(CTxOutCompressor(REF(coin.vout[i])), nType, nVersion);
        // height
        nSize += ::GetSerializeSize(VARINT(coin.nHeight), nType, nVersion);

        // originScId is not serialized in CCoins, hence it is not accounted in GetSerializeSize
        return nSize;
    }

    static void CalcMaskSize(const CCoins& coin, unsigned int &nBytes, unsigned int &nNonzeroBytes) {
        /**
         * calculate number of bytes for the bitmask, and its number of non-zero bytes
         * each bit in the bitmask represents the availability of one output, but the
         * availabilities of the first two outputs are encoded separately
         */
        unsigned int nLastUsedByte = 0;
        for (unsigned int b = 0; 2+b*8 < coin.vout.size(); b++) {
            bool fZero = true;
            for (unsigned int i = 0; i < 8 && 2+b*8+i < coin.vout.size(); i++) {
                if (!coin.vout[2+b*8+i].IsNull()) {
                    fZero = false;
                    continue;
                }
            }
            if (!fZero) {
                nLastUsedByte = b + 1;
                nNonzeroBytes++;
            }
        }
        nBytes += nLastUsedByte;
    }

public:

    class CCoinsFromTx {
    public:
        CCoins& coinToStore;
        CCoinsFromTx(CCoins& coin): coinToStore(coin) {};

        template<typename Stream>
        void Serialize(Stream &s, int nType, int nVersion) const {
            CCoinsCoreSerialize(coinToStore, s, nType, nVersion);
            return;
        }

        template<typename Stream>
        void Unserialize(Stream &s, int nType, int nVersion) {
            CCoinsCoreUnserialize(coinToStore, s, nType, nVersion);
            return;
        }

        unsigned int GetSerializeSize(int nType, int nVersion) const {
            return CCoinsCoreSerializedSize(coinToStore, nType, nVersion);
        }
    };

    // coins coming from certificates needs to be serialized with their originScid. CCoinsfromCerts takes care of it
    class CCoinsFromCert {
    public:
        CCoins& coinToStore;
        CCoinsFromCert(CCoins& coin): coinToStore(coin) {};

        unsigned int GetSerializeSize(int nType, int nVersion) const {
            unsigned int nSize = 0;
            nSize += CCoinsCoreSerializedSize(coinToStore, nType, nVersion);
            nSize += ::GetSerializeSize(coinToStore.originScId, nType , nVersion);
            return nSize;
        }

        template<typename Stream>
        void Serialize(Stream &s, int nType, int nVersion) const {
            CCoinsCoreSerialize(coinToStore, s, nType, nVersion);
            ::Serialize(s, coinToStore.originScId, nType, nVersion);
            return;
        }

        template<typename Stream>
        void Unserialize(Stream &s, int nType, int nVersion) {
            CCoinsCoreUnserialize(coinToStore, s, nType, nVersion);
            ::Unserialize(s, coinToStore.originScId, nType, nVersion);
            return;
        }
    };
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CLevelDBWrapper
{
public:
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
public:
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool LoadBlockIndexGuts();
};

#endif // BITCOIN_TXDB_H
