// test-simd.cpp - Correctness and benchmark tests for AVX2 SIMD optimizations.
//
// Verifies that SIMD versions of InlineBitset::or_with, BitBoard shift/expand,
// and bitboard_flood_fill produce identical results to the scalar versions.
// Also benchmarks both implementations for comparison.
//
// Usage:
//   ./test-simd --test       # Run correctness tests only
//   ./test-simd --bench      # Run benchmarks only
//   ./test-simd              # Run both

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>

#include <immintrin.h>

// ============================================================================
// InlineBitset (duplicated from inline-bitset.h with both scalar and SIMD)
// ============================================================================

struct InlineBitset {
  static constexpr int kMaxWords = 128;
  uint64_t words[kMaxWords];
  int size_bits;

  InlineBitset() : size_bits(0) { memset(words, 0, sizeof(words)); }
  explicit InlineBitset(int n) : size_bits(n) { memset(words, 0, sizeof(words)); }

  InlineBitset(const InlineBitset& rhs) : size_bits(rhs.size_bits) {
    int nwords = (size_bits + 63) >> 6;
    memcpy(words, rhs.words, nwords * 8);
  }
  InlineBitset& operator=(const InlineBitset& rhs) {
    size_bits = rhs.size_bits;
    int nwords = (size_bits + 63) >> 6;
    memcpy(words, rhs.words, nwords * 8);
    return *this;
  }

  bool operator==(const InlineBitset& rhs) const {
    if (size_bits != rhs.size_bits) return false;
    int nwords = (size_bits + 63) >> 6;
    return memcmp(words, rhs.words, nwords * 8) == 0;
  }

  void or_with_scalar(const InlineBitset& rhs) {
    const int nwords = (size_bits + 63) >> 6;
    for (int w = 0; w < nwords; w++) {
      words[w] |= rhs.words[w];
    }
  }

  void or_with_simd(const InlineBitset& rhs) {
    const int nwords = (size_bits + 63) >> 6;
    int w = 0;
    for (; w + 4 <= nwords; w += 4) {
      __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words[w]));
      __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&rhs.words[w]));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(&words[w]), _mm256_or_si256(a, b));
    }
    for (; w < nwords; w++) {
      words[w] |= rhs.words[w];
    }
  }
};

// ============================================================================
// BitBoard (duplicated from wordescape.cpp with both scalar and SIMD)
// ============================================================================

static constexpr int kBoardHeight = 13;
static constexpr int kBoardWidth = 10;
static constexpr int kGridCells = kBoardHeight * kBoardWidth;

struct BitBoard {
  uint64_t w[3] = {};

  void set(int pos) { w[pos >> 6] |= 1ULL << (pos & 63); }
  void clear(int pos) { w[pos >> 6] &= ~(1ULL << (pos & 63)); }
  bool test(int pos) const { return w[pos >> 6] & (1ULL << (pos & 63)); }

  BitBoard operator|(const BitBoard& o) const { return {{w[0]|o.w[0], w[1]|o.w[1], w[2]|o.w[2]}}; }
  BitBoard operator&(const BitBoard& o) const { return {{w[0]&o.w[0], w[1]&o.w[1], w[2]&o.w[2]}}; }
  BitBoard operator~() const { return {{~w[0], ~w[1], ~w[2]}}; }
  BitBoard& operator|=(const BitBoard& o) { w[0]|=o.w[0]; w[1]|=o.w[1]; w[2]|=o.w[2]; return *this; }
  bool any() const { return w[0] | w[1] | w[2]; }

  bool operator==(const BitBoard& o) const {
    return w[0] == o.w[0] && w[1] == o.w[1] && w[2] == o.w[2];
  }

  BitBoard shr(int n) const {
    return {{(w[0] >> n) | (w[1] << (64 - n)),
             (w[1] >> n) | (w[2] << (64 - n)),
             w[2] >> n}};
  }

  BitBoard shl(int n) const {
    return {{w[0] << n,
             (w[1] << n) | (w[0] >> (64 - n)),
             (w[2] << n) | (w[1] >> (64 - n))}};
  }

  template<typename F>
  void for_each_bit(F&& f) const {
    for (int i = 0; i < 3; i++) {
      uint64_t bits = w[i];
      while (bits) {
        int bit = __builtin_ctzll(bits);
        f(i * 64 + bit);
        bits &= bits - 1;
      }
    }
  }

  int popcount() const {
    return __builtin_popcountll(w[0]) + __builtin_popcountll(w[1]) + __builtin_popcountll(w[2]);
  }
};

// Column/row masks
static BitBoard not_col_0, not_col_9;
static BitBoard sRow0Mask, sRow12Mask;

static void init_bitboard_masks() {
  for (int y = 0; y < kBoardHeight; y++) {
    for (int x = 0; x < kBoardWidth; x++) {
      int pos = y * kBoardWidth + x;
      if (x != 0) not_col_0.set(pos);
      if (x != kBoardWidth - 1) not_col_9.set(pos);
      if (y == 0) sRow0Mask.set(pos);
      if (y == kBoardHeight - 1) sRow12Mask.set(pos);
    }
  }
}

// --- Scalar versions ---

static BitBoard expand_all_dirs_scalar(const BitBoard& b) {
  BitBoard result = {};
  result |= b.shr(1)  & not_col_0;
  result |= b.shl(1)  & not_col_9;
  result |= b.shr(10);
  result |= b.shl(10);
  result |= b.shr(11) & not_col_0;
  result |= b.shr(9)  & not_col_9;
  result |= b.shl(9)  & not_col_0;
  result |= b.shl(11) & not_col_9;
  return result;
}

static BitBoard bitboard_flood_fill_scalar(const BitBoard& seeds, const BitBoard& alive) {
  BitBoard reached = seeds & alive;
  BitBoard frontier = reached;
  while (frontier.any()) {
    BitBoard expanded = expand_all_dirs_scalar(frontier) & alive & (~reached);
    reached |= expanded;
    frontier = expanded;
  }
  return reached;
}

// --- SIMD versions ---

static inline __m256i bb_load(const BitBoard& b) {
  __m128i lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&b.w[0]));
  __m128i hi = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&b.w[2]));
  return _mm256_inserti128_si256(_mm256_castsi128_si256(lo), hi, 1);
}

static inline void bb_store(BitBoard& b, __m256i v) {
  __m128i lo = _mm256_castsi256_si128(v);
  __m128i hi = _mm256_extracti128_si256(v, 1);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(&b.w[0]), lo);
  b.w[2] = _mm_extract_epi64(hi, 0);
}

static inline __m256i bb_shr(__m256i v, int n) {
  __m256i shifted = _mm256_srli_epi64(v, n);
  __m256i carry_src = _mm256_permute4x64_epi64(v, 0x39);
  __m256i carry = _mm256_slli_epi64(carry_src, 64 - n);
  return _mm256_or_si256(shifted, carry);
}

static inline __m256i bb_shl(__m256i v, int n) {
  __m256i shifted = _mm256_slli_epi64(v, n);
  __m256i carry_src = _mm256_permute4x64_epi64(v, 0x93);
  __m256i carry = _mm256_srli_epi64(carry_src, 64 - n);
  return _mm256_or_si256(shifted, carry);
}

static BitBoard expand_all_dirs_simd(const BitBoard& b) {
  __m256i v = bb_load(b);
  __m256i nc0 = bb_load(not_col_0);
  __m256i nc9 = bb_load(not_col_9);

  __m256i result = _mm256_setzero_si256();
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shr(v, 1), nc0));
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shl(v, 1), nc9));
  result = _mm256_or_si256(result, bb_shr(v, 10));
  result = _mm256_or_si256(result, bb_shl(v, 10));
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shr(v, 11), nc0));
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shr(v, 9), nc9));
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shl(v, 9), nc0));
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shl(v, 11), nc9));

  BitBoard out;
  bb_store(out, result);
  return out;
}

static BitBoard bitboard_flood_fill_simd(const BitBoard& seeds, const BitBoard& alive) {
  __m256i alive_v = bb_load(alive);
  __m256i nc0 = bb_load(not_col_0);
  __m256i nc9 = bb_load(not_col_9);
  __m256i reached_v = _mm256_and_si256(bb_load(seeds), alive_v);
  __m256i frontier_v = reached_v;

  while (!_mm256_testz_si256(frontier_v, frontier_v)) {
    __m256i expanded = _mm256_setzero_si256();
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shr(frontier_v, 1), nc0));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shl(frontier_v, 1), nc9));
    expanded = _mm256_or_si256(expanded, bb_shr(frontier_v, 10));
    expanded = _mm256_or_si256(expanded, bb_shl(frontier_v, 10));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shr(frontier_v, 11), nc0));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shr(frontier_v, 9), nc9));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shl(frontier_v, 9), nc0));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shl(frontier_v, 11), nc9));

    expanded = _mm256_andnot_si256(reached_v, _mm256_and_si256(expanded, alive_v));
    reached_v = _mm256_or_si256(reached_v, expanded);
    frontier_v = expanded;
  }

  BitBoard result;
  bb_store(result, reached_v);
  return result;
}

// ============================================================================
// Test helpers
// ============================================================================

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", msg);                                           \
      g_tests_failed++;                                                       \
    } else {                                                                  \
      g_tests_passed++;                                                       \
    }                                                                         \
  } while (0)

static BitBoard random_bitboard(std::mt19937_64& rng, double density = 0.5) {
  BitBoard b;
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  for (int i = 0; i < kGridCells; i++) {
    if (dist(rng) < density) b.set(i);
  }
  return b;
}

// ============================================================================
// Correctness tests
// ============================================================================

static void test_or_with() {
  printf("Testing InlineBitset::or_with ...\n");
  std::mt19937_64 rng(42);

  // Test with various sizes including 128 words (full), partial sizes, etc.
  int test_sizes[] = {64, 128, 256, 500, 1000, 2000, 4000, 8000, 8192};
  for (int sz : test_sizes) {
    InlineBitset a(sz), b(sz);
    int nwords = (sz + 63) >> 6;
    for (int w = 0; w < nwords; w++) {
      a.words[w] = rng();
      b.words[w] = rng();
    }

    InlineBitset a_scalar = a, a_simd = a;
    a_scalar.or_with_scalar(b);
    a_simd.or_with_simd(b);

    bool match = (a_scalar == a_simd);
    char msg[128];
    snprintf(msg, sizeof(msg), "or_with size=%d: scalar == simd", sz);
    CHECK(match, msg);
    if (!match) {
      for (int w = 0; w < nwords; w++) {
        if (a_scalar.words[w] != a_simd.words[w]) {
          printf("    word[%d]: scalar=0x%016lx simd=0x%016lx\n",
                 w, a_scalar.words[w], a_simd.words[w]);
          break;
        }
      }
    }
  }

  // Edge case: empty bitset
  {
    InlineBitset a(0), b(0);
    InlineBitset a_scalar = a, a_simd = a;
    a_scalar.or_with_scalar(b);
    a_simd.or_with_simd(b);
    CHECK(a_scalar == a_simd, "or_with size=0: scalar == simd");
  }

  // Edge case: all-ones
  {
    InlineBitset a(8192), b(8192);
    int nwords = 128;
    memset(a.words, 0xFF, nwords * 8);
    memset(b.words, 0xFF, nwords * 8);
    InlineBitset a_scalar = a, a_simd = a;
    a_scalar.or_with_scalar(b);
    a_simd.or_with_simd(b);
    CHECK(a_scalar == a_simd, "or_with all-ones: scalar == simd");
  }

  // Edge case: all-zeros
  {
    InlineBitset a(8192), b(8192);
    InlineBitset a_scalar = a, a_simd = a;
    a_scalar.or_with_scalar(b);
    a_simd.or_with_simd(b);
    CHECK(a_scalar == a_simd, "or_with all-zeros: scalar == simd");
  }

  // Edge case: size not aligned to 256 bits (e.g. 65 bits = 2 words, only 1 AVX pass + 2 scalar)
  // Actually 2 words < 4, so no AVX passes. Try 5 words = 320 bits (1 AVX pass + 1 scalar).
  {
    InlineBitset a(320), b(320);
    int nwords = 5;
    for (int w = 0; w < nwords; w++) {
      a.words[w] = rng();
      b.words[w] = rng();
    }
    InlineBitset a_scalar = a, a_simd = a;
    a_scalar.or_with_scalar(b);
    a_simd.or_with_simd(b);
    CHECK(a_scalar == a_simd, "or_with size=320 (5 words): scalar == simd");
  }
}

static void test_bb_shift() {
  printf("Testing BitBoard shr/shl SIMD vs scalar ...\n");
  std::mt19937_64 rng(123);

  int shift_amounts[] = {1, 9, 10, 11, 2, 7, 13, 32, 63};
  for (int n : shift_amounts) {
    for (int trial = 0; trial < 20; trial++) {
      BitBoard b = random_bitboard(rng, 0.5);

      // Test shr
      BitBoard scalar_shr = b.shr(n);
      __m256i v = bb_load(b);
      __m256i simd_shr_v = bb_shr(v, n);
      BitBoard simd_shr;
      bb_store(simd_shr, simd_shr_v);

      // Only compare the 130 valid bits (mask out bits >= 130)
      // Actually, scalar shr produces valid data in all 192 bits. The SIMD
      // version might produce garbage in lane 3 but bb_store ignores it.
      // For a fair comparison, mask to 192 bits (all 3 words).
      bool shr_match = (scalar_shr == simd_shr);
      if (!shr_match) {
        char msg[128];
        snprintf(msg, sizeof(msg), "shr(%d) trial %d", n, trial);
        CHECK(false, msg);
        printf("    scalar: [0x%016lx, 0x%016lx, 0x%016lx]\n",
               scalar_shr.w[0], scalar_shr.w[1], scalar_shr.w[2]);
        printf("    simd:   [0x%016lx, 0x%016lx, 0x%016lx]\n",
               simd_shr.w[0], simd_shr.w[1], simd_shr.w[2]);
      } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "shr(%d) trial %d", n, trial);
        CHECK(true, msg);
      }

      // Test shl
      BitBoard scalar_shl = b.shl(n);
      __m256i simd_shl_v = bb_shl(v, n);
      BitBoard simd_shl;
      bb_store(simd_shl, simd_shl_v);

      bool shl_match = (scalar_shl == simd_shl);
      if (!shl_match) {
        char msg[128];
        snprintf(msg, sizeof(msg), "shl(%d) trial %d", n, trial);
        CHECK(false, msg);
        printf("    scalar: [0x%016lx, 0x%016lx, 0x%016lx]\n",
               scalar_shl.w[0], scalar_shl.w[1], scalar_shl.w[2]);
        printf("    simd:   [0x%016lx, 0x%016lx, 0x%016lx]\n",
               simd_shl.w[0], simd_shl.w[1], simd_shl.w[2]);
      } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "shl(%d) trial %d", n, trial);
        CHECK(true, msg);
      }
    }
  }

  // Edge case: single bit at word boundaries
  printf("Testing BitBoard shift at word boundaries ...\n");
  for (int n : shift_amounts) {
    int boundary_bits[] = {0, 63, 64, 127, 128, 129};
    for (int bit : boundary_bits) {
      BitBoard b;
      b.set(bit);

      BitBoard scalar_shr = b.shr(n);
      __m256i v = bb_load(b);
      BitBoard simd_shr;
      bb_store(simd_shr, bb_shr(v, n));
      char msg[128];
      snprintf(msg, sizeof(msg), "shr(%d) single bit at %d", n, bit);
      CHECK(scalar_shr == simd_shr, msg);

      BitBoard scalar_shl = b.shl(n);
      BitBoard simd_shl;
      bb_store(simd_shl, bb_shl(v, n));
      snprintf(msg, sizeof(msg), "shl(%d) single bit at %d", n, bit);
      CHECK(scalar_shl == simd_shl, msg);
    }
  }
}

static void test_expand_all_dirs() {
  printf("Testing expand_all_dirs SIMD vs scalar ...\n");
  std::mt19937_64 rng(456);

  for (int trial = 0; trial < 100; trial++) {
    BitBoard b = random_bitboard(rng, 0.3);
    BitBoard scalar_result = expand_all_dirs_scalar(b);
    BitBoard simd_result = expand_all_dirs_simd(b);

    char msg[128];
    snprintf(msg, sizeof(msg), "expand_all_dirs trial %d", trial);
    CHECK(scalar_result == simd_result, msg);
    if (!(scalar_result == simd_result)) {
      printf("    scalar: [0x%016lx, 0x%016lx, 0x%016lx]\n",
             scalar_result.w[0], scalar_result.w[1], scalar_result.w[2]);
      printf("    simd:   [0x%016lx, 0x%016lx, 0x%016lx]\n",
             simd_result.w[0], simd_result.w[1], simd_result.w[2]);
    }
  }

  // Edge case: single cell in each position
  for (int pos = 0; pos < kGridCells; pos++) {
    BitBoard b;
    b.set(pos);
    BitBoard scalar_result = expand_all_dirs_scalar(b);
    BitBoard simd_result = expand_all_dirs_simd(b);

    char msg[128];
    snprintf(msg, sizeof(msg), "expand_all_dirs single cell at %d", pos);
    CHECK(scalar_result == simd_result, msg);
  }

  // Edge case: empty board
  {
    BitBoard b;
    CHECK(expand_all_dirs_scalar(b) == expand_all_dirs_simd(b),
          "expand_all_dirs empty board");
  }

  // Edge case: full board
  {
    BitBoard b;
    for (int i = 0; i < kGridCells; i++) b.set(i);
    CHECK(expand_all_dirs_scalar(b) == expand_all_dirs_simd(b),
          "expand_all_dirs full board");
  }
}

static void test_flood_fill() {
  printf("Testing bitboard_flood_fill SIMD vs scalar ...\n");
  std::mt19937_64 rng(789);

  // Random boards with varying density
  double densities[] = {0.1, 0.3, 0.5, 0.7, 0.9};
  for (double d : densities) {
    for (int trial = 0; trial < 50; trial++) {
      BitBoard alive = random_bitboard(rng, d);
      // Seeds from row 0 (like player 1 home edge)
      BitBoard seeds = alive & sRow0Mask;

      BitBoard scalar_result = bitboard_flood_fill_scalar(seeds, alive);
      BitBoard simd_result = bitboard_flood_fill_simd(seeds, alive);

      char msg[128];
      snprintf(msg, sizeof(msg), "flood_fill density=%.1f trial %d (row0 seeds)", d, trial);
      CHECK(scalar_result == simd_result, msg);
      if (!(scalar_result == simd_result)) {
        printf("    scalar: [0x%016lx, 0x%016lx, 0x%016lx] pop=%d\n",
               scalar_result.w[0], scalar_result.w[1], scalar_result.w[2],
               scalar_result.popcount());
        printf("    simd:   [0x%016lx, 0x%016lx, 0x%016lx] pop=%d\n",
               simd_result.w[0], simd_result.w[1], simd_result.w[2],
               simd_result.popcount());
      }
    }

    // Seeds from row 12 (like player 2 home edge)
    for (int trial = 0; trial < 50; trial++) {
      BitBoard alive = random_bitboard(rng, d);
      BitBoard seeds = alive & sRow12Mask;

      BitBoard scalar_result = bitboard_flood_fill_scalar(seeds, alive);
      BitBoard simd_result = bitboard_flood_fill_simd(seeds, alive);

      char msg[128];
      snprintf(msg, sizeof(msg), "flood_fill density=%.1f trial %d (row12 seeds)", d, trial);
      CHECK(scalar_result == simd_result, msg);
    }
  }

  // Edge case: empty alive
  {
    BitBoard seeds, alive;
    seeds.set(0);
    CHECK(bitboard_flood_fill_scalar(seeds, alive) == bitboard_flood_fill_simd(seeds, alive),
          "flood_fill empty alive");
  }

  // Edge case: seeds not in alive
  {
    BitBoard seeds, alive;
    seeds.set(5);
    alive.set(10);
    alive.set(20);
    CHECK(bitboard_flood_fill_scalar(seeds, alive) == bitboard_flood_fill_simd(seeds, alive),
          "flood_fill seeds not in alive");
  }

  // Edge case: fully connected board
  {
    BitBoard alive;
    for (int i = 0; i < kGridCells; i++) alive.set(i);
    BitBoard seeds;
    seeds.set(0);
    BitBoard scalar_result = bitboard_flood_fill_scalar(seeds, alive);
    BitBoard simd_result = bitboard_flood_fill_simd(seeds, alive);
    CHECK(scalar_result == simd_result, "flood_fill fully connected");
    CHECK(scalar_result.popcount() == kGridCells, "flood_fill fully connected reaches all cells");
  }
}

// ============================================================================
// Benchmarks
// ============================================================================

static void bench_or_with() {
  printf("\nBenchmark: InlineBitset::or_with (128 words = 8192 bits)\n");

  std::mt19937_64 rng(42);
  InlineBitset a(8192), b(8192);
  int nwords = 128;
  for (int w = 0; w < nwords; w++) {
    a.words[w] = rng();
    b.words[w] = rng();
  }

  const int iters = 10000000;

  // Scalar benchmark
  {
    InlineBitset tmp = a;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) {
      tmp.or_with_scalar(b);
      // Prevent dead code elimination: use inline asm to mark tmp as live
      asm volatile("" : "+m"(tmp.words[0]));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    printf("  scalar: %.1f ns/op\n", ns);
  }

  // SIMD benchmark
  {
    InlineBitset tmp = a;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) {
      tmp.or_with_simd(b);
      asm volatile("" : "+m"(tmp.words[0]));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    printf("  simd:   %.1f ns/op\n", ns);
  }
}

static void bench_expand_all_dirs() {
  printf("\nBenchmark: expand_all_dirs\n");

  std::mt19937_64 rng(42);
  BitBoard b = random_bitboard(rng, 0.5);

  const int iters = 10000000;

  // Scalar
  {
    BitBoard result;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) {
      result = expand_all_dirs_scalar(b);
      asm volatile("" : "+m"(result.w[0]));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    printf("  scalar: %.1f ns/op\n", ns);
  }

  // SIMD
  {
    BitBoard result;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) {
      result = expand_all_dirs_simd(b);
      asm volatile("" : "+m"(result.w[0]));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    printf("  simd:   %.1f ns/op\n", ns);
  }
}

static void bench_flood_fill() {
  printf("\nBenchmark: bitboard_flood_fill\n");

  std::mt19937_64 rng(42);
  // Create a moderately dense board (typical game state)
  BitBoard alive = random_bitboard(rng, 0.5);
  BitBoard seeds = alive & sRow0Mask;

  const int iters = 2000000;

  // Scalar
  {
    BitBoard result;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) {
      result = bitboard_flood_fill_scalar(seeds, alive);
      asm volatile("" : "+m"(result.w[0]));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    printf("  scalar: %.1f ns/op\n", ns);
  }

  // SIMD
  {
    BitBoard result;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iters; i++) {
      result = bitboard_flood_fill_simd(seeds, alive);
      asm volatile("" : "+m"(result.w[0]));
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    printf("  simd:   %.1f ns/op\n", ns);
  }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  bool run_tests = true;
  bool run_bench = true;

  if (argc > 1) {
    if (strcmp(argv[1], "--test") == 0) {
      run_bench = false;
    } else if (strcmp(argv[1], "--bench") == 0) {
      run_tests = false;
    }
  }

  init_bitboard_masks();

  if (run_tests) {
    printf("=== SIMD Correctness Tests ===\n\n");

    test_or_with();
    test_bb_shift();
    test_expand_all_dirs();
    test_flood_fill();

    printf("\n=== Results: %d passed, %d failed ===\n",
           g_tests_passed, g_tests_failed);
    if (g_tests_failed > 0) {
      printf("FAIL\n");
      if (!run_bench) return 1;
    } else {
      printf("PASS\n");
    }
  }

  if (run_bench) {
    printf("\n=== SIMD Benchmarks ===\n");
    bench_or_with();
    bench_expand_all_dirs();
    bench_flood_fill();
    printf("\n");
  }

  return g_tests_failed > 0 ? 1 : 0;
}
