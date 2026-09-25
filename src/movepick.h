#pragma once

#include "history.h"
#include "movegen.h"

class MovePicker {
public:
    enum Stage {
        TT_MOVE, GEN_NOISY, GOOD_NOISY, GEN_QUIETS, GOOD_QUIETS, BAD_NOISY, BAD_QUIETS,
        EVASION_TT, EVASION_GEN, EVASIONS,
        QS_TT, QS_GEN, QS_NOISY,
        PC_TT, PC_GEN, PC_NOISY,
        DONE
    };

    // main search
    MovePicker(const Position& p, Move ttm, Histories& h, const PieceToHistory** ch, Bitboard threats, int ply, int depth);
    // quiescence
    MovePicker(const Position& p, Move ttm, Histories& h, const PieceToHistory** ch, Bitboard threats);
    // probcut
    MovePicker(const Position& p, Move ttm, int threshold, Histories& h);

    Move next(bool skipQuiets = false);
    Stage stage() const { return stg; }

private:
    void score_noisy();
    void score_quiets();
    void score_evasions();
    Move select_best(int from, int to);

    const Position& pos;
    Histories& hist;
    const PieceToHistory** contHist;
    Move ttMove;
    Bitboard threats;
    int ply, depth, threshold;
    Stage stg;

    MoveList list;
    int cur = 0, end = 0;
    ScoredMove bad[MAX_MOVES];
    int badCount = 0, badCur = 0;
};
