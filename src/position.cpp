#include "position.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string_view>

#include "movegen.h"

namespace Zobrist {
Key psq[PIECE_NB][SQUARE_NB];
Key castling[16];
Key enpassant[8];
Key side;
Key rule50[16];
} // namespace Zobrist

namespace {

constexpr int SeeValue[7] = {100, 300, 300, 500, 900, 0, 0};

int CastleMask[SQUARE_NB];

Key cuckoo[8192];
Move cuckooMove[8192];

inline int cuckoo_h1(Key k) { return int(k & 0x1FFF); }
inline int cuckoo_h2(Key k) { return int((k >> 16) & 0x1FFF); }

void init_cuckoo() {
    std::memset(cuckoo, 0, sizeof(cuckoo));
    for (auto& m : cuckooMove) m = Move::none();
    for (int pc = 0; pc < PIECE_NB; ++pc) {
        PieceType pt = type_of(Piece(pc));
        if (pt == PAWN) continue;
        for (Square s1 = A1; s1 <= H8; ++s1)
            for (Square s2 = Square(s1 + 1); s2 <= H8; ++s2) {
                if (!(attacks_bb(pt, s1, 0) & square_bb(s2))) continue;
                Move move(s1, s2);
                Key key = Zobrist::psq[pc][s1] ^ Zobrist::psq[pc][s2] ^ Zobrist::side;
                int i = cuckoo_h1(key);
                while (true) {
                    std::swap(cuckoo[i], key);
                    std::swap(cuckooMove[i], move);
                    if (move == Move::none()) break;
                    i = (i == cuckoo_h1(key)) ? cuckoo_h2(key) : cuckoo_h1(key);
                }
            }
    }
}

} // namespace

void Zobrist::init() {
    u64 s = 0x2545F4914F6CDD1DULL;
    auto rnd = [&]() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    };
    for (auto& p : psq)
        for (auto& k : p) k = rnd();
    for (auto& k : castling) k = rnd();
    castling[0] = 0;
    for (auto& k : enpassant) k = rnd();
    side = rnd();
    for (auto& k : rule50) k = rnd();

    for (int& m : CastleMask) m = ALL_CASTLING;
    CastleMask[E1] &= ~(WHITE_OO | WHITE_OOO);
    CastleMask[H1] &= ~WHITE_OO;
    CastleMask[A1] &= ~WHITE_OOO;
    CastleMask[E8] &= ~(BLACK_OO | BLACK_OOO);
    CastleMask[H8] &= ~BLACK_OO;
    CastleMask[A8] &= ~BLACK_OOO;

    init_cuckoo();
}

void Position::castling_rook(Square kingTo, Square& rookFrom, Square& rookTo) {
    switch (kingTo) {
    case G1: rookFrom = H1, rookTo = F1; break;
    case C1: rookFrom = A1, rookTo = D1; break;
    case G8: rookFrom = H8, rookTo = F8; break;
    default: rookFrom = A8, rookTo = D8; break;
    }
}

inline void Position::put_piece(Piece p, Square s) {
    board[s] = p;
    byType[type_of(p)] |= square_bb(s);
    byColor[color_of(p)] |= square_bb(s);
}

inline void Position::remove_piece(Square s) {
    Piece p = board[s];
    byType[type_of(p)] ^= square_bb(s);
    byColor[color_of(p)] ^= square_bb(s);
    board[s] = NO_PIECE;
}

inline void Position::move_piece(Square from, Square to) {
    Piece p = board[from];
    Bitboard ft = square_bb(from) | square_bb(to);
    byType[type_of(p)] ^= ft;
    byColor[color_of(p)] ^= ft;
    board[from] = NO_PIECE;
    board[to] = p;
}

void Position::compute_keys(StateInfo& st) const {
    st.key = st.pawnKey = st.minorKey = st.majorKey = 0;
    st.nonPawnKey[WHITE] = st.nonPawnKey[BLACK] = 0;
    for (Square s = A1; s <= H8; ++s) {
        Piece p = board[s];
        if (p == NO_PIECE) continue;
        Key k = Zobrist::psq[p][s];
        st.key ^= k;
        PieceType pt = type_of(p);
        if (pt == PAWN) st.pawnKey ^= k;
        else {
            st.nonPawnKey[color_of(p)] ^= k;
            if (pt == KNIGHT || pt == BISHOP || pt == KING) st.minorKey ^= k;
            if (pt == ROOK || pt == QUEEN || pt == KING) st.majorKey ^= k;
        }
    }
    st.key ^= Zobrist::castling[st.castling];
    if (st.ep != NO_SQ) st.key ^= Zobrist::enpassant[file_of(st.ep)];
    if (stm == BLACK) st.key ^= Zobrist::side;
}

void Position::update_pins(StateInfo& st) const {
    for (Color c : {WHITE, BLACK}) {
        Square ksq = king_sq(c);
        Bitboard snipers = (rook_attacks(ksq, 0) & pieces(~c, ROOK, QUEEN))
                         | (bishop_attacks(ksq, 0) & pieces(~c, BISHOP, QUEEN));
        Bitboard occ = pieces();
        st.blockers[c] = st.pinners[c] = 0;
        while (snipers) {
            Square s = pop_lsb(snipers);
            Bitboard b = between_bb(ksq, s) & occ;
            if (b && !more_than_one(b)) {
                st.blockers[c] |= b;
                if (b & pieces(c)) st.pinners[c] |= square_bb(s);
            }
        }
    }
}

bool Position::set(const std::string& fen) {
    std::istringstream ss(fen);
    std::string placement, side, castle, ep;
    int r50 = 0, fullmove = 1;
    ss >> placement >> side >> castle >> ep;
    if (!(ss >> r50)) r50 = 0;
    if (!(ss >> fullmove)) fullmove = 1;

    for (auto& p : board) p = NO_PIECE;
    for (auto& b : byType) b = 0;
    for (auto& b : byColor) b = 0;

    int r = 7, f = 0;
    for (char c : placement) {
        if (c == '/') {
            --r;
            f = 0;
        } else if (c >= '1' && c <= '8') {
            f += c - '0';
        } else {
            const std::string_view pcs = "PNBRQKpnbrqk";
            const size_t idx = pcs.find(c);
            if (idx == std::string_view::npos || f > 7 || r < 0) return false;
            put_piece(Piece(idx), make_square(f, r));
            ++f;
        }
    }
    if (popcount(pieces(WHITE, KING)) != 1 || popcount(pieces(BLACK, KING)) != 1) return false;

    stm = side == "b" ? BLACK : WHITE;

    states.clear();
    states.reserve(1024);
    states.emplace_back();
    StateInfo& st = states.back();
    std::memset(&st, 0, sizeof(st));

    st.castling = 0;
    for (char c : castle) {
        if (c == 'K' && board[E1] == W_KING && board[H1] == W_ROOK) st.castling |= WHITE_OO;
        if (c == 'Q' && board[E1] == W_KING && board[A1] == W_ROOK) st.castling |= WHITE_OOO;
        if (c == 'k' && board[E8] == B_KING && board[H8] == B_ROOK) st.castling |= BLACK_OO;
        if (c == 'q' && board[E8] == B_KING && board[A8] == B_ROOK) st.castling |= BLACK_OOO;
    }

    st.ep = NO_SQ;
    if (ep.size() == 2 && ep[0] >= 'a' && ep[0] <= 'h' && ep[1] == (stm == WHITE ? '6' : '3')) {
        Square s = make_square(ep[0] - 'a', ep[1] - '1');
        if ((PawnAttacks[~stm][s] & pieces(stm, PAWN)) && (pieces(~stm, PAWN) & square_bb(s - pawn_push(stm)))
            && !(pieces() & (square_bb(s) | square_bb(s + pawn_push(stm))))) {
            st.ep = s;
            if (!ep_capture_exists(s)) st.ep = NO_SQ;
        }
    }

    st.rule50 = std::max(0, r50);
    st.pliesFromNull = 0;
    st.captured = NO_PIECE;
    st.move = Move::none();
    gamePly = std::max(2 * (fullmove - 1), 0) + (stm == BLACK);

    compute_keys(st);
    st.checkers = attackers_to(king_sq(stm)) & pieces(~stm);
    update_pins(st);

    // side not to move must not be in check
    if (attackers_to(king_sq(~stm)) & pieces(stm)) return false;
    return true;
}

std::string Position::fen() const {
    std::string s;
    for (int r = 7; r >= 0; --r) {
        int emptyCnt = 0;
        for (int f = 0; f < 8; ++f) {
            Piece p = board[make_square(f, r)];
            if (p == NO_PIECE) {
                ++emptyCnt;
                continue;
            }
            if (emptyCnt) s += char('0' + emptyCnt), emptyCnt = 0;
            s += "PNBRQKpnbrqk"[p];
        }
        if (emptyCnt) s += char('0' + emptyCnt);
        if (r) s += '/';
    }
    s += stm == WHITE ? " w " : " b ";
    int cr = castling_rights();
    if (!cr) s += '-';
    if (cr & WHITE_OO) s += 'K';
    if (cr & WHITE_OOO) s += 'Q';
    if (cr & BLACK_OO) s += 'k';
    if (cr & BLACK_OOO) s += 'q';
    s += ' ';
    if (ep_square() == NO_SQ) s += '-';
    else {
        s += char('a' + file_of(ep_square()));
        s += char('1' + rank_of(ep_square()));
    }
    s += ' ' + std::to_string(rule50()) + ' ' + std::to_string(1 + (gamePly - (stm == BLACK)) / 2);
    return s;
}

Bitboard Position::attackers_to(Square s, Bitboard occ) const {
    return (PawnAttacks[BLACK][s] & pieces(WHITE, PAWN))
         | (PawnAttacks[WHITE][s] & pieces(BLACK, PAWN))
         | (KnightAttacks[s] & pieces(KNIGHT))
         | (rook_attacks(s, occ) & pieces(ROOK, QUEEN))
         | (bishop_attacks(s, occ) & pieces(BISHOP, QUEEN))
         | (KingAttacks[s] & pieces(KING));
}

Bitboard Position::attacks_by(Color c) const {
    Bitboard occ = pieces();
    Bitboard att = c == WHITE ? pawn_attacks_bb<WHITE>(pieces(c, PAWN)) : pawn_attacks_bb<BLACK>(pieces(c, PAWN));
    Bitboard b = pieces(c, KNIGHT);
    while (b) att |= KnightAttacks[pop_lsb(b)];
    b = pieces(c, BISHOP, QUEEN);
    while (b) att |= bishop_attacks(pop_lsb(b), occ);
    b = pieces(c, ROOK, QUEEN);
    while (b) att |= rook_attacks(pop_lsb(b), occ);
    att |= KingAttacks[king_sq(c)];
    return att;
}

bool Position::legal(Move m) const {
    const Color us = stm;
    const Square from = m.from(), to = m.to();
    const Square ksq = king_sq(us);

    if (m.type() == EN_PASSANT) {
        Square capsq = to - pawn_push(us);
        Bitboard occ = (pieces() ^ square_bb(from) ^ square_bb(capsq)) | square_bb(to);
        return !(rook_attacks(ksq, occ) & pieces(~us, ROOK, QUEEN))
            && !(bishop_attacks(ksq, occ) & pieces(~us, BISHOP, QUEEN));
    }
    if (m.type() == CASTLING) return true;
    if (from == ksq) return !(attackers_to(to, pieces() ^ square_bb(from)) & pieces(~us));
    return !(blockers(us) & square_bb(from)) || aligned(from, to, ksq);
}

bool Position::pseudo_legal(Move m) const {
    if (!m.is_ok()) return false;
    const Color us = stm;
    const Square from = m.from(), to = m.to();
    const Piece pc = board[from];
    if (pc == NO_PIECE || color_of(pc) != us) return false;

    if (m.type() != NORMAL) {
        MoveList list;
        generate<GEN_ALL>(*this, list);
        return list.contains(m);
    }
    if ((m.raw() >> 12) & 3) return false;
    if (pieces(us) & square_bb(to)) return false;

    const PieceType pt = type_of(pc);
    if (pt == PAWN) {
        if ((BB::Rank1 | BB::Rank8) & square_bb(to)) return false;
        const int up = pawn_push(us);
        bool capture = PawnAttacks[us][from] & pieces(~us) & square_bb(to);
        bool push = from + up == to && empty(to);
        bool dbl = from + 2 * up == to && relative_rank(us, rank_of(from)) == 1 && empty(to) && empty(from + up);
        if (!capture && !push && !dbl) return false;
    } else if (!(attacks_bb(pt, from, pieces()) & square_bb(to)))
        return false;

    if (checkers() && pt != KING) {
        if (more_than_one(checkers())) return false;
        if (!((between_bb(king_sq(us), lsb(checkers())) | checkers()) & square_bb(to))) return false;
    }
    return true;
}

bool Position::gives_check(Move m) const {
    const Color us = stm, them = ~us;
    const Square from = m.from(), to = m.to();
    const Square ksq = king_sq(them);
    const Bitboard kbb = square_bb(ksq);
    const PieceType pt = type_of(board[from]);
    Bitboard occ = (pieces() ^ square_bb(from)) | square_bb(to);

    switch (m.type()) {
    case NORMAL:
        if (pt == PAWN) {
            if (PawnAttacks[us][to] & kbb) return true;
        } else if (pt != KING && (attacks_bb(pt, to, occ) & kbb))
            return true;
        return (blockers(them) & square_bb(from)) && !aligned(from, to, ksq);
    case PROMOTION:
        if (attacks_bb(m.promotion(), to, occ) & kbb) return true;
        return (blockers(them) & square_bb(from)) && !aligned(from, to, ksq);
    case EN_PASSANT: {
        if (PawnAttacks[us][to] & kbb) return true;
        occ ^= square_bb(to - pawn_push(us));
        return (rook_attacks(ksq, occ) & pieces(us, ROOK, QUEEN)) || (bishop_attacks(ksq, occ) & pieces(us, BISHOP, QUEEN));
    }
    case CASTLING: {
        Square rfrom, rto;
        castling_rook(to, rfrom, rto);
        Bitboard occ2 = (pieces() ^ square_bb(from) ^ square_bb(rfrom)) | square_bb(to) | square_bb(rto);
        return rook_attacks(rto, occ2) & kbb;
    }
    }
    return false;
}

bool Position::see_ge(Move m, int threshold) const {
    if (m.type() != NORMAL) return 0 >= threshold;

    const Square from = m.from(), to = m.to();
    int swap = (board[to] == NO_PIECE ? 0 : SeeValue[type_of(board[to])]) - threshold;
    if (swap < 0) return false;
    swap = SeeValue[type_of(board[from])] - swap;
    if (swap <= 0) return true;

    Bitboard occ = pieces() ^ square_bb(from) ^ square_bb(to);
    Color side = stm;
    Bitboard attackers = attackers_to(to, occ);
    const Bitboard bishops = pieces(BISHOP, QUEEN), rooks = pieces(ROOK, QUEEN);
    int res = 1;

    while (true) {
        side = ~side;
        attackers &= occ;
        Bitboard mine = attackers & pieces(side);
        if (!mine) break;
        if (pinners(side) & occ) {
            mine &= ~blockers(side);
            if (!mine) break;
        }
        res ^= 1;

        Bitboard bb;
        if ((bb = mine & pieces(PAWN))) {
            if ((swap = SeeValue[PAWN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & bishops;
        } else if ((bb = mine & pieces(KNIGHT))) {
            if ((swap = SeeValue[KNIGHT] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
        } else if ((bb = mine & pieces(BISHOP))) {
            if ((swap = SeeValue[BISHOP] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= bishop_attacks(to, occ) & bishops;
        } else if ((bb = mine & pieces(ROOK))) {
            if ((swap = SeeValue[ROOK] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= rook_attacks(to, occ) & rooks;
        } else if ((bb = mine & pieces(QUEEN))) {
            if ((swap = SeeValue[QUEEN] - swap) < res) break;
            occ ^= square_bb(lsb(bb));
            attackers |= (bishop_attacks(to, occ) & bishops) | (rook_attacks(to, occ) & rooks);
        } else
            return (attackers & ~pieces(side)) ? res ^ 1 : res;
    }
    return bool(res);
}

void Position::make(Move m) {
    if (states.size() == states.capacity()) states.reserve(states.size() * 2);
    states.push_back(states.back());
    StateInfo& st = states.back();

    const Color us = stm, them = ~us;
    const Square from = m.from(), to = m.to();
    const Piece pc = board[from];
    const PieceType pt = type_of(pc);

    st.move = m;
    st.rule50++;
    st.pliesFromNull++;
    st.dirty.adds = st.dirty.subs = 0;
    st.captured = NO_PIECE;
    ++gamePly;

    Key key = st.key ^ Zobrist::side;
    if (st.ep != NO_SQ) {
        key ^= Zobrist::enpassant[file_of(st.ep)];
        st.ep = NO_SQ;
    }

    auto keyTouch = [&](Piece p, Square s) {
        Key k = Zobrist::psq[p][s];
        key ^= k;
        PieceType t = type_of(p);
        if (t == PAWN) st.pawnKey ^= k;
        else {
            st.nonPawnKey[color_of(p)] ^= k;
            if (t == KNIGHT || t == BISHOP || t == KING) st.minorKey ^= k;
            if (t == ROOK || t == QUEEN || t == KING) st.majorKey ^= k;
        }
    };

    if (m.type() == CASTLING) {
        Square rfrom, rto;
        castling_rook(to, rfrom, rto);
        Piece rook = board[rfrom];
        move_piece(from, to);
        move_piece(rfrom, rto);
        keyTouch(pc, from), keyTouch(pc, to);
        keyTouch(rook, rfrom), keyTouch(rook, rto);
        st.dirty.sub(pc, from), st.dirty.add(pc, to);
        st.dirty.sub(rook, rfrom), st.dirty.add(rook, rto);
    } else {
        Square capsq = m.type() == EN_PASSANT ? to - pawn_push(us) : to;
        Piece captured = board[capsq];
        if (captured != NO_PIECE) {
            remove_piece(capsq);
            keyTouch(captured, capsq);
            st.dirty.sub(captured, capsq);
            st.captured = captured;
            st.rule50 = 0;
        }

        move_piece(from, to);
        keyTouch(pc, from);
        st.dirty.sub(pc, from);

        if (pt == PAWN) {
            st.rule50 = 0;
            if ((int(to) ^ int(from)) == 16) {
                Square epSq = from + pawn_push(us);
                if (PawnAttacks[us][epSq] & pieces(them, PAWN)) st.ep = epSq;
            } else if (m.type() == PROMOTION) {
                Piece promo = make_piece(us, m.promotion());
                remove_piece(to);
                put_piece(promo, to);
                keyTouch(promo, to);
                st.dirty.add(promo, to);
            }
        }
        if (m.type() != PROMOTION) {
            keyTouch(pc, to);
            st.dirty.add(pc, to);
        }
    }

    const int newCastling = st.castling & CastleMask[from] & CastleMask[to];
    if (newCastling != st.castling) {
        key ^= Zobrist::castling[st.castling] ^ Zobrist::castling[newCastling];
        st.castling = newCastling;
    }

    stm = them;
    if (st.ep != NO_SQ) {
        if (ep_capture_exists(st.ep)) key ^= Zobrist::enpassant[file_of(st.ep)];
        else st.ep = NO_SQ;
    }
    st.key = key;
    st.checkers = attackers_to(king_sq(them)) & pieces(us);
    update_pins(st);
}

bool Position::ep_capture_exists(Square epSq) const {
    for (Bitboard b = PawnAttacks[~stm][epSq] & pieces(stm, PAWN); b;)
        if (legal(Move::make<EN_PASSANT>(pop_lsb(b), epSq))) return true;
    return false;
}

void Position::unmake(Move m) {
    const StateInfo& st = states.back();
    stm = ~stm;
    const Color us = stm;
    const Square from = m.from(), to = m.to();

    if (m.type() == CASTLING) {
        Square rfrom, rto;
        castling_rook(to, rfrom, rto);
        move_piece(to, from);
        move_piece(rto, rfrom);
    } else {
        if (m.type() == PROMOTION) {
            remove_piece(to);
            put_piece(make_piece(us, PAWN), to);
        }
        move_piece(to, from);
        if (st.captured != NO_PIECE) {
            Square capsq = m.type() == EN_PASSANT ? to - pawn_push(us) : to;
            put_piece(st.captured, capsq);
        }
    }
    states.pop_back();
    --gamePly;
}

void Position::make_null() {
    if (states.size() == states.capacity()) states.reserve(states.size() * 2);
    states.push_back(states.back());
    StateInfo& st = states.back();
    st.move = Move::null();
    st.rule50++;
    st.pliesFromNull = 0;
    st.captured = NO_PIECE;
    st.dirty.adds = st.dirty.subs = 0;
    st.key ^= Zobrist::side;
    if (st.ep != NO_SQ) {
        st.key ^= Zobrist::enpassant[file_of(st.ep)];
        st.ep = NO_SQ;
    }
    st.checkers = 0;
    stm = ~stm;
    ++gamePly;
}

void Position::unmake_null() {
    states.pop_back();
    stm = ~stm;
    --gamePly;
}

Key Position::key_after(Move m) const {
    const Square from = m.from(), to = m.to();
    const Piece pc = board[from];
    Key k = key() ^ Zobrist::side ^ Zobrist::psq[pc][from] ^ Zobrist::psq[pc][to];
    if (board[to] != NO_PIECE && m.type() != CASTLING) k ^= Zobrist::psq[board[to]][to];
    return k;
}

bool Position::is_repetition(int ply) const {
    const StateInfo& cur = states.back();
    const int end = std::min({cur.rule50, cur.pliesFromNull, int(states.size()) - 1});
    int count = 0;
    for (int i = 4; i <= end; i += 2) {
        if (states[states.size() - 1 - i].key == cur.key) {
            if (i < ply || ++count == 2) return true;
        }
    }
    return false;
}

bool Position::insufficient_material() const {
    if (pieces(PAWN) || pieces(ROOK) || pieces(QUEEN)) return false;
    return popcount(pieces(KNIGHT) | pieces(BISHOP)) <= 1;
}

bool Position::is_draw(int ply) const {
    if (rule50() >= 100) {
        if (!checkers()) return true;
        MoveList list;
        generate_legal(*this, list);
        if (list.size()) return true;
    }
    return is_repetition(ply) || insufficient_material();
}

bool Position::has_game_cycle(int ply) const {
    const StateInfo& cur = states.back();
    const int end = std::min({cur.rule50, cur.pliesFromNull, int(states.size()) - 1});
    if (end < 3) return false;

    const Key original = cur.key;
    Key other = original ^ state(1).key ^ Zobrist::side;

    for (int i = 3; i <= end; i += 2) {
        other ^= state(i - 1).key ^ state(i).key ^ Zobrist::side;
        if (other != 0) continue;

        Key moveKey = original ^ state(i).key;
        int j = cuckoo_h1(moveKey);
        if (cuckoo[j] != moveKey) {
            j = cuckoo_h2(moveKey);
            if (cuckoo[j] != moveKey) continue;
        }
        Move move = cuckooMove[j];
        if (between_bb(move.from(), move.to()) & pieces()) continue;
        if (ply > i) return true;
    }
    return false;
}

int Position::material() const {
    return 3 * popcount(pieces(KNIGHT)) + 3 * popcount(pieces(BISHOP)) + 5 * popcount(pieces(ROOK)) + 9 * popcount(pieces(QUEEN));
}

std::string move_to_uci(Move m) {
    if (m == Move::none()) return "0000";
    if (m == Move::null()) return "0000";
    std::string s;
    s += char('a' + file_of(m.from()));
    s += char('1' + rank_of(m.from()));
    s += char('a' + file_of(m.to()));
    s += char('1' + rank_of(m.to()));
    if (m.type() == PROMOTION) s += "nbrq"[m.promotion() - KNIGHT];
    return s;
}

Move uci_to_move(const Position& pos, const std::string& s) {
    MoveList list;
    generate_legal(pos, list);
    for (int i = 0; i < list.size(); ++i)
        if (move_to_uci(list[i]) == s) return list[i];
    return Move::none();
}
