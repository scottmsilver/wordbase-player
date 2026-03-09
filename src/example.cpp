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
}  // namespace


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
