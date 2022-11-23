#ifndef _COINS_SELECTION_ALGORITHM_H
#define _COINS_SELECTION_ALGORITHM_H


#include <utility>
#include <vector>
#include "amount.h"


#define COINS_SELECTION_ALGORITHM_DEBUGGING 0


enum class CoinsSelectionAlgorithmType {
    UNDEFINED = 0,
    BRANCH_AND_BOUND = 1,
    SLIDING_WINDOW = 2
};

/* ---------- CCoinsSelectionAlgorithm ---------- */

class CCoinsSelectionAlgorithm
{
protected:
    // auxiliary
    std::vector<bool> tempSelection;    
    const int problemDimension;
    const int maxIndex;
    // auxiliary
       
    // profiling and control
    bool stop;
    #if COINS_SELECTION_ALGORITHM_DEBUGGING
    uint64_t executionMicroseconds;
    #endif
    // profiling and control

public:
    // auxiliary
    CoinsSelectionAlgorithmType type;
    // auxiliary

    // input variables
    const std::vector<CAmount> amounts;
    const std::vector<size_t> sizes;
    const CAmount targetAmount;
    const CAmount targetAmountPlusOffset;
    const size_t availableTotalSize;
    // input variables
    
    // output variables
    std::vector<bool> optimalSelection;
    CAmount optimalTotalAmount;
    size_t optimalTotalSize;
    uint optimalTotalSelection;
    // output variables

private:
    std::vector<CAmount> PrepareAmounts(const std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);
    std::vector<size_t> PrepareSizes(const std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);

protected:
    virtual void Reset();

public:
    CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType _type,
                             const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                             const CAmount _targetAmount,
                             const CAmount _targetAmountPlusOffset,
                             const size_t _availableTotalSize);
    virtual ~CCoinsSelectionAlgorithm();
    virtual void Solve() = 0;
};

/* ---------- ---------- */

/* ---------- CCoinsSelectionSlidingWindow ---------- */

class CCoinsSelectionSlidingWindow : public CCoinsSelectionAlgorithm
{
protected:
    // profiling
    #if COINS_SELECTION_ALGORITHM_DEBUGGING
    uint64_t iterations;
    #endif
    // profiling

protected:
    void Reset() override;

public:
    CCoinsSelectionSlidingWindow(const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                 const CAmount _targetAmount,
                                 const CAmount _targetAmountPlusOffset,
                                 const size_t _availableTotalSize);
    ~CCoinsSelectionSlidingWindow();
    void Solve() override;
};

/* ---------- ---------- */

/* ---------- CCoinsSelectionBranchAndBound ---------- */

class CCoinsSelectionBranchAndBound : public CCoinsSelectionAlgorithm
{
protected:
    // auxiliary
    const std::vector<CAmount> cumulativeAmountsForward;
    // auxiliary

    // profiling
    #if COINS_SELECTION_ALGORITHM_DEBUGGING
    uint64_t recursions;
    uint64_t reachedNodes;
    uint64_t reachedLeaves;
    #endif
    // profiling

private:
    std::vector<CAmount> PrepareCumulativeAmountsForward();
    void SolveRecursive(int currentIndex, size_t tempTotalSize, CAmount tempTotalAmount, uint tempTotalSelection);

protected:
    void Reset() override;

public:
    CCoinsSelectionBranchAndBound(const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                  const CAmount _targetAmount,
                                  const CAmount _targetAmountPlusOffset,
                                  const size_t _availableTotalSize);
    ~CCoinsSelectionBranchAndBound();
    void Solve() override;
};

/* ---------- ---------- */

#endif // _COINS_SELECTION_ALGORITHM_H
