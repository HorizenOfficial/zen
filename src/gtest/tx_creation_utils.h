#ifndef TX_CREATION_UTILS_H
#define TX_CREATION_UTILS_H

#include <boost/thread.hpp>

#include "amount.h"
#include "chainparams.h"
#include "coins.h"
#include "keystore.h"
#include "sc/asyncproofverifier.h"
#include "undo.h"

class CTransaction;
class CMutableTransaction;
class CScCertificate;
class CMutableScCertificate;
class uint256;
class CBlock;

namespace txCreationUtils
{
class CNakedCCoinsViewCache : public CCoinsViewCache
{
public:
    CNakedCCoinsViewCache(CCoinsView* pWrappedView) : CCoinsViewCache(pWrappedView)
    {
        uint256 dummyAnchor = uint256S("59d2cde5e65c1414c32ba54f0fe4bdb3d67618125286e6a191317917c812c6d7"); //anchor for empty block!?
        this->hashAnchor = dummyAnchor;

        CAnchorsCacheEntry dummyAnchorsEntry;
        dummyAnchorsEntry.entered = true;
        dummyAnchorsEntry.flags = CAnchorsCacheEntry::DIRTY;
        this->cacheAnchors[dummyAnchor] = dummyAnchorsEntry;
    };
    CSidechainsMap& getSidechainMap() { return this->cacheSidechains; };
    CSidechainEventsMap& getScEventsMap() { return this->cacheSidechainEvents; };
};

CMutableTransaction populateTx(int txVersion,
                               const CAmount& creationTxAmount = CAmount(0),
                               int epochLength = 5,
                               int sidechainVersion = 0,
                               const CAmount& ftScFee = CAmount(0),
                               const CAmount& mbtrScFee = CAmount(0),
                               int mbtrDataLength = 0);
void signTx(CMutableTransaction& mtx);
void signTx(CMutableScCertificate& mcert);

CTransaction createNewSidechainTxWith(const CAmount & creationTxAmount, int epochLength = 15, int sidechainVersion = 0);
CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount, int sidechainVersion = 0);
CTxCeasedSidechainWithdrawalInput CreateCSWInput(
    const uint256& scId, const std::string& nullifierHex, const std::string& actCertDataHex,
    const std::string& ceasingCumScTxCommTreeHex, CAmount amount);
CTransaction createCSWTxWith(const CTxCeasedSidechainWithdrawalInput& csw);

CTransaction createCoinBase(const CAmount& amount);
CTransaction createTransparentTx(bool ccIsNull = true); //ccIsNull = false allows generation of faulty tx with non-empty cross chain output
CTransaction createSproutTx(bool ccIsNull = true);      //ccIsNull = false allows generation of faulty tx with non-empty cross chain output

void addNewScCreationToTx(CTransaction& tx, const CAmount& scAmount, int sidechainVersion = 0);

CScCertificate createCertificate(const uint256 & scId, int epochNum,
                                 const CFieldElement& endEpochCumScTxCommTreeRoot, CAmount changeTotalAmount/* = 0*/, unsigned int numChangeOut/* = 0*/,
                                 CAmount bwtTotalAmount/* = 1*/, unsigned int numBwt/* = 1*/, CAmount ftScFee/* = 0*/, CAmount mbtrScFee/* = 0*/, const int quality = 3);

uint256 CreateSpendableCoinAtHeight(CCoinsViewCache& targetView, unsigned int coinHeight);

void storeSidechain(CSidechainsMap& mapToWriteInto, const uint256& scId, const CSidechain& sidechain);
void storeSidechainEvent(CSidechainEventsMap& mapToWriteInto, int eventHeight, const CSidechainEvents& scEvent);
} // namespace txCreationUtils

namespace chainSettingUtils
{
void ExtendChainActiveToHeight(int targetHeight);
void ExtendChainActiveWithBlock(const CBlock& block);
} // namespace chainSettingUtils

namespace blockchain_test_utils
{

using namespace txCreationUtils;

/**
 * @brief A struct containing all the parameters needed to create a Transcation object. 
 */
struct CTransactionCreationArguments
{
    bool fGenerateValidInput = false;                             /**< Whether to include a valid input in the transaction (and a UTXO for it) */
    int32_t nVersion;                                             /**< The version of the transaction. */

    std::vector<CTxCeasedSidechainWithdrawalInput> vcsw_ccin;     /**< The list of CSW inputs */
    std::vector<CTxScCreationOut>                  vsc_ccout;     /**< The list of sidechain creation outputs */
    std::vector<CTxForwardTransferOut>             vft_ccout;     /**< The list of sidechain forward transfer outputs */
    std::vector<CBwtRequestOut>                    vmbtr_out;     /**< The list of sidechain backward transfer request outputs */
};

class CBlockUndo_OldVersion
{
    public:
        std::vector<CTxUndo> vtxundo;
        uint256 old_tree_root;

        ADD_SERIALIZE_METHODS;

        template <typename Stream, typename Operation>
        inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
            READWRITE(vtxundo);
            READWRITE(old_tree_root);
        }   
};

class CInMemorySidechainDb final: public CCoinsView {
public:
    CInMemorySidechainDb()  = default;
    virtual ~CInMemorySidechainDb() = default;

    bool HaveSidechain(const uint256& scId) const override {
        return sidechainsInMemoryMap.count(scId) && sidechainsInMemoryMap.at(scId).flag != CSidechainsCacheEntry::Flags::ERASED;
    }
    bool GetSidechain(const uint256& scId, CSidechain& info) const override {
        if(!HaveSidechain(scId))
            return false;
        info = sidechainsInMemoryMap.at(scId).sidechain;
        return true;
    }

    bool HaveSidechainEvents(int height)  const override {
        return eventsInMemoryMap.count(height) && eventsInMemoryMap.at(height).flag != CSidechainEventsCacheEntry::Flags::ERASED;
    }
    bool GetSidechainEvents(int height, CSidechainEvents& scEvents) const override {
        if(!HaveSidechainEvents(height))
            return false;
        scEvents = eventsInMemoryMap.at(height).scEvents;
        return true;
    }

    virtual void GetScIds(std::set<uint256>& scIdsList) const override {
        for (auto& entry : sidechainsInMemoryMap)
            scIdsList.insert(entry.first);
        return;
    }

    bool BatchWrite(CCoinsMap &mapCoins, const uint256 &hashBlock,
                    const uint256 &hashAnchor, CAnchorsMap &mapAnchors,
                    CNullifiersMap &mapNullifiers, CSidechainsMap& sidechainMap,
                    CSidechainEventsMap& mapSidechainEvents, CCswNullifiersMap& cswNullifiers) override
    {
        for (auto& entryToWrite : sidechainMap)
            WriteMutableEntry(entryToWrite.first, entryToWrite.second, sidechainsInMemoryMap);

        for (auto& entryToWrite : mapSidechainEvents)
            WriteMutableEntry(entryToWrite.first, entryToWrite.second, eventsInMemoryMap);

        sidechainMap.clear();
        mapSidechainEvents.clear();
        return true;
    }

private:
    mutable boost::unordered_map<uint256, CSidechainsCacheEntry, CCoinsKeyHasher> sidechainsInMemoryMap;
    mutable boost::unordered_map<int, CSidechainEventsCacheEntry> eventsInMemoryMap;
};

/**
 * @brief An helper class with utility functions to be used for managing the blockchain
 * (for instance to create a sidechain, to generate blocks, etc.).
 * 
 */
class BlockchainTestManager
{
public:
    static BlockchainTestManager& GetInstance()
    {
        assert(Params().NetworkIDString() == "regtest");

        static BlockchainTestManager instance;
        return instance;
    }

    // BLOCK HELPERS
    static CBlock GenerateValidBlock(int height);

    BlockchainTestManager(const BlockchainTestManager&) = delete;
    BlockchainTestManager& operator=(const BlockchainTestManager&) = delete;

    // GETTERS
    std::shared_ptr<CInMemorySidechainDb> CoinsView() const;
    std::shared_ptr<CNakedCCoinsViewCache> CoinsViewCache() const;
    std::string TempFolderPath() const;

    // BLOCKCHAIN HELPERS
    void ExtendChainActiveToHeight(int targetHeight) const;
    void ExtendChainActiveWithBlock(const CBlock& block) const;
    void Reset();

    // TRANSACTION HELPERS
    CTxCeasedSidechainWithdrawalInput CreateCswInput(uint256 scId, CAmount nValue, ProvingSystem provingSystem) const;
    CTxScCreationOut CreateScCreationOut(uint8_t sidechainVersion, ProvingSystem provingSystem) const;
    CTxForwardTransferOut CreateForwardTransferOut(uint256 scId) const;
    CBwtRequestOut CreateBackwardTransferRequestOut(uint256 scId) const;
    CMutableTransaction CreateTransaction(const CTransactionCreationArguments& args) const;

    // MEMPOOL HELPERS
    MempoolReturnValue TestAcceptTxToMemoryPool(CValidationState &state, const CTransaction &tx) const;

    // SIDECHAIN HELPERS
    CScCertificate GenerateCertificate(uint256 scId, int epochNumber, int64_t quality, ProvingSystem provingSystem, CTransactionBase* inputTxBase = nullptr) const;
    void GenerateSidechainTestParameters(ProvingSystem provingSystem, TestCircuitType circuitType) const;
    CScProof GenerateTestCertificateProof(CCertProofVerifierInput certificate, ProvingSystem provingSystem, TestCircuitType circuitType = TestCircuitType::Certificate) const;
    CScProof GenerateTestCswProof(CCswProofVerifierInput csw, ProvingSystem provingSystem, TestCircuitType circuitType = TestCircuitType::CSW) const;
    CScVKey GetTestVerificationKey(ProvingSystem provingSystem, TestCircuitType circuitType) const;
    CSidechain GenerateSidechain(uint256 scId, uint8_t version) const;
    void StoreSidechainWithCurrentHeight(const uint256& scId, const CSidechain& sidechain, int chainActiveHeight) const;
    bool VerifyCertificateProof(CCertProofVerifierInput certificate) const;
    bool VerifyCswProof(CCswProofVerifierInput csw) const;

    // ASYNC PROOF VERIFIER HELPERS
    size_t PendingAsyncCertProofs() const;
    size_t PendingAsyncCswProofs() const;
    AsyncProofVerifierStatistics GetAsyncProofVerifierStatistics() const;
    uint32_t GetAsyncProofVerifierMaxBatchVerifyDelay() const;
    void ResetAsyncProofVerifier() const;

private:
    BlockchainTestManager();
    ~BlockchainTestManager();

    std::string GetTestFilePath(ProvingSystem provingSystem, TestCircuitType circuitType) const;
    sc_pk_t* GetTestProvingKey(ProvingSystem provingSystem, TestCircuitType circuitType) const;
    void InitCoinGeneration();
    void InitSidechainParameters();
    std::pair<uint256, CCoinsCacheEntry> GenerateCoinsAmount(const CAmount & amountToGenerate) const;
    std::vector<unsigned char> ReadBytesFromFile(std::string filepath) const;
    bool StoreCoins(std::pair<uint256, CCoinsCacheEntry> entryToStore) const;

    boost::thread_group threadGroup;
    std::shared_ptr<CInMemorySidechainDb> view;
    std::shared_ptr<CNakedCCoinsViewCache> viewCache;

    CKey                     coinsKey;
    CBasicKeyStore           keystore;
    CScript                  coinsScript;

    boost::filesystem::path tempFolderPath;

};

void RandomSidechainField(CFieldElement &fe);

} // namespace blockchain_test_utils

#endif
