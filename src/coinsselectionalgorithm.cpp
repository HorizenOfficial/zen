#include "coinsselectionalgorithm.hpp"
#include <cstdio>
#include <iostream>
#include <string>
#if COINS_SELECTION_ALGORITHM_PROFILING
#include <chrono>
#endif

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
                                                             // sizes must be initialized after amounts (PrepareSizes leverages sorting done by PrepareAmounts)
                                                             sizes {PrepareSizes(_amountsAndSizes)}, 
                                                             targetAmount {_targetAmount},
                                                             targetAmountPlusOffset {_targetAmountPlusOffset},
                                                             availableTotalSize {_availableTotalSize}
{
    tempSelection = std::vector<char>(problemDimension, 0);    
    optimalSelection = std::vector<char>(problemDimension, 0);
}

CCoinsSelectionAlgorithmBase::~CCoinsSelectionAlgorithmBase()
{
    StopSolving();
}

std::vector<CAmount> CCoinsSelectionAlgorithmBase::PrepareAmounts(std::vector<std::pair<CAmount, size_t>>& amountsAndSizes)
{
    std::sort(amountsAndSizes.begin(), amountsAndSizes.end(), std::greater<>());
    std::vector<CAmount> sortedAmounts(amountsAndSizes.size(), 0);
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedAmounts[index] = amountsAndSizes[index].first;
    }
    return sortedAmounts;
}

std::vector<size_t> CCoinsSelectionAlgorithmBase::PrepareSizes(std::vector<std::pair<CAmount, size_t>>& amountsAndSizes)
{
    std::vector<size_t> sortedSizes(amountsAndSizes.size(), 0);
    for (int index = 0; index < problemDimension; ++index)
    {
        sortedSizes[index] = amountsAndSizes[index].second;
    }
    return sortedSizes;
}

bool CCoinsSelectionAlgorithmBase::Reset()
{
    bool resetActuallyDone = false;

    if (isRunning || hasCompleted || stopRequested)
    {
        if (isRunning)
        {
            StopSolving();
        }
        tempSelection.assign(problemDimension, 0);
        optimalSelection.assign(problemDimension, 0);

        optimalTotalAmount = 0;
        optimalTotalSize = 0;
        optimalTotalSelection= 0;

        isRunning = false;
        asyncStartRequested = false;
        stopRequested = false;
        solvingThread = nullptr;
        hasCompleted = false;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        executionMicroseconds = 0;
        #endif
        resetActuallyDone = true;
    }

    return resetActuallyDone;
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
    if (!isRunning && !asyncStartRequested)
    {
        asyncStartRequested = true;
        solvingThread = std::unique_ptr<std::thread>(new std::thread(&CCoinsSelectionAlgorithmBase::Solve, this));
    }
}

void CCoinsSelectionAlgorithmBase::StopSolving()
{
    stopRequested = true;
    if (solvingThread != nullptr)
    {
        solvingThread->join();
        solvingThread.reset();
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

bool CCoinsSelectionAlgorithmBase::GetHasCompleted() const
{
    return hasCompleted;
}

#if COINS_SELECTION_ALGORITHM_PROFILING
uint64_t CCoinsSelectionAlgorithmBase::GetExecutionMicroseconds() const
{
    return executionMicroseconds;
}
#endif

const std::vector<char>& CCoinsSelectionAlgorithmBase::GetOptimalSelection() const
{
    return optimalSelection;
}

CAmount CCoinsSelectionAlgorithmBase::GetOptimalTotalAmount() const
{
    return optimalTotalAmount;
}

size_t CCoinsSelectionAlgorithmBase::GetOptimalTotalSize() const
{
    return optimalTotalSize;
}

unsigned int CCoinsSelectionAlgorithmBase::GetOptimalTotalSelection() const
{
    return optimalTotalSelection;
}

/* ---------- CCoinsSelectionAlgorithmBase ---------- */

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
}

CCoinsSelectionSlidingWindow::~CCoinsSelectionSlidingWindow()
{
}

bool CCoinsSelectionSlidingWindow::Reset()
{
    bool resetActuallyDone = CCoinsSelectionAlgorithmBase::Reset();

    if (resetActuallyDone)
    {
        #if COINS_SELECTION_ALGORITHM_PROFILING
        iterations = 0;
        #endif
    }

    return resetActuallyDone;
}

void CCoinsSelectionSlidingWindow::Solve()
{
    if (!isRunning)
    {
        Reset();

        isRunning = true;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        #endif
        size_t tempTotalSize = 0;
        CAmount tempTotalAmount = 0;
        unsigned int tempTotalSelection = 0;
        int windowFrontIndex = maxIndex;
        int windowBackIndex = maxIndex;
        bool admissibleFound = false;
        bool bestAdmissibleFound = false; // "best" for this specific algorithm implementation
        for (; !stopRequested && windowFrontIndex >= 0; --windowFrontIndex)   
        {
            #if COINS_SELECTION_ALGORITHM_PROFILING
            ++iterations;
            #endif

            // insert new coin in selection
            tempSelection[windowFrontIndex] = 1;
            tempTotalSize += sizes[windowFrontIndex];
            tempTotalAmount += amounts[windowFrontIndex];
            tempTotalSelection += 1;

            // check upper-limit constraints
            while (tempTotalSize > availableTotalSize ||
                   tempTotalAmount > targetAmountPlusOffset)
            {
                #if COINS_SELECTION_ALGORITHM_PROFILING
                ++iterations;
                #endif
       
                // if admissible solution still not found, pop from back of the sliding window
                if (!admissibleFound)
                {
                    tempSelection[windowBackIndex] = 0;
                    tempTotalSize -= sizes[windowBackIndex];
                    tempTotalAmount -= amounts[windowBackIndex];
                    tempTotalSelection -= 1;
                    --windowBackIndex;
                }
                // if admissible solution already found, pop from front of the sliding window only the element just inserted (and break)
                else
                {
                    tempSelection[windowFrontIndex] = 0;
                    tempTotalSize -= sizes[windowFrontIndex];
                    tempTotalAmount -= amounts[windowFrontIndex];
                    tempTotalSelection -= 1;
                    bestAdmissibleFound = true;
                    break; // almost surely it is useless, but kept for better flow
                }
            }

            // check lower-limit constraint
            if (tempTotalAmount >= targetAmount)
            {
                admissibleFound = true;
                // if best admissible solution already found or reached array end, set optimal solution
                if (bestAdmissibleFound || windowFrontIndex == 0)
                {
                    optimalTotalSize = tempTotalSize;
                    optimalTotalAmount = tempTotalAmount;
                    optimalTotalSelection = tempTotalSelection;
                    optimalSelection.assign(tempSelection.begin(), tempSelection.end());
                    break;
                }
            }
        }
        #if COINS_SELECTION_ALGORITHM_PROFILING
        executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
        #endif
        hasCompleted = !stopRequested;
        isRunning = false;
    }
}

/* ---------- CCoinsSelectionSlidingWindow ---------- */

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
}

CCoinsSelectionBranchAndBound::~CCoinsSelectionBranchAndBound() {
}

std::vector<CAmount> CCoinsSelectionBranchAndBound::PrepareCumulativeAmountsForward()
{
    std::vector<CAmount> cumulativeAmountsForwardTemp(problemDimension + 1, 0);
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
    // it has been empirically found that it is better to perform first exclusion and then inclusion this,
    // together with the descending order of coins, is probably due to the fact in this way the algorithm
    // arrives quickly at exploring tree branches with low amount coins (instead of dealing with included
    // high amount coins that would hardly represent the optimal solution)
    for (char value = 0; value <= 1; ++value)
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
                            optimalSelection.assign(tempSelection.begin(), tempSelection.end());
                        }
                    }
                }
            }
        }
    }
}

bool CCoinsSelectionBranchAndBound::Reset()
{
    bool resetActuallyDone = CCoinsSelectionAlgorithmBase::Reset();

    if (resetActuallyDone)
    {
        #if COINS_SELECTION_ALGORITHM_PROFILING
        recursions = 0;
        reachedNodes = 0;
        reachedLeaves = 0;
        #endif
    }

    return resetActuallyDone;
}

void CCoinsSelectionBranchAndBound::Solve()
{
    if (!isRunning)
    {
        Reset();

        isRunning = true;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        #endif
        SolveRecursive(0, 0, 0, 0);
        #if COINS_SELECTION_ALGORITHM_PROFILING
        executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
        #endif
        hasCompleted = !stopRequested;
        isRunning = false;
    }
}

/* ---------- CCoinsSelectionBranchAndBound ---------- */

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
                                                                                numberOfJoinsplitsOutputsAmounts {(unsigned int)_joinsplitsOutputsAmounts.size()},
                                                                                joinsplitsOutputsAmounts(_joinsplitsOutputsAmounts)
{
}

CCoinsSelectionForNotes::~CCoinsSelectionForNotes()
{
}

bool CCoinsSelectionForNotes::Reset()
{
    bool resetActuallyDone = CCoinsSelectionAlgorithmBase::Reset();

    if (resetActuallyDone)
    {
        #if COINS_SELECTION_ALGORITHM_PROFILING
        iterations = 0;
        #endif
    }

    return resetActuallyDone;
}

void CCoinsSelectionForNotes::Solve()
{
    if (!isRunning)
    {
        Reset();

        isRunning = true;
        #if COINS_SELECTION_ALGORITHM_PROFILING
        uint64_t microsecondsBefore = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        #endif

        int windowBackIndex = maxIndex;
        int windowFrontIndex = maxIndex;
        bool admissibleFound = false;
        bool bestAdmissibleFound = false; // "best" for this specific algorithm implementation
        bool breakOuterLoop = false;

        for (; !stopRequested && !breakOuterLoop && windowBackIndex >= 0; --windowBackIndex)
        {
            // quick reset before restarting with sliding window back index decreased by one position
            std::fill(tempSelection.begin(), tempSelection.end(), 0);
            size_t tempTotalSize = 0;
            CAmount tempTotalAmount = 0;
            unsigned int tempTotalSelection = 0;
        
            // joinsplits auxiliary variables
            int joinsplitsOutputAmountIndex = 0;
            bool isFirstJoinsplitInput = true;
            CAmount joinsplitValue = 0;
            CAmount changeFromPreviousJoinsplit = 0;

            for (windowFrontIndex = windowBackIndex; !stopRequested && windowFrontIndex >= 0; --windowFrontIndex)   
            {
                #if COINS_SELECTION_ALGORITHM_PROFILING
                ++iterations;
                #endif

                // insert new note in selection
                tempSelection[windowFrontIndex] = 1;
                size_t tempTotalSizeIterationIncrease = isFirstJoinsplitInput ? sizes[windowFrontIndex] : 0;
                tempTotalSize += tempTotalSizeIterationIncrease;
                tempTotalAmount += amounts[windowFrontIndex];
                tempTotalSelection += 1;

                // update joinsplit auxiliary variables
                if (isFirstJoinsplitInput && changeFromPreviousJoinsplit == 0)
                {
                    // first joinsplit input
                    joinsplitValue = amounts[windowFrontIndex];
                    isFirstJoinsplitInput = false;
                }
                else
                {
                    // first joinsplit input as previous joinsplit change
                    if (isFirstJoinsplitInput && changeFromPreviousJoinsplit > 0)
                    {
                        joinsplitValue = changeFromPreviousJoinsplit;
                    }
                    //second joinsplit input
                    joinsplitValue += amounts[windowFrontIndex];
                    if (joinsplitsOutputAmountIndex < numberOfJoinsplitsOutputsAmounts)
                    {
                        if (joinsplitValue >= joinsplitsOutputsAmounts[joinsplitsOutputAmountIndex])
                        {
                            changeFromPreviousJoinsplit = joinsplitValue - joinsplitsOutputsAmounts[joinsplitsOutputAmountIndex];
                            ++joinsplitsOutputAmountIndex;
                        }
                        else
                        {
                            joinsplitsOutputsAmounts[joinsplitsOutputAmountIndex] -= joinsplitValue;
                            changeFromPreviousJoinsplit = 0;
                        }
                    }
                    isFirstJoinsplitInput = true;
                }

                // check upper-limit constraints
                if (tempTotalSize + (numberOfJoinsplitsOutputsAmounts - joinsplitsOutputAmountIndex) * sizes[0] > availableTotalSize || // first element size is used (but actually all sizes are equal)
                    tempTotalAmount > targetAmountPlusOffset)
                {
                    // if admissible solution still not found, restart with sliding window back index increased by one position
                    if (!admissibleFound)
                    {
                        break;
                    }
                    // if admissible solution already found, pop from front of the sliding window only the element just inserted
                    else
                    {
                        tempSelection[windowFrontIndex] = 0;
                        tempTotalSize -= tempTotalSizeIterationIncrease;
                        tempTotalAmount -= amounts[windowFrontIndex];
                        tempTotalSelection -= 1;
                        bestAdmissibleFound = true;
                    }                    
                }

                // check lower-limit constraint
                if (tempTotalAmount >= targetAmount)
                {
                    admissibleFound = true;
                    // if best admissible solution already found or reached array end, set optimal solution
                    if (bestAdmissibleFound || windowFrontIndex == 0)
                    {
                        optimalTotalSize = tempTotalSize + (numberOfJoinsplitsOutputsAmounts - joinsplitsOutputAmountIndex) * sizes[0]; // first element size is used (but actually all sizes are equal)
                        optimalTotalAmount = tempTotalAmount;
                        optimalTotalSelection = tempTotalSelection;
                        optimalSelection.assign(tempSelection.begin(), tempSelection.end());
                        breakOuterLoop = true;
                        break;
                    }
                }
            }
        }
        #if COINS_SELECTION_ALGORITHM_PROFILING
        executionMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - microsecondsBefore;
        #endif
        hasCompleted = !stopRequested;
        isRunning = false;
    }
}

/* ---------- CCoinsSelectionForNotes ---------- */
