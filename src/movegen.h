#pragma once

#include "position.h"

enum GenType { GEN_NOISY, GEN_QUIET, GEN_ALL };

struct ScoredMove {
    Move move;
    int score;
};

struct MoveList {
    ScoredMove moves[MAX_MOVES];
    int count = 0;

    void add(Move m) { moves[count++].move = m; }
    int size() const { return count; }
    Move operator[](int i) const { return moves[i].move; }
    bool contains(Move m) const {
        for (int i = 0; i < count; ++i)
            if (moves[i].move == m) return true;
        return false;
    }
};

template<GenType T>
void generate(const Position& pos, MoveList& list);

void generate_legal(const Position& pos, MoveList& list);
