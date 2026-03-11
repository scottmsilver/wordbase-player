/*
 Game Tree Search Algorithms
 Copyright (C) 2015-2016  Adam Stelmaszczyk <stelmaszczyk.adam@gmail.com>

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

#include <boost/math/distributions/binomial.hpp>
#include <cstring>
#include <unordered_map>
#include <functional>
#include <sys/time.h>
#include <algorithm>
#include <iostream>
#include <assert.h>
#include <sstream>
#include <iomanip>
#include <memory>
#include <vector>
#include <random>
#include <cstdint>
#include "easylogging++.h"


static const int MAX_SIMULATIONS = 10000000;
static const double UCT_C = sqrt(2);
static const double WIN_SCORE = 1;
static const double DRAW_SCORE = 0.5;
static const double LOSE_SCORE = 0;

static const int MAX_DEPTH = 20;
static const int INF = 2147483647;

struct Random {
  std::mt19937 engine;

  virtual ~Random() {}

  int uniform(int min, int max) {
    return std::uniform_int_distribution<int>{min, max}(engine);
  }
};

struct Timer {
  double start_time;

  virtual ~Timer() {}

  void start() {
    start_time = get_time();
  }

  double get_time() const {
    timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + tv.tv_usec * 1e-6;
  }

  double seconds_elapsed() const {
    return get_time() - start_time;
  }

  bool exceeded(double seconds) const {
    return seconds_elapsed() > seconds;
  }

  friend std::ostream &operator<<(std::ostream &os, const Timer &timer) {
    return os << std::setprecision(2) << std::fixed << timer.seconds_elapsed() << "s";
  }
};

template<class M>
// Move is used via CRTP (Move<WordBaseMove>), never through base pointers.
// All methods are non-virtual to eliminate vtable overhead:
//   - No vtable pointer per object (saves 8 bytes per move)
//   - No indirect dispatch for destructor/hash/operator==
//   - Compiler can inline trivial destructor (was 2% of profile as virtual)
//
// Derived classes (e.g. WordBaseMove) define read(), to_stream(),
// operator==(), and hash() directly — no virtual dispatch needed since
// the template parameter M is always the concrete type at compile time.
struct Move {
  friend std::ostream &operator<<(std::ostream &os, const Move &move) {
    return static_cast<const M&>(move).to_stream(os);
  }
};

enum TTEntryType { EXACT_VALUE, LOWER_BOUND, UPPER_BOUND };

// Bit-packed TT entry: exactly 8 bytes (64 bits).
// 8 entries per 64-byte cache line (was ~2 at 32 bytes original).
//
// Bit layout:
//   [63:32] verification_key  32 bits — truncated position hash
//   [31:19] move              13 bits — LegalWordId (max 8191)
//   [18:7]  value             12 bits — signed eval (±2047, ±INF → ±2000)
//   [6:2]   depth              5 bits — search depth (max 31)
//   [1:0]   value_type         2 bits — EXACT/LOWER/UPPER
//
// Note: tightly coupled to WordBaseMove (accesses mLegalWordId directly).
template<class M>
struct TTEntry {
  uint64_t data;

  static constexpr int VALUE_INF = 2000;  // sentinel for ±INF in 12-bit field

  TTEntry() : data(0) {}

  // moveId must be < 8192, depth must be <= 31. Values outside range are
  // silently masked. Non-terminal eval values must be in [-1999, 1999];
  // values near ±INF are mapped to the ±2000 sentinel.
  TTEntry(const M &move, uint64_t vkey, int depth, int value, TTEntryType vtype)
    : data(pack(static_cast<uint32_t>(vkey), move.mLegalWordId, value, depth, vtype)) {}

  M get_move() const {
    M m;
    m.mLegalWordId = static_cast<int>((data >> 19) & 0x1FFF);
    return m;
  }

  int get_depth() const {
    return static_cast<int>((data >> 2) & 0x1F);
  }

  int get_value() const {
    int raw = static_cast<int>((data >> 7) & 0xFFF);
    if (raw & 0x800) raw |= ~0xFFF;  // sign-extend 12 bits
    if (raw >= VALUE_INF) return INF;
    if (raw <= -VALUE_INF) return -INF;
    return raw;
  }

  TTEntryType get_value_type() const {
    return static_cast<TTEntryType>(data & 0x3);
  }

  // Compare stored verification key (upper 32 bits of data) against
  // the lower 32 bits of the full 64-bit position key.
  bool matches(uint64_t full_key) const {
    return (data >> 32) == (full_key & 0xFFFFFFFF);
  }

  std::ostream &to_stream(std::ostream &os) const {
    return os << "move: " << get_move() << " verification_key: " << (data >> 32)
              << " depth: " << get_depth() << " value: " << get_value()
              << " value_type: " << get_value_type();
  }

  friend std::ostream &operator<<(std::ostream &os, const TTEntry &entry) {
    return entry.to_stream(os);
  }

private:
  static uint64_t pack(uint32_t vkey, int moveId, int value, int depth, TTEntryType vtype) {
    int pv = packValue(value);
    return (static_cast<uint64_t>(vkey) << 32)
         | (static_cast<uint64_t>(moveId & 0x1FFF) << 19)
         | (static_cast<uint64_t>(pv & 0xFFF) << 7)
         | (static_cast<uint64_t>(depth & 0x1F) << 2)
         | (static_cast<uint64_t>(vtype & 0x3));
  }

  static int packValue(int v) {
    if (v >= INF - 1000) return VALUE_INF;
    if (v <= -INF + 1000) return -VALUE_INF;
    return v;
  }
};


template<class S, class M>
struct State {
  unsigned visits = 0;
  double score = 0;
  char player_to_move = 0;
  S *parent = nullptr;
  std::unordered_map<size_t, std::shared_ptr<S>> children = std::unordered_map<size_t, std::shared_ptr<S>>();

  State(char player_to_move) : player_to_move(player_to_move) {}

  virtual ~State() {}

  void update_stats(double result) {
    score += result;
    ++visits;
  }

  double get_uct(double c) const {
    assert(visits > 0);
    double parent_visits = 0.0;
    if (parent != nullptr) {
      parent_visits = parent->visits;
    }
    return (score / visits) + c * sqrt(log(parent_visits) / visits);
  }

  std::shared_ptr<S> create_child(M &move) {
    S child = clone();
    child.make_move(move);
    child.parent = (S*) this;
    return std::make_shared<S>(child);
  }

  S* add_child(M &move) {
    auto child = create_child(move);
    auto key = move.hash();
    auto pair = children.insert({key, child});
    auto it = pair.first;
    return it->second.get();
  }

  S* get_child(M &move) {
    auto key = move.hash();
    auto it = children.find(key);
    if (it == children.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  virtual void swap_players() {}

  virtual S clone() const = 0;

  virtual int get_goodness() const = 0;

  virtual std::vector<M> get_legal_moves(int max_moves) const = 0;

  // Fill a caller-provided vector with legal moves, reusing its capacity.
  // Default implementation delegates to get_legal_moves; subclasses can override.
  virtual void fill_legal_moves(std::vector<M>& out, int max_moves) const {
    out = get_legal_moves(max_moves);
  }

  virtual char get_enemy(char player) const = 0;

  virtual bool is_terminal() const = 0;

  virtual bool is_winner(char player) const = 0;

  virtual void make_move(const M &move) = 0;

  // Lightweight snapshot/restore for search undo. Override in game-specific
  // subclasses to avoid copying large fields (e.g., played-word bitsets).
  // Default: full state copy (same as StateUndoer).
  int mSearchDepthRemaining = 99;
  struct DefaultSnapshot { S savedState; };
  DefaultSnapshot takeSnapshot(const M&) const {
    return {*static_cast<const S*>(this)};
  }
  void restoreSnapshot(const DefaultSnapshot& snap) {
    *static_cast<S*>(this) = snap.savedState;
  }
  void undoPlayedWords(const M&) {} // no-op for default

  // Check whether a move is valid for the current player.
  // Used to validate transposition table entries against hash collisions.
  // Default returns true; override in game-specific subclasses.
  virtual bool isValidMove(const M &) const { return true; }

  virtual std::ostream &to_stream(std::ostream &os) const = 0;

  friend std::ostream &operator<<(std::ostream &os, const State &state) {
    return state.to_stream(os);
  }

  virtual bool operator==(const S &other) const = 0;

  virtual size_t hash() const = 0;

  virtual uint64_t tt_verification_key() const {
    return static_cast<uint64_t>(hash());
  }
};

template<class S, class M>
struct Algorithm {
  Algorithm() { }

  Algorithm(const Algorithm& algorithm) {}

  virtual ~Algorithm() {}

  virtual void reset() {}

  virtual M get_move(S *state) = 0;

  virtual std::string read_log() const {
    return "";
  }

  virtual std::string get_name() const = 0;

  friend std::ostream &operator<<(std::ostream &os, const Algorithm &algorithm) {
    os << algorithm.get_name();
    return os;
  }
};

template<class S, class M>
struct Human : public Algorithm<S, M> {
  Human() : Algorithm<S, M>() {}

  M get_move(S *state) override {
    const std::vector<M> &legal_moves = state->get_legal_moves();
    if (legal_moves.empty()) {
      std::stringstream stream;
      state->to_stream(stream);
      throw std::invalid_argument("Given state is terminal:\n" + stream.str());
    }
    while (true) {
      M move = M();
      move.read();
      if (find(legal_moves.begin(), legal_moves.end(), move) != legal_moves.end()) {
        return move;
      } else {
        std::cout << "Move " << move << " is not legal" << std::endl;
      }
    }
  }

  std::string get_name() const {
    return "Human";
  }
};

template<class M>
struct MinimaxResult {
  int goodness;
  M best_move;
  bool completed;
};


class IndentingOStreambuf : public std::streambuf
{
  std::streambuf*     myDest;
  bool                myIsAtStartOfLine;
  std::string         myIndent;
  std::ostream*       myOwner;
protected:
  virtual int         overflow( int ch )
  {
    if ( myIsAtStartOfLine && ch != '\n' ) {
      myDest->sputn( myIndent.data(), myIndent.size() );
    }
    myIsAtStartOfLine = ch == '\n';
    return myDest->sputc( ch );
  }
public:
  explicit            IndentingOStreambuf(
                                          std::streambuf* dest, int indent = 4 )
  : myDest( dest )
  , myIsAtStartOfLine( true )
  , myIndent( indent, '.' )
  , myOwner( NULL )
  {
  }
  explicit            IndentingOStreambuf(
                                          std::ostream& dest, int indent = 4 )
  : myDest( dest.rdbuf() )
  , myIsAtStartOfLine( true )
  , myIndent( indent, '.' )
  , myOwner( &dest )
  {
    myOwner->rdbuf( this );
  }
  virtual             ~IndentingOStreambuf()
  {
    if ( myOwner != NULL ) {
      myOwner->rdbuf( myDest );
    }
  }
};

template<class S, class M>
class StateUndoer {
private:
  S mSavedState;
  S& mStateToUndo;

public:
  // Keep undo generic unless a measured hotspot justifies per-state specialization.
  StateUndoer(S& state, const M&) : mSavedState(state), mStateToUndo(state) {}
  ~StateUndoer() { mStateToUndo = mSavedState; }
};

template<class S, class M>
struct Minimax : public Algorithm<S, M> {
  struct SearchStats {
    bool completed = false;
    int goodness = 0;
    int nodes = 0;
    int leafs = 0;
    int beta_cuts = 0;
    int cut_bf_sum = 0;
    int tt_hits = 0;
    int tt_exacts = 0;
    int tt_cuts = 0;
    int tt_size = 0;
    int max_depth = 0;
    double elapsed_seconds = 0.0;
    double nodes_per_second = 0.0;
    M best_move;

    double average_cut_branching_factor() const {
      return beta_cuts == 0 ? 0.0 : static_cast<double>(cut_bf_sum) / beta_cuts;
    }
  };

  // --- Flat (open-addressing) transposition table ---
  //
  // Replaces std::unordered_map which allocates a heap node per entry.
  // With unordered_map, each of the ~225K TT insertions per search turn
  // triggers a malloc, and when entries are evicted, a corresponding free.
  // This caused ~7% of total runtime in malloc_consolidate/unlink_chunk.
  //
  // The flat table uses a fixed-size power-of-2 array with direct hash
  // indexing: slot = hash & mask. Collisions use "always replace" policy
  // (standard in chess engines). Benefits:
  //   - Zero heap allocations during search
  //   - Single array access instead of hash bucket → linked list traversal
  //   - Better cache locality (entries are contiguous in memory)
  //
  // Collision detection uses the verification_key stored in TTEntry
  // (32-bit truncation of a 64-bit hash, independent from the index hash).
  // Empty slots have verification_key == 0 (sentinel). The probability of
  // a real position having verification_key == 0 is 2^-32, negligible.
  //
  // Table size: 2^18 = 256K entries × 8 bytes = 2 MB (fits in L3 cache).
  static constexpr size_t DEFAULT_TT_SIZE_BITS = 18;
  static constexpr size_t TT_SIZE_BITS = DEFAULT_TT_SIZE_BITS;  // legacy alias
  static constexpr size_t TT_SIZE = 1ULL << DEFAULT_TT_SIZE_BITS;
  static constexpr size_t TT_MASK = TT_SIZE - 1;

  size_t mTTSizeBits;
  size_t mTTSize;
  size_t mTTMask;
  std::vector<TTEntry<M>> flat_tt;
  // External TT for shared-TT parallel modes (Lazy SMP, YBWC).
  // When non-null, TT operations use this instead of flat_tt.
  TTEntry<M>* mSharedTTPtr = nullptr;

  double MAX_SECONDS;
  const int MAX_MOVES;
  std::function<int(S*)> get_goodness;
  Timer timer;
  int beta_cuts, cut_bf_sum;
  int tt_hits, tt_exacts, tt_cuts;
  int nodes, leafs;
  int mMaxDepth;
  bool mUseTranspositionTable;
  std::ostream* mTraceStream;
  SearchStats mLastSearchStats;
  std::vector<M> mCachedRootMoves;
  bool mHasCachedRootMoves = false;

  // Killer moves: two moves per ply that caused beta cutoffs.
  // Killer moves are tried right after the TT move, before heuristic ordering.
  static constexpr int MAX_PLY = 64;
  static constexpr int NUM_KILLERS = 2;
  M mKillers[MAX_PLY][NUM_KILLERS];
  bool mKillerValid[MAX_PLY][NUM_KILLERS];

  // History heuristic: tracks how often each LegalWordId causes cutoffs.
  // Used as a secondary sort key when no TT or killer move is available.
  // Indexed by LegalWordId. Capped at reasonable size.
  static constexpr int HISTORY_TABLE_SIZE = 65536;
  int mHistory[HISTORY_TABLE_SIZE];

  // Countermove heuristic: when opponent plays move X, move Y is a good response.
  // Indexed by the previous move's LegalWordId.
  int mCountermove[HISTORY_TABLE_SIZE];  // stores LegalWordId of best response
  bool mCountermoveValid[HISTORY_TABLE_SIZE];

  // Per-instance move buffers for each ply (replaces function-local static
  // depth_move_buffers[], which was not thread-safe).
  std::vector<M> mDepthMoveBuffers[MAX_PLY];
  // When true, mCachedRootMoves was set externally and should not be cleared.
  bool mRootMovesLocked = false;

  struct RootMoveScore {
    M move;
    int score;
  };
  std::vector<RootMoveScore> mCurrentRootScores;
  std::vector<RootMoveScore> mLastCompletedRootScores;

  Minimax(double max_seconds = 10, int max_moves = INF, std::function<int(S*)> get_goodness = nullptr) :
  Algorithm<S, M>(),
  mTTSizeBits(DEFAULT_TT_SIZE_BITS),
  mTTSize(1ULL << DEFAULT_TT_SIZE_BITS),
  mTTMask((1ULL << DEFAULT_TT_SIZE_BITS) - 1),
  flat_tt(1ULL << DEFAULT_TT_SIZE_BITS),
	  MAX_SECONDS(max_seconds),
	  MAX_MOVES(max_moves),
	  get_goodness(get_goodness),
	  timer(Timer()),
	  mMaxDepth(MAX_DEPTH), mUseTranspositionTable(true), mTraceStream(&std::cout), mLastSearchStats() {
    memset(mKillerValid, 0, sizeof(mKillerValid));
    memset(mHistory, 0, sizeof(mHistory));
    memset(mCountermoveValid, 0, sizeof(mCountermoveValid));
  }

  void reset() override {
    if (!mSharedTTPtr) {
      memset(flat_tt.data(), 0, flat_tt.size() * sizeof(TTEntry<M>));
    }
  }

  void setMaxSeconds(double seconds) {
    MAX_SECONDS = seconds;
  }

  void setMaxDepth(int depth) {
    mMaxDepth = depth;
  }

  void setTTSizeBits(size_t bits) {
    mTTSizeBits = bits;
    mTTSize = 1ULL << bits;
    mTTMask = mTTSize - 1;
    if (!mSharedTTPtr) {
      flat_tt.assign(mTTSize, TTEntry<M>{});
    }
  }

  void setUseTranspositionTable(bool useTranspositionTable) {
    mUseTranspositionTable = useTranspositionTable;
  }

  void setTraceStream(std::ostream* traceStream) {
    mTraceStream = traceStream;
  }

  // Use a shared transposition table (for Lazy SMP / YBWC parallel modes).
  // Frees the local flat_tt to save memory.
  void setSharedTT(TTEntry<M>* ptr) {
    mSharedTTPtr = ptr;
    if (ptr) {
      flat_tt.clear();
      flat_tt.shrink_to_fit();
    }
  }

  // Override root moves (for parallel search: each thread gets a subset).
  void setRootMoves(const std::vector<M>& moves) {
    mCachedRootMoves = moves;
    mHasCachedRootMoves = true;
    mRootMovesLocked = true;
  }

  const SearchStats& getLastSearchStats() const {
    return mLastSearchStats;
  }

  const std::vector<RootMoveScore>& getLastRootScores() const { return mLastCompletedRootScores; }

  M get_move(S *state) override {
    if (state->is_terminal()) {
      std::stringstream stream;
      state->to_stream(stream);
      throw std::invalid_argument("Given state is terminal:\n" + stream.str());
    }
    timer.start();
    mLastSearchStats = SearchStats();
    // Clear killer moves for this search (but keep history — it persists across ID iterations).
    memset(mKillerValid, 0, sizeof(mKillerValid));
    // Age the history table: halve all values to prevent overflow and
    // let recent iterations dominate.
    for (int i = 0; i < HISTORY_TABLE_SIZE; ++i) mHistory[i] >>= 1;
    if (!mRootMovesLocked) {
      mHasCachedRootMoves = false;
    }
    M best_move;
    mLastCompletedRootScores.clear();
    for (int max_depth = 1; max_depth <= mMaxDepth; ++max_depth) {
      mCurrentRootScores.clear();
      LOG(DEBUG) << " { ---------------------d(" << max_depth << ")------------------------------------" << std::endl;
      beta_cuts = 0;
      cut_bf_sum = 0;
      tt_hits = 0;
      tt_exacts = 0;
      tt_cuts = 0;
      nodes = 0;
      leafs = 0;
      LOG(DEBUG) << *state << std::endl;

      auto result = minimax(state, max_depth, -INF, INF, 0);
      if (result.completed) {
        best_move = result.best_move;
        mLastCompletedRootScores = mCurrentRootScores;
        mLastSearchStats.completed = true;
        mLastSearchStats.goodness = result.goodness;
        mLastSearchStats.nodes = nodes;
        mLastSearchStats.leafs = leafs;
        mLastSearchStats.beta_cuts = beta_cuts;
        mLastSearchStats.cut_bf_sum = cut_bf_sum;
        mLastSearchStats.tt_hits = tt_hits;
        mLastSearchStats.tt_exacts = tt_exacts;
        mLastSearchStats.tt_cuts = tt_cuts;
        mLastSearchStats.tt_size = mTTSize;
        mLastSearchStats.max_depth = max_depth;
        mLastSearchStats.elapsed_seconds = timer.seconds_elapsed();
        mLastSearchStats.nodes_per_second = mLastSearchStats.elapsed_seconds == 0.0 ? 0.0 : nodes / mLastSearchStats.elapsed_seconds;
        mLastSearchStats.best_move = best_move;
        if (mTraceStream != nullptr) {
          *mTraceStream << "goodness: " << result.goodness
            << " time: " << timer
            << " move: " << best_move
            << " nodes: " << nodes
            << " leafs: " << leafs
            << " beta_cuts: " << beta_cuts
            << " cutBF: " << mLastSearchStats.average_cut_branching_factor()
            << " tt_hits: " << tt_hits
            << " tt_exacts: " << tt_exacts
            << " tt_cuts: " << tt_cuts
            << " tt_size: " << TT_SIZE
            << " max_depth: " << max_depth << std::endl;
        }
      }
      LOG(DEBUG) << " } ---------------------d(" << max_depth << ")------------------------------------" << std::endl;
      if (timer.exceeded(MAX_SECONDS)) {
        break;
      }
      if (mTraceStream != nullptr) {
        *mTraceStream << (double) nodes / timer.seconds_elapsed() << " nodes/s" << std::endl;
      }
    }
    return best_move;
  }

  // Find Minimax value of the given tree,
  // Minimax value lies within a range of [alpha; beta] window.
  // Whenever alpha >= beta, further checks of children in a node can be pruned.
  MinimaxResult<M> minimax(S *state, int depth, int alpha, int beta, int indent, int prevMoveId = -1) {
    ++nodes;
    const int alpha_original = alpha;

    M best_move;
    if (depth == 0 || state->is_terminal()) {
      ++leafs;
      const int goodness = get_goodness ? get_goodness(state) : state->get_goodness();
      return {goodness, best_move, false};
    }

    TTEntry<M> entry;
    bool entry_found = get_tt_entry(state, entry);
    // Validate that the stored move is still legal for this position.
    // Hash/verification collisions or cross-turn TT reuse can produce
    // entries whose move is invalid for the current state.
    // If isValidMove becomes a bottleneck, we could restrict this check
    // to EXACT_VALUE entries only (those skip the search entirely).
    // Bounds entries with wrong values just cause suboptimal pruning.
    if (mUseTranspositionTable && entry_found && entry.get_depth() >= depth
        && state->isValidMove(entry.get_move())) {
      ++tt_hits;
      if (entry.get_value_type() == TTEntryType::EXACT_VALUE) {
        ++tt_exacts;
        return {entry.get_value(), entry.get_move(), true};
      }
      if (entry.get_value_type() == TTEntryType::LOWER_BOUND && alpha < entry.get_value()) {
        alpha = entry.get_value();
      }
      if (entry.get_value_type() == TTEntryType::UPPER_BOUND && beta > entry.get_value()) {
        beta = entry.get_value();
      }
      if (alpha >= beta) {
        ++tt_cuts;
        return {entry.get_value(), entry.get_move(), true};
      }
    }

    // Internal Iterative Deepening: if no TT move exists at a node
    // with enough remaining depth, do a shallow search to find a good
    // move to try first (which also populates the TT for PVS).
    if (indent > 0 && depth >= 4 && !entry_found) {
      minimax(state, depth - 2, alpha, beta, indent, prevMoveId);
      entry_found = get_tt_entry(state, entry);
    }

    // Static eval for pruning decisions (computed once, shared).
    static constexpr int FUTILITY_MARGIN_D1 = 200;
    static constexpr int FUTILITY_MARGIN_D2 = 500;
    static constexpr int RFP_MARGIN_D1 = 100;
    static constexpr int RFP_MARGIN_D2 = 300;
    bool canFutilityPrune = false;
    if (indent > 0 && depth <= 2 && alpha > -INF + 1000 && beta < INF - 1000) {
      int staticEval = get_goodness ? get_goodness(state) : state->get_goodness();
      // Reverse futility pruning: if eval is far above beta, prune
      // the entire node — no move can make it worse enough.
      int rfpMargin = (depth == 1) ? RFP_MARGIN_D1 : RFP_MARGIN_D2;
      if (staticEval - rfpMargin >= beta) {
        return {staticEval, best_move, true};
      }
      // Forward futility pruning: if eval is far below alpha, skip
      // late moves (but always search the first few + staged moves).
      int margin = (depth == 1) ? FUTILITY_MARGIN_D1 : FUTILITY_MARGIN_D2;
      canFutilityPrune = (staticEval + margin <= alpha);
    }

    int max_goodness = -INF;
    bool completed = true;
    bool found_best_move = false;

    // Track move IDs searched in early stages (to skip duplicates later).
    int staged_ids[5];
    int num_staged = 0;
    int moves_searched = 0;
    bool search_stopped = false;

    // Common logic for searching one move. Uses lightweight snapshot
    // (excludes mPlayedWords ~1KB) instead of full state copy.
    // reduction: number of plies to reduce depth by (0 = full search).
    auto searchMove = [&](const M& move, int reduction = 0) {
      auto snap = state->takeSnapshot(move);
      state->mSearchDepthRemaining = depth;
      state->make_move(move);

      int goodness;
      // PVS: first move gets full window; subsequent moves get null window.
      if (moves_searched == 0) {
	goodness = -minimax(state, depth - 1, -beta, -alpha,
			    indent + 1, move.mLegalWordId).goodness;
      } else {
	// Null window search (with optional LMR reduction).
	goodness = -minimax(state, depth - 1 - reduction, -alpha - 1, -alpha,
			    indent + 1, move.mLegalWordId).goodness;
	// Re-search at full depth+window if the null/reduced search
	// found a score inside (alpha, beta) — it may be better than
	// we thought but we need an accurate score.
	if (goodness > alpha && goodness < beta) {
	  goodness = -minimax(state, depth - 1, -beta, -alpha,
			      indent + 1, move.mLegalWordId).goodness;
	}
      }

      ++moves_searched;
      VLOG(9) << *state << std::endl;

      if (indent == 0) {
	mCurrentRootScores.push_back({move, goodness});
      }

      if ((nodes & 4095) == 0 && timer.exceeded(MAX_SECONDS)) {
	completed = false;
	search_stopped = true;
      } else if (goodness > max_goodness) {
	max_goodness = goodness;
	best_move = move;
	found_best_move = true;
	VLOG(9) << "choosing --> h(" << goodness << ")" << best_move << std::endl;
	if (max_goodness >= beta) {
	  ++beta_cuts;
	  cut_bf_sum += moves_searched;
	  if (indent < MAX_PLY) {
	    if (!mKillerValid[indent][0] ||
		mKillers[indent][0].mLegalWordId != move.mLegalWordId) {
	      mKillers[indent][1] = mKillers[indent][0];
	      mKillerValid[indent][1] = mKillerValid[indent][0];
	      mKillers[indent][0] = move;
	      mKillerValid[indent][0] = true;
	    }
	  }
	  if (prevMoveId >= 0 && prevMoveId < HISTORY_TABLE_SIZE) {
	    mCountermove[prevMoveId] = move.mLegalWordId;
	    mCountermoveValid[prevMoveId] = true;
	  }
	  int id = move.mLegalWordId;
	  if (id >= 0 && id < HISTORY_TABLE_SIZE) {
	    int bonus = depth * depth;
	    int& e = mHistory[id];
	    e += bonus - e * bonus / 16384;
	  }
	  search_stopped = true;
	}
      }

      // Restore state: snapshot clears changed played-word bits, then restores
      // grid, hash, bitboards, player, etc.
      state->restoreSnapshot(snap);
      if (!search_stopped && alpha < max_goodness) alpha = max_goodness;
    };

    // --- Staged move generation (non-root nodes only) ---
    // Try TT move, killers, and countermove BEFORE generating the full
    // move list. This avoids the expensive fill_legal_moves call (~37%
    // of runtime) at nodes where a privileged move causes a cutoff.
    if (indent > 0) {
      // Stage 1: TT move.
      if (entry_found && state->isValidMove(entry.get_move())) {
	staged_ids[num_staged++] = entry.get_move().mLegalWordId;
	searchMove(entry.get_move());
      }

      // Stage 2: Killer moves.
      for (int k = 0; k < NUM_KILLERS && !search_stopped && indent < MAX_PLY; k++) {
	if (mKillerValid[indent][k]) {
	  int kid = mKillers[indent][k].mLegalWordId;
	  bool dup = false;
	  for (int j = 0; j < num_staged; j++)
	    if (staged_ids[j] == kid) { dup = true; break; }
	  if (!dup && state->isValidMove(mKillers[indent][k])) {
	    staged_ids[num_staged++] = kid;
	    searchMove(mKillers[indent][k]);
	  }
	}
      }

      // Stage 3: Countermove.
      if (!search_stopped && prevMoveId >= 0 && prevMoveId < HISTORY_TABLE_SIZE
	  && mCountermoveValid[prevMoveId]) {
	int cmId = mCountermove[prevMoveId];
	bool dup = false;
	for (int j = 0; j < num_staged; j++)
	  if (staged_ids[j] == cmId) { dup = true; break; }
	if (!dup) {
	  M cmMove(cmId);
	  if (state->isValidMove(cmMove)) {
	    staged_ids[num_staged++] = cmId;
	    searchMove(cmMove);
	  }
	}
      }
    }

    // Stage 4: Generate all remaining moves (skipped if a staged move caused cutoff).
    if (!search_stopped) {
      std::vector<M>& legal_moves = (indent == 0 && mHasCachedRootMoves)
	? mCachedRootMoves
	: mDepthMoveBuffers[indent];
      if (!(indent == 0 && mHasCachedRootMoves)) {
	state->fill_legal_moves(legal_moves, MAX_MOVES);
      }

      // At root: promote TT/killers/CM to front (no staging at root).
      if (indent == 0 && !legal_moves.empty()) {
	int front = 0;
	if (mUseTranspositionTable && entry_found && state->isValidMove(entry.get_move())) {
	  for (int i = front; i < static_cast<int>(legal_moves.size()); i++) {
	    if (legal_moves[i].mLegalWordId == entry.get_move().mLegalWordId) {
	      std::swap(legal_moves[front], legal_moves[i]);
	      front++;
	      break;
	    }
	  }
	}
	for (int k = 0; k < NUM_KILLERS && indent < MAX_PLY; k++) {
	  if (mKillerValid[indent][k]) {
	    int killerId = mKillers[indent][k].mLegalWordId;
	    for (int i = front; i < static_cast<int>(legal_moves.size()); i++) {
	      if (legal_moves[i].mLegalWordId == killerId) {
		std::swap(legal_moves[front], legal_moves[i]);
		front++;
		break;
	      }
	    }
	  }
	}
	if (prevMoveId >= 0 && prevMoveId < HISTORY_TABLE_SIZE
	    && mCountermoveValid[prevMoveId]) {
	  int cmId = mCountermove[prevMoveId];
	  for (int i = front; i < static_cast<int>(legal_moves.size()); i++) {
	    if (legal_moves[i].mLegalWordId == cmId) {
	      std::swap(legal_moves[front], legal_moves[i]);
	      front++;
	      break;
	    }
	  }
	}
	if (depth >= 3 && front < static_cast<int>(legal_moves.size())) {
	  const int* hist = mHistory;
	  std::stable_sort(legal_moves.begin() + front, legal_moves.end(),
	    [hist](const M& a, const M& b) {
	      int ha = (a.mLegalWordId >= 0 && a.mLegalWordId < HISTORY_TABLE_SIZE)
		       ? hist[a.mLegalWordId] : 0;
	      int hb = (b.mLegalWordId >= 0 && b.mLegalWordId < HISTORY_TABLE_SIZE)
		       ? hist[b.mLegalWordId] : 0;
	      return ha > hb;
	    });
	}
      }

      // At non-root: sort by history (staged moves will be skipped).
      if (indent > 0 && depth >= 3 && !legal_moves.empty()) {
	const int* hist = mHistory;
	std::stable_sort(legal_moves.begin(), legal_moves.end(),
	  [hist](const M& a, const M& b) {
	    int ha = (a.mLegalWordId >= 0 && a.mLegalWordId < HISTORY_TABLE_SIZE)
		     ? hist[a.mLegalWordId] : 0;
	    int hb = (b.mLegalWordId >= 0 && b.mLegalWordId < HISTORY_TABLE_SIZE)
		     ? hist[b.mLegalWordId] : 0;
	    return ha > hb;
	  });
      }

      for (const auto& move : legal_moves) {
	// Skip moves already searched in stages 1-3.
	bool dup = false;
	for (int j = 0; j < num_staged; j++) {
	  if (staged_ids[j] == move.mLegalWordId) { dup = true; break; }
	}
	if (dup) continue;

	// Futility pruning: skip late moves at shallow depth when
	// static eval + margin is below alpha. Always search at least
	// 2 non-staged moves before pruning.
	if (canFutilityPrune && moves_searched >= 2) {
	  continue;
	}

	// Late Move Reduction: moves searched after the first few at
	// non-root nodes with sufficient depth are searched at reduced
	// depth first. If the reduced search beats alpha, re-search at
	// full depth (handled inside searchMove).
	int reduction = 0;
	if (indent > 0 && depth >= 3 && moves_searched >= 3) {
	  // Base reduction: 1 ply. Increase for later moves.
	  reduction = 1;
	  if (moves_searched >= 8) reduction = 2;
	  if (moves_searched >= 20 && depth >= 5) reduction = 3;
	  // Don't reduce below depth 1.
	  if (reduction >= depth - 1) reduction = depth - 2;
	  if (reduction < 0) reduction = 0;
	}
	searchMove(move, reduction);
	if (search_stopped) break;
      }

      if (!found_best_move && !legal_moves.empty()) {
	best_move = legal_moves[random.uniform(0, legal_moves.size() - 1)];
      }
    }

    if (mUseTranspositionTable && completed) {
      update_tt(state, alpha_original, beta, max_goodness, best_move, depth);
    }
    return {max_goodness, best_move, completed};
  }

  Random random;

  // Look up a position in the flat transposition table.
  //
  // Example: state has hash=0xABCD1234, verification_key=0x9876...
  //   slot index = 0xABCD1234 & 0x3FFFF = 0x1234  (lower 18 bits)
  //   flat_tt[0x1234].verification_key == 0x9876... ?
  //     yes → entry found (same position, or astronomically unlikely collision)
  //     no  → miss (slot empty or holds a different position)
  bool get_tt_entry(S *state, TTEntry<M> &entry) {
    auto key = state->hash();
    TTEntry<M>* tt = mSharedTTPtr ? mSharedTTPtr : flat_tt.data();
    auto& slot = tt[key & mTTMask];
    if (!slot.matches(state->tt_verification_key())) {
      return false;
    }
    entry = slot;
    return true;
  }

  // Store a position in the flat TT. "Always replace" policy: if the slot
  // is occupied by a different position, it's silently overwritten.
  void add_tt_entry(S *state, const TTEntry<M> &entry) {
    auto key = state->hash();
    TTEntry<M>* tt = mSharedTTPtr ? mSharedTTPtr : flat_tt.data();
    tt[key & mTTMask] = entry;
  }

  void update_tt(S *state, int alpha, int beta, int max_goodness, M &best_move, int depth) {
    TTEntryType value_type;
    if (max_goodness <= alpha) {
      value_type = TTEntryType::UPPER_BOUND;
    }
    else if (max_goodness >= beta) {
      value_type = TTEntryType::LOWER_BOUND;
    }
    else {
      value_type = TTEntryType::EXACT_VALUE;
    }
    TTEntry<M> entry = {best_move, state->tt_verification_key(), depth, max_goodness, value_type};
    add_tt_entry(state, entry);
  }

  std::string get_name() const override {
    return "Minimax";
  }
};

// https://en.wikipedia.org/wiki/Monte_Carlo_tree_search
template<class S, class M>
struct MonteCarloTreeSearch : public Algorithm<S, M> {
  const double max_seconds;
  const int max_simulations;
  const bool block;
  Random random;

  MonteCarloTreeSearch(double max_seconds = 1,
                       int max_simulations = MAX_SIMULATIONS,
                       bool block = false) :
  Algorithm<S, M>(),
  max_seconds(max_seconds),
  block(block),
  max_simulations(max_simulations) {}

  M get_move(S *root) override {
    if (root->is_terminal()) {
      std::stringstream stream;
      root->to_stream(stream);
      throw std::invalid_argument("Given state is terminal:\n" + stream.str());
    }
    Timer timer;
    timer.start();
    int simulation = 0;
    while (simulation < max_simulations && !timer.exceeded(max_seconds)) {
      monte_carlo_tree_search(root);
      ++simulation;
    }
    LOG(DEBUG) << "ratio: " << root->score / root->visits << std::endl;
    LOG(DEBUG) << "simulations: " << simulation << std::endl;
    auto legal_moves = root->get_legal_moves();
    LOG(DEBUG) << "moves: " << legal_moves.size() << std::endl;
    for (auto move : legal_moves) {
      LOG(DEBUG) << "move: " << move;
      auto child = root->get_child(move);
      if (child != nullptr) {
        LOG(DEBUG) << " score: " << child->score
        << " visits: " << child->visits
        << " UCT: " << child->get_uct(UCT_C);
      }
      LOG(DEBUG) << std::endl;
    }
    return get_most_visited_move(root);
  }

  void monte_carlo_tree_search(S *root) {
    S *current = tree_policy(root, root);
    auto result = rollout(current, root);
    propagate_up(current, result);
  }

  void propagate_up(S *current, double result) {
    current->update_stats(result);
    if (current->parent) {
      propagate_up(current->parent, result);
    }
  }

  S* tree_policy(S *state, S *root) {
    if (state->is_terminal()) {
      return state;
    }
    M move = get_tree_policy_move(state, root);
    auto child = state->get_child(move);
    if (child == nullptr) {
      return state->add_child(move);
    }
    return tree_policy(child, root);
  }

  M get_most_visited_move(S *state) {
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    M best_move;
    double max_visits = -INF;
    for (auto move : legal_moves) {
      auto child = state->get_child(move);
      if (child != nullptr) {
        auto visits = child->visits;
        if (max_visits < visits) {
          max_visits = visits;
          best_move = move;
        }
      }
    }
    assert(max_visits != -INF);
    return best_move;
  }

  M get_best_move(S *state, S *root) {
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    M best_move;
    if (state->player_to_move == root->player_to_move) {
      // maximize
      double best_uct = -INF;
      for (auto move : legal_moves) {
        auto child = state->get_child(move);
        if (child != nullptr) {
          auto uct = child->get_uct(UCT_C);
          if (best_uct < uct) {
            best_uct = uct;
            best_move = move;
          }
        } else {
          return move;
        }
      }
    }
    else {
      // minimize
      double best_uct = INF;
      for (auto move : legal_moves) {
        auto child = state->get_child(move);
        if (child != nullptr) {
          auto uct = child->get_uct(-UCT_C);
          if (best_uct > uct) {
            best_uct = uct;
            best_move = move;
          }
        } else {
          return move;
        }
      }
    }
    return best_move;
  }

  M get_random_move(const S *state) {
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    int index = random.uniform(0, legal_moves.size() - 1);
    return legal_moves[index];
  }


  std::shared_ptr<M> get_winning_move(S *state) {
    auto current_player = state->player_to_move;
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    for (M &move : legal_moves) {
      StateUndoer<S,M> undoer(*state, move); {
	state->make_move(move);
	if (state->is_winner(current_player)) {
	  return std::make_shared<M>(move);
	}
      }
    }
    return nullptr;
  }

  std::shared_ptr<M> get_blocking_move(S *state) {
    auto current_player = state->player_to_move;
    auto enemy = state->get_enemy(current_player);
    state->player_to_move = enemy;
    auto legal_moves = state->get_legal_moves();
    assert(legal_moves.size() > 0);
    for (M &move : legal_moves) {
      StateUndoer<S,M> undoer(*state, move); {
	state->make_move(move);
	if (state->is_winner(enemy)) {
	  state->player_to_move = current_player;
	  return std::make_shared<M>(move);
	}
      }
    }
    state->player_to_move = current_player;
    return nullptr;
  }

  M get_tree_policy_move(S *state, S *root) {
    // If player has a winning move he makes it.
    auto move_ptr = get_winning_move(state);
    if (move_ptr != nullptr) {
      return *move_ptr;
    }
    if (block) {
      // If player has a blocking move he makes it.
      move_ptr = get_blocking_move(state);
      if (move_ptr != nullptr) {
        return *move_ptr;
      }
    }
    return get_best_move(state, root);
  }

  M get_default_policy_move(S *state) {
    // If player has a winning move he makes it.
    auto move_ptr = get_winning_move(state);
    if (move_ptr != nullptr) {
      return *move_ptr;
    }
    // If player has a blocking move he makes it.
    move_ptr = get_blocking_move(state);
    if (move_ptr != nullptr) {
      return *move_ptr;
    }
    return get_random_move(state);
  }

  double rollout(S *current, S *root) {
    if (current->is_terminal()) {
      if (current->is_winner(root->player_to_move)) {
        return WIN_SCORE;
      }
      if (current->is_winner(root->get_enemy(root->player_to_move))) {
        return LOSE_SCORE;
      }
      return DRAW_SCORE;
    }
    M move = get_default_policy_move(current);
    StateUndoer<S,M> undoer(*current, move); {
      current->make_move(move);
      auto result = rollout(current, root);
      return result;
    }
  }

  std::string get_name() const override {
    return "MonteCarloTreeSearch";
  }

};

template<class S, class M>
struct Tester {
  S *root = nullptr;
  Algorithm<S, M> &algorithm_1;
  Algorithm<S, M> &algorithm_2;
  const int MATCHES;
  const bool VERBOSE;
  const double SIGNIFICANCE_LEVEL = 0.005; // two sided 99% confidence interval

  Tester(S *state, Algorithm<S, M> &algorithm_1, Algorithm<S, M> &algorithm_2, int matches = INF, bool verbose = false) :
  root(state), algorithm_1(algorithm_1), algorithm_2(algorithm_2), MATCHES(matches), VERBOSE(verbose) {}

  virtual ~Tester() {}

  int start() {
    int draws = 0;
    int algorithm_1_wins = 0;
    int algorithm_2_wins = 0;
    char enemy = root->get_enemy(root->player_to_move);
    for (int i = 1; i <= MATCHES; ++i) {
      auto current = root->clone();
      if (i % 4 == 0 || i % 4 == 2) {
        current.player_to_move = current.get_enemy(current.player_to_move);
      }
      if (i % 4 == 0 || i % 4 == 3) {
        current.swap_players();
      }
      if (VERBOSE) {
        LOG(DEBUG) << current << std::endl;
      }
      while (!current.is_terminal()) {
        auto &algorithm = (current.player_to_move == root->player_to_move) ? algorithm_1 : algorithm_2;
        if (VERBOSE) {
          LOG(DEBUG) << current.player_to_move << " " << algorithm << std::endl;
        }
        algorithm.reset();
        Timer timer;
        timer.start();
        auto copy = current.clone();
        auto move = algorithm.get_move(&copy);
        if (VERBOSE) {
          std::cout << algorithm.read_log();
          std::cout << timer << std::endl;
        }
        current.make_move(move);
        if (VERBOSE) {
          std::cout << current << std::endl;
        }
      }
      std::cout << "Match " << i << ": ";
      if (current.is_winner(root->player_to_move)) {
        ++algorithm_1_wins;
        std::cout << root->player_to_move << " " << algorithm_1 << " won" << std::endl;
      } else if (current.is_winner(enemy)) {
        ++algorithm_2_wins;
        std::cout << enemy << " " << algorithm_2 << " won" << std::endl;
      } else {
        ++draws;
        std::cout << "draw" << std::endl;
      }
      std::cout << root->player_to_move << " " << algorithm_1 << " wins: " << algorithm_1_wins << std::endl;
      std::cout << enemy << " " << algorithm_2 << " wins: " << algorithm_2_wins << std::endl;
      std::cout << "Draws: " << draws << std::endl;
      double successes = algorithm_1_wins + 0.5 * draws;
      double ratio = successes / i;
      std::cout << "Ratio: " << ratio << std::endl;
      double lower = boost::math::binomial_distribution<>::find_lower_bound_on_p(i, successes, SIGNIFICANCE_LEVEL);
      double upper = boost::math::binomial_distribution<>::find_upper_bound_on_p(i, successes, SIGNIFICANCE_LEVEL);
      std::cout << "Lower confidence bound: " << lower << std::endl;
      std::cout << "Upper confidence bound: " << upper << std::endl;
      std::cout << std::endl;
      if (upper < 0.5 || lower > 0.5) {
        break;
      }
    }
    return draws;
  }
};
