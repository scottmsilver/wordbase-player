#ifndef INLINE_BITSET_H
#define INLINE_BITSET_H

#include <cstdint>
#include <cstring>
#ifdef WORDBASE_USE_SIMD
#include <immintrin.h>
#endif

// Fixed-capacity bitset that avoids heap allocation. Max 8192 bits (1KB).
//
// Used instead of boost::dynamic_bitset in hot paths to keep data in
// L1/L2 cache rather than chasing heap pointers. The OR accumulator in
// fill_legal_moves touches ~1KB per owned cell (~65 cells = ~65KB); keeping
// this on the stack avoids heap allocation overhead and improves locality.
//
// Copy/assign only copies the used portion (size_bits / 64 words), not
// the full 1KB, which is important since StateUndoer copies the entire
// game state (including mPlayedWords) at every search node.
struct InlineBitset {
  static constexpr int kMaxWords = 128;  // 128 * 64 = 8192 bits
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

  bool operator[](int i) const { return (words[i >> 6] >> (i & 63)) & 1; }
  void set(int i, bool v) {
    if (v) words[i >> 6] |= (1ULL << (i & 63));
    else words[i >> 6] &= ~(1ULL << (i & 63));
  }
  int size() const { return size_bits; }
  bool operator==(const InlineBitset& rhs) const {
    if (size_bits != rhs.size_bits) return false;
    int nwords = (size_bits + 63) >> 6;
    return memcmp(words, rhs.words, nwords * 8) == 0;
  }

  // OR all words from another InlineBitset into this one.
  // Both must have the same size. Used in fill_legal_moves to accumulate
  // per-cell word bitsets into a single "all reachable words" bitset.
  void or_with(const InlineBitset& rhs) {
    const int nwords = (size_bits + 63) >> 6;
#ifdef WORDBASE_USE_SIMD
    int w = 0;
    for (; w + 4 <= nwords; w += 4) {
      __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&words[w]));
      __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&rhs.words[w]));
      _mm256_storeu_si256(reinterpret_cast<__m256i*>(&words[w]), _mm256_or_si256(a, b));
    }
    for (; w < nwords; w++) {
      words[w] |= rhs.words[w];
    }
#else
    for (int w = 0; w < nwords; w++) {
      words[w] |= rhs.words[w];
    }
#endif
  }
};

#endif
