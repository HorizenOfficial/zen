#include <gtest/gtest.h>
#include "tx_creation_utils.h"
#include <coins.h>
#include <chainparams.h>
#include <undo.h>
#include <pubkey.h>
#include <main.h>
#include <txdb.h>
#include <main.h>
#include <consensus/validation.h>

#include <miner.h>
#include <gtest/libzendoo_test_files.h>

class CInMemorySidechainDb final: public CCoinsView {
public:
    CInMemorySidechainDb()  = default;
    virtual ~CInMemorySidechainDb() = default;

    bool HaveSidechain(const uint256& scId) const override { return inMemoryMap.count(scId); }
    bool GetSidechain(const uint256& scId, CSidechain& info) const override {
        if(!inMemoryMap.count(scId))
            return false;
        info = inMemoryMap[scId];
        return true;
    }

    virtual void GetScIds(std::set<uint256>& scIdsList) const override {
        for (auto& entry : inMemoryMap)
            scIdsList.insert(entry.first);
        return;
    }

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap, CSidechainEventsMap& mapSidechainEvents) override
    {
        for (auto& entry : sidechainMap)
            switch (entry.second.flag) {
                case CSidechainsCacheEntry::Flags::FRESH:
                case CSidechainsCacheEntry::Flags::DIRTY:
                    inMemoryMap[entry.first] = entry.second.scInfo;
                    break;
                case CSidechainsCacheEntry::Flags::ERASED:
                    inMemoryMap.erase(entry.first);
                    break;
                case CSidechainsCacheEntry::Flags::DEFAULT:
                    break;
                default:
                    return false;
            }
        sidechainMap.clear();
        return true;
    }

private:
    mutable boost::unordered_map<uint256, CSidechain, ObjectHasher> inMemoryMap;
};

class SidechainConnectCertsBlockTestSuite : public ::testing::Test {
public:
    SidechainConnectCertsBlockTestSuite():
        fakeChainStateDb(nullptr), sidechainsView(nullptr),
        dummyBlock(), dummyUndo(), dummyVoidedCertMap(), dummyScriptPubKey(),
        dummyCoins(), dummyHash(), dummyAnchor(), dummyAnchors(), dummyNullifiers(),
        dummySidechains(), dummyScEvents(), dummyState(), dummyChain(),
        dummyFeeAmount(), dummyCoinbaseScript()
    {
        dummyScriptPubKey = GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/false);
    }

    ~SidechainConnectCertsBlockTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        fakeChainStateDb   = new CInMemorySidechainDb();
        sidechainsView     = new CCoinsViewCache(fakeChainStateDb);

        // Below stuff needed for Connect Block tests
        fCoinbaseEnforcedProtectionEnabled = false; //simplify input checks for ConnectBlock tests

        dummyHash = dummyBlock.GetHash();

        dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
        CAnchorsCacheEntry dummyAnchorsEntry;
        dummyAnchorsEntry.entered = true;
        dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;
        dummyAnchors[dummyAnchor] = dummyAnchorsEntry;

        dummyCoinbaseScript = CScript() << OP_DUP << OP_HASH160
                << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    };

    void TearDown() override {
        delete sidechainsView;
        sidechainsView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;
    };
protected:
    CInMemorySidechainDb *fakeChainStateDb;
    CCoinsViewCache      *sidechainsView;

    //helpers
    CBlock                  dummyBlock;
    CTxUndo                 dummyUndo;
    std::map<uint256, bool> dummyVoidedCertMap;
    CScript                 dummyScriptPubKey;

    CCoinsMap           dummyCoins;
    uint256             dummyHash;
    uint256             dummyAnchor;
    CAnchorsMap         dummyAnchors;
    CNullifiersMap      dummyNullifiers;
    CSidechainsMap      dummySidechains;
    CSidechainEventsMap dummyScEvents;

    CValidationState    dummyState;
    CChain              dummyChain;

    uint256 storeSidechain(const uint256& scId, const CSidechain& sidechain);
    CBlock fillBlockHeader(const uint256& prevBlockHash);

    CAmount dummyFeeAmount;
    CScript dummyCoinbaseScript;
    uint256 CreateSpendableTxAtHeight(unsigned int coinHeight);
    void    CreateCheckpointAfter(CBlockIndex* blkIdx);
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// ConnectBlock ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainConnectCertsBlockTestSuite, SingleCertInBlock)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputTxHash = CreateSpendableTxAtHeight(certBlockHeight-COINBASE_MATURITY);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 100;
    initialScState.creationData.withdrawalEpochLength = 20;
    initialScState.topCommittedCertHash = uint256S("cccc");
    initialScState.topCommittedCertQuality = 100;
    initialScState.topCommittedCertReferencedEpoch = initialScState.EpochFor(certBlockHeight)-1;
    initialScState.topCommittedCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    storeSidechain(scId, initialScState);

    // CREATE BLOCK WITH CERTIFICATE
    CMutableScCertificate singleCert;
    singleCert.vin.push_back(CTxIn(inputTxHash, 0, CScript(), 0));
    singleCert.nVersion    = SC_CERT_VERSION;
    singleCert.scProof     = libzendoomc::ScProof(ParseHex(SAMPLE_PROOF));
    singleCert.scId        = scId;
    singleCert.epochNumber = initialScState.topCommittedCertReferencedEpoch;
    singleCert.quality     = 110;
    singleCert.endEpochBlockHash = *(chainActive.Tip()->pprev->phashBlock);
    singleCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));

    CBlock certBlock = fillBlockHeader(uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(singleCert);

    // CREATE BLOCK INDEX FOR CERTIFICATE BLOCK
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    //ADD CHECKPOINT TO SKIP EXPENSIVE CHECKS
    CreateCheckpointAfter(certBlockIndex);

    bool fJustCheck = true;
    bool fCheckScTxesCommitment = false;

    // TEST
    EXPECT_TRUE(ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain, fJustCheck, fCheckScTxesCommitment, &dummyVoidedCertMap));
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
uint256 SidechainConnectCertsBlockTestSuite::storeSidechain(const uint256& scId, const CSidechain& sidechain)
{
    CSidechainsMap mapSidechain;
    mapSidechain[scId] = CSidechainsCacheEntry(sidechain,CSidechainsCacheEntry::Flags::FRESH);

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyAnchor, dummyAnchors,
                               dummyNullifiers, mapSidechain, dummyScEvents);

    return scId;
}

CBlock SidechainConnectCertsBlockTestSuite::fillBlockHeader(const uint256& prevBlockHash)
{
    CBlock res;
    res.nVersion = MIN_BLOCK_VERSION;
    res.hashPrevBlock = prevBlockHash;
    res.hashMerkleRoot = uint256();
    res.hashScTxsCommitment.SetNull();

    static unsigned int runCounter = 0;
    SetMockTime(time(nullptr) + ++runCounter);
    CBlockIndex fakePrevBlockIdx(Params().GenesisBlock());
    UpdateTime(&res, Params().GetConsensus(), &fakePrevBlockIdx);

    res.nBits = UintToArith256(Params().GetConsensus().powLimit).GetCompact();
    res.nNonce = Params().GenesisBlock().nNonce;
    return res;
}

uint256 SidechainConnectCertsBlockTestSuite::CreateSpendableTxAtHeight(unsigned int coinHeight)
{
    CTransaction inputTx = createCoinbase(dummyCoinbaseScript, dummyFeeAmount, coinHeight);
    UpdateCoins(inputTx, *sidechainsView, dummyUndo, coinHeight);
    if (!sidechainsView->HaveCoins(inputTx.GetHash()))
        return uint256();
    else
        return inputTx.GetHash();
}

void SidechainConnectCertsBlockTestSuite::CreateCheckpointAfter(CBlockIndex* blkIdx)
{
    assert(blkIdx != nullptr);

    CBlock dummyCheckpointBlock;
    CBlockIndex* dummyCheckPoint = AddToBlockIndex(dummyCheckpointBlock);
    dummyCheckPoint->nHeight = blkIdx->nHeight + 1;
    dummyCheckPoint->pprev = blkIdx;
    Checkpoints::CCheckpointData& checkpoints = const_cast<Checkpoints::CCheckpointData&>(Params().Checkpoints());
    checkpoints.mapCheckpoints[dummyCheckPoint->nHeight] = dummyCheckpointBlock.GetHash();
}
