#include "coinsselectionalgorithm.hpp"
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

/* ---------- CCoinsSelectionAlgorithmBase ---------- */

CCoinsSelectionAlgorithmBase::CCoinsSelectionAlgorithmBase(CoinsSelectionAlgorithmType _type,
                                                   std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                   CAmount _targetAmount,
                                                   CAmount _targetAmountPlusOffset,
                                                   size_t _availableTotalSize)
                                                   : type {_type},
                                                     problemDimension {(unsigned int)_amountsAndSizes.size()},
                                                     maxIndex {(unsigned int)_amountsAndSizes.size() - 1},
                                                     amounts {PrepareAmounts(_amountsAndSizes)},
                                                     sizes {PrepareSizes(_amountsAndSizes)},
                                                     targetAmount {_targetAmount},
                                                     targetAmountPlusOffset {_targetAmountPlusOffset},
                                                     availableTotalSize {_availableTotalSize}
{
    tempSelection = new bool[problemDimension];    
    optimalSelection = new bool[problemDimension];
    for (int index = 0; index < problemDimension; ++index)
    {
        tempSelection[index] = false;
        optimalSelection[index] = false;
    }

    optimalTotalAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;

    isRunning = false;
    stopRequested = false;
    solvingThread = nullptr;
    hasCompleted = false;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = 0;
    #endif
}

CCoinsSelectionAlgorithmBase::~CCoinsSelectionAlgorithmBase()
{
    // solvingThread stopping must be performed before data destruction
    StopSolving();
    delete[] amounts;
    delete[] sizes;
    delete[] tempSelection;
    delete[] optimalSelection;
}

CAmount* CCoinsSelectionAlgorithmBase::PrepareAmounts(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedAmountsAndSizes = unsortedAmountsAndSizes;
    std::sort(sortedAmountsAndSizes.begin(), sortedAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    CAmount* sortedAmounts = new CAmount[problemDimension];
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedAmounts[index] = sortedAmountsAndSizes[index].first;
    }
    return sortedAmounts;
}

size_t* CCoinsSelectionAlgorithmBase::PrepareSizes(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes)
{
    std::vector<std::pair<CAmount, size_t>> sortedAmountsAndSizes = unsortedAmountsAndSizes;
    std::sort(sortedAmountsAndSizes.begin(), sortedAmountsAndSizes.end(), [](std::pair<CAmount, size_t> left, std::pair<CAmount, size_t> right) -> bool { return ( left.first > right.first); } );
    size_t* sortedSizes = new size_t[problemDimension];
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedSizes[index] = sortedAmountsAndSizes[index].second;
    }
    return sortedSizes;
}

void CCoinsSelectionAlgorithmBase::Reset()
{  
    for (int index = 0; index < problemDimension; ++index)
    {
        tempSelection[index] = false;
        optimalSelection[index] = false;
    }

    optimalTotalAmount = 0;
    optimalTotalSize = 0;
    optimalTotalSelection= 0;

    isRunning = false;
    stopRequested = false;
    solvingThread = nullptr;
    hasCompleted = false;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    executionMicroseconds = 0;
    #endif
}

std::string CCoinsSelectionAlgorithmBase::ToString()
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

void CCoinsSelectionAlgorithmBase::StartSolvingAsync()
{
    if (!isRunning)
    {
        if (solvingThread != nullptr)
        {
            // this can be the case of a new start on the same algorithm instance,
            // hence a deletion of previous pointer is performed before reallocation
            delete solvingThread;
        }
        solvingThread = new std::thread(&CCoinsSelectionAlgorithmBase::Solve, this);
    }
}

void CCoinsSelectionAlgorithmBase::StopSolving()
{
    if (!stopRequested)
    {
        stopRequested = true;
        if (solvingThread != nullptr)
        {
            solvingThread->join();
            delete solvingThread;
            solvingThread = nullptr;
        }
    }
}

void CCoinsSelectionAlgorithmBase::GetBestAlgorithmBySolution(std::unique_ptr<CCoinsSelectionAlgorithmBase> &left, std::unique_ptr<CCoinsSelectionAlgorithmBase> &right, std::unique_ptr<CCoinsSelectionAlgorithmBase> &best)
{
    if ((left->optimalTotalSelection > right->optimalTotalSelection) ||
        (left->optimalTotalSelection == right->optimalTotalSelection && left->optimalTotalAmount <= right->optimalTotalAmount))
    {
        best.swap(left);
    }
    else
    {
        best.swap(right);
    }
}

/* ---------- ---------- */

/* ---------- CCoinsSelectionSlidingWindow ---------- */

CCoinsSelectionSlidingWindow::CCoinsSelectionSlidingWindow(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                           CAmount _targetAmount,
                                                           CAmount _targetAmountPlusOffset,
                                                           size_t _availableTotalSize)
                                                           : CCoinsSelectionAlgorithmBase(CoinsSelectionAlgorithmType::SLIDING_WINDOW,
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
    CCoinsSelectionAlgorithmBase::Reset();

    #if COINS_SELECTION_ALGORITHM_PROFILING
    iterations = 0;
    #endif
}

void CCoinsSelectionSlidingWindow::Solve()
{
    if (!isRunning)
    {
        isRunning = true;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        #endif
        size_t tempTotalSize = 0;
        CAmount tempTotalAmount = 0;
        unsigned int tempTotalSelection = 0;
        int exclusionIndex = maxIndex;
        int inclusionIndex = maxIndex;
        for (; inclusionIndex >= 0; --inclusionIndex)   
        {
            if (stopRequested)
            {
                break;
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
        if (stopRequested == false)
            hasCompleted = true;
        isRunning = false;
    }
}

/* ---------- ---------- */

/* ---------- CCoinsSelectionBranchAndBound ---------- */

CCoinsSelectionBranchAndBound::CCoinsSelectionBranchAndBound(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                             CAmount _targetAmount,
                                                             CAmount _targetAmountPlusOffset,
                                                             size_t _availableTotalSize)
                                                             : CCoinsSelectionAlgorithmBase(CoinsSelectionAlgorithmType::BRANCH_AND_BOUND,
                                                                                            _amountsAndSizes,
                                                                                            _targetAmount,
                                                                                            _targetAmountPlusOffset,
                                                                                            _availableTotalSize),
                                                                                            cumulativeAmountsForward {PrepareCumulativeAmountsForward()}
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    recursions = 0;
    reachedNodes = 0;
    reachedLeaves = 0;
    #endif
}

CCoinsSelectionBranchAndBound::~CCoinsSelectionBranchAndBound() {
    delete [] cumulativeAmountsForward;
}

CAmount* CCoinsSelectionBranchAndBound::PrepareCumulativeAmountsForward()
{
    CAmount* cumulativeAmountsForwardTemp = new CAmount[problemDimension + 1];
    cumulativeAmountsForwardTemp[problemDimension] = 0;
    for (int index = problemDimension - 1; index >= 0; --index)
    {
        cumulativeAmountsForwardTemp[index] = cumulativeAmountsForwardTemp[index + 1] + amounts[index];
    }
    return cumulativeAmountsForwardTemp;
}

void CCoinsSelectionBranchAndBound::SolveRecursive(int currentIndex, size_t tempTotalSize, CAmount tempTotalAmount, unsigned int tempTotalSelection)
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    ++recursions;
    #endif
    int nextIndex = currentIndex + 1;
    // it has been empirically found that it is better to perform first exclusion and then inclusion
    // this, together with the descending order of coins, is probably due to the fact in this way the algorithm
    // arrives quickly at exploring tree branches with low amount coins (instead of dealing with included high
    // amount coins that would hardly represent the optimal solution)
    for (bool value : { false, true })
    {
        if (stopRequested)
        {
            break;
        }
        tempSelection[currentIndex] = value;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        ++reachedNodes;
        #endif
        size_t tempTotalSizeNew = tempTotalSize + (value ? sizes[currentIndex] : 0);
        if (tempTotalSizeNew <= availableTotalSize) // {backtracking}
        {
            CAmount tempTotalAmountNew = tempTotalAmount + (value ? amounts[currentIndex] : 0);
            if (tempTotalAmountNew <= targetAmountPlusOffset) // {backtracking}
            {
                CAmount tempTotalAmountNewBiggestPossible = tempTotalAmountNew + cumulativeAmountsForward[nextIndex];
                if (tempTotalAmountNewBiggestPossible >= targetAmount) // {backtracking}
                {
                    unsigned int tempTotalSelectionNew = tempTotalSelection + (value ? 1 : 0);
                    unsigned int maxTotalSelectionForward = tempTotalSelectionNew + (maxIndex - currentIndex);
                    if ((maxTotalSelectionForward > optimalTotalSelection) ||
                        (maxTotalSelectionForward == optimalTotalSelection && tempTotalAmountNewBiggestPossible < optimalTotalAmount)) // {bounding}
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
    CCoinsSelectionAlgorithmBase::Reset();

    #if COINS_SELECTION_ALGORITHM_PROFILING
    recursions = 0;
    reachedNodes = 0;
    reachedLeaves = 0;
    #endif
}

void CCoinsSelectionBranchAndBound::Solve()
{
    if (!isRunning)
    {
        isRunning = true;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        #endif
        SolveRecursive(0, 0, 0, 0);
        #if COINS_SELECTION_ALGORITHM_PROFILING
        executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
        #endif
        if (stopRequested == false)
            hasCompleted = true;
        isRunning = false;
    }
}

/* ---------- ---------- */

/* ---------- CCoinsSelectionForNotes ---------- */

CCoinsSelectionForNotes::CCoinsSelectionForNotes(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                                 CAmount _targetAmount,
                                                 CAmount _targetAmountPlusOffset,
                                                 size_t _availableTotalSize,
                                                 std::vector<CAmount> _joinsplitsOutputsAmounts)
                                                 : CCoinsSelectionAlgorithmBase(CoinsSelectionAlgorithmType::FOR_NOTES,
                                                                                _amountsAndSizes,
                                                                                _targetAmount,
                                                                                _targetAmountPlusOffset,
                                                                                _availableTotalSize),
                                                                                numberOfJoinsplitsOutputsAmounts {_joinsplitsOutputsAmounts.size()},
                                                                                joinsplitsOutputsAmounts {PrepareJoinsplitsOutputsAmounts(_joinsplitsOutputsAmounts)}
{
    #if COINS_SELECTION_ALGORITHM_PROFILING
    iterations = 0;
    #endif
}

CCoinsSelectionForNotes::~CCoinsSelectionForNotes() {
    delete [] joinsplitsOutputsAmounts;
}

CAmount* CCoinsSelectionForNotes::PrepareJoinsplitsOutputsAmounts(std::vector<CAmount> joinsplitsOutputsAmounts)
{
    CAmount* joinsplitsOutputsAmountsTemp = new CAmount[numberOfJoinsplitsOutputsAmounts];
    for (int index = 0; index < numberOfJoinsplitsOutputsAmounts; ++index)
    {
        joinsplitsOutputsAmountsTemp[index] = joinsplitsOutputsAmounts[index];
    }
    return joinsplitsOutputsAmountsTemp;
}

void CCoinsSelectionForNotes::Reset()
{
    CCoinsSelectionAlgorithmBase::Reset();

    #if COINS_SELECTION_ALGORITHM_PROFILING
    iterations = 0;
    #endif
}

void CCoinsSelectionForNotes::Solve()
{
    if (!isRunning)
    {
        isRunning = true;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        #endif
        size_t tempTotalSize = 0;
        CAmount tempTotalAmount = 0;
        unsigned int tempTotalSelection = 0;
        int exclusionIndex = maxIndex;
        int inclusionIndex = maxIndex;
        for (; inclusionIndex >= 0; --inclusionIndex)   
        {
            if (stopRequested)
            {
                break;
            }
            tempSelection[inclusionIndex] = true;
            //if (tempTotalSelection >= joinSplitsAlreadyIncluded)
            QUI C'E' DA FINIRE (CAPIRE SE E' MEGLIO TOGLIERE COMUNQUE DA AVAILABLE BYTES OPPURE GESTIRE LA SIZE DIRETTAMENTE QUA)
            {
                tempTotalSize += sizes[inclusionIndex];
            }
            tempTotalAmount += amounts[inclusionIndex];
            tempTotalSelection += 1;
            while (tempTotalSize > availableTotalSize ||
                   tempTotalAmount > targetAmountPlusOffset)
            {
                tempSelection[exclusionIndex] = false;
                //if (tempTotalSelection > joinSplitsAlreadyIncluded)
                {
                    tempTotalSize -= sizes[exclusionIndex];
                }
                tempTotalAmount -= amounts[exclusionIndex];
                tempTotalSelection -= 1;
                --exclusionIndex;
            }
            if (tempTotalAmount >= targetAmount)
            {
                optimalTotalSize = tempTotalSize;
                optimalTotalAmount = tempTotalAmount;
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
        if (stopRequested == false)
            hasCompleted = true;
        isRunning = false;
    }
}
