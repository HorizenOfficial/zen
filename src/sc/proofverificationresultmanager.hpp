#ifndef _SC_PROOF_VERIFICATION_MANAGER_H
#define _SC_PROOF_VERIFICATION_MANAGER_H

#include "limitedmap.h"
#include "sc/sidechaintypes.h"
#include "proofverifier.h"

constexpr int RESULTS_LIMIT = 100;

/**
 * @brief Helper class for managing proofs verifications results
 * 
 */
class CScProofVerificationResultManager
{
public:

    CCriticalSection cs_proofsVerificationsResults;

    LimitedMap<uint256, std::pair<uint64_t, ProofVerificationResult>> mostRecentProofsVerificationsResults;

    static CScProofVerificationResultManager& GetInstance()
    {
        static CScProofVerificationResultManager instance;
        
        return instance;
    }

private:
    CScProofVerificationResultManager() : mostRecentProofsVerificationsResults(RESULTS_LIMIT)
    {
    }
};

#endif // _SC_PROOF_VERIFICATION_MANAGER_H
