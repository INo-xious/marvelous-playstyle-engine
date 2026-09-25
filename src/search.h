#pragma once

#include <chrono>
#include <vector>

#include "position.h"

namespace Search {

inline i64 now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct Limits {
    i64 time[COLOR_NB] = {0, 0};
    bool hasTime[COLOR_NB] = {false, false};
    i64 inc[COLOR_NB] = {0, 0};
    i64 movetime = 0;
    i64 start = 0;
    int movestogo = 0;
    int depth = 0;
    int mate = 0;
    int multiPV = 1;
    int moveOverhead = 30;
    u64 nodes = 0;
    u64 softNodes = 0;
    bool infinite = false;
    bool ponder = false;
    bool silent = false;
    std::vector<Move> searchmoves;

    bool use_time() const { return hasTime[WHITE] || hasTime[BLACK] || movetime; }
};

struct Result {
    Move best = Move::none();
    Move ponder = Move::none();
    Value score = VALUE_NONE;
    int depth = 0;
    u64 nodes = 0;
};

extern bool showWDL;

void init();
void set_threads(int n);
void new_game();
void start(const Position& pos, const Limits& limits);
void stop();
void ponderhit();
void wait();
void shutdown();
void finish();
Result last_result();

} // namespace Search
