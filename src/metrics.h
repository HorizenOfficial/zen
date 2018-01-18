// Copyright (c) 2016 The Zcash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uint256.h"

#include <atomic>
#include <mutex>
#include <string>

struct AtomicCounter {
    std::atomic<uint64_t> value;

    AtomicCounter() : value {0} { }

    void increment(){
        ++value;
    }

    void decrement(){
        --value;
    }

    int get() const {
        return value.load();
    }
};

class AtomicTimer {
private:
    std::mutex mtx;
    uint64_t threads;
    int64_t start_time;
    int64_t total_time;

public:
    AtomicTimer() : threads(0), start_time(0), total_time(0) {}

    /**
     * Starts timing on first call, and counts the number of calls.
     */
    void start();

    /**
     * Counts number of calls, and stops timing after it has been called as
     * many times as start().
     */
    void stop();

    bool running();

    uint64_t threadCount();

    double rate(const AtomicCounter& count);
};

extern AtomicCounter transactionsValidated;
extern AtomicCounter ehSolverRuns;
extern AtomicCounter solutionTargetChecks;
extern AtomicTimer miningTimer;

void TrackMinedBlock(uint256 hash);

void MarkStartTime();
double GetLocalSolPS();
int EstimateNetHeightInner(int height, int64_t tipmediantime,
                           int heightLastCheckpoint, int64_t timeLastCheckpoint,
                           int64_t genesisTime, int64_t targetSpacing);

void TriggerRefresh();

void ConnectMetricsScreen();
void ThreadShowMetricsScreen();

/**
 * Rendering options:
 * Zcash: img2txt -W 40 -H 20 -f utf8 -d none -g 0.7 Z-yellow.orange-logo.png
 * Heart: img2txt -W 40 -H 20 -f utf8 -d none 2000px-Heart_corazón.svg.png
 */
// ZEN_MOD_START
const std::string METRICS_ART =

"                             [0;1;37;97;47m;S[0m                                                                \n"
"                         [0;1;37;97;47mS[0;1;30;90;47m:%%%%%t[0;1;37;97;47m;[0m                                                             \n"
"                       [0;1;31;91;43m8[0;37;43m@[0;33;47m8[0;1;30;90;47m%%%%%%%S[0;33;5;40;100m.@[0m                                                           \n"
"                       [0;33;5;41;101m::;[0;1;31;91;43m8[0;37;43mS[0;33;47m8[0;1;30;90;47m%8[0;33;5;40;100mt[0;30;5;40;100m@[0;31;40m;[0;32;40m..[0;1;30;90;47m8[0m                                                          \n"
"                       [0;33;5;41;101m;:::::[0;31;43m@[0;32;40m......[0m                                                           \n"
"                       [0;33;5;41;101m;:::::[0;31;43m@[0;32;40m......[0m    [0;32;40m.............[0m     [0;32;40m............[0m    [0;32;40m.....[0m         [0;32;40m...[0;1;30;90;40m8[0m   \n"
"        [0;1;37;97;47m;[0;1;30;90;47mt[0;1;37;97;47m.[0m          [0;37;5;40;100mX[0;1;30;90;40m8[0;33;5;41;101m;:::::[0;31;43m@[0;32;40m...;[0;33;5;40;100m [0m              [0;32;40m....[0m     [0;32;40m....[0m            [0;32;40m......[0m        [0;32;40m...[0;1;30;90;40m8[0m   \n"
"     [0;1;37;97;47m [0;1;30;90;47m%%%%%%%:[0;1;37;97;47m%[0m   [0;33;5;40;100m.[0;32;40m;...[0;33;5;41;101m;:::::[0;31;43m@[0;30;5;40;100m8[0;37;5;40;100m@[0m                [0;32;40m....[0m      [0;32;40m...[0m             [0;32;40m........[0m      [0;32;40m...[0;1;30;90;40m8[0m   \n"
"   [0;1;31;91;43m88[0;33;47m8[0;1;30;90;47m%%%%%%%[0;33;5;40;100m X[0;1;30;90;40m@[0;32;40m.......[0;33;5;41;101m::::::[0;33;47m8[0m                [0;32;40m....[0m        [0;32;40m...[0m             [0;32;40m....[0m [0;32;40m....[0m     [0;32;40m...[0;1;30;90;40m8[0m   \n"
"   [0;33;5;41;101m:::;[0;1;31;91;43m8[0;37;43m@[0;33;47m8[0;33;5;40;100mt[0;31;5;40;100mS[0;31;40mt[0;32;40m..........[0;33;5;41;101m;:::::[0;1;33;93;47m8[0m               [0;32;40m....[0m         [0;32;40m...........[0m     [0;32;40m....[0m  [0;32;40m....[0m    [0;32;40m...[0;1;30;90;40m8[0m   \n"
"   [0;33;5;41;101m::::::[0;31;43m8[0;32;40m.........[0;31;40m;[0;31;5;40;100m@[0;33;5;40;100mt[0;1;30;90;47m@S[0;37;43m8[0;1;31;91;43m8[0;33;5;41;101m;::[0;33;47m8[0m              [0;32;40m....[0m          [0;32;40m...........[0m     [0;32;40m....[0m    [0;32;40m....[0m  [0;32;40m...[0;1;30;90;40m8[0m   \n"
"   [0;33;5;41;101m;:::::[0;31;43m8[0;32;40m......[0;1;30;90;40m8[0;33;5;40;100mX [0;1;30;90;47mS%%%%%%%[0;33;47m8[0;37;43mX[0;33;47m8[0m            [0;32;40m....[0m            [0;32;40m...[0m             [0;32;40m....[0m     [0;32;40m....[0m [0;32;40m...[0;1;30;90;40m8[0m   \n"
"     [0;1;33;93;47m8[0;1;31;91;43m8[0;33;5;41;101m::[0;31;43m8[0;32;40m..:[0;33;5;40;100m:[0m    [0;1;37;97;47mS[0;1;30;90;47m.%%%%%%%%%%t[0;1;37;97;47m:[0m         [0;32;40m....[0m             [0;32;40m...[0m             [0;32;40m....[0m       [0;32;40m......[0;1;30;90;40m8[0m   \n"
"         [0;1;30;90;43m8[0;37;5;40;100mS[0m           [0;1;37;97;47m;[0;33;47m8[0;1;30;90;47m%%%%%%%%%%.[0;1;37;97;47mS[0m    [0;32;40m....[0m              [0;32;40m....[0m            [0;32;40m....[0m        [0;32;40m.....[0;1;30;90;40m8[0m   \n"
"                       [0;33;5;41;101m:[0;1;31;91;43m8[0;37;43mX[0;33;47m8[0;1;30;90;47m%%%%%8[0;33;5;40;100mt[0;30;5;40;100m@[0;31;40m:[0;37;5;40;100m8[0m  [0;1;30;90;40m8[0;32;40m..............[0m    [0;32;40m............[0m    [0;32;40m....[0m          [0;32;40m...[0;1;30;90;40m8[0m   \n"
"                       [0;33;5;41;101m::::;[0;1;31;91;43m8[0;1;30;90;43m8[0;33;5;40;100mX[0;1;30;90;40m8[0;32;40m....[0m                                                           \n"
"                       [0;33;5;41;101m::::::[0;31;43m@[0;32;40m......[0m                                                           \n"
"                       [0;1;33;93;43m.[0;33;5;41;101m;::::[0;31;43m@[0;32;40m.....t[0m                                                           \n"
"                          [0;1;33;93;47m8[0;33;5;41;101m;:[0;31;43m@[0;32;40m..[0;30;5;40;100m8[0m                                                              \n"
"                                                                                               \n";
// ZEN_MOD_END
