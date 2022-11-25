#ifndef _COINS_SELECTION_ALGORITHM_H
#define _COINS_SELECTION_ALGORITHM_H


#include <utility>
#include <vector>
#include <string>
#include <thread>
#include "amount.h"


#define COINS_SELECTION_ALGORITHM_PROFILING 0


enum class CoinsSelectionAlgorithmType {
    UNDEFINED = 0,
    SLIDING_WINDOW = 1,
    BRANCH_AND_BOUND = 2
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

    // profiling and control
    bool hasStarted;
    bool asyncStartRequested;
    bool asyncStopRequested;
    std::thread* solvingThread;
    bool completed;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    uint64_t executionMicroseconds;
    #endif
    // profiling and control

private:
    std::vector<CAmount> PrepareAmounts(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);
    std::vector<size_t> PrepareSizes(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);

protected:
    virtual void Reset();

public:
    CCoinsSelectionAlgorithm(CoinsSelectionAlgorithmType _type,
                             std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                             CAmount _targetAmount,
                             CAmount _targetAmountPlusOffset,
                             size_t _availableTotalSize);
    virtual ~CCoinsSelectionAlgorithm();
    void StartSolving();
    virtual void Solve() = 0;
    void StopSolving();
    std::string ToString();
    static CCoinsSelectionAlgorithm& GetBestAlgorithmBySolution(CCoinsSelectionAlgorithm& first, CCoinsSelectionAlgorithm& second);
};

/* ---------- ---------- */

/* ---------- CCoinsSelectionSlidingWindow ---------- */

class CCoinsSelectionSlidingWindow : public CCoinsSelectionAlgorithm
{
protected:
    // profiling
    #if COINS_SELECTION_ALGORITHM_PROFILING
    uint64_t iterations;
    #endif
    // profiling

protected:
    void Reset() override;

public:
    CCoinsSelectionSlidingWindow(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                 CAmount _targetAmount,
                                 CAmount _targetAmountPlusOffset,
                                 size_t _availableTotalSize);
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
    #if COINS_SELECTION_ALGORITHM_PROFILING
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
    CCoinsSelectionBranchAndBound(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                  CAmount _targetAmount,
                                  CAmount _targetAmountPlusOffset,
                                  size_t _availableTotalSize);
    ~CCoinsSelectionBranchAndBound();
    void Solve() override;
};

/* ---------- ---------- */

#endif // _COINS_SELECTION_ALGORITHM_H
