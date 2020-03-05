#ifndef _SIDECHAIN_RPC_H
#define _SIDECHAIN_RPC_H

#include "amount.h"

#include "sc/sidechaintypes.h"

//------------------------------------------------------------------------------------

class UniValue;
class CTransaction;
class CTransactionBase;
class CMutableTransaction;
class CMutableTransactionBase;
class CSidechain;

namespace Sidechain
{

// utility class for handling custom data in sc
class CScCustomData : public base_blob<MAX_CUSTOM_DATA_BITS> {
public:
    CScCustomData() {}
    CScCustomData(const base_blob<MAX_CUSTOM_DATA_BITS>& b) : base_blob<MAX_CUSTOM_DATA_BITS>(b) {}
    explicit CScCustomData(const std::vector<unsigned char>& vch) : base_blob<MAX_CUSTOM_DATA_BITS>(vch) {}

    void fill(std::vector<unsigned char>& vBytes, int nBytes) const;
};

class CRecipientHandler
{
    private:
       CMutableTransactionBase* txBase;
       std::string& err;

    public:
       CRecipientHandler(CMutableTransactionBase* objIn, std::string& errIn)
           : txBase(objIn), err(errIn) {}

    bool visit(const CcRecipientVariant& rec);

    bool handle(const CRecipientScCreation& r);
    bool handle(const CRecipientCertLock& r);
    bool handle(const CRecipientForwardTransfer& r);
    bool handle(const CRecipientBackwardTransfer& r);
};

class CcRecipientVisitor : public boost::static_visitor<bool>
{
    private:
       CRecipientHandler* handler;
    public:
       explicit CcRecipientVisitor(CRecipientHandler* hIn) : handler(hIn) {}

    template <typename T>
    bool operator() (const T& r) const { return handler->handle(r); }
};

class CcRecipientAmountVisitor : public boost::static_visitor<CAmount>
{
    public:
    CAmount operator() (const CRecipientScCreation& r) const { return r.nValue; }
    CAmount operator() (const CRecipientCertLock& r) const { return r.nValue; }
    CAmount operator() (const CRecipientForwardTransfer& r) const { return r.nValue; }
    CAmount operator() (const CRecipientBackwardTransfer& r) const { return r.nValue; }
};

// used in get tx family of rpc commands
void AddSidechainOutsToJSON (const CTransaction& tx, UniValue& parentObj);

// used when creating a raw transaction with cc outputs
bool AddSidechainCreationOutputs(UniValue& sc_crs, CMutableTransaction& rawTx, std::string& error);
bool AddSidechainForwardOutputs(UniValue& fwdtr, CMutableTransaction& rawTx, std::string& error);

// used when funding a raw tx 
void fundCcRecipients(const CTransaction& tx, std::vector<CcRecipientVariant>& vecCcSend);
}; // end of namespace

#endif // _SIDECHAIN_RPC_H
