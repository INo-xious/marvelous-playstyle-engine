#include "marvelous/chess.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace marvelous {
namespace {

constexpr int kEmpty = 0;
constexpr int kPawn = 1;
constexpr int kKnight = 2;
constexpr int kBishop = 3;
constexpr int kRook = 4;
constexpr int kQueen = 5;
constexpr int kKing = 6;

constexpr int kWhiteKingside = 1;
constexpr int kWhiteQueenside = 2;
constexpr int kBlackKingside = 4;
constexpr int kBlackQueenside = 8;

constexpr int kMateScore = 100000;
constexpr int kInfinity = 1000000;

int file_of(int square) { return square % 8; }
int rank_of(int square) { return square / 8; }
int square_of(int file, int rank) { return rank * 8 + file; }
bool on_board(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }

int color_sign(Color color) { return color == Color::White ? 1 : -1; }
Color opposite(Color color) { return color == Color::White ? Color::Black : Color::White; }
bool belongs_to(int piece, Color color) { return piece * color_sign(color) > 0; }

int piece_from_fen(char value) {
    const bool white = std::isupper(static_cast<unsigned char>(value));
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    int piece = 0;
    switch (lower) {
        case 'p': piece = kPawn; break;
        case 'n': piece = kKnight; break;
        case 'b': piece = kBishop; break;
        case 'r': piece = kRook; break;
        case 'q': piece = kQueen; break;
        case 'k': piece = kKing; break;
        default: throw std::invalid_argument("invalid FEN piece");
    }
    return white ? piece : -piece;
}

char piece_to_fen(int piece) {
    const int type = std::abs(piece);
    char value = '?';
    switch (type) {
        case kPawn: value = 'p'; break;
        case kKnight: value = 'n'; break;
        case kBishop: value = 'b'; break;
        case kRook: value = 'r'; break;
        case kQueen: value = 'q'; break;
        case kKing: value = 'k'; break;
        default: throw std::invalid_argument("invalid piece");
    }
    return piece > 0 ? static_cast<char>(std::toupper(value)) : value;
}

int square_from_text(const std::string& text) {
    if (text.size() != 2 || text[0] < 'a' || text[0] > 'h' || text[1] < '1' || text[1] > '8') {
        return -1;
    }
    return square_of(text[0] - 'a', text[1] - '1');
}

std::string square_to_text(int square) {
    std::string text = "a1";
    text[0] = static_cast<char>('a' + file_of(square));
    text[1] = static_cast<char>('1' + rank_of(square));
    return text;
}

int promotion_piece(char promotion) {
    switch (promotion) {
        case 'q': return kQueen;
        case 'r': return kRook;
        case 'b': return kBishop;
        case 'n': return kKnight;
        default: return kQueen;
    }
}

int material_value(int type) {
    switch (type) {
        case kPawn: return 100;
        case kKnight: return 320;
        case kBishop: return 330;
        case kRook: return 500;
        case kQueen: return 900;
        default: return 0;
    }
}

int negamax(const Board& board, int depth, int ply, int alpha, int beta,
            std::uint64_t& nodes) {
    ++nodes;
    const auto moves = board.legal_moves();
    if (moves.empty()) {
        return board.in_check(board.side_to_move()) ? -kMateScore + ply : 0;
    }
    if (depth == 0) {
        return board.evaluate();
    }

    std::vector<Move> ordered = moves;
    std::stable_sort(ordered.begin(), ordered.end(), [&board](const Move& a, const Move& b) {
        return board.is_capture(a) > board.is_capture(b);
    });

    int best = -kInfinity;
    for (const auto& move : ordered) {
        Board next = board;
        next.make_move(move);
        const int score = -negamax(next, depth - 1, ply + 1, -beta, -alpha, nodes);
        best = std::max(best, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) {
            break;
        }
    }
    return best;
}

}  // namespace

Board::Board() { load_fen(kStartFen); }

Board::Board(const std::string& fen_string) { load_fen(fen_string); }

void Board::load_fen(const std::string& fen_string) {
    squares_.fill(kEmpty);
    std::istringstream stream(fen_string);
    std::string placement;
    std::string active;
    std::string castling;
    std::string ep;
    if (!(stream >> placement >> active >> castling >> ep >> halfmove_clock_ >> fullmove_number_)) {
        throw std::invalid_argument("FEN must contain six fields");
    }

    int rank = 7;
    int file = 0;
    for (char value : placement) {
        if (value == '/') {
            if (file != 8 || rank == 0) {
                throw std::invalid_argument("invalid FEN board layout");
            }
            --rank;
            file = 0;
        } else if (std::isdigit(static_cast<unsigned char>(value))) {
            file += value - '0';
        } else {
            if (!on_board(file, rank)) {
                throw std::invalid_argument("invalid FEN board layout");
            }
            squares_[square_of(file, rank)] = piece_from_fen(value);
            ++file;
        }
    }
    if (rank != 0 || file != 8) {
        throw std::invalid_argument("invalid FEN board layout");
    }

    if (active == "w") {
        side_ = Color::White;
    } else if (active == "b") {
        side_ = Color::Black;
    } else {
        throw std::invalid_argument("invalid active color");
    }

    castling_ = 0;
    if (castling.find('K') != std::string::npos) castling_ |= kWhiteKingside;
    if (castling.find('Q') != std::string::npos) castling_ |= kWhiteQueenside;
    if (castling.find('k') != std::string::npos) castling_ |= kBlackKingside;
    if (castling.find('q') != std::string::npos) castling_ |= kBlackQueenside;
    ep_square_ = ep == "-" ? -1 : square_from_text(ep);
    if (ep != "-" && ep_square_ < 0) {
        throw std::invalid_argument("invalid en passant square");
    }
}

std::string Board::fen() const {
    std::ostringstream output;
    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const int piece = squares_[square_of(file, rank)];
            if (piece == kEmpty) {
                ++empty;
            } else {
                if (empty > 0) output << empty;
                empty = 0;
                output << piece_to_fen(piece);
            }
        }
        if (empty > 0) output << empty;
        if (rank > 0) output << '/';
    }
    output << (side_ == Color::White ? " w " : " b ");
    std::string castling;
    if (castling_ & kWhiteKingside) castling += 'K';
    if (castling_ & kWhiteQueenside) castling += 'Q';
    if (castling_ & kBlackKingside) castling += 'k';
    if (castling_ & kBlackQueenside) castling += 'q';
    output << (castling.empty() ? "-" : castling) << ' ';
    output << (ep_square_ < 0 ? "-" : square_to_text(ep_square_));
    output << ' ' << halfmove_clock_ << ' ' << fullmove_number_;
    return output.str();
}

bool Board::square_attacked(int target, Color by_color) const {
    const int target_file = file_of(target);
    const int target_rank = rank_of(target);
    const int sign = color_sign(by_color);

    for (int file_delta : {-1, 1}) {
        const int source_file = target_file - file_delta;
        const int source_rank = target_rank - sign;
        if (on_board(source_file, source_rank) &&
            squares_[square_of(source_file, source_rank)] == sign * kPawn) {
            return true;
        }
    }

    constexpr std::array<std::pair<int, int>, 8> knight_offsets{{
        {1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}}};
    for (const auto [df, dr] : knight_offsets) {
        const int file = target_file + df;
        const int rank = target_rank + dr;
        if (on_board(file, rank) && squares_[square_of(file, rank)] == sign * kKnight) {
            return true;
        }
    }

    constexpr std::array<std::pair<int, int>, 8> king_offsets{{
        {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
    for (const auto [df, dr] : king_offsets) {
        const int file = target_file + df;
        const int rank = target_rank + dr;
        if (on_board(file, rank) && squares_[square_of(file, rank)] == sign * kKing) {
            return true;
        }
    }

    const auto ray_attacked = [&](int df, int dr, int first_type, int second_type) {
        int file = target_file + df;
        int rank = target_rank + dr;
        while (on_board(file, rank)) {
            const int piece = squares_[square_of(file, rank)];
            if (piece != kEmpty) {
                return piece == sign * first_type || piece == sign * second_type;
            }
            file += df;
            rank += dr;
        }
        return false;
    };

    for (const auto [df, dr] : std::array<std::pair<int, int>, 4>{{
             {1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        if (ray_attacked(df, dr, kRook, kQueen)) return true;
    }
    for (const auto [df, dr] : std::array<std::pair<int, int>, 4>{{
             {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}}) {
        if (ray_attacked(df, dr, kBishop, kQueen)) return true;
    }
    return false;
}

int Board::king_square(Color color) const {
    const int king = color_sign(color) * kKing;
    for (int square = 0; square < 64; ++square) {
        if (squares_[square] == king) return square;
    }
    return -1;
}

bool Board::in_check(Color color) const {
    const int square = king_square(color);
    return square >= 0 && square_attacked(square, opposite(color));
}

std::vector<Move> Board::pseudo_legal_moves() const {
    std::vector<Move> moves;
    const int sign = color_sign(side_);
    const auto add_promotions = [&moves](int from, int to, bool en_passant = false) {
        for (char promotion : {'q', 'r', 'b', 'n'}) {
            moves.push_back({from, to, promotion, en_passant, false});
        }
    };

    for (int from = 0; from < 64; ++from) {
        const int piece = squares_[from];
        if (!belongs_to(piece, side_)) continue;
        const int type = std::abs(piece);
        const int file = file_of(from);
        const int rank = rank_of(from);

        if (type == kPawn) {
            const int next_rank = rank + sign;
            const int promotion_rank = side_ == Color::White ? 7 : 0;
            const int start_rank = side_ == Color::White ? 1 : 6;
            if (on_board(file, next_rank)) {
                const int one = square_of(file, next_rank);
                if (squares_[one] == kEmpty) {
                    if (next_rank == promotion_rank) add_promotions(from, one);
                    else moves.push_back({from, one});
                    const int two_rank = rank + 2 * sign;
                    const int two = square_of(file, two_rank);
                    if (rank == start_rank && squares_[two] == kEmpty) {
                        moves.push_back({from, two});
                    }
                }
            }
            for (int df : {-1, 1}) {
                const int capture_file = file + df;
                const int capture_rank = rank + sign;
                if (!on_board(capture_file, capture_rank)) continue;
                const int to = square_of(capture_file, capture_rank);
                const bool enemy = squares_[to] != kEmpty && !belongs_to(squares_[to], side_);
                const bool en_passant = to == ep_square_;
                if (!enemy && !en_passant) continue;
                if (capture_rank == promotion_rank) add_promotions(from, to, en_passant);
                else moves.push_back({from, to, '\0', en_passant, false});
            }
            continue;
        }

        if (type == kKnight) {
            constexpr std::array<std::pair<int, int>, 8> offsets{{
                {1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}}};
            for (const auto [df, dr] : offsets) {
                const int to_file = file + df;
                const int to_rank = rank + dr;
                if (!on_board(to_file, to_rank)) continue;
                const int to = square_of(to_file, to_rank);
                if (!belongs_to(squares_[to], side_)) moves.push_back({from, to});
            }
            continue;
        }

        if (type == kBishop || type == kRook || type == kQueen) {
            std::vector<std::pair<int, int>> directions;
            if (type == kBishop || type == kQueen) {
                directions.insert(directions.end(), {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}});
            }
            if (type == kRook || type == kQueen) {
                directions.insert(directions.end(), {{1, 0}, {-1, 0}, {0, 1}, {0, -1}});
            }
            for (const auto [df, dr] : directions) {
                int to_file = file + df;
                int to_rank = rank + dr;
                while (on_board(to_file, to_rank)) {
                    const int to = square_of(to_file, to_rank);
                    if (belongs_to(squares_[to], side_)) break;
                    moves.push_back({from, to});
                    if (squares_[to] != kEmpty) break;
                    to_file += df;
                    to_rank += dr;
                }
            }
            continue;
        }

        if (type == kKing) {
            for (int df = -1; df <= 1; ++df) {
                for (int dr = -1; dr <= 1; ++dr) {
                    if (df == 0 && dr == 0) continue;
                    const int to_file = file + df;
                    const int to_rank = rank + dr;
                    if (!on_board(to_file, to_rank)) continue;
                    const int to = square_of(to_file, to_rank);
                    if (!belongs_to(squares_[to], side_)) moves.push_back({from, to});
                }
            }

            if (side_ == Color::White && from == 4) {
                if ((castling_ & kWhiteKingside) && squares_[5] == kEmpty && squares_[6] == kEmpty &&
                    squares_[7] == kRook && !square_attacked(4, Color::Black) &&
                    !square_attacked(5, Color::Black) && !square_attacked(6, Color::Black)) {
                    moves.push_back({4, 6, '\0', false, true});
                }
                if ((castling_ & kWhiteQueenside) && squares_[1] == kEmpty && squares_[2] == kEmpty &&
                    squares_[3] == kEmpty && squares_[0] == kRook &&
                    !square_attacked(4, Color::Black) && !square_attacked(3, Color::Black) &&
                    !square_attacked(2, Color::Black)) {
                    moves.push_back({4, 2, '\0', false, true});
                }
            } else if (side_ == Color::Black && from == 60) {
                if ((castling_ & kBlackKingside) && squares_[61] == kEmpty && squares_[62] == kEmpty &&
                    squares_[63] == -kRook && !square_attacked(60, Color::White) &&
                    !square_attacked(61, Color::White) && !square_attacked(62, Color::White)) {
                    moves.push_back({60, 62, '\0', false, true});
                }
                if ((castling_ & kBlackQueenside) && squares_[57] == kEmpty && squares_[58] == kEmpty &&
                    squares_[59] == kEmpty && squares_[56] == -kRook &&
                    !square_attacked(60, Color::White) && !square_attacked(59, Color::White) &&
                    !square_attacked(58, Color::White)) {
                    moves.push_back({60, 58, '\0', false, true});
                }
            }
        }
    }
    return moves;
}

std::vector<Move> Board::legal_moves() const {
    std::vector<Move> legal;
    for (const auto& move : pseudo_legal_moves()) {
        Board next = *this;
        next.make_move(move);
        if (!next.in_check(side_)) legal.push_back(move);
    }
    return legal;
}

bool Board::is_capture(const Move& move) const {
    return move.en_passant || (move.to >= 0 && squares_[move.to] != kEmpty);
}

void Board::make_move(const Move& move) {
    if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64) {
        throw std::invalid_argument("move square outside board");
    }
    const Color moving_color = side_;
    int piece = squares_[move.from];
    const int captured = squares_[move.to];
    if (!belongs_to(piece, moving_color)) {
        throw std::invalid_argument("move does not belong to side to move");
    }

    if (std::abs(piece) == kKing) {
        castling_ &= moving_color == Color::White ? ~(kWhiteKingside | kWhiteQueenside)
                                                  : ~(kBlackKingside | kBlackQueenside);
    }
    if (move.from == 0 || move.to == 0) castling_ &= ~kWhiteQueenside;
    if (move.from == 7 || move.to == 7) castling_ &= ~kWhiteKingside;
    if (move.from == 56 || move.to == 56) castling_ &= ~kBlackQueenside;
    if (move.from == 63 || move.to == 63) castling_ &= ~kBlackKingside;

    squares_[move.from] = kEmpty;
    if (move.en_passant) {
        const int captured_square = move.to - color_sign(moving_color) * 8;
        squares_[captured_square] = kEmpty;
    }
    if (move.castle) {
        if (move.to == 6) {
            squares_[5] = squares_[7];
            squares_[7] = kEmpty;
        } else if (move.to == 2) {
            squares_[3] = squares_[0];
            squares_[0] = kEmpty;
        } else if (move.to == 62) {
            squares_[61] = squares_[63];
            squares_[63] = kEmpty;
        } else if (move.to == 58) {
            squares_[59] = squares_[56];
            squares_[56] = kEmpty;
        }
    }
    if (move.promotion != '\0') {
        piece = color_sign(moving_color) * promotion_piece(move.promotion);
    }
    squares_[move.to] = piece;

    ep_square_ = -1;
    if (std::abs(piece) == kPawn && std::abs(move.to - move.from) == 16) {
        ep_square_ = (move.to + move.from) / 2;
    }
    halfmove_clock_ = (std::abs(piece) == kPawn || captured != kEmpty || move.en_passant)
                          ? 0
                          : halfmove_clock_ + 1;
    if (moving_color == Color::Black) ++fullmove_number_;
    side_ = opposite(side_);
}

std::string Board::move_to_uci(const Move& move) {
    std::string text = square_to_text(move.from) + square_to_text(move.to);
    if (move.promotion != '\0') text += move.promotion;
    return text;
}

std::optional<Move> Board::parse_uci_move(const std::string& text) const {
    for (const auto& move : legal_moves()) {
        if (move_to_uci(move) == text) return move;
    }
    return std::nullopt;
}

std::uint64_t Board::perft(int depth) const {
    if (depth < 0) throw std::invalid_argument("perft depth cannot be negative");
    if (depth == 0) return 1;
    std::uint64_t nodes = 0;
    for (const auto& move : legal_moves()) {
        Board next = *this;
        next.make_move(move);
        nodes += next.perft(depth - 1);
    }
    return nodes;
}

int Board::evaluate() const {
    int white_score = 0;
    for (int piece : squares_) {
        if (piece == kEmpty) continue;
        const int value = material_value(std::abs(piece));
        white_score += piece > 0 ? value : -value;
    }
    return side_ == Color::White ? white_score : -white_score;
}

SearchResult search(const Board& board, int depth) {
    SearchResult result;
    const auto moves = board.legal_moves();
    if (moves.empty()) return result;

    int best_score = -kInfinity;
    for (const auto& move : moves) {
        Board next = board;
        next.make_move(move);
        const int score = -negamax(next, std::max(0, depth - 1), 1, -kInfinity, kInfinity,
                                   result.nodes);
        if (score > best_score) {
            best_score = score;
            result.best_move = move;
        }
    }
    result.score = best_score;
    return result;
}

}  // namespace marvelous
