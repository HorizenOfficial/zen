#ifndef SIDECHAIN_TX_COMMITMENT_GUARD
#define SIDECHAIN_TX_COMMITMENT_GUARD

#include "coins.h"
#include <sc/sidechaintypes.h>

class uint256;
class CTransaction;

class CScCertificate;
class CTxForwardTransferOut;
class CBwtRequestOut;
class CTxCeasedSidechainWithdrawalInput;

class SidechainTxsCommitmentGuard
{
public:
    SidechainTxsCommitmentGuard() {};
    ~SidechainTxsCommitmentGuard() {};

    SidechainTxsCommitmentGuard(const SidechainTxsCommitmentGuard&) = delete;
    SidechainTxsCommitmentGuard& operator=(const SidechainTxsCommitmentGuard&) = delete;

    bool add(const CTransaction& tx, bool autoRewind = false);
    bool add(const CScCertificate& cert);

    void rewind(const CTransaction& tx);
    void rewind(const CScCertificate& cert);

private:
    bool add_fwt(const CTxForwardTransferOut& ccout);
    bool add_bwtr(const CBwtRequestOut& ccout);
    bool add_csw(const CTxCeasedSidechainWithdrawalInput& ccin);
    bool add_cert(const CScCertificate& cert);

    void rewind(const CTransaction& tx, const int failingFt, const int failingBwtr, const int failingCsw);
    void keepMapsClean();

    // Keeping information separated to closely mimic CCTPlib structures
    struct CommitmentBuilderStatsAliveCounter {
        uint32_t   ft = 0;
        uint32_t bwtr = 0;
        uint32_t cert = 0;
        uint32_t  bwt = 0;
    };
    struct CommitmentBuilderStatsCeasedCounter {
        uint32_t  csw = 0;
    };
    struct CommitmentBuilderStats {
        std::map<uint256, CommitmentBuilderStatsAliveCounter>  cbsaMap;
        std::map<uint256, CommitmentBuilderStatsCeasedCounter> cbscMap;
        // The following values MUST be aligned with those specified in CCTPlib!
        static const int   SC_LIMIT = 4096;
        static const int   FT_LIMIT = 4095;
        static const int BWTR_LIMIT = 4095;
        static const int CERT_LIMIT = 4095;
        static const int  CSW_LIMIT = 4095;
        static const int  BWT_LIMIT = 4096;

        bool checkAvailableSpaceAliveSC(const uint256& scid) {
            if ((cbsaMap.count(scid) == 0) && ((cbsaMap.size() + cbscMap.size()) >= SC_LIMIT)) {
                LogPrint("sc", "%s():%d - scTxsCommitment building failed: too many sidechains, when adding scId[%s].\n",
                    __func__, __LINE__, scid.ToString());
                return false;
            }
            return true;
        }
        bool checkAvailableSpaceCeasedSC(const uint256& scid) {
            if ((cbscMap.count(scid) == 0) && ((cbsaMap.size() + cbscMap.size()) >= SC_LIMIT)) {
                LogPrint("sc", "%s():%d - scTxsCommitment building failed: too many sidechains, when adding scId[%s].\n",
                    __func__, __LINE__, scid.ToString());
                return false;
            }
            return true;
        }
        bool checkExistenceInAliveSCTree(const uint256& scid) {
            if (cbsaMap.count(scid) > 0) {
                return true;
            }
            return false;
        }
        bool checkExistenceInCeasedSCTree(const uint256& scid) {
            if (cbscMap.count(scid) > 0) {
                return true;
            }
            return false;
        }
    } cbs;

public:
    const CommitmentBuilderStats& getCBS() { return cbs; };
};

#endif
