#include "zen/delay.h"
#include "checkpoints.h"

int GetBlockDelay(const CBlockIndex& newBlock, const CBlockIndex& prevBlock, const int activeChainHeight)
{
    const int PENALTY_THRESHOLD = 0;
    
    if (activeChainHeight == -1) { return 0; }
    LogPrintf("%s:  activeChainHeight %d  - newBlock.nHeight %d \n",__func__, activeChainHeight, newBlock.nHeight);

    if (Checkpoints::isSync) {
        LogPrintf("%s:  isSync true \n",__func__);
        return 0;
    }
    LogPrintf("%s:  isSync false \n",__func__);


    // if the current chain is penalised.
    if (prevBlock.nChainDelay > 0) {
        // Positive values to increase the penalty until
        // we reach the current active height.
        if (activeChainHeight >= newBlock.nHeight ) {
            return (activeChainHeight - newBlock.nHeight);
        // -1 to decrease the penalty afterwards.
        } else {
            return -1;
        } 
    // no penalty yet, or penalty already resolved.
    } else {
        // Introduce penalty in case we receive a historic block.
        // (uses a threshold value)
        if (activeChainHeight - newBlock.nHeight > PENALTY_THRESHOLD ){
            return (activeChainHeight - newBlock.nHeight - PENALTY_THRESHOLD);
        // no delay detected.
        } else {
            return 0;
        }
    }
}
bool IsChainPenalised(const CChain& chain)
{
    return (chain.Tip()->nChainDelay < 0);
}
