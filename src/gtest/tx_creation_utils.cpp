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
#include <zen/forks/fork8_sidechainfork.h>
#include <script/sign.h>
#include <boost/filesystem.hpp>
#include <sc/proofverifier.h>
#include "txdb.h"

CMutableTransaction txCreationUtils::populateTx(int txVersion, const CAmount & creationTxAmount, int epochLength,
                                                int sidechainVersion,
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
    mtx.vsc_ccout[0].version = sidechainVersion;
    mtx.vsc_ccout[0].nValue = creationTxAmount;
    mtx.vsc_ccout[0].address = uint256S("bebe111222dada");
    mtx.vsc_ccout[0].withdrawalEpochLength = epochLength;
    mtx.vsc_ccout[0].wCertVk   = CScVKey{SAMPLE_CERT_DARLIN_VK};
    mtx.vsc_ccout[0].wCeasedVk = CScVKey{SAMPLE_CSW_DARLIN_VK};
    mtx.vsc_ccout[0].vFieldElementCertificateFieldConfig.push_back(0x4);
    mtx.vsc_ccout[0].vFieldElementCertificateFieldConfig.push_back(0x7);
    mtx.vsc_ccout[0].vBitVectorCertificateFieldConfig.push_back({254*8, 33});
    mtx.vsc_ccout[0].vBitVectorCertificateFieldConfig.push_back({254*8*2, 55});
    mtx.vsc_ccout[0].customData.push_back(0x66);
    mtx.vsc_ccout[0].customData.push_back(0x77);
    mtx.vsc_ccout[0].customData.push_back(0xfe);
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

CTransaction txCreationUtils::createNewSidechainTxWith(const CAmount & creationTxAmount, int epochLength, int sidechainVersion)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, creationTxAmount, epochLength, sidechainVersion);

    mtx.resizeOut(0);
    mtx.vjoinsplit.resize(0);
    mtx.vft_ccout.resize(0);
    signTx(mtx);

    return CTransaction(mtx);
}

CTransaction txCreationUtils::createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount, int sidechainVersion)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, fwdTxAmount, 5, sidechainVersion);
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

void txCreationUtils::addNewScCreationToTx(CTransaction & tx, const CAmount & scAmount, int sidechainVersion)
{
    CMutableTransaction mtx = tx;

    mtx.nVersion = SC_TX_VERSION;

    CTxScCreationOut aSidechainCreationTx;
    aSidechainCreationTx.nValue = scAmount;
    aSidechainCreationTx.withdrawalEpochLength = 100;
    aSidechainCreationTx.version = sidechainVersion;
    mtx.vsc_ccout.push_back(aSidechainCreationTx);

    tx = mtx;
    return;
}

CScCertificate txCreationUtils::createCertificate(
    const uint256 & scId, int epochNum,
    const CFieldElement& endEpochCumScTxCommTreeRoot, CAmount changeTotalAmount, unsigned int numChangeOut,
    CAmount bwtTotalAmount, unsigned int numBwt, CAmount ftScFee, CAmount mbtrScFee, const int quality)
{
    CMutableScCertificate res;
    res.nVersion = SC_CERT_VERSION;
    res.scId = scId;
    res.epochNumber = epochNum;
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
 * @brief Generate a valid block for the specified height.
 * 
 * The block includes a coinbase transaction.
 * 
 * @param height The target height of the block
 * @return CBlock The generated block
 */
CBlock BlockchainTestManager::GenerateValidBlock(int height)
{
    CMutableTransaction mtx;

    // No inputs.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    // Set height
    mtx.vin[0].scriptSig = CScript() << height << OP_0;

    mtx.resizeOut(1);
    mtx.getOut(0).scriptPubKey = CScript() << OP_TRUE;
    mtx.getOut(0).nValue = 0;

    CAmount reward = GetBlockSubsidy(height, Params().GetConsensus());

    for (Fork::CommunityFundType cfType=Fork::CommunityFundType::FOUNDATION; cfType < Fork::CommunityFundType::ENDTYPE; cfType = Fork::CommunityFundType(cfType + 1)) {
        CAmount vCommunityFund = ForkManager::getInstance().getCommunityFundReward(height, reward, cfType);
        if (vCommunityFund > 0) {
            // Take some reward away from miners
            mtx.getOut(0).nValue -= vCommunityFund;
            // And give it to the community
            mtx.addOut(CTxOut(vCommunityFund, Params().GetCommunityFundScriptAtHeight(height, cfType)));
        }
    }
    
    CBlock block;
    block.vtx.push_back(mtx);

    return block;
}

/**
 * @brief BlockchainTestManager constructor.
 */
BlockchainTestManager::BlockchainTestManager()
{
    assert(Params().NetworkIDString() == "regtest");
    InitSidechainParameters();
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
 * @brief BlockchainTestManager desctructor.
 */
BlockchainTestManager::~BlockchainTestManager()
{
    if (!tempFolderPath.empty())
    {
        boost::system::error_code ec;
        boost::filesystem::remove_all(tempFolderPath.string(), ec);
    }
}

/**
 * @brief CoinsView getter.
 * 
 * @return std::shared_ptr<CInMemorySidechainDb> The in-memory CoinsView used by the manager.
 */
std::shared_ptr<CInMemorySidechainDb> BlockchainTestManager::CoinsView() const
{
    return view;
}

/**
 * @brief CoinsViewCache getter.
 * 
 * @return std::shared_ptr<CNakedCCoinsViewCache> The CoinsViewCache used by the manager.
 */
std::shared_ptr<CNakedCCoinsViewCache> BlockchainTestManager::CoinsViewCache() const
{
    return viewCache;
}

/**
 * @brief Temp folder path getter.
 * 
 * @return std::string The path used as temporary folder.
 */
std::string BlockchainTestManager::TempFolderPath() const
{
    return tempFolderPath.string();
}

/**
 * @brief Adds new blocks to the active chain to reach the targetHeight.
 * 
 * @param targetHeight The height to be reached by the active chain.
 */
void BlockchainTestManager::ExtendChainActiveToHeight(int targetHeight) const
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
void BlockchainTestManager::ExtendChainActiveWithBlock(const CBlock& block) const
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

    InitCoinGeneration();

    ResetAsyncProofVerifier();
}

/**
 * @brief Creates a CSW input object.
 * 
 * @param scId The ID of the sidechain the CSW refers to
 * @param nValue The amount of the CSW input
 * @param provingSystem The proving system for the verification of the CSW input proof
 * @return CTxCeasedSidechainWithdrawalInput the CSW input created.
 */
CTxCeasedSidechainWithdrawalInput BlockchainTestManager::CreateCswInput(uint256 scId, CAmount nValue, ProvingSystem provingSystem) const
{
    CTxCeasedSidechainWithdrawalInput input;

    input.scId = scId;
    input.nValue = nValue;
    input.actCertDataHash = CFieldElement{SAMPLE_FIELD};
    input.ceasingCumScTxCommTree = CFieldElement{SAMPLE_FIELD};
    input.nullifier = CFieldElement{SAMPLE_FIELD};
    input.pubKeyHash = uint160S("aaaa");

    CSidechain sidechain;
    assert(viewCache->GetSidechain(scId, sidechain));

    CCswProofVerifierInput verifierInput = CScProofVerifier::CswInputToVerifierItem(input, nullptr, sidechain.fixedParams, nullptr);
    input.scProof = GenerateTestCswProof(verifierInput, provingSystem);

    return input;
}

/**
 * @brief Creates a Sidechain Creation output.
 * 
 * @param sidechainVersion The version of the sidechain to be created
 * @return CTxScCreationOut The sidechain creation output created.
 */
CTxScCreationOut BlockchainTestManager::CreateScCreationOut(uint8_t sidechainVersion, ProvingSystem provingSystem) const
{
    CTxScCreationOut scCreationOut;
    scCreationOut.version = sidechainVersion;
    scCreationOut.withdrawalEpochLength = getScMinWithdrawalEpochLength();
    scCreationOut.nValue = CAmount(10);
    scCreationOut.forwardTransferScFee = 0;
    scCreationOut.mainchainBackwardTransferRequestScFee = 0;
    scCreationOut.wCertVk = GetTestVerificationKey(provingSystem, TestCircuitType::Certificate);
    return scCreationOut;
}

/**
 * @brief Creates a Sidechain Forward Transfer output.
 * 
 * @param scId The ID of the sidechain to which the coins must be transferred
 * @return CTxScForwardTransferOut The sidechain forward transfer output created.
 */
CTxForwardTransferOut BlockchainTestManager::CreateForwardTransferOut(uint256 scId) const
{
    CTxForwardTransferOut forwardTransferOut;
    forwardTransferOut.address = uint256S("aaaa");
    forwardTransferOut.scId = scId;
    forwardTransferOut.nValue = CAmount(1);
    forwardTransferOut.mcReturnAddress = uint160S("bbbb");
    return forwardTransferOut;
}

CBwtRequestOut BlockchainTestManager::CreateBackwardTransferRequestOut(uint256 scId) const
{
    CBwtRequestOut bwtRequestOut;
    bwtRequestOut.scId = scId;
    bwtRequestOut.mcDestinationAddress = uint160S("aaaa");
    bwtRequestOut.scFee = CAmount(1);
    bwtRequestOut.vScRequestData.push_back(CFieldElement{SAMPLE_FIELD});
    return bwtRequestOut;
}

/**
 * @brief Creates a mutable transaction based on the parameters passed as input.
 * 
 * @param args The parameters of the transaction
 * @return CMutableTransaction the generated transaction.
 */
CMutableTransaction BlockchainTestManager::CreateTransaction(const CTransactionCreationArguments& args) const
{
    CMutableTransaction tx;

    tx.nVersion = args.nVersion;

    tx.vcsw_ccin = args.vcsw_ccin;
    tx.vft_ccout = args.vft_ccout;
    tx.vmbtr_out = args.vmbtr_out;
    tx.vsc_ccout = args.vsc_ccout;

    if (args.fGenerateValidInput)
    {
        uint32_t totalInputAmount = 0;

        // Count the total amount of coins we need as input
        for (const auto& out : args.vft_ccout)
        {
            totalInputAmount += out.nValue;
        }

        for (const auto& out : args.vmbtr_out)
        {
            totalInputAmount += out.scFee;
        }

        for (const auto& out : args.vsc_ccout)
        {
            totalInputAmount += out.nValue;
        }


        std::pair<uint256, CCoinsCacheEntry> coinData = GenerateCoinsAmount(totalInputAmount);
        StoreCoins(coinData);

        tx.vin.resize(1);
        tx.vin[0].prevout = COutPoint(coinData.first, 0);
        assert(SignSignature(keystore, coinData.second.coins.vout[0].scriptPubKey, tx, 0) == true);
    }

    return tx;
}

/**
 * @brief Tries to send a transaction to memory pool.
 * 
 * @param state The validation state that will be returned as result of the operation
 * @param tx The transaction to be added to the mempool
 * @return The result of the operation
 */
MempoolReturnValue BlockchainTestManager::TestAcceptTxToMemoryPool(CValidationState &state, const CTransaction &tx) const
{
    CCoinsViewCache* saved_pcoinsTip = pcoinsTip;

    CTxMemPool pool(::minRelayTxFee);
    pcoinsTip = viewCache.get();
    pcoinsTip->SetBestBlock(chainActive.Tip()->GetBlockHash());
    pindexBestHeader = chainActive.Tip();

    CCoinsViewCache view(pcoinsTip);

    LOCK(cs_main);
    MempoolReturnValue val = AcceptTxToMemoryPool(pool, state, tx, LimitFreeFlag::OFF, RejectAbsurdFeeFlag::OFF, MempoolProofVerificationFlag::SYNC);

    pcoinsTip = saved_pcoinsTip;

    return val;
}

/**
 * @brief Generate a new sidechain certificate.
 * 
 * @param scId The ID of the sidechain the certificate refers to
 * @param epochNumber The epoch number of the certificate
 * @param quality The quality of the certificate
 * @param provingSystem The proving system to be used for the certificate generation
 * @param inputTxBase The pointer to the transaction to be used as input for the certificate
 * @return CScCertificate The generated certificate.
 */
CScCertificate BlockchainTestManager::GenerateCertificate(uint256 scId, int epochNumber, int64_t quality, ProvingSystem provingSystem, CTransactionBase* inputTxBase) const
{
    uint256 dummyBlockHash {};
    CFieldElement endEpochCumScTxCommTreeRoot = CFieldElement{SAMPLE_FIELD};
    CAmount inputAmount{20};
    CAmount dummyNonZeroFee {10};
    CAmount changeTotalAmount = inputAmount - dummyNonZeroFee;
    CAmount bwtTotalAmount {0};
    unsigned int numChangeOut = 1;
    unsigned int numBwt = 2;

    CMutableScCertificate res;
    res.nVersion = SC_CERT_VERSION;
    res.scId = scId;
    res.epochNumber = epochNumber;
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

    CSidechain sidechain;
    assert(viewCache->GetSidechain(scId, sidechain));

    CCertProofVerifierInput input = CScProofVerifier::CertificateToVerifierItem(res, sidechain.fixedParams, nullptr);
    res.scProof = GenerateTestCertificateProof(input, provingSystem);

    return res;
}

/**
 * @brief Generate the proof test parameters (proving and verification keys) for the specified
 * proving system and circuit type.
 * 
 * @param provingSystem The proving system whose parameters have to be created
 * @param circuitType The type of circuit whose parameters have to be created
 */
void BlockchainTestManager::GenerateSidechainTestParameters(ProvingSystem provingSystem, TestCircuitType circuitType) const
{
    CctpErrorCode errorCode;
    zendoo_generate_mc_test_params(
        circuitType, provingSystem, 1 << 10,
        (path_char_t*)tempFolderPath.string().c_str(), strlen(tempFolderPath.string().c_str()), &errorCode);
}

/**
 * @brief Generate a certificate proof based on the certificate parameters
 * and the proving key provided.
 * 
 * @param certificate The parameters of the certificate to be proved
 * @param provingSystem The proving system to use for the proof generation
 * @return CScProof The generated proof.
 */
CScProof BlockchainTestManager::GenerateTestCertificateProof(
    CCertProofVerifierInput certificate, ProvingSystem provingSystem, TestCircuitType circuitType) const
{
    wrappedFieldPtr sptrScId = CFieldElement(certificate.scId).GetFieldElement();
    field_t* scidFe = sptrScId.get();

    wrappedFieldPtr sptrConst = certificate.constant.GetFieldElement();
    wrappedFieldPtr sptrCum   = certificate.endEpochCumScTxCommTreeRoot.GetFieldElement();

    std::string certProofPath = GetTestFilePath(provingSystem, circuitType) + "proof";
    sc_pk_t* provingKey = GetTestProvingKey(provingSystem, circuitType);

    backward_transfer_t* btList = certificate.bt_list.data();

    // In case there are no custom fields, return a null pointer.
    // This is required by the CCTP Lib.
    if (certificate.bt_list.size() == 0)
    {
        btList = nullptr;
    }

    int custom_fields_len = certificate.vCustomFields.size(); 
    std::unique_ptr<const field_t*[]> custom_fields(new const field_t*[custom_fields_len]);
    int i = 0;
    std::vector<wrappedFieldPtr> vSptr;
    for (auto entry: certificate.vCustomFields)
    {
        wrappedFieldPtr sptrFe = entry.GetFieldElement();
        custom_fields[i] = sptrFe.get();
        vSptr.push_back(sptrFe);
        i++;
    }
    if (custom_fields_len == 0)
    {
        custom_fields.reset();
        assert(custom_fields.get() == nullptr);
    }

    CctpErrorCode errorCode;

    //TODO: Add custom fields
    zendoo_create_cert_test_proof(false /*zk*/,
                                  sptrConst.get(),
                                  scidFe,
                                  certificate.epochNumber,
                                  certificate.quality,
                                  btList,
                                  certificate.bt_list.size(),
                                  custom_fields.get(),
                                  custom_fields_len,
                                  sptrCum.get(),
                                  certificate.mainchainBackwardTransferRequestScFee,
                                  certificate.forwardTransferScFee,
                                  provingKey,
                                  (path_char_t*)certProofPath.c_str(),
                                  strlen(certProofPath.c_str()),
                                  1 << 10,
                                  &errorCode);

    zendoo_sc_pk_free(provingKey);

    return CScProof(ReadBytesFromFile(certProofPath));
}

/**
 * @brief Generate a certificate proof based on the certificate parameters
 * and the proving key provided.
 * 
 * @param csw The parameters of the CSW input to be proved
 * @param provingSystem The proving system to use for the proof generation
 * @return CScProof The generated proof.
 */
CScProof BlockchainTestManager::GenerateTestCswProof(CCswProofVerifierInput csw, ProvingSystem provingSystem, TestCircuitType circuitType) const
{
    wrappedFieldPtr sptrConst = csw.constant.GetFieldElement();
    wrappedFieldPtr sptrScId = CFieldElement(csw.scId).GetFieldElement();
    field_t* scidFe = sptrScId.get();
    field_t* constantFe = sptrConst.get();
     
    const uint160& cswPkHash = csw.pubKeyHash;
    BufferWithSize bwsCswPkHash(cswPkHash.begin(), cswPkHash.size());
    
    wrappedFieldPtr sptrCdh = csw.certDataHash.GetFieldElement();
    wrappedFieldPtr sptrCum = csw.ceasingCumScTxCommTree.GetFieldElement();
    wrappedFieldPtr sptrNullifier = csw.nullifier.GetFieldElement();

    std::string cswProofPath = GetTestFilePath(provingSystem, circuitType) + "proof";
    sc_pk_t* provingKey = GetTestProvingKey(provingSystem, circuitType);

    CctpErrorCode code;

    bool ret = zendoo_create_csw_test_proof(false, /*zk*/
                                            csw.nValue,
                                            constantFe,
                                            scidFe,
                                            sptrNullifier.get(), 
                                            &bwsCswPkHash,
                                            sptrCdh.get(),
                                            sptrCum.get(),
                                            provingKey,
                                            (path_char_t*)cswProofPath.c_str(),
                                            strlen(cswProofPath.c_str()),
                                            1 << 10,
                                            &code);

#if 0
    dumpFe(constantFe, "constantFe");
    dumpFe(scidFe, "scidFe");
    dumpFe(sptrNullifier.get(), "nullifierFe");
    dumpBuffer(&bwsCswPkHash, "bws_pk");
    dumpFe(sptrCdh.get(), "cdhFe");
    dumpFe(sptrCum.get(), "cumFe");
#endif

    zendoo_sc_pk_free(provingKey);

    return CScProof(ReadBytesFromFile(cswProofPath));
}

/**
 * @brief Get the test verification key 
 * 
 * @param provingSystem 
 * @param circuitType 
 * @return CScVKey 
 */
CScVKey BlockchainTestManager::GetTestVerificationKey(ProvingSystem provingSystem, TestCircuitType circuitType) const
{
    return CScVKey(ReadBytesFromFile(GetTestFilePath(provingSystem, circuitType) + "vk"));
}

/**
 * @brief Generate a sidechain object
 * 
 * @param scId The sidechain id
 * @return CSidechain The generated sidechain object
 */
CSidechain BlockchainTestManager::GenerateSidechain(uint256 scId, uint8_t version) const
{
    CSidechain sc;
    sc.fixedParams.version = version;
    sc.fixedParams.constant = CFieldElement{SAMPLE_FIELD};
    sc.fixedParams.wCertVk = GetTestVerificationKey(ProvingSystem::CoboundaryMarlin, TestCircuitType::CertificateNoConstant);
    sc.fixedParams.wCeasedVk = GetTestVerificationKey(ProvingSystem::CoboundaryMarlin, TestCircuitType::CSWNoConstant);
    return sc;
}

/**
 * @brief Adds a new sidechain and extends the blockchain to reach the chainActiveHeight.
 * 
 * @param scId The id of new sidechain
 * @param sidechain The parameters of the sidechain
 * @param chainActiveHeight The new height to be reaced by the active chain
 */
void BlockchainTestManager::StoreSidechainWithCurrentHeight(const uint256& scId, const CSidechain& sidechain, int chainActiveHeight) const
{
    ExtendChainActiveToHeight(chainActiveHeight);
    viewCache->SetBestBlock(chainActive.Tip()->GetBlockHash());
    txCreationUtils::storeSidechain(viewCache->getSidechainMap(), scId, sidechain);
}

/**
 * @brief Checks whether the certificate proof is correct or not.
 * 
 * @param certificate The certificate parameters
 * @return true If the certificate proof is correctly verified.
 * @return false If the certificate proof is not valid.
 */
bool BlockchainTestManager::VerifyCertificateProof(CCertProofVerifierInput certificate) const
{
    wrappedFieldPtr sptrScId = CFieldElement(certificate.scId).GetFieldElement();
    field_t* scidFe = sptrScId.get();

    wrappedFieldPtr   sptrConst  = certificate.constant.GetFieldElement();
    wrappedFieldPtr   sptrCum    = certificate.endEpochCumScTxCommTreeRoot.GetFieldElement();
    wrappedScProofPtr sptrProof  = certificate.proof.GetProofPtr();
    wrappedScVkeyPtr  sptrCertVk = certificate.verificationKey.GetVKeyPtr();

    int customFieldsLen = certificate.vCustomFields.size(); 

    std::unique_ptr<const field_t*[]> customFields(new const field_t*[customFieldsLen]);
    int i = 0;
    std::vector<wrappedFieldPtr> customFieldsWrapper;
    for (auto entry: certificate.vCustomFields)
    {
        wrappedFieldPtr sptr = entry.GetFieldElement();
        customFields[i] = sptr.get();
        customFieldsWrapper.push_back(sptr);
        i++;
    }

    // In case there are no custom fields, return a null pointer.
    // This is required by the CCTP Lib.
    if (customFieldsLen == 0)
    {
        customFields.reset();
    }

    backward_transfer_t* btList = certificate.bt_list.data();

    // In case there are no custom fields, return a null pointer.
    // This is required by the CCTP Lib.
    if (certificate.bt_list.size() == 0)
    {
        btList = nullptr;
    }

    CctpErrorCode errorCode;

    return zendoo_verify_certificate_proof(sptrConst.get(),
                                           scidFe,
                                           certificate.epochNumber,
                                           certificate.quality,
                                           btList,
                                           certificate.bt_list.size(),
                                           customFields.get(),
                                           customFieldsLen,
                                           sptrCum.get(),
                                           certificate.mainchainBackwardTransferRequestScFee,
                                           certificate.forwardTransferScFee,
                                           sptrProof.get(),
                                           sptrCertVk.get(),
                                           &errorCode);
}

/**
 * @brief Checks whether the CSW input proof is correct or not.
 * 
 * @param csw The CSW input parameters
 * @return true If the CSW input proof is correctly verified.
 * @return false If the CSW input proof is not valid.
 */
bool BlockchainTestManager::VerifyCswProof(CCswProofVerifierInput csw) const
{
    wrappedFieldPtr sptrConst = csw.constant.GetFieldElement();
    wrappedFieldPtr sptrScId = CFieldElement(csw.scId).GetFieldElement();
    field_t* scidFe = sptrScId.get();
     
    const uint160& cswPkHash = csw.pubKeyHash;
    BufferWithSize bwsCswPkHash(cswPkHash.begin(), cswPkHash.size());
    
    wrappedFieldPtr   sptrCdh        = csw.certDataHash.GetFieldElement();
    wrappedFieldPtr   sptrCum        = csw.ceasingCumScTxCommTree.GetFieldElement();
    wrappedFieldPtr   sptrNullifier  = csw.nullifier.GetFieldElement();
    wrappedScProofPtr sptrProof      = csw.proof.GetProofPtr();
    wrappedScVkeyPtr  sptrCeasedVk   = csw.verificationKey.GetVKeyPtr();

    CctpErrorCode code;

    return zendoo_verify_csw_proof(csw.nValue,
                                   sptrConst.get(),
                                   scidFe,
                                   sptrNullifier.get(),
                                   &bwsCswPkHash,
                                   sptrCdh.get(),
                                   sptrCum.get(),
                                   sptrProof.get(),
                                   sptrCeasedVk.get(),
                                   &code);
}

/**
 * @brief Gets the number of pending certificate proofs waiting to be verified in the async proof verifier.
 * 
 * @return size_t The number of pending certificate proofs waiting to be verified in the async proof verifier.
 */
size_t BlockchainTestManager::PendingAsyncCertProofs() const
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().PendingAsyncCertProofs();
}

/**
 * @brief Gets the number of pending CSW proofs waiting to be verified in the async proof verifier.
 * 
 * @return size_t The number of pending CSW proofs waiting to be verified in the async proof verifier.
 */
size_t BlockchainTestManager::PendingAsyncCswProofs() const
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().PendingAsyncCswProofs();
}

/**
 * @brief Gets the statistics of the async proof verifier.
 * 
 * @return AsyncProofVerifierStatistics The statistics of the async proof verifier.
 */
AsyncProofVerifierStatistics BlockchainTestManager::GetAsyncProofVerifierStatistics() const
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().GetStatistics();
}

/**
 * @brief Gets the maximum delay [ms] between batch verifications performed by the async proof verifier.
 * 
 * @return uint32_t The maximum delay [ms] between batch verifications performed by the async proof verifier.
 */
uint32_t BlockchainTestManager::GetAsyncProofVerifierMaxBatchVerifyDelay() const
{
    return TEST_FRIEND_CScAsyncProofVerifier::GetInstance().GetMaxBatchVerifyDelay();
}

/**
 * @brief Resets the proof verifier.
 * 
 * This is particularly useful since the async proof verifier class is a singleton
 * and its state might need to be cleared when performing unit tests.
 */
void BlockchainTestManager::ResetAsyncProofVerifier()const
{
    TEST_FRIEND_CScAsyncProofVerifier::GetInstance().Reset();
}

/**
 * @brief Gets the path of the test file for a specific proving system and circuit.
 * 
 * Note that this is a generic function, returning a generic path to several files;
 * to be used it is required to append a suffix specifying the file to use.
 * 
 * For instance, this function may return "/tmp/b2f9-3080-b5d4-0b68/cob_marlin_cert_test_"
 * but the real files may be:
 * 
 *  - /tmp/b2f9-3080-b5d4-0b68/cob_marlin_cert_test_vk
 *  - /tmp/b2f9-3080-b5d4-0b68/cob_marlin_cert_test_pk
 *  - /tmp/b2f9-3080-b5d4-0b68/cob_marlin_cert_test_proof
 * 
 * @param provingSystem The proving system of interest
 * @param circuitType The type of entity of interest
 * @return std::string The path to the file.
 */
std::string BlockchainTestManager::GetTestFilePath(ProvingSystem provingSystem, TestCircuitType circuitType) const
{
    std::string filename;

    switch (provingSystem)
    {
        case ProvingSystem::CoboundaryMarlin:
            filename = "/cob_marlin_";
            break;
        case ProvingSystem::Darlin:
            filename = "/darlin_";
            break;
        default:
            assert(false);
    }

    switch (circuitType)
    {
        case TestCircuitType::Certificate:
            filename += "cert_test_";
            break;
        case TestCircuitType::CSW:
            filename += "csw_test_";
            break;
        case TestCircuitType::CertificateNoConstant:
            filename += "cert_no_const_test_";
            break;
        case TestCircuitType::CSWNoConstant:
            filename += "csw_no_const_test_";
            break;
    }

    return tempFolderPath.string() + filename;
}

/**
 * @brief Gets the proving key that can be used to verify a test proof.
 * 
 * @param provingSystem The proving system used to generate the proof
 * @param circuitType The type of proof to be verified (e.g. Certificate, CSW)
 * @return std::unique_ptr<sc_pk_t*> The pointer to the proving key.
 */
sc_pk_t* BlockchainTestManager::GetTestProvingKey(ProvingSystem provingSystem, TestCircuitType circuitType) const
{
    std::string provingKeyPath = GetTestFilePath(provingSystem, circuitType) + "pk";

    CctpErrorCode errorCode;

    return zendoo_deserialize_sc_pk_from_file(
        (path_char_t*)provingKeyPath.c_str(),
        provingKeyPath.length(),
        true, /*semantic_checks*/
        &errorCode
    );
}

void BlockchainTestManager::InitCoinGeneration()
{
    coinsKey.MakeNewKey(true);
    keystore.AddKey(coinsKey);
    coinsScript << OP_DUP << OP_HASH160 << ToByteVector(coinsKey.GetPubKey().GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
}

/**
 * @brief Initializes the sidechain related parameters, like the DLog keys, the verification key
 * and the proving key.
 */
void BlockchainTestManager::InitSidechainParameters()
{
    // Create a new temporary folder
    tempFolderPath = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(tempFolderPath);

    CctpErrorCode errorCode;
    zendoo_init_dlog_keys(Sidechain::SEGMENT_SIZE, &errorCode);
}

std::pair<uint256, CCoinsCacheEntry> BlockchainTestManager::GenerateCoinsAmount(const CAmount & amountToGenerate) const
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

/**
 * @brief Reads all bytes from a file.
 * 
 * @param filepath The path of the file to be read
 * @return std::vector<unsigned char> The read bytes.
 */
std::vector<unsigned char> BlockchainTestManager::ReadBytesFromFile(std::string filepath) const
{
    std::ifstream input(filepath, std::ios::binary);

    std::vector<unsigned char> bytes(
         (std::istreambuf_iterator<char>(input)),
         (std::istreambuf_iterator<char>()));

    input.close();

    return bytes;
}

bool BlockchainTestManager::StoreCoins(std::pair<uint256, CCoinsCacheEntry> entryToStore) const
{
    viewCache->WriteCoins(entryToStore.first, entryToStore.second);
    
    return viewCache->HaveCoins(entryToStore.first) == true;
}

void RandomSidechainField(CFieldElement &fe) {
    std::vector<unsigned char> vec;
    for (unsigned int i = 0; i < sizeof(CFieldElement)-1; i++)
    {
        vec.push_back((unsigned char)(insecure_rand() % 0xff));
    }
    vec.resize(CFieldElement::ByteSize());
    fe.SetByteArray(vec);
}


} // namespace blockchain_test_utils
