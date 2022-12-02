#include "coinsselectionalgorithm.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

/* ---------- CCoinsSelectionAlgorithm ---------- */

CCoinsSelectionAlgorithm::CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType _type,
                                                   std::vector<std::pair<CAmount, size_t>> _netAmountsAndSizes,
                                                   CAmount _targetNetAmount,
                                                   CAmount _targetNetAmountPlusOffset,
                                                   size_t _availableTotalSize)
                                                   : type {_type},
                                                     problemDimension{(int)_netAmountsAndSizes.size()},
                                                     maxIndex{(int)_netAmountsAndSizes.size() - 1},
                                                     netAmounts {PrepareNetAmounts(_netAmountsAndSizes)},
                                                     sizes {PrepareSizes(_netAmountsAndSizes)},
                                                     targetNetAmount {_targetNetAmount},
                                                     targetNetAmountPlusOffset {_targetNetAmountPlusOffset},
                                                     availableTotalSize {_availableTotalSize}
{
    tempSelection = new bool[problemDimension];    
    optimalSelection = new bool[problemDimension];
    for (int index = 0; index < problemDimension; ++index)
    {
        tempSelection[index] = false;
        optimalSelection[index] = false;
    }

    optimalTotalNetAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;

    hasStarted = false;
    asyncStartRequested = false;
    stopRequested = false;
    hasCompleted = false;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = 0;
    #endif
}

CCoinsSelectionAlgorithm::~CCoinsSelectionAlgorithm() {
    delete[] netAmounts;
    delete[] sizes;
    delete[] tempSelection;
    delete[] optimalSelection;
}

CAmount* CCoinsSelectionAlgorithm::PrepareNetAmounts(std::vector<std::pair<CAmount, size_t>> unsortedNetAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedNetAmountsAndSizes = unsortedNetAmountsAndSizes;
    std::sort(sortedNetAmountsAndSizes.begin(), sortedNetAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    CAmount* sortedNetAmounts = new CAmount[problemDimension];
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedNetAmounts[index] = sortedNetAmountsAndSizes[index].first;
    }
    return sortedNetAmounts;
}

size_t* CCoinsSelectionAlgorithm::PrepareSizes(std::vector<std::pair<CAmount, size_t>> unsortedNetAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedNetAmountsAndSizes = unsortedNetAmountsAndSizes;
    std::sort(sortedNetAmountsAndSizes.begin(), sortedNetAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    size_t* sortedSizes = new size_t[problemDimension];
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedSizes[index] = sortedNetAmountsAndSizes[index].second;
    }
    return sortedSizes;
}

void CCoinsSelectionAlgorithm::Reset()
{  
    for (int index = 0; index < problemDimension; ++index)
    {
        tempSelection[index] = false;
        optimalSelection[index] = false;
    }

    optimalTotalNetAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;

    hasStarted = false;
    asyncStartRequested = false;
    stopRequested = false;
    hasCompleted = false;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = 0;
    #endif
}

std::string CCoinsSelectionAlgorithm::ToString()
{
    return std::string("Input:")
                                + "{targetNetAmount=" + std::to_string(targetNetAmount) +
                                  ",targetNetAmountPlusOffset=" + std::to_string(targetNetAmountPlusOffset) +
                                  ",availableTotalSize=" + std::to_string(availableTotalSize) + "}\n" +
                       "Output:"
                                + "{optimalTotalNetAmount=" + std::to_string(optimalTotalNetAmount) +
                                  ",optimalTotalSize=" + std::to_string(optimalTotalSize) +
                                  ",optimalTotalSelection=" + std::to_string(optimalTotalSelection) + "}\n";
}

void CCoinsSelectionAlgorithm::StartSolvingAsync()
{
    if (!asyncStartRequested)
    {
        asyncStartRequested = true;
        solvingThread = new std::thread(&CCoinsSelectionAlgorithm::Solve, this);
    }
}

void CCoinsSelectionAlgorithm::StopSolving()
{
    if (asyncStartRequested && !stopRequested)
    {
        stopRequested = true;
        solvingThread->join();
    }
}

CCoinsSelectionAlgorithm& CCoinsSelectionAlgorithm::GetBestAlgorithmBySolution(CCoinsSelectionAlgorithm& left, CCoinsSelectionAlgorithm& right)
{
    if ((left.optimalTotalSelection > right.optimalTotalSelection) ||
        (left.optimalTotalSelection == right.optimalTotalSelection && left.optimalTotalNetAmount <= right.optimalTotalNetAmount))
    {
        return left;
    }
    else
    {
        return right;
    }
}

/* ---------- ---------- */

/* ---------- CCoinsSelectionSlidingWindow ---------- */

CCoinsSelectionSlidingWindow::CCoinsSelectionSlidingWindow(std::vector<std::pair<CAmount, size_t>> _netAmountsAndSizes,
                                                           CAmount _targetNetAmount,
                                                           CAmount _targetNetAmountPlusOffset,
                                                           size_t _availableTotalSize)
                                                           : CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType::SLIDING_WINDOW,
                                                                                      _netAmountsAndSizes,
                                                                                      _targetNetAmount,
                                                                                      _targetNetAmountPlusOffset,
                                                                                      _availableTotalSize)
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    iterations = 0;
    #endif
}

CCoinsSelectionSlidingWindow::~CCoinsSelectionSlidingWindow() {
}

void CCoinsSelectionSlidingWindow::Reset()
{
    CCoinsSelectionAlgorithm::Reset();

    #if COINS_SELECTION_ALGORITHM_PROFILING
    iterations = 0;
    #endif
}

void CCoinsSelectionSlidingWindow::Solve()
{
    hasStarted = true;;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    #endif
    size_t tempTotalSize = 0;
    CAmount tempTotalNetAmount = 0;
    unsigned int tempTotalSelection = 0;
    int exclusionIndex = maxIndex;
    int inclusionIndex = maxIndex;
    for (; inclusionIndex >= 0; --inclusionIndex)   
    {
        if (stopRequested)
        {
            return;
        }
        tempSelection[inclusionIndex] = true;
        tempTotalSize += sizes[inclusionIndex];
        tempTotalNetAmount += netAmounts[inclusionIndex];
        tempTotalSelection += 1;
        while (tempTotalSize > availableTotalSize ||
               tempTotalNetAmount > targetNetAmountPlusOffset)
        {
            tempSelection[exclusionIndex] = false;
            tempTotalSize -= sizes[exclusionIndex];
            tempTotalNetAmount -= netAmounts[exclusionIndex];
            tempTotalSelection -= 1;
            --exclusionIndex;
        }
        if (tempTotalNetAmount >= targetNetAmount)
        {
            optimalTotalSize = tempTotalSize;
            optimalTotalNetAmount = tempTotalNetAmount;
            optimalTotalSelection = tempTotalSelection;
            for (int index = 0; index < problemDimension; ++index)
            {
                optimalSelection[index] = tempSelection[index];
            }
            break;
        }
    }   
    #if COINS_SELECTION_ALGORITHM_PROFILING
    iterations = (maxIndex + 1 - inclusionIndex) + (maxIndex - exclusionIndex);
    executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
    #endif
    hasCompleted = true;
}

/* ---------- ---------- */

/* ---------- CCoinsSelectionBranchAndBound ---------- */

CCoinsSelectionBranchAndBound::CCoinsSelectionBranchAndBound(std::vector<std::pair<CAmount, size_t>> _netAmountsAndSizes,
                                                             CAmount _targetNetAmount,
                                                             CAmount _targetNetAmountPlusOffset,
                                                             size_t _availableTotalSize)
                                                             : CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType::BRANCH_AND_BOUND,
                                                                                        _netAmountsAndSizes,
                                                                                        _targetNetAmount,
                                                                                        _targetNetAmountPlusOffset,
                                                                                        _availableTotalSize),
                                                                                        cumulativeNetAmountsForward{PrepareCumulativeNetAmountsForward()}
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    recursions = 0;
    reachedNodes = 0;
    reachedLeaves = 0;
    #endif
}

CCoinsSelectionBranchAndBound::~CCoinsSelectionBranchAndBound() {
    delete [] cumulativeNetAmountsForward;
}

CAmount* CCoinsSelectionBranchAndBound::PrepareCumulativeNetAmountsForward()
{
    CAmount* cumulativeNetAmountsForwardTemp = new CAmount[problemDimension + 1];
    cumulativeNetAmountsForwardTemp[problemDimension] = 0;
    for (int index = problemDimension - 1; index >= 0; --index)
    {
        cumulativeNetAmountsForwardTemp[index] = cumulativeNetAmountsForwardTemp[index + 1] + netAmounts[index];
    }
    return cumulativeNetAmountsForwardTemp;
}

void CCoinsSelectionBranchAndBound::SolveRecursive(int currentIndex, size_t tempTotalSize, CAmount tempTotalNetAmount, unsigned int tempTotalSelection)
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    ++recursions;
    #endif
    int nextIndex = currentIndex + 1;
    for (bool value : { false, true })
    {
        // it has been empirically found that it is better to perform first exclusion and then inclusion
        // this, together with the descending order of coins, is probably due to the fact in this way the algorithm
        // arrives quickly at exploring tree branches with low amount coins (instead of dealing with included high
        // amount coins that would hardly represent the optimal solution)
        if (stopRequested)
        {
            return;
        }
        tempSelection[currentIndex] = value;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        ++reachedNodes;
        #endif
        size_t tempTotalSizeNew = tempTotalSize + (value ? sizes[currentIndex] : 0);
        if (tempTotalSizeNew <= availableTotalSize) // {backtracking}
        {
            CAmount tempTotalNetAmountNew = tempTotalNetAmount + (value ? netAmounts[currentIndex] : 0);
            if (tempTotalNetAmountNew <= targetNetAmountPlusOffset) // {backtracking}
            {
                CAmount tempTotalNetAmountNewBiggestPossible = tempTotalNetAmountNew + cumulativeNetAmountsForward[nextIndex];
                if (tempTotalNetAmountNewBiggestPossible >= targetNetAmount) // {backtracking}
                {
                    unsigned int tempTotalSelectionNew = tempTotalSelection + (value ? 1 : 0);
                    unsigned int maxTotalSelectionForward = tempTotalSelectionNew + (maxIndex - currentIndex);
                    if ((maxTotalSelectionForward > optimalTotalSelection) ||
                        (maxTotalSelectionForward == optimalTotalSelection && tempTotalNetAmountNewBiggestPossible < optimalTotalNetAmount)) // {bounding}
                    {
                        if (currentIndex < maxIndex)
                        {
                            SolveRecursive(nextIndex, tempTotalSizeNew, tempTotalNetAmountNew, tempTotalSelectionNew);
                        }
                        else
                        {
                            #if COINS_SELECTION_ALGORITHM_PROFILING
                            ++reachedLeaves;
                            #endif
                            optimalTotalSize = tempTotalSizeNew;
                            optimalTotalNetAmount = tempTotalNetAmountNew;
                            optimalTotalSelection = tempTotalSelectionNew;
                            for (int index = 0; index < problemDimension; ++index)
                            {
                                optimalSelection[index] = tempSelection[index];
                            }
                        }
                    }
                }
            }
        }
    }
}

void CCoinsSelectionBranchAndBound::Reset()
{
    CCoinsSelectionAlgorithm::Reset();

    #if COINS_SELECTION_ALGORITHM_PROFILING
    recursions = 0;
    reachedNodes = 0;
    reachedLeaves = 0;
    #endif
}

void CCoinsSelectionBranchAndBound::Solve()
{
    hasStarted = true;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    #endif
    SolveRecursive(0, 0, 0, 0);
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
    #endif
    hasCompleted = true;
}

/* ---------- ---------- */