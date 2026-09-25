#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "bitboard.h"
#include "types.h"

namespace Zobrist {
extern Key psq[PIECE_NB][SQUARE_NB];
extern Key castling[16];
extern Key enpassant[8];
extern Key side;
extern Key rule50[16];
void init();

inline Key rule50_key(int r50) { return r50 < 14 ? 0 : rule50[std::min((r50 - 14) / 8, 15)]; }
} // namespace Zobrist

struct DirtyPieces {
    int adds = 0, subs = 0;
    Piece addPc[2];
    Square addSq[2];
    Piece subPc[2];
    Square subSq[2];

    void add(Piece p, Square s) { addPc[adds] = p, addSq[adds++] = s; }
    void sub(Piece p, Square s) { subPc[subs] = p, subSq[subs++] = s; }
};

struct StateInfo {
    Key key;
    Key pawnKey;
    Key nonPawnKey[COLOR_NB];
    Key minorKey;
    Key majorKey;
    int castling;
    int rule50;
    int pliesFromNull;
    Square ep;
    Piece captured;
    Move move;
    Bitboard checkers;
    Bitboard blockers[COLOR_NB];
    Bitboard pinners[COLOR_NB];
    DirtyPieces dirty;
};

class Position {
public:
    static constexpr const char* StartFEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Position() { set(StartFEN); }
    bool set(const std::string& fen);
    std::string fen() const;

    Bitboard pieces() const { return byColor[WHITE] | byColor[BLACK]; }
    Bitboard pieces(Color c) const { return byColor[c]; }
    Bitboard pieces(PieceType pt) const { return byType[pt]; }
    Bitboard pieces(PieceType a, PieceType b) const { return byType[a] | byType[b]; }
    Bitboard pieces(Color c, PieceType pt) const { return byColor[c] & byType[pt]; }
    Bitboard pieces(Color c, PieceType a, PieceType b) const { return byColor[c] & (byType[a] | byType[b]); }
    Piece piece_on(Square s) const { return board[s]; }
    bool empty(Square s) const { return board[s] == NO_PIECE; }
    Square king_sq(Color c) const { return lsb(pieces(c, KING)); }
    Color side_to_move() const { return stm; }
    int game_ply() const { return gamePly; }

    const StateInfo& state() const { return states.back(); }
    const StateInfo& state(int back) const { return states[states.size() - 1 - back]; }
    int state_count() const { return int(states.size()); }
    Key key() const { return state().key; }
    Key pawn_key() const { return state().pawnKey; }
    Key non_pawn_key(Color c) const { return state().nonPawnKey[c]; }
    Key minor_key() const { return state().minorKey; }
    Key major_key() const { return state().majorKey; }
    Bitboard checkers() const { return state().checkers; }
    bool in_check() const { return state().checkers; }
    Bitboard blockers(Color c) const { return state().blockers[c]; }
    Bitboard pinners(Color c) const { return state().pinners[c]; }
    int castling_rights() const { return state().castling; }
    Square ep_square() const { return state().ep; }
    int rule50() const { return state().rule50; }
    Piece captured_piece() const { return state().captured; }

    Piece moved_piece(Move m) const { return board[m.from()]; }
    bool is_capture(Move m) const {
        return (!empty(m.to()) && m.type() != CASTLING) || m.type() == EN_PASSANT;
    }
    bool is_noisy(Move m) const {
        return is_capture(m) || (m.type() == PROMOTION && m.promotion() == QUEEN);
    }
    PieceType captured_type(Move m) const {
        return m.type() == EN_PASSANT ? PAWN : m.type() == CASTLING ? NO_PIECE_TYPE : board[m.to()] == NO_PIECE ? NO_PIECE_TYPE : type_of(board[m.to()]);
    }

    Bitboard attackers_to(Square s, Bitboard occ) const;
    Bitboard attackers_to(Square s) const { return attackers_to(s, pieces()); }
    Bitboard attacks_by(Color c) const;
    bool gives_check(Move m) const;
    bool legal(Move m) const;
    bool pseudo_legal(Move m) const;
    bool see_ge(Move m, int threshold) const;

    void make(Move m);
    void unmake(Move m);
    void make_null();
    void unmake_null();
    Key key_after(Move m) const;

    bool is_repetition(int ply) const;
    bool is_draw(int ply) const;
    bool has_game_cycle(int ply) const;
    bool insufficient_material() const;
    bool has_non_pawns(Color c) const { return pieces(c) & ~pieces(PAWN, KING); }
    int piece_count() const { return popcount(pieces()); }
    int material() const;

    static void castling_rook(Square kingTo, Square& rookFrom, Square& rookTo);

private:
    void put_piece(Piece p, Square s);
    void remove_piece(Square s);
    void move_piece(Square from, Square to);
    void update_pins(StateInfo& st) const;
    bool ep_capture_exists(Square epSq) const;
    void compute_keys(StateInfo& st) const;

    Piece board[SQUARE_NB];
    Bitboard byType[PIECE_TYPE_NB];
    Bitboard byColor[COLOR_NB];
    Color stm;
    int gamePly;
    std::vector<StateInfo> states;
};

std::string move_to_uci(Move m);
Move uci_to_move(const Position& pos, const std::string& s);
