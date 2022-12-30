// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"

#include "chainparams.h"
#include "hash.h"
#include "main.h"
#include "pow.h"
#include "uint256.h"

#include <stdint.h>

#include <boost/thread.hpp>
#include <sc/sidechaintypes.h>
#include "utilmoneystr.h"
#include "maturityheightindex.h"

using namespace std;

static const char DB_ANCHOR = 'A';
static const char DB_NULLIFIER = 's';
static const char DB_COINS = 'c';
static const char DB_SIDECHAINS = 'i';
static const char DB_CEASEDSCS = 'd';
static const char DB_BLOCK_FILES = 'f';
static const char DB_TXINDEX = 't';

static const char DB_ADDRESSINDEX = 'D';
static const char DB_ADDRESSUNSPENTINDEX = 'u';
static const char DB_TIMESTAMPINDEX = 'T';
static const char DB_BLOCKHASHINDEX = 'z';
static const char DB_SPENTINDEX = 'p';

static const char DB_BLOCK_INDEX = 'b';
static const char DB_BEST_BLOCK = 'B';
static const char DB_BEST_ANCHOR = 'a';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_FAST_REINDEX_FLAG = 'S';
static const char DB_LAST_BLOCK = 'l';
static const char DB_CSW_NULLIFIER = 'n';
static const char DB_MATURITY_HEIGHT = 'h';


void static BatchWriteAnchor(CLevelDBBatch &batch,
                             const uint256 &croot,
                             const ZCIncrementalMerkleTree &tree,
                             const bool &entered)
{
    if (!entered)
        batch.Erase(make_pair(DB_ANCHOR, croot));
    else {
        batch.Write(make_pair(DB_ANCHOR, croot), tree);
    }
}

void static BatchWriteNullifier(CLevelDBBatch &batch, const uint256 &nf, const bool &entered) {
    if (!entered)
        batch.Erase(make_pair(DB_NULLIFIER, nf));
    else
        batch.Write(make_pair(DB_NULLIFIER, nf), true);
}

void static BatchWriteCoins(CLevelDBBatch &batch, const uint256 &hash, const CCoins &coins) {
    if (coins.IsPruned())
        batch.Erase(make_pair(DB_COINS, hash));
    else
        batch.Write(make_pair(DB_COINS, hash), coins);
}

void static BatchSidechains(CLevelDBBatch &batch, const uint256 &scId, const CSidechainsCacheEntry &sidechain) {
    switch (sidechain.flag) {
        case CSidechainsCacheEntry::Flags::FRESH:
        case CSidechainsCacheEntry::Flags::DIRTY:
            batch.Write(make_pair(DB_SIDECHAINS, scId), sidechain.sidechain);
            break;
        case CSidechainsCacheEntry::Flags::ERASED:
            batch.Erase(make_pair(DB_SIDECHAINS, scId));
            break;
        case CSidechainsCacheEntry::Flags::DEFAULT:
        default:
            break;
    }
    return;
}

void static BatchCeasedScs(CLevelDBBatch &batch, int height, const CSidechainEventsCacheEntry &ceasedScs) {
    switch (ceasedScs.flag) {
        case CSidechainEventsCacheEntry::Flags::FRESH:
        case CSidechainEventsCacheEntry::Flags::DIRTY:
            batch.Write(make_pair(DB_CEASEDSCS, height), ceasedScs.scEvents);
            break;
        case CSidechainEventsCacheEntry::Flags::ERASED:
            batch.Erase(make_pair(DB_CEASEDSCS, height));
            break;
        case CSidechainEventsCacheEntry::Flags::DEFAULT:
        default:
            break;
    }
    return;
}

void static BatchWriteHashBestChain(CLevelDBBatch &batch, const uint256 &hash) {
    batch.Write(DB_BEST_BLOCK, hash);
}

void static BatchWriteHashBestAnchor(CLevelDBBatch &batch, const uint256 &hash) {
    batch.Write(DB_BEST_ANCHOR, hash);
}

void static BatchWriteCswNullifier(CLevelDBBatch &batch, const uint256 &scId, const CFieldElement &nullifier, CCswNullifiersCacheEntry state) {
    std::pair<uint256, CFieldElement> position = std::make_pair(scId, nullifier);

    switch(state.flag) {
        case CCswNullifiersCacheEntry::Flags::FRESH:
            batch.Write(make_pair(DB_CSW_NULLIFIER, position), true);
            break;
        case CCswNullifiersCacheEntry::Flags::ERASED:
            batch.Erase(make_pair(DB_CSW_NULLIFIER, position));
            break;
        case CCswNullifiersCacheEntry::Flags::DEFAULT:
        default:
            break;
    }
}

CCoinsViewDB::CCoinsViewDB(std::string dbName, size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / dbName, nCacheSize, fMemory, fWipe, false, 64) {
}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe, false, 64) {
}


bool CCoinsViewDB::GetAnchorAt(const uint256 &rt, ZCIncrementalMerkleTree &tree) const {
    if (rt == ZCIncrementalMerkleTree::empty_root()) {
        ZCIncrementalMerkleTree new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetNullifier(const uint256 &nf) const {
    bool spent = false;
    bool read = db.Read(make_pair(DB_NULLIFIER, nf), spent);

    return read;
}

bool CCoinsViewDB::GetCoins(const uint256 &txid, CCoins &coins) const {
    return db.Read(make_pair(DB_COINS, txid), coins);
}

bool CCoinsViewDB::HaveCoins(const uint256 &txid) const {
    return db.Exists(make_pair(DB_COINS, txid));
}

bool CCoinsViewDB::GetSidechain(const uint256& scId, CSidechain& info) const
{
    return db.Read(std::make_pair(DB_SIDECHAINS, scId), info);
}

bool CCoinsViewDB::HaveSidechain(const uint256& scId) const
{
    return db.Exists(std::make_pair(DB_SIDECHAINS, scId));
}

bool CCoinsViewDB::HaveSidechainEvents(int height) const
{
    return db.Exists(std::make_pair(DB_CEASEDSCS, height));
}

bool CCoinsViewDB::GetSidechainEvents(int height, CSidechainEvents& ceasingScs) const
{
    return db.Read(std::make_pair(DB_CEASEDSCS, height), ceasingScs);
}

void CCoinsViewDB::GetScIds(std::set<uint256>& scIdsList) const
{
    std::unique_ptr<leveldb::Iterator> it(const_cast<CLevelDBWrapper*>(&db)->NewIterator());
    static const std::string scIdsPrefix = std::string(1,DB_SIDECHAINS);

    for(it->Seek(scIdsPrefix); it->Valid() && it->key().starts_with(scIdsPrefix); it->Next())
    {
        boost::this_thread::interruption_point();

        leveldb::Slice slKey = it->key();
        // serialize key, skipping prefix
        CDataStream ssKey(slKey.data() + sizeof(char), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
        uint256 keyScId;
        ssKey >> keyScId;
        scIdsList.insert(keyScId);
    }

    return;
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

uint256 CCoinsViewDB::GetBestAnchor() const {
    uint256 hashBestAnchor;
    if (!db.Read(DB_BEST_ANCHOR, hashBestAnchor))
        return ZCIncrementalMerkleTree::empty_root();
    return hashBestAnchor;
}

bool CCoinsViewDB::HaveCswNullifier(const uint256& scId, const CFieldElement &nullifier) const {
    std::pair<uint256, CFieldElement> position = std::make_pair(scId, nullifier);
    return db.Exists(make_pair(DB_CSW_NULLIFIER, position));
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins,
                              const uint256 &hashBlock,
                              const uint256 &hashAnchor,
                              CAnchorsMap &mapAnchors,
                              CNullifiersMap &mapNullifiers,
                              CSidechainsMap& mapSidechains,
                              CSidechainEventsMap& mapSidechainEvents,
                              CCswNullifiersMap& cswNullifies) {
    CLevelDBBatch batch;
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            BatchWriteCoins(batch, it->first, it->second.coins);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }

    for (CAnchorsMap::iterator it = mapAnchors.begin(); it != mapAnchors.end();) {
        if (it->second.flags & CAnchorsCacheEntry::DIRTY) {
            BatchWriteAnchor(batch, it->first, it->second.tree, it->second.entered);
            // TODO: changed++?
        }
        CAnchorsMap::iterator itOld = it++;
        mapAnchors.erase(itOld);
    }

    for (CNullifiersMap::iterator it = mapNullifiers.begin(); it != mapNullifiers.end();) {
        if (it->second.flags & CNullifiersCacheEntry::DIRTY) {
            BatchWriteNullifier(batch, it->first, it->second.entered);
            // TODO: changed++?
        }
        CNullifiersMap::iterator itOld = it++;
        mapNullifiers.erase(itOld);
    }

    for (CSidechainsMap::iterator it = mapSidechains.begin(); it != mapSidechains.end();) {
        BatchSidechains(batch, it->first, it->second);
        CSidechainsMap::iterator itOld = it++;
        mapSidechains.erase(itOld);
    }

    for (CSidechainEventsMap::iterator it = mapSidechainEvents.begin(); it != mapSidechainEvents.end();) {
        BatchCeasedScs(batch, it->first, it->second);
        CSidechainEventsMap::iterator itOld = it++;
        mapSidechainEvents.erase(itOld);
    }
    
    for (CCswNullifiersMap::iterator it = cswNullifies.begin(); it != cswNullifies.end();) {
        const std::pair<uint256, CFieldElement>& position = it->first;
        BatchWriteCswNullifier(batch, position.first, position.second, it->second);
        CCswNullifiersMap::iterator itOld = it++;
        cswNullifies.erase(itOld);
    }

    if (!hashBlock.IsNull())
        BatchWriteHashBestChain(batch, hashBlock);
    if (!hashAnchor.IsNull())
        BatchWriteHashBestAnchor(batch, hashAnchor);

    LogPrint("coindb", "Committing %u changed transactions (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return db.WriteBatch(batch);
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe, bool compression, int maxOpenFiles) : CLevelDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe, compression, maxOpenFiles) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::WriteFastReindexing(bool fReindexFast) {
    if (fReindexFast)
        return Write(DB_FAST_REINDEX_FLAG, '1');
    else
        return Erase(DB_FAST_REINDEX_FLAG);
}
bool CBlockTreeDB::ReadFastReindexing(bool &fReindexFast) {
	fReindexFast = Exists(DB_FAST_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) {
    return Read(DB_LAST_BLOCK, nFile);
}

bool CCoinsViewDB::GetStats(CCoinsStats &stats) const {
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<leveldb::Iterator> pcursor(const_cast<CLevelDBWrapper*>(&db)->NewIterator());
    pcursor->SeekToFirst();

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = GetBestBlock();
    ss << stats.hashBlock;
    CAmount nTotalAmount = 0;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if (chType == DB_COINS) {
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                CCoins coins;
                ssValue >> coins;
                uint256 txhash;
                ssKey >> txhash;
                ss << txhash;
                ss << VARINT(coins.nVersion);
                ss << (coins.fCoinBase ? 'c' : 'n');
                ss << VARINT(coins.nHeight);

                // add cert attribute to the hash writer obj, such values are meaningful only in this case 
                // the size of the hash writer obj buffer is different anyway (larger) from the actual serialized size
                // because the coin serialization is compressed 
                if (coins.IsFromCert()) {
                    ss << coins.nFirstBwtPos;
                    ss << coins.nBwtMaturityHeight;
                }

                // - transactions and certificates are lumped together 
                // - nTotalAmount includes certificate valid bwt amounts (not-null, as for low-quality certs)
                //   even if not yet matured, as it is done currently with coinbase vouts
                stats.nTransactions++;
                for (unsigned int i=0; i<coins.vout.size(); i++) {
                    const CTxOut &out = coins.vout[i];
                    if (!out.IsNull()) {
                        stats.nTransactionOutputs++;
                        ss << VARINT(i+1);
                        ss << out;
                        nTotalAmount += out.nValue;
                    }
                }
                
                stats.nSerializedSize += 32 + slValue.size();
                ss << VARINT(0);
            }
            pcursor->Next();
        } catch (const std::exception& e) {
            return error("%s: Deserialize or I/O error - %s", __func__, e.what());
        }
    }
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;
    return true;
}

void CCoinsViewDB::Dump_info()  const
{
    // dump leveldb contents on stdout
    std::unique_ptr<leveldb::Iterator> it(const_cast<CLevelDBWrapper*>(&db)->NewIterator());
    for (it->SeekToFirst(); it->Valid(); it->Next())
    {
        leveldb::Slice slKey = it->key();
        CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
        char chType;
        uint256 keyScId;
        ssKey >> chType;
        ssKey >> keyScId;

        if (chType == DB_SIDECHAINS)
        {
            leveldb::Slice slValue = it->value();
            CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
            CSidechain info;
            ssValue >> info;

            std::cout
                << "scId[" << keyScId.ToString() << "]" << std::endl
                << "  ==> balance: " << FormatMoney(info.balance) << std::endl
                << "  creating block height: " << info.creationBlockHeight  << std::endl
                << "  creating tx hash: " << info.creationTxHash.ToString() << std::endl
                // creation parameters
                << "  withdrawalEpochLength: " << info.fixedParams.withdrawalEpochLength << std::endl;
        }
        else
        {
            std::cout << "unknown type " << chType << std::endl;
        }
    }
}

bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<const CBlockIndex*>& blockinfo) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<int, const CBlockFileInfo*> >::const_iterator it=fileInfo.begin(); it != fileInfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_FILES, it->first), *it->second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Write(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()), CDiskBlockIndex(*it));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CTxIndexValue &val) {
    return Read(make_pair(DB_TXINDEX, txid), val);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CTxIndexValue> >&vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<uint256,CTxIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadMaturityHeightIndex(const int height, std::vector<CMaturityHeightKey> &val) {
    boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(DB_MATURITY_HEIGHT, CMaturityHeightIteratorKey(height));
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            
            char chType;
            ssKey >> chType;
            if (chType == DB_MATURITY_HEIGHT)
            {
                CMaturityHeightKey indexKey;
                ssKey >> indexKey;

                if (indexKey.blockHeight == height) {
                    val.push_back(indexKey);
                    pcursor->Next();
                }
                else
                {
                    break;
                }
            } else {
                break;
            }
        } catch (const std::exception& e) {
            return error(e.what());
        }
    }
    return true;
}

bool CBlockTreeDB::UpdateMaturityHeightIndex(const std::vector<std::pair<CMaturityHeightKey,CMaturityHeightValue>> &vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<CMaturityHeightKey,CMaturityHeightValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        //If the value is null we mean we want to erase the pair from the DB otherwise we persist it
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_MATURITY_HEIGHT, it->first));
        } else {
            batch.Write(make_pair(DB_MATURITY_HEIGHT, it->first), it->second);
        }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value) {
    return Read(make_pair(DB_SPENTINDEX, key), value);
}

bool CBlockTreeDB::UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<CSpentIndexKey,CSpentIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_SPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_SPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_ADDRESSUNSPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_ADDRESSUNSPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressUnspentIndex(uint160 addressHash, AddressType type,
                                           std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs) {

    boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(type, addressHash));
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            CAddressUnspentKey indexKey;
            ssKey >> chType;
            ssKey >> indexKey;
            if (chType == DB_ADDRESSUNSPENTINDEX && indexKey.hashBytes == addressHash) {
                try {
                    leveldb::Slice slValue = pcursor->value();
                    CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                    CAddressUnspentValue nValue;
                    ssValue >> nValue;
                    unspentOutputs.push_back(make_pair(indexKey, nValue));
                    pcursor->Next();
                } catch (const std::exception& e) {
                    return error("failed to get address unspent value");
                }
            } else {
                break;
            }
        } catch (const std::exception& e) {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::UpdateAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &vect)
{
    CLevelDBBatch batch;

    for (std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
    {
        if (it->second.IsNull())
        {
            batch.Erase(make_pair(DB_ADDRESSINDEX, it->first));
        }
        else
        {
            batch.Write(make_pair(DB_ADDRESSINDEX, it->first), it->second);
        }
    }

    return WriteBatch(batch);
}

bool CBlockTreeDB::WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> >&vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_ADDRESSINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> >&vect) {
    CLevelDBBatch batch;
    for (std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_ADDRESSINDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressIndex(uint160 addressHash, AddressType type,
                                    std::vector<std::pair<CAddressIndexKey, CAddressIndexValue> > &addressIndex,
                                    int start, int end) {

    boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    if (start > 0 && end > 0) {
        ssKeySet << make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorHeightKey(type, addressHash, start));
    } else {
        ssKeySet << make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorKey(type, addressHash));
    }
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            CAddressIndexKey indexKey;
            ssKey >> chType;
            ssKey >> indexKey;
            if (chType == DB_ADDRESSINDEX && indexKey.hashBytes == addressHash) {
                if (end > 0 && indexKey.blockHeight > end) {
                    break;
                }
                try {
                    leveldb::Slice slValue = pcursor->value();
                    CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                    CAddressIndexValue indexValue;
                    ssValue >> indexValue;

                    addressIndex.push_back(make_pair(indexKey, indexValue));
                    pcursor->Next();
                } catch (const std::exception& e) {
                    return error("failed to get address index value");
                }
            } else {
                break;
            }
        } catch (const std::exception& e) {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteTimestampIndex(const CTimestampIndexKey &timestampIndex) {
    CLevelDBBatch batch;
    batch.Write(make_pair(DB_TIMESTAMPINDEX, timestampIndex), 0);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes) {

    boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(DB_TIMESTAMPINDEX, CTimestampIndexIteratorKey(low));
    pcursor->Seek(ssKeySet.str());

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            CTimestampIndexKey indexKey;
            ssKey >> chType;
            ssKey >> indexKey;
            if (chType == DB_TIMESTAMPINDEX && indexKey.timestamp < high) {
                if (fActiveOnly) {
                    if (blockOnchainActive(indexKey.blockHash)) {
                        hashes.push_back(std::make_pair(indexKey.blockHash, indexKey.timestamp));
                    }
                } else {
                    hashes.push_back(std::make_pair(indexKey.blockHash, indexKey.timestamp));
                }

                pcursor->Next();
            } else {
                break;
            }
        } catch (const std::exception& e) {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex, const CTimestampBlockIndexValue &logicalts) {
    CLevelDBBatch batch;
    batch.Write(make_pair(DB_BLOCKHASHINDEX, blockhashIndex), logicalts);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampBlockIndex(const uint256 &hash, unsigned int &ltimestamp) {

    CTimestampBlockIndexValue(lts);
    if (!Read(std::make_pair(DB_BLOCKHASHINDEX, hash), lts))
	return false;

    ltimestamp = lts.ltimestamp;
    return true;
}

bool CBlockTreeDB::blockOnchainActive(const uint256 &hash) {
    BlockMap::iterator mi = mapBlockIndex.find(hash);

    if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second))
    {
        return true;
    }

    return false;
}

bool CBlockTreeDB::WriteString(const std::string &name, std::string sValue) {
    return Write(std::make_pair(DB_FLAG, name), sValue);
}

bool CBlockTreeDB::ReadString(const std::string &name, std::string &sValue) {
    if (!Read(std::make_pair(DB_FLAG, name), sValue))
        return false;
    return true;
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts()
{
    boost::scoped_ptr<leveldb::Iterator> pcursor(NewIterator());

    CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
    ssKeySet << make_pair(DB_BLOCK_INDEX, uint256());
    pcursor->Seek(ssKeySet.str());

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            leveldb::Slice slKey = pcursor->key();
            CDataStream ssKey(slKey.data(), slKey.data()+slKey.size(), SER_DISK, CLIENT_VERSION);
            char chType;
            ssKey >> chType;
            if (chType == DB_BLOCK_INDEX) {
                leveldb::Slice slValue = pcursor->value();
                CDataStream ssValue(slValue.data(), slValue.data()+slValue.size(), SER_DISK, CLIENT_VERSION);
                CDiskBlockIndex diskindex;
                ssValue >> diskindex;

                // Construct block index object
                CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev          = InsertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight        = diskindex.nHeight;
                pindexNew->nFile          = diskindex.nFile;
                pindexNew->nDataPos       = diskindex.nDataPos;
                pindexNew->nUndoPos       = diskindex.nUndoPos;
                pindexNew->hashAnchor     = diskindex.hashAnchor;
                pindexNew->nVersion       = diskindex.nVersion;
                pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
                pindexNew->nTime          = diskindex.nTime;
                pindexNew->nBits          = diskindex.nBits;
                pindexNew->nNonce         = diskindex.nNonce;
                pindexNew->nSolution      = diskindex.nSolution;
                pindexNew->nStatus        = diskindex.nStatus;
                pindexNew->nTx            = diskindex.nTx;
                pindexNew->nSproutValue   = diskindex.nSproutValue;
                pindexNew->hashScTxsCommitment = diskindex.hashScTxsCommitment;
                pindexNew->scCumTreeHash  = diskindex.scCumTreeHash;

                if (!CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits, Params().GetConsensus()))
                    return error("LoadBlockIndex(): CheckProofOfWork failed: %s", pindexNew->ToString());

                pcursor->Next();
            } else {
                break; // if shutdown requested or finished loading block index
            }
        } catch (const std::exception& e) {
            return error("%s: Deserialize or I/O error - %s", __func__, e.what());
        }
    }

    return true;
}

