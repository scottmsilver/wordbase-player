#include <sstream>
#include <string>
#include <unordered_set>

#include "board.h"
#include "gtest/gtest.h"
#include "word-dictionary.h"

#include "wordescape.cpp"

namespace {

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
  TEST_F(FooTest, SimpleWordsAtGridSquare) {
    std::istringstream dictionaryFileContents(std::string("cao\n"));
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board("caorsorbafal*sutseidnercbnolecavksidlvrtselruamasiuxigdbrsyngoenerhaneodrosmtsihlaltdymecrescehudndmnefingelermaeamoksbaoflbdecuhlg", wd);

    const std::vector<std::pair<std::string, CoordinateList>>& wordPaths = board.findValidWordPaths(0, 0);
    EXPECT_EQ(wordPaths.size(), 1);
    EXPECT_EQ(wordPaths[0].first.compare("cao"), 0);
    EXPECT_EQ(wordPaths[0].second, CoordinateList({{0, 0}, {0, 1}, {0, 2}})); 
  }


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

    vector<WordBaseMove> moves = state.get_legal_moves2(10, NULL);
    EXPECT_EQ(moves.size(), 1);
    const LegalWord& legalWord2 = board.getLegalWord(moves[0].mLegalWordId);
    EXPECT_EQ(legalWord2.mWordSequence, CoordinateList({{0, 0}, {0, 1}, {0, 2}}));
  }
}  // namespace


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
