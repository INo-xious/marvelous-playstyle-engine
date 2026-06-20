#include "marvelous/chess.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void expect_equal(std::uint64_t actual, std::uint64_t expected, const char* label) {
    if (actual != expected) {
        std::cerr << label << ": expected " << expected << ", got " << actual << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    marvelous::Board start;
    expect_equal(start.perft(1), 20, "start depth 1");
    expect_equal(start.perft(2), 400, "start depth 2");
    expect_equal(start.perft(3), 8902, "start depth 3");
    expect_equal(start.perft(4), 197281, "start depth 4");

    marvelous::Board castling_benchmark(
        "r3k2r/p1ppqpb1/bn2pnp1/2pP4/1p2P3/2N2N2/PPQBBPPP/R3K2R w KQkq - 0 1");
    expect_equal(castling_benchmark.perft(1), 45, "castling benchmark depth 1");
    expect_equal(castling_benchmark.perft(2), 1947, "castling benchmark depth 2");
    expect_equal(castling_benchmark.perft(3), 85877, "castling benchmark depth 3");

    std::cout << "all engine tests passed\n";
    return 0;
}
