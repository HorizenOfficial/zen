#include "coinsselectionalgorithm.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

/* ---------- CCoinsSelectionAlgorithm ---------- */

CCoinsSelectionAlgorithm::CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType _type,
                                                   std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                   CAmount _targetAmount,
                                                   CAmount _targetAmountPlusOffset,
                                                   size_t _availableTotalSize)
                                                   : type {_type},
                                                     problemDimension{(int)_amountsAndSizes.size()},
                                                     maxIndex{problemDimension - 1},
                                                     amounts {PrepareAmounts(_amountsAndSizes)},
                                                     sizes {PrepareSizes(_amountsAndSizes)},
                                                     targetAmount {_targetAmount},
                                                     targetAmountPlusOffset {_targetAmountPlusOffset},
                                                     availableTotalSize {_availableTotalSize}
{
    tempSelection = std::vector<bool>(problemDimension, false);
    
    optimalSelection = std::vector<bool>(problemDimension, false);
    optimalTotalAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;

    stop = false;
    completed = false;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = 0;
    #endif
}

CCoinsSelectionAlgorithm::~CCoinsSelectionAlgorithm() {
}

std::vector<CAmount> CCoinsSelectionAlgorithm::PrepareAmounts(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedAmountsAndSizes = unsortedAmountsAndSizes;
    std::sort(sortedAmountsAndSizes.begin(), sortedAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    std::vector<CAmount> sortedAmounts = std::vector<CAmount>(problemDimension, 0);
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedAmounts[index] = sortedAmountsAndSizes[index].first;
    }
    return sortedAmounts;
}

std::vector<size_t> CCoinsSelectionAlgorithm::PrepareSizes(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedAmountsAndSizes = unsortedAmountsAndSizes;
    std::sort(sortedAmountsAndSizes.begin(), sortedAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    std::vector<size_t> sortedSizes = std::vector<size_t>(problemDimension, 0);
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedSizes[index] = sortedAmountsAndSizes[index].second;
    }
    return sortedSizes;
}

void CCoinsSelectionAlgorithm::Reset()
{  
    std::fill(tempSelection.begin(), tempSelection.end(), false);

    std::fill(optimalSelection.begin(), optimalSelection.end(), false);
    optimalTotalAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;

    stop = false;
    completed = false;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = 0;
    #endif
}

std::string CCoinsSelectionAlgorithm::ToString()
{
    return std::string("Input:")
                                + "{targetAmount=" + std::to_string(targetAmount) +
                                  ",targetAmountPlusOffset=" + std::to_string(targetAmountPlusOffset) +
                                  ",availableTotalSize=" + std::to_string(availableTotalSize) + "}\n" +
                       "Output:"
                                + "{optimalTotalAmount=" + std::to_string(optimalTotalAmount) +
                                  ",optimalTotalSize=" + std::to_string(optimalTotalSize) +
                                  ",optimalTotalSelection=" + std::to_string(optimalTotalSelection) + "}\n";
}

CCoinsSelectionAlgorithm& CCoinsSelectionAlgorithm::GetBestAlgorithmBySolution(CCoinsSelectionAlgorithm& first, CCoinsSelectionAlgorithm& second)
{
    if ((first.optimalTotalSelection > second.optimalTotalSelection) ||
        (first.optimalTotalSelection == second.optimalTotalSelection && first.optimalTotalAmount <= second.optimalTotalAmount))
    {
        return first;
    }
    else
    {
        return second;
    }
}

/* ---------- ---------- */

/* ---------- CCoinsSelectionSlidingWindow ---------- */

CCoinsSelectionSlidingWindow::CCoinsSelectionSlidingWindow(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                           CAmount _targetAmount,
                                                           CAmount _targetAmountPlusOffset,
                                                           size_t _availableTotalSize)
                                                           : CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType::SLIDING_WINDOW,
                                                                                      _amountsAndSizes,
                                                                                      _targetAmount,
                                                                                      _targetAmountPlusOffset,
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
    #if COINS_SELECTION_ALGORITHM_PROFILING
    uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    #endif
    size_t tempTotalSize = 0;
    CAmount tempTotalAmount = 0;
    uint tempTotalSelection = 0;
    int exclusionIndex = maxIndex;
    int inclusionIndex = maxIndex;
    for (; inclusionIndex >= 0; --inclusionIndex)   
    {
        if (stop)
        {
            return;
        }
        tempSelection[inclusionIndex] = true;
        tempTotalSize += sizes[inclusionIndex];
        tempTotalAmount += amounts[inclusionIndex];
        tempTotalSelection += 1;
        while (tempTotalSize > availableTotalSize ||
               tempTotalAmount > targetAmountPlusOffset)
        {
            tempSelection[exclusionIndex] = false;
            tempTotalSize -= sizes[exclusionIndex];
            tempTotalAmount -= amounts[exclusionIndex];
            tempTotalSelection -= 1;
            --exclusionIndex;
        }
        if (tempTotalAmount >= targetAmount)
        {
            optimalTotalSize = tempTotalSize;
            optimalTotalAmount = tempTotalAmount;
            optimalTotalSelection = tempTotalSelection;
            optimalSelection = tempSelection;
            break;
        }
    }   
    #if COINS_SELECTION_ALGORITHM_PROFILING
    iterations = (maxIndex + 1 - inclusionIndex) + (maxIndex - exclusionIndex);
    executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
    #endif
    completed = true;
    // #if COINS_SELECTION_ALGORITHM_PROFILING
    // freopen("CCoinsSelectionSlidingWindow.txt","a",stdout);
    // std::cout << std::to_string(optimalTotalSelection) + " - " + std::to_string(optimalTotalAmount) + " - " + std::to_string(optimalTotalSize) + "\n";
    // std::cout << std::to_string(iterations) + " - " + std::to_string(executionMicroseconds) + "\n\n";
    // #endif
}

/* ---------- ---------- */

/* ---------- CCoinsSelectionBranchAndBound ---------- */

CCoinsSelectionBranchAndBound::CCoinsSelectionBranchAndBound(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                             CAmount _targetAmount,
                                                             CAmount _targetAmountPlusOffset,
                                                             size_t _availableTotalSize)
                                                             : CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType::BRANCH_AND_BOUND,
                                                                                        _amountsAndSizes,
                                                                                        _targetAmount,
                                                                                        _targetAmountPlusOffset,
                                                                                        _availableTotalSize),
                                                                                        cumulativeAmountsForward{PrepareCumulativeAmountsForward()}
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    recursions = 0;
    reachedNodes = 0;
    reachedLeaves = 0;
    #endif
}

CCoinsSelectionBranchAndBound::~CCoinsSelectionBranchAndBound() {
}

std::vector<CAmount> CCoinsSelectionBranchAndBound::PrepareCumulativeAmountsForward()
{
    std::vector<CAmount> cumulativeAmountForwardTemp = std::vector<CAmount>(problemDimension + 1, 0);
    for (int index = problemDimension - 1; index >= 0; --index)
    {
        cumulativeAmountForwardTemp[index] = cumulativeAmountForwardTemp[index + 1] + amounts[index];
    }
    return cumulativeAmountForwardTemp;
}

void CCoinsSelectionBranchAndBound::SolveRecursive(int currentIndex, size_t tempTotalSize, CAmount tempTotalAmount, uint tempTotalSelection)
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    ++recursions;
    #endif
    int nextIndex = currentIndex + 1;
    for (bool value : { false, true })
    {
        if (stop)
        {
            return;
        }
        tempSelection[currentIndex] = value;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        ++reachedNodes;
        #endif
        size_t tempTotalSizeNew = tempTotalSize + (value ? sizes[currentIndex] : 0);
        if (tempTotalSizeNew <= availableTotalSize) // {BT}
        {
            CAmount tempTotalAmountNew = tempTotalAmount + (value ? amounts[currentIndex] : 0);
            if (tempTotalAmountNew <= targetAmountPlusOffset) // {BT}
            {
                CAmount tempTotalAmountNewBiggestPossible = tempTotalAmountNew + cumulativeAmountsForward[nextIndex];
                if (tempTotalAmountNewBiggestPossible >= targetAmount) // {BT}
                {
                    uint tempTotalSelectionNew = tempTotalSelection + (value ? 1 : 0);
                    uint maxTotalSelectionForward = tempTotalSelectionNew + (maxIndex - currentIndex);
                    if ((maxTotalSelectionForward > optimalTotalSelection) ||
                        (maxTotalSelectionForward == optimalTotalSelection && tempTotalAmountNewBiggestPossible < optimalTotalAmount)) // {B}
                    {
                        if (currentIndex < maxIndex)
                        {
                            SolveRecursive(nextIndex, tempTotalSizeNew, tempTotalAmountNew, tempTotalSelectionNew);
                        }
                        else
                        {
                            #if COINS_SELECTION_ALGORITHM_PROFILING
                            ++reachedLeaves;
                            #endif
                            optimalTotalSize = tempTotalSizeNew;
                            optimalTotalAmount = tempTotalAmountNew;
                            optimalTotalSelection = tempTotalSelectionNew;
                            optimalSelection = tempSelection;
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
    freopen("CHECK.txt","a",stdout);
    std::cout << "I have started" << std::flush;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    #endif
    SolveRecursive(0, 0, 0, 0);
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
    #endif
    completed = true;
    freopen("CHECK.txt","a",stdout);
    std::cout << "I have completed" << std::flush;
    // #if COINS_SELECTION_ALGORITHM_PROFILING
    // freopen("CCoinsSelectionBranchAndBound.txt","a",stdout);
    // std::cout << std::to_string(optimalTotalSelection) + " - " + std::to_string(optimalTotalAmount) + " - " + std::to_string(optimalTotalSize) + "\n";
    // std::cout << std::to_string(recursions) + " - " + std::to_string(reachedNodes) + " - "  + std::to_string(reachedLeaves) + " - " + std::to_string(executionMicroseconds) + "\n\n";
    // #endif
}

/* ---------- ---------- */
