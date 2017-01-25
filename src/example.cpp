#include <sstream>
#include <string>
#include <unordered_set>

#include "board.h"
#include "gtest/gtest.h"
#include "word-dictionary.h"

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

  // Tests that the Foo::Bar() method does Abc.
  TEST_F(FooTest, SimpleWordsAtGridSquare) {
    std::istringstream dictionaryFileContents(std::string("cao\n"));
    WordDictionary wd(dictionaryFileContents);
    BoardStatic board("caorsorbafal*sutseidnercbnolecavksidlvrtselruamasiuxigdbrsyngoenerhaneodrosmtsihlaltdymecrescehudndmnefingelermaeamoksbaoflbdecuhlg", wd);
    const std::vector<std::pair<std::string, MoveSequence>>& wordPaths = board.findValidWordPaths(0, 0);
    EXPECT_EQ(wordPaths.size(), 1);

    const MoveSequence& wordPath = wordPaths[0].second;

    EXPECT_EQ(wordPath[0].first, 0);
    EXPECT_EQ(wordPath[0].second, 0);
    EXPECT_EQ(wordPath[1].first, 0);
    EXPECT_EQ(wordPath[1].second, 1);
    EXPECT_EQ(wordPath[2].first, 0);
    EXPECT_EQ(wordPath[2].second, 2);
  }

  // Tests that Foo does Xyz.
  TEST_F(FooTest, DoesXyz) {
    // Exercises the Xyz feature of Foo.
  }

}  // namespace


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
