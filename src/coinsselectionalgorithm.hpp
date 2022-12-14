#ifndef _COINS_SELECTION_ALGORITHM_H
#define _COINS_SELECTION_ALGORITHM_H


#include <string>
#include <thread>
#include <vector>
#include "amount.h"


//! Flag for profiling/debugging mode
#define COINS_SELECTION_ALGORITHM_PROFILING 0
//! This represents the number of intermediate change levels inside the interval [targetAmount + 0, targetAmount + maxChange]
/*!
  Low value -> higher quantity of selected utxos and higher change, high value -> lower quantity of selected utxos and lower change
*/
#define COINS_SELECTION_INTERMEDIATE_CHANGE_LEVELS 9


//! Types of coins selection algorithm
enum class CoinsSelectionAlgorithmType {
    UNDEFINED = 0,
    SLIDING_WINDOW = 1,
    BRANCH_AND_BOUND = 2,
    FOR_NOTES = 3
};

/* ---------- CCoinsSelectionAlgorithmBase ---------- */

//! Abstract class for algorithm of coins selection
/*!
  This class provides common fields required by each implementation and utility methods
*/
class CCoinsSelectionAlgorithmBase
{    
protected:
    // auxiliary
    //! The temporary set of selected elements (true->selected, false->unselected)
    bool* tempSelection;
    //! Max index of elements (equal to "problemDimension - 1")
    const unsigned int maxIndex;
    // auxiliary

    // profiling and control
    //! Flag identifying if the solving routine is running
    bool isRunning;
    //! Flag identifying if a stop of the solving routine has been requested
    bool stopRequested;
    //! The thread associated to the solving routine
    std::thread* solvingThread;
    // profiling and control

public:
    //! The algorithm type
    CoinsSelectionAlgorithmType type;

    // input variables
    //! Number of elements
    const unsigned int problemDimension;
    //! The array of amounts
    const CAmount* amounts;
    //! The array of sizes (in terms of bytes of the associated input)
    const size_t* sizes;
    //! The target amount to satisfy (it is a lower-limit constraint)
    const CAmount targetAmount;
    //! The target amount plus a positive offset (it is an upper-limit constraint)
    const CAmount targetAmountPlusOffset;
    //! The available total size (in terms of bytes, it is an upper-limit constraint)
    const size_t availableTotalSize;
    // input variables

    // output variables
    //! The optimal set of selected elements (true->selected, false->unselected)
    bool* optimalSelection;
    //! The total amount of optimal selection
    CAmount optimalTotalAmount;
    //! The total size of optimal selection
    size_t optimalTotalSize;
    //! The quantity of elements of optimal selection (this is the variable to be maximized)
    unsigned int optimalTotalSelection;
    // output variables

    // profiling and control
    //! Flag identifying if the solving routine has completed
    bool hasCompleted;
    #if COINS_SELECTION_ALGORITHM_PROFILING
    //! Microseconds elapsed to complete solving routine
    uint64_t executionMicroseconds;
    #endif
    // profiling and control

private:
    //! Method for preparing array of amounts sorting them with descending order (with respect to amounts)
    /*!
      \param unsortedAmountsAndSizes vector of pairs of amounts and sizes of the elements
      \return the array of amounts in descending order
    */
    CAmount* PrepareAmounts(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);
    //! Method for preparing array of sizes sorting them with descending order (with respect to amounts)
    /*!
      \param unsortedAmountsAndSizes vector of pairs of amounts and sizes of the elements
      \return the array of sizes (arranged in descending order with respect to input amounts)
    */
    size_t* PrepareSizes(std::vector<std::pair<CAmount, size_t>> unsortedAmountsAndSizes);

protected:
    //! Method for resetting internal variables (must be called before restarting the algorithm)
    virtual void Reset();

public:
    //! Constructor
    /*!
      \param _type algorithm type
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
    */
    CCoinsSelectionAlgorithmBase(CoinsSelectionAlgorithmType _type,
                             std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                             CAmount _targetAmount,
                             CAmount _targetAmountPlusOffset,
                             size_t _availableTotalSize);
    //! Destructor
    ~CCoinsSelectionAlgorithmBase();
    //! Method for asynchronously starting the solving routine
    void StartSolvingAsync();
    //! Abstract method for synchronously running the solving routine
    virtual void Solve() = 0;
    //! Method for synchronously stopping the solving routine
    void StopSolving();
    //! Method for providing a string representation of the algorithm input and output variables
    /*!
      \return a string representation of the algorithm input and output variables
    */
    std::string ToString();
    //! Static method for selecting the best among two algorithms based on their output variables
    /*!
      \param left tne algorithm for comparison (position does not matter)
      \param right the other algorithm for comparison (position does not matter)
      \return the best algorithm
    */
    static void GetBestAlgorithmBySolution(std::unique_ptr<CCoinsSelectionAlgorithmBase> &left, std::unique_ptr<CCoinsSelectionAlgorithmBase> &right, std::unique_ptr<CCoinsSelectionAlgorithmBase> &best);
};

/* ---------- ---------- */

/* ---------- CCoinsSelectionSlidingWindow ---------- */

//! "Sliding Window" implementation of algorithm of coins selection
/*!
  This class provides a specific implementation of the solving routine.
  In this implementation coins are iteratively added to (or removed from) current selection set starting from lowest
  amount coin and proceeding towards highest amount coin (FIFO queue).
  At each iteration the algorithm pushes in the next coin; if the target amount plus offset and available total size
  constraints (upper-limit) are not met, the algorithm starts popping out the smallest coins until the two constraints
  above are met; then the algorithm checks if the target amount constraint (lower-limit) is met, if so it returns the
  current selection set without further optimization, otherwise it continues
*/
class CCoinsSelectionSlidingWindow : public CCoinsSelectionAlgorithmBase
{
protected:
    // profiling
    #if COINS_SELECTION_ALGORITHM_PROFILING
    //! Counter for keeping track of the number of iterations the solving routine has performed
    uint64_t iterations;
    #endif
    // profiling

protected:
    //! Method for resetting internal variables (must be called before restarting the algorithm)
    void Reset() override;

public:
    //! Constructor
    /*!
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
    */
    CCoinsSelectionSlidingWindow(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                 CAmount _targetAmount,
                                 CAmount _targetAmountPlusOffset,
                                 size_t _availableTotalSize);
    //! Destructor
    ~CCoinsSelectionSlidingWindow();
    //! Method for synchronously running the solving routine with "Sliding Window" strategy
    void Solve() override;
};

/* ---------- ---------- */

/* ---------- CCoinsSelectionBranchAndBound ---------- */

//! "Branch & Bound" implementation of algorithm of coins selection
/*!
  This class provides a specific implementation of the solving routine.
  In this implementation, a binary tree is considered as the combination of excluding/including each coin.
  This would lead to a number of combinations equal to 2^problemDimension with a brute force strategy.
  The algorithm doesn't rely on simple brute force strategy, instead two additional aspects are taken into account for
  speeding up the algorithm and avoiding exploring branches which would not give an improved solution (with respect to
  the temporary optimal one): backtracking and bounding.
  Starting with an "all coins unselected" setup, the algorithm recursively explores the tree (from biggest coin toward smallest
  coin) opening two new branches, the first one excluding the current coin, the second one including the current coin; when a
  leaf is reached, the output variables are checked to identify if an improved solution (with respect to the temporary optimal
  one) is found and eventually marked as the new temporary optimal solution.
  The tree actual exploration differs very significantly from the tree full exploration thanks to:
  +] backtracking (1): given that at a certain recursion, including a new coin would automatically increase both the temporary
     total amount as well as the temporary total size, if during the tree exploration the two upper-limit constraints associated
     to target amount plus offset and to total size are broken then all the branches from the current recursion on are cut;
     this is done in order to avoid reaching leaves that would certainly be not admissible with respect to these two constraints.
  +] backtracking (2): given that at a certain recursion, the highest total amount reachable is computed as the sum of current
     total amount and of all the amounts of coins from the current recursion on, if during the tree exploration this sum does not
     exceed the lower-limit associated to target amount then all the branches from the current recursion on are cut; this is done
     in order to avoid reaching leaves that would certainly be not admissible with respect to this constraint.
  +] bounding: given that at a certain recursion, the highest total selection reachable is computed as the sum of current total
     selection and of the quantity of coins from the current recrusion on, if during tree exploration this sum does not exceed
     the temporary optimal solution (ties are handled prioritizing low total amount) then all the branches from the current
     recursion on are cut; this is done in order to avoid reaching leaves that would certainly not improve the temporary optimal
     solution.
*/
class CCoinsSelectionBranchAndBound : public CCoinsSelectionAlgorithmBase
{
protected:
    // auxiliary
    //! The array of cumulative amounts (considered summing amounts from index to end of amounts array)
    const CAmount* cumulativeAmountsForward;
    // auxiliary

    // profiling
    #if COINS_SELECTION_ALGORITHM_PROFILING
    //! Counter for keeping track of the number of recursions the solving routine has performed
    uint64_t recursions;
    //! Counter for keeping track of the number of nodes reached by the solving routine
    uint64_t reachedNodes;
    //! Counter for keeping track of the number of leaves reached by the solving routine
    uint64_t reachedLeaves;
    #endif
    // profiling

private:
    //! Method for preparing array of cumulative amounts
    /*!
      \return the array of cumulative amounts
    */
    CAmount* PrepareCumulativeAmountsForward();
    //! Method for synchronously running the solving routine recursion with "Branch & Bound" strategy
    /*!
      \param currentIndex the current index the tree exploration is at
      \param tempTotalSize the temporary total size of tree exploration
      \param tempTotalAmount the temporary total amount of tree exploration
      \param tempTotalSelection the temporary total selection of tree exploration
    */
    void SolveRecursive(int currentIndex, size_t tempTotalSize, CAmount tempTotalAmount, unsigned int tempTotalSelection);

protected:
    //! Method for resetting internal variables (must be called before restarting the algorithm)
    void Reset() override;

public:
    //! Constructor
    /*!
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
    */
    CCoinsSelectionBranchAndBound(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                  CAmount _targetAmount,
                                  CAmount _targetAmountPlusOffset,
                                  size_t _availableTotalSize);
    //! Destructor
    ~CCoinsSelectionBranchAndBound();
    //! Method for synchronously running the solving routine with "Branch & Bound" strategy
    void Solve() override;
};

/* ---------- ---------- */

/* ---------- CCoinsSelectionForNotes ---------- */

//! "For Notes" implementation of algorithm of coins selection
/*!
  This class provides a specific implementation of the solving routine.
*/
class CCoinsSelectionForNotes : public CCoinsSelectionAlgorithmBase
{
protected:
    // profiling
    #if COINS_SELECTION_ALGORITHM_PROFILING
    //! Counter for keeping track of the number of iterations the solving routine has performed
    uint64_t iterations;
    #endif
    // profiling

public:
    // input variables
    //! Number of joinsplits outputs amounts
    const unsigned int numberOfJoinsplitsOutputsAmounts;
    //! Joinsplits outputs amounts
    const CAmount* joinsplitsOutputsAmounts;
    // input variables

private:
    //! Method for preparing array of joinsplits outputs amounts
    /*!
      \return the array of cumulative amounts
    */
    CAmount* PrepareJoinsplitsOutputsAmounts(std::vector<CAmount> joinsplitsOutputsAmounts);

protected:
    //! Method for resetting internal variables (must be called before restarting the algorithm)
    void Reset() override;

public:
    //! Constructor
    /*!
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
      \param _joinsplitsOutputsAmount amounts of joinsplits outputs (order matters)
    */
    CCoinsSelectionForNotes(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                            CAmount _targetAmount,
                            CAmount _targetAmountPlusOffset,
                            size_t _availableTotalSize,
                            std::vector<CAmount> _joinsplitsOutputsAmounts);
    //! Destructor
    ~CCoinsSelectionForNotes();
    //! Method for synchronously running the solving routine with "Sliding Window" strategy
    void Solve() override;
};

/* ---------- ---------- */

#endif // _COINS_SELECTION_ALGORITHM_H