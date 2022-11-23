#ifndef _COINS_SELECTION_ALGORITHM_H
#define _COINS_SELECTION_ALGORITHM_H


#include <utility>
#include <vector>
#include "amount.h"


enum class CoinsSelectionAlgorithmType {
    UNDEFINED = 0,
    BRANCH_AND_BOUND = 1,
    SLIDING_WINDOW = 2
};


class CCoinsSelectionAlgorithm
{
protected:
    // temp
    std::vector<bool> tempSelection;    
    // temp
    
    // optimal
    CAmount optimalTotalAmount;
    size_t optimalTotalSize;
    uint optimalTotalSelection;
    // optimal
    
    // profiling and control
    const uint64_t problemSize;
    const int maxIndex;
    uint64_t recursions;
    uint64_t reachedLeaves;
    bool stop;
    uint64_t executionMicroseconds;
    // profiling and control

public:
    CoinsSelectionAlgorithmType type;

    // input variables
    const std::vector<CAmount> amounts;
    const std::vector<size_t> sizes;
    const CAmount targetAmount;
    const CAmount targetAmountPlusOffset;
    const size_t availableTotalSize;
    // input variables
    
    // output variables
    std::vector<bool> optimalSelection;
    // output variables

private:
    std::vector<CAmount> PrepareAmounts(const std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);
    std::vector<size_t> PrepareSizes(const std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);

protected:
    void Reset();

public:
    CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType _type,
                             const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                             const CAmount _targetAmount,
                             const CAmount _targetAmountPlusOffset,
                             const size_t _availableTotalSize);
    virtual ~CCoinsSelectionAlgorithm();
    virtual void Solve() = 0;
};


class CCoinsSelectionSlidingWindow : public CCoinsSelectionAlgorithm
{
public:
    CCoinsSelectionSlidingWindow(const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                 const CAmount _targetAmount,
                                 const CAmount _targetAmountPlusOffset,
                                 const size_t _availableTotalSize);
    ~CCoinsSelectionSlidingWindow();
    void Solve() override;
};


class CCoinsSelectionBranchAndBound : public CCoinsSelectionAlgorithm
{
protected:
    const std::vector<CAmount> cumulativeAmountsForward;

private:
    std::vector<CAmount> PrepareCumulativeAmountsForward();

protected:
    void SolveRecursive(int currentIndex, size_t tempTotalSize, CAmount tempTotalAmount, uint tempTotalSelection);

public:
    CCoinsSelectionBranchAndBound(const std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                  const CAmount _targetAmount,
                                  const CAmount _targetAmountPlusOffset,
                                  const size_t _availableTotalSize);
    ~CCoinsSelectionBranchAndBound();
    void Solve() override;
};

#endif // _COINS_SELECTION_ALGORITHM_H
