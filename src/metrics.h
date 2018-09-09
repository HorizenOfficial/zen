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
// ZEN MOD START
 * Logo: img2txt -W 60 -H 30 -f utf8 -d none -g 0.7 Horizen-logo.png > horizen.utf8
 */
const std::string METRICS_ART =

"                        [0;34;40m;ttt%%%%ttt;[0m                        \n"
"                  [0;34;40mt%SSSSSSSSSSSSSSSSSSSS%t[0m                  \n"
"              [0;34;40mt%SSSSSSSSSSSSSSSSSSSSSSSSSSSS%t[0m              \n"
"           [0;34;40m;%SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSt[0m              \n"
"         [0;34;40mtSSSSSSSSSSSSS%t;[0m        [0;34;40m;t%SSSS%t[0m                 \n"
"       [0;34;40m;SSSSSSSSSSSt[0m                                        \n"
"      [0;34;40m%SSSSSSSSSt[0m                                           \n"
"     [0;34;40mSSSSSSSSSt[0m                                      [0;32;46m%[0;1;32;92;46m8[0;32;46m8[0m    \n"
"   [0;34;40m;SSSSSSSS%[0m          [0;34;40mt%SSSSSSSSSS%t[0m               [0;1;32;92;46m8888[0;32;46m8[0m   \n"
"   [0;34;40mSSSSSSSSt[0m        [0;34;40mtSSSSSSSSSSSSSSSSSSt[0m          [0;32;46m@[0;1;32;92;46m888888[0;32;46m8[0m  \n"
"  [0;34;40mSSSSSSSSt[0m       [0;34;40mtSSSSSSSSSSSSSSSSSSSSSSt[0m        [0;1;32;92;46m88888888[0m  \n"
" [0;34;40mtSSSSSSSt[0m       [0;34;40m%SSSSSSSSSSSSSSSSSSSSSSSSS[0m       [0;32;46m8[0;1;32;92;46m8888888[0;32;46mt[0m \n"
" [0;34;40mSSSSSSSS[0m       [0;34;40m%SSSSSSSSSSSSSSSSSSSSSSSSSSS[0m       [0;1;30;90;46mt[0;1;32;92;46m8888[0;1;30;90;46mt;;[0m \n"
" [0;34;40mSSSSSSSt[0m       [0;34;40mSSSSSSSSSSSSSSSSSSSSSSSSSSSSt[0m      [0;32;46m@[0;1;30;90;46m;;;;;;;[0;32;46m8[0m \n"
"[0;34;40mtSSSSSSS;[0m      [0;34;40m%SSSSSSSSSSSSSSSS%%%ttttt%%%S%[0m       [0;1;30;90;46m;;;;;;;:[0m \n"
"[0;34;40mtSSSSSSS;[0m      [0;34;40m%SSSSSSSSS%;[0m                         [0;1;30;90;46m;;;;;;;:[0m \n"
" [0;34;40mSSSSSSSt[0m     [0;34;40mtSSSSS%t[0m       [0;1;30;90;46m:::.:::::::::::[0m       [0;1;30;90;46m:;;;;;;;:[0m \n"
" [0;34;40mSSSSSSSS[0m   [0;34;40m%SSSS%[0m      [0;1;30;90;46m..[0;1;36;96;46mttt%%%%SSSSSS[0;1;30;90;46m:::::[0m       [0;1;30;90;46m::::::::[0m \n"
" [0;34;40m%SSSSSSSt%SSS%[0m     [0;34;46mS[0;1;36;96;46m.::;;;;ttt%%%%SSSSSSS[0;1;30;90;46m:[0m        [0;1;30;90;46m::::::::[0m \n"
"  [0;34;40mSSSSSSSSSS%[0m     [0;34;46mS[0;1;36;96;46m...::::;;;;tttt%%%%SSS[0;1;30;90;46m.[0m        [0;1;30;90;46m::::::::[0m  \n"
"  [0;34;40m;SSSSSSS%[0m         [0;34;46mt[0;1;36;96;46m...::::;;;;tttt%%%[0;1;30;90;46m.[0m         [0;1;30;90;46m:[0;1;36;96;46mSSSSSSS[0;1;30;90;46m:[0m  \n"
"   [0;34;40mtSSSSS[0m              [0;34;46m%[0;1;34;94;46m8[0;1;36;96;46m.::::;;;;;;[0;1;30;90;46m..[0m         [0;1;30;90;46m:[0;1;36;96;46m%SSSSSSS[0;1;30;90;46m.[0m   \n"
"    [0;34;40mtSS%[0m                     [0;34;46m88[0m               [0;1;30;90;46m.[0;1;36;96;46m%%%%%%%%[0;1;30;90;46m:[0m    \n"
"      [0;34;40mt[0m                                     [0;1;30;90;46m.[0;1;36;96;46mttttttttt[0;1;30;90;46m.[0m     \n"
"                                         [0;1;30;90;46m.[0;1;36;96;46m;;;;;;;;;;[0;1;30;90;46m.[0m       \n"
"                  [0;34;46mS[0;1;34;94;46m888[0;34;46m;X8[0m           [0;34;46mX;[0;1;36;96;46m.:::::::::;;[0;1;30;90;46m.[0m         \n"
"               [0;34;46mS[0;1;34;94;46m88888888888888888888[0;1;36;96;46m........::::[0;34;46m%[0m           \n"
"              [0;34;46mS[0;1;34;94;46m88888888888888888888888888[0;1;36;96;46m....[0;34;46mt[0m              \n"
"                 [0;34;46m8%[0;1;34;94;46m8888888888888888888888[0;34;46mt8[0m                 \n"
"                       [0;34;46m8XSt;[0;1;34;94;46m8888[0;34;46m;t%X8[0m                       \n";
// ZEN MOD END
