/*
 wordescape.cpp
 Copyright (C) 2016 Scott Silver.

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <boost/format.hpp>
#include <boost/functional/hash.hpp>
#include <boost/sort/spreadsort/spreadsort.hpp>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef WORDBASE_USE_SIMD
#include <immintrin.h>
#endif

#include "board.h"
#include "grid.h"
#include "gtsa.hpp"
#include "obstack/obstack.hpp"
#include "string-util.h"
#include "wordbase-move.h"

const char PLAYER_1 = 1;
const char PLAYER_2 = 2;
const char PLAYER_UNOWNED = 0;
const char PLAYER_BOMB = 3;
const char PLAYER_MEGABOMB = 4;

const char kOwnerUnowned = 0;
const char kOwnerMaximizer = 1;
const char kOwnerMinimizer = 2;

// --- Bitboard flood-fill for connectivity checking ---
//
// After a move captures enemy cells, we need to check if any remaining enemy
// cells are now disconnected from their home edge. Disconnected cells are
// removed from the board (set to unowned).
//
// The naive approach uses a per-cell stack-based BFS (flood-fill), visiting
// one cell at a time with bounds checks and stack push/pop per cell.
//
// The bitboard approach represents the entire 13x10 grid as a 130-bit value
// (3 x uint64_t). Instead of visiting cells one-by-one, we expand ALL
// frontier cells simultaneously using bitwise shift and OR operations:
//
//   Old (per-cell BFS):     ~65 cells × 8 neighbors × (bounds check + stack op)
//   New (bitboard BFS):     ~13 iterations × 8 shifts × 3 uint64 ops = ~312 ops
//
// This gives ~5-10x speedup on the flood-fill itself (measured: reduced
// make_move from 47% to 29% of total profile, flood-fill from ~40% to 8.8%).
//
// How the bit-shifting works:
//
//   The grid is stored row-major, so cell (y, x) is at bit position y*10 + x.
//   To find all neighbors of all set cells simultaneously, we shift the whole
//   bitboard by the appropriate offset:
//
//     Grid positions (width=10):        Bit layout in uint64_t words:
//       (0,0) (0,1) ... (0,9)            bits 0-9   = row 0
//       (1,0) (1,1) ... (1,9)            bits 10-19 = row 1
//       ...                               ...
//       (6,0) (6,1) ... (6,3)             bits 60-63 = row 6 cols 0-3
//                                       [word boundary at bit 64]
//       (6,4) (6,5) ... (6,9)             bits 64-69 = row 6 cols 4-9
//       ...                               ...
//       (12,0) ... (12,9)                 bits 120-129 = row 12
//
//   Neighbor directions as bit offsets:
//     right     = >>1   (but col 9 must not wrap to col 0 of next row)
//     left      = <<1   (but col 0 must not wrap to col 9 of prev row)
//     down      = >>10  (move to same column, next row)
//     up        = <<10  (move to same column, prev row)
//     down-right= >>11  (down + right, mask col 0)
//     down-left = >>9   (down + left, mask col 9)
//     up-right  = <<9   (up + right, mask col 0)
//     up-left   = <<11  (up + left, mask col 9)
//
//   Column masks prevent wrap-around. Example with a 5-wide grid:
//
//     Before shift:          After >>1 (right):       After >>1 & not_col_0:
//     . . . . X              . . . . .                . . . . .
//     . . . . .              X . . . .  (WRONG wrap)  . . . . .  (masked out)
//
//   The BFS loop expands the frontier by one step in all 8 directions each
//   iteration, AND-ing with 'alive' (enemy cells) and masking off already-
//   reached cells. Converges in at most 13 iterations (grid height).

constexpr int kGridCells = kBoardHeight * kBoardWidth;  // 130

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

  // Shift right by n bits (n < 64). Equivalent to >> on a 192-bit integer.
  // Cross-word carry: low bits of w[i+1] flow into high bits of w[i].
  BitBoard shr(int n) const {
    return {{(w[0] >> n) | (w[1] << (64 - n)),
             (w[1] >> n) | (w[2] << (64 - n)),
             w[2] >> n}};
  }

  // Shift left by n bits (n < 64). Equivalent to << on a 192-bit integer.
  // Cross-word carry: high bits of w[i] flow into low bits of w[i+1].
  BitBoard shl(int n) const {
    return {{w[0] << n,
             (w[1] << n) | (w[0] >> (64 - n)),
             (w[2] << n) | (w[1] >> (64 - n))}};
  }

  // Iterate over set bits, calling f(bit_position) for each.
  // Uses the "clear lowest set bit" trick: bits &= bits - 1.
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
};

// Column masks for bitboard shifts: prevent wrap-around across row boundaries.
//
//   not_col_0: all cells NOT in column 0.
//     Applied after right-shifts (>>1, >>11, <<9) to prevent the rightmost
//     cell of one row from appearing as column 0 of the next row.
//
//   not_col_9: all cells NOT in column 9.
//     Applied after left-shifts (<<1, >>9, <<11) to prevent the leftmost
//     cell of one row from appearing as column 9 of the previous row.
//
//   sRow0Mask / sRow12Mask: all cells in row 0 / row 12 (home edges).
//     Used to extract home-edge seeds from player bitboards for flood-fill,
//     avoiding a full grid scan.
static BitBoard not_col_0, not_col_9;
static BitBoard sRow0Mask, sRow12Mask;
static bool bitboard_masks_initialized = false;

static void init_bitboard_masks() {
  if (bitboard_masks_initialized) return;
  for (int y = 0; y < kBoardHeight; y++) {
    for (int x = 0; x < kBoardWidth; x++) {
      int pos = y * kBoardWidth + x;
      if (x != 0) not_col_0.set(pos);
      if (x != kBoardWidth - 1) not_col_9.set(pos);
      if (y == 0) sRow0Mask.set(pos);
      if (y == kBoardHeight - 1) sRow12Mask.set(pos);
    }
  }
  bitboard_masks_initialized = true;
}

// Expand a frontier bitboard one step in all 8 directions on the grid.
// Each shift moves all set bits to a neighboring position simultaneously.
// Column masks prevent horizontal wrap-around; vertical shifts (>>10, <<10)
// naturally don't wrap because the grid width (10) separates rows.
static BitBoard expand_all_dirs(const BitBoard& b) {
  BitBoard result = {};
  result |= b.shr(1)  & not_col_0;   // right      (x+1)
  result |= b.shl(1)  & not_col_9;   // left       (x-1)
  result |= b.shr(10);               // down       (y+1)
  result |= b.shl(10);               // up         (y-1)
  result |= b.shr(11) & not_col_0;   // down-right (y+1, x+1)
  result |= b.shr(9)  & not_col_9;   // down-left  (y+1, x-1)
  result |= b.shl(9)  & not_col_0;   // up-right   (y-1, x+1)
  result |= b.shl(11) & not_col_9;   // up-left    (y-1, x-1)
  return result;
}

// Bitboard BFS: find all cells in 'alive' reachable from 'seeds'.
//
// Example: finding connected enemy cells after P1 captures two P2 cells.
//
//   Grid state:              enemy bitboard (P2):      home edge seeds:
//   1 1 1 1 1  row 0         . . . . .                 . . . . .
//   . 1 1 . .  row 1         . . . . .                 . . . . .
//   . 2 . . .  row 2         . 1 . . .                 . . . . .
//   2 2 2 2 2  row 3         1 1 1 1 1                 1 1 1 1 1
//
//   Iteration 1 (expand from home edge row 3):
//     frontier = row 3 cells   →  expanded = row 2 neighbors of row 3
//     reached gains cell (2,1)
//
//   Iteration 2: frontier = {(2,1)}  →  no new enemy neighbors  →  done.
//     reached = {row 3 cells, (2,1)} = all connected
//     disconnected = enemy & ~reached = {} (nothing disconnected)
//
//   If cell (2,1) had no enemy neighbor in row 3, it would NOT be in
//   'reached' and would be removed from the board.
//
// Returns a bitboard of all cells reachable from seeds through alive cells.
static BitBoard bitboard_flood_fill(const BitBoard& seeds, const BitBoard& alive) {
  BitBoard reached = seeds & alive;
  BitBoard frontier = reached;
  while (frontier.any()) {
    BitBoard expanded = expand_all_dirs(frontier) & alive & (~reached);
    reached |= expanded;
    frontier = expanded;
  }
  return reached;
}

#ifdef WORDBASE_USE_SIMD
// --- AVX2 SIMD versions of BitBoard operations ---
//
// A BitBoard is 3 x uint64 = 192 bits. We load it into a __m256i with the
// fourth lane zeroed. All shift/mask/OR operations happen in-register,
// avoiding per-word scalar loops.

// Load a BitBoard into a __m256i (w[0], w[1], w[2], 0).
// Uses two 128-bit loads to avoid reading past the struct.
static inline __m256i bb_load(const BitBoard& b) {
  __m128i lo = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&b.w[0]));
  __m128i hi = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(&b.w[2]));
  return _mm256_inserti128_si256(_mm256_castsi128_si256(lo), hi, 1);
}

// Store a __m256i back to a BitBoard (only lanes 0-2).
static inline void bb_store(BitBoard& b, __m256i v) {
  __m128i lo = _mm256_castsi256_si128(v);
  __m128i hi = _mm256_extracti128_si256(v, 1);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(&b.w[0]), lo);
  b.w[2] = _mm_extract_epi64(hi, 0);
}

// Shift right by n bits across the 192-bit value packed in lanes 0-2.
//
// Scalar equivalent:
//   w[0] = (w[0] >> n) | (w[1] << (64-n))
//   w[1] = (w[1] >> n) | (w[2] << (64-n))
//   w[2] = w[2] >> n
//
// Permute 0x39 = 0b00'11'10'01 maps: dest[0]=src[1], dest[1]=src[2],
// dest[2]=src[3]=0, dest[3]=src[0] (don't care). This gives us the
// carry source {w[1], w[2], 0, w[0]}.
static inline __m256i bb_shr(__m256i v, int n) {
  __m256i shifted = _mm256_srli_epi64(v, n);
  __m256i carry_src = _mm256_permute4x64_epi64(v, 0x39);
  __m256i carry = _mm256_slli_epi64(carry_src, 64 - n);
  return _mm256_or_si256(shifted, carry);
}

// Shift left by n bits across the 192-bit value packed in lanes 0-2.
//
// Scalar equivalent:
//   w[0] = w[0] << n
//   w[1] = (w[1] << n) | (w[0] >> (64-n))
//   w[2] = (w[2] << n) | (w[1] >> (64-n))
//
// Permute 0x93 = 0b10'01'00'11 maps: dest[0]=src[3]=0, dest[1]=src[0],
// dest[2]=src[1], dest[3]=src[2] (don't care). This gives us the
// carry source {0, w[0], w[1], w[2]}.
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
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shr(v, 1), nc0));   // right
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shl(v, 1), nc9));   // left
  result = _mm256_or_si256(result, bb_shr(v, 10));                          // down
  result = _mm256_or_si256(result, bb_shl(v, 10));                          // up
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shr(v, 11), nc0));  // down-right
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shr(v, 9), nc9));   // down-left
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shl(v, 9), nc0));   // up-right
  result = _mm256_or_si256(result, _mm256_and_si256(bb_shl(v, 11), nc9));  // up-left

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
    // Inline expand_all_dirs for frontier
    __m256i expanded = _mm256_setzero_si256();
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shr(frontier_v, 1), nc0));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shl(frontier_v, 1), nc9));
    expanded = _mm256_or_si256(expanded, bb_shr(frontier_v, 10));
    expanded = _mm256_or_si256(expanded, bb_shl(frontier_v, 10));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shr(frontier_v, 11), nc0));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shr(frontier_v, 9), nc9));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shl(frontier_v, 9), nc0));
    expanded = _mm256_or_si256(expanded, _mm256_and_si256(bb_shl(frontier_v, 11), nc9));

    // expanded &= alive & ~reached
    expanded = _mm256_andnot_si256(reached_v, _mm256_and_si256(expanded, alive_v));
    reached_v = _mm256_or_si256(reached_v, expanded);
    frontier_v = expanded;
  }

  BitBoard result;
  bb_store(result, reached_v);
  return result;
}
#endif

boost::arena::obstack gObstack(1024*1024*1024);

// The storage representation of state of a Wordbase game; essentially a grid
// the correct size and initialized to Player 1 owns first row and Player 2 owns
// second row.
class WordBaseGridState : public Grid<char, kBoardHeight, kBoardWidth> {
public:
  // Initialize GridState by copying the underlying contents.
  WordBaseGridState(const WordBaseGridState& state) : Grid<char, kBoardHeight, kBoardWidth>(state) {}

  // Initialize GridState to an unused board.
  WordBaseGridState() : Grid() {
    fill(kOwnerUnowned);
    for (int x = 0; x < kBoardWidth; x++) {
      set(0, x, PLAYER_1);
      set(kBoardHeight - 1, x, PLAYER_2);
    }
  }
};

// Functor to help compare two moves on the basis of their heuristic score (aka Goodness)
struct Goodness {
  const BoardStatic& mBoard;
  char mPlayerToMove;

  Goodness(const BoardStatic& board, char playerToMove) : mBoard(board), mPlayerToMove(playerToMove) {}

  // Return true if the i is a better more than j.
  bool operator()(const WordBaseMove& i, const WordBaseMove& j) const {
    // FIX-ME this is suspect. Why does > give better values than <
    return heuristicValue(i) > heuristicValue(j);
  }

  // Return the heuristic value (aka Goodness) of this move.
  int heuristicValue(const WordBaseMove& x) const {
    return (mPlayerToMove == PLAYER_1) ? mBoard.getLegalWord(x.mLegalWordId).mMaximizerGoodness : mBoard.getLegalWord(x.mLegalWordId).mMinimizerGoodness;
  }

  // Used in conjunction with spreadsort. Right shift the value. See spreadshort for documentation.
  inline int operator()(const WordBaseMove & x, unsigned offset) const {
    return heuristicValue(x) >> offset;
  }
};

// Functor to help compare two moves on the basis of their heuristic score (aka Goodness)
struct Goodness2 {
  const BoardStatic& mBoard;
  char mPlayerToMove;

  Goodness2(const BoardStatic& board, char playerToMove) : mBoard(board), mPlayerToMove(playerToMove) {}

  // Return true if the i is a better more than j.
  bool operator()(const WordBaseMove& i, const WordBaseMove& j) const {
    return heuristicValue(i) < heuristicValue(j);
  }

  // Return the heuristic value (aka Goodness) of this move.
  int heuristicValue(const WordBaseMove& x) const {
    return (mPlayerToMove == PLAYER_1) ? mBoard.getLegalWord(x.mLegalWordId).mRenumberedMaximizerGoodness : mBoard.getLegalWord(x.mLegalWordId).mRenumberedMinimizerGoodness;
  }

  // Used in conjunction with spreadsort. Right shift the value. See spreadshort for documentation.
  inline int operator()(const WordBaseMove & x, unsigned offset) const {
    return heuristicValue(x) >> offset;
  }
};


// InlineBitset is defined in inline-bitset.h (shared with board.h).

struct WordBaseState : public State<WordBaseState, WordBaseMove> {
  BoardStatic* mBoard;
  WordBaseGridState mState;
  InlineBitset mPlayedWords;
  size_t mHashValue;
  uint64_t mTtVerificationKey;
  int mGoodnessAccum;
  bool mTookEnemyCell;
  // Incrementally maintained bitboards for each player's cells.
  // Updated in setCellState, used directly in make_move's connectivity
  // check to avoid scanning all 130 grid cells to build the enemy bitboard.
  // Cost: 48 extra bytes per state copy (2 × 24 bytes), negligible vs the
  // 1KB InlineBitset copy. Savings: eliminate 130-cell grid scan per move.
  BitBoard mPlayer1Bits, mPlayer2Bits;

  WordBaseState(BoardStatic* board, char playerToMove)
    : State<WordBaseState, WordBaseMove>(playerToMove),
      mBoard(board),
      mPlayedWords(mBoard->getLegalWordsSize()),
      mHashValue(0),
      mTtVerificationKey(0),
      mGoodnessAccum(0),
      mTookEnemyCell(false) {
    init_bitboard_masks();
    initLookupTables();
    putBomb(board->getBombs(), false);
    putBomb(board->getMegabombs(), true);
    mHashValue = computeHashFromState();
    mTtVerificationKey = computeVerificationKeyFromState();
    mGoodnessAccum = computeGoodnessAccum();
    // Initialize player bitboards from the grid state.
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        const int pos = y * kBoardWidth + x;
        const char owner = mState.get(y, x);
        if (owner == PLAYER_1) mPlayer1Bits.set(pos);
        else if (owner == PLAYER_2) mPlayer2Bits.set(pos);
      }
    }
  }

  // Copy constructor.
  WordBaseState(const WordBaseState& rhs) :
  State<WordBaseState, WordBaseMove>(rhs.player_to_move),
  mBoard(rhs.mBoard),
  mState(rhs.mState), mPlayedWords(rhs.mPlayedWords), mHashValue(rhs.mHashValue), mTtVerificationKey(rhs.mTtVerificationKey), mGoodnessAccum(rhs.mGoodnessAccum), mTookEnemyCell(false),
  mPlayer1Bits(rhs.mPlayer1Bits), mPlayer2Bits(rhs.mPlayer2Bits) {
    mSearchDepthRemaining = rhs.mSearchDepthRemaining;
  }

  WordBaseState clone() const override {
    return WordBaseState(*this);
  }

  // Lightweight state snapshot that excludes mPlayedWords (~1KB).
  // Used by the search to save/restore state around make_move, with
  // played-word changes undone incrementally (5-6x less data to copy).
  struct LightSnapshot {
    WordBaseGridState mState;           // 130 bytes
    size_t mHashValue;                  // 8
    uint64_t mTtVerificationKey;        // 8
    int mGoodnessAccum;                 // 4
    char player_to_move;                // 1
    BitBoard mPlayer1Bits, mPlayer2Bits; // 48
    int mSearchDepthRemaining;          // 4
    // Saved played-word bits that were false before make_move and set to true.
    // Only these bits should be cleared during undo (parent-level bits stay).
    LegalWordId changedIds[8];
    int numChanged;
    // Total: ~239 bytes vs ~1236 bytes for full state
  };

  LightSnapshot takeSnapshot(const WordBaseMove& move) const {
    LightSnapshot s = {mState, mHashValue, mTtVerificationKey, mGoodnessAccum,
                       player_to_move, mPlayer1Bits, mPlayer2Bits,
                       mSearchDepthRemaining, {}, 0};
    // Record which equivalent-word bits are currently false (will be set by make_move).
    const std::vector<LegalWordId>& eqIds = mBoard->getEquivalentLegalWordIds(move.mLegalWordId);
    for (auto id : eqIds) {
      if (!mPlayedWords[id]) {
        s.changedIds[s.numChanged++] = id;
        if (s.numChanged >= 8) break;  // safety cap
      }
    }
    return s;
  }

  void restoreSnapshot(const LightSnapshot& s) {
    // First, clear only the played-word bits that were actually changed.
    for (int i = 0; i < s.numChanged; i++) {
      mPlayedWords.set(s.changedIds[i], false);
    }
    // Then restore all other state fields.
    mState = s.mState;
    mHashValue = s.mHashValue;
    mTtVerificationKey = s.mTtVerificationKey;
    mGoodnessAccum = s.mGoodnessAccum;
    player_to_move = s.player_to_move;
    mPlayer1Bits = s.mPlayer1Bits;
    mPlayer2Bits = s.mPlayer2Bits;
    mSearchDepthRemaining = s.mSearchDepthRemaining;
  }

  // No-op: played word undo is now handled inside restoreSnapshot.
  void undoPlayedWords(const WordBaseMove&) {}

  const WordBaseGridState& getGridState() const { return mState; }
  const BoardStatic& getBoardStatic() const { return *mBoard; }

  // Place bombs at each point in the supplied sequence.
  void putBomb(CoordinateList sequence, bool megaBomb) {
    for (auto&& bombPlace : sequence) {
      setCellState(bombPlace.first, bombPlace.second, megaBomb ? PLAYER_MEGABOMB : PLAYER_BOMB);
    }
  }

  static size_t mixHashToken(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return static_cast<size_t>(value);
  }

  static uint64_t mixVerificationToken(uint64_t value) {
    value += 0x6a09e667f3bcc909ULL;
    value = (value ^ (value >> 33)) * 0xff51afd7ed558ccdULL;
    value = (value ^ (value >> 33)) * 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return value;
  }

  // --- Precomputed Zobrist-style hash tables ---
  //
  // setCellState is called for every cell in every word played (millions of
  // times per search). Each call XORs in/out hash and verification tokens for
  // the old and new owner (4 token lookups per call).
  //
  // Without precomputation, each token requires 3 multiplies + 3 shifts
  // (the mixHashToken/mixVerificationToken functions). That's 24 multiply-shift
  // operations per setCellState call.
  //
  // With precomputation, each token is a single array lookup.
  // Table size: 130 cells × 5 owners × 16 bytes (hash + verify) = 10.4 KB.
  // Fits comfortably in L1 cache (typically 32-64 KB).
  //
  // Owner values: PLAYER_UNOWNED=0, PLAYER_1=1, PLAYER_2=2, PLAYER_BOMB=3, PLAYER_MEGABOMB=4
  static constexpr int kMaxOwner = 5;
  static size_t   sCellHashTable[kGridCells][kMaxOwner];
  static uint64_t sCellVerifyTable[kGridCells][kMaxOwner];
  static int      sGoodnessTable[kBoardHeight][kMaxOwner];
  static bool     sTablesInitialized;

  static void initLookupTables() {
    if (sTablesInitialized) return;
    for (int pos = 0; pos < kGridCells; pos++) {
      for (int owner = 0; owner < kMaxOwner; owner++) {
        const uint64_t index = static_cast<uint64_t>(pos);
        const uint64_t ownerValue = static_cast<uint64_t>(owner);
        sCellHashTable[pos][owner] = mixHashToken(
            (index << 8) ^ ownerValue ^ 0x3141592653589793ULL);
        sCellVerifyTable[pos][owner] = mixVerificationToken(
            (index << 8) ^ ownerValue ^ 0x243f6a8885a308d3ULL);
      }
    }
    for (int y = 0; y < kBoardHeight; y++) {
      for (int owner = 0; owner < kMaxOwner; owner++) {
        if (owner == PLAYER_1) sGoodnessTable[y][owner] = (y + 1) * (y + 1);
        else if (owner == PLAYER_2) sGoodnessTable[y][owner] = -1 * (y - kBoardHeight) * (y - kBoardHeight);
        else sGoodnessTable[y][owner] = 0;
      }
    }
    sTablesInitialized = true;
  }

  static size_t cellHashToken(int y, int x, char owner) {
    return sCellHashTable[y * kBoardWidth + x][static_cast<unsigned char>(owner)];
  }

  static uint64_t cellVerificationToken(int y, int x, char owner) {
    return sCellVerifyTable[y * kBoardWidth + x][static_cast<unsigned char>(owner)];
  }

  static size_t playedWordHashToken(LegalWordId legalWordId) {
    return mixHashToken(static_cast<uint64_t>(legalWordId) ^ 0x2718281828459045ULL);
  }

  static uint64_t playedWordVerificationToken(LegalWordId legalWordId) {
    return mixVerificationToken(static_cast<uint64_t>(legalWordId) ^ 0x13198a2e03707344ULL);
  }

  static size_t playerHashToken(char player) {
    return mixHashToken(static_cast<uint64_t>(static_cast<unsigned char>(player)) ^ 0xfeedfacecafebeefULL);
  }

  static uint64_t playerVerificationToken(char player) {
    return mixVerificationToken(static_cast<uint64_t>(static_cast<unsigned char>(player)) ^ 0xa4093822299f31d0ULL);
  }

  static int goodnessContrib(char owner, int y) {
    return sGoodnessTable[y][static_cast<unsigned char>(owner)];
  }

  int computeGoodnessAccum() const {
    int h = 0;
    for (int y = 1; y < kBoardHeight - 1; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        h += goodnessContrib(mState.get(y, x), y);
      }
    }
    return h;
  }

  void setCellState(int y, int x, char owner) {
    const char currentOwner = mState.get(y, x);
    if (currentOwner == owner) {
      return;
    }

    // Update incremental goodness for non-edge rows.
    if (y > 0 && y < kBoardHeight - 1) {
      mGoodnessAccum -= goodnessContrib(currentOwner, y);
      mGoodnessAccum += goodnessContrib(owner, y);
    }

    // Update player bitboards: clear old owner, set new owner.
    const int pos = y * kBoardWidth + x;
    if (currentOwner == PLAYER_1) mPlayer1Bits.clear(pos);
    else if (currentOwner == PLAYER_2) mPlayer2Bits.clear(pos);
    if (owner == PLAYER_1) mPlayer1Bits.set(pos);
    else if (owner == PLAYER_2) mPlayer2Bits.set(pos);

    mHashValue ^= cellHashToken(y, x, currentOwner);
    mTtVerificationKey ^= cellVerificationToken(y, x, currentOwner);
    mState.set(y, x, owner);
    mHashValue ^= cellHashToken(y, x, owner);
    mTtVerificationKey ^= cellVerificationToken(y, x, owner);
  }

  void setPlayedWord(LegalWordId legalWordId, bool played) {
    if (mPlayedWords[legalWordId] == played) {
      return;
    }

    mHashValue ^= playedWordHashToken(legalWordId);
    mTtVerificationKey ^= playedWordVerificationToken(legalWordId);
    mPlayedWords.set(legalWordId, played);
  }

  void setPlayerToMove(char player) {
    if (player_to_move == player) {
      return;
    }

    mHashValue ^= playerHashToken(player_to_move);
    mTtVerificationKey ^= playerVerificationToken(player_to_move);
    player_to_move = player;
    mHashValue ^= playerHashToken(player_to_move);
    mTtVerificationKey ^= playerVerificationToken(player_to_move);
  }

  size_t computeHashFromState() const {
    size_t seed = playerHashToken(player_to_move);
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        seed ^= cellHashToken(y, x, mState.get(y, x));
      }
    }
    for (size_t legalWordId = 0; legalWordId < mPlayedWords.size(); legalWordId++) {
      if (mPlayedWords[legalWordId]) {
        seed ^= playedWordHashToken(static_cast<LegalWordId>(legalWordId));
      }
    }
    return seed;
  }

  uint64_t computeVerificationKeyFromState() const {
    uint64_t seed = playerVerificationToken(player_to_move);
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        seed ^= cellVerificationToken(y, x, mState.get(y, x));
      }
    }
    for (size_t legalWordId = 0; legalWordId < mPlayedWords.size(); legalWordId++) {
      if (mPlayedWords[legalWordId]) {
        seed ^= playedWordVerificationToken(static_cast<LegalWordId>(legalWordId));
      }
    }
    return seed;
  }

  // Return the value of this board state from the perspective of the given player.
  // In other words, it should be positive if player_to_move has an advantage.
  int get_goodness() const override {
    // Check terminal conditions on edge rows.
    for (int x = 0; x < kBoardWidth; x++) {
      if (mState.get(0, x) == PLAYER_2) {
        return (player_to_move == PLAYER_1 ? 1 : -1) * -INF;
      }
      if (mState.get(kBoardHeight - 1, x) == PLAYER_1) {
        return (player_to_move == PLAYER_1 ? 1 : -1) * INF;
      }
    }

    int color = player_to_move == PLAYER_1 ? 1 : -1;
    return mGoodnessAccum * color;
  }

  // Return all the legal moves from this current state, only consider a maximum
  // of max_moves moves.
  std::vector<WordBaseMove> get_legal_moves(int max_moves = INF) const override {
    return get_legal_moves2(max_moves, NULL);
  }

  // Return a list of legal moves filtered by an optional filter and sorted
  // by most likely to be the "best" move.
  // filter must exactly match the found move, keeping in mind that the same
  // word may appear through multiple paths.
  // Also note that words which are already played are excluded.
  std::vector<WordBaseMove> get_legal_moves(int max_moves, const char* filter) const {
    // Maintain an ordered set, ordered by "goodness" which is a heuristic for whether
    // we think the move is likely to be very good.
    std::vector<WordBaseMove> moves;

    // For each letter owned by the current player find candidate words and filter
    // them appropriately.
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (mState.get(y, x) == player_to_move) {
          auto legalWords = mBoard->getLegalWords(y, x);
          for (auto&& legalWordId : legalWords) {
            // Ensure already played words are ignored.
            if (!mPlayedWords[legalWordId]) {
              if (filter == NULL || mBoard->getLegalWord(legalWordId).mWord.compare(filter) == 0) {
                moves.push_back(WordBaseMove(legalWordId));
              }
            }
          }
        }
      }
    }

    // Sort moves in order of Goodness. NB: This is probably the slowest part in traversing to the next
    // ply. So we have tried to heavily optimize this sort.
    boost::sort::spreadsort::integer_sort(moves.begin(), moves.end(), Goodness(*mBoard, player_to_move), Goodness(*mBoard, player_to_move));

    if (max_moves != INF && moves.size() > static_cast<size_t>(max_moves)) {
      moves.resize(max_moves);
    }

    return moves;
  }

  // Return a list of legal moves filtered by an optional filter and sorted
  // by most likely to be the "best" move.
  // filter must exactly match the found move, keeping in mind that the same
  // word may appear through multiple paths.
  // Also note that words which are already played are excluded.
  std::vector<WordBaseMove> get_legal_moves2(int max_moves, const char* filter) const {
    thread_local InlineBitset validWordBits;
    const int legalWordsSize = mBoard->getLegalWordsSize();
    validWordBits = InlineBitset(legalWordsSize);

    const bool isMaximizer = (this->player_to_move == PLAYER_1);
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (mState.get(y, x) == player_to_move) {
          const auto& legalWords = mBoard->getLegalWords(y, x);
          const auto& wordBits = legalWords.wordBits(isMaximizer);
          if (wordBits.size() != 0) {
            validWordBits.or_with(wordBits);
          }
        }
      }
    }

    const int nwords = (legalWordsSize + 63) >> 6;
    const int maxMoveCount = (max_moves == INF) ? legalWordsSize : max_moves;
    std::vector<WordBaseMove> moves;

    for (int w = 0; w < nwords; w++) {
      uint64_t bits = validWordBits.words[w];
      while (bits) {
        int bit = __builtin_ctzll(bits);
        int renumberedGoodness = w * 64 + bit;
        bits &= bits - 1;
        LegalWordId legalWordId = mBoard->getLegalWordIdFromRenumberedGoodness(renumberedGoodness, isMaximizer);
        if (!mPlayedWords[legalWordId]) {
          if (filter == NULL || mBoard->getLegalWord(legalWordId).mWord.compare(filter) == 0) {
            moves.push_back(WordBaseMove(legalWordId));
            if (static_cast<int>(moves.size()) >= maxMoveCount) {
              return moves;
            }
          }
        }
      }
    }

    return moves;
  }

  // Fill a caller-provided vector, reusing its heap allocation across calls.
  void fill_legal_moves(std::vector<WordBaseMove>& out, int max_moves) const override {
    // Collect all legal words reachable from owned cells into a bitset, then
    // iterate in goodness order (best-first).
    //
    // Uses InlineBitset (stack-allocated, 1KB) for both the accumulator and
    // the per-cell word bitsets, keeping all OR operations in L1/L2 cache.
    // Previously used boost::dynamic_bitset (heap-allocated), where ~65 ORs
    // of ~1KB bitsets = ~65KB of heap memory traffic per call.
    thread_local InlineBitset validWordBits;
    const int legalWordsSize = mBoard->getLegalWordsSize();
    validWordBits = InlineBitset(legalWordsSize);

    const bool isMaximizer = (this->player_to_move == PLAYER_1);
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (mState.get(y, x) == player_to_move) {
          const auto& legalWords = mBoard->getLegalWords(y, x);
          const auto& wordBits = legalWords.wordBits(isMaximizer);
          if (wordBits.size() != 0) {
            validWordBits.or_with(wordBits);
          }
        }
      }
    }

    out.clear();
    const int nwords = (legalWordsSize + 63) >> 6;
    int count = 0;
    const int maxMoveCount = (max_moves == INF) ? legalWordsSize : max_moves;

    for (int w = 0; w < nwords; w++) {
      uint64_t bits = validWordBits.words[w];
      while (bits) {
        int bit = __builtin_ctzll(bits);
        int renumberedGoodness = w * 64 + bit;
        bits &= bits - 1;
        LegalWordId legalWordId = mBoard->getLegalWordIdFromRenumberedGoodness(renumberedGoodness, isMaximizer);
        if (!mPlayedWords[legalWordId]) {
          out.push_back(WordBaseMove(legalWordId));
          if (++count >= maxMoveCount) {
            return;
          }
        }
      }
    }
  }

  char get_enemy(char player) const override {
    return (player == PLAYER_1) ? PLAYER_2 : PLAYER_1;
  }

  bool is_terminal() const override {
    for (int x = 0; x < kBoardWidth; x++) {
      if (mState.get(0, x) == PLAYER_2) {
        return true;
      }

      if (mState.get(kBoardHeight - 1, x) == PLAYER_1) {
        return true;
      }
    }

    // FIX-ME probably need to handle case where there are no more moves to make.
    return false;
  }

  // FIX-ME combine with a combined is_terminal.
  bool is_winner(char player) const override {
    for (int x = 0; x < kBoardWidth; x++) {
      if (player == PLAYER_2) {
        if (mState.get(0, x) == PLAYER_2) {
          return true;
        } else if (mState.get(kBoardHeight - 1, x) == PLAYER_1) {
          return true;
        }
      }
    }

    return false;
  }

  // Record the claiming of a single grid square.
  // Deal with the impacts of bombs by recursing, as appropriate.
  // Sets mTookEnemyCell if an enemy cell is overwritten.
  void recordOne(int y, int x) {
    if (y < 0 || y >= kBoardHeight || x < 0 || x >= kBoardWidth) {
      return;
    }

    const char currentOwner = mState.get(y, x);
    bool hadBomb = (currentOwner == PLAYER_BOMB);
    bool hadMegabomb = (currentOwner == PLAYER_MEGABOMB);
    if (currentOwner == get_enemy(player_to_move)) {
      mTookEnemyCell = true;
    }

    setCellState(y, x, player_to_move);

    // A bomb causes the player to get the grid squares North, South, East and
    // West of this grid square.
    if (hadBomb) {
      recordOne(y - 1, x);
      recordOne(y + 1, x);
      recordOne(y, x - 1);
      recordOne(y, x + 1);
    } else if (hadMegabomb) {
      recordOne(y - 1, x);
      recordOne(y - 1, x + 1);
      recordOne(y - 1, x - 1);
      recordOne(y, x - 1);
      recordOne(y, x + 1);
      recordOne(y + 1, x);
      recordOne(y + 1, x + 1);
      recordOne(y + 1, x - 1);
    }
  }

  // Check whether a move is valid for the current player.
  bool isValidMove(const WordBaseMove& move) const override {
    if (move.mLegalWordId < 0 || move.mLegalWordId >= static_cast<int>(mPlayedWords.size())) {
      return false;
    }
    if (mPlayedWords[move.mLegalWordId]) {
      return false;
    }
    const CoordinateList& ws = mBoard->getLegalWord(move.mLegalWordId).mWordSequence;
    return !ws.empty() && mState.get(ws[0].first, ws[0].second) == player_to_move;
  }

  // Record a single move in the game. Iterates through each grid square, claiming that square.
  void recordMove(const WordBaseMove& move) {
    assert(isValidMove(move));
    mTookEnemyCell = false;
    const CoordinateList& wordSequence = mBoard->getLegalWord(move.mLegalWordId).mWordSequence;

    // Record each letter.
    for (const auto& pathElement : wordSequence) {
      recordOne(pathElement.first, pathElement.second);
    }

    // Mark this word as played.
    const std::vector<LegalWordId>& equivalentWordIds = mBoard->getEquivalentLegalWordIds(move.mLegalWordId);
    for (auto legalWordId : equivalentWordIds) {
      setPlayedWord(legalWordId, true);
    }
  }

  // Make a move, change the current player to the other after doing this.
  void make_move(const WordBaseMove& move) override {
    // Make the move.
    recordMove(move);

    // Only check connectivity if enemy cells were taken (otherwise no disconnection possible).
    //
    // Key insight: only the ENEMY's territory can be disconnected. The moving
    // player's cells can never lose connectivity because every word starts from
    // a cell they already own (connected to their home row) and extends through
    // adjacent cells. So all their cells either existed before (still connected)
    // or were just placed (connected via the word path).
    //
    // Example: Player 1 (top) plays a word that cuts through Player 2's territory:
    //   Before:          After:
    //   1 1 1 1 1        1 1 1 1 1      <- P1 home row
    //   . 2 2 . .        . 1 1 . .      <- P1 took these P2 cells
    //   . 2 . . .        . 2 . . .      <- this P2 cell is now disconnected
    //   2 2 2 2 2        2 2 2 2 2      <- P2 home row
    //
    // We only flood-fill from the enemy's home edge, cutting work roughly in half
    // vs checking both players.
    // Skip the expensive flood-fill connectivity check at shallow search
    // depths (lazy evaluation). The flood fill accounts for ~20% of total
    // search time. At depth <= 1, we're about to evaluate the leaf — the
    // slight inaccuracy from not removing disconnected enemy cells is
    // offset by the speed gain (more nodes searched at deeper levels).
    if (mTookEnemyCell && mSearchDepthRemaining > 1) {
      const char enemy = get_enemy(player_to_move);

      const BitBoard& enemyBits = (enemy == PLAYER_1) ? mPlayer1Bits : mPlayer2Bits;
      const BitBoard& edgeMask = (enemy == PLAYER_1) ? sRow0Mask : sRow12Mask;
      BitBoard homeEdge = enemyBits & edgeMask;

      // Bitboard flood-fill from the enemy home edge.
#ifdef WORDBASE_USE_SIMD
      BitBoard connected = bitboard_flood_fill_simd(homeEdge, enemyBits);
#else
      BitBoard connected = bitboard_flood_fill(homeEdge, enemyBits);
#endif

      // Remove disconnected enemy cells (setCellState also updates bitboards).
      BitBoard disconnected = enemyBits & (~connected);
      disconnected.for_each_bit([&](int pos) {
        setCellState(pos / kBoardWidth, pos % kBoardWidth, PLAYER_UNOWNED);
      });
    }

    // Change to the new player.
    setPlayerToMove(get_enemy(player_to_move));
  }

  std::ostream &to_stream(std::ostream &os) const override {
    return os;
  }

  bool operator==(const WordBaseState &other) const override {
    return player_to_move == other.player_to_move && mState == other.mState && mPlayedWords == other.mPlayedWords;
  }

  size_t hash() const override {
    return mHashValue;
  }

  uint64_t tt_verification_key() const override {
    return mTtVerificationKey;
  }

  const std::vector<std::string> getAlreadyPlayed() const {
    std::vector<std::string> alreadyPlayed;

    for (int curId = 0; curId < mPlayedWords.size(); curId++) {
      if (mPlayedWords[curId]) {
        alreadyPlayed.push_back(mBoard->getLegalWord(curId).mWord);
      }
    }

    return alreadyPlayed;
  }

  // Add words to the already played this; this is used for testing
  // or for joining games already in progress.
  void addAlreadyPlayed(const std::string& alreadyPlayed) {
    for (int curId = 0; curId < mPlayedWords.size(); curId++) {
      if (mBoard->getLegalWord(curId).mWord.compare(alreadyPlayed) == 0) {
        setPlayedWord(curId, true);
      }
    }
  }
};

// Static member definitions for precomputed lookup tables.
size_t   WordBaseState::sCellHashTable[kGridCells][WordBaseState::kMaxOwner];
uint64_t WordBaseState::sCellVerifyTable[kGridCells][WordBaseState::kMaxOwner];
int      WordBaseState::sGoodnessTable[kBoardHeight][WordBaseState::kMaxOwner];
bool     WordBaseState::sTablesInitialized = false;

// Print out a move sequence.
std::ostream& operator<<(std::ostream& os, const CoordinateList& foo) {
  bool first = true;
  for (auto&& sequenceElement : foo) {
    if (!first) {
      os << ",";
    }

    os << "(" << sequenceElement.first << "," << sequenceElement.second << ")";
    first = false;
  }

  return os;
}

// Print out a static board.
std::ostream& operator<<(std::ostream& os, const BoardStatic & foo) {
  for (int y = 0; y < kBoardHeight; y++) {
    for (int x = 0; x < kBoardWidth; x++) {
      os << foo.mGrid[y * kBoardWidth + x];
    }
    os << std::endl;
  }
  return os;
}

// Return a character representing the owned state of a square.
char ownerText(char owner) {
  char ownerText;

  switch (owner) {
    case PLAYER_1:
    case PLAYER_2:
      ownerText = '.';
      break;
    case PLAYER_BOMB:
      ownerText = '*';
      break;
    case PLAYER_MEGABOMB:
      ownerText = '+';
      break;
    case PLAYER_UNOWNED:
      ownerText = ' ';
      break;
    default:
      ownerText = '?';
  }

  return ownerText;
}

// Print out the a WordBaseState.
std::ostream& operator<<(std::ostream& os, const WordBaseState& foo) {
  os << boost::format("player(%d): h=%d") % int(foo.player_to_move) % foo.get_goodness() << std::endl;

  os << "  ";
  for (int x = 0; x < kBoardWidth; x++) {
    os << boost::format("%2d") % x;
  }

  os << std::endl;

  for (int y = 0; y < kBoardHeight; y++) {
    os << boost::format("%2d") % y;
    for (int x = 0; x < kBoardWidth; x++) {
      char owner = foo.mState.get(y, x);
      char letter = foo.mBoard->mGrid[y * kBoardWidth + x];
      if (owner == PLAYER_1) {
        letter = std::toupper(letter);
      }
      os << boost::format("%c%c") % ownerText(owner) % letter;
    }
    os << std::endl;
  }

  return os;
}
