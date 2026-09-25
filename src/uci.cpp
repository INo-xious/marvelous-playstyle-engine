#include "uci.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>

#include "bench.h"
#include "eval.h"
#include "movegen.h"
#include "search.h"
#include "tt.h"

namespace {

constexpr const char* EngineName = "Marvelous";
constexpr const char* EngineAuthor = "Marvel Harisson";

struct Options {
    int hash = 64;
    int threads = 1;
    int moveOverhead = 30;
    int multiPV = 1;
} options;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool parse_int(const std::string& s, long long& out) {
    char* end = nullptr;
    long long v = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str()) return false;
    out = v;
    return true;
}

void print_options() {
    std::printf("option name Hash type spin default 64 min 1 max 65536\n");
    std::printf("option name Threads type spin default 1 min 1 max 1024\n");
    std::printf("option name Move Overhead type spin default 30 min 0 max 5000\n");
    std::printf("option name MultiPV type spin default 1 min 1 max 218\n");
    std::printf("option name Ponder type check default false\n");
    std::printf("option name UCI_ShowWDL type check default false\n");
    std::printf("option name Clear Hash type button\n");
}

void set_option(std::istringstream& is) {
    std::string tok, name, value;
    is >> tok;
    while (is >> tok && tok != "value") name += (name.empty() ? "" : " ") + tok;
    while (is >> tok) value += (value.empty() ? "" : " ") + tok;
    const std::string n = lower(name);
    long long v = 0;

    if (n == "hash" && parse_int(value, v)) {
        options.hash = int(std::clamp(v, 1LL, 65536LL));
        TT.resize(options.hash, options.threads);
    } else if (n == "threads" && parse_int(value, v)) {
        options.threads = int(std::clamp(v, 1LL, 1024LL));
        Search::set_threads(options.threads);
    } else if (n == "move overhead" && parse_int(value, v)) {
        options.moveOverhead = int(std::clamp(v, 0LL, 5000LL));
    } else if (n == "multipv" && parse_int(value, v)) {
        options.multiPV = int(std::clamp(v, 1LL, 218LL));
    } else if (n == "uci_showwdl") {
        Search::showWDL = lower(value) == "true";
    } else if (n == "clear hash") {
        TT.clear(options.threads);
    }
}

void set_position(Position& pos, std::istringstream& is) {
    std::string tok, fen;
    is >> tok;
    if (tok == "startpos") {
        fen = Position::StartFEN;
        is >> tok;
    } else if (tok == "fen") {
        while (is >> tok && tok != "moves") fen += tok + " ";
    } else
        return;

    if (!pos.set(fen)) {
        std::printf("info string invalid fen\n");
        pos.set(Position::StartFEN);
        return;
    }
    while (is >> tok) {
        Move m = uci_to_move(pos, tok);
        if (!m) break;
        pos.make(m);
    }
}

void go(Position& pos, std::istringstream& is) {
    Search::Limits limits;
    limits.start = Search::now();
    limits.moveOverhead = options.moveOverhead;
    limits.multiPV = options.multiPV;
    std::string tok;
    long long v;
    while (is >> tok) {
        if (tok == "searchmoves") {
            while (is >> tok) {
                Move m = uci_to_move(pos, tok);
                if (m) limits.searchmoves.push_back(m);
            }
        } else if (tok == "infinite") limits.infinite = true;
        else if (tok == "ponder") limits.ponder = true;
        else if (tok == "perft") {
            if (is >> tok && parse_int(tok, v)) Bench::perft_divide(pos, int(v));
            return;
        } else {
            std::string val;
            if (!(is >> val) || !parse_int(val, v)) continue;
            if (tok == "wtime") limits.time[WHITE] = v;
            else if (tok == "btime") limits.time[BLACK] = v;
            else if (tok == "winc") limits.inc[WHITE] = v;
            else if (tok == "binc") limits.inc[BLACK] = v;
            else if (tok == "movestogo") limits.movestogo = int(v);
            else if (tok == "depth") limits.depth = int(v);
            else if (tok == "nodes") limits.nodes = u64(std::max(0LL, v));
            else if (tok == "movetime") limits.movetime = v;
            else if (tok == "mate") limits.mate = int(v);
        }
    }
    Search::start(pos, limits);
}

} // namespace

void UCI::loop(int argc, char** argv) {
    Position pos;
    TT.resize(options.hash, options.threads);
    Search::set_threads(options.threads);

    if (argc > 1) {
        std::string cmd = argv[1];
        if (cmd == "bench") {
            int depth = argc > 2 ? std::atoi(argv[2]) : 0;
            Bench::run(depth);
            Search::shutdown();
            return;
        }
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string tok;
        is >> tok;

        if (tok == "uci") {
            std::printf("id name %s\nid author %s\n", EngineName, EngineAuthor);
            print_options();
            std::printf("uciok\n");
        } else if (tok == "isready") {
            std::printf("readyok\n");
        } else if (tok == "setoption") {
            Search::wait();
            set_option(is);
        } else if (tok == "ucinewgame") {
            Search::wait();
            TT.clear(options.threads);
            Search::new_game();
        } else if (tok == "position") {
            Search::wait();
            set_position(pos, is);
        } else if (tok == "go") {
            Search::wait();
            go(pos, is);
        } else if (tok == "stop") {
            Search::stop();
        } else if (tok == "ponderhit") {
            Search::ponderhit();
        } else if (tok == "quit") {
            Search::stop();
            break;
        } else if (tok == "bench") {
            Search::wait();
            std::string d;
            is >> d;
            Bench::run(d.empty() ? 0 : std::atoi(d.c_str()));
        } else if (tok == "d") {
            std::printf("%s\n", pos.fen().c_str());
        } else if (tok == "eval") {
            std::printf("eval %d (side to move)\n", Eval::evaluate_fresh(pos));
        }
        std::fflush(stdout);
    }
    Search::finish();
}
