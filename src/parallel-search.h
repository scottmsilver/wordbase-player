// parallel-search.h — Three parallel search strategies for comparison.
//
// All three implement Algorithm<S,M> and expose getLastSearchStats().
// Use --parallel-mode root|lazysmp|ybwc --threads N in perf-test.
#pragma once

#include <algorithm>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Strategy 1: Root-Level Parallelism
//
// Split root moves across N threads. Each thread gets its own Minimax
// instance with independent TT, killers, and history tables. After time
// expires, the best result across all threads is returned.
//
// Trade-offs:
//   + Simple, no shared state, no synchronization overhead
//   + Each thread explores deep into its assigned moves
//   - No information sharing between threads (separate TTs)
//   - Weaker alpha bounds (each thread only knows its own best)
//   - Load imbalance if some moves are much harder than others
// ---------------------------------------------------------------------------
template<class S, class M>
struct RootParallelSearch : public Algorithm<S, M> {
  int mNumThreads;
  double mMaxSeconds;
  int mMaxMoves;
  int mMaxDepth;
  bool mUseTranspositionTable;
  std::function<int(S*)> mGetGoodness;
  typename Minimax<S, M>::SearchStats mLastSearchStats;

  RootParallelSearch(int numThreads, double maxSeconds, int maxMoves = INF,
                     int maxDepth = MAX_DEPTH, bool useTT = true,
                     std::function<int(S*)> getGoodness = nullptr)
    : mNumThreads(numThreads), mMaxSeconds(maxSeconds), mMaxMoves(maxMoves),
      mMaxDepth(maxDepth), mUseTranspositionTable(useTT), mGetGoodness(getGoodness) {}

  const typename Minimax<S, M>::SearchStats& getLastSearchStats() const {
    return mLastSearchStats;
  }

  M get_move(S* state) override {
    std::vector<M> allMoves;
    state->fill_legal_moves(allMoves, mMaxMoves);
    if (allMoves.empty()) return M();

    // Round-robin distribute moves across threads.
    // Move[0] (best heuristic) goes to thread 0, move[1] to thread 1, etc.
    std::vector<std::vector<M>> threadMoves(mNumThreads);
    for (size_t i = 0; i < allMoves.size(); i++) {
      threadMoves[i % mNumThreads].push_back(allMoves[i]);
    }

    struct ThreadResult {
      M bestMove;
      int bestScore = -INF;
      typename Minimax<S, M>::SearchStats stats;
    };
    std::vector<ThreadResult> results(mNumThreads);

    std::vector<std::thread> threads;
    for (int t = 0; t < mNumThreads; t++) {
      if (threadMoves[t].empty()) continue;
      threads.emplace_back([&, t]() {
        S threadState = state->clone();
        Minimax<S, M> engine(mMaxSeconds, mMaxMoves, mGetGoodness);
        engine.setMaxDepth(mMaxDepth);
        engine.setUseTranspositionTable(mUseTranspositionTable);
        engine.setTraceStream(nullptr);
        engine.setRootMoves(threadMoves[t]);

        results[t].bestMove = engine.get_move(&threadState);
        results[t].bestScore = engine.getLastSearchStats().goodness;
        results[t].stats = engine.getLastSearchStats();
      });
    }
    for (auto& t : threads) t.join();

    // Pick best result: prefer deeper search, break ties by score.
    int bestIdx = 0;
    for (int t = 1; t < mNumThreads; t++) {
      if (threadMoves[t].empty()) continue;
      if (results[t].stats.max_depth > results[bestIdx].stats.max_depth ||
          (results[t].stats.max_depth == results[bestIdx].stats.max_depth &&
           results[t].bestScore > results[bestIdx].bestScore)) {
        bestIdx = t;
      }
    }

    // Aggregate stats across all threads.
    mLastSearchStats = results[bestIdx].stats;
    mLastSearchStats.nodes = 0;
    mLastSearchStats.leafs = 0;
    mLastSearchStats.beta_cuts = 0;
    mLastSearchStats.tt_hits = 0;
    double maxElapsed = 0;
    for (int t = 0; t < mNumThreads; t++) {
      if (threadMoves[t].empty()) continue;
      mLastSearchStats.nodes += results[t].stats.nodes;
      mLastSearchStats.leafs += results[t].stats.leafs;
      mLastSearchStats.beta_cuts += results[t].stats.beta_cuts;
      mLastSearchStats.tt_hits += results[t].stats.tt_hits;
      maxElapsed = std::max(maxElapsed, results[t].stats.elapsed_seconds);
    }
    mLastSearchStats.elapsed_seconds = maxElapsed;
    mLastSearchStats.nodes_per_second = maxElapsed > 0
      ? mLastSearchStats.nodes / maxElapsed : 0;

    return results[bestIdx].bestMove;
  }

  std::string get_name() const override { return "RootParallel"; }
};

// ---------------------------------------------------------------------------
// Strategy 2: Lazy SMP (Symmetric Multi-Processing)
//
// All N threads run full iterative deepening from the root, sharing a single
// transposition table. Threads naturally diverge because different TT states
// lead to different move orderings and cutoffs. This is the standard approach
// used by Stockfish and other top chess engines.
//
// Trade-offs:
//   + Shared TT provides automatic information sharing between threads
//   + All threads explore the full move space (no artificial partitioning)
//   + Simple implementation with proven effectiveness
//   - Benign data races on TT reads/writes (caught by verification key)
//   - Some redundant work across threads
//   - Diminishing returns past 4-8 threads
// ---------------------------------------------------------------------------
template<class S, class M>
struct LazySMPSearch : public Algorithm<S, M> {
  int mNumThreads;
  double mMaxSeconds;
  int mMaxMoves;
  int mMaxDepth;
  bool mUseTranspositionTable;
  std::function<int(S*)> mGetGoodness;
  typename Minimax<S, M>::SearchStats mLastSearchStats;

  LazySMPSearch(int numThreads, double maxSeconds, int maxMoves = INF,
                int maxDepth = MAX_DEPTH, bool useTT = true,
                std::function<int(S*)> getGoodness = nullptr)
    : mNumThreads(numThreads), mMaxSeconds(maxSeconds), mMaxMoves(maxMoves),
      mMaxDepth(maxDepth), mUseTranspositionTable(useTT), mGetGoodness(getGoodness) {}

  const typename Minimax<S, M>::SearchStats& getLastSearchStats() const {
    return mLastSearchStats;
  }

  M get_move(S* state) override {
    // Shared TT — all threads read/write the same table.
    // Data races are benign: the verification key detects torn reads.
    constexpr size_t TT_SIZE = Minimax<S, M>::TT_SIZE;
    std::vector<TTEntry<M>> sharedTT(TT_SIZE);

    struct ThreadResult {
      M bestMove;
      typename Minimax<S, M>::SearchStats stats;
    };
    std::vector<ThreadResult> results(mNumThreads);

    std::vector<std::thread> threads;
    for (int t = 0; t < mNumThreads; t++) {
      threads.emplace_back([&, t]() {
        S threadState = state->clone();
        Minimax<S, M> engine(mMaxSeconds, mMaxMoves, mGetGoodness);
        engine.setMaxDepth(mMaxDepth);
        engine.setUseTranspositionTable(mUseTranspositionTable);
        engine.setTraceStream(nullptr);
        engine.setSharedTT(sharedTT.data());

        results[t].bestMove = engine.get_move(&threadState);
        results[t].stats = engine.getLastSearchStats();
      });
    }
    for (auto& t : threads) t.join();

    // Use result from thread with deepest completed search.
    int bestIdx = 0;
    for (int t = 1; t < mNumThreads; t++) {
      if (results[t].stats.max_depth > results[bestIdx].stats.max_depth) {
        bestIdx = t;
      }
    }

    // Aggregate stats.
    mLastSearchStats = results[bestIdx].stats;
    mLastSearchStats.nodes = 0;
    mLastSearchStats.leafs = 0;
    mLastSearchStats.beta_cuts = 0;
    mLastSearchStats.tt_hits = 0;
    double maxElapsed = 0;
    for (int t = 0; t < mNumThreads; t++) {
      mLastSearchStats.nodes += results[t].stats.nodes;
      mLastSearchStats.leafs += results[t].stats.leafs;
      mLastSearchStats.beta_cuts += results[t].stats.beta_cuts;
      mLastSearchStats.tt_hits += results[t].stats.tt_hits;
      maxElapsed = std::max(maxElapsed, results[t].stats.elapsed_seconds);
    }
    mLastSearchStats.elapsed_seconds = maxElapsed;
    mLastSearchStats.nodes_per_second = maxElapsed > 0
      ? mLastSearchStats.nodes / maxElapsed : 0;

    return results[bestIdx].bestMove;
  }

  std::string get_name() const override { return "LazySMP"; }
};

// ---------------------------------------------------------------------------
// Strategy 3: YBWC (Young Brothers Wait Concept) — Root-Level
//
// Main thread (thread 0) searches ALL root moves via full iterative deepening.
// Worker threads (1..N-1) search only non-PV root moves, sharing the TT with
// the main thread. Workers pre-populate TT entries that the main thread hits
// when it reaches those moves, speeding up its search.
//
// Trade-offs:
//   + PV move gets full attention from main thread (best move ordering)
//   + Workers speculatively pre-compute non-PV subtrees via shared TT
//   + Good alpha bound from PV guides worker cutoffs
//   - Workers may do redundant work if main thread prunes differently
//   - Main thread has more work (all moves) than any single worker
//   - Slightly more complex than Lazy SMP with similar effectiveness
// ---------------------------------------------------------------------------
template<class S, class M>
struct YBWCSearch : public Algorithm<S, M> {
  int mNumThreads;
  double mMaxSeconds;
  int mMaxMoves;
  int mMaxDepth;
  bool mUseTranspositionTable;
  std::function<int(S*)> mGetGoodness;
  typename Minimax<S, M>::SearchStats mLastSearchStats;

  YBWCSearch(int numThreads, double maxSeconds, int maxMoves = INF,
             int maxDepth = MAX_DEPTH, bool useTT = true,
             std::function<int(S*)> getGoodness = nullptr)
    : mNumThreads(numThreads), mMaxSeconds(maxSeconds), mMaxMoves(maxMoves),
      mMaxDepth(maxDepth), mUseTranspositionTable(useTT), mGetGoodness(getGoodness) {}

  const typename Minimax<S, M>::SearchStats& getLastSearchStats() const {
    return mLastSearchStats;
  }

  M get_move(S* state) override {
    std::vector<M> allMoves;
    state->fill_legal_moves(allMoves, mMaxMoves);
    if (allMoves.empty()) return M();

    // Shared TT between main thread and all workers.
    constexpr size_t TT_SIZE = Minimax<S, M>::TT_SIZE;
    std::vector<TTEntry<M>> sharedTT(TT_SIZE);

    // Non-PV moves for workers (skip move[0], the expected best).
    std::vector<M> nonPvMoves(allMoves.begin() + 1, allMoves.end());
    int numWorkers = std::min(mNumThreads - 1, std::max(1, (int)nonPvMoves.size()));

    // Distribute non-PV moves among workers.
    std::vector<std::vector<M>> workerMoves(numWorkers);
    for (size_t i = 0; i < nonPvMoves.size(); i++) {
      workerMoves[i % numWorkers].push_back(nonPvMoves[i]);
    }

    struct ThreadResult {
      M bestMove;
      typename Minimax<S, M>::SearchStats stats;
    };
    ThreadResult mainResult;
    std::vector<ThreadResult> workerResults(numWorkers);

    std::vector<std::thread> threads;

    // Main thread: searches ALL moves with shared TT.
    threads.emplace_back([&]() {
      S threadState = state->clone();
      Minimax<S, M> engine(mMaxSeconds, mMaxMoves, mGetGoodness);
      engine.setMaxDepth(mMaxDepth);
      engine.setUseTranspositionTable(mUseTranspositionTable);
      engine.setTraceStream(nullptr);
      engine.setSharedTT(sharedTT.data());

      mainResult.bestMove = engine.get_move(&threadState);
      mainResult.stats = engine.getLastSearchStats();
    });

    // Worker threads: search only non-PV moves (pre-populate TT).
    for (int t = 0; t < numWorkers; t++) {
      if (workerMoves[t].empty()) continue;
      threads.emplace_back([&, t]() {
        S threadState = state->clone();
        Minimax<S, M> engine(mMaxSeconds, mMaxMoves, mGetGoodness);
        engine.setMaxDepth(mMaxDepth);
        engine.setUseTranspositionTable(mUseTranspositionTable);
        engine.setTraceStream(nullptr);
        engine.setSharedTT(sharedTT.data());
        engine.setRootMoves(workerMoves[t]);

        workerResults[t].bestMove = engine.get_move(&threadState);
        workerResults[t].stats = engine.getLastSearchStats();
      });
    }
    for (auto& t : threads) t.join();

    // Main thread result is primary. Check if any worker found deeper.
    M bestMove = mainResult.bestMove;
    int bestDepth = mainResult.stats.max_depth;
    int bestScore = mainResult.stats.goodness;

    for (int t = 0; t < numWorkers; t++) {
      if (workerMoves[t].empty()) continue;
      if (workerResults[t].stats.max_depth > bestDepth ||
          (workerResults[t].stats.max_depth == bestDepth &&
           workerResults[t].stats.goodness > bestScore)) {
        bestMove = workerResults[t].bestMove;
        bestDepth = workerResults[t].stats.max_depth;
        bestScore = workerResults[t].stats.goodness;
      }
    }

    // Aggregate stats.
    mLastSearchStats = mainResult.stats;
    mLastSearchStats.best_move = bestMove;
    mLastSearchStats.goodness = bestScore;
    mLastSearchStats.max_depth = bestDepth;
    mLastSearchStats.nodes = mainResult.stats.nodes;
    mLastSearchStats.leafs = mainResult.stats.leafs;
    mLastSearchStats.beta_cuts = mainResult.stats.beta_cuts;
    mLastSearchStats.tt_hits = mainResult.stats.tt_hits;
    double maxElapsed = mainResult.stats.elapsed_seconds;
    for (int t = 0; t < numWorkers; t++) {
      if (workerMoves[t].empty()) continue;
      mLastSearchStats.nodes += workerResults[t].stats.nodes;
      mLastSearchStats.leafs += workerResults[t].stats.leafs;
      mLastSearchStats.beta_cuts += workerResults[t].stats.beta_cuts;
      mLastSearchStats.tt_hits += workerResults[t].stats.tt_hits;
      maxElapsed = std::max(maxElapsed, workerResults[t].stats.elapsed_seconds);
    }
    mLastSearchStats.elapsed_seconds = maxElapsed;
    mLastSearchStats.nodes_per_second = maxElapsed > 0
      ? mLastSearchStats.nodes / maxElapsed : 0;

    return bestMove;
  }

  std::string get_name() const override { return "YBWC"; }
};
