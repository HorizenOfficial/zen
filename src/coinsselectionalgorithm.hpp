#ifndef _COINS_SELECTION_ALGORITHM_H
#define _COINS_SELECTION_ALGORITHM_H


#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include "amount.h"


//! Flag for profiling/debugging mode
#define COINS_SELECTION_ALGORITHM_PROFILING 0

//! This represents the number of intermediate change levels inside the interval [targetAmount + 0, targetAmount + maxChange]
/*!
  Low value -> higher quantity of selected utxos and higher change, high value -> lower quantity of selected utxos and lower change
*/
constexpr int COINS_SELECTION_INTERMEDIATE_CHANGE_LEVELS = 9;


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
    //! The algorithm type
    const CoinsSelectionAlgorithmType type;

    // ---------- auxiliary
    //! The temporary set of selected elements (1->selected, 0->unselected)
    //! std::vector<char> is used over std::vector<bool> for favoring processing over optimization
    std::vector<char> tempSelection;

    //! Max index of elements (equal to "problemDimension - 1")
    const unsigned int maxIndex;
    // ---------- auxiliary

    // ---------- profiling and control
    //! Flag identifying if the solving routine is running
    std::atomic<bool> isRunning = false;

    //! Flag identifying if an async start of the solving routine has been requested
    std::atomic<bool> asyncStartRequested = false;

    //! Flag identifying if a stop of the solving routine has been requested
    std::atomic<bool> stopRequested = false;

    //! The thread associated to the solving routine
    std::unique_ptr<std::thread> solvingThread = nullptr;

    //! Flag identifying if the solving routine has completed
    std::atomic<bool> hasCompleted = false;

    //! Milliseconds the solving routine started
    uint64_t executionStartMilliseconds = 0;

    //! Milliseconds elapsed completing the solving routine
    uint64_t executionElapsedMilliseconds = 0;

    //! Flag identifying if the solving routine has hit timeout
    bool timeoutHit = false;
    // ---------- profiling and control

    // ---------- output variables
    //! The optimal set of selected elements (1->selected, 0->unselected)
    //! std::vector<char> is used over std::vector<bool> for favoring processing over optimization
    std::vector<char> optimalSelection;

    //! The total amount of optimal selection
    CAmount optimalTotalAmount = 0;

    //! The total size of optimal selection
    size_t optimalTotalSize = 0;

    //! The quantity of elements of optimal selection (this is the variable to be maximized)
    unsigned int optimalTotalSelection = 0;
    // ---------- output variables

public:
    // ---------- input variables
    //! Number of elements
    const unsigned int problemDimension;

    //! The array of amounts
    const std::vector<CAmount> amounts;

    //! The array of sizes (in terms of bytes of the associated input)
    const std::vector<size_t> sizes;

    //! The target amount to satisfy (it is a lower-limit constraint)
    const CAmount targetAmount;

    //! The target amount plus a positive offset (it is an upper-limit constraint)
    const CAmount targetAmountPlusOffset;

    //! The available total size (in terms of bytes, it is an upper-limit constraint)
    const size_t availableTotalSize;

    //! Timeout for completing solving routine (in milliseconds)
    const uint64_t executionTimeoutMilliseconds = 0;
    // ---------- input variables

private:
    //! Method for preparing array of amounts sorting them with descending order (with respect to amounts)
    /*!
      \param amountsAndSizes vector of pairs of amounts and sizes of the elements
      \return the array of amounts in descending order
    */
    std::vector<CAmount> PrepareAmounts(std::vector<std::pair<CAmount, size_t>>& amountsAndSizes);

    //! Method for preparing array of sizes (expects descending order with respect to amounts)
    /*!
      \param amountsAndSizes vector of pairs of amounts and sizes of the elements
      \return the array of sizes
    */
    std::vector<size_t> PrepareSizes(std::vector<std::pair<CAmount, size_t>>& amountsAndSizes);

protected:
    //! Method for resetting internal variables (must be called before restarting the algorithm)
    /*!
      \return the flag representing if the reset was actually done
    */
    virtual bool Reset();

public:
    //! Constructor
    /*!
      \param _type algorithm type
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
      \param _executionTimeoutMilliseconds timeout for completing solving routine (in milliseconds) (default to 0)
    */
    CCoinsSelectionAlgorithmBase(CoinsSelectionAlgorithmType _type,
                                 std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                 CAmount _targetAmount,
                                 CAmount _targetAmountPlusOffset,
                                 size_t _availableTotalSize,
                                 uint64_t _executionTimeoutMilliseconds = 0);

    //! Deleted copy constructor
    CCoinsSelectionAlgorithmBase(const CCoinsSelectionAlgorithmBase&) = delete;

    //! Deleted move constructor (deletion not needed, but better to be explicit)
    CCoinsSelectionAlgorithmBase(CCoinsSelectionAlgorithmBase&&) = delete;

    //! Deleted assignment operator
    CCoinsSelectionAlgorithmBase& operator=(const CCoinsSelectionAlgorithmBase&) = delete;

    //! Deleted move operator (deletion not needed, but better to be explicit)
    CCoinsSelectionAlgorithmBase& operator=(CCoinsSelectionAlgorithmBase&&) = delete;

    //! Destructor
    virtual ~CCoinsSelectionAlgorithmBase();

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
      \param left the algorithm for comparison (this is returned in case of full tie)
      \param right the other algorithm for comparison
      \param best the best algorithm
    */
    static void GetBestAlgorithmBySolution(std::unique_ptr<CCoinsSelectionAlgorithmBase> &left, std::unique_ptr<CCoinsSelectionAlgorithmBase> &right, std::unique_ptr<CCoinsSelectionAlgorithmBase> &best);

    // ---------- getters
    //! Method for getting the algorithm type
    /*!
      \return the algorithm type
    */
    CoinsSelectionAlgorithmType GetAlgorithmType() const;

    //! Method for getting if the solving routine has completed
    /*!
      \return the flag representing if the solving routine has completed
    */
    bool GetHasCompleted() const;

    //! Method for getting the milliseconds elapsed completing the solving routine
    /*!
      \return the milliseconds elapsed completing the solving routine
    */
    uint64_t GetExecutionElapsedMilliseconds() const;

    //! Method for getting the optimal set of selected elements (true->selected, false->unselected)
    /*!
      \return the optimal set of selected elements
    */
    const std::vector<char>& GetOptimalSelection() const;

    //! Method for getting the total amount of optimal selection
    /*!
      \return the total amount of optimal selection
    */
    CAmount GetOptimalTotalAmount() const;

    //! Method for getting the total size of optimal selection (the underlying variable is the one to be maximized)
    /*!
      \return the total size of optimal selection
    */
    size_t GetOptimalTotalSize() const;

    //! Method for getting the quantity of elements of optimal selection
    /*!
      \return the quantity of elements of optimal selection
    */
    unsigned int GetOptimalTotalSelection() const;
    // ---------- getters
};

/* ---------- CCoinsSelectionAlgorithmBase ---------- */

/* ---------- CCoinsSelectionSlidingWindow ---------- */

//! "Sliding Window" implementation of algorithm of coins selection
/*!
  This class provides a specific implementation of the solving routine.
  In this implementation coins are iteratively added to (or removed from) current selection set starting from lowest
  amount coin and proceeding towards highest amount coin.
  At each iteration the algorithm pushes in the next coin; if the target amount plus offset and available total size
  constraints (upper-limit) are not met, the algorithm starts popping out the smallest coins until the two constraints
  above are met; then the algorithm checks if the target amount constraint (lower-limit) is met; if it is not met, the
  algorithm continues with next coin insertion, otherwise it marks the finding of an admissible solution and performs
  additional insertions until one of the upper-limit constraints is broken (and thus removing the just inserted coin)
  or the set of available coins is empty, eventually setting the best selection set.
*/
class CCoinsSelectionSlidingWindow : public CCoinsSelectionAlgorithmBase
{
protected:
    // ---------- profiling
    #if COINS_SELECTION_ALGORITHM_PROFILING
    //! Counter for keeping track of the number of iterations the solving routine has performed
    uint64_t iterations = 0;
    #endif
    // ---------- profiling

protected:
    //! Method for resetting internal variables (must be called before restarting the algorithm)
    /*!
      \return the flag representing if the reset was actually done
    */
    bool Reset() override;

public:
    //! Constructor
    /*!
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
      \param _executionTimeoutMilliseconds timeout for completing solving routine (in milliseconds) (default to 0)
    */
    CCoinsSelectionSlidingWindow(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                 CAmount _targetAmount,
                                 CAmount _targetAmountPlusOffset,
                                 size_t _availableTotalSize,
                                 uint64_t _executionTimeoutMilliseconds = 0);

    //! Deleted copy constructor
    CCoinsSelectionSlidingWindow(const CCoinsSelectionSlidingWindow&) = delete;

    //! Deleted move constructor (deletion not needed, but better to be explicit)
    CCoinsSelectionSlidingWindow(CCoinsSelectionSlidingWindow&&) = delete;

    //! Deleted assignment operator
    CCoinsSelectionSlidingWindow& operator=(const CCoinsSelectionSlidingWindow&) = delete;

    //! Deleted move operator (deletion not needed, but better to be explicit)
    CCoinsSelectionSlidingWindow& operator=(CCoinsSelectionSlidingWindow&&) = delete;

    //! Destructor
    ~CCoinsSelectionSlidingWindow();

    //! Method for synchronously running the solving routine with "Sliding Window" strategy
    void Solve() override;
};

/* ---------- CCoinsSelectionSlidingWindow ---------- */

/* ---------- CCoinsSelectionBranchAndBound ---------- */

//! "Branch & Bound" implementation of algorithm of coins selection
/*!
  This class provides a specific implementation of the solving routine.
  In this implementation, a binary tree is considered as the combination of excluding/including each coin.
  This would lead to a number of combinations equal to 2^problemDimension with a brute force strategy.
  The algorithm doesn't rely on simple brute force strategy, instead two additional aspects are taken into account for
  speeding up the algorithm and avoiding exploring branches which would not give an improved solution (with respect to
  the temporary optimal one): backtracking and bounding.
  Starting with an "all coins unselected" setup, the algorithm recursively explores the tree (from biggest coin towards
  smallest coin) opening two new branches, the first one excluding the current coin, the second one including the current
  coin; when a leaf is reached, the output variables are checked to identify if an improved solution (with respect to
  the temporary optimal one) is found and eventually marked as the new temporary optimal solution.
  The tree actual exploration differs very significantly from the tree full exploration thanks to:
  +] backtracking (1): given that at a certain recursion, including a new coin would automatically increase both the
     temporary total amount as well as the temporary total size, if during the tree exploration the two upper-limit
     constraints associated to target amount plus offset and to total size are broken then all the branches from the
     current recursion on are cut; this is done in order to avoid reaching leaves that would certainly be not admissible
     with respect to these two constraints,
  +] backtracking (2): given that at a certain recursion, the highest total amount reachable is computed as the sum of
     current total amount and of all the amounts of coins from the current recursion on, if during the tree exploration
     this sum does not exceed the lower-limit associated to target amount then all the branches from the current recursion
     on are cut; this is done in order to avoid reaching leaves that would certainly be not admissible with respect to this
     constraint,
  +] bounding: given that at a certain recursion, the highest total selection reachable is computed as the sum of current
     total selection and of the quantity of coins from the current recursion on, if during tree exploration this sum does
     not exceed the temporary optimal solution (ties are handled prioritizing low total amount) then all the branches from
     the current recursion on are cut; this is done in order to avoid reaching leaves that would certainly not improve the
     temporary optimal solution.
*/
class CCoinsSelectionBranchAndBound : public CCoinsSelectionAlgorithmBase
{
  //! The timeout check period (in order to avoid checking too frequently)  
  static constexpr int TIMEOUT_CHECK_PERIOD = 10;

protected:
    // ---------- auxiliary
    //! The array of cumulative amounts (considered summing amounts from index to end of amounts array)
    const std::vector<CAmount> cumulativeAmountsForward;
    // ---------- auxiliary

    // ---------- profiling
    #if COINS_SELECTION_ALGORITHM_PROFILING
    //! Counter for keeping track of the number of recursions the solving routine has performed
    uint64_t recursions = 0;

    //! Counter for keeping track of the number of nodes reached by the solving routine
    uint64_t reachedNodes = 0;

    //! Counter for keeping track of the number of leaves reached by the solving routine
    uint64_t reachedLeaves = 0;
    #endif
    // ---------- profiling

private:
    //! Method for preparing array of cumulative amounts
    /*!
      \return the array of cumulative amounts
    */
    std::vector<CAmount> PrepareCumulativeAmountsForward();

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
    /*!
      \return the flag representing if the reset was actually done
    */
    bool Reset() override;

public:
    //! Constructor
    /*!
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
      \param _executionTimeoutMilliseconds timeout for completing solving routine (in milliseconds) (default to 0)
    */
    CCoinsSelectionBranchAndBound(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                                  CAmount _targetAmount,
                                  CAmount _targetAmountPlusOffset,
                                  size_t _availableTotalSize,
                                  uint64_t _executionTimeoutMilliseconds = 0);

    //! Deleted copy constructor
    CCoinsSelectionBranchAndBound(const CCoinsSelectionBranchAndBound&) = delete;

    //! Deleted move constructor (deletion not needed, but better to be explicit)
    CCoinsSelectionBranchAndBound(CCoinsSelectionBranchAndBound&&) = delete;

    //! Deleted assignment operator
    CCoinsSelectionBranchAndBound& operator=(const CCoinsSelectionBranchAndBound&) = delete;

    //! Deleted move operator (deletion not needed, but better to be explicit)
    CCoinsSelectionBranchAndBound& operator=(CCoinsSelectionBranchAndBound&&) = delete;

    //! Destructor
    ~CCoinsSelectionBranchAndBound();

    //! Method for synchronously running the solving routine with "Branch & Bound" strategy
    void Solve() override;
};

/* ---------- CCoinsSelectionBranchAndBound ---------- */

/* ---------- CCoinsSelectionForNotes ---------- */

//! "For Notes" implementation of algorithm of coins selection
/*!
  The implementation details of this method are stricly connected to the implementation of AsyncRPCOperation_sendmany::main_impl() as for commit "d1104ef903147338692344069e30c666d8b78614"

  This class provides a specific implementation of the solving routine.
  A crucial consideration is that, unlike coins selection, the selection of a note doesn't give an independent contribution
  to overall selection size; indeed, from an iteration point of view, each selection of a note actually adds size only if
  it triggers the insertion of a new joinsplit; furthermore, from a global point of view, the overall selection of notes
  may require a number of joinsplits that is lower than the number of joinsplits that is requested by the recipients, hence
  the overall size has to be updated accordingly.
  In this implementation notes are iteratively added to (or removed from) current selection set starting from lowest
  amount note and proceeding towards highest amount note.
  At each iteration the algorithm pushes in the next note and check if a new joinsplit has to be included, eventually
  updating the overall selection size accordingly; if the target amount plus offset and available total size (eventually
  increased by mandatory joinsplits to be included for satisfying outputs amounts) constraints (upper-limit) are not met,
  the algorithm restarts with a new search excluding the very first note used within last search; then the algorithm checks
  if the target amount constraint (lower-limit) is met; if it is not met, the algorithm continues with next note insertion,
  otherwise it marks the finding of an admissible solution and performs additonal insertions until one of the upper-limit
  constraints is broken (and thus removing the just inserted note) or the set of available notes is empty, eventually
  setting the best selection set.
*/
class CCoinsSelectionForNotes : public CCoinsSelectionAlgorithmBase
{
protected:
    // ---------- profiling
    #if COINS_SELECTION_ALGORITHM_PROFILING
    //! Counter for keeping track of the number of iterations the solving routine has performed
    uint64_t iterations = 0;
    #endif
    // ---------- profiling

    // ---------- input variables
    //! Number of joinsplits outputs amounts
    const unsigned int numberOfJoinsplitsOutputsAmounts;

    //! Joinsplits outputs amounts
    std::vector<CAmount> joinsplitsOutputsAmounts;
    // ---------- input variables

protected:
    //! Method for resetting internal variables (must be called before restarting the algorithm)
    /*!
      \return the flag representing if the reset was actually done
    */
    bool Reset() override;

public:
    //! Constructor
    /*!
      \param _amountsAndSizes vector of pairs of amounts and sizes of the elements
      \param _targetAmount target amount to satisfy (it is a lower-limit constraint)
      \param _targetAmountPlusOffset target amount plus a positive offset (it is an upper-limit constraint)
      \param _availableTotalSize available total size (in terms of bytes, it is an upper-limit constraint)
      \param _executionTimeoutMilliseconds timeout for completing solving routine (in milliseconds) (default to 0)
      \param _joinsplitsOutputsAmount amounts of joinsplits outputs (order matters) (default to empty vector)
    */
    CCoinsSelectionForNotes(std::vector<std::pair<CAmount, size_t>> _amountsAndSizes,
                            CAmount _targetAmount,
                            CAmount _targetAmountPlusOffset,
                            size_t _availableTotalSize,
                            uint64_t _executionTimeoutMilliseconds = 0,
                            std::vector<CAmount> _joinsplitsOutputsAmounts = std::vector<CAmount>());

    //! Deleted copyconstructor
    CCoinsSelectionForNotes(const CCoinsSelectionForNotes&) = delete;

    //! Deleted move constructor (deletion not needed, but better to be explicit)
    CCoinsSelectionForNotes(CCoinsSelectionForNotes&&) = delete;

    //! Deleted assignment operator
    CCoinsSelectionForNotes& operator=(const CCoinsSelectionForNotes&) = delete;

    //! Deleted move operator (deletion not needed, but better to be explicit)
    CCoinsSelectionForNotes& operator=(CCoinsSelectionForNotes&&) = delete;

    //! Destructor
    ~CCoinsSelectionForNotes();

    //! Method for synchronously running the solving routine with "Sliding Window" strategy
    void Solve() override;
};

/* ---------- CCoinsSelectionForNotes ---------- */

#endif // _COINS_SELECTION_ALGORITHM_H