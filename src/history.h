#pragma once

#include <algorithm>
#include <cstring>

#include "position.h"

constexpr int MAIN_HIST_MAX = 7183;
constexpr int CAPT_HIST_MAX = 10692;
constexpr int CONT_HIST_MAX = 30000;
constexpr int PAWN_HIST_MAX = 8192;
constexpr int LOWPLY_HIST_MAX = 7183;
constexpr int CORR_MAX = 1024;

constexpr int PAWN_HIST_SIZE = 8192;
constexpr int CORR_SIZE = 16384;
constexpr int LOW_PLY = 5;

template<int Max>
inline void gravity(i16& h, int bonus) {
    bonus = std::clamp(bonus, -Max, Max);
    h += bonus - h * std::abs(bonus) / Max;
}

using PieceToHistory = i16[PIECE_NB][SQUARE_NB];

struct Histories {
    i16 main[COLOR_NB][2][2][SQUARE_NB * SQUARE_NB];
    i16 lowPly[LOW_PLY][SQUARE_NB * SQUARE_NB];
    i16 capture[PIECE_NB][SQUARE_NB][6];
    PieceToHistory cont[2][2][PIECE_NB][SQUARE_NB];
    i16 pawn[PAWN_HIST_SIZE][PIECE_NB][SQUARE_NB];

    i16 pawnCorr[COLOR_NB][CORR_SIZE];
    i16 nonPawnCorr[COLOR_NB][COLOR_NB][CORR_SIZE];
    i16 minorCorr[COLOR_NB][CORR_SIZE];
    i16 majorCorr[COLOR_NB][CORR_SIZE];
    PieceToHistory contCorr[PIECE_NB][SQUARE_NB];

    template<typename T>
    static void fill(T& arr, i16 v) {
        i16* p = reinterpret_cast<i16*>(&arr);
        std::fill(p, p + sizeof(T) / sizeof(i16), v);
    }

    void clear() {
        fill(main, -5);
        fill(lowPly, 102);
        fill(capture, -742);
        fill(cont, -586);
        fill(pawn, -1338);
        fill(pawnCorr, 0);
        fill(nonPawnCorr, 0);
        fill(minorCorr, 0);
        fill(majorCorr, 0);
        fill(contCorr, 0);
    }

    void new_search() {
        i16* p = reinterpret_cast<i16*>(&main);
        for (size_t i = 0; i < sizeof(main) / sizeof(i16); ++i) p[i] = i16(p[i] * 729 / 1024);
        fill(lowPly, 102);
    }

    i16& main_entry(Color c, Move m, Bitboard threats) {
        return main[c][bool(threats & square_bb(m.from()))][bool(threats & square_bb(m.to()))][m.from_to()];
    }
    i16& capture_entry(const Position& pos, Move m) {
        PieceType cap = pos.captured_type(m);
        return capture[pos.moved_piece(m)][m.to()][cap == NO_PIECE_TYPE ? PAWN : cap];
    }
    i16& pawn_entry(const Position& pos, Move m) {
        return pawn[pos.pawn_key() & (PAWN_HIST_SIZE - 1)][pos.moved_piece(m)][m.to()];
    }
};
