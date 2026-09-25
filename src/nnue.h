#pragma once

#include <string>

#include "position.h"

namespace NNUE {

constexpr int INPUTS = 768;
constexpr int L1 = 1024;
constexpr int OUTPUT_BUCKETS = 8;
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 400;

// king buckets over the half board (files a-d), rank 1 first
constexpr int BucketLayout[32] = {
    0,  1,  2,  3,
    4,  5,  6,  7,
    8,  9,  10, 11,
    8,  9,  10, 11,
    12, 12, 13, 13,
    12, 12, 13, 13,
    14, 14, 15, 15,
    14, 14, 15, 15,
};
constexpr int INPUT_BUCKETS = 16;

struct alignas(64) Network {
    i16 ftWeights[INPUT_BUCKETS * INPUTS * L1];
    i16 ftBias[L1];
    i16 outWeights[OUTPUT_BUCKETS][2 * L1];
    i16 outBias[OUTPUT_BUCKETS];
};

bool init();
bool load(const std::string& path);
std::string source();

struct alignas(64) Accumulator {
    i16 v[COLOR_NB][L1];
    bool computed[COLOR_NB];
    int bucket[COLOR_NB];
    DirtyPieces dirty;
};

struct alignas(64) FinnyEntry {
    i16 v[L1];
    Bitboard bb[COLOR_NB][PIECE_TYPE_NB];
};

class Evaluator {
public:
    Evaluator();
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    void reset(const Position& pos);
    void push(const Position& pos);
    void push_null();
    void pop() { --top; }
    Value evaluate(const Position& pos);

private:
    void ensure(const Position& pos, Color persp);
    void refresh(const Position& pos, Color persp, Accumulator& acc);

    Accumulator* stack;
    FinnyEntry (*finny)[INPUT_BUCKETS * 2];
    const Network* finnyNet = nullptr;
    int top = 0;
};

Value evaluate_fresh(const Position& pos);

} // namespace NNUE
