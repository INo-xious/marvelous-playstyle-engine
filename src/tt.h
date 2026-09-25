#pragma once

#include <cstddef>

#include "types.h"

constexpr int DEPTH_QS = 0;
constexpr int DEPTH_UNSEARCHED = -2;
constexpr int DEPTH_ENTRY_OFFSET = -3;

struct TTData {
    Move move;
    Value score;
    Value eval;
    int depth;
    Bound bound;
    bool pv;
};

struct TTEntry {
    u16 key16;
    u16 move16;
    i16 score16;
    i16 eval16;
    u8 depth8;
    u8 genBound8;

    bool occupied() const { return depth8 != 0; }
};

static_assert(sizeof(TTEntry) == 10);

constexpr int CLUSTER_SIZE = 3;

struct alignas(32) TTCluster {
    TTEntry entry[CLUSTER_SIZE];
    char pad[2];
};

static_assert(sizeof(TTCluster) == 32);

class TranspositionTable {
public:
    ~TranspositionTable();
    void resize(size_t mb, int threads);
    void clear(int threads);
    void new_search() { generation8 += GENERATION_DELTA; }

    // returns true on hit; entry points to the slot to write back into
    bool probe(Key key, TTData& data, TTEntry*& entry) const;
    void store(TTEntry* entry, Key key, Value score, bool pv, Bound bound, int depth, Move move, Value eval);
    void prefetch(Key key) const { __builtin_prefetch(cluster_of(key)); }
    int hashfull() const;

private:
    static constexpr u8 GENERATION_BITS = 3;
    static constexpr int GENERATION_DELTA = 1 << GENERATION_BITS;
    static constexpr int GENERATION_CYCLE = 255 + GENERATION_DELTA;
    static constexpr int GENERATION_MASK = (0xFF << GENERATION_BITS) & 0xFF;

    TTCluster* cluster_of(Key key) const {
        return &table[(unsigned __int128)key * (unsigned __int128)clusterCount >> 64];
    }
    int relative_age(u8 genBound) const { return (GENERATION_CYCLE + generation8 - genBound) & GENERATION_MASK; }

    TTCluster* table = nullptr;
    size_t clusterCount = 0;
    u8 generation8 = 0;
};

extern TranspositionTable TT;

inline Value value_to_tt(Value v, int ply) {
    if (v == VALUE_NONE) return VALUE_NONE;
    return is_win(v) ? v + ply : is_loss(v) ? v - ply : v;
}

inline Value value_from_tt(Value v, int ply, int rule50) {
    if (v == VALUE_NONE) return VALUE_NONE;
    if (is_win(v)) {
        // a mate/TB win may be unreachable under the 50-move rule
        if (v >= VALUE_MATE_IN_MAX_PLY && VALUE_MATE - v > 100 - rule50) return VALUE_TB_WIN_IN_MAX_PLY - 1;
        if (VALUE_TB - v > 100 - rule50) return VALUE_TB_WIN_IN_MAX_PLY - 1;
        return v - ply;
    }
    if (is_loss(v)) {
        if (v <= -VALUE_MATE_IN_MAX_PLY && VALUE_MATE + v > 100 - rule50) return -VALUE_TB_WIN_IN_MAX_PLY + 1;
        if (VALUE_TB + v > 100 - rule50) return -VALUE_TB_WIN_IN_MAX_PLY + 1;
        return v + ply;
    }
    return v;
}
