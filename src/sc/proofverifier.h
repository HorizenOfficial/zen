#ifndef SC_PROOF_VERIFIER_H
#define SC_PROOF_VERIFIER_H

namespace libzendoomc{
    static constexpr size_t GROTH_PROOF_SIZE = (
        193 +  // π_A
        193 + // π_B
        385);  // π_C

    typedef std::vector<unsigned char> ScProof;
    typedef std::vector<unsigned char> ScVk;
}

#endif // SC_PROOF_VERIFIER_H
