#include "tx_creation_utils.h"
#include <gtest/libzendoo_test_files.h>
#include <primitives/transaction.h>
#include <primitives/certificate.h>
#include <script/interpreter.h>
#include <main.h>
#include <pubkey.h>
#include <miner.h>
#include <undo.h>
#include "tx_creation_utils.h"
#include <pow.h>
#include <coins.h>
#include <zen/forks/fork7_sidechainfork.h>
#include <script/sign.h>

CMutableTransaction txCreationUtils::populateTx(int txVersion, const CAmount & creationTxAmount, int epochLength,
                                                const CAmount& ftScFee, const CAmount& mbtrScFee, int mbtrDataLength)
{
    CMutableTransaction mtx;
    mtx.nVersion = txVersion;

    mtx.vin.resize(2);
    mtx.vin[0].prevout.hash = uint256S("1");
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.hash = uint256S("2");
    mtx.vin[1].prevout.n = 0;

    mtx.resizeOut(2);
    mtx.getOut(0).nValue = 0;
    mtx.getOut(1).nValue = 0;

    mtx.vjoinsplit.push_back(
            JSDescription::getNewInstance(txVersion == GROTH_TX_VERSION));
    mtx.vjoinsplit.push_back(
            JSDescription::getNewInstance(txVersion == GROTH_TX_VERSION));
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0");
    mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("1");
    mtx.vjoinsplit[1].nullifiers.at(0) = uint256S("2");
    mtx.vjoinsplit[1].nullifiers.at(1) = uint256S("3");

    mtx.vsc_ccout.resize(1);
    mtx.vsc_ccout[0].nValue = creationTxAmount;
    mtx.vsc_ccout[0].withdrawalEpochLength = epochLength;
    mtx.vsc_ccout[0].wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    mtx.vsc_ccout[0].wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    mtx.vsc_ccout[0].vFieldElementCertificateFieldConfig.push_back(22);
    mtx.vsc_ccout[0].customData.push_back(0x33);
    mtx.vsc_ccout[0].forwardTransferScFee = ftScFee;
    mtx.vsc_ccout[0].mainchainBackwardTransferRequestScFee = mbtrScFee;
    mtx.vsc_ccout[0].mainchainBackwardTransferRequestDataLength = mbtrDataLength;

    return mtx;
}

void txCreationUtils::signTx(CMutableTransaction& mtx)
{
    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;
    // Compute the correct hSig.
    // TODO: #966.
    static const uint256 one(uint256S("1"));
    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL);
    if (dataToBeSigned == one) {
        throw std::runtime_error("SignatureHash failed");
    }
    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL, dataToBeSigned.begin(), 32, joinSplitPrivKey ) == 0);
}

void txCreationUtils::signTx(CMutableScCertificate& mcert)
{
    // Compute the correct hSig.
    // TODO: #966.
    static const uint256 one(uint256S("1"));
    // Empty output script.
    CScript scriptCode;
    CScCertificate signedCert(mcert);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signedCert, NOT_AN_INPUT, SIGHASH_ALL);
    if (dataToBeSigned == one) {
        throw std::runtime_error("SignatureHash failed");
    }
    // Add the signature
}

CTransaction txCreationUtils::createNewSidechainTxWith(const CAmount & creationTxAmount, int epochLength)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, creationTxAmount, epochLength);
    mtx.resizeOut(0);
    mtx.vjoinsplit.resize(0);
    mtx.vft_ccout.resize(0);
    signTx(mtx);

    return CTransaction(mtx);
}

CTransaction txCreationUtils::createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, fwdTxAmount);
    mtx.resizeOut(0);
    mtx.vjoinsplit.resize(0);
    mtx.vsc_ccout.resize(0);

    mtx.vft_ccout.resize(1);
    mtx.vft_ccout[0].scId = newScId;
    mtx.vft_ccout[0].nValue = fwdTxAmount;

    signTx(mtx);

    return CTransaction(mtx);
}

CTxCeasedSidechainWithdrawalInput txCreationUtils::CreateCSWInput(
    const uint256& scId, const std::string& nullifierHex, const std::string& actCertDataHex,
    const std::string& ceasingCumScTxCommTreeHex, CAmount amount)
{
    std::vector<unsigned char> tmp1 = ParseHex(nullifierHex);
    tmp1.resize(CFieldElement::ByteSize());
    CFieldElement nullifier{tmp1};

    std::vector<unsigned char> tmp2 = ParseHex(actCertDataHex);
    tmp2.resize(CFieldElement::ByteSize());
    CFieldElement actCertDataHash{tmp2};

    std::vector<unsigned char> tmp3 = ParseHex(ceasingCumScTxCommTreeHex);
    tmp3.resize(CFieldElement::ByteSize());
    CFieldElement ceasingCumScTxCommTree{tmp3};

    uint160 dummyPubKeyHash {};
    CScProof dummyScProof{SAMPLE_CERT_DARLIN_PROOF};
    CScript dummyRedeemScript;

    return CTxCeasedSidechainWithdrawalInput(amount, scId, nullifier, dummyPubKeyHash, dummyScProof, actCertDataHash, ceasingCumScTxCommTree, dummyRedeemScript);
}

CTransaction txCreationUtils::createCSWTxWith(const CTxCeasedSidechainWithdrawalInput& csw)
{
    CMutableTransaction mtx;
    mtx.nVersion = SC_TX_VERSION;
    mtx.vcsw_ccin.resize(1);
    mtx.vcsw_ccin[0] = csw;

    return CTransaction(mtx);
}

CTransaction txCreationUtils::createCoinBase(const CAmount& amount)
{
    CMutableTransaction mutCoinBase;
    mutCoinBase.vin.push_back(CTxIn(uint256(), -1));
    mutCoinBase.addOut(CTxOut(amount,CScript()));
    return CTransaction(mutCoinBase);
}

// Well-formatted transparent txs have no sc-related info.
// ccisNull allow you to create a faulty transparent tx, for testing purposes.
CTransaction txCreationUtils::createTransparentTx(bool ccIsNull)
{
    CMutableTransaction mtx = populateTx(TRANSPARENT_TX_VERSION);
    mtx.vjoinsplit.resize(0);

    if (ccIsNull)
    {
        mtx.vcsw_ccin.resize(0);
        mtx.vsc_ccout.resize(0);
        mtx.vft_ccout.resize(0);
    }
    signTx(mtx);

    return CTransaction(mtx);
}

CTransaction txCreationUtils::createSproutTx(bool ccIsNull)
{
    CMutableTransaction mtx;

    if (ccIsNull)
    {
        mtx = populateTx(PHGR_TX_VERSION);
        mtx.vcsw_ccin.resize(0);
        mtx.vsc_ccout.resize(0);
        mtx.vft_ccout.resize(0);
    } else
    {
        mtx = populateTx(SC_TX_VERSION);
    }
    signTx(mtx);

    return CTransaction(mtx);
}

void txCreationUtils::addNewScCreationToTx(CTransaction & tx, const CAmount & scAmount)
{
    CMutableTransaction mtx = tx;

    mtx.nVersion = SC_TX_VERSION;

    CTxScCreationOut aSidechainCreationTx;
    aSidechainCreationTx.nValue = scAmount;
    aSidechainCreationTx.withdrawalEpochLength = 100;
    mtx.vsc_ccout.push_back(aSidechainCreationTx);

    tx = mtx;
    return;
}

CScCertificate txCreationUtils::createCertificate(
    const uint256 & scId, int epochNum, const uint256 & endEpochBlockHash,
    const CFieldElement& endEpochCumScTxCommTreeRoot, CAmount changeTotalAmount, unsigned int numChangeOut,
    CAmount bwtTotalAmount, unsigned int numBwt, CAmount ftScFee, CAmount mbtrScFee, const int quality)
{
    CMutableScCertificate res;
    res.nVersion = SC_CERT_VERSION;
    res.scId = scId;
    res.epochNumber = epochNum;
    res.endEpochBlockHash = endEpochBlockHash;
    res.endEpochCumScTxCommTreeRoot = endEpochCumScTxCommTreeRoot;
    res.quality = quality;
    res.forwardTransferScFee = ftScFee;
    res.mainchainBackwardTransferRequestScFee = mbtrScFee;

    res.scProof = CScProof{SAMPLE_CERT_DARLIN_PROOF};

    res.vin.resize(1);
    res.vin[0].prevout.hash = uint256S("1");
    res.vin[0].prevout.n = 0;
    
    CScript dummyScriptPubKey =
            GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/false);
    for(unsigned int idx = 0; idx < numChangeOut; ++idx)
        res.addOut(CTxOut(changeTotalAmount/numChangeOut,dummyScriptPubKey));

    for(unsigned int idx = 0; idx < numBwt; ++idx)
        res.addBwt(CTxOut(bwtTotalAmount/numBwt, dummyScriptPubKey));

    return res;
}

uint256 txCreationUtils::CreateSpendableCoinAtHeight(CCoinsViewCache& targetView, unsigned int coinHeight)
{
    CAmount dummyFeeAmount {0};
    CScript dummyCoinbaseScript = CScript() << OP_DUP << OP_HASH160
            << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;

    CTransaction inputTx = createCoinbase(dummyCoinbaseScript, dummyFeeAmount, coinHeight);
    CTxUndo dummyUndo;
    UpdateCoins(inputTx, targetView, dummyUndo, coinHeight);
    assert(targetView.HaveCoins(inputTx.GetHash()));
    return inputTx.GetHash();
}

void txCreationUtils::storeSidechain(CSidechainsMap& mapToWriteInto, const uint256& scId, const CSidechain& sidechain)
{
    auto value = CSidechainsCacheEntry(sidechain, CSidechainsCacheEntry::Flags::DIRTY);
    WriteMutableEntry(scId, value, mapToWriteInto);
}

void txCreationUtils::storeSidechainEvent(CSidechainEventsMap& mapToWriteInto, int eventHeight, const CSidechainEvents& scEvent)
{
    auto value = CSidechainEventsCacheEntry(scEvent, CSidechainsCacheEntry::Flags::DIRTY);
    WriteMutableEntry(eventHeight, value, mapToWriteInto);
}

void chainSettingUtils::ExtendChainActiveToHeight(int targetHeight)
{
    if (chainActive.Height() > targetHeight)
       return chainActive.SetTip(chainActive[targetHeight]); //TODO: delete content before setting tip

    ZCIncrementalMerkleTree dummyTree;
    dummyTree.append(GetRandHash());

    uint256 prevBlockHash = chainActive.Height() <= 0 ? uint256(): *(chainActive.Tip()->phashBlock);
    for (unsigned int height=std::max(chainActive.Height(),0); height<= targetHeight; ++height) {
        uint256 currBlockHash = ArithToUint256(height);
        CBlockIndex* pNewBlockIdx = new CBlockIndex();
        assert(pNewBlockIdx != nullptr);

        pNewBlockIdx->nHeight = height;
        pNewBlockIdx->pprev = height == 0? nullptr : mapBlockIndex.at(prevBlockHash);
        pNewBlockIdx->nTime = 1269211443 + height * Params().GetConsensus().nPowTargetSpacing;
        pNewBlockIdx->nBits = 0x1e7fffff;
        pNewBlockIdx->nChainWork = height == 0 ? arith_uint256(0) : mapBlockIndex.at(prevBlockHash)->nChainWork + GetBlockProof(*(mapBlockIndex.at(prevBlockHash)));
        pNewBlockIdx->hashAnchor = dummyTree.root();
        pNewBlockIdx->nVersion = ForkManager::getInstance().getNewBlockVersion(height);

        BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(currBlockHash, pNewBlockIdx)).first;
        pNewBlockIdx->phashBlock = &(mi->first);

        if (pNewBlockIdx->pprev && pNewBlockIdx->nVersion == BLOCK_VERSION_SC_SUPPORT )
        {
            // don't do a real cumulative poseidon hash if it is not necessary
            pNewBlockIdx->scCumTreeHash = CFieldElement{SAMPLE_FIELD};
        }

        chainActive.SetTip(mapBlockIndex.at(currBlockHash));

        prevBlockHash = currBlockHash;
    }
    return;
}

void chainSettingUtils::ExtendChainActiveWithBlock(const CBlock& block)
{
    ZCIncrementalMerkleTree dummyTree;
    dummyTree.append(GetRandHash());

    uint256 prevBlockHash = chainActive.Height() <= 0 ? uint256(): *(chainActive.Tip()->phashBlock);

    uint256 currBlockHash = block.GetHash();
    CBlockIndex* pNewBlockIdx = new CBlockIndex();
    assert(pNewBlockIdx != nullptr);

    pNewBlockIdx->nHeight = chainActive.Height()+1;
    pNewBlockIdx->pprev = (chainActive.Height()+1) == 0? nullptr : mapBlockIndex.at(prevBlockHash);
    pNewBlockIdx->nTime = 1269211443 + chainActive.Height()+1 * Params().GetConsensus().nPowTargetSpacing;
    pNewBlockIdx->nBits = 0x1e7fffff;
    pNewBlockIdx->nChainWork = (chainActive.Height()+1) == 0 ? arith_uint256(0) : mapBlockIndex.at(prevBlockHash)->nChainWork + GetBlockProof(*(mapBlockIndex.at(prevBlockHash)));
    pNewBlockIdx->hashAnchor = dummyTree.root();

    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(currBlockHash, pNewBlockIdx)).first;
    pNewBlockIdx->phashBlock = &(mi->first);
    chainActive.SetTip(mapBlockIndex.at(currBlockHash));
    return;
}


namespace blockchain_test_utils
{
/**
 * @brief Constructs a new BlockchainTestManager.
 */
BlockchainTestManager::BlockchainTestManager()
{
    assert(Params().NetworkIDString() == "regtest");

    Reset();

    // Start the thread for async sidechain proof verification
    threadGroup.create_thread(
        boost::bind(
                &CScAsyncProofVerifier::RunPeriodicVerification,
                &CScAsyncProofVerifier::GetInstance()
        )
    );
}

/**
 * @brief CoinsView getter.
 * 
 * @return std::shared_ptr<CInMemorySidechainDb> The in-memory CoinsView used by the manager.
 */
std::shared_ptr<CInMemorySidechainDb> BlockchainTestManager::CoinsView()
{
    return view;
}

/**
 * @brief CoinsViewCache getter.
 * 
 * @return std::shared_ptr<CNakedCCoinsViewCache> The CoinsViewCache used by the manager.
 */
std::shared_ptr<CNakedCCoinsViewCache> BlockchainTestManager::CoinsViewCache()
{
    return viewCache;
}

/**
 * @brief Adds new blocks to the active chain to reach the targetHeight.
 * 
 * @param targetHeight The height to be reached by the active chain.
 */
void BlockchainTestManager::ExtendChainActiveToHeight(int targetHeight)
{
    if (chainActive.Height() > targetHeight)
       return chainActive.SetTip(chainActive[targetHeight]); //TODO: delete content before setting tip

    ZCIncrementalMerkleTree dummyTree;
    dummyTree.append(GetRandHash());

    uint256 prevBlockHash = chainActive.Height() <= 0 ? uint256(): *(chainActive.Tip()->phashBlock);
    for (unsigned int height=std::max(chainActive.Height(),0); height<= targetHeight; ++height) {
        uint256 currBlockHash = ArithToUint256(height);
        CBlockIndex* pNewBlockIdx = new CBlockIndex();
        assert(pNewBlockIdx != nullptr);

        pNewBlockIdx->nHeight = height;
        pNewBlockIdx->pprev = height == 0? nullptr : mapBlockIndex.at(prevBlockHash);
        pNewBlockIdx->nTime = 1269211443 + height * Params().GetConsensus().nPowTargetSpacing;
        pNewBlockIdx->nBits = 0x1e7fffff;
        pNewBlockIdx->nChainWork = height == 0 ? arith_uint256(0) : mapBlockIndex.at(prevBlockHash)->nChainWork + GetBlockProof(*(mapBlockIndex.at(prevBlockHash)));
        pNewBlockIdx->hashAnchor = dummyTree.root();
        pNewBlockIdx->nVersion = ForkManager::getInstance().getNewBlockVersion(height);

        BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(currBlockHash, pNewBlockIdx)).first;
        pNewBlockIdx->phashBlock = &(mi->first);

        if (pNewBlockIdx->pprev && pNewBlockIdx->nVersion == BLOCK_VERSION_SC_SUPPORT )
        {
            // don't do a real cumulative poseidon hash if it is not necessary
            pNewBlockIdx->scCumTreeHash = CFieldElement{SAMPLE_FIELD};
        }

        chainActive.SetTip(mapBlockIndex.at(currBlockHash));

        prevBlockHash = currBlockHash;
    }
    return;
}

/**
 * @brief Adds a new block to the active chain.
 * 
 * @param block The block to be added to the active chain.
 */
void BlockchainTestManager::ExtendChainActiveWithBlock(const CBlock& block)
{
    ZCIncrementalMerkleTree dummyTree;
    dummyTree.append(GetRandHash());

    uint256 prevBlockHash = chainActive.Height() <= 0 ? uint256(): *(chainActive.Tip()->phashBlock);

    uint256 currBlockHash = block.GetHash();
    CBlockIndex* pNewBlockIdx = new CBlockIndex();
    assert(pNewBlockIdx != nullptr);

    pNewBlockIdx->nHeight = chainActive.Height()+1;
    pNewBlockIdx->pprev = (chainActive.Height()+1) == 0? nullptr : mapBlockIndex.at(prevBlockHash);
    pNewBlockIdx->nTime = 1269211443 + chainActive.Height()+1 * Params().GetConsensus().nPowTargetSpacing;
    pNewBlockIdx->nBits = 0x1e7fffff;
    pNewBlockIdx->nChainWork = (chainActive.Height()+1) == 0 ? arith_uint256(0) : mapBlockIndex.at(prevBlockHash)->nChainWork + GetBlockProof(*(mapBlockIndex.at(prevBlockHash)));
    pNewBlockIdx->hashAnchor = dummyTree.root();

    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(currBlockHash, pNewBlockIdx)).first;
    pNewBlockIdx->phashBlock = &(mi->first);
    chainActive.SetTip(mapBlockIndex.at(currBlockHash));
    return;
}

/**
 * @brief Resets the status of the blockchain.
 * 
 * This is particularly useful to reset the status of the async proof verifier
 * since it is a singleton class.
 */
void BlockchainTestManager::Reset()
{
    view = std::make_shared<CInMemorySidechainDb>();
    viewCache = std::make_shared<CNakedCCoinsViewCache>(view.get());

    ResetAsyncProofVerifier();
}

/**
 * @brief Creates a CSW input object.
 * 
 * @param scId The ID of the sidechain the CSW refers to
 * @return CTxCeasedSidechainWithdrawalInput the CSW input created.
 */
CTxCeasedSidechainWithdrawalInput BlockchainTestManager::CreateCswInput(uint256 scId, CAmount nValue)
{
    CTxCeasedSidechainWithdrawalInput input;

    input.scId = scId;
    input.nValue = nValue;
    input.scProof = CScProof{SAMPLE_CSW_DARLIN_PROOF};

    return input;
}

/**
 * @brief Creates a mutable transaction based on the parameters passed as input.
 * 
 * @param args The parameters of the transaction
 * @return CMutableTransaction the generated transaction.
 */
CMutableTransaction BlockchainTestManager::CreateTransaction(const CTransactionCreationArguments& args)
{
    CMutableTransaction tx;

    tx.nVersion = args.nVersion;

    tx.vcsw_ccin = args.vcsw_ccin;
    tx.vft_ccout = args.vft_ccout;
    tx.vmbtr_out = args.vmbtr_out;
    tx.vsc_ccout = args.vsc_ccout;

    return tx;
}

/**
 * @brief Generate a new sidechain certificate.
 * 
 * @param scId The ID of the sidechain the certificate refers to
 * @param epochNumber The epoch number of the certificate
 * @param quality The quality of the certificate
 * @return CScCertificate The generated certificate.
 */
CScCertificate BlockchainTestManager::GenerateCertificate(uint256 scId, int epochNumber, int64_t quality, CTransactionBase* inputTxBase)
{
    uint256 dummyBlockHash {};
    CFieldElement endEpochCumScTxCommTreeRoot = CFieldElement{SAMPLE_FIELD};
    CAmount inputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount changeTotalAmount = inputAmount - dummyNonZeroFee;
    CAmount bwtTotalAmount {0};
    unsigned int numChangeOut = 1;
    unsigned int numBwt = 2;
    //CTransactionBase* inputTxBase = nullptr;

    CMutableScCertificate res;
    res.nVersion = SC_CERT_VERSION;
    res.scId = scId;
    res.epochNumber = epochNumber;
    res.endEpochBlockHash = dummyBlockHash;
    res.endEpochCumScTxCommTreeRoot = endEpochCumScTxCommTreeRoot;
    res.quality = quality;
    res.forwardTransferScFee = 0;
    res.mainchainBackwardTransferRequestScFee = 0;

    CScript dummyScriptPubKey =
            GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))),/*withCheckBlockAtHeight*/true);
    for(unsigned int idx = 0; idx < numChangeOut; ++idx)
        res.addOut(CTxOut(changeTotalAmount/numChangeOut,dummyScriptPubKey));

    for(unsigned int idx = 0; idx < numBwt; ++idx)
        res.addBwt(CTxOut(bwtTotalAmount/numBwt, dummyScriptPubKey));

    if (inputTxBase)
    {
        res.vin.push_back(CTxIn(COutPoint(inputTxBase->GetHash(), 0), CScript(), -1));
        SignSignature(keystore, inputTxBase->GetVout()[0].scriptPubKey, res, 0);
    }
    else if (inputAmount > 0)
    {
        std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(inputAmount);
        StoreCoins(coinData);
    
        res.vin.push_back(CTxIn(COutPoint(coinData.first, 0), CScript(), -1));
        SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, res, 0);
    }

    // TODO: when the CCTP lib integration is ready, create the proof properly.
    res.scProof = CScProof{SAMPLE_CERT_DARLIN_PROOF};

    return res;
}

/**
 * @brief Adds a new sidechain and extends the blockchain to reach the chainActiveHeight.
 * 
 * @param scId The id of new sidechain
 * @param sidechain The parameters of the sidechain
 * @param chainActiveHeight The new height to be reaced by the active chain
 */
void BlockchainTestManager::StoreSidechainWithCurrentHeight(const uint256& scId, const CSidechain& sidechain, int chainActiveHeight)
{
    ExtendChainActiveToHeight(chainActiveHeight);
    viewCache->SetBestBlock(chainActive.Tip()->GetBlockHash());
    txCreationUtils::storeSidechain(viewCache->getSidechainMap(), scId, sidechain);
}

/**
 * @brief Gets the number of pending certificate proves waiting to be verified in the async proof verifier.
 * 
 * @return size_t The number of pending certificate proves waiting to be verified in the async proof verifier.
 */
size_t BlockchainTestManager::PendingAsyncCertProves()
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().PendingAsyncCertProves();
}

/**
 * @brief Gets the number of pending CSW proves waiting to be verified in the async proof verifier.
 * 
 * @return size_t The number of pending CSW proves waiting to be verified in the async proof verifier.
 */
size_t BlockchainTestManager::PendingAsyncCswProves()
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().PendingAsyncCswProves();
}

/**
 * @brief Gets the statistics of the async proof verifier.
 * 
 * @return AsyncProofVerifierStatistics The statistics of the async proof verifier.
 */
AsyncProofVerifierStatistics BlockchainTestManager::GetAsyncProofVerifierStatistics()
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().GetStatistics();
}

/**
 * @brief Gets the maximum delay [ms] between batch verifications performed by the async proof verifier.
 * 
 * @return uint32_t The maximum delay [ms] between batch verifications performed by the async proof verifier.
 */
uint32_t BlockchainTestManager::GetAsyncProofVerifierMaxBatchVerifyDelay()
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().GetMaxBatchVerifyDelay();
}

/**
 * @brief Resets the proof verifier.
 * 
 * This is particularly useful since the async proof verifier class is a singleton
 * and its state might need to be cleared when performing unit tests.
 */
void BlockchainTestManager::ResetAsyncProofVerifier()
{
    TEST_FRIEND_CScAsyncProofVerifier::GetInstance().Reset();
}

void BlockchainTestManager::InitCoinGeneration()
{
    coinsKey.MakeNewKey(true);
    keystore.AddKey(coinsKey);
    coinsScript << OP_DUP << OP_HASH160 << ToByteVector(coinsKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
}

std::pair<uint256, CCoinsCacheEntry> BlockchainTestManager::GenerateCoinsAmount(const CAmount & amountToGenerate)
{
    static unsigned int hashSeed = 1987;
    CCoinsCacheEntry entry;
    entry.flags = CCoinsCacheEntry::FRESH | CCoinsCacheEntry::DIRTY;

    entry.coins.fCoinBase = false;
    entry.coins.nVersion = TRANSPARENT_TX_VERSION;
    entry.coins.nHeight = SidechainFork().getHeight(CBaseChainParams::REGTEST);

    entry.coins.vout.resize(1);
    entry.coins.vout[0].nValue = amountToGenerate;
    entry.coins.vout[0].scriptPubKey = coinsScript;

    std::stringstream num;
    num << std::hex << ++hashSeed;

    return std::pair<uint256, CCoinsCacheEntry>(uint256S(num.str()), entry);
}

bool BlockchainTestManager::StoreCoins(std::pair<uint256, CCoinsCacheEntry> entryToStore)
{
    viewCache->WriteCoins(entryToStore.first, entryToStore.second);
    
    return viewCache->HaveCoins(entryToStore.first) == true;
}

} // namespace blockchain_test_utils