#pragma once

#include "types.h"

namespace BB {

constexpr Bitboard FileA = 0x0101010101010101ULL;
constexpr Bitboard FileH = FileA << 7;
constexpr Bitboard Rank1 = 0xFFULL;
constexpr Bitboard Rank2 = Rank1 << 8;
constexpr Bitboard Rank3 = Rank1 << 16;
constexpr Bitboard Rank4 = Rank1 << 24;
constexpr Bitboard Rank5 = Rank1 << 32;
constexpr Bitboard Rank6 = Rank1 << 40;
constexpr Bitboard Rank7 = Rank1 << 48;
constexpr Bitboard Rank8 = Rank1 << 56;

constexpr Bitboard file_bb(int f) { return FileA << f; }
constexpr Bitboard rank_bb(int r) { return Rank1 << (8 * r); }

void init();

} // namespace BB

constexpr Bitboard square_bb(Square s) { return 1ULL << s; }

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }
inline Square lsb(Bitboard b) { return Square(__builtin_ctzll(b)); }
inline Square msb(Bitboard b) { return Square(63 ^ __builtin_clzll(b)); }
inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
inline bool more_than_one(Bitboard b) { return b & (b - 1); }

template<int D>
constexpr Bitboard shift(Bitboard b) {
    if constexpr (D == 8) return b << 8;
    else if constexpr (D == -8) return b >> 8;
    else if constexpr (D == 16) return b << 16;
    else if constexpr (D == -16) return b >> 16;
    else if constexpr (D == 1) return (b & ~BB::FileH) << 1;
    else if constexpr (D == -1) return (b & ~BB::FileA) >> 1;
    else if constexpr (D == 9) return (b & ~BB::FileH) << 9;
    else if constexpr (D == 7) return (b & ~BB::FileA) << 7;
    else if constexpr (D == -7) return (b & ~BB::FileH) >> 7;
    else if constexpr (D == -9) return (b & ~BB::FileA) >> 9;
    else return 0;
}

template<Color C>
constexpr Bitboard pawn_attacks_bb(Bitboard b) {
    return C == WHITE ? shift<9>(b) | shift<7>(b) : shift<-7>(b) | shift<-9>(b);
}

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned shift;

    unsigned index(Bitboard occ) const { return unsigned(((occ & mask) * magic) >> shift); }
};

extern Magic RookMagics[64];
extern Magic BishopMagics[64];
extern Bitboard PawnAttacks[2][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
extern Bitboard BetweenBB[64][64];
extern Bitboard LineBB[64][64];

inline Bitboard rook_attacks(Square s, Bitboard occ) {
    const Magic& m = RookMagics[s];
    return m.attacks[m.index(occ)];
}

inline Bitboard bishop_attacks(Square s, Bitboard occ) {
    const Magic& m = BishopMagics[s];
    return m.attacks[m.index(occ)];
}

inline Bitboard queen_attacks(Square s, Bitboard occ) {
    return rook_attacks(s, occ) | bishop_attacks(s, occ);
}

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
    case KNIGHT: return KnightAttacks[s];
    case BISHOP: return bishop_attacks(s, occ);
    case ROOK: return rook_attacks(s, occ);
    case QUEEN: return queen_attacks(s, occ);
    case KING: return KingAttacks[s];
    default: return 0;
    }
}

// squares strictly between a and b when aligned, else empty
inline Bitboard between_bb(Square a, Square b) { return BetweenBB[a][b]; }
inline Bitboard line_bb(Square a, Square b) { return LineBB[a][b]; }
inline bool aligned(Square a, Square b, Square c) { return LineBB[a][b] & square_bb(c); }
