#include "zen/delay.h"

int GetBlockDelay(const CBlockIndex& newBlock, const CBlockIndex& prevBlock, const int activeChainHeight, const bool isStartupSyncing)
{
    const int PENALTY_THRESHOLD = 5;

    if(isStartupSyncing) {
    	return 0;
    }

    if(newBlock.nHeight < activeChainHeight ) {
      	LogPrintf("Received a delayed block (activeChainHeight: %d, newBlockHeight: %d)!\n", activeChainHeight, newBlock.nHeight);
    }

    // if the current chain is penalised.
    if (prevBlock.nChainDelay > 0) {
        // Positive values to increase the penalty until
        // we reach the current active height.
        if (activeChainHeight >= newBlock.nHeight ) {
        	return (activeChainHeight - newBlock.nHeight);
        } else {
        	LogPrintf("Decreasing penalty to chain (activeChainHeight: %d, newBlockHeight: %d, prevBlockChainDelay: %d)!\n", activeChainHeight, newBlock.nHeight, prevBlock.nChainDelay);
        	// -1 to decrease the penalty afterwards.
            return -1;
        } 
    // no penalty yet, or penalty already resolved.
    } else {
        // Introduce penalty in case we receive a historic block.
        // (uses a threshold value)
        if (activeChainHeight - newBlock.nHeight > PENALTY_THRESHOLD ){
            return (activeChainHeight - newBlock.nHeight);
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
