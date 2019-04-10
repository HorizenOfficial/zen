#ifndef SAPLINGFORK_H
#define SAPLINGFORK_H

#include "fork6_overwinterfork.h"
namespace zen {

class SaplingFork : public OverWinterFork
{
public:
    SaplingFork();

    virtual bool isTransactionUpgradeActive(TransactionTypeActive  txType) const;

};

}
#endif // SAPLINGFORK_H
