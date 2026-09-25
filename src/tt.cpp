#include "tt.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

TranspositionTable TT;

TranspositionTable::~TranspositionTable() { aligned_free(table); }

void TranspositionTable::resize(size_t mb, int threads) {
    aligned_free(table);
    clusterCount = mb * 1024 * 1024 / sizeof(TTCluster);
    table = static_cast<TTCluster*>(aligned_malloc(64, clusterCount * sizeof(TTCluster)));
    if (!table) {
        std::fprintf(stderr, "failed to allocate %zu MB for hash\n", mb);
        std::exit(1);
    }
    clear(threads);
}

void TranspositionTable::clear(int threads) {
    generation8 = 0;
    const size_t total = clusterCount * sizeof(TTCluster);
    threads = std::max(1, threads);
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back([=, this]() {
            size_t chunk = total / threads;
            size_t start = chunk * t;
            size_t len = t == threads - 1 ? total - start : chunk;
            std::memset(reinterpret_cast<char*>(table) + start, 0, len);
        });
    for (auto& th : pool) th.join();
}

bool TranspositionTable::probe(Key key, TTData& data, TTEntry*& entry) const {
    TTCluster* cl = cluster_of(key);
    const u16 key16 = u16(key);

    for (int i = 0; i < CLUSTER_SIZE; ++i) {
        TTEntry& e = cl->entry[i];
        if (e.key16 == key16 && e.occupied()) {
            e.genBound8 = u8(generation8 | (e.genBound8 & (GENERATION_DELTA - 1)));
            entry = &e;
            data.move = Move(e.move16);
            data.score = e.score16;
            data.eval = e.eval16;
            data.depth = e.depth8 + DEPTH_ENTRY_OFFSET;
            data.bound = Bound(e.genBound8 & 3);
            data.pv = e.genBound8 & 4;
            return true;
        }
    }

    TTEntry* replace = &cl->entry[0];
    for (int i = 1; i < CLUSTER_SIZE; ++i) {
        TTEntry& e = cl->entry[i];
        if (replace->depth8 - relative_age(replace->genBound8) > e.depth8 - relative_age(e.genBound8))
            replace = &e;
    }
    entry = replace;
    data = TTData{Move::none(), VALUE_NONE, VALUE_NONE, DEPTH_ENTRY_OFFSET, BOUND_NONE, false};
    return false;
}

void TranspositionTable::store(TTEntry* e, Key key, Value score, bool pv, Bound bound, int depth, Move move, Value eval) {
    const u16 key16 = u16(key);
    if (move || key16 != e->key16) e->move16 = move.raw();

    if (bound == BOUND_EXACT || key16 != e->key16 || depth - DEPTH_ENTRY_OFFSET + 2 * pv > e->depth8 - 4
        || relative_age(e->genBound8)) {
        e->key16 = key16;
        e->depth8 = u8(depth - DEPTH_ENTRY_OFFSET);
        e->genBound8 = u8(generation8 | (u8(pv) << 2) | bound);
        e->score16 = i16(score);
        e->eval16 = i16(eval);
    }
}

int TranspositionTable::hashfull() const {
    int cnt = 0;
    for (size_t i = 0; i < 1000 && i < clusterCount; ++i)
        for (int j = 0; j < CLUSTER_SIZE; ++j) {
            const TTEntry& e = table[i].entry[j];
            cnt += e.occupied() && (e.genBound8 & GENERATION_MASK) == generation8;
        }
    return cnt / CLUSTER_SIZE;
}
