#include "sc/sidechaintypes.h"
#include "util.h"
#include "utilstrencodings.h"

CPoseidonHash::CPoseidonHash()
{
    SetNull();
}

CPoseidonHash::CPoseidonHash(const uint256& sha256)
    : innerHash(sha256) {} //UPON INTEGRATION OF POSEIDON HASH STUFF, THIS MUST DISAPPER

void CPoseidonHash::SetNull()
{
    innerHash.SetNull();
}

CPoseidonHash CPoseidonHash::ComputeHash(const CPoseidonHash& a, const CPoseidonHash& b) 
{
    uint256 hash = Hash(BEGIN(a.innerHash), END(a.innerHash), BEGIN(b.innerHash), END(b.innerHash));
    return CPoseidonHash(hash);
}

