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

namespace Sidechain
{

class ScInfo;

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
    CAmount operator() (const CRecipientScCreation& r) const
    {
        // fwd contributions are in apposite obj below
        return 0;
    }

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

// used in getscinfo rpc cmd
void AddScInfoToJSON(UniValue& result);
bool AddScInfoToJSON(const uint256& scId, UniValue& sc);
void AddScInfoToJSON(const uint256& scId, const ScInfo& info, UniValue& sc);
}; // end of namespace

#endif // _SIDECHAIN_RPC_H
