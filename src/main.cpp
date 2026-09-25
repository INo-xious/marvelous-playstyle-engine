#include <cstdio>

#include "bitboard.h"
#include "nnue.h"
#include "position.h"
#include "search.h"
#include "uci.h"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    BB::init();
    Zobrist::init();
    NNUE::init();
    Search::init();
    UCI::loop(argc, argv);
    return 0;
}
