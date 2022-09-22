// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CORE_IO_H
#define BITCOIN_CORE_IO_H

#include "primitives/certificate.h"
#include "primitives/transaction.h"
#include <memory>
#include <string>
#include <vector>

class CBlock;
class CScript;
class uint256;
class UniValue;

// core_read.cpp
extern CScript ParseScript(const std::string& s);
extern bool DecodeHexTx(CTransaction& tx, const std::string& strHexTx);
extern bool DecodeHexCert(CScCertificate& cert, const std::string& strHexCert);
extern bool DecodeHex(std::unique_ptr<CTransactionBase>& pTxBase, const std::string& strHex);
extern bool DecodeHexBlk(CBlock&, const std::string& strHexBlk);
extern uint256 ParseHashUV(const UniValue& v, const std::string& strName);
extern uint256 ParseHashStr(const std::string&, const std::string& strName);
extern std::vector<unsigned char> ParseHexUV(const UniValue& v, const std::string& strName);

// core_write.cpp
extern std::string FormatScript(const CScript& script);
extern std::string EncodeHexTx(const CTransaction& tx);
extern std::string EncodeHexCert(const CScCertificate& cert);
extern std::string EncodeHex(const std::unique_ptr<CTransactionBase>& pTxBase);
extern void ScriptPubKeyToUniv(const CScript& scriptPubKey,
                               UniValue& out,
                               bool fIncludeHex);
extern void TxToUniv(const CTransaction& tx, const uint256& hashBlock, UniValue& entry);

template <typename Stream>
void makeSerializedTxObj(Stream& is, int objVer, std::unique_ptr<CTransactionBase>& pTxBase, int nType, int nVersion)
{
    if (CTransactionBase::IsTransaction(objVer)) {
        CTransaction tx(objVer);
        tx.SerializationOpInternal(is, CSerActionUnserialize(), nType, nVersion);
        pTxBase.reset(new CTransaction(tx));
    } else if (CTransactionBase::IsCertificate(objVer)) {
        CScCertificate cert(objVer);
        cert.SerializationOpInternal(is, CSerActionUnserialize(), nType, nVersion);
        pTxBase.reset(new CScCertificate(cert));
    } else {
        // This case should never happen unless an invalid version is passed along
        // the caller should test the pTxBase, it is reset to null just in case,
        // even if it should already be set that way
        // another option is to throw an exception, as a failing serialization would do
        pTxBase.reset(nullptr);
    }
}

#endif // BITCOIN_CORE_IO_H
