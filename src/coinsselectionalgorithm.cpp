#include "coinsselectionalgorithm.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

CCoinsSelectionAlgorithm::CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType _type,
                                                   const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                   const CAmount _targetAmount,
                                                   const CAmount _targetAmountPlusOffset,
                                                   const size_t _availableTotalSize)
                                                   : type {_type},
                                                     problemSize{_amountsAndSizes.size()},
                                                     maxIndex{problemSize - 1},
                                                     amounts {PrepareAmounts(_amountsAndSizes)},
                                                     sizes {PrepareSizes(_amountsAndSizes)},
                                                     targetAmount {_targetAmount},
                                                     targetAmountPlusOffset {_targetAmountPlusOffset},
                                                     availableTotalSize {_availableTotalSize}
{
    tempSelection = std::vector<bool>(problemSize, false);
    
    optimalTotalAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;
    optimalSelection = std::vector<bool>(problemSize, false);

    recursions = 0;
    reachedLeaves = 0;
    stop = false;
    executionMicroseconds = 0;
}

CCoinsSelectionAlgorithm::~CCoinsSelectionAlgorithm() {
}

std::vector<CAmount> CCoinsSelectionAlgorithm::PrepareAmounts(const std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedAmountsAndSizes = unsortedAmountsAndSizes;
    std::sort(sortedAmountsAndSizes.begin(), sortedAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    std::vector<CAmount> sortedAmounts = std::vector<CAmount>(problemSize, 0);
    for (int index = 0; index < problemSize; index++)
    {
        sortedAmounts[index] = sortedAmountsAndSizes[index].first;
    }
    return sortedAmounts;
}

std::vector<size_t> CCoinsSelectionAlgorithm::PrepareSizes(const std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedAmountsAndSizes = unsortedAmountsAndSizes;
    std::sort(sortedAmountsAndSizes.begin(), sortedAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    std::vector<size_t> sortedSizes = std::vector<size_t>(problemSize, 0);
    for (int index = 0; index < problemSize; index++)
    {
        sortedSizes[index] = sortedAmountsAndSizes[index].second;
    }
    return sortedSizes;
}

void CCoinsSelectionAlgorithm::Reset()
{  
    std::fill(tempSelection.begin(), tempSelection.end(), false);

    optimalTotalAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;
    std::fill(optimalSelection.begin(), optimalSelection.end(), false);

    recursions = 0;
    reachedLeaves = 0;
    stop = false;
    executionMicroseconds = 0;
}


// CCoinsSelectionSlidingWindow::CCoinsSelectionSlidingWindow(const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
//                                                            const CAmount _targetAmount,
//                                                            const CAmount _targetAmountPlusOffset,
//                                                            const size_t _availableTotalSize)
//                                                            : CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType::SLIDING_WINDOW,
//                                                                                       _amountsAndSizes,
//                                                                                       _targetAmount,
//                                                                                       _targetAmountPlusOffset,
//                                                                                       _availableTotalSize)
// {
// }

// void CCoinsSelectionSlidingWindow::Solve()
// {

// }


CCoinsSelectionBranchAndBound::CCoinsSelectionBranchAndBound(const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                             const CAmount _targetAmount,
                                                             const CAmount _targetAmountPlusOffset,
                                                             const size_t _availableTotalSize)
                                                             : CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType::BRANCH_AND_BOUND,
                                                                                        _amountsAndSizes,
                                                                                        _targetAmount,
                                                                                        _targetAmountPlusOffset,
                                                                                        _availableTotalSize),
                                                                                        cumulativeAmountsForward{PrepareCumulativeAmountsForward()}
{
    
}

std::vector<CAmount> CCoinsSelectionBranchAndBound::PrepareCumulativeAmountsForward()
{
    std::vector<CAmount> cumulativeAmountForwardTemp = std::vector<CAmount>(problemSize + 1, 0);
    for (int index = problemSize - 1; index >= 0; index--)
    {
        cumulativeAmountForwardTemp[index] = cumulativeAmountForwardTemp[index + 1] + amounts[index];
    }
    return cumulativeAmountForwardTemp;
}

CCoinsSelectionBranchAndBound::~CCoinsSelectionBranchAndBound() {
}

static uint64_t microsecondsBefore;
static uint64_t microsecondsFirst;
static uint64_t microsecondsAfter;

void CCoinsSelectionBranchAndBound::Solve()
{
    Reset();
    microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    SolveRecursive(0, 0, 0, 0);
    microsecondsAfter = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    uint64_t firstFindingMicroseconds = microsecondsFirst - microsecondsBefore;
    executionMicroseconds = microsecondsAfter - microsecondsBefore;
    freopen("output.txt","a",stdout);
    std::cout << std::to_string(optimalTotalSelection) + " - " + std::to_string(optimalTotalAmount) + " - " + std::to_string(optimalTotalSize) + "\n";
    std::cout << std::to_string(recursions) + " - " + std::to_string(reachedLeaves) + " - " + std::to_string(executionMicroseconds) + " - " + std::to_string(firstFindingMicroseconds) + "\n\n";
}

void CCoinsSelectionBranchAndBound::SolveRecursive(int currentIndex, size_t tempTotalSize, CAmount tempTotalAmount, uint tempTotalSelection)
{
    ++recursions;
    int nextIndex = currentIndex + 1;
    for (bool value : { false, true })
    {
        if (stop)
        {
            return;
        }
        tempSelection[currentIndex] = value;
        size_t tempTotalSizeNew = tempTotalSize + (value ? sizes[currentIndex] : 0);
        if (tempTotalSizeNew <= availableTotalSize) // {BT}
        {
            CAmount tempTotalAmountNew = tempTotalAmount + (value ? amounts[currentIndex] : 0);
            if (tempTotalAmountNew <= targetAmountPlusOffset) // {BT}
            {
                CAmount tempTotalAmountNewBiggestPossible = tempTotalAmountNew + cumulativeAmountsForward[nextIndex];
                if (tempTotalAmountNewBiggestPossible >= targetAmount) // {B}
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
                            ++reachedLeaves;
                            // no need of better-than-optimal check because the condition was checked before
                            optimalTotalSize = tempTotalSizeNew;
                            optimalTotalAmount = tempTotalAmountNew;
                            optimalTotalSelection = tempTotalSelectionNew;
                            optimalSelection = tempSelection;
                            // if (reachedLeaves == 1)
                            // {
                            //     microsecondsFirst = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                            //     stop = true;
                            // }
                        }
                    }
                }
            }
        }
    }
}
