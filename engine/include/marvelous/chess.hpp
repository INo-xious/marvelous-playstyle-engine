#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace marvelous {

enum class Color { White, Black };

struct Move {
    int from = -1;
    int to = -1;
    char promotion = '\0';
    bool en_passant = false;
    bool castle = false;

    bool operator==(const Move&) const = default;
};

struct SearchResult {
    Move best_move;
    int score = 0;
    std::uint64_t nodes = 0;
};

class Board {
  public:
    static constexpr const char* kStartFen =
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Board();
    explicit Board(const std::string& fen);

    void load_fen(const std::string& fen);
    [[nodiscard]] std::string fen() const;
    [[nodiscard]] Color side_to_move() const { return side_; }
    [[nodiscard]] std::vector<Move> legal_moves() const;
    [[nodiscard]] bool in_check(Color color) const;
    [[nodiscard]] bool is_capture(const Move& move) const;
    [[nodiscard]] std::optional<Move> parse_uci_move(const std::string& text) const;
    [[nodiscard]] std::uint64_t perft(int depth) const;
    [[nodiscard]] int evaluate() const;

    void make_move(const Move& move);

    static std::string move_to_uci(const Move& move);

  private:
    std::array<int, 64> squares_{};
    Color side_ = Color::White;
    int castling_ = 0;
    int ep_square_ = -1;
    int halfmove_clock_ = 0;
    int fullmove_number_ = 1;

    [[nodiscard]] std::vector<Move> pseudo_legal_moves() const;
    [[nodiscard]] bool square_attacked(int square, Color by_color) const;
    [[nodiscard]] int king_square(Color color) const;
};

SearchResult search(const Board& board, int depth);

}  // namespace marvelous
