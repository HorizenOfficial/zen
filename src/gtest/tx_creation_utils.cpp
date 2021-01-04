#include <script/interpreter.h>
#include <main.h>
#include <pubkey.h>
#include "tx_creation_utils.h"
#include <miner.h>
#include <undo.h>

CMutableTransaction txCreationUtils::populateTx(int txVersion, const CAmount & creationTxAmount, const CAmount & fwdTxAmount, int epochLength)
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

CTransaction txCreationUtils::createNewSidechainTxWith(const CAmount & creationTxAmount, int epochLength)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, creationTxAmount, CAmount(0), epochLength);
    mtx.resizeOut(0);
    mtx.vjoinsplit.resize(0);
    mtx.vft_ccout.resize(0);
    signTx(mtx);

    return CTransaction(mtx);
}

CTransaction txCreationUtils::createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount)
{
    CMutableTransaction mtx = populateTx(SC_TX_VERSION, CAmount(0), fwdTxAmount);
    mtx.resizeOut(0);
    mtx.vjoinsplit.resize(0);
    mtx.vsc_ccout.resize(0);

    mtx.vft_ccout.resize(1);
    mtx.vft_ccout[0].scId = newScId;
    mtx.vft_ccout[0].nValue = fwdTxAmount;

    signTx(mtx);

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

CScCertificate txCreationUtils::createCertificate(const uint256 & scId, int epochNum, const uint256 & endEpochBlockHash,
                                                  CAmount changeTotalAmount, unsigned int numChangeOut,
                                                  CAmount bwtTotalAmount, unsigned int numBwt) {
    CMutableScCertificate res;
    res.nVersion = SC_CERT_VERSION;
    res.scId = scId;
    res.epochNumber = epochNum;
    res.endEpochBlockHash = endEpochBlockHash;
    res.quality = 3; //setup to non zero value

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
    CAmount dummyFeeAmount{0};
    CScript dummyCoinbaseScript = CScript() << OP_DUP << OP_HASH160
            << ToByteVector(uint160()) << OP_EQUALVERIFY << OP_CHECKSIG;

    CTransaction inputTx = createCoinbase(dummyCoinbaseScript, dummyFeeAmount, coinHeight);
    CTxUndo dummyUndo;
    UpdateCoins(inputTx, targetView, dummyUndo, coinHeight);
    assert(targetView.HaveCoins(inputTx.GetHash()));
    return inputTx.GetHash();
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

        BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(currBlockHash, pNewBlockIdx)).first;
        pNewBlockIdx->phashBlock = &(mi->first);
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
