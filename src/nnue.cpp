#include "nnue.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "eval.h"

#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(EVALFILE)
#if defined(__APPLE__)
asm(".section __DATA,__const\n"
    ".globl _marvelousNetData\n"
    ".p2align 6\n"
    "_marvelousNetData:\n"
    ".incbin \"" EVALFILE "\"\n"
    ".globl _marvelousNetEnd\n"
    "_marvelousNetEnd:\n"
    ".byte 0\n"
    ".text\n");
#elif defined(_WIN32)
asm(".section .rdata,\"dr\"\n"
    ".globl marvelousNetData\n"
    ".p2align 6\n"
    "marvelousNetData:\n"
    ".incbin \"" EVALFILE "\"\n"
    ".globl marvelousNetEnd\n"
    "marvelousNetEnd:\n"
    ".byte 0\n"
    ".text\n");
#else
asm(".section .rodata\n"
    ".globl marvelousNetData\n"
    ".p2align 6\n"
    "marvelousNetData:\n"
    ".incbin \"" EVALFILE "\"\n"
    ".globl marvelousNetEnd\n"
    "marvelousNetEnd:\n"
    ".byte 0\n"
    ".previous\n");
#endif
extern "C" const unsigned char marvelousNetData[];
extern "C" const unsigned char marvelousNetEnd[];
#endif

namespace NNUE {

namespace {

const Network* net = nullptr;
Network* loaded = nullptr;
std::string netSource = "none";

inline int bucket_key(Color persp, Square ksq) {
    const Square rel = relative_square(persp, ksq);
    const bool flip = file_of(rel) > 3;
    const int f = flip ? 7 - file_of(rel) : file_of(rel);
    return BucketLayout[rank_of(rel) * 4 + f] * 2 + flip;
}

inline const i16* row(Color persp, int bkey, Piece pc, Square s) {
    const int flip = (bkey & 1) ? 7 : 0;
    const int idx = (bkey >> 1) * INPUTS + (color_of(pc) == persp ? 0 : 384) + type_of(pc) * 64
                  + (relative_square(persp, s) ^ flip);
    return net->ftWeights + size_t(idx) * L1;
}

inline void add_sub(i16* __restrict out, const i16* __restrict in, const i16* __restrict a, const i16* __restrict s) {
    for (int i = 0; i < L1; ++i) out[i] = in[i] + a[i] - s[i];
}

inline void add_sub2(i16* __restrict out, const i16* __restrict in, const i16* __restrict a, const i16* __restrict s1,
                     const i16* __restrict s2) {
    for (int i = 0; i < L1; ++i) out[i] = in[i] + a[i] - s1[i] - s2[i];
}

inline void add2_sub2(i16* __restrict out, const i16* __restrict in, const i16* __restrict a1, const i16* __restrict a2,
                      const i16* __restrict s1, const i16* __restrict s2) {
    for (int i = 0; i < L1; ++i) out[i] = in[i] + a1[i] + a2[i] - s1[i] - s2[i];
}

inline void add_in(i16* __restrict v, const i16* __restrict a) {
    for (int i = 0; i < L1; ++i) v[i] += a[i];
}

inline void sub_in(i16* __restrict v, const i16* __restrict a) {
    for (int i = 0; i < L1; ++i) v[i] -= a[i];
}

inline i32 screlu_dot(const i16* __restrict acc, const i16* __restrict w) {
#if defined(__ARM_NEON)
    const int16x8_t zero = vdupq_n_s16(0);
    const int16x8_t qa = vdupq_n_s16(QA);
    int32x4_t s[8];
    for (auto& x : s) x = vdupq_n_s32(0);
    for (int i = 0; i < L1; i += 32) {
        for (int k = 0; k < 4; ++k) {
            const int16x8_t a = vminq_s16(vmaxq_s16(vld1q_s16(acc + i + 8 * k), zero), qa);
            const int16x8_t p = vmulq_s16(a, vld1q_s16(w + i + 8 * k));
            s[2 * k] = vmlal_s16(s[2 * k], vget_low_s16(p), vget_low_s16(a));
            s[2 * k + 1] = vmlal_high_s16(s[2 * k + 1], p, a);
        }
    }
    const int32x4_t t = vaddq_s32(vaddq_s32(vaddq_s32(s[0], s[1]), vaddq_s32(s[2], s[3])),
                                  vaddq_s32(vaddq_s32(s[4], s[5]), vaddq_s32(s[6], s[7])));
    return vaddvq_s32(t);
#elif defined(__AVX2__)
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16(QA);
    __m256i s0 = _mm256_setzero_si256(), s1 = s0;
    for (int i = 0; i < L1; i += 32) {
        const __m256i a = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(acc + i)), zero), qa);
        const __m256i b = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(acc + i + 16)), zero), qa);
        const __m256i pa = _mm256_mullo_epi16(a, _mm256_load_si256((const __m256i*)(w + i)));
        const __m256i pb = _mm256_mullo_epi16(b, _mm256_load_si256((const __m256i*)(w + i + 16)));
        s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(pa, a));
        s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(pb, b));
    }
    const __m256i s = _mm256_add_epi32(s0, s1);
    __m128i x = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
    x = _mm_add_epi32(x, _mm_shuffle_epi32(x, 0x4E));
    x = _mm_add_epi32(x, _mm_shuffle_epi32(x, 0xB1));
    return _mm_cvtsi128_si32(x);
#else
    i32 sum = 0;
    for (int i = 0; i < L1; ++i) {
        const i16 v = std::clamp<i16>(acc[i], 0, QA);
        sum += i32(i16(v * w[i])) * v;
    }
    return sum;
#endif
}

bool valid_size(size_t n) { return n >= sizeof(Network); }

} // namespace

bool init() {
#if defined(EVALFILE)
    const size_t n = size_t(marvelousNetEnd - marvelousNetData);
    if (valid_size(n)) {
        net = reinterpret_cast<const Network*>(marvelousNetData);
        netSource = "embedded";
        return true;
    }
#endif
    return false;
}

bool load(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const size_t n = size_t(in.tellg());
    if (!valid_size(n)) return false;
    Network* buf = static_cast<Network*>(aligned_malloc(64, sizeof(Network)));
    if (!buf) return false;
    in.seekg(0);
    in.read(reinterpret_cast<char*>(buf), sizeof(Network));
    if (!in) {
        aligned_free(buf);
        return false;
    }
    aligned_free(loaded);
    loaded = buf;
    net = loaded;
    netSource = path;
    return true;
}

std::string source() { return netSource; }

bool active() { return net != nullptr; }

Evaluator::Evaluator() {
    stack = static_cast<Accumulator*>(aligned_malloc(64, sizeof(Accumulator) * (MAX_PLY + 16)));
    finny = static_cast<FinnyEntry(*)[INPUT_BUCKETS * 2]>(aligned_malloc(64, sizeof(FinnyEntry) * 2 * INPUT_BUCKETS * 2));
    for (int p = 0; p < 2; ++p)
        for (int b = 0; b < INPUT_BUCKETS * 2; ++b) {
            FinnyEntry& e = finny[p][b];
            if (net) std::memcpy(e.v, net->ftBias, sizeof(e.v));
            std::memset(e.bb, 0, sizeof(e.bb));
        }
    finnyNet = net;
}

Evaluator::~Evaluator() {
    aligned_free(stack);
    aligned_free(finny);
}

void Evaluator::refresh(const Position& pos, Color persp, Accumulator& acc) {
    const int bkey = acc.bucket[persp];
    FinnyEntry& e = finny[persp][bkey];
    for (Color c : {WHITE, BLACK})
        for (int pt = PAWN; pt <= KING; ++pt) {
            const Bitboard now = pos.pieces(c, PieceType(pt));
            const Bitboard old = e.bb[c][pt];
            const Piece pc = make_piece(c, PieceType(pt));
            for (Bitboard b = now & ~old; b;) add_in(e.v, row(persp, bkey, pc, pop_lsb(b)));
            for (Bitboard b = old & ~now; b;) sub_in(e.v, row(persp, bkey, pc, pop_lsb(b)));
            e.bb[c][pt] = now;
        }
    std::memcpy(acc.v[persp], e.v, sizeof(e.v));
    acc.computed[persp] = true;
}

void Evaluator::reset(const Position& pos) {
    if (!net) return;
    if (finnyNet != net) {
        for (int p = 0; p < 2; ++p)
            for (int b = 0; b < INPUT_BUCKETS * 2; ++b) {
                std::memcpy(finny[p][b].v, net->ftBias, sizeof(finny[p][b].v));
                std::memset(finny[p][b].bb, 0, sizeof(finny[p][b].bb));
            }
        finnyNet = net;
    }
    top = 0;
    Accumulator& acc = stack[0];
    for (Color c : {WHITE, BLACK}) {
        acc.bucket[c] = bucket_key(c, pos.king_sq(c));
        refresh(pos, c, acc);
    }
}

void Evaluator::push(const Position& pos) {
    Accumulator& acc = stack[++top];
    acc.computed[WHITE] = acc.computed[BLACK] = false;
    acc.dirty = pos.state().dirty;
    acc.bucket[WHITE] = bucket_key(WHITE, pos.king_sq(WHITE));
    acc.bucket[BLACK] = bucket_key(BLACK, pos.king_sq(BLACK));
}

void Evaluator::push_null() {
    Accumulator& acc = stack[++top];
    acc.computed[WHITE] = acc.computed[BLACK] = false;
    acc.dirty.adds = acc.dirty.subs = 0;
    acc.bucket[WHITE] = stack[top - 1].bucket[WHITE];
    acc.bucket[BLACK] = stack[top - 1].bucket[BLACK];
}

void Evaluator::ensure(const Position& pos, Color persp) {
    if (stack[top].computed[persp]) return;

    int j = top;
    while (!stack[j].computed[persp]) {
        if (stack[j].bucket[persp] != stack[j - 1].bucket[persp]) {
            refresh(pos, persp, stack[top]);
            return;
        }
        --j;
    }

    for (int k = j + 1; k <= top; ++k) {
        const Accumulator& prev = stack[k - 1];
        Accumulator& cur = stack[k];
        const DirtyPieces& d = cur.dirty;
        const int b = cur.bucket[persp];
        i16* out = cur.v[persp];
        const i16* in = prev.v[persp];

        if (d.adds == 1 && d.subs == 1)
            add_sub(out, in, row(persp, b, d.addPc[0], d.addSq[0]), row(persp, b, d.subPc[0], d.subSq[0]));
        else if (d.adds == 1 && d.subs == 2)
            add_sub2(out, in, row(persp, b, d.addPc[0], d.addSq[0]), row(persp, b, d.subPc[0], d.subSq[0]),
                     row(persp, b, d.subPc[1], d.subSq[1]));
        else if (d.adds == 2 && d.subs == 2)
            add2_sub2(out, in, row(persp, b, d.addPc[0], d.addSq[0]), row(persp, b, d.addPc[1], d.addSq[1]),
                      row(persp, b, d.subPc[0], d.subSq[0]), row(persp, b, d.subPc[1], d.subSq[1]));
        else {
            std::memcpy(out, in, sizeof(cur.v[persp]));
            for (int i = 0; i < d.adds; ++i) add_in(out, row(persp, b, d.addPc[i], d.addSq[i]));
            for (int i = 0; i < d.subs; ++i) sub_in(out, row(persp, b, d.subPc[i], d.subSq[i]));
        }
        cur.computed[persp] = true;
    }
}

Value Evaluator::evaluate(const Position& pos) {
    if (!net) return Eval::evaluate_fresh(pos);
    ensure(pos, WHITE);
    ensure(pos, BLACK);
    const Accumulator& acc = stack[top];
    const Color stm = pos.side_to_move();
    const int bucket = (pos.piece_count() - 2) / 4;
    const i16* w = net->outWeights[bucket];
    const i32 sum = screlu_dot(acc.v[stm], w) + screlu_dot(acc.v[~stm], w + L1);
    return Value((i64(sum) / QA + net->outBias[bucket]) * SCALE / (QA * QB));
}

Value evaluate_fresh(const Position& pos) {
    if (!net) return Eval::evaluate_fresh(pos);
    auto ev = std::make_unique<Evaluator>();
    ev->reset(pos);
    return ev->evaluate(pos);
}

} // namespace NNUE
