// parallel-search.h — Three parallel search strategies for comparison.
//
// All three implement Algorithm<S,M> and expose getLastSearchStats().
// Use --parallel-mode root|lazysmp|ybwc --threads N in perf-test.
#pragma once

#include <algorithm>
#include <thread>
#include <vector>

// Common base for all parallel strategies. Holds shared config and helpers.
template<class S, class M>
struct ParallelSearchBase : public Algorithm<S, M> {
  int mNumThreads;
  double mMaxSeconds;
  int mMaxMoves;
  int mMaxDepth;
  bool mUseTranspositionTable;
  std::function<int(S*)> mGetGoodness;
  typename Minimax<S, M>::SearchStats mLastSearchStats;

  ParallelSearchBase(int numThreads, double maxSeconds, int maxMoves = INF,
                     int maxDepth = MAX_DEPTH, bool useTT = true,
                     std::function<int(S*)> getGoodness = nullptr)
    : mNumThreads(numThreads), mMaxSeconds(maxSeconds), mMaxMoves(maxMoves),
      mMaxDepth(maxDepth), mUseTranspositionTable(useTT), mGetGoodness(getGoodness) {}

  const typename Minimax<S, M>::SearchStats& getLastSearchStats() const {
    return mLastSearchStats;
  }

protected:
  struct ThreadResult {
    M bestMove{};
    typename Minimax<S, M>::SearchStats stats{};
  };

  // Configure a Minimax engine with this strategy's settings.
  void configureEngine(Minimax<S, M>& engine, TTEntry<M>* sharedTT = nullptr,
                       const std::vector<M>* rootMoves = nullptr) {
    engine.setMaxDepth(mMaxDepth);
    engine.setUseTranspositionTable(mUseTranspositionTable);
    engine.setTraceStream(nullptr);
    if (sharedTT) engine.setSharedTT(sharedTT);
    if (rootMoves) engine.setRootMoves(*rootMoves);
  }

  // Run a search on a thread: clone state, create engine, search, store result.
  void runThread(S* state, ThreadResult& result,
                 TTEntry<M>* sharedTT = nullptr,
                 const std::vector<M>* rootMoves = nullptr) {
    S threadState = state->clone();
    Minimax<S, M> engine(mMaxSeconds, mMaxMoves, mGetGoodness);
    configureEngine(engine, sharedTT, rootMoves);
    result.bestMove = engine.get_move(&threadState);
    result.stats = engine.getLastSearchStats();
  }

  // Pick the best result: prefer deeper search, break ties by score.
  static int pickBest(const std::vector<ThreadResult>& results, int count) {
    int bestIdx = 0;
    for (int t = 1; t < count; t++) {
      if (results[t].stats.max_depth > results[bestIdx].stats.max_depth ||
          (results[t].stats.max_depth == results[bestIdx].stats.max_depth &&
           results[t].stats.goodness > results[bestIdx].stats.goodness)) {
        bestIdx = t;
      }
    }
    return bestIdx;
  }

  // Aggregate stats from multiple results into mLastSearchStats.
  void aggregateStats(const ThreadResult& primary,
                      const std::vector<ThreadResult>& others, int count) {
    mLastSearchStats = primary.stats;
    double maxElapsed = primary.stats.elapsed_seconds;
    for (int t = 0; t < count; t++) {
      mLastSearchStats.nodes += others[t].stats.nodes;
      mLastSearchStats.leafs += others[t].stats.leafs;
      mLastSearchStats.beta_cuts += others[t].stats.beta_cuts;
      mLastSearchStats.tt_hits += others[t].stats.tt_hits;
      maxElapsed = std::max(maxElapsed, others[t].stats.elapsed_seconds);
    }
    mLastSearchStats.elapsed_seconds = maxElapsed;
    mLastSearchStats.nodes_per_second = maxElapsed > 0
      ? mLastSearchStats.nodes / maxElapsed : 0;
  }

  // Simpler overload: all results in one vector, best already chosen.
  void aggregateStats(const std::vector<ThreadResult>& results, int count, int bestIdx) {
    mLastSearchStats = results[bestIdx].stats;
    mLastSearchStats.nodes = 0;
    mLastSearchStats.leafs = 0;
    mLastSearchStats.beta_cuts = 0;
    mLastSearchStats.tt_hits = 0;
    double maxElapsed = 0;
    for (int t = 0; t < count; t++) {
      mLastSearchStats.nodes += results[t].stats.nodes;
      mLastSearchStats.leafs += results[t].stats.leafs;
      mLastSearchStats.beta_cuts += results[t].stats.beta_cuts;
      mLastSearchStats.tt_hits += results[t].stats.tt_hits;
      maxElapsed = std::max(maxElapsed, results[t].stats.elapsed_seconds);
    }
    mLastSearchStats.elapsed_seconds = maxElapsed;
    mLastSearchStats.nodes_per_second = maxElapsed > 0
      ? mLastSearchStats.nodes / maxElapsed : 0;
  }

  // Distribute moves round-robin across N buckets.
  static std::vector<std::vector<M>> distributeMoves(
      const std::vector<M>& moves, int numBuckets) {
    std::vector<std::vector<M>> buckets(numBuckets);
    for (size_t i = 0; i < moves.size(); i++) {
      buckets[i % numBuckets].push_back(moves[i]);
    }
    return buckets;
  }
};

// ---------------------------------------------------------------------------
// Strategy 1: Root-Level Parallelism
//
// Split root moves across N threads. Each thread gets its own Minimax
// instance with independent TT, killers, and history tables.
//
//   + Simple, no shared state, no synchronization overhead
//   - No information sharing between threads (separate TTs)
//   - Weaker alpha bounds (each thread only knows its own best)
// ---------------------------------------------------------------------------
template<class S, class M>
struct RootParallelSearch : public ParallelSearchBase<S, M> {
  using Base = ParallelSearchBase<S, M>;
  using typename Base::ThreadResult;

  using Base::Base;  // inherit constructor

  M get_move(S* state) override {
    std::vector<M> allMoves;
    state->fill_legal_moves(allMoves, this->mMaxMoves);
    if (allMoves.empty()) return M();

    auto threadMoves = Base::distributeMoves(allMoves, this->mNumThreads);
    std::vector<ThreadResult> results(this->mNumThreads);

    std::vector<std::thread> threads;
    for (int t = 0; t < this->mNumThreads; t++) {
      if (threadMoves[t].empty()) continue;
      threads.emplace_back([&, t]() {
        this->runThread(state, results[t], nullptr, &threadMoves[t]);
      });
    }
    for (auto& t : threads) t.join();

    int bestIdx = Base::pickBest(results, this->mNumThreads);
    Base::aggregateStats(results, this->mNumThreads, bestIdx);
    return results[bestIdx].bestMove;
  }

  std::string get_name() const override { return "RootParallel"; }
};

// ---------------------------------------------------------------------------
// Strategy 2: Lazy SMP (Symmetric Multi-Processing)
//
// All N threads run full iterative deepening from the root, sharing a single
// transposition table. Standard approach used by Stockfish.
//
//   + Shared TT provides automatic information sharing
//   + All threads explore the full move space
//   - Benign data races on TT (caught by verification key)
//   - Some redundant work across threads
// ---------------------------------------------------------------------------
template<class S, class M>
struct LazySMPSearch : public ParallelSearchBase<S, M> {
  using Base = ParallelSearchBase<S, M>;
  using typename Base::ThreadResult;

  using Base::Base;

  M get_move(S* state) override {
    if (mSharedTT.empty()) mSharedTT.resize(Minimax<S, M>::TT_SIZE);
    else std::fill(mSharedTT.begin(), mSharedTT.end(), TTEntry<M>{});

    std::vector<ThreadResult> results(this->mNumThreads);
    std::vector<std::thread> threads;
    for (int t = 0; t < this->mNumThreads; t++) {
      threads.emplace_back([&, t]() {
        this->runThread(state, results[t], mSharedTT.data());
      });
    }
    for (auto& t : threads) t.join();

    int bestIdx = Base::pickBest(results, this->mNumThreads);
    Base::aggregateStats(results, this->mNumThreads, bestIdx);
    return results[bestIdx].bestMove;
  }

  std::string get_name() const override { return "LazySMP"; }

private:
  std::vector<TTEntry<M>> mSharedTT;
};

// ---------------------------------------------------------------------------
// Strategy 3: YBWC (Young Brothers Wait Concept) — Root-Level
//
// Main thread searches ALL root moves. Worker threads search only non-PV
// moves, sharing the TT. Workers pre-populate TT entries for the main thread.
//
//   + PV move gets full attention; workers pre-compute non-PV subtrees
//   - Workers may do redundant work
//   - Main thread has more work than any single worker
// ---------------------------------------------------------------------------
template<class S, class M>
struct YBWCSearch : public ParallelSearchBase<S, M> {
  using Base = ParallelSearchBase<S, M>;
  using typename Base::ThreadResult;

  using Base::Base;

  M get_move(S* state) override {
    std::vector<M> allMoves;
    state->fill_legal_moves(allMoves, this->mMaxMoves);
    if (allMoves.empty()) return M();

    if (mSharedTT.empty()) mSharedTT.resize(Minimax<S, M>::TT_SIZE);
    else std::fill(mSharedTT.begin(), mSharedTT.end(), TTEntry<M>{});

    // Non-PV moves for workers (skip move[0], the expected best).
    std::vector<M> nonPvMoves(allMoves.begin() + 1, allMoves.end());
    int numWorkers = std::min(this->mNumThreads - 1, std::max(1, (int)nonPvMoves.size()));
    auto workerMoves = Base::distributeMoves(nonPvMoves, numWorkers);

    ThreadResult mainResult;
    std::vector<ThreadResult> workerResults(numWorkers);

    std::vector<std::thread> threads;

    // Main thread: all moves, shared TT.
    threads.emplace_back([&]() {
      this->runThread(state, mainResult, mSharedTT.data());
    });

    // Workers: non-PV subsets, shared TT.
    for (int t = 0; t < numWorkers; t++) {
      if (workerMoves[t].empty()) continue;
      threads.emplace_back([&, t]() {
        this->runThread(state, workerResults[t], mSharedTT.data(), &workerMoves[t]);
      });
    }
    for (auto& t : threads) t.join();

    // Check if any worker beat the main thread.
    int bestDepth = mainResult.stats.max_depth;
    int bestScore = mainResult.stats.goodness;
    M bestMove = mainResult.bestMove;
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

    Base::aggregateStats(mainResult, workerResults, numWorkers);
    this->mLastSearchStats.best_move = bestMove;
    this->mLastSearchStats.goodness = bestScore;
    this->mLastSearchStats.max_depth = bestDepth;
    return bestMove;
  }

  std::string get_name() const override { return "YBWC"; }

private:
  std::vector<TTEntry<M>> mSharedTT;
};
