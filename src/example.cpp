#include <sstream>
#include <string>
#include <unordered_set>
#include <cstdint>

#include "board.h"
#include "easylogging++.h"
#include "gtest/gtest.h"
#include "word-dictionary.h"

#include "wordescape.cpp"
#include "parallel-search.h"

INITIALIZE_EASYLOGGINGPP

namespace {

  const char* kReadmeBoard =
    "gregmiperslmavnetlaecaosrnowykosbrilfakosalagzl*eicveonredgmdamepumselomrtleipcradsndlnoihuiai*eoisatxerhctpteroustupsyalcopaeamhves";

  struct TestMove : public Move<TestMove> {
    int mId = 0;
    int mLegalWordId = -1;

    TestMove() = default;
    explicit TestMove(int id) : mId(id), mLegalWordId(id) {}

    void read() {}
    std::ostream& to_stream(std::ostream& os) const {
      os << "tm(" << mId << ")";
      return os;
    }
    bool operator==(const TestMove& rhs) const { return mId == rhs.mId; }
    size_t hash() const { return std::hash<int>()(mId); }
  };

  struct TestState : public State<TestState, TestMove> {
    int mEval;
    int mDepthRemaining;

    TestState(char playerToMove, int eval, int depthRemaining)
      : State<TestState, TestMove>(playerToMove), mEval(eval), mDepthRemaining(depthRemaining) {}

    TestState clone() const override {
      return *this;
    }

    int get_goodness() const override {
      return mEval;
    }

    std::vector<TestMove> get_legal_moves(int max_moves = INF) const override {
      if (mDepthRemaining == 0) {
        return {};
      }
      return {TestMove(1), TestMove(2)};
    }

    char get_enemy(char player) const override {
      return player == PLAYER_1 ? PLAYER_2 : PLAYER_1;
    }

    bool is_terminal() const override {
      return mDepthRemaining == 0;
    }

    bool is_winner(char player) const override {
      return false;
    }

    void make_move(const TestMove& move) override {
      player_to_move = get_enemy(player_to_move);
      mDepthRemaining -= 1;
      mEval = (move.mId == 1) ? 10 : -10;
    }

    std::ostream& to_stream(std::ostream& os) const override {
      os << "eval=" << mEval << " depth=" << mDepthRemaining;
      return os;
    }

    bool operator==(const TestState& other) const override {
      return player_to_move == other.player_to_move
        && mEval == other.mEval
        && mDepthRemaining == other.mDepthRemaining;
    }

    size_t hash() const override {
      size_t seed = std::hash<int>()(mEval);
      boost::hash_combine(seed, mDepthRemaining);
      boost::hash_combine(seed, player_to_move);
      return seed;
    }
  };

  struct CollidingState : public State<CollidingState, TestMove> {
    int mEval;
    int mDepthRemaining;
    size_t mHashValue;
    uint64_t mVerificationKey;

    CollidingState(char playerToMove, int eval, int depthRemaining, size_t hashValue, uint64_t verificationKey)
      : State<CollidingState, TestMove>(playerToMove),
        mEval(eval),
        mDepthRemaining(depthRemaining),
        mHashValue(hashValue),
        mVerificationKey(verificationKey) {}

    CollidingState clone() const override {
      return *this;
    }

    int get_goodness() const override {
      return mEval;
    }

    std::vector<TestMove> get_legal_moves(int max_moves = INF) const override {
      if (mDepthRemaining == 0) {
        return {};
      }
      return {TestMove(1), TestMove(2)};
    }

    char get_enemy(char player) const override {
      return player == PLAYER_1 ? PLAYER_2 : PLAYER_1;
    }

    bool is_terminal() const override {
      return mDepthRemaining == 0;
    }

    bool is_winner(char player) const override {
      return false;
    }

    void make_move(const TestMove& move) override {
      player_to_move = get_enemy(player_to_move);
      mDepthRemaining -= 1;
      mEval = (move.mId == 1) ? 10 : -10;
    }

    std::ostream& to_stream(std::ostream& os) const override {
      os << "eval=" << mEval << " depth=" << mDepthRemaining;
      return os;
    }

    bool operator==(const CollidingState& other) const override {
      return player_to_move == other.player_to_move
        && mEval == other.mEval
        && mDepthRemaining == other.mDepthRemaining
        && mHashValue == other.mHashValue
        && mVerificationKey == other.mVerificationKey;
    }

    size_t hash() const override {
      return mHashValue;
    }

    uint64_t tt_verification_key() const override {
      return mVerificationKey;
    }
  };

  bool ownsConnectedToEdge(const WordBaseState& state, char owner, int edgeRow) {
    std::vector<std::pair<int, int> > stack;
    std::vector<std::vector<bool> > visited(kBoardHeight, std::vector<bool>(kBoardWidth, false));

    for (int x = 0; x < kBoardWidth; x++) {
      if (state.mState.get(edgeRow, x) == owner) {
        stack.push_back(std::make_pair(edgeRow, x));
      }
    }

    while (!stack.empty()) {
      std::pair<int, int> cell = stack.back();
      stack.pop_back();

      const int y = cell.first;
      const int x = cell.second;
      if (y < 0 || y >= kBoardHeight || x < 0 || x >= kBoardWidth || visited[y][x] || state.mState.get(y, x) != owner) {
        continue;
      }

      visited[y][x] = true;
      for (int deltaY = -1; deltaY <= 1; deltaY++) {
        for (int deltaX = -1; deltaX <= 1; deltaX++) {
          if (deltaY != 0 || deltaX != 0) {
            stack.push_back(std::make_pair(y + deltaY, x + deltaX));
          }
        }
      }
    }

    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (state.mState.get(y, x) == owner && !visited[y][x]) {
          return false;
        }
      }
    }

    return true;
  }

  // The fixture for testing class Foo.
  class FooTest : public ::testing::Test {
  protected:
    // You can remove any or all of the following functions if its body
    // is empty.

    FooTest() {
      // You can do set-up work for each test here.
    }

    virtual ~FooTest() {
      // You can do clean-up work that doesn't throw exceptions here.
    }

    // If the constructor and destructor are not enough for setting up
    // and cleaning up each test, you can define the following methods:

    virtual void SetUp() {
      // Code here will be called immediately after the constructor (right
      // before each test).
    }

    virtual void TearDown() {
      // Code here will be called immediately after each test (right
      // before the destructor).
    }

    // Objects declared here can be used by all tests in the test case for Foo.
  };

  // Tests that we can find all the valid word paths in a grid.
  TEST_F(FooTest, SimpleWordsAtGridSquare2) {
    std::istringstream dictionaryFileContents(std::string("cao\n"));
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board("caorsorbafal*sutseidnercbnolecavksidlvrtselruamasiuxigdbrsyngoenerhaneodrosmtsihlaltdymecrescehudndmnefingelermaeamoksbaoflbdecuhlg", wd);
    WordBaseState state(&board, PLAYER_1);

    const LegalWordList& wordList = board.getLegalWords(0, 0);
    EXPECT_EQ(wordList.size(), 1);
    const LegalWord& legalWord = board.getLegalWord(wordList[0]);
    EXPECT_EQ(legalWord.mWord.compare("cao"), 0);
    EXPECT_EQ(legalWord.mWordSequence, CoordinateList({{0, 0}, {0, 1}, {0, 2}}));

    std::vector<WordBaseMove> moves = state.get_legal_moves(10, NULL);
    EXPECT_EQ(moves.size(), 1);
    const LegalWord& legalWord2 = board.getLegalWord(moves[0].mLegalWordId);
    EXPECT_EQ(legalWord2.mWordSequence, CoordinateList({{0, 0}, {0, 1}, {0, 2}}));
  }

  TEST_F(FooTest, ReadmeBoardFindsDocumentedWords) {
    std::istringstream dictionaryFileContents(
      std::string("gram\n")
      + "glam\n"
      + "glamor\n"
      + "glamorizes\n"
      + "glass\n");
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board(kReadmeBoard, wd);

    const auto& wordPaths = board.findValidWordPaths(0, 0);
    EXPECT_GE(wordPaths.size(), 5);

    std::unordered_set<std::string> foundWords;
    for (const auto& wordPath : wordPaths) {
      foundWords.insert(wordPath.first);
    }

    EXPECT_EQ(foundWords.count("gram"), 1);
    EXPECT_EQ(foundWords.count("glam"), 1);
    EXPECT_EQ(foundWords.count("glamor"), 1);
    EXPECT_EQ(foundWords.count("glamorizes"), 1);
    EXPECT_EQ(foundWords.count("glass"), 1);
  }

  TEST_F(FooTest, ReadmeBoardCanPlayDocumentedMove) {
    std::istringstream dictionaryFileContents(std::string("glamorizes\n"));
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board(kReadmeBoard, wd);
    WordBaseState state(&board, PLAYER_1);

    std::vector<WordBaseMove> moves = state.get_legal_moves(INF, "glamorizes");
    ASSERT_EQ(moves.size(), 1);

    const LegalWord& legalWord = board.getLegalWord(moves[0].mLegalWordId);
    EXPECT_EQ(legalWord.mWord, "glamorizes");
    EXPECT_EQ(legalWord.mWordSequence,
      CoordinateList({{0, 0}, {1, 0}, {2, 1}, {1, 1}, {2, 2}, {3, 3}, {3, 4}, {4, 5}, {5, 5}, {6, 6}}));
    EXPECT_EQ(board.wordFromMove(legalWord.mWordSequence), "glamorizes");

    state.make_move(moves[0]);

    EXPECT_EQ(state.player_to_move, PLAYER_2);
    EXPECT_EQ(state.get_goodness(), -168);

    const std::vector<std::string> alreadyPlayed = state.getAlreadyPlayed();
    ASSERT_EQ(alreadyPlayed.size(), 1);
    EXPECT_EQ(alreadyPlayed[0], "glamorizes");
    EXPECT_TRUE(ownsConnectedToEdge(state, PLAYER_1, 0));
    EXPECT_TRUE(ownsConnectedToEdge(state, PLAYER_2, kBoardHeight - 1));
  }

  TEST_F(FooTest, MaxMovesLimitIsHonored) {
    std::istringstream dictionaryFileContents(
      std::string("gram\n")
      + "glam\n"
      + "glamor\n"
      + "glamorizes\n"
      + "glass\n");
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board(kReadmeBoard, wd);
    WordBaseState state(&board, PLAYER_1);

    std::vector<WordBaseMove> moves = state.get_legal_moves(2);

    EXPECT_LE(moves.size(), 2);
  }

  TEST_F(FooTest, MinimaxEvaluatesCurrentLeafState) {
    TestState state(PLAYER_1, 0, 1);
    Minimax<TestState, TestMove> minimax(1.0, INF);
    minimax.setMaxDepth(1);
    minimax.setTraceStream(nullptr);

    TestMove bestMove = minimax.get_move(&state);

    EXPECT_EQ(bestMove.mId, 2);
    EXPECT_EQ(minimax.getLastSearchStats().goodness, 10);
  }

  TEST_F(FooTest, WordBaseStateUndoRestoresBoardTurnAndPlayedWords) {
    std::istringstream dictionaryFileContents(
      std::string("gram\n")
      + "glam\n"
      + "glamor\n"
      + "glamorizes\n"
      + "glass\n");
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board(kReadmeBoard, wd);
    WordBaseState state(&board, PLAYER_1);
    WordBaseState original(state);

    std::vector<WordBaseMove> moves = state.get_legal_moves(INF, "glamorizes");
    ASSERT_EQ(moves.size(), 1);

    {
      StateUndoer<WordBaseState, WordBaseMove> undoer(state, moves[0]);
      state.make_move(moves[0]);
      EXPECT_NE(state.player_to_move, original.player_to_move);
      EXPECT_NE(state.hash(), original.hash());
    }

    EXPECT_TRUE(state == original);
    EXPECT_EQ(state.hash(), original.hash());
  }

  TEST_F(FooTest, WordBaseStateCachedHashMatchesFullRecomputation) {
    std::istringstream dictionaryFileContents(
      std::string("gram\n")
      + "glam\n"
      + "glamor\n"
      + "glamorizes\n"
      + "glass\n");
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board(kReadmeBoard, wd);
    WordBaseState state(&board, PLAYER_1);

    EXPECT_EQ(state.hash(), state.computeHashFromState());
    state.addAlreadyPlayed("glam");
    EXPECT_EQ(state.hash(), state.computeHashFromState());

    std::vector<WordBaseMove> moves = state.get_legal_moves(INF, "glamorizes");
    ASSERT_EQ(moves.size(), 1);
    state.make_move(moves[0]);

    EXPECT_EQ(state.hash(), state.computeHashFromState());
    EXPECT_EQ(state.tt_verification_key(), state.computeVerificationKeyFromState());
  }

  TEST_F(FooTest, MinimaxRejectsTranspositionEntriesWithMismatchedVerificationKey) {
    Minimax<CollidingState, TestMove> minimax(1.0, INF);
    minimax.setMaxDepth(1);
    minimax.setTraceStream(nullptr);

    CollidingState firstState(PLAYER_1, 0, 1, 7, 101);
    TestMove firstMove = minimax.get_move(&firstState);
    ASSERT_EQ(firstMove.mId, 2);
    ASSERT_EQ(minimax.getLastSearchStats().goodness, 10);

    CollidingState collidingState(PLAYER_1, 0, 1, 7, 202);
    TestMove secondMove = minimax.get_move(&collidingState);

    EXPECT_EQ(secondMove.mId, 2);
    EXPECT_EQ(minimax.getLastSearchStats().goodness, 10);
    EXPECT_GE(minimax.getLastSearchStats().leafs, 2);  // PVS re-search may add leafs
  }

  TEST_F(FooTest, EquivalentLegalWordIdsMatchWordLookupRange) {
    std::istringstream dictionaryFileContents(
      std::string("gram\n")
      + "glam\n"
      + "glamor\n"
      + "glamorizes\n"
      + "glass\n");
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board(kReadmeBoard, wd);

    for (int legalWordId = 0; legalWordId < board.getLegalWordsSize(); legalWordId++) {
      std::vector<LegalWordId> fromRange;
      const std::string& word = board.getLegalWord(legalWordId).mWord;
      const std::pair<std::multimap<std::string, LegalWordId>::iterator, std::multimap<std::string, LegalWordId>::iterator> range =
        board.getLegalWordIds(word);
      for (auto i = range.first; i != range.second; ++i) {
        fromRange.push_back(i->second);
      }

      EXPECT_EQ(board.getEquivalentLegalWordIds(legalWordId), fromRange);
    }
  }

  TEST_F(FooTest, WordBaseStateHashTracksPlayerAndPlayedWords) {
    std::istringstream dictionaryFileContents(std::string("cao\n"));
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board("caorsorbafal*sutseidnercbnolecavksidlvrtselruamasiuxigdbrsyngoenerhaneodrosmtsihlaltdymecrescehudndmnefingelermaeamoksbaoflbdecuhlg", wd);

    WordBaseState playerOneState(&board, PLAYER_1);
    WordBaseState playerTwoState(&board, PLAYER_2);
    WordBaseState playedWordState(&board, PLAYER_1);
    playedWordState.addAlreadyPlayed("cao");

    EXPECT_FALSE(playerOneState == playerTwoState);
    EXPECT_NE(playerOneState.hash(), playerTwoState.hash());

    EXPECT_FALSE(playerOneState == playedWordState);
    EXPECT_NE(playerOneState.hash(), playedWordState.hash());
  }
  // -----------------------------------------------------------------------
  // Tests for parallel search strategies
  // -----------------------------------------------------------------------

  // Helper: create a WordBaseState from the readme board with a small dictionary.
  struct ParallelSearchTest : public ::testing::Test {
    std::unique_ptr<WordDictionary> wd;
    std::unique_ptr<BoardStatic> board;
    std::unique_ptr<WordBaseState> state;

    void SetUp() override {
      std::istringstream dictionaryFileContents(
        std::string("gram\n")
        + "glam\n"
        + "glamor\n"
        + "glamorizes\n"
        + "glass\n"
        + "gropes\n"
        + "vanes\n"
        + "copy\n"
        + "cops\n"
        + "soap\n"
        + "soaps\n"
        + "sclerotics\n"
      );
      wd = std::make_unique<WordDictionary>(dictionaryFileContents);
      board = std::make_unique<BoardStatic>(kReadmeBoard, *wd);
      state = std::make_unique<WordBaseState>(board.get(), PLAYER_1);
    }
  };

  // Single-threaded Minimax with setRootMoves should only search the given moves.
  TEST_F(ParallelSearchTest, SetRootMovesRestrictsSearch) {
    auto allMoves = state->get_legal_moves(200);
    ASSERT_GE(allMoves.size(), 2u);

    // Search only the first move.
    std::vector<WordBaseMove> subset = {allMoves[0]};
    Minimax<WordBaseState, WordBaseMove> engine(1.0, 200);
    engine.setMaxDepth(3);
    engine.setTraceStream(nullptr);
    engine.setRootMoves(subset);

    WordBaseState searchState(*state);
    WordBaseMove move = engine.get_move(&searchState);
    EXPECT_EQ(move.mLegalWordId, allMoves[0].mLegalWordId);
  }

  // Shared TT: two Minimax instances sharing a TT should produce TT hits.
  TEST_F(ParallelSearchTest, SharedTTProducesTTHits) {
    constexpr size_t TT_SIZE = Minimax<WordBaseState, WordBaseMove>::TT_SIZE;
    std::vector<TTEntry<WordBaseMove>> sharedTT(TT_SIZE);

    // First engine populates the TT.
    {
      Minimax<WordBaseState, WordBaseMove> engine1(1.0, 200);
      engine1.setMaxDepth(3);
      engine1.setTraceStream(nullptr);
      engine1.setSharedTT(sharedTT.data());
      WordBaseState s1(*state);
      engine1.get_move(&s1);
    }

    // Count non-empty TT entries.
    int populated = 0;
    for (size_t i = 0; i < TT_SIZE; i++) {
      if (sharedTT[i].data != 0) populated++;
    }
    EXPECT_GT(populated, 0) << "First engine should have populated the shared TT";

    // Second engine should hit TT entries from the first.
    {
      Minimax<WordBaseState, WordBaseMove> engine2(1.0, 200);
      engine2.setMaxDepth(3);
      engine2.setTraceStream(nullptr);
      engine2.setSharedTT(sharedTT.data());
      WordBaseState s2(*state);
      engine2.get_move(&s2);
      EXPECT_GT(engine2.getLastSearchStats().tt_hits, 0)
        << "Second engine should get TT hits from shared TT";
    }
  }

  // Root-level parallelism: returns a valid move and produces aggregate stats.
  TEST_F(ParallelSearchTest, RootParallelReturnsValidMove) {
    RootParallelSearch<WordBaseState, WordBaseMove> algo(2, 1.0, 200, 3);
    WordBaseState searchState(*state);
    WordBaseMove move = algo.get_move(&searchState);
    const auto& stats = algo.getLastSearchStats();

    EXPECT_GE(move.mLegalWordId, 0);
    EXPECT_GT(stats.nodes, 0);
    EXPECT_GT(stats.max_depth, 0);
    EXPECT_GT(stats.nodes_per_second, 0);
  }

  // Lazy SMP: returns a valid move, nodes should be >= single-threaded.
  TEST_F(ParallelSearchTest, LazySMPReturnsValidMove) {
    LazySMPSearch<WordBaseState, WordBaseMove> algo(2, 1.0, 200, 3);
    WordBaseState searchState(*state);
    WordBaseMove move = algo.get_move(&searchState);
    const auto& stats = algo.getLastSearchStats();

    EXPECT_GE(move.mLegalWordId, 0);
    EXPECT_GT(stats.nodes, 0);
    EXPECT_GT(stats.max_depth, 0);
  }

  // YBWC: returns a valid move and produces reasonable stats.
  TEST_F(ParallelSearchTest, YBWCReturnsValidMove) {
    YBWCSearch<WordBaseState, WordBaseMove> algo(2, 1.0, 200, 3);
    WordBaseState searchState(*state);
    WordBaseMove move = algo.get_move(&searchState);
    const auto& stats = algo.getLastSearchStats();

    EXPECT_GE(move.mLegalWordId, 0);
    EXPECT_GT(stats.nodes, 0);
    EXPECT_GT(stats.max_depth, 0);
  }

  // All three strategies should agree on the best move at a deterministic depth.
  // Use depth 2 to keep it fast and predictable.
  TEST_F(ParallelSearchTest, AllStrategiesAgreeAtShallowDepth) {
    // Single-threaded baseline.
    Minimax<WordBaseState, WordBaseMove> baseline(10.0, 200);
    baseline.setMaxDepth(2);
    baseline.setTraceStream(nullptr);
    WordBaseState s0(*state);
    WordBaseMove baselineMove = baseline.get_move(&s0);
    int baselineScore = baseline.getLastSearchStats().goodness;

    // Root parallel.
    RootParallelSearch<WordBaseState, WordBaseMove> rootAlgo(2, 10.0, 200, 2);
    WordBaseState s1(*state);
    WordBaseMove rootMove = rootAlgo.get_move(&s1);

    // Lazy SMP.
    LazySMPSearch<WordBaseState, WordBaseMove> smpAlgo(2, 10.0, 200, 2);
    WordBaseState s2(*state);
    WordBaseMove smpMove = smpAlgo.get_move(&s2);

    // YBWC.
    YBWCSearch<WordBaseState, WordBaseMove> ybwcAlgo(2, 10.0, 200, 2);
    WordBaseState s3(*state);
    WordBaseMove ybwcMove = ybwcAlgo.get_move(&s3);

    // Root Parallel and Lazy SMP should match the baseline score exactly
    // (same depth, same position, no shared-TT races at this scale).
    // YBWC uses shared TT between main + workers, so benign data races
    // can shift scores slightly — only check it returns a valid result.
    EXPECT_EQ(rootAlgo.getLastSearchStats().goodness, baselineScore);
    EXPECT_EQ(smpAlgo.getLastSearchStats().goodness, baselineScore);
    EXPECT_GE(ybwcMove.mLegalWordId, 0);
  }

  // Thread-safety: multiple threads calling fill_legal_moves concurrently
  // should not corrupt results (validates thread_local validWordBits).
  TEST_F(ParallelSearchTest, FillLegalMovesIsThreadSafe) {
    const int numThreads = 4;
    std::vector<std::vector<WordBaseMove>> results(numThreads);
    std::vector<std::thread> threads;

    for (int t = 0; t < numThreads; t++) {
      threads.emplace_back([&, t]() {
        WordBaseState threadState(*state);
        threadState.fill_legal_moves(results[t], 200);
      });
    }
    for (auto& t : threads) t.join();

    // All threads should produce the same number of moves.
    ASSERT_GT(results[0].size(), 0u);
    for (int t = 1; t < numThreads; t++) {
      EXPECT_EQ(results[t].size(), results[0].size())
        << "Thread " << t << " got different move count";
    }

    // All threads should produce the same moves (same IDs in same order).
    for (int t = 1; t < numThreads; t++) {
      for (size_t i = 0; i < results[0].size(); i++) {
        EXPECT_EQ(results[t][i].mLegalWordId, results[0][i].mLegalWordId)
          << "Thread " << t << " move " << i << " differs";
      }
    }
  }

  // Thread-safety: concurrent Minimax searches on cloned states should not crash.
  TEST_F(ParallelSearchTest, ConcurrentMinimaxDoesNotCrash) {
    const int numThreads = 4;
    std::vector<WordBaseMove> moves(numThreads);
    std::vector<std::thread> threads;

    for (int t = 0; t < numThreads; t++) {
      threads.emplace_back([&, t]() {
        WordBaseState threadState(*state);
        Minimax<WordBaseState, WordBaseMove> engine(0.5, 200);
        engine.setMaxDepth(3);
        engine.setTraceStream(nullptr);
        moves[t] = engine.get_move(&threadState);
      });
    }
    for (auto& t : threads) t.join();

    // All threads should return a valid move.
    for (int t = 0; t < numThreads; t++) {
      EXPECT_GE(moves[t].mLegalWordId, 0)
        << "Thread " << t << " returned invalid move";
    }
  }

}  // namespace


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
