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

template<class M>
struct TTEntry {
  M move;
  uint64_t verification_key;
  int depth;
  int value;
  TTEntryType value_type;

  // Zero-initialize verification_key so empty flat-TT slots read as 0,
  // which won't match any real position's verification key.
  TTEntry() : verification_key(0) {}

  // Non-virtual: TTEntry is never used polymorphically. Removing virtual
  // eliminates the vtable pointer (8 bytes per entry) and allows the
  // compiler to inline/omit the destructor.
  ~TTEntry() {}

  TTEntry(const M &move, uint64_t verification_key, int depth, int value, TTEntryType value_type) :
  move(move), verification_key(verification_key), depth(depth), value(value), value_type(value_type) {}

 std::ostream &to_stream(std::ostream &os) const {
	    return os << "move: " << move << " verification_key: " << verification_key << " depth: " << depth << " value: " << value << " value_type: " << value_type;
  }

  friend std::ostream &operator<<(std::ostream &os, const TTEntry &entry) {
    return entry.to_stream(os);
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
  // Collision detection uses the verification_key already stored in TTEntry
  // (a 64-bit hash from a different hash function than the index hash).
  // Empty slots have verification_key == 0 (sentinel). The probability of
  // a real position having verification_key == 0 is 2^-64, negligible.
  //
  // Table size: 2^18 = 256K entries × ~28 bytes ≈ 7 MB (fits in L3 cache).
  static constexpr size_t TT_SIZE_BITS = 18;
  static constexpr size_t TT_SIZE = 1ULL << TT_SIZE_BITS;
  static constexpr size_t TT_MASK = TT_SIZE - 1;
  std::vector<TTEntry<M>> flat_tt;

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

  struct RootMoveScore {
    M move;
    int score;
  };
  std::vector<RootMoveScore> mCurrentRootScores;
  std::vector<RootMoveScore> mLastCompletedRootScores;

  Minimax(double max_seconds = 10, int max_moves = INF, std::function<int(S*)> get_goodness = nullptr) :
  Algorithm<S, M>(),
  flat_tt(TT_SIZE),
	  MAX_SECONDS(max_seconds),
	  MAX_MOVES(max_moves),
	  get_goodness(get_goodness),
	  timer(Timer()),
	  mMaxDepth(MAX_DEPTH), mUseTranspositionTable(true), mTraceStream(&std::cout), mLastSearchStats() {
    memset(mKillerValid, 0, sizeof(mKillerValid));
    memset(mHistory, 0, sizeof(mHistory));
  }

  void reset() override {
    memset(flat_tt.data(), 0, flat_tt.size() * sizeof(TTEntry<M>));
  }

  void setMaxSeconds(double seconds) {
    MAX_SECONDS = seconds;
  }

  void setMaxDepth(int depth) {
    mMaxDepth = depth;
  }

  void setUseTranspositionTable(bool useTranspositionTable) {
    mUseTranspositionTable = useTranspositionTable;
  }

  void setTraceStream(std::ostream* traceStream) {
    mTraceStream = traceStream;
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
    mHasCachedRootMoves = false;
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
        mLastSearchStats.tt_size = TT_SIZE;
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
  MinimaxResult<M> minimax(S *state, int depth, int alpha, int beta, int indent) {
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
    if (mUseTranspositionTable && entry_found && entry.depth >= depth
        && state->isValidMove(entry.move)) {
      ++tt_hits;
      if (entry.value_type == TTEntryType::EXACT_VALUE) {
        ++tt_exacts;
        return {entry.value, entry.move, true};
      }
      if (entry.value_type == TTEntryType::LOWER_BOUND && alpha < entry.value) {
        alpha = entry.value;
      }
      if (entry.value_type == TTEntryType::UPPER_BOUND && beta > entry.value) {
        beta = entry.value;
      }
      if (alpha >= beta) {
        ++tt_cuts;
        return {entry.value, entry.move, true};
      }
    }

    int max_goodness = -INF;
    bool completed = true;
    // Reuse per-depth move buffers to avoid heap allocation in the search loop.
    static std::vector<M> depth_move_buffers[64];
    std::vector<M>& legal_moves = (indent == 0 && mHasCachedRootMoves)
      ? mCachedRootMoves
      : depth_move_buffers[indent];
    if (!(indent == 0 && mHasCachedRootMoves)) {
      state->fill_legal_moves(legal_moves, MAX_MOVES);

      // Promote TT move, killer moves, and high-history moves to the front.
      // This is the key move ordering improvement: TT > killers > history > heuristic.
      if (indent > 0 && !legal_moves.empty()) {
        int front = 0;

        // 1. TT move first (from shallow entries that didn't cause a cutoff above).
        if (mUseTranspositionTable && entry_found && state->isValidMove(entry.move)) {
          for (int i = front; i < static_cast<int>(legal_moves.size()); i++) {
            if (legal_moves[i].mLegalWordId == entry.move.mLegalWordId) {
              std::swap(legal_moves[front], legal_moves[i]);
              front++;
              break;
            }
          }
        }

        // 2. Killer moves next.
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

        // 3. Sort remaining moves by history score, but only at nodes with
        //    enough remaining depth to amortize the sort cost.
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
    }

    assert(legal_moves.size() > 0);
    bool found_best_move = false;

    for (int i = 0; i < legal_moves.size(); i++) {
      const M& move = legal_moves[i];

      // Scope around StateUndoer is important here, so that it gets undone.
      // FIX-ME to some cool macro thing that makes that clearer.
      {
	StateUndoer<S, M> undoer(*state, move);

	state->make_move(move);
	const int goodness = -minimax(
				      state,
				      depth - 1,
				      -beta,
				      -alpha,
				      indent + 1).goodness;
	VLOG(9) << *state << std::endl;

	if (indent == 0) {
	  mCurrentRootScores.push_back({legal_moves[i], goodness});
	}

	if (timer.exceeded(MAX_SECONDS)) {
	  completed = false;
	  break;
	}

	if (goodness > max_goodness) {
	  max_goodness = goodness;
	  best_move = move;
	  found_best_move = true;
	  VLOG(9) << "choosing --> h(" << goodness << ")" << best_move << std::endl;
	  if (max_goodness >= beta) {
	    ++beta_cuts;
	    cut_bf_sum += i + 1;
	    // Record killer move (only non-TT moves).
	    if (indent < MAX_PLY) {
	      if (!mKillerValid[indent][0] ||
	          mKillers[indent][0].mLegalWordId != move.mLegalWordId) {
	        mKillers[indent][1] = mKillers[indent][0];
	        mKillerValid[indent][1] = mKillerValid[indent][0];
	        mKillers[indent][0] = move;
	        mKillerValid[indent][0] = true;
	      }
	    }
	    // Update history: reward the cutoff move.
	    int id = move.mLegalWordId;
	    if (id >= 0 && id < HISTORY_TABLE_SIZE) {
	      mHistory[id] += depth * depth;
	    }
	    break;
	  }
	}
      }

      if (alpha < max_goodness) {
	alpha = max_goodness;
      }
    }

    if (mUseTranspositionTable && completed) {
      update_tt(state, alpha_original, beta, max_goodness, best_move, depth);
    }

    // If no best move is found, then any move is good as any other.
    // Perhaps just picking the zero'th move is better, since they are sorted,
    // theoretically by best...
    if (!found_best_move) {
      best_move = legal_moves[random.uniform(0, legal_moves.size() - 1)];
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
    auto& slot = flat_tt[key & TT_MASK];
    if (slot.verification_key != state->tt_verification_key()) {
      return false;
    }
    entry = slot;
    return true;
  }

  // Store a position in the flat TT. "Always replace" policy: if the slot
  // is occupied by a different position, it's silently overwritten.
  void add_tt_entry(S *state, const TTEntry<M> &entry) {
    auto key = state->hash();
    flat_tt[key & TT_MASK] = entry;
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
