#include "movepick.h"

namespace {

constexpr int NoisyValue[7] = {100, 300, 300, 500, 900, 0, 0};
constexpr int ThreatValue[7] = {0, 781, 825, 1276, 2538, 0, 0};
constexpr int GoodQuietThreshold = -14000;
constexpr int QuietSortLimit = -3560;

} // namespace

MovePicker::MovePicker(const Position& p, Move ttm, Histories& h, const PieceToHistory** ch, Bitboard thr, int pl, int d)
    : pos(p), hist(h), contHist(ch), ttMove(ttm), threats(thr), ply(pl), depth(d), threshold(0) {
    const bool ok = ttm && pos.pseudo_legal(ttm);
    if (pos.in_check()) stg = ok ? EVASION_TT : EVASION_GEN;
    else stg = ok ? TT_MOVE : GEN_NOISY;
    if (!ok) ttMove = Move::none();
}

MovePicker::MovePicker(const Position& p, Move ttm, Histories& h, const PieceToHistory** ch, Bitboard thr)
    : pos(p), hist(h), contHist(ch), ttMove(ttm), threats(thr), ply(LOW_PLY), depth(0), threshold(0) {
    if (pos.in_check()) {
        const bool ok = ttm && pos.pseudo_legal(ttm);
        stg = ok ? EVASION_TT : EVASION_GEN;
        if (!ok) ttMove = Move::none();
    } else {
        const bool ok = ttm && pos.is_noisy(ttm) && pos.pseudo_legal(ttm);
        stg = ok ? QS_TT : QS_GEN;
        if (!ok) ttMove = Move::none();
    }
}

MovePicker::MovePicker(const Position& p, Move ttm, int thr, Histories& h)
    : pos(p), hist(h), contHist(nullptr), ttMove(ttm), threats(0), ply(LOW_PLY), depth(0), threshold(thr) {
    const bool ok = ttm && pos.is_noisy(ttm) && pos.pseudo_legal(ttm) && pos.see_ge(ttm, thr);
    stg = ok ? PC_TT : PC_GEN;
    if (!ok) ttMove = Move::none();
}

void MovePicker::score_noisy() {
    for (int i = cur; i < end; ++i) {
        Move m = list.moves[i].move;
        PieceType cap = pos.captured_type(m);
        int s = 16 * NoisyValue[cap == NO_PIECE_TYPE ? 6 : cap] + hist.capture_entry(pos, m);
        if (m.type() == PROMOTION) s += m.promotion() == QUEEN ? 16384 : -32768;
        list.moves[i].score = s;
    }
}

void MovePicker::score_quiets() {
    const Color us = pos.side_to_move(), them = ~us;
    const Bitboard occ = pos.pieces();
    const Square ksq = pos.king_sq(them);

    Bitboard pawnAtt = them == WHITE ? pawn_attacks_bb<WHITE>(pos.pieces(them, PAWN)) : pawn_attacks_bb<BLACK>(pos.pieces(them, PAWN));
    Bitboard minorAtt = pawnAtt;
    for (Bitboard b = pos.pieces(them, KNIGHT); b;) minorAtt |= KnightAttacks[pop_lsb(b)];
    for (Bitboard b = pos.pieces(them, BISHOP); b;) minorAtt |= bishop_attacks(pop_lsb(b), occ);
    Bitboard rookAtt = minorAtt;
    for (Bitboard b = pos.pieces(them, ROOK); b;) rookAtt |= rook_attacks(pop_lsb(b), occ);
    const Bitboard byLesser[7] = {0, pawnAtt, pawnAtt, minorAtt, rookAtt, 0, 0};

    const Bitboard bishopChecks = bishop_attacks(ksq, occ), rookChecks = rook_attacks(ksq, occ);
    const Bitboard checkSq[7] = {PawnAttacks[them][ksq], KnightAttacks[ksq], bishopChecks, rookChecks, bishopChecks | rookChecks, 0, 0};

    for (int i = cur; i < end; ++i) {
        Move m = list.moves[i].move;
        Piece pc = pos.moved_piece(m);
        PieceType pt = type_of(pc);
        Square to = m.to();

        int s = 2 * hist.main_entry(us, m, threats);
        s += 2 * hist.pawn_entry(pos, m);
        s += (*contHist[0])[pc][to];
        s += (*contHist[1])[pc][to];
        s += (*contHist[2])[pc][to];
        s += (*contHist[3])[pc][to];
        s += (*contHist[5])[pc][to];

        if ((checkSq[pt] & square_bb(to)) && pos.see_ge(m, -25)) s += 16384;

        s += ThreatValue[pt] * 20 * (int(bool(byLesser[pt] & square_bb(m.from()))) - int(bool(byLesser[pt] & square_bb(to))));

        if (ply < LOW_PLY) s += 8 * hist.lowPly[ply][m.from_to()] / (1 + ply);
        if (m.type() == PROMOTION) s -= 100000;
        list.moves[i].score = s;
    }
}

void MovePicker::score_evasions() {
    const Color us = pos.side_to_move();
    for (int i = cur; i < end; ++i) {
        Move m = list.moves[i].move;
        if (pos.is_capture(m)) {
            PieceType cap = pos.captured_type(m);
            list.moves[i].score = (1 << 28) + NoisyValue[cap] * 16 - type_of(pos.moved_piece(m));
        } else {
            Piece pc = pos.moved_piece(m);
            list.moves[i].score = hist.main_entry(us, m, threats) + (*contHist[0])[pc][m.to()];
        }
    }
}

Move MovePicker::select_best(int from, int to) {
    int best = from;
    for (int i = from + 1; i < to; ++i)
        if (list.moves[i].score > list.moves[best].score) best = i;
    std::swap(list.moves[from], list.moves[best]);
    return list.moves[from].move;
}

Move MovePicker::next(bool skipQuiets) {
    while (true) {
        switch (stg) {
        case TT_MOVE:
        case EVASION_TT:
        case QS_TT:
        case PC_TT:
            stg = Stage(stg + 1);
            return ttMove;

        case GEN_NOISY:
        case QS_GEN:
        case PC_GEN:
            list.count = 0;
            generate<GenType::GEN_NOISY>(pos, list);
            cur = 0, end = list.count;
            score_noisy();
            stg = Stage(stg + 1);
            break;

        case GOOD_NOISY:
            while (cur < end) {
                Move m = select_best(cur, end);
                int score = list.moves[cur].score;
                ++cur;
                if (m == ttMove) continue;
                bool under = m.type() == PROMOTION && m.promotion() != QUEEN;
                if (!under && pos.see_ge(m, -score / 32)) return m;
                bad[badCount++] = {m, score};
            }
            stg = GEN_QUIETS;
            break;

        case GEN_QUIETS:
            if (skipQuiets) {
                cur = end = 0;
                stg = BAD_NOISY;
                break;
            }
            list.count = 0;
            generate<GenType::GEN_QUIET>(pos, list);
            cur = 0, end = list.count;
            score_quiets();
            {
                const int limit = QuietSortLimit * depth;
                for (int s = 1, sortedEnd = 0; s < end; ++s)
                    if (list.moves[s].score >= limit) {
                        ScoredMove tmp = list.moves[s];
                        list.moves[s] = list.moves[++sortedEnd];
                        int j = sortedEnd;
                        for (; j > 0 && list.moves[j - 1].score < tmp.score; --j) list.moves[j] = list.moves[j - 1];
                        list.moves[j] = tmp;
                    }
            }
            stg = GOOD_QUIETS;
            break;

        case GOOD_QUIETS:
            if (!skipQuiets)
                while (cur < end) {
                    const ScoredMove& sm = list.moves[cur++];
                    if (sm.move != ttMove && sm.score > GoodQuietThreshold) return sm.move;
                }
            stg = BAD_NOISY;
            break;

        case BAD_NOISY:
            while (badCur < badCount) {
                Move m = bad[badCur++].move;
                if (m != ttMove) return m;
            }
            stg = BAD_QUIETS;
            cur = 0;
            break;

        case BAD_QUIETS:
            if (!skipQuiets)
                while (cur < end) {
                    const ScoredMove& sm = list.moves[cur++];
                    if (sm.move != ttMove && sm.score <= GoodQuietThreshold) return sm.move;
                }
            stg = DONE;
            break;

        case EVASION_GEN:
            list.count = 0;
            generate<GenType::GEN_ALL>(pos, list);
            cur = 0, end = list.count;
            score_evasions();
            stg = EVASIONS;
            break;

        case EVASIONS:
        case QS_NOISY:
            while (cur < end) {
                Move m = select_best(cur, end);
                ++cur;
                if (m != ttMove) return m;
            }
            stg = DONE;
            break;

        case PC_NOISY:
            while (cur < end) {
                Move m = select_best(cur, end);
                ++cur;
                if (m != ttMove && pos.see_ge(m, threshold)) return m;
            }
            stg = DONE;
            break;

        case DONE:
            return Move::none();
        }
    }
}
