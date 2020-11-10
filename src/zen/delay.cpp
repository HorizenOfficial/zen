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
    	// other blocks get penalty increased proportionally to distance from tip
    	int64_t blockDelay =  std::max<int64_t>((activeChainHeight - newBlock.nHeight),int64_t(-1));

    	LogPrintf("calculated blockDelay %d for newBlockHeight %d (activeChainHeight: %d, prevBlockChainDelay: %d)!\n",
    			blockDelay, activeChainHeight, newBlock.nHeight, prevBlock.nChainDelay);
    	return blockDelay;
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
