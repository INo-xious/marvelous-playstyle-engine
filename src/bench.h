#pragma once

#include "position.h"

namespace Bench {
void run(int depth);
void perft_divide(Position& pos, int depth);
u64 perft(Position& pos, int depth);
} // namespace Bench
