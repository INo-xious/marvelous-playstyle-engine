#include "search.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include "eval.h"
#include "history.h"
#include "movegen.h"
#include "movepick.h"
#include "tt.h"

namespace Search {
bool showWDL = false;
}

namespace {

using namespace Search;

enum NodeType { NonPV, PV, Root };

constexpr int SeeVal[7] = {100, 300, 300, 500, 900, 0, 0};

int Lmr[2][64][64];

struct Stack {
    Move* pv;
    PieceToHistory* contHist;
    PieceToHistory* contCorr;
    Move move;
    Move excluded;
    Piece movedPiece;
    Value staticEval;
    Bitboard threats;
    int ply;
    int moveCount;
    int cutoffCnt;
    int reduction;
    int statScore;
    bool inCheck;
    bool ttPv;
    bool ttHit;
};

struct RootMove {
    Move move;
    Value score = -VALUE_INFINITE;
    Value prevScore = -VALUE_INFINITE;
    Value avgScore = VALUE_NONE;
    Value uciScore = -VALUE_INFINITE;
    bool lowerbound = false, upperbound = false;
    int selDepth = 0;
    u64 nodes = 0;
    std::vector<Move> pv;

    explicit RootMove(Move m) : move(m), pv{m} {}
    bool operator<(const RootMove& o) const { return score != o.score ? score > o.score : prevScore > o.prevScore; }
};

struct TimeControl {
    std::atomic<i64> start{0};
    i64 soft = 0, hard = 0;
    bool useSoft = false, useHard = false;
};

std::atomic<bool> stopFlag{false};
std::atomic<bool> pondering{false};
Limits limits;
TimeControl tc;
Result lastResult;

int normalize(Value v) { return v; }

std::string score_to_uci(Value v) {
    if (is_decisive(v) && std::abs(v) >= VALUE_MATE_IN_MAX_PLY)
        return "mate " + std::to_string(v > 0 ? (VALUE_MATE - v + 1) / 2 : -(VALUE_MATE + v) / 2);
    if (is_decisive(v)) return "cp " + std::to_string(v > 0 ? 20000 - (VALUE_TB - v) : -20000 + (VALUE_TB + v));
    return "cp " + std::to_string(normalize(v));
}

class Worker {
public:
    explicit Worker(int i) : id(i), hist(std::make_unique<Histories>()) {
        hist->clear();
        std::memset(&sentinelHist, 0, sizeof(sentinelHist));
        std::memset(&sentinelCorr, 0, sizeof(sentinelCorr));
    }

    void prepare(const Position& root, const std::vector<Move>& moves) {
        pos = root;
        rootMoves.clear();
        for (Move m : moves) rootMoves.emplace_back(m);
        nodes.store(0, std::memory_order_relaxed);
        rootDepth = completedDepth = selDepth = pvIdx = nmpMinPly = 0;
        bestMoveChanges = 0;
        stability = 0;
        scoreEma = VALUE_NONE;
        hist->new_search();
        nnue.reset(pos);
    }

    void iterative_deepening();

    int id;
    Position pos;
    Eval::Evaluator nnue;
    std::unique_ptr<Histories> hist;
    std::vector<RootMove> rootMoves;
    std::atomic<u64> nodes{0};
    int rootDepth = 0, completedDepth = 0, selDepth = 0, pvIdx = 0, nmpMinPly = 0;
    int bestMoveChanges = 0;

private:
    template<NodeType NT>
    Value search(Stack* ss, Value alpha, Value beta, int depth, bool cutNode);
    template<NodeType NT>
    Value qsearch(Stack* ss, Value alpha, Value beta);

    Value evaluate() {
        Value v = nnue.evaluate(pos);
        return std::clamp(v, -VALUE_TB_WIN_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
    }
    Value draw_value() const { return VALUE_DRAW - 1 + Value(nodes.load(std::memory_order_relaxed) & 2); }
    int correction(const Stack* ss) const;
    Value corrected(Value raw, int corr) const {
        Value v = raw * (200 - pos.rule50()) / 200 + corr;
        return std::clamp(v, -VALUE_TB_WIN_IN_MAX_PLY + 1, VALUE_TB_WIN_IN_MAX_PLY - 1);
    }
    void update_correction(const Stack* ss, int bonus);
    void update_cont(Stack* ss, Piece pc, Square to, int bonus);
    void update_quiet(Stack* ss, Move m, int bonus);
    void update_all_stats(Stack* ss, Move best, Square prevSq, Move* quiets, int nq, Move* captures, int nc, int depth,
                          Move ttMove, bool pvNode);

    void make_move(Stack* ss, Move m) {
        ss->move = m;
        ss->movedPiece = pos.moved_piece(m);
        ss->contHist = &hist->cont[ss->inCheck][pos.is_noisy(m)][ss->movedPiece][m.to()];
        ss->contCorr = &hist->contCorr[ss->movedPiece][m.to()];
        nodes.store(nodes.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
        pos.make(m);
        nnue.push(pos);
    }
    void unmake_move(Move m) {
        nnue.pop();
        pos.unmake(m);
    }

    void check_time();

public:
    void print_info(int depth) const;

private:
    bool root_has(Move m) const {
        for (size_t i = pvIdx; i < rootMoves.size(); ++i)
            if (rootMoves[i].move == m) return true;
        return false;
    }
    RootMove& root_move(Move m) {
        for (auto& rm : rootMoves)
            if (rm.move == m) return rm;
        return rootMoves[0];
    }
    bool soft_stop(i64 elapsed);

    PieceToHistory sentinelHist;
    PieceToHistory sentinelCorr;
    int callsCnt = 0;
    int stability = 0;
    Move lastBest = Move::none();
    Value scoreEma = VALUE_NONE;
};

std::vector<std::unique_ptr<Worker>> workers;
std::thread mainThread;

u64 total_nodes() {
    u64 n = 0;
    for (auto& w : workers) n += w->nodes.load(std::memory_order_relaxed);
    return n;
}

void update_pv(Move* pv, Move m, const Move* child) {
    *pv++ = m;
    if (child)
        while (*child) *pv++ = *child++;
    *pv = Move::none();
}

int Worker::correction(const Stack* ss) const {
    const Color us = pos.side_to_move();
    const int pawn = hist->pawnCorr[us][pos.pawn_key() & (CORR_SIZE - 1)];
    const int minor = hist->minorCorr[us][pos.minor_key() & (CORR_SIZE - 1)];
    const int npw = hist->nonPawnCorr[us][WHITE][pos.non_pawn_key(WHITE) & (CORR_SIZE - 1)];
    const int npb = hist->nonPawnCorr[us][BLACK][pos.non_pawn_key(BLACK) & (CORR_SIZE - 1)];
    int cont = 80695;
    const Move m = (ss - 1)->move;
    if (m.is_ok()) {
        const Piece pc = (ss - 1)->movedPiece;
        const Square to = m.to();
        cont = 7885 * ((*(ss - 2)->contCorr)[pc][to] + (*(ss - 4)->contCorr)[pc][to]) + 6307 * (*(ss - 6)->contCorr)[pc][to];
    }
    return (13806 * pawn + 9512 * minor + 11615 * (npw + npb) + cont) / 131072;
}

void Worker::update_correction(const Stack* ss, int bonus) {
    const Color us = pos.side_to_move();
    gravity<CORR_MAX>(hist->pawnCorr[us][pos.pawn_key() & (CORR_SIZE - 1)], bonus);
    gravity<CORR_MAX>(hist->minorCorr[us][pos.minor_key() & (CORR_SIZE - 1)], bonus * 150 / 128);
    gravity<CORR_MAX>(hist->nonPawnCorr[us][WHITE][pos.non_pawn_key(WHITE) & (CORR_SIZE - 1)], bonus * 186 / 128);
    gravity<CORR_MAX>(hist->nonPawnCorr[us][BLACK][pos.non_pawn_key(BLACK) & (CORR_SIZE - 1)], bonus * 186 / 128);
    const Move m = (ss - 1)->move;
    if (m.is_ok()) {
        const Piece pc = (ss - 1)->movedPiece;
        const Square to = m.to();
        if ((ss - 2)->move.is_ok()) gravity<CORR_MAX>((*(ss - 2)->contCorr)[pc][to], bonus * 130 / 128);
        if ((ss - 4)->move.is_ok()) gravity<CORR_MAX>((*(ss - 4)->contCorr)[pc][to], bonus * 70 / 128);
        if ((ss - 6)->move.is_ok()) gravity<CORR_MAX>((*(ss - 6)->contCorr)[pc][to], bonus * 35 / 128);
    }
}

void Worker::update_cont(Stack* ss, Piece pc, Square to, int bonus) {
    static constexpr int Weights[7] = {0, 520, 390, 145, 251, 66, 209};
    static constexpr int Multipliers[7] = {94, 103, 110, 106, 119, 126, 121};
    int positive = 0;
    for (int i = 1; i <= 6; ++i) {
        if (ss->inCheck && i > 2) break;
        if (!(ss - i)->move.is_ok()) continue;
        i16& e = (*(ss - i)->contHist)[pc][to];
        if (e > 0) ++positive;
        gravity<CONT_HIST_MAX>(e, bonus * Weights[i] * Multipliers[positive] / 65536 + 73 * (i < 2));
    }
}

void Worker::update_quiet(Stack* ss, Move m, int bonus) {
    const Color us = pos.side_to_move();
    gravity<MAIN_HIST_MAX>(hist->main_entry(us, m, ss->threats), bonus);
    if (ss->ply < LOW_PLY) gravity<LOWPLY_HIST_MAX>(hist->lowPly[ss->ply][m.from_to()], bonus * 712 / 1024);
    update_cont(ss, pos.moved_piece(m), m.to(), bonus * 750 / 1024);
    gravity<PAWN_HIST_MAX>(hist->pawn_entry(pos, m), bonus * (bonus > -4 ? 1104 : 459) / 1024);
}

void Worker::update_all_stats(Stack* ss, Move best, Square prevSq, Move* quiets, int nq, Move* captures, int nc,
                              int depth, Move ttMove, bool pvNode) {
    int bonus = std::min(133 * depth - 81, 1487) + 364 * (best == ttMove) + (ss - 1)->statScore / 28;
    const int malus = std::min(968 * depth - 235, 2244);
    if (!pvNode) bonus += int(bonus * i64(nq + nc) / 256);

    if (!pos.is_noisy(best)) {
        update_quiet(ss, best, bonus * 899 / 1024);
        int m = malus * 1159 / 1024;
        for (int i = 0; i < nq; ++i) {
            m = m * 921 / 1024;
            update_quiet(ss, quiets[i], -m);
        }
    } else
        gravity<CAPT_HIST_MAX>(hist->capture_entry(pos, best), bonus * 1427 / 1024);

    if (prevSq != NO_SQ && (ss - 1)->moveCount == 1 + (ss - 1)->ttHit && pos.captured_piece() == NO_PIECE)
        update_cont(ss - 1, pos.piece_on(prevSq), prevSq, -malus * 713 / 1024);

    for (int i = 0; i < nc; ++i) gravity<CAPT_HIST_MAX>(hist->capture_entry(pos, captures[i]), -malus * 1489 / 1024);
}

void Worker::check_time() {
    if (id != 0 || --callsCnt > 0) return;
    callsCnt = limits.nodes ? int(std::min<u64>(1024, limits.nodes / 1024 + 1)) : 1024;
    if (completedDepth < 1) return;
    if (limits.nodes && total_nodes() >= limits.nodes) {
        stopFlag = true;
        return;
    }
    if (pondering.load(std::memory_order_relaxed)) return;
    if (tc.useHard && now() - tc.start.load(std::memory_order_relaxed) >= tc.hard) stopFlag = true;
}

template<NodeType NT>
Value Worker::qsearch(Stack* ss, Value alpha, Value beta) {
    constexpr bool PvNode = NT == PV;

    if (alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply)) {
        alpha = VALUE_DRAW;
        if (alpha >= beta) return alpha;
    }

    Move pvLine[MAX_PLY + 1];
    if (PvNode) {
        (ss + 1)->pv = pvLine;
        ss->pv[0] = Move::none();
        if (selDepth < ss->ply + 1) selDepth = ss->ply + 1;
    }

    check_time();
    ss->inCheck = pos.in_check();
    ss->moveCount = 0;

    if (pos.is_draw(ss->ply)) return VALUE_DRAW;
    if (ss->ply >= MAX_PLY) return ss->inCheck ? VALUE_DRAW : evaluate();

    const Key ttKey = pos.key() ^ Zobrist::rule50_key(pos.rule50());
    TTData tte;
    TTEntry* slot;
    const bool ttHit = TT.probe(ttKey, tte, slot);
    Move ttMove = ttHit ? tte.move : Move::none();
    const Value ttValue = ttHit ? value_from_tt(tte.score, ss->ply, pos.rule50()) : VALUE_NONE;
    const bool ttPv = ttHit && tte.pv;

    if (!PvNode && ttValue != VALUE_NONE && tte.depth >= DEPTH_QS
        && (tte.bound & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER)))
        return ttValue;

    Value rawEval = VALUE_NONE, bestValue, futilityBase;
    if (ss->inCheck) {
        bestValue = futilityBase = -VALUE_INFINITE;
        ss->staticEval = VALUE_NONE;
        ss->threats = pos.attacks_by(~pos.side_to_move());
    } else {
        rawEval = ttHit && tte.eval != VALUE_NONE ? tte.eval : evaluate();
        ss->staticEval = bestValue = corrected(rawEval, correction(ss));
        if (ttValue != VALUE_NONE && !is_decisive(ttValue) && (tte.bound & (ttValue > bestValue ? BOUND_LOWER : BOUND_UPPER)))
            bestValue = ttValue;

        if (bestValue >= beta) {
            if (!is_decisive(bestValue)) bestValue = (bestValue + beta) / 2;
            if (!ttHit) TT.store(slot, ttKey, VALUE_NONE, false, BOUND_NONE, DEPTH_UNSEARCHED, Move::none(), rawEval);
            return bestValue;
        }
        alpha = std::max(alpha, bestValue);
        futilityBase = ss->staticEval + 160;
        ss->threats = 0;
    }

    const PieceToHistory* contHist[6] = {(ss - 1)->contHist, (ss - 2)->contHist, (ss - 3)->contHist,
                                         (ss - 4)->contHist, (ss - 5)->contHist, (ss - 6)->contHist};
    MovePicker mp(pos, ttMove, *hist, contHist, ss->threats);
    const Square prevSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.to() : NO_SQ;
    Move bestMove = Move::none();
    int moveCount = 0;

    Move move;
    while ((move = mp.next())) {
        if (!pos.legal(move)) continue;
        const bool givesCheck = pos.gives_check(move);
        const bool capture = pos.is_capture(move);
        ++moveCount;

        if (!is_loss(bestValue)) {
            if (!givesCheck && move.to() != prevSq && !is_loss(futilityBase) && move.type() != PROMOTION) {
                if (moveCount > 2) continue;
                const Value fv = futilityBase + SeeVal[pos.captured_type(move) == NO_PIECE_TYPE ? 6 : pos.captured_type(move)];
                if (fv <= alpha) {
                    bestValue = std::max(bestValue, fv);
                    continue;
                }
                if (!pos.see_ge(move, alpha - futilityBase)) {
                    bestValue = std::max(bestValue, std::min(alpha, futilityBase));
                    continue;
                }
            }
            if (ss->inCheck && !capture && moveCount > 1 && !is_loss(bestValue)) continue;
            if (!pos.see_ge(move, -50)) continue;
        }

        TT.prefetch(pos.key_after(move));
        make_move(ss, move);
        const Value value = -qsearch<NT>(ss + 1, -beta, -alpha);
        unmake_move(move);

        if (stopFlag.load(std::memory_order_relaxed)) return VALUE_ZERO;

        if (value > bestValue) {
            bestValue = value;
            if (value > alpha) {
                bestMove = move;
                if (PvNode) update_pv(ss->pv, move, (ss + 1)->pv);
                if (value >= beta) break;
                alpha = value;
            }
        }
    }

    if (ss->inCheck && bestValue == -VALUE_INFINITE) return mated_in(ss->ply);

    if (!is_decisive(bestValue) && bestValue > beta) bestValue = (bestValue + beta) / 2;

    TT.store(slot, ttKey, value_to_tt(bestValue, ss->ply), ttPv, bestValue >= beta ? BOUND_LOWER : BOUND_UPPER, DEPTH_QS,
             bestMove, rawEval);
    return bestValue;
}

template<NodeType NT>
Value Worker::search(Stack* ss, Value alpha, Value beta, int depth, bool cutNode) {
    constexpr bool PvNode = NT != NonPV;
    constexpr bool RootNode = NT == Root;

    if (!RootNode && alpha < VALUE_DRAW && pos.has_game_cycle(ss->ply)) {
        alpha = draw_value();
        if (alpha >= beta) return alpha;
    }

    if (depth <= 0) return qsearch<PvNode ? PV : NonPV>(ss, alpha, beta);

    depth = std::min(depth, MAX_PLY - 1);

    Move pvLine[MAX_PLY + 1];
    const Color us = pos.side_to_move();
    ss->inCheck = pos.in_check();
    ss->moveCount = 0;
    Value bestValue = -VALUE_INFINITE;
    Move bestMove = Move::none();

    check_time();
    if (PvNode && selDepth < ss->ply + 1) selDepth = ss->ply + 1;

    if (!RootNode) {
        if (stopFlag.load(std::memory_order_relaxed) || ss->ply >= MAX_PLY)
            return (ss->ply >= MAX_PLY && !ss->inCheck) ? evaluate() : draw_value();
        if (pos.is_draw(ss->ply)) return draw_value();

        alpha = std::max(mated_in(ss->ply), alpha);
        beta = std::min(mate_in(ss->ply + 1), beta);
        if (alpha >= beta) return alpha;
    }

    (ss + 1)->excluded = Move::none();
    (ss + 2)->cutoffCnt = 0;
    const Move excluded = ss->excluded;
    const Square prevSq = (ss - 1)->move.is_ok() ? (ss - 1)->move.to() : NO_SQ;
    const bool priorCapture = pos.captured_piece() != NO_PIECE;
    const int priorReduction = (ss - 1)->reduction;
    (ss - 1)->reduction = 0;
    ss->threats = pos.attacks_by(~us);

    const Key ttKey = pos.key() ^ Zobrist::rule50_key(pos.rule50());
    TTData tte;
    TTEntry* slot;
    const bool ttHit = TT.probe(ttKey, tte, slot);
    ss->ttHit = ttHit;
    Move ttMove = RootNode ? rootMoves[pvIdx].move : ttHit ? tte.move : Move::none();
    if (!RootNode && ttMove && !pos.pseudo_legal(ttMove)) ttMove = Move::none();
    const Value ttValue = ttHit ? value_from_tt(tte.score, ss->ply, pos.rule50()) : VALUE_NONE;
    if (!excluded) ss->ttPv = PvNode || (ttHit && tte.pv);
    const bool ttCapture = ttMove && pos.is_noisy(ttMove);

    if (!PvNode && !excluded && ttValue != VALUE_NONE && tte.depth >= depth + (ttValue >= beta)
        && (cutNode || ttValue <= alpha) && (tte.bound & (ttValue >= beta ? BOUND_LOWER : BOUND_UPPER))) {
        if (ttMove && ttValue >= beta) {
            if (!ttCapture) update_quiet(ss, ttMove, std::min(131 * depth, 1600));
            if (prevSq != NO_SQ && (ss - 1)->moveCount < 4 && !priorCapture)
                update_cont(ss - 1, pos.piece_on(prevSq), prevSq, -2210);
        }
        if (pos.rule50() < 90) return ttValue;
    }

    Value rawEval = VALUE_NONE, eval = VALUE_NONE;
    bool improving = false, oppWorsening = false;
    int corr = 0;

    if (ss->inCheck) {
        ss->staticEval = VALUE_NONE;
    } else {
        corr = correction(ss);
        if (excluded) {
            eval = ss->staticEval;
        } else {
            rawEval = ttHit && tte.eval != VALUE_NONE ? tte.eval : evaluate();
            ss->staticEval = eval = corrected(rawEval, corr);
            if (!ttHit) TT.store(slot, ttKey, VALUE_NONE, ss->ttPv, BOUND_NONE, DEPTH_UNSEARCHED, Move::none(), rawEval);
            if (ttValue != VALUE_NONE && !is_decisive(ttValue) && (tte.bound & (ttValue > eval ? BOUND_LOWER : BOUND_UPPER)))
                eval = ttValue;
        }

        if ((ss - 2)->staticEval != VALUE_NONE) improving = ss->staticEval > (ss - 2)->staticEval;
        else if ((ss - 4)->staticEval != VALUE_NONE) improving = ss->staticEval > (ss - 4)->staticEval;
        else improving = true;
        oppWorsening = (ss - 1)->staticEval != VALUE_NONE && ss->staticEval + (ss - 1)->staticEval > 1;

        if ((ss - 1)->move.is_ok() && !(ss - 1)->inCheck && !priorCapture && (ss - 1)->staticEval != VALUE_NONE && !excluded) {
            const int evalDiff = std::clamp(-3 * int((ss - 1)->staticEval + ss->staticEval), -189, 194) + 60;
            gravity<MAIN_HIST_MAX>(hist->main_entry(~us, (ss - 1)->move, (ss - 1)->threats), evalDiff * 11);
            if (!ttHit && type_of(pos.piece_on(prevSq)) != PAWN && (ss - 1)->move.type() != PROMOTION)
                gravity<PAWN_HIST_MAX>(hist->pawn[pos.pawn_key() & (PAWN_HIST_SIZE - 1)][pos.piece_on(prevSq)][prevSq], evalDiff * 13);
        }

        if (!PvNode && !excluded && (ss - 1)->staticEval != VALUE_NONE) {
            if (priorReduction >= 3 && ss->staticEval + (ss - 1)->staticEval <= 0) ++depth;
            if (priorReduction >= 2 && depth >= 2 && ss->staticEval + (ss - 1)->staticEval >= 140) --depth;
        }

        if (!PvNode && !excluded) {
            // reverse futility pruning
            if (!ss->ttPv && depth <= 10 && eval >= beta && !is_decisive(eval) && !is_loss(beta) && (!ttMove || ttCapture)) {
                const int margin = 80 * depth - 90 * improving - 30 * oppWorsening + std::abs(corr) / 2;
                if (eval - std::max(margin, 20) >= beta) return (eval + 2 * beta) / 3;
            }

            // razoring
            if (depth <= 4 && std::abs(alpha) < 2000 && eval + 300 * depth <= alpha) {
                const Value v = qsearch<NonPV>(ss, alpha, alpha + 1);
                if (v <= alpha) return v;
            }

            // null move pruning
            if ((ss - 1)->move != Move::null() && ss->ply >= nmpMinPly && depth >= 3 && eval >= beta
                && ss->staticEval >= beta + 210 - 12 * depth - 40 * improving
                && !(ttHit && tte.bound == BOUND_UPPER && ttValue != VALUE_NONE && ttValue < beta)
                && pos.has_non_pawns(us) && !is_loss(beta)) {
                const int R = 4 + depth / 3 + std::min((eval - beta) / 200, 4) + ttCapture;
                ss->move = Move::null();
                ss->movedPiece = NO_PIECE;
                ss->contHist = &sentinelHist;
                ss->contCorr = &sentinelCorr;
                nodes.store(nodes.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                pos.make_null();
                nnue.push_null();
                Value v = -search<NonPV>(ss + 1, -beta, -beta + 1, depth - R, !cutNode);
                nnue.pop();
                pos.unmake_null();

                if (stopFlag.load(std::memory_order_relaxed)) return VALUE_ZERO;
                if (v >= beta) {
                    if (is_win(v)) v = beta;
                    if (nmpMinPly || depth < 14) return v;
                    nmpMinPly = ss->ply + 3 * (depth - R) / 4;
                    const Value w = search<NonPV>(ss, beta - 1, beta, depth - R, false);
                    nmpMinPly = 0;
                    if (w >= beta) return v;
                }
            }
        }

        improving |= ss->staticEval >= beta;

        if ((PvNode || cutNode) && depth >= 4 && !excluded && (!ttMove || tte.depth + 4 < depth)) --depth;

        // probcut
        const Value pcBeta = beta + 230 - 50 * improving;
        if (!PvNode && !excluded && depth >= 5 && !is_decisive(beta)
            && !(ttValue != VALUE_NONE && tte.depth >= depth - 3 && ttValue < pcBeta)) {
            MovePicker mp(pos, ttMove, pcBeta - ss->staticEval, *hist);
            Move m;
            while ((m = mp.next())) {
                if (!pos.legal(m)) continue;
                TT.prefetch(pos.key_after(m));
                make_move(ss, m);
                Value v = -qsearch<NonPV>(ss + 1, -pcBeta, -pcBeta + 1);
                if (v >= pcBeta) v = -search<NonPV>(ss + 1, -pcBeta, -pcBeta + 1, depth - 4, !cutNode);
                unmake_move(m);
                if (stopFlag.load(std::memory_order_relaxed)) return VALUE_ZERO;
                if (v >= pcBeta) {
                    TT.store(slot, ttKey, value_to_tt(v, ss->ply), ss->ttPv, BOUND_LOWER, depth - 3, m, rawEval);
                    return is_decisive(v) ? v : v - (pcBeta - beta);
                }
            }
        }
    }

    // small probcut from the transposition table
    {
        const Value pcBeta = beta + 350;
        if (!excluded && ttValue != VALUE_NONE && (tte.bound & BOUND_LOWER) && tte.depth >= depth - 4 && ttValue >= pcBeta
            && !is_decisive(beta) && !is_decisive(ttValue))
            return pcBeta;
    }

    const PieceToHistory* contHist[6] = {(ss - 1)->contHist, (ss - 2)->contHist, (ss - 3)->contHist,
                                         (ss - 4)->contHist, (ss - 5)->contHist, (ss - 6)->contHist};
    MovePicker mp(pos, ttMove, *hist, contHist, ss->threats, ss->ply, depth);

    Move quiets[32], captures[32];
    int nq = 0, nc = 0;
    int moveCount = 0;
    bool skipQuiets = false;
    Value value = bestValue;

    Move move;
    while ((move = mp.next(skipQuiets))) {
        if (move == excluded) continue;
        if (RootNode) {
            if (!root_has(move)) continue;
        } else if (!pos.legal(move))
            continue;

        ss->moveCount = ++moveCount;
        if (RootNode && id == 0 && !limits.silent && now() - tc.start.load(std::memory_order_relaxed) > 3000)
            std::printf("info depth %d currmove %s currmovenumber %d\n", depth, move_to_uci(move).c_str(), moveCount + pvIdx);
        if (PvNode) (ss + 1)->pv = nullptr;

        const bool capture = pos.is_noisy(move);
        const Piece movedPiece = pos.moved_piece(move);
        const Square to = move.to();
        const bool givesCheck = pos.gives_check(move);
        int newDepth = depth - 1;
        int extension = 0;
        int captHist = capture ? hist->capture_entry(pos, move) : 0;

        int r = Lmr[capture][std::min(depth, 63)][std::min(moveCount, 63)];

        if (!RootNode && pos.has_non_pawns(us) && !is_loss(bestValue)) {
            if (moveCount >= (3 + depth * depth) / (2 - improving)) skipQuiets = true;

            int lmrDepth = newDepth - (r + 700 * ss->ttPv) / 1024;

            if (capture || givesCheck) {
                const PieceType cap = pos.captured_type(move);
                if (!givesCheck && lmrDepth < 7 && !ss->inCheck && move.type() != PROMOTION) {
                    const Value fv = ss->staticEval + 200 + 200 * lmrDepth + SeeVal[cap == NO_PIECE_TYPE ? 6 : cap] + captHist / 8;
                    if (fv <= alpha) continue;
                }
                if (!pos.see_ge(move, std::min(0, -95 * depth - captHist / 64))) continue;
            } else {
                int history = (*contHist[0])[movedPiece][to] + (*contHist[1])[movedPiece][to] + hist->pawn_entry(pos, move);
                if (history < -4136 * depth) continue;
                history += 69 * hist->main_entry(us, move, ss->threats) / 32;
                lmrDepth += history / 3576;

                const Value fv = ss->staticEval + 175 + 105 * lmrDepth;
                if (!ss->inCheck && lmrDepth < 9 && fv <= alpha) {
                    if (!is_decisive(bestValue) && bestValue < fv && !is_win(fv)) bestValue = fv;
                    continue;
                }
                lmrDepth = std::max(lmrDepth, 0);
                if (!pos.see_ge(move, -20 * lmrDepth * lmrDepth)) continue;
            }
        }

        if (!RootNode && move == ttMove && !excluded && depth >= 6 + ss->ttPv && ttValue != VALUE_NONE
            && !is_decisive(ttValue) && (tte.bound & BOUND_LOWER) && tte.depth >= depth - 3 && ss->ply < 2 * rootDepth) {
            const Value singularBeta = ttValue - depth - depth * (ss->ttPv && !PvNode);
            const int singularDepth = (depth - 1) / 2;
            ss->excluded = move;
            value = search<NonPV>(ss, singularBeta - 1, singularBeta, singularDepth, cutNode);
            ss->excluded = Move::none();
            ss->moveCount = moveCount;

            if (stopFlag.load(std::memory_order_relaxed)) return VALUE_ZERO;

            if (value < singularBeta) {
                extension = 1;
                if (!PvNode && value < singularBeta - 11) extension = 2 + (!capture && value < singularBeta - 75);
            } else if (value >= beta && !is_decisive(value))
                return value;
            else if (ttValue >= beta)
                extension = -3;
            else if (cutNode)
                extension = -2;
        }

        newDepth += extension;

        TT.prefetch(pos.key_after(move));
        const u64 nodesBefore = nodes.load(std::memory_order_relaxed);
        make_move(ss, move);

        int statScore;
        if (capture) statScore = 7 * SeeVal[pos.captured_piece() == NO_PIECE ? 6 : type_of(pos.captured_piece())] + captHist - 4000;
        else statScore = 2 * hist->main_entry(us, move, ss->threats) + (*contHist[0])[movedPiece][to] + (*contHist[1])[movedPiece][to] - 4000;
        ss->statScore = statScore;

        r += 1000 * !PvNode;
        r -= 1100 * ss->ttPv;
        r += 1000 * (ss->ttPv && ttValue != VALUE_NONE && ttValue <= alpha);
        r += 1800 * cutNode;
        r += 900 * !improving;
        r -= 900 * givesCheck;
        r += 1000 * ttCapture;
        r -= statScore / 10;
        r -= 4 * std::abs(corr);
        r -= 40 * moveCount;
        if ((ss + 1)->cutoffCnt > 2) r += 1000;

        if (depth >= 2 && moveCount > 1 + RootNode) {
            const int reduced = std::clamp(newDepth - r / 1024, 1, newDepth + 1);
            ss->reduction = newDepth - reduced;
            value = -search<NonPV>(ss + 1, -(alpha + 1), -alpha, reduced, true);
            ss->reduction = 0;

            if (value > alpha) {
                const bool deeper = reduced < newDepth && value > bestValue + 45 + 2 * newDepth;
                const bool shallower = value < bestValue + newDepth;
                newDepth += deeper - shallower;
                if (newDepth > reduced) value = -search<NonPV>(ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);
                update_cont(ss, movedPiece, to, 1400);
            }
        } else if (!PvNode || moveCount > 1) {
            value = -search<NonPV>(ss + 1, -(alpha + 1), -alpha, newDepth, !cutNode);
        }

        if (PvNode && (moveCount == 1 || value > alpha)) {
            (ss + 1)->pv = pvLine;
            pvLine[0] = Move::none();
            if (move == ttMove && tte.depth > 1) newDepth = std::max(newDepth, 1);
            value = -search<PV>(ss + 1, -beta, -alpha, newDepth, false);
        }

        unmake_move(move);

        if (stopFlag.load(std::memory_order_relaxed)) return VALUE_ZERO;

        if (RootNode) {
            RootMove& rm = root_move(move);
            rm.nodes += nodes.load(std::memory_order_relaxed) - nodesBefore;
            rm.avgScore = rm.avgScore == VALUE_NONE ? value : (value + rm.avgScore) / 2;
            if (moveCount == 1 || value > alpha) {
                rm.score = rm.uciScore = value;
                rm.selDepth = selDepth;
                rm.lowerbound = rm.upperbound = false;
                if (value >= beta) rm.lowerbound = true, rm.uciScore = beta;
                else if (value <= alpha) rm.upperbound = true, rm.uciScore = alpha;
                rm.pv.resize(1);
                for (Move* m = (ss + 1)->pv; m && *m; ++m) rm.pv.push_back(*m);
                if (moveCount > 1 && !pvIdx) ++bestMoveChanges;
            } else
                rm.score = -VALUE_INFINITE;
        }

        if (value > bestValue) {
            bestValue = value;
            if (value > alpha) {
                bestMove = move;
                if (PvNode && !RootNode) update_pv(ss->pv, move, (ss + 1)->pv);
                if (value >= beta) {
                    ss->cutoffCnt += (extension < 2) || PvNode;
                    break;
                }
                if (depth > 2 && depth < 12 && !is_decisive(value)) --depth;
                alpha = value;
            }
        }

        if (move != bestMove && moveCount <= 32) {
            if (capture) captures[nc++] = move;
            else quiets[nq++] = move;
        }
    }

    if (!moveCount) bestValue = excluded ? alpha : ss->inCheck ? mated_in(ss->ply) : VALUE_DRAW;
    else if (bestMove)
        update_all_stats(ss, bestMove, prevSq, quiets, nq, captures, nc, depth, ttMove, PvNode);
    else if (!priorCapture && prevSq != NO_SQ) {
        int scale = -241 - (ss - 1)->statScore / 98 + std::min(59 * depth, 420) + 186 * ((ss - 1)->moveCount > 9)
                  + 142 * (!ss->inCheck && bestValue <= ss->staticEval - 35)
                  + 159 * (!(ss - 1)->inCheck && (ss - 1)->staticEval != VALUE_NONE && bestValue <= -(ss - 1)->staticEval - 23);
        scale = std::max(scale, 0);
        const int bonus = std::min(150 * depth - 85, 1337) * scale;
        update_cont(ss - 1, pos.piece_on(prevSq), prevSq, bonus * 263 / 16384);
        gravity<MAIN_HIST_MAX>(hist->main_entry(~us, (ss - 1)->move, (ss - 1)->threats), bonus * 215 / 32768);
        if (type_of(pos.piece_on(prevSq)) != PAWN && (ss - 1)->move.type() != PROMOTION)
            gravity<PAWN_HIST_MAX>(hist->pawn[pos.pawn_key() & (PAWN_HIST_SIZE - 1)][pos.piece_on(prevSq)][prevSq], bonus * 324 / 8192);
    } else if (priorCapture && prevSq != NO_SQ) {
        gravity<CAPT_HIST_MAX>(hist->capture[pos.piece_on(prevSq)][prevSq][type_of(pos.captured_piece())], 892);
    }

    if (bestValue >= beta && !is_decisive(bestValue) && !is_decisive(beta) && !is_decisive(alpha))
        bestValue = (bestValue * depth + beta) / (depth + 1);

    if (bestValue <= alpha) ss->ttPv = ss->ttPv || ((ss - 1)->ttPv && depth > 3);

    if (!excluded && !(RootNode && pvIdx))
        TT.store(slot, ttKey, value_to_tt(bestValue, ss->ply), ss->ttPv,
                 bestValue >= beta ? BOUND_LOWER : PvNode && bestMove ? BOUND_EXACT : BOUND_UPPER,
                 moveCount ? depth : std::min(MAX_PLY - 1, depth + 6), bestMove, rawEval);

    if (!ss->inCheck && !excluded && !(bestMove && pos.is_capture(bestMove)) && (bestValue > ss->staticEval) == bool(bestMove)) {
        const int bonus = std::clamp((bestValue - ss->staticEval) * depth * (bestMove ? 12 : 18) / 128, -CORR_MAX / 4, CORR_MAX / 4);
        update_correction(ss, 1061 * bonus / 1024);
    }

    return bestValue;
}

void Worker::print_info(int depth) const {
    const i64 elapsed = std::max<i64>(1, now() - tc.start.load(std::memory_order_relaxed));
    const u64 n = total_nodes();
    const int lines = std::min<int>(limits.multiPV, int(rootMoves.size()));
    for (int i = 0; i < lines; ++i) {
        const RootMove& rm = rootMoves[i];
        const bool updated = rm.score != -VALUE_INFINITE;
        if (depth == 1 && !updated && i > 0) continue;
        const int d = updated ? depth : std::max(1, depth - 1);
        const Value v = updated ? rm.uciScore : rm.prevScore;
        if (v == -VALUE_INFINITE) continue;
        std::string pv;
        for (Move m : rm.pv) pv += " " + move_to_uci(m);
        std::printf("info depth %d seldepth %d multipv %d score %s%s nodes %llu nps %llu hashfull %d tbhits 0 time %lld pv%s\n",
                    d, rm.selDepth, i + 1, score_to_uci(v).c_str(),
                    i == pvIdx && updated && rm.lowerbound ? " lowerbound" : i == pvIdx && updated && rm.upperbound ? " upperbound" : "",
                    (unsigned long long)n, (unsigned long long)(n * 1000 / elapsed), TT.hashfull(), (long long)elapsed, pv.c_str());
    }
    std::fflush(stdout);
}

bool Worker::soft_stop(i64 elapsed) {
    if (!tc.useSoft) return false;
    const RootMove& best = rootMoves[0];
    stability = best.move == lastBest ? stability + 1 : 1;
    lastBest = best.move;

    const u64 total = std::max<u64>(1, nodes.load(std::memory_order_relaxed));
    const double frac = double(best.nodes) / double(total);
    double scale = std::max(2.59 - 1.6 * frac, 0.188);
    if (completedDepth >= 6) scale *= std::min(2.36, 0.78 + 8.59 * std::pow(stability + 0.9, -2.57));

    const Value score = best.score;
    if (!is_decisive(score)) {
        if (scoreEma != VALUE_NONE) {
            const double c = (score - scoreEma) / 5.0;
            const double inv = c * 0.36 / (std::abs(c) + 0.94) * (c > 0 ? 0.94 : 1.10);
            scale *= std::clamp(1.0 - inv, 0.63, 2.48);
            scoreEma += (score - scoreEma) / 8;
        } else
            scoreEma = score;
    } else if (is_win(score))
        scale = 0.15;
    else
        scale = 0.5;

    scale = std::max(scale, 0.09);
    return elapsed >= i64(double(tc.soft) * scale);
}

void Worker::iterative_deepening() {
    Stack stackArr[MAX_PLY + 10];
    Stack* ss = stackArr + 7;
    std::memset(stackArr, 0, sizeof(stackArr));
    for (int i = 0; i < MAX_PLY + 10; ++i) {
        stackArr[i].contHist = &sentinelHist;
        stackArr[i].contCorr = &sentinelCorr;
        stackArr[i].staticEval = VALUE_NONE;
        stackArr[i].movedPiece = NO_PIECE;
    }
    for (int i = 0; i <= MAX_PLY + 2; ++i) (ss + i)->ply = i;
    Move pv[MAX_PLY + 1];
    ss->pv = pv;

    const int multiPV = std::min<int>(limits.multiPV, int(rootMoves.size()));
    Value score = -VALUE_INFINITE;

    for (rootDepth = 1; rootDepth < MAX_PLY && !stopFlag.load(std::memory_order_relaxed); ++rootDepth) {
        if (limits.depth && rootDepth > limits.depth) break;

        for (auto& rm : rootMoves) rm.prevScore = rm.score;
        if (id == 0) bestMoveChanges = 0;

        for (pvIdx = 0; pvIdx < multiPV && !stopFlag.load(std::memory_order_relaxed); ++pvIdx) {
            selDepth = 0;
            Value alpha = -VALUE_INFINITE, beta = VALUE_INFINITE, delta = 0;
            const Value avg = rootMoves[pvIdx].avgScore;
            if (rootDepth >= 4 && avg != VALUE_NONE) {
                delta = 10 + int(i64(avg) * avg / 15000) + id % 4;
                alpha = std::max(avg - delta, -VALUE_INFINITE);
                beta = std::min(avg + delta, VALUE_INFINITE);
            }

            int failHigh = 0;
            while (true) {
                const int d = std::max(1, rootDepth - failHigh);
                score = search<Root>(ss, alpha, beta, d, false);
                std::stable_sort(rootMoves.begin() + pvIdx, rootMoves.end());
                if (stopFlag.load(std::memory_order_relaxed)) break;

                if (id == 0 && !limits.silent && (score <= alpha || score >= beta) && now() - tc.start.load() > 3000)
                    print_info(rootDepth);

                if (score <= alpha) {
                    beta = (alpha + beta) / 2;
                    alpha = std::max(score - delta, -VALUE_INFINITE);
                    failHigh = 0;
                } else if (score >= beta) {
                    beta = std::min(score + delta, VALUE_INFINITE);
                    if (!is_decisive(score)) failHigh = std::min(failHigh + 1, 3);
                } else
                    break;

                if (is_decisive(score)) {
                    if (score <= alpha) alpha = -VALUE_INFINITE;
                    if (score >= beta) beta = VALUE_INFINITE;
                }
                delta += 47 * delta / 128;
            }
            std::stable_sort(rootMoves.begin(), rootMoves.begin() + pvIdx + 1);
        }

        if (!stopFlag.load(std::memory_order_relaxed)) completedDepth = rootDepth;
        if (id != 0) continue;

        if (!limits.silent) print_info(rootDepth);

        if (stopFlag.load(std::memory_order_relaxed)) break;

        const Value best = rootMoves[0].score;
        if (limits.mate && is_decisive(best) && VALUE_MATE - std::abs(best) <= 2 * limits.mate) stopFlag = true;
        if (limits.softNodes && total_nodes() >= limits.softNodes) stopFlag = true;

        if (!pondering.load() && !limits.infinite) {
            const i64 elapsed = now() - tc.start.load();
            if (tc.useSoft && soft_stop(elapsed)) stopFlag = true;
            if (tc.useHard && elapsed >= tc.hard) stopFlag = true;
        }
    }
}

Worker* select_best() {
    Worker* best = workers[0].get();
    if (workers.size() == 1 || limits.depth || limits.multiPV > 1) return best;

    Value minScore = VALUE_INFINITE;
    for (auto& w : workers)
        if (w->completedDepth > 0) minScore = std::min(minScore, w->rootMoves[0].score);

    std::vector<std::pair<Move, i64>> votes;
    auto vote_of = [&](Move m) -> i64& {
        for (auto& v : votes)
            if (v.first == m) return v.second;
        votes.emplace_back(m, 0);
        return votes.back().second;
    };
    for (auto& w : workers)
        if (w->completedDepth > 0) vote_of(w->rootMoves[0].move) += i64(w->rootMoves[0].score - minScore + 10) * w->completedDepth;

    for (auto& w : workers) {
        if (w->completedDepth == 0) continue;
        const Value bs = best->rootMoves[0].score, ws = w->rootMoves[0].score;
        if (is_win(bs)) {
            if (ws > bs) best = w.get();
        } else if (is_loss(bs)) {
            if (ws > bs) best = w.get();
        } else if (is_win(ws)) {
            best = w.get();
        } else if (!is_loss(ws) && vote_of(w->rootMoves[0].move) > vote_of(best->rootMoves[0].move)) {
            best = w.get();
        }
    }
    return best;
}

Move ponder_from_tt(Position& pos, Move best) {
    pos.make(best);
    TTData d;
    TTEntry* e;
    Move m = Move::none();
    if (TT.probe(pos.key() ^ Zobrist::rule50_key(pos.rule50()), d, e) && d.move && pos.pseudo_legal(d.move) && pos.legal(d.move))
        m = d.move;
    pos.unmake(best);
    return m;
}

void main_search() {
    Worker* main = workers[0].get();

    std::vector<std::thread> helpers;
    for (size_t i = 1; i < workers.size(); ++i) helpers.emplace_back([i]() { workers[i]->iterative_deepening(); });
    main->iterative_deepening();

    while (!stopFlag.load() && (pondering.load() || limits.infinite)) std::this_thread::sleep_for(std::chrono::milliseconds(1));

    stopFlag = true;
    for (auto& t : helpers) t.join();

    Worker* best = select_best();
    if (best != main && !limits.silent) best->print_info(best->completedDepth);

    const RootMove& rm = best->rootMoves[0];
    Move ponder = rm.pv.size() > 1 ? rm.pv[1] : ponder_from_tt(best->pos, rm.move);

    lastResult.best = rm.move;
    lastResult.ponder = ponder;
    lastResult.score = rm.score == -VALUE_INFINITE ? rm.prevScore : rm.score;
    lastResult.depth = best->completedDepth;
    lastResult.nodes = total_nodes();

    if (!limits.silent) {
        if (ponder) std::printf("bestmove %s ponder %s\n", move_to_uci(rm.move).c_str(), move_to_uci(ponder).c_str());
        else std::printf("bestmove %s\n", move_to_uci(rm.move).c_str());
        std::fflush(stdout);
    }
}

void init_time(const Position& pos, size_t legalMoves) {
    tc.useSoft = tc.useHard = false;
    const Color us = pos.side_to_move();
    const i64 overhead = limits.moveOverhead;

    if (limits.movetime) {
        tc.useHard = true;
        tc.hard = std::max<i64>(1, limits.movetime - overhead);
    } else if (limits.time[us]) {
        const i64 limit = std::max<i64>(1, limits.time[us] - overhead);
        const int mtg = limits.movestogo ? std::min(limits.movestogo, 50) : 20;
        const double base = double(limit) / mtg + 0.9 * double(limits.inc[us]);
        double soft = 0.70 * base;
        const double hard = std::min((mtg == 1 ? 0.90 : 0.60) * double(limit), 5.0 * soft);
        soft = std::min(soft, hard);
        tc.useSoft = tc.useHard = true;
        tc.soft = std::max<i64>(1, i64(soft));
        tc.hard = std::max<i64>(1, i64(hard));
    }
    if (legalMoves == 1 && tc.useHard && limits.multiPV == 1) tc.hard = std::min<i64>(tc.hard, 250);
}

} // namespace

void Search::init() {
    for (int d = 1; d < 64; ++d)
        for (int m = 1; m < 64; ++m) {
            const double l = std::log(d) * std::log(m);
            Lmr[0][d][m] = int(1024 * (0.85 + l / 2.35));
            Lmr[1][d][m] = int(1024 * (-0.15 + l / 2.75));
        }
    for (int m = 0; m < 64; ++m) Lmr[0][0][m] = Lmr[1][0][m] = Lmr[0][m][0] = Lmr[1][m][0] = 0;
    if (workers.empty()) workers.push_back(std::make_unique<Worker>(0));
}

void Search::set_threads(int n) {
    wait();
    n = std::max(1, n);
    while (int(workers.size()) > n) workers.pop_back();
    while (int(workers.size()) < n) workers.push_back(std::make_unique<Worker>(int(workers.size())));
}

void Search::new_game() {
    wait();
    for (auto& w : workers) w->hist->clear();
}

void Search::start(const Position& pos, const Limits& lim) {
    wait();
    limits = lim;
    if (!limits.start) limits.start = now();
    tc.start = limits.start;
    stopFlag = false;
    pondering = limits.ponder;

    MoveList legal;
    generate_legal(pos, legal);
    std::vector<Move> moves;
    for (int i = 0; i < legal.size(); ++i)
        if (limits.searchmoves.empty() || std::find(limits.searchmoves.begin(), limits.searchmoves.end(), legal[i]) != limits.searchmoves.end())
            moves.push_back(legal[i]);

    if (moves.empty()) {
        lastResult = Result{};
        if (!limits.silent) {
            std::printf("info depth 0 score %s\nbestmove 0000\n", pos.in_check() ? "mate 0" : "cp 0");
            std::fflush(stdout);
        }
        return;
    }

    init_time(pos, moves.size());
    TT.new_search();
    for (auto& w : workers) w->prepare(pos, moves);

    mainThread = std::thread(main_search);
}

void Search::stop() {
    pondering = false;
    stopFlag = true;
}

void Search::ponderhit() {
    tc.start = now();
    pondering = false;
}

void Search::wait() {
    if (mainThread.joinable()) mainThread.join();
}

void Search::finish() {
    if (limits.infinite || pondering.load()) stop();
    wait();
}

void Search::shutdown() {
    stop();
    wait();
}

Search::Result Search::last_result() { return lastResult; }
