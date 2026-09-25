#include "bitboard.h"

#include <cstring>

Magic RookMagics[64];
Magic BishopMagics[64];
Bitboard PawnAttacks[2][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];

namespace {

Bitboard RookTable[0x19000];
Bitboard BishopTable[0x1480];

constexpr int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

Bitboard sliding_attacks(const int dirs[4][2], Square s, Bitboard occ) {
    Bitboard att = 0;
    for (int d = 0; d < 4; ++d) {
        int f = file_of(s) + dirs[d][0], r = rank_of(s) + dirs[d][1];
        while (f >= 0 && f < 8 && r >= 0 && r < 8) {
            Bitboard b = square_bb(make_square(f, r));
            att |= b;
            if (occ & b) break;
            f += dirs[d][0];
            r += dirs[d][1];
        }
    }
    return att;
}

struct Prng {
    u64 s;
    u64 next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    u64 sparse() { return next() & next() & next(); }
};

void init_magics(const int dirs[4][2], Magic magics[], Bitboard* table) {
    static Bitboard occupancy[4096], reference[4096];
    static int epoch[4096];
    static int cnt = 0;
    Prng rng{0x9E3779B97F4A7C15ULL};

    for (Square s = A1; s <= H8; ++s) {
        Bitboard edges = ((BB::Rank1 | BB::Rank8) & ~BB::rank_bb(rank_of(s)))
                       | ((BB::FileA | BB::FileH) & ~BB::file_bb(file_of(s)));
        Magic& m = magics[s];
        m.mask = sliding_attacks(dirs, s, 0) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = s == A1 ? table : magics[s - 1].attacks + (1 << (64 - magics[s - 1].shift));

        int size = 0;
        Bitboard b = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attacks(dirs, s, b);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);

        for (int i = 0; i < size;) {
            do
                m.magic = rng.sparse();
            while (popcount((m.magic * m.mask) >> 56) < 6);

            ++cnt;
            for (i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx] = cnt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i])
                    break;
            }
        }
    }
}

} // namespace

void BB::init() {
    for (Square s = A1; s <= H8; ++s) {
        Bitboard b = square_bb(s);
        PawnAttacks[WHITE][s] = pawn_attacks_bb<WHITE>(b);
        PawnAttacks[BLACK][s] = pawn_attacks_bb<BLACK>(b);

        KnightAttacks[s] = 0;
        KingAttacks[s] = 0;
        const int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        for (auto& d : kn) {
            int f = file_of(s) + d[0], r = rank_of(s) + d[1];
            if (f >= 0 && f < 8 && r >= 0 && r < 8) KnightAttacks[s] |= square_bb(make_square(f, r));
        }
        for (int df = -1; df <= 1; ++df)
            for (int dr = -1; dr <= 1; ++dr) {
                if (!df && !dr) continue;
                int f = file_of(s) + df, r = rank_of(s) + dr;
                if (f >= 0 && f < 8 && r >= 0 && r < 8) KingAttacks[s] |= square_bb(make_square(f, r));
            }
    }

    init_magics(RookDirs, RookMagics, RookTable);
    init_magics(BishopDirs, BishopMagics, BishopTable);

    for (Square a = A1; a <= H8; ++a)
        for (Square b = A1; b <= H8; ++b) {
            BetweenBB[a][b] = LineBB[a][b] = 0;
            if (a == b) continue;
            if (sliding_attacks(BishopDirs, a, 0) & square_bb(b)) {
                LineBB[a][b] = (sliding_attacks(BishopDirs, a, 0) & sliding_attacks(BishopDirs, b, 0)) | square_bb(a) | square_bb(b);
                BetweenBB[a][b] = sliding_attacks(BishopDirs, a, square_bb(b)) & sliding_attacks(BishopDirs, b, square_bb(a));
            } else if (sliding_attacks(RookDirs, a, 0) & square_bb(b)) {
                LineBB[a][b] = (sliding_attacks(RookDirs, a, 0) & sliding_attacks(RookDirs, b, 0)) | square_bb(a) | square_bb(b);
                BetweenBB[a][b] = sliding_attacks(RookDirs, a, square_bb(b)) & sliding_attacks(RookDirs, b, square_bb(a));
            }
        }
}
