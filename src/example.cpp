#include <sstream>
#include <string>
#include <unordered_set>

#include "board.h"
#include "easylogging++.h"
#include "gtest/gtest.h"
#include "word-dictionary.h"

#include "wordescape.cpp"

INITIALIZE_EASYLOGGINGPP

namespace {

  const char* kReadmeBoard =
    "gregmiperslmavnetlaecaosrnowykosbrilfakosalagzl*eicveonredgmdamepumselomrtleipcradsndlnoihuiai*eoisatxerhctpteroustupsyalcopaeamhves";

  struct TestMove : public Move<TestMove> {
    int mId = 0;

    TestMove() = default;
    explicit TestMove(int id) : mId(id) {}

    void read() override {}
    std::ostream& to_stream(std::ostream& os) const override {
      os << "tm(" << mId << ")";
      return os;
    }
    bool operator==(const TestMove& rhs) const override { return mId == rhs.mId; }
    size_t hash() const override { return std::hash<int>()(mId); }
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
}  // namespace


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
