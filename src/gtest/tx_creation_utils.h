#ifndef TX_CREATION_UTILS_H
#define TX_CREATION_UTILS_H

#include <primitives/transaction.h>
#include <primitives/certificate.h>

namespace txCreationUtils {
CMutableTransaction populateTx(int txVersion, const uint256 & newScId = uint256S("0"), const CAmount & creationTxAmount = CAmount(0), const CAmount & fwdTxAmount = CAmount(0));
void signTx(CMutableTransaction& mtx);

CTransaction createNewSidechainTxWith(const uint256 & newScId, const CAmount & creationTxAmount);
CTransaction createFwdTransferTxWith(const uint256 & newScId, const CAmount & fwdTxAmount);

CTransaction createNewSidechainTxWithNoFwdTransfer(const uint256 & newScId);
CTransaction createTransparentTx(bool ccIsNull = true); //ccIsNull = false allows generation of faulty tx with non-empty cross chain output
CTransaction createSproutTx(bool ccIsNull = true); //ccIsNull = false allows generation of faulty tx with non-empty cross chain output

void extendTransaction(CTransaction & tx, const uint256 & scId, const CAmount & amount);

CScCertificate createCertificate(const uint256 & scId, int epochNum, const uint256 & endEpochBlockHash, const CAmount& totalAmount);
};

namespace chainSettingUtils {
    void GenerateChainActive(int targetHeight);
};

#endif
