#include "eval.h"

namespace {

constexpr int MgValue[6] = {82, 337, 365, 477, 1025, 0};
constexpr int EgValue[6] = {94, 281, 297, 512, 936, 0};
constexpr int Phase[6] = {0, 1, 1, 2, 4, 0};

int center_dist(Square s) {
    int f = file_of(s), r = rank_of(s);
    int df = f < 4 ? 3 - f : f - 4;
    int dr = r < 4 ? 3 - r : r - 4;
    return df + dr;
}

} // namespace

Value Eval::evaluate_fresh(const Position& pos) {
    int mg[2] = {0, 0}, eg[2] = {0, 0}, phase = 0;
    for (Color c : {WHITE, BLACK}) {
        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard b = pos.pieces(c, PieceType(pt));
            while (b) {
                Square s = pop_lsb(b);
                Square rs = relative_square(c, s);
                int cd = center_dist(s);
                mg[c] += MgValue[pt];
                eg[c] += EgValue[pt];
                phase += Phase[pt];
                switch (pt) {
                case PAWN:
                    mg[c] += (rank_of(rs) - 1) * 4 - (cd > 2 ? 0 : -8);
                    eg[c] += (rank_of(rs) - 1) * 12;
                    break;
                case KNIGHT:
                    mg[c] -= cd * 8;
                    eg[c] -= cd * 6;
                    break;
                case BISHOP:
                case QUEEN:
                    mg[c] -= cd * 3;
                    eg[c] -= cd * 3;
                    break;
                case ROOK:
                    mg[c] += rank_of(rs) == 6 ? 20 : 0;
                    break;
                case KING:
                    mg[c] -= rank_of(rs) * 12 + (cd < 3 ? 10 : 0);
                    eg[c] -= cd * 10;
                    break;
                }
            }
        }
        if (popcount(pos.pieces(c, BISHOP)) >= 2) mg[c] += 30, eg[c] += 50;
    }
    phase = std::min(phase, 24);
    const Color us = pos.side_to_move();
    int mgs = mg[us] - mg[~us], egs = eg[us] - eg[~us];
    return (mgs * phase + egs * (24 - phase)) / 24 + 12;
}

Value Eval::Evaluator::evaluate(const Position& pos) { return evaluate_fresh(pos); }
