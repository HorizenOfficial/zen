#include "zen/delay.h"

int64_t GetBlockDelay(const CBlockIndex& newBlock, const CBlockIndex& prevBlock, const int activeChainHeight, const bool isStartupSyncing)
{
    if(isStartupSyncing)
    {
    	return 0;
    }

    if(newBlock.nHeight < activeChainHeight )
    {
      	LogPrintf("Received a delayed block (activeChainHeight: %d, newBlockHeight: %d)!\n", activeChainHeight, newBlock.nHeight);
    }

    // if the current chain is penalised.
    if (prevBlock.nChainDelay > 0)
    {
    	// blocks on tip get penalty reduced by 1;
    	// others get penalty increased proportionally to distance from tip
    	return std::max<int>((activeChainHeight - newBlock.nHeight),-1);
    }

    // Introduce penalty in case we receive a historic block.
    // (uses a threshold value)
    if (activeChainHeight - newBlock.nHeight > PENALTY_THRESHOLD )
    {
        return (activeChainHeight - newBlock.nHeight);
    } else // no delay detected.
    {
        return 0;
    }
}
