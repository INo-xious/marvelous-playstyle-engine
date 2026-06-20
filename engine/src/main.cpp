#include "marvelous/chess.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void set_position(marvelous::Board& board, const std::string& command) {
    std::istringstream input(command);
    std::string token;
    input >> token;  // position
    input >> token;

    if (token == "startpos") {
        board.load_fen(marvelous::Board::kStartFen);
    } else if (token == "fen") {
        std::vector<std::string> fields;
        for (int i = 0; i < 6 && input >> token; ++i) fields.push_back(token);
        if (fields.size() != 6) throw std::invalid_argument("position fen requires six fields");
        std::string fen;
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (i > 0) fen += ' ';
            fen += fields[i];
        }
        board.load_fen(fen);
    } else {
        throw std::invalid_argument("expected startpos or fen");
    }

    if (input >> token && token == "moves") {
        while (input >> token) {
            const auto move = board.parse_uci_move(token);
            if (!move) throw std::invalid_argument("illegal move in position command: " + token);
            board.make_move(*move);
        }
    }
}

int requested_depth(const std::string& command) {
    std::istringstream input(command);
    std::string token;
    int depth = 4;
    while (input >> token) {
        if (token == "depth" && input >> depth) break;
    }
    return std::clamp(depth, 1, 8);
}

using StyleBook = std::unordered_map<std::string, std::string>;

std::string position_key(const marvelous::Board& board) {
    std::istringstream input(board.fen());
    std::string field;
    std::string key;
    for (int index = 0; index < 4 && input >> field; ++index) {
        if (!key.empty()) key += ' ';
        key += field;
    }
    return key;
}

bool load_style_book(const std::string& path, StyleBook& style_book) {
    std::ifstream input(path);
    if (!input) return false;
    style_book.clear();
    std::string line;
    std::getline(input, line);  // header
    while (std::getline(input, line)) {
        std::istringstream row(line);
        std::string key;
        std::string move;
        if (std::getline(row, key, '\t') && std::getline(row, move, '\t')) {
            style_book[key] = move;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string(argv[1]) == "--perft") {
            marvelous::Board board;
            const int depth = std::stoi(argv[2]);
            if (argc >= 4) board.load_fen(argv[3]);
            std::cout << board.perft(depth) << '\n';
            return 0;
        }

        marvelous::Board board;
        StyleBook style_book;
        load_style_book("data/processed/style_book.tsv", style_book);
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line == "uci") {
                std::cout << "id name MarveIous Initial\n";
                std::cout << "id author Marvel Harisson\n";
                std::cout << "option name StyleBookPath type string default data/processed/style_book.tsv\n";
                std::cout << "uciok\n";
            } else if (line == "isready") {
                std::cout << "readyok\n";
            } else if (line == "ucinewgame") {
                board.load_fen(marvelous::Board::kStartFen);
            } else if (line.rfind("position ", 0) == 0) {
                set_position(board, line);
            } else if (line.rfind("setoption name StyleBookPath value ", 0) == 0) {
                const std::string path = line.substr(35);
                const bool loaded = load_style_book(path, style_book);
                std::cout << "info string style book " << (loaded ? "loaded" : "not found") << '\n';
            } else if (line.rfind("go", 0) == 0) {
                const auto style = style_book.find(position_key(board));
                if (style != style_book.end()) {
                    const auto move = board.parse_uci_move(style->second);
                    if (move) {
                        std::cout << "info string personal style-book match\n";
                        std::cout << "bestmove " << style->second << '\n';
                        continue;
                    }
                }
                const int depth = requested_depth(line);
                const auto result = marvelous::search(board, depth);
                if (result.best_move.from < 0) {
                    std::cout << "bestmove 0000\n";
                } else {
                    std::cout << "info depth " << depth << " score cp " << result.score
                              << " nodes " << result.nodes << '\n';
                    std::cout << "bestmove " << marvelous::Board::move_to_uci(result.best_move)
                              << '\n';
                }
            } else if (line.rfind("perft ", 0) == 0) {
                const int depth = std::stoi(line.substr(6));
                std::cout << "nodes " << board.perft(depth) << '\n';
            } else if (line == "moves") {
                for (const auto& move : board.legal_moves()) {
                    std::cout << marvelous::Board::move_to_uci(move) << ' ';
                }
                std::cout << '\n';
            } else if (line == "d") {
                std::cout << board.fen() << '\n';
            } else if (line == "quit") {
                break;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
