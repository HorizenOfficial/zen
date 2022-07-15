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

class SidechainsConnectCertsBlockTestSuite : public ::testing::Test {
public:
    SidechainsConnectCertsBlockTestSuite():
        fakeChainStateDb(nullptr), sidechainsView(nullptr),
        dummyBlock(), dummyHash(), dummyCertStatusUpdateInfo(), dummyScriptPubKey(),
        dummyState(), dummyChain(), dummyScEvents(), dummyFeeAmount(), dummyCoinbaseScript(),
        csMainLock(cs_main, "cs_main", __FILE__, __LINE__)
    {
        dummyScriptPubKey = GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/false);
    }

    ~SidechainsConnectCertsBlockTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        // clear globals
        UnloadBlockIndex();
        mGlobalForkTips.clear();

        fakeChainStateDb   = new blockchain_test_utils::CInMemorySidechainDb();
        sidechainsView     = new txCreationUtils::CNakedCCoinsViewCache(fakeChainStateDb);

        dummyHash = dummyBlock.GetHash();
        dummyCoinbaseScript = CScript() << OP_DUP << OP_HASH160
                << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;
    };

    void TearDown() override {
        delete sidechainsView;
        sidechainsView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;

        // clear globals
        UnloadBlockIndex();
        mGlobalForkTips.clear();
    };

protected:
    blockchain_test_utils::CInMemorySidechainDb  *fakeChainStateDb;
    txCreationUtils::CNakedCCoinsViewCache *sidechainsView;

    //helpers
    CBlock                                    dummyBlock;
    uint256                                   dummyHash;
    std::vector<CScCertificateStatusUpdateInfo>  dummyCertStatusUpdateInfo;
    CScript                                   dummyScriptPubKey;

    CValidationState    dummyState;
    CChain              dummyChain;

    CSidechainEventsMap dummyScEvents;

    void storeSidechain(const uint256& scId, const CSidechain& sidechain);
    void storeSidechainEvent(int eventHeight, const CSidechainEvents& scEvent);

    void fillBlockHeader(CBlock& blockToFill, const uint256& prevBlockHash, int bl_ver = MIN_BLOCK_VERSION);

    CAmount dummyFeeAmount;
    CScript dummyCoinbaseScript;
    void    CreateCheckpointAfter(CBlockIndex* blkIdx);

private:
    //Critical sections below needed when compiled with --enable-debug, which activates ASSERT_HELD
    CCriticalBlock csMainLock;
};

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////// ConnectBlock ////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_SingleCert_SameEpoch_CertCoinHasBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 300;
    initialScState.fixedParams.withdrawalEpochLength = 20;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;

    // create coinbase to finance certificate submission (just in view)
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificate ...
    CMutableScCertificate singleCert;
    singleCert.vin.push_back(CTxIn(inputTxHash, 0, CScript(), 0));
    singleCert.nVersion    = SC_CERT_VERSION;
    singleCert.scProof     = CScProof{SAMPLE_CERT_DARLIN_PROOF};
    singleCert.scId        = scId;
    singleCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    singleCert.quality     = initialScState.lastTopQualityCertQuality * 2;
    singleCert.endEpochCumScTxCommTreeRoot = chainActive.Tip()->pprev->scCumTreeHash;
    singleCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    singleCert.forwardTransferScFee = 0;
    singleCert.mainchainBackwardTransferRequestScFee = 0;

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(singleCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    // test
    pcoinsTip = sidechainsView;
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain,
                            flagBlockProcessingType::CHECK_ONLY, flagScRelatedChecks::OFF,
                            flagScProofVerification::ON, flagLevelDBIndexesWrite::OFF,
                            &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    ASSERT_TRUE(sidechainsView->HaveCoins(singleCert.GetHash()));
    CCoins certCoin;
    sidechainsView->GetCoins(singleCert.GetHash(), certCoin);
    EXPECT_TRUE(certCoin.IsFromCert());
    EXPECT_TRUE(certCoin.vout.size() == 1);
    EXPECT_TRUE(certCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(certCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_SingleCert_DifferentEpoch_CertCoinHasBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 300;
    initialScState.fixedParams.withdrawalEpochLength = 20;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch+1;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;

    // create coinbase to finance certificate submission (just in view)
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));
    uint256 inputTxHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificate ...
    CMutableScCertificate singleCert;
    singleCert.vin.push_back(CTxIn(inputTxHash, 0, CScript(), 0));
    singleCert.nVersion    = SC_CERT_VERSION;
    singleCert.scProof     = CScProof{SAMPLE_CERT_DARLIN_PROOF};
    singleCert.scId        = scId;
    singleCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch + 1;
    singleCert.quality     = 1;
    singleCert.endEpochCumScTxCommTreeRoot = chainActive.Tip()->pprev->scCumTreeHash;
    singleCert.addBwt(CTxOut(CAmount(90), dummyScriptPubKey));
    singleCert.forwardTransferScFee = 0;
    singleCert.mainchainBackwardTransferRequestScFee = 0;

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(singleCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain,
                            flagBlockProcessingType::CHECK_ONLY, flagScRelatedChecks::OFF,
                            flagScProofVerification::ON, flagLevelDBIndexesWrite::OFF,
                            &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    ASSERT_TRUE(sidechainsView->HaveCoins(singleCert.GetHash()));
    CCoins certCoin;
    sidechainsView->GetCoins(singleCert.GetHash(), certCoin);
    EXPECT_TRUE(certCoin.IsFromCert());
    EXPECT_TRUE(certCoin.vout.size() == 1);
    EXPECT_TRUE(certCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(certCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_MultipleCerts_SameEpoch_LowQualityCertCoinHasNotBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 300;
    initialScState.fixedParams.withdrawalEpochLength = 20;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));

    // create coinbase to finance certificate submission (just in view)
    uint256 inputLowQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputHighQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificates ...
    CMutableScCertificate lowQualityCert;
    lowQualityCert.vin.push_back(CTxIn(inputLowQCertHash, 0, CScript(), 0));
    lowQualityCert.nVersion    = SC_CERT_VERSION;
    lowQualityCert.scProof     = CScProof{SAMPLE_CERT_DARLIN_PROOF};
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch;
    lowQualityCert.quality     = initialScState.lastTopQualityCertQuality * 2;
    lowQualityCert.endEpochCumScTxCommTreeRoot = chainActive.Tip()->pprev->scCumTreeHash;
    lowQualityCert.addBwt(CTxOut(CAmount(40), dummyScriptPubKey));
    lowQualityCert.forwardTransferScFee = 0;
    lowQualityCert.mainchainBackwardTransferRequestScFee = 0;

    CMutableScCertificate highQualityCert;
    highQualityCert.vin.push_back(CTxIn(inputHighQCertHash, 0, CScript(), 0));
    highQualityCert.nVersion    = lowQualityCert.nVersion;
    highQualityCert.scProof     = lowQualityCert.scProof;
    highQualityCert.scId        = lowQualityCert.scId;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;
    highQualityCert.quality     = lowQualityCert.quality * 2;
    highQualityCert.endEpochCumScTxCommTreeRoot = chainActive.Tip()->pprev->scCumTreeHash;
    highQualityCert.addBwt(CTxOut(CAmount(50), dummyScriptPubKey));
    highQualityCert.forwardTransferScFee = 0;
    highQualityCert.mainchainBackwardTransferRequestScFee = 0;

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(lowQualityCert);
    certBlock.vcert.push_back(highQualityCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain,
                            flagBlockProcessingType::CHECK_ONLY, flagScRelatedChecks::OFF,
                            flagScProofVerification::ON, flagLevelDBIndexesWrite::OFF,
                            &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    CCoins lowQualityCertCoin;
    EXPECT_FALSE(sidechainsView->GetCoins(lowQualityCert.GetHash(), lowQualityCertCoin));

    CCoins highQualityCertCoin;
    sidechainsView->GetCoins(highQualityCert.GetHash(), highQualityCertCoin);
    EXPECT_TRUE(highQualityCertCoin.IsFromCert());
    EXPECT_TRUE(highQualityCertCoin.vout.size() == 1);
    EXPECT_TRUE(highQualityCertCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(highQualityCertCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_MultipleCerts_DifferentEpoch_LowQualityCertCoinHasNotBwt)
{
    // setup sidechain initial state...
    CSidechain initialScState;
    uint256 scId = uint256S("aaaa");
    initialScState.creationBlockHeight = 300;
    initialScState.fixedParams.withdrawalEpochLength = 20;
    initialScState.fixedParams.version = 0;
    initialScState.lastTopQualityCertHash = uint256S("cccc");
    initialScState.lastTopQualityCertQuality = 100;
    initialScState.lastTopQualityCertReferencedEpoch = 7;
    initialScState.lastTopQualityCertBwtAmount = 50;
    initialScState.balance = CAmount(100);
    initialScState.InitScFees();
    storeSidechain(scId, initialScState);

    //... and initial ceasing event too
    CSidechainEvents event;
    event.ceasingScs.insert(scId);
    storeSidechainEvent(initialScState.GetScheduledCeasingHeight(), event);

    // set relevant heights
    int certEpoch = initialScState.lastTopQualityCertReferencedEpoch + 1;
    int certBlockHeight = initialScState.GetCertSubmissionWindowStart(certEpoch)+1;
    ASSERT_TRUE(certBlockHeight <= initialScState.GetCertSubmissionWindowEnd(certEpoch));

    // create coinbase to finance certificate submission (just in view)
    uint256 inputLowQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputHighQCertHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // create block with certificates ...
    CMutableScCertificate lowQualityCert;
    lowQualityCert.vin.push_back(CTxIn(inputLowQCertHash, 0, CScript(), 0));
    lowQualityCert.nVersion    = SC_CERT_VERSION;
    lowQualityCert.scProof     = CScProof{SAMPLE_CERT_DARLIN_PROOF};
    lowQualityCert.scId        = scId;
    lowQualityCert.epochNumber = initialScState.lastTopQualityCertReferencedEpoch +1;
    lowQualityCert.quality     = 1;
    lowQualityCert.endEpochCumScTxCommTreeRoot = chainActive.Tip()->pprev->scCumTreeHash;
    lowQualityCert.addBwt(CTxOut(CAmount(40), dummyScriptPubKey));
    lowQualityCert.forwardTransferScFee = 0;
    lowQualityCert.mainchainBackwardTransferRequestScFee = 0;

    CMutableScCertificate highQualityCert;
    highQualityCert.vin.push_back(CTxIn(inputHighQCertHash, 0, CScript(), 0));
    highQualityCert.nVersion    = lowQualityCert.nVersion;
    highQualityCert.scProof     = lowQualityCert.scProof ;
    highQualityCert.scId        = lowQualityCert.scId;
    highQualityCert.epochNumber = lowQualityCert.epochNumber;
    highQualityCert.quality     = lowQualityCert.quality * 2;
    highQualityCert.endEpochCumScTxCommTreeRoot = chainActive.Tip()->pprev->scCumTreeHash;
    highQualityCert.addBwt(CTxOut(CAmount(50), dummyScriptPubKey));
    highQualityCert.forwardTransferScFee = 0;
    highQualityCert.mainchainBackwardTransferRequestScFee = 0;

    CBlock certBlock;
    fillBlockHeader(certBlock, uint256S("aaa"));
    certBlock.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));
    certBlock.vcert.push_back(lowQualityCert);
    certBlock.vcert.push_back(highQualityCert);

    // ... and corresponding block index
    CBlockIndex* certBlockIndex = AddToBlockIndex(certBlock);
    certBlockIndex->nHeight = certBlockHeight;
    certBlockIndex->pprev = chainActive.Tip();
    certBlockIndex->pprev->phashBlock = &dummyHash;
    certBlockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(certBlockIndex);

    // test
    bool res = ConnectBlock(certBlock, dummyState, certBlockIndex, *sidechainsView, dummyChain,
                            flagBlockProcessingType::CHECK_ONLY, flagScRelatedChecks::OFF,
                            flagScProofVerification::ON, flagLevelDBIndexesWrite::OFF,
                            &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    CCoins lowQualityCertCoin;
    EXPECT_FALSE(sidechainsView->GetCoins(lowQualityCert.GetHash(), lowQualityCertCoin));

    CCoins highQualityCertCoin;
    sidechainsView->GetCoins(highQualityCert.GetHash(), highQualityCertCoin);
    EXPECT_TRUE(highQualityCertCoin.IsFromCert());
    EXPECT_TRUE(highQualityCertCoin.vout.size() == 1);
    EXPECT_TRUE(highQualityCertCoin.nFirstBwtPos == 0);
    EXPECT_TRUE(highQualityCertCoin.IsAvailable(0));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_ScCreation_then_Mbtr_InSameBlock)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputScCreationHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputMbtrHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain dummyScState;
    uint256 dummyScId = uint256();
    storeSidechain(dummyScId, dummyScState); //Setup bestBlock

    // create block with scCreation and mbtr ...
    CBlock block;
    fillBlockHeader(block, uint256S("aaa"));
    block.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));

    CMutableTransaction scCreation;
    scCreation.vin.push_back(CTxIn(inputScCreationHash, 0, CScript(), 0));
    scCreation.nVersion    = SC_TX_VERSION;
    scCreation.vsc_ccout.resize(1);
    scCreation.vsc_ccout[0].nValue = CAmount(1);
    scCreation.vsc_ccout[0].withdrawalEpochLength = 15;
    scCreation.vsc_ccout[0].forwardTransferScFee = CAmount(0);
    scCreation.vsc_ccout[0].mainchainBackwardTransferRequestScFee = CAmount(0);
    scCreation.vsc_ccout[0].mainchainBackwardTransferRequestDataLength = 1; // The size of mcBwtReq.vScRequestData
    scCreation.vsc_ccout[0].wCertVk = CScVKey{SAMPLE_CERT_DARLIN_VK};
    scCreation.vsc_ccout[0].version = 0;

    CMutableTransaction mbtrTx;
    mbtrTx.vin.push_back(CTxIn(inputMbtrHash, 0, CScript(), 0));
    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = CTransaction(scCreation).GetScIdFromScCcOut(0);
    mcBwtReq.scFee = CAmount(0);
    mcBwtReq.vScRequestData = std::vector<CFieldElement> { CFieldElement{ SAMPLE_FIELD } };
    mbtrTx.nVersion = SC_TX_VERSION;
    mbtrTx.vmbtr_out.push_back(mcBwtReq);

    block.vtx.push_back(scCreation);
    block.vtx.push_back(mbtrTx);

    // ... and corresponding block index
    CBlockIndex* blockIndex = AddToBlockIndex(block);
    blockIndex->nHeight = certBlockHeight;
    blockIndex->pprev = chainActive.Tip();
    blockIndex->pprev->phashBlock = &dummyHash;
    blockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(blockIndex);

    // test
    bool res = ConnectBlock(block, dummyState, blockIndex, *sidechainsView, dummyChain,
                            flagBlockProcessingType::CHECK_ONLY, flagScRelatedChecks::OFF,
                            flagScProofVerification::ON, flagLevelDBIndexesWrite::OFF,
                            &dummyCertStatusUpdateInfo);

    //checks
    ASSERT_TRUE(res);
    ASSERT_TRUE(sidechainsView->HaveSidechain(CTransaction(scCreation).GetScIdFromScCcOut(0)));
}

TEST_F(SidechainsConnectCertsBlockTestSuite, ConnectBlock_Mbtr_then_ScCreation_InSameBlock)
{
    // create coinbase to finance certificate submission (just in view)
    int certBlockHeight {201};
    uint256 inputScCreationHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY);
    uint256 inputMbtrHash = txCreationUtils::CreateSpendableCoinAtHeight(*sidechainsView, certBlockHeight-COINBASE_MATURITY-1);

    // extend blockchain to right height
    chainSettingUtils::ExtendChainActiveToHeight(certBlockHeight - 1);

    // setup sidechain initial state
    CSidechain dummyScState;
    uint256 dummyScId = uint256();
    storeSidechain(dummyScId, dummyScState); //Setup bestBlock

    // create faulty block with mbtr before scCreation ...
    CBlock block;
    fillBlockHeader(block, uint256S("aaa"));
    block.vtx.push_back(createCoinbase(dummyCoinbaseScript, dummyFeeAmount, certBlockHeight));

    CMutableTransaction scCreation;
    scCreation.vin.push_back(CTxIn(inputScCreationHash, 0, CScript(), 0));
    scCreation.nVersion    = SC_TX_VERSION;
    scCreation.vsc_ccout.resize(1);
    scCreation.vsc_ccout[0].nValue = CAmount(1);
    scCreation.vsc_ccout[0].withdrawalEpochLength = 15;
    scCreation.vsc_ccout[0].wCertVk = CScVKey{SAMPLE_CERT_DARLIN_VK};
    scCreation.vsc_ccout[0].version = 0;

    CMutableTransaction mbtrTx;
    mbtrTx.vin.push_back(CTxIn(inputMbtrHash, 0, CScript(), 0));
    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = CTransaction(scCreation).GetScIdFromScCcOut(0);
    mcBwtReq.scFee = CAmount(0);
    mbtrTx.nVersion = SC_TX_VERSION;
    mbtrTx.vmbtr_out.push_back(mcBwtReq);

    block.vtx.push_back(mbtrTx);
    block.vtx.push_back(scCreation);

    // ... and corresponding block index
    CBlockIndex* blockIndex = AddToBlockIndex(block);
    blockIndex->nHeight = certBlockHeight;
    blockIndex->pprev = chainActive.Tip();
    blockIndex->pprev->phashBlock = &dummyHash;
    blockIndex->nHeight = certBlockHeight;

    // add checkpoint to skip expensive checks
    CreateCheckpointAfter(blockIndex);

    // test
    bool res = ConnectBlock(block, dummyState, blockIndex, *sidechainsView, dummyChain,
                            flagBlockProcessingType::CHECK_ONLY, flagScRelatedChecks::OFF,
                            flagScProofVerification::ON, flagLevelDBIndexesWrite::OFF,
                            &dummyCertStatusUpdateInfo);

    //checks
    EXPECT_FALSE(res);
}
///////////////////////////////////////////////////////////////////////////////
/////////////////////////////////// HELPERS ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void SidechainsConnectCertsBlockTestSuite::storeSidechain(const uint256& scId, const CSidechain& sidechain)
{
    txCreationUtils::storeSidechain(sidechainsView->getSidechainMap(), scId, sidechain);

    CSidechainsMap      dummySidechains;
    CSidechainEventsMap dummySidechainsEvents;
    CCoinsMap           dummyCoins;
    uint256             dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
    CNullifiersMap      dummyNullifiers;

    CAnchorsCacheEntry dummyAnchorsEntry;
    dummyAnchorsEntry.entered = true;
    dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;

    CAnchorsMap dummyAnchors;
    dummyAnchors[dummyAnchor] = dummyAnchorsEntry;

    CCswNullifiersMap dummyCswNullifiers;

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyAnchor, dummyAnchors,
                               dummyNullifiers, dummySidechains, dummySidechainsEvents, dummyCswNullifiers);

    return;
}

void SidechainsConnectCertsBlockTestSuite::storeSidechainEvent(int eventHeight, const CSidechainEvents& scEvent)
{
    txCreationUtils::storeSidechainEvent(sidechainsView->getScEventsMap(), eventHeight, scEvent);

    CSidechainsMap      dummySidechains;
    CSidechainEventsMap dummySidechainsEvents;
    CCoinsMap           dummyCoins;
    uint256             dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
    CNullifiersMap      dummyNullifiers;

    CAnchorsCacheEntry dummyAnchorsEntry;
    dummyAnchorsEntry.entered = true;
    dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;

    CAnchorsMap dummyAnchors;
    dummyAnchors[dummyAnchor] = dummyAnchorsEntry;

    CCswNullifiersMap dummyCswNullifiers;

    sidechainsView->BatchWrite(dummyCoins, dummyHash, dummyAnchor, dummyAnchors,
                               dummyNullifiers, dummySidechains, dummySidechainsEvents, dummyCswNullifiers);

    return;
}

void SidechainsConnectCertsBlockTestSuite::fillBlockHeader(CBlock& blockToFill, const uint256& prevBlockHash, int bl_ver)
{
    blockToFill.nVersion = bl_ver;
    blockToFill.hashPrevBlock = prevBlockHash;
    blockToFill.hashMerkleRoot = uint256();
    blockToFill.hashScTxsCommitment.SetNull();

    static unsigned int runCounter = 0;
    SetMockTime(time(nullptr) + ++runCounter);
    CBlockIndex fakePrevBlockIdx(Params().GenesisBlock());
    UpdateTime(&blockToFill, Params().GetConsensus(), &fakePrevBlockIdx);

    blockToFill.nBits = UintToArith256(Params().GetConsensus().powLimit).GetCompact();
    blockToFill.nNonce = Params().GenesisBlock().nNonce;
    return;
}

void SidechainsConnectCertsBlockTestSuite::CreateCheckpointAfter(CBlockIndex* blkIdx)
{
    assert(blkIdx != nullptr);

    CBlock dummyCheckpointBlock;
    CBlockIndex* dummyCheckPoint = AddToBlockIndex(dummyCheckpointBlock);
    dummyCheckPoint->nHeight = blkIdx->nHeight + 1;
    dummyCheckPoint->pprev = blkIdx;
    Checkpoints::CCheckpointData& checkpoints = const_cast<Checkpoints::CCheckpointData&>(Params().Checkpoints());
    checkpoints.mapCheckpoints[dummyCheckPoint->nHeight] = dummyCheckpointBlock.GetHash();
}

///////////////////////////////////////////////////////////////////////////////
/////////////////////////////// BLOCK_FORMATION ///////////////////////////////
///////////////////////////////////////////////////////////////////////////////
#include <algorithm>

class SidechainsBlockFormationTestSuite : public ::testing::Test {
public:
    SidechainsBlockFormationTestSuite():
        fakeChainStateDb(nullptr), blockchainView(nullptr),
        vecPriority(), orphanList(), mapDependers(),
        dummyHeight(1987), dummyLockTimeCutoff(0),
        dummyAmount(10), dummyScript(), dummyOut(dummyAmount, dummyScript) {}

    ~SidechainsBlockFormationTestSuite() = default;

    void SetUp() override {
        SelectParams(CBaseChainParams::REGTEST);

        UnloadBlockIndex();

        fakeChainStateDb   = new blockchain_test_utils::CInMemorySidechainDb();
        blockchainView     = new CCoinsViewCache(fakeChainStateDb);
    };

    void TearDown() override {
        delete blockchainView;
        blockchainView = nullptr;

        delete fakeChainStateDb;
        fakeChainStateDb = nullptr;

        UnloadBlockIndex();
    };

protected:
    blockchain_test_utils::CInMemorySidechainDb *fakeChainStateDb;
    CCoinsViewCache      *blockchainView;

    std::vector<TxPriority> vecPriority;
    std::list<COrphan> orphanList;
    std::map<uint256, std::vector<COrphan*> > mapDependers;

    int dummyHeight;
    int64_t dummyLockTimeCutoff;

    CAmount dummyAmount;
    CScript dummyScript;
    CTxOut dummyOut;
};

TEST_F(SidechainsBlockFormationTestSuite, EmptyMempoolOrdering)
{
    ASSERT_TRUE(mempool.size() == 0);

    GetBlockTxPriorityData(*blockchainView, dummyHeight, dummyLockTimeCutoff, vecPriority, orphanList, mapDependers);
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    EXPECT_TRUE(vecPriority.size() == 0);
    EXPECT_TRUE(orphanList.size() == 0);
    EXPECT_TRUE(mapDependers.size() == 0);
}

TEST_F(SidechainsBlockFormationTestSuite, SingleTxes_MempoolOrdering)
{
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);
    uint256 inputCoinHash_2 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight-1);

    CMutableTransaction tx_highFee;
    tx_highFee.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    tx_highFee.addOut(dummyOut);
    CTxMemPoolEntry tx_highFee_entry(tx_highFee, /*fee*/CAmount(100), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(tx_highFee.GetHash(), tx_highFee_entry));

    CMutableTransaction tx_highPriority;
    tx_highPriority.vin.push_back(CTxIn(inputCoinHash_2, 0, dummyScript));
    tx_highPriority.addOut(dummyOut);
    CTxMemPoolEntry tx_highPriority_entry(tx_highPriority, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/100.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(tx_highPriority.GetHash(), tx_highPriority_entry));

    //test
    GetBlockTxPriorityData(*blockchainView, dummyHeight, dummyLockTimeCutoff, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 2);
    EXPECT_TRUE(orphanList.size() == 0);

    TxPriorityCompare sortByFee(/*sort-by-fee*/true);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByFee);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == tx_highFee.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == tx_highPriority.GetHash());

    TxPriorityCompare sortByPriority(/*sort-by-fee*/false);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByPriority);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == tx_highPriority.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == tx_highFee.GetHash());
}

TEST_F(SidechainsBlockFormationTestSuite, DifferentScIdCerts_FeesAndPriorityOnlyContributeToMempoolOrdering)
{
    LOCK(mempool.cs); //needed when compiled with --enable-debug, which activates ASSERT_HELD
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);
    uint256 inputCoinHash_2 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight-1);

    CMutableScCertificate cert_highFee;
    cert_highFee.scId = uint256S("aaa");
    cert_highFee.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    cert_highFee.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highFee_entry(cert_highFee, /*fee*/CAmount(100), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highFee.GetHash(), cert_highFee_entry));

    CMutableScCertificate cert_highPriority;
    cert_highPriority.scId = uint256S("bbb");
    cert_highPriority.vin.push_back(CTxIn(inputCoinHash_2, 0, dummyScript));
    cert_highPriority.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highPriority_entry(cert_highPriority, /*fee*/CAmount(1),   /*time*/ 1000, /*priority*/100.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highPriority.GetHash(), cert_highPriority_entry));

    //test
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 2);
    EXPECT_TRUE(orphanList.size() == 0);

    TxPriorityCompare sortByFee(/*sort-by-fee*/true);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByFee);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == cert_highFee.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_highPriority.GetHash());

    TxPriorityCompare sortByPriority(/*sort-by-fee*/false);
    std::make_heap(vecPriority.begin(), vecPriority.end(), sortByPriority);
    EXPECT_TRUE(vecPriority.front().get<2>()->GetHash() == cert_highPriority.GetHash());
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_highFee.GetHash());
}

TEST_F(SidechainsBlockFormationTestSuite, SameScIdCerts_HighwQualityCertsSpedingLowQualityOnesAreAccepted)
{
    LOCK(mempool.cs); //needed when compiled with --enable-debug, which activates ASSERT_HELD
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);

    CMutableScCertificate cert_lowQuality;
    cert_lowQuality.scId = uint256S("aaa");
    cert_lowQuality.quality = 100;
    cert_lowQuality.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    cert_lowQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_lowQuality_entry(cert_lowQuality, /*fee*/CAmount(1),   /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_lowQuality.GetHash(), cert_lowQuality_entry));

    CMutableScCertificate cert_highQuality;
    cert_highQuality.scId = cert_lowQuality.scId;
    cert_highQuality.quality = cert_lowQuality.quality * 2;
    cert_highQuality.vin.push_back(CTxIn(cert_lowQuality.GetHash(), 0, dummyScript));
    cert_highQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highQuality_entry(cert_highQuality, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highQuality.GetHash(), cert_highQuality_entry));

    //test
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 1);
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_lowQuality.GetHash());
    EXPECT_TRUE(orphanList.size() == 1);
    EXPECT_TRUE(*dynamic_cast<const CScCertificate*>(orphanList.back().ptx) == CScCertificate(cert_highQuality));
}

TEST_F(SidechainsBlockFormationTestSuite, SameScIdCerts_LowQualityCertsSpedingHighQualityOnesAreRejected)
{
    LOCK(mempool.cs); //needed when compiled with --enable-debug, which activates ASSERT_HELD
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);

    CMutableScCertificate cert_highQuality;
    cert_highQuality.scId = uint256S("aaa");
    cert_highQuality.quality = 100;
    cert_highQuality.vin.push_back(CTxIn(inputCoinHash_1, 0, dummyScript));
    cert_highQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_highQuality_entry(cert_highQuality, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_highQuality.GetHash(), cert_highQuality_entry));

    CMutableScCertificate cert_lowQuality;
    cert_lowQuality.scId = cert_highQuality.scId;
    cert_lowQuality.quality = cert_highQuality.quality / 2;
    cert_lowQuality.vin.push_back(CTxIn(cert_highQuality.GetHash(), 0, dummyScript));
    cert_lowQuality.addOut(dummyOut);
    CCertificateMemPoolEntry cert_lowQuality_entry(cert_lowQuality, /*fee*/CAmount(1),   /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(cert_lowQuality.GetHash(), cert_lowQuality_entry));

    //test
    GetBlockCertPriorityData(*blockchainView, dummyHeight, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 1);
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == cert_highQuality.GetHash());
    EXPECT_TRUE(orphanList.size() == 0) << "cert_lowQuality should not be counted since it's wrong dependency";
}

TEST_F(SidechainsBlockFormationTestSuite, Unconfirmed_Mbtr_scCreation_DulyOrdered)
{
    uint256 inputCoinHash_1 = txCreationUtils::CreateSpendableCoinAtHeight(*blockchainView, dummyHeight);

    CMutableTransaction mutScCreation = txCreationUtils::createNewSidechainTxWith(dummyAmount, dummyHeight);
    mutScCreation.vin.at(0) = CTxIn(inputCoinHash_1, 0, dummyScript);
    CTransaction scCreation(mutScCreation);
    const uint256& scId = scCreation.GetScIdFromScCcOut(0);
    CTxMemPoolEntry scCreation_entry(scCreation, /*fee*/CAmount(1), /*time*/ 1000, /*priority*/1.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(scCreation.GetHash(), scCreation_entry));

    CMutableTransaction mbtrTx;
    CBwtRequestOut mcBwtReq;
    mcBwtReq.scId = scId;
    mbtrTx.nVersion = SC_TX_VERSION;
    mbtrTx.vmbtr_out.push_back(mcBwtReq);
    CTxMemPoolEntry mbtr_entry(mbtrTx, /*fee*/CAmount(1000),   /*time*/ 1000, /*priority*/1000.0, /*height*/dummyHeight);
    ASSERT_TRUE(mempool.addUnchecked(mbtrTx.GetHash(), mbtr_entry, /*fCurrentEstimate*/true));

    //test
    int64_t dummyLockTimeCutoff{0};
    GetBlockTxPriorityData(*blockchainView, dummyHeight, dummyLockTimeCutoff, vecPriority, orphanList, mapDependers);

    //checks
    EXPECT_TRUE(vecPriority.size() == 1);
    EXPECT_TRUE(vecPriority.back().get<2>()->GetHash() == scCreation.GetHash());
    EXPECT_TRUE(orphanList.size() == 1);
    EXPECT_TRUE(orphanList.front().ptx->GetHash() == CTransaction(mbtrTx).GetHash());
}

TEST_F(SidechainsConnectCertsBlockTestSuite, SizeCheck)
{
    srand(time(NULL));
    for (int k = 0; k < 20; k++)
    {
        static const int NUM_TXES  = rand() % 512;
        static const int NUM_CERTS = rand() % 512;
 
        size_t size_of_num_tx   = ::GetSizeOfCompactSize(NUM_TXES); 
        size_t size_of_num_cert = ::GetSizeOfCompactSize(NUM_CERTS); 
 
        CBlock certBlock;

        bool sc_support = (k % 2 == 0);
        //bool sc_support = false;
        if (sc_support)
        {
            fillBlockHeader(certBlock, uint256S("aaa"), BLOCK_VERSION_SC_SUPPORT);
        }
        else
        {
            fillBlockHeader(certBlock, uint256S("aaa"), BLOCK_VERSION_BEFORE_SC);
        }
 
        size_t totTxSize1 = 0;
        for (int i = 0; i < NUM_TXES; i++)
        {
            CTransaction tx;
            certBlock.vtx.push_back(tx);
            totTxSize1 += tx.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
        }
 
        size_t totCertSize1 = 0;
        if (sc_support)
        {
            for (int i = 0; i < NUM_CERTS; i++)
            {
                CScCertificate cert;
                certBlock.vcert.push_back(cert);
                totCertSize1 += cert.GetSerializeSize(SER_NETWORK, PROTOCOL_VERSION);
            }
        }
 
        // compute block size with legacy func
        size_t sz1 = ::GetSerializeSize(certBlock, SER_NETWORK, PROTOCOL_VERSION);
 
        size_t h_sz = 0;
        size_t totTxSize2 = 0;
        size_t totCertSize2 = 0;

        // compute block size with new func
        size_t sz2 = certBlock.GetSerializeComponentsSize(h_sz, totTxSize2, totCertSize2);
 
#if 0
        std::cout << "block sc support = " << (int)sc_support << std::endl;
        std::cout << "-------------------------------------------" << std::endl;
        std::cout << "block sz1     = " << sz1 << std::endl;
        std::cout << "totTxSize1    = " << totTxSize1 << std::endl;
        std::cout << "totCertSize1  = " << totCertSize1 << std::endl;
        std::cout << "-------------------------------------------" << std::endl;
        std::cout << "header sz    = " << h_sz << std::endl;
        std::cout << "num txes     = " << NUM_TXES  << std::endl;
        std::cout << "size_of_num_tx = " << size_of_num_tx << std::endl;
        std::cout << "totTxSize2   = " << totTxSize2 << std::endl;
        std::cout << "vtx ser sz = " << ::GetSerializeSize(certBlock.vtx,   SER_NETWORK, PROTOCOL_VERSION) << std::endl;
        std::cout << "num certs    = " << NUM_CERTS << std::endl;
        std::cout << "size_of_num_cert = " << size_of_num_cert << std::endl;
        std::cout << "totCertSize2 = " << totCertSize2 << std::endl;
        std::cout << "vcert ser sz = " << ::GetSerializeSize(certBlock.vcert,   SER_NETWORK, PROTOCOL_VERSION) << std::endl;
        std::cout << "=============================================" << std::endl;
#endif
 
        EXPECT_TRUE(sz1 == sz2);

        EXPECT_TRUE(size_of_num_tx   + totTxSize2   == ::GetSerializeSize(certBlock.vtx,   SER_NETWORK, PROTOCOL_VERSION));
        if (sc_support)
        {
            EXPECT_TRUE(sz1 == (h_sz + size_of_num_tx + totTxSize2 + size_of_num_cert + totCertSize2 ));
            EXPECT_TRUE(size_of_num_cert + totCertSize2 == ::GetSerializeSize(certBlock.vcert, SER_NETWORK, PROTOCOL_VERSION));
        }
        else
        {
            EXPECT_TRUE(sz1 == (h_sz + size_of_num_tx + totTxSize2));
            // empty vector in any case
            EXPECT_TRUE(1 == ::GetSerializeSize(certBlock.vcert, SER_NETWORK, PROTOCOL_VERSION));
        }

    }
}

