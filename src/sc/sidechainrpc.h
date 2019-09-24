#ifndef _SIDECHAIN_RPC_H
#define _SIDECHAIN_RPC_H

#include "amount.h"

#include "sc/sidechaintypes.h"

//------------------------------------------------------------------------------------

namespace Sidechain
{

class CRecipientFactory;

class CcRecipientVisitor : public boost::static_visitor<bool>
{
    private:
       CRecipientFactory* fact;
    public:
       explicit CcRecipientVisitor(CRecipientFactory* factIn) : fact(factIn) {}

    template <typename T>
    bool operator() (const T& r) const; //{ return fact->set(r); }
};

class CRecipientFactory
{
    private:
       CMutableTransaction* tx;
       std::string& err;

    public:
       CRecipientFactory(CMutableTransaction* txIn, std::string& errIn)
           : tx(txIn), err(errIn) {}

    bool set(const CcRecipientVariant& rec)
    {
        return boost::apply_visitor(CcRecipientVisitor(this), rec);
    };

    bool set(const CRecipientScCreation& r);
    bool set(const CRecipientCertLock& r);
    bool set(const CRecipientForwardTransfer& r);
    bool set(const CRecipientBackwardTransfer& r);
};

class CcRecipientAmountVisitor : public boost::static_visitor<CAmount>
{
    public:
    CAmount operator() (const CRecipientScCreation& r) const
    {
        // creation fee are in standard vout of the tx, while fwd contributions are in apposite obj below
        return SC_CREATION_FEE;
    }

    CAmount operator() (const CRecipientCertLock& r) const { return r.nValue; }
    CAmount operator() (const CRecipientForwardTransfer& r) const { return r.nValue; }
    CAmount operator() (const CRecipientBackwardTransfer& r) const { return r.nValue; }
};

}; // end of namespace

#endif // _SIDECHAIN_RPC_H
