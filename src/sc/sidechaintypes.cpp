#include "sc/sidechaintypes.h"
#include "sc/proofverifier.h"
#include "util.h"
#include "utilstrencodings.h"
#include <stdexcept>

CPoseidonHash::CPoseidonHash()
{
    SetNull();
}

//UPON INTEGRATION OF POSEIDON HASH STUFF, THIS MUST DISAPPER
CPoseidonHash::CPoseidonHash(const uint256& sha256)
    : innerHash(sha256)
{
    innerFieldElement.SetHex(innerHash.GetHex());
}

CPoseidonHash::CPoseidonHash(const libzendoomc::ScFieldElement& fe)
    : innerHash(), innerFieldElement(fe)
{
}

void CPoseidonHash::SetNull()
{
    innerHash.SetNull();
    innerFieldElement.SetNull();
}

CPoseidonHash CPoseidonHash::ComputeHash(const CPoseidonHash& a, const CPoseidonHash& b) 
{
    uint256 hash = Hash(BEGIN(a.innerHash), END(a.innerHash), BEGIN(b.innerHash), END(b.innerHash));
    return CPoseidonHash(hash);
}

#ifdef BITCOIN_TX
libzendoomc::ScFieldElement CPoseidonHash::ComputeHash(const libzendoomc::ScFieldElement& a, const libzendoomc::ScFieldElement& b) 
{
    return libzendoomc::ScFieldElement();
}
#else
libzendoomc::ScFieldElement CPoseidonHash::ComputeHash(const libzendoomc::ScFieldElement& a, const libzendoomc::ScFieldElement& b) 
{
    libzendoomc::ScFieldElement res;
    if (!libzendoomc::CalculateHash(a, b, res))
    {
        LogPrintf("%s():%d - failed to compute poseidon hash from: %s / %s \n", __func__, __LINE__,
            a.ToString(), b.ToString());
        throw std::runtime_error("Could not compute poseidon hash");
    }
    return res;
}
#endif
