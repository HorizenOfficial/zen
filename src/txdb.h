// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "addressindex.h"
#include "chain.h"
#include "coins.h"
#include "leveldbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CBlockFileInfo;
class CBlockIndex;
struct CTxIndexValue;
struct CMaturityHeightKey;
struct CMaturityHeightIteratorKey;
struct CMaturityHeightValue;

struct CAddressUnspentKey;
struct CAddressUnspentValue;
struct CAddressIndexKey;
struct CAddressIndexValue;
struct CAddressIndexIteratorKey;
struct CAddressIndexIteratorHeightKey;
struct CTimestampIndexKey;
struct CTimestampIndexIteratorKey;
struct CTimestampBlockIndexKey;
struct CTimestampBlockIndexValue;
struct CSpentIndexKey;
struct CSpentIndexValue;

class uint256;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;

static const std::string DEFAULT_INDEX_VERSION_STR = "0.0";
static const std::string CURRENT_INDEX_VERSION_STR = "1.0";

struct CDiskTxPos : public CDiskBlockPos
{
    unsigned int nTxOffset; // after header

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CDiskBlockPos*)this);
        READWRITE(VARINT(nTxOffset));
    }

    CDiskTxPos(const CDiskBlockPos &blockIn, unsigned int nTxOffsetIn) : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {
    }

    CDiskTxPos() {
        SetNull();
    }

    void SetNull() {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }
};

struct CTxIndexValue {

    static const int INVALID_MATURITY_HEIGHT = -1;

    CDiskTxPos txPosition;
    int txIndex;
    int maturityHeight;     // It can contain negative numbers

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(txPosition);

        if (ser_action.ForRead() && (s.size() == 0))
        {
            // can happen when we are reading old records in txindex db
            // we have completed the stream buffer, set remaining values to default
            txIndex = 0;
            maturityHeight = 0;
        }
        else
        {
            READWRITE(VARINT(txIndex));
            // Since the maturity can be negative, we have to manipulate it to store the sign bit in a VARINT
            READWRITE_VARINT_WITH_SIGN(maturityHeight);
        }
    }

    CTxIndexValue(const CDiskTxPos& txPos, int txIdx, int maturity) {
        txPosition = txPos;
        txIndex = txIdx;
        maturityHeight = maturity;
    }

    CTxIndexValue() {
        SetNull();
    }

    void SetNull() {
        txPosition = CDiskTxPos();
        txIndex = 0;
        maturityHeight = INVALID_MATURITY_HEIGHT;
    }
};

/** CCoinsView backed by the LevelDB coin database (chainstate/) */
class CCoinsViewDB : public CCoinsView
{
protected:
    CLevelDBWrapper db;
    CCoinsViewDB(std::string dbName, size_t nCacheSize, int maxOpenFiles, bool fMemory = false, bool fWipe = false);
public:
    CCoinsViewDB(size_t nCacheSize, int maxOpenFiles, bool fMemory = false, bool fWipe = false);

    bool GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree)   const override;
    bool GetNullifier(const uint256 &nf)                                 const override;
    bool GetCoins(const uint256 &txid, CCoins &coins)                    const override;
    bool HaveCoins(const uint256 &txid)                                  const override;
    bool GetSidechain(const uint256& scId, CSidechain& info)             const override;
    bool HaveSidechain(const uint256& scId)                              const override;
    bool HaveSidechainEvents(int height)                                 const override;
    bool GetSidechainEvents(int height, CSidechainEvents& ceasingScs)    const override;
    void GetScIds(std::set<uint256>& scIdsList)                          const override;
    uint256 GetBestBlock()                                               const override;
    uint256 GetBestAnchor()                                              const override;
    bool HaveCswNullifier(const uint256& scId,
                          const CFieldElement& nullifier)  const override;

    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashAnchor,
                    CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers,
                    CSidechainsMap& mapSidechains,
                    CSidechainEventsMap& mapSidechainEvents,
                    CCswNullifiersMap& cswNullifies)                           override;
    bool GetStats(CCoinsStats &stats)                                    const override;
    void Dump_info() const;
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CLevelDBWrapper
{
public:
    CBlockTreeDB(size_t nCacheSize, int maxOpenFiles, bool fMemory = false, bool fWipe = false);
private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
public:
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo);
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindex);
    bool ReadReindexing(bool &fReindex);
    bool WriteFastReindexing(bool fReindexFast);
    bool ReadFastReindexing(bool &fReindexFast);
    bool ReadTxIndex(const uint256 &txid, CTxIndexValue &val);
    bool WriteTxIndex(const std::vector<std::pair<uint256, CTxIndexValue> > &list);
    bool ReadMaturityHeightIndex(int height, std::vector<CMaturityHeightKey> &val);
    bool UpdateMaturityHeightIndex(const std::vector<std::pair<CMaturityHeightKey, CMaturityHeightValue>> &maturityHeightList);

    bool ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);
    bool UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect);
    bool UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect);
    bool ReadAddressUnspentIndex(uint160 addressHash, AddressType type,
                                 std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &vect);
    bool WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &vect);
    bool EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &vect);
    bool UpdateAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &vect);
    bool ReadAddressIndex(uint160 addressHash, AddressType type,
                          std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &addressIndex,
                          int start = 0, int end = 0);
    bool WriteTimestampIndex(const CTimestampIndexKey &timestampIndex);
    bool ReadTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &vect);
    bool WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex, const CTimestampBlockIndexValue &logicalts);
    bool ReadTimestampBlockIndex(const uint256 &hash, unsigned int &logicalTS);
    bool blockOnchainActive(const uint256 &hash);

    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);
    bool WriteString(const std::string &name, std::string fValue);
    bool ReadString(const std::string &name, std::string &fValue);
    bool LoadBlockIndexGuts();
};

#endif // BITCOIN_TXDB_H
