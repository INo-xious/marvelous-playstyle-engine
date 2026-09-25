#include "movegen.h"

namespace {

template<GenType T>
void add_promotions(MoveList& list, Square from, Square to, bool capture) {
    if (T != GEN_QUIET) list.add(Move::make<PROMOTION>(from, to, QUEEN));
    if (T == GEN_ALL || (T == GEN_NOISY && capture) || (T == GEN_QUIET && !capture)) {
        list.add(Move::make<PROMOTION>(from, to, KNIGHT));
        list.add(Move::make<PROMOTION>(from, to, ROOK));
        list.add(Move::make<PROMOTION>(from, to, BISHOP));
    }
}

template<GenType T, Color Us>
void gen_pawns(const Position& pos, MoveList& list, Bitboard target) {
    constexpr Color Them = Us == WHITE ? BLACK : WHITE;
    constexpr int Up = Us == WHITE ? 8 : -8;
    constexpr int UpRight = Us == WHITE ? 9 : -7;
    constexpr int UpLeft = Us == WHITE ? 7 : -9;
    constexpr Bitboard PromoRank = Us == WHITE ? BB::Rank7 : BB::Rank2;
    constexpr Bitboard DoubleRank = Us == WHITE ? BB::Rank3 : BB::Rank6;

    const Bitboard pawns = pos.pieces(Us, PAWN);
    const Bitboard promoters = pawns & PromoRank;
    const Bitboard others = pawns & ~PromoRank;
    const Bitboard empty = ~pos.pieces();
    const Bitboard enemies = pos.pieces(Them) & target;

    if constexpr (T != GEN_NOISY) {
        Bitboard single = shift<Up>(others) & empty;
        Bitboard dbl = shift<Up>(single & DoubleRank) & empty & target;
        single &= target;
        while (single) {
            Square to = pop_lsb(single);
            list.add(Move(to - Up, to));
        }
        while (dbl) {
            Square to = pop_lsb(dbl);
            list.add(Move(to - 2 * Up, to));
        }
    }

    if (promoters) {
        Bitboard push = shift<Up>(promoters) & empty & target;
        Bitboard capR = shift<UpRight>(promoters) & enemies;
        Bitboard capL = shift<UpLeft>(promoters) & enemies;
        while (capR) {
            Square to = pop_lsb(capR);
            add_promotions<T>(list, to - UpRight, to, true);
        }
        while (capL) {
            Square to = pop_lsb(capL);
            add_promotions<T>(list, to - UpLeft, to, true);
        }
        while (push) {
            Square to = pop_lsb(push);
            add_promotions<T>(list, to - Up, to, false);
        }
    }

    if constexpr (T != GEN_QUIET) {
        Bitboard capR = shift<UpRight>(others) & enemies;
        Bitboard capL = shift<UpLeft>(others) & enemies;
        while (capR) {
            Square to = pop_lsb(capR);
            list.add(Move(to - UpRight, to));
        }
        while (capL) {
            Square to = pop_lsb(capL);
            list.add(Move(to - UpLeft, to));
        }

        Square ep = pos.ep_square();
        if (ep != NO_SQ && (target & (square_bb(ep) | square_bb(ep - Up)))) {
            Bitboard b = others & PawnAttacks[Them][ep];
            while (b) list.add(Move::make<EN_PASSANT>(pop_lsb(b), ep));
        }
    }
}

template<GenType T>
Bitboard destinations(const Position& pos, Color us) {
    if constexpr (T == GEN_NOISY) return pos.pieces(~us);
    else if constexpr (T == GEN_QUIET) return ~pos.pieces();
    else return ~pos.pieces(us);
}

template<GenType T>
void gen_pieces(const Position& pos, MoveList& list, Bitboard target) {
    const Color us = pos.side_to_move();
    const Bitboard occ = pos.pieces();
    const Bitboard dest = destinations<T>(pos, us) & target;

    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
        Bitboard froms = pos.pieces(us, pt);
        while (froms) {
            Square from = pop_lsb(froms);
            Bitboard b = attacks_bb(pt, from, occ) & dest;
            while (b) list.add(Move(from, pop_lsb(b)));
        }
    }
}

template<GenType T>
void gen_king(const Position& pos, MoveList& list) {
    const Color us = pos.side_to_move();
    const Square ksq = pos.king_sq(us);
    Bitboard b = KingAttacks[ksq] & destinations<T>(pos, us);
    while (b) list.add(Move(ksq, pop_lsb(b)));
}

void gen_castling(const Position& pos, MoveList& list) {
    const Color us = pos.side_to_move();
    const Color them = ~us;
    const int cr = pos.castling_rights();
    const Bitboard occ = pos.pieces();
    auto safe = [&](Square s) { return !(pos.attackers_to(s, occ) & pos.pieces(them)); };

    if (us == WHITE) {
        if ((cr & WHITE_OO) && !(occ & (square_bb(F1) | square_bb(G1))) && safe(F1) && safe(G1))
            list.add(Move::make<CASTLING>(E1, G1));
        if ((cr & WHITE_OOO) && !(occ & (square_bb(B1) | square_bb(C1) | square_bb(D1))) && safe(D1) && safe(C1))
            list.add(Move::make<CASTLING>(E1, C1));
    } else {
        if ((cr & BLACK_OO) && !(occ & (square_bb(F8) | square_bb(G8))) && safe(F8) && safe(G8))
            list.add(Move::make<CASTLING>(E8, G8));
        if ((cr & BLACK_OOO) && !(occ & (square_bb(B8) | square_bb(C8) | square_bb(D8))) && safe(D8) && safe(C8))
            list.add(Move::make<CASTLING>(E8, C8));
    }
}

} // namespace

template<GenType T>
void generate(const Position& pos, MoveList& list) {
    const Color us = pos.side_to_move();
    const Bitboard checkers = pos.checkers();
    Bitboard target = ~0ULL;

    if (checkers) {
        if (more_than_one(checkers)) {
            gen_king<T>(pos, list);
            return;
        }
        target = between_bb(pos.king_sq(us), lsb(checkers)) | checkers;
    }

    if (us == WHITE) gen_pawns<T, WHITE>(pos, list, target);
    else gen_pawns<T, BLACK>(pos, list, target);

    gen_pieces<T>(pos, list, target);
    gen_king<T>(pos, list);

    if (T != GEN_NOISY && !checkers) gen_castling(pos, list);
}

template void generate<GEN_NOISY>(const Position&, MoveList&);
template void generate<GEN_QUIET>(const Position&, MoveList&);
template void generate<GEN_ALL>(const Position&, MoveList&);

void generate_legal(const Position& pos, MoveList& list) {
    MoveList all;
    generate<GEN_ALL>(pos, all);
    list.count = 0;
    for (int i = 0; i < all.count; ++i)
        if (pos.legal(all[i])) list.add(all[i]);
}
