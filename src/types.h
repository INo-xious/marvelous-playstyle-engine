#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <malloc.h>
#endif

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8 = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;

using Bitboard = u64;

inline void* aligned_malloc(size_t align, size_t size) {
#if defined(_WIN32)
    return _aligned_malloc(size, align);
#else
    return std::aligned_alloc(align, (size + align - 1) / align * align);
#endif
}

inline void aligned_free(void* p) {
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
using Key = u64;

constexpr int MAX_PLY = 246;
constexpr int MAX_MOVES = 256;

enum Color : int { WHITE, BLACK, COLOR_NB = 2 };

constexpr Color operator~(Color c) { return Color(c ^ 1); }

enum PieceType : int { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NO_PIECE_TYPE, PIECE_TYPE_NB = 6 };

enum Piece : int {
    W_PAWN, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
    B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
    NO_PIECE, PIECE_NB = 12
};

constexpr Piece make_piece(Color c, PieceType pt) { return Piece(pt + 6 * c); }
constexpr PieceType type_of(Piece p) { return PieceType(p % 6); }
constexpr Color color_of(Piece p) { return Color(p >= 6); }

enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQ, SQUARE_NB = 64
};

constexpr Square operator+(Square s, int d) { return Square(int(s) + d); }
constexpr Square operator-(Square s, int d) { return Square(int(s) - d); }
inline Square& operator++(Square& s) { return s = Square(int(s) + 1); }

constexpr int file_of(Square s) { return s & 7; }
constexpr int rank_of(Square s) { return s >> 3; }
constexpr Square make_square(int f, int r) { return Square(r * 8 + f); }
constexpr Square flip_rank(Square s) { return Square(s ^ 56); }
constexpr Square flip_file(Square s) { return Square(s ^ 7); }
constexpr Square relative_square(Color c, Square s) { return Square(s ^ (56 * c)); }
constexpr int relative_rank(Color c, int r) { return c == WHITE ? r : 7 - r; }
constexpr int pawn_push(Color c) { return c == WHITE ? 8 : -8; }

enum CastlingRight : int {
    NO_CASTLING = 0,
    WHITE_OO = 1,
    WHITE_OOO = 2,
    BLACK_OO = 4,
    BLACK_OOO = 8,
    ALL_CASTLING = 15
};

using Value = int;

constexpr Value VALUE_ZERO = 0;
constexpr Value VALUE_DRAW = 0;
constexpr Value VALUE_INFINITE = 32001;
constexpr Value VALUE_NONE = 32002;
constexpr Value VALUE_MATE = 32000;
constexpr Value VALUE_MATE_IN_MAX_PLY = VALUE_MATE - MAX_PLY;
constexpr Value VALUE_TB = VALUE_MATE_IN_MAX_PLY - 1;
constexpr Value VALUE_TB_WIN_IN_MAX_PLY = VALUE_TB - MAX_PLY;

constexpr Value mate_in(int ply) { return VALUE_MATE - ply; }
constexpr Value mated_in(int ply) { return -VALUE_MATE + ply; }
constexpr bool is_win(Value v) { return v >= VALUE_TB_WIN_IN_MAX_PLY; }
constexpr bool is_loss(Value v) { return v <= -VALUE_TB_WIN_IN_MAX_PLY; }
constexpr bool is_decisive(Value v) { return is_win(v) || is_loss(v); }

// from 0-5, to 6-11, promotion piece 12-13, move type 14-15
enum MoveType : u16 {
    NORMAL = 0,
    PROMOTION = 1 << 14,
    EN_PASSANT = 2 << 14,
    CASTLING = 3 << 14
};

class Move {
public:
    Move() = default;
    constexpr explicit Move(u16 d) : data(d) {}
    constexpr Move(Square from, Square to) : data(u16(from | (to << 6))) {}

    template<MoveType T>
    static constexpr Move make(Square from, Square to, PieceType promo = KNIGHT) {
        return Move(u16(T | ((promo - KNIGHT) << 12) | (to << 6) | from));
    }

    constexpr Square from() const { return Square(data & 0x3F); }
    constexpr Square to() const { return Square((data >> 6) & 0x3F); }
    constexpr MoveType type() const { return MoveType(data & (3 << 14)); }
    constexpr PieceType promotion() const { return PieceType(((data >> 12) & 3) + KNIGHT); }
    constexpr u16 raw() const { return data; }
    constexpr int from_to() const { return data & 0xFFF; }

    constexpr bool is_ok() const { return data != 0 && data != 65; }
    static constexpr Move none() { return Move(0); }
    static constexpr Move null() { return Move(65); }

    constexpr bool operator==(const Move& m) const { return data == m.data; }
    constexpr bool operator!=(const Move& m) const { return data != m.data; }
    constexpr explicit operator bool() const { return data != 0; }

private:
    u16 data = 0;
};

enum Bound : u8 { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

constexpr Value PieceValue[7] = {100, 300, 300, 500, 900, 0, 0};
