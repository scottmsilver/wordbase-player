#ifndef BOARD_H
#define BOARD_H

#include <boost/dynamic_bitset.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "coordinate-list.h"
#include "grid.h"
#include "string-util.h"
#include "word-dictionary.h"

const int kBoardHeight = 13;
const int kBoardWidth = 10;
const int kWordLengthWeight = 16;
const int kWordProgressWeight = 32;
const int kBombTouchWeight = 96;
const int kMegabombTouchWeight = 192;
// Reward paths that claim squares which are strong future anchors in this board's legal-word graph.
// Example: on the README board, owning squares along the "glamorize/glamorized/glamorizer/glamorizes"
// lane is better than owning a side path that mostly leaves short follow-ups like "glass".
const int kSquareWordCountDivisor = 1;
// Reward paths whose claimed squares unlock a broad set of distinct future starts, not just many overlapping ones.
// Example: a path that leaves you starts for {"stare", "stern", "sting", "stone"} is stronger than
// one that mostly repeats the same family like {"glamorize", "glamorized", "glamorizer", "glamorizes"}.
const int kFutureMoveDiversityDivisor = 2;
// Reward paths that preserve a broad set of long attacking follow-ups.
// Example: keeping starts for {"glamorize", "glamorized", "glamorizer", "glamorizes"} is usually
// more dangerous than keeping mostly short continuations like {"glass", "glam"}.
const int kLongFutureMoveLength = 8;
const int kLongFutureMoveDiversityDivisor = 4;
// Reward paths that run through squares whose surrounding words usually keep pushing toward the goal line.
// Example: a square that mostly sits inside deep lanes like "perilled" should beat one whose words stall in place,
// even if both squares participate in the same raw number of legal words.
const int kSquareForwardReachDivisor = 4;
// Reward squares that fan out into multiple goalward continuations, not just the single routine next step.
// Example: a "perilled" interior square that can branch into several deeper attacks should beat an "eel" pocket
// whose words mostly keep only one non-retreating continuation toward the opponent.
const int kSquareGoalwardBranchingDivisor = 2;

// One operation that we do a lot is merge LegalWordLists together - which correspond to different moves to evaluate.
// Since our goal is to evaluate moves in an order that prunes our search the most, we do some work up front so that
// we can take a set of LegalWordLists and merge them together and get a sorted list of LegalWords we wish to visit.
// Typically they are sorted by goodness/heuristic value, according to whether the maximizer or minimizer is playing.
//
// The straightforward way to do this is to merge LegalWordsLists together and sort along the way - turns out that in the
// "naive" way we build an array of possibilities and then sort that, using mostly a counting sort.
//
// However it turns out this merging can be done efficiently if we can use bit operations and borrow some clever
// principles from counting sort.
//
// So we do a kind of cool counting sort.
// Let's assume we have a unique and relatively small set of integers with max value V to sort.
//
// Let's sort as follows:
//   * Create a bit field where the kth value is on if k is in your set.
//   * To "sort" you walk through all the bits i from 0 to V and print i, and then your set is sorted.
//
// Algorithm analysis:
//   * O(V) to set the bits and O(V) to print the values, so it's O(V)
//
// And for our case, we are taking N lists and merging them in a sorted way. So the merge operation is very fast for us
// we just take two sets and bitwise or them (which we can get some nice speed ups from our processor...) - it's sub-linear.
//
// Since we want to sort by heuristic/goodness value our values will be the goodness values.
// And since these values have no bound - but the total number are knowable in the board, we calculate their
// "regular" Goodness Value (which is an int of size 32bits) and then we just sort them once and renumber from 0 to N, where N
// is total number of valid words in the board - typically about 50k.
// And then since when we're done we actually need the words and the their renumbered goodness values
// we have a bunch of bookkeeping to go from a goodness value back to the LegalWord.
typedef int LegalWordId;

// A unique legal word in a board.
struct LegalWord {
  LegalWordId mId;
  std::string mWord;
  CoordinateList mWordSequence;
  int mMaximizerGoodness;
  int mMinimizerGoodness;

  int mRenumberedMaximizerGoodness;
  int mRenumberedMinimizerGoodness;
};


// Maintains a set of LegalWords for a given board.
// Each LegalWord has unique id.
class LegalWordFactory {
  // Next (LegalWord)Id to allocate.
  int mNextId;
  
  // LegalWord indexed by LegalWordId. As in the word  LegalWordId 0 is the first entry in this vector.
  std::vector<std::shared_ptr<LegalWord>> mLegalWordMap;
  
  // Maps CoordinateList to a LegalWord.
  std::unordered_map<CoordinateList, std::shared_ptr<LegalWord>> mCoordinateListMap;
  
  // Maps string version of word to all all LegalWordIds for that word.
  std::multimap<std::string, LegalWordId> mWordToLegalWordIds;
  
  // Maps renumbered minimizer value to a LegalWord.
  std::vector<LegalWordId> mRenumberedMinimizerValueToLegalWord;

  // Maps renumbered maximizer value to a LegalWord.
  std::vector<LegalWordId> mRenumberedMaximizerValueToLegalWord;

  // For each legal word id, store all legal word ids with the same text.
  std::vector<std::vector<LegalWordId> > mEquivalentWordIds;

public:
  LegalWordFactory() : mNextId(0) { }
  
  // Returns a unique LegalWordId for the passed in instance of the word at wordSequence or throws if it has already seen that wordSequence.
  //
  // maximizerGoodness and minimizerGoodness are the heuristic values of this word from the perspective of the maximizer
  // and mi
  const LegalWord& acquireWord(const CoordinateList& wordSequence, const std::string& word, int maximizerGoodness, int minimizerGoodness) {
    // The same instance of a word can only exist once.
    if (mCoordinateListMap.find(wordSequence) != mCoordinateListMap.end()) {
      throw;
    }
    
    std::shared_ptr<LegalWord> legalWord(new LegalWord({mNextId++, word, wordSequence, maximizerGoodness, minimizerGoodness, 0, 0}));
    
    mLegalWordMap.resize(legalWord->mId + 1);
    mEquivalentWordIds.resize(legalWord->mId + 1);
    mLegalWordMap[legalWord->mId] = legalWord;
    mCoordinateListMap.insert(std::pair<CoordinateList, std::shared_ptr<LegalWord>>(wordSequence, legalWord));
    mWordToLegalWordIds.insert(std::pair<std::string, LegalWordId>(word, legalWord->mId));
    
    return *legalWord;
  }
  
  // Return the LegalWord for the given id.
  const LegalWord& getWord(LegalWordId id) const {
    if (id < mNextId && id >= 0) {
      return *mLegalWordMap[id];
    } else {
      throw;
    }
  }

  LegalWord& mutableWord(LegalWordId id) {
    if (id < mNextId && id >= 0) {
      return *mLegalWordMap[id];
    } else {
      throw;
    }
  }
  
  // Return the beginning and end of range of all LegalWordIds with this same word.
  // FIX-ME(ssilver): We should be returning const ranges.
  std::pair<std::multimap<std::string, LegalWordId>::iterator, std::multimap<std::string, LegalWordId>::iterator> getLegalWordIds(const std::string& word) {
    return mWordToLegalWordIds.equal_range(word);
  }

  void finalizeEquivalentWordIds() {
    for (auto i = mWordToLegalWordIds.begin(); i != mWordToLegalWordIds.end();) {
      const std::pair<std::multimap<std::string, LegalWordId>::iterator, std::multimap<std::string, LegalWordId>::iterator> sameWordRange =
        mWordToLegalWordIds.equal_range(i->first);

      std::vector<LegalWordId> equivalentIds;
      for (auto j = sameWordRange.first; j != sameWordRange.second; ++j) {
        equivalentIds.push_back(j->second);
      }
      for (auto legalWordId : equivalentIds) {
        mEquivalentWordIds[legalWordId] = equivalentIds;
      }

      i = sameWordRange.second;
    }
  }

  const std::vector<LegalWordId>& getEquivalentWordIds(LegalWordId id) const {
    return mEquivalentWordIds[id];
  }
  
  // Returns the LegalWord associated at coordinateList or throws.
  const LegalWord& getWord(const CoordinateList& coordinateList) const {
    auto legalWordIterator = mCoordinateListMap.find(coordinateList);
    if (legalWordIterator != mCoordinateListMap.end()) {
      return *legalWordIterator->second;
    } else {
      throw;
    }
  }
  
  struct Goodness2 {
    bool mIsMaximizer;
    
    Goodness2(bool isMaximizer) : mIsMaximizer(isMaximizer) { }
    
    bool operator()(const std::shared_ptr<LegalWord>& i, const std::shared_ptr<LegalWord>& j) const {
      return heuristicValue(i) > heuristicValue(j);
    }
    
    int heuristicValue(const std::shared_ptr<LegalWord>& x) const {
      return (mIsMaximizer) ? x->mMaximizerGoodness : x->mMinimizerGoodness;
    }
    
    // Used in conjunction with spreadsort. Right shift the value
    inline int operator()(const std::shared_ptr<LegalWord>& x, unsigned offset) const {
      return heuristicValue(x) >> offset;
    }
  };
  
  void renumberByGoodness() {
    // Make a copy.
    std::vector<std::shared_ptr<LegalWord>> legalWordsCopy(mLegalWordMap);
    
    // Sort by heuristic.
    std::sort(legalWordsCopy.begin(), legalWordsCopy.end(), Goodness2(false));
    
    // Renumber.
    int number = 0;
    mRenumberedMinimizerValueToLegalWord.resize(legalWordsCopy.size());
    for (auto legalWord : legalWordsCopy) {
      legalWord->mRenumberedMinimizerGoodness = number++;
      mRenumberedMinimizerValueToLegalWord[legalWord->mRenumberedMinimizerGoodness] = legalWord->mId;
    }
    
    std::sort(legalWordsCopy.begin(), legalWordsCopy.end(), Goodness2(true));
    number = 0;
    mRenumberedMaximizerValueToLegalWord.resize(legalWordsCopy.size());
    for (auto legalWord : legalWordsCopy) {
      legalWord->mRenumberedMaximizerGoodness = number++;
      mRenumberedMaximizerValueToLegalWord[legalWord->mRenumberedMaximizerGoodness] = legalWord->mId;
    }
  }
  
  const LegalWordId getLegalWordFromRenumberedGoodness(int goodness, bool isMaximizer) const {
    auto& goodnessToLegalWordMap = isMaximizer ? mRenumberedMaximizerValueToLegalWord : mRenumberedMinimizerValueToLegalWord;
    assert(goodness < goodnessToLegalWordMap.size());
    return goodnessToLegalWordMap[goodness];
  }
  
  int getSize() const { return mNextId; }
};


// A list of legal words, typically for a position in the board.
class LegalWordList  {
private:
  // The kth bit is set for all k where k is the set of renumbered (unique) minimizer (or maximizer) goodness values for each legal word.
  // In other words, if the word at (0,0),(1,0),(2,0) had a renumber minimizer (or maximizer) goodness value of 3, then the 3rd bit in the set
  // would be set.
  boost::dynamic_bitset<> mMinimizerWordIdBits;
  boost::dynamic_bitset<> mMaximizerWordIdBits;

  // The set of LegalWordIds at this position.
  std::vector<int> mLegalWordIds;
  
public:
  LegalWordList() : mMinimizerWordIdBits(0), mMaximizerWordIdBits(0) {
  }

  std::vector<int>::const_iterator begin() const { return mLegalWordIds.begin(); }
  std::vector<int>::const_iterator end() const { return mLegalWordIds.end(); }
  size_t size() const { return mLegalWordIds.size(); }
  LegalWordId operator[](size_t index) const { return mLegalWordIds[index]; }

  const boost::dynamic_bitset<>& wordBits(bool isMaximizer) const {
    return isMaximizer ? mMaximizerWordIdBits : mMinimizerWordIdBits;
  }

  void updateRenumberedGoodnessBits(int renumberedMaximizerGoodness, int renumberedMinimizerGoodness, int maxBits) {
    mMinimizerWordIdBits.resize(maxBits);
    mMaximizerWordIdBits.resize(maxBits);
    mMaximizerWordIdBits[renumberedMaximizerGoodness] = 1;
    mMinimizerWordIdBits[renumberedMinimizerGoodness] = 1;
  }
  
  void push_back(LegalWordId legalWordId) {
    mLegalWordIds.push_back(legalWordId);
  }
};

// A Wordbase board.
class BoardStatic {
  // A map, indexed by the "grid key" of all the valid word paths at the given square in the grid.
  // The key is a "grid key" which is y * kBoardWidth + x.
  // FIX-ME(ssilver): Change this to use a Grid<> type.
  std::unordered_map<int, std::vector<std::pair<std::string, CoordinateList>>> mValidWordPathsGrid;
  LegalWordFactory mLegalWordFactory;
  Grid<LegalWordList, kBoardHeight, kBoardWidth> mLegalWords;
  
  // The location of the bombs on the board; each entry in the CoordinateList is the location of a bomb.
  CoordinateList mBombs;
  
  // The location of the mega-bombs on the board; each entry in the CoordinateList is the location of a mega-bomb.
  CoordinateList mMegabombs;

  Grid<int, kBoardHeight, kBoardWidth> mSquareWordCounts;
  Grid<int, kBoardHeight, kBoardWidth> mMaximizerSquareForwardReach;
  Grid<int, kBoardHeight, kBoardWidth> mMinimizerSquareForwardReach;
  Grid<int, kBoardHeight, kBoardWidth> mMaximizerSquareGoalwardBranching;
  Grid<int, kBoardHeight, kBoardWidth> mMinimizerSquareGoalwardBranching;
  std::vector<int> mLegalWordScratchGeneration;
  int mCurrentScratchGeneration;
  
public:
  std::vector<char> mGrid;
  const WordDictionary& mDictionary;
  
  // Create a new board.
  //
  // gridText is string representing a grid in row major order of height kBoardHeight and
  //   width kBoardWidth. They should be lower case characters. A * or a + before an item
  //   means bomb and megabomb.
  // dictionary
  BoardStatic(const std::string& gridText, const WordDictionary& dictionary) : mDictionary(dictionary), mCurrentScratchGeneration(0) {
    // Build a new grid from the string, ignore spaces.
    int y = 0;
    int x = 0;
    for (int i = 0; i < gridText.length(); i++) {
      if (gridText[i] == '*') {
        mBombs.push_back(std::pair<int, int>(y, x));
      } else if (gridText[i] == '+') {
        mMegabombs.push_back(std::pair<int, int>(y, x));
      } else if (gridText[i] != ' ') {
        mGrid.push_back(gridText[i]);
        x++;
        if (x == kBoardWidth) {
          x = 0;
          y++;
        }
      }
    }
    
    if (mGrid.size() != kBoardHeight * kBoardWidth) {
      throw;
    }
    
    findLegalWordsForGrid();
    initializeSquareWordCounts();
    initializeSquareForwardReach();
    initializeSquareGoalwardBranching();
    mLegalWordScratchGeneration.assign(mLegalWordFactory.getSize(), 0);
    recomputeLegalWordGoodness();
    mLegalWordFactory.finalizeEquivalentWordIds();
    mLegalWordFactory.renumberByGoodness();
    
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        LegalWordList& legalWordList = mLegalWords.get(y, x);
        for (auto legalWordId : legalWordList) {
          const LegalWord& legalWord = mLegalWordFactory.getWord(legalWordId);
          legalWordList.updateRenumberedGoodnessBits(legalWord.mRenumberedMaximizerGoodness, legalWord.mRenumberedMinimizerGoodness, mLegalWordFactory.getSize());
        }
      }
    }
  }
  
  const LegalWord& getLegalWord(int id) const {
    return mLegalWordFactory.getWord(id);
  }
  
  const LegalWord& getLegalWord(const CoordinateList& coordinateList) const {
    return mLegalWordFactory.getWord(coordinateList);
  }
  
  const LegalWordId getLegalWordIdFromRenumberedGoodness(int goodness, bool isMaximizer) const {
    return mLegalWordFactory.getLegalWordFromRenumberedGoodness(goodness, isMaximizer);
  }
  
  int getLegalWordsSize() { return mLegalWordFactory.getSize(); }
  
  // Return the beginning and end of range of all LegalWordIds with this same word.
  std::pair<std::multimap<std::string, LegalWordId>::iterator, std::multimap<std::string, LegalWordId>::iterator> getLegalWordIds(const std::string& word) {
    return mLegalWordFactory.getLegalWordIds(word);
  }

  const std::vector<LegalWordId>& getEquivalentLegalWordIds(LegalWordId legalWordId) const {
    return mLegalWordFactory.getEquivalentWordIds(legalWordId);
  }
  
  // Return the word represented by the passed in sequence.
  std::string wordFromMove(const CoordinateList& move) {
    std::stringstream wordText;
    
    for (auto pathElement : move) {
      wordText << mGrid[pathElement.first * kBoardWidth + pathElement.second];
    }
    
    return wordText.str();
  }
  
  // Return all the valid words for the given grid square.
  const std::vector<std::pair<std::string, CoordinateList>>& findValidWordPaths(int y, int x) {
    int gridKey = y * kBoardWidth + x;
    auto pathsAtGridIterator = mValidWordPathsGrid.find(gridKey);
    
    if (pathsAtGridIterator == mValidWordPathsGrid.end()) {
      std::vector<std::pair<std::string, CoordinateList>> validWordPaths;
      LegalWordList wordList;
      
      findWordPaths(y, x, "", CoordinateList(), validWordPaths, wordList);
      pathsAtGridIterator = mValidWordPathsGrid.insert(
                                                       std::pair<int, std::vector<std::pair<std::string, CoordinateList>>>(
                                                                                                                           gridKey, validWordPaths)).first;
      mLegalWords.set(y, x, wordList);
    }
    
    return pathsAtGridIterator->second;
  }
  
  void findLegalWordsForGrid() {
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        findValidWordPaths(y, x);
      }
    }
  }
  
  const LegalWordList& getLegalWords(int y, int x) const {
    return mLegalWords.get(y, x);
  }
  
  const CoordinateList& getBombs() const { return mBombs; }
  const CoordinateList& getMegabombs() const { return mMegabombs; }
  
private:
  void initializeSquareWordCounts() {
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        const LegalWordList& legalWords = mLegalWords.get(y, x);
        mSquareWordCounts.set(y, x, static_cast<int>(legalWords.size()));
      }
    }
  }

  void initializeSquareForwardReach() {
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        const LegalWordList& legalWords = mLegalWords.get(y, x);
        int maximizerReachTotal = 0;
        int minimizerReachTotal = 0;
        for (auto legalWordId : legalWords) {
          const LegalWord& legalWord = mLegalWordFactory.getWord(legalWordId);
          int furthestRow = 0;
          int nearestRow = kBoardHeight - 1;
          for (const auto& cell : legalWord.mWordSequence) {
            furthestRow = std::max(furthestRow, cell.first);
            nearestRow = std::min(nearestRow, cell.first);
          }
          maximizerReachTotal += furthestRow;
          minimizerReachTotal += (kBoardHeight - 1 - nearestRow);
        }

        const int legalWordCount = static_cast<int>(legalWords.size());
        mMaximizerSquareForwardReach.set(y, x, legalWordCount == 0 ? 0 : maximizerReachTotal / legalWordCount);
        mMinimizerSquareForwardReach.set(y, x, legalWordCount == 0 ? 0 : minimizerReachTotal / legalWordCount);
      }
    }
  }

  void initializeSquareGoalwardBranching() {
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        const LegalWordList& legalWords = mLegalWords.get(y, x);
        std::vector<int> maximizerNextSquares;
        std::vector<int> minimizerNextSquares;
        for (auto legalWordId : legalWords) {
          const LegalWord& legalWord = mLegalWordFactory.getWord(legalWordId);
          const CoordinateList& wordSequence = legalWord.mWordSequence;
          for (size_t index = 0; index < wordSequence.size(); ++index) {
            if (wordSequence[index].first != y || wordSequence[index].second != x) {
              continue;
            }

            if (index + 1 < wordSequence.size()) {
              const std::pair<int, int>& nextCell = wordSequence[index + 1];
              if (nextCell.first >= y) {
                maximizerNextSquares.push_back(nextCell.first * kBoardWidth + nextCell.second);
              }
              if (nextCell.first <= y) {
                minimizerNextSquares.push_back(nextCell.first * kBoardWidth + nextCell.second);
              }
            }
          }
        }

        std::sort(maximizerNextSquares.begin(), maximizerNextSquares.end());
        maximizerNextSquares.erase(std::unique(maximizerNextSquares.begin(), maximizerNextSquares.end()), maximizerNextSquares.end());
        std::sort(minimizerNextSquares.begin(), minimizerNextSquares.end());
        minimizerNextSquares.erase(std::unique(minimizerNextSquares.begin(), minimizerNextSquares.end()), minimizerNextSquares.end());

        // Count only excess fanout beyond the first goalward continuation so ordinary single-lane squares stay neutral.
        mMaximizerSquareGoalwardBranching.set(y, x, std::max(0, static_cast<int>(maximizerNextSquares.size()) - 1));
        mMinimizerSquareGoalwardBranching.set(y, x, std::max(0, static_cast<int>(minimizerNextSquares.size()) - 1));
      }
    }
  }

  void recomputeLegalWordGoodness() {
    for (LegalWordId legalWordId = 0; legalWordId < mLegalWordFactory.getSize(); ++legalWordId) {
      LegalWord& legalWord = mLegalWordFactory.mutableWord(legalWordId);
      legalWord.mMaximizerGoodness = maximizerGoodness(legalWord.mWordSequence);
      legalWord.mMinimizerGoodness = minimizerGoodness(legalWord.mWordSequence);
      // Two paths can touch similarly "good" squares but leave very different follow-up move sets.
      // For example, one path may preserve starts for "stare", "stern", "sting", and "stone",
      // while another mainly keeps variants of the same stem such as "glamorized"/"glamorizer".
      const int diversityBonus = futureMoveDiversityBonus(legalWord.mWordSequence);
      // Long follow-ups are rarer and more threatening, so reward broad long-word continuation sets separately.
      const int longDiversityBonus = longFutureMoveDiversityBonus(legalWord.mWordSequence);
      legalWord.mMaximizerGoodness += diversityBonus;
      legalWord.mMinimizerGoodness += diversityBonus;
      legalWord.mMaximizerGoodness += longDiversityBonus;
      legalWord.mMinimizerGoodness += longDiversityBonus;
    }
  }

  bool pathTouches(const CoordinateList& wordSequence, const CoordinateList& targets) const {
    for (const auto& cell : wordSequence) {
      if (std::find(targets.begin(), targets.end(), cell) != targets.end()) {
        return true;
      }
    }
    return false;
  }

  int squareWordCountBonus(const CoordinateList& wordSequence) const {
    int bonus = 0;
    for (const auto& cell : wordSequence) {
      bonus += mSquareWordCounts.get(cell.first, cell.second);
    }
    return bonus / kSquareWordCountDivisor;
  }

  int squareForwardReachBonus(const CoordinateList& wordSequence, bool isMaximizer) const {
    int bonus = 0;
    const Grid<int, kBoardHeight, kBoardWidth>& squareForwardReach =
      isMaximizer ? mMaximizerSquareForwardReach : mMinimizerSquareForwardReach;
    for (const auto& cell : wordSequence) {
      bonus += squareForwardReach.get(cell.first, cell.second);
    }
    return bonus / kSquareForwardReachDivisor;
  }

  int squareGoalwardBranchingBonus(const CoordinateList& wordSequence, bool isMaximizer) const {
    int bonus = 0;
    const Grid<int, kBoardHeight, kBoardWidth>& squareGoalwardBranching =
      isMaximizer ? mMaximizerSquareGoalwardBranching : mMinimizerSquareGoalwardBranching;
    for (const auto& cell : wordSequence) {
      bonus += squareGoalwardBranching.get(cell.first, cell.second);
    }
    return bonus / kSquareGoalwardBranchingDivisor;
  }

  int futureMoveDiversityBonus(const CoordinateList& wordSequence) {
    // This is build-time work only: estimate how many distinct future starts this path unlocks.
    // Example: if the claimed squares leave both "stare" and "stone" available next turn, that is
    // better than leaving only the "glamorize" family, even if both paths look similarly advanced.
    ++mCurrentScratchGeneration;
    if (mCurrentScratchGeneration == 0) {
      std::fill(mLegalWordScratchGeneration.begin(), mLegalWordScratchGeneration.end(), 0);
      mCurrentScratchGeneration = 1;
    }

    int uniqueMoves = 0;
    for (const auto& cell : wordSequence) {
      const LegalWordList& legalWordList = mLegalWords.get(cell.first, cell.second);
      for (auto legalWordId : legalWordList) {
        if (mLegalWordScratchGeneration[legalWordId] != mCurrentScratchGeneration) {
          mLegalWordScratchGeneration[legalWordId] = mCurrentScratchGeneration;
          ++uniqueMoves;
        }
      }
    }

    return uniqueMoves / kFutureMoveDiversityDivisor;
  }

  int longFutureMoveDiversityBonus(const CoordinateList& wordSequence) {
    // Count only distinct long continuations so we do not overvalue paths that mostly preserve short cleanup words.
    ++mCurrentScratchGeneration;
    if (mCurrentScratchGeneration == 0) {
      std::fill(mLegalWordScratchGeneration.begin(), mLegalWordScratchGeneration.end(), 0);
      mCurrentScratchGeneration = 1;
    }

    int uniqueLongMoves = 0;
    for (const auto& cell : wordSequence) {
      const LegalWordList& legalWordList = mLegalWords.get(cell.first, cell.second);
      for (auto legalWordId : legalWordList) {
        const LegalWord& legalWord = mLegalWordFactory.getWord(legalWordId);
        if (legalWord.mWordSequence.size() < kLongFutureMoveLength) {
          continue;
        }
        if (mLegalWordScratchGeneration[legalWordId] != mCurrentScratchGeneration) {
          mLegalWordScratchGeneration[legalWordId] = mCurrentScratchGeneration;
          ++uniqueLongMoves;
        }
      }
    }

    return uniqueLongMoves / kLongFutureMoveDiversityDivisor;
  }
  int maximizerGoodness(const CoordinateList& wordSequence) const {
    int maximizerGoodness = 0;
    int furthestRow = 0;
    for (auto x : wordSequence) {
      maximizerGoodness += (x.first + 1) * (x.first + 1);
      furthestRow = std::max(furthestRow, x.first);
    }

    maximizerGoodness += static_cast<int>(wordSequence.size()) * kWordLengthWeight;
    maximizerGoodness += furthestRow * kWordProgressWeight;
    if (pathTouches(wordSequence, mBombs)) {
      maximizerGoodness += kBombTouchWeight;
    }
    if (pathTouches(wordSequence, mMegabombs)) {
      maximizerGoodness += kMegabombTouchWeight;
    }
    maximizerGoodness += squareWordCountBonus(wordSequence);
    maximizerGoodness += squareForwardReachBonus(wordSequence, true);
    maximizerGoodness += squareGoalwardBranchingBonus(wordSequence, true);

    return maximizerGoodness;
  }
  
  int minimizerGoodness(const CoordinateList& moveSequence) const {
    int minimizerGoodness = 0;
    int furthestRow = kBoardHeight - 1;
    for (auto x : moveSequence) {
      minimizerGoodness += (x.first - kBoardHeight) * (x.first - kBoardHeight);
      furthestRow = std::min(furthestRow, x.first);
    }

    minimizerGoodness += static_cast<int>(moveSequence.size()) * kWordLengthWeight;
    minimizerGoodness += (kBoardHeight - 1 - furthestRow) * kWordProgressWeight;
    if (pathTouches(moveSequence, mBombs)) {
      minimizerGoodness += kBombTouchWeight;
    }
    if (pathTouches(moveSequence, mMegabombs)) {
      minimizerGoodness += kMegabombTouchWeight;
    }
    minimizerGoodness += squareWordCountBonus(moveSequence);
    minimizerGoodness += squareForwardReachBonus(moveSequence, false);
    minimizerGoodness += squareGoalwardBranchingBonus(moveSequence, false);

    return minimizerGoodness;
  }
  
  
  // Adds all valid words from given grid square to a map.
  void findWordPaths(int y, int x, std::string prefix, CoordinateList prefixPath,
                     std::vector<std::pair<std::string, CoordinateList>>& validWordPaths, LegalWordList& wordList) {
    if (y < 0 || y >= kBoardHeight || x < 0 || x >= kBoardWidth) {
      return;
    }
    
    // If (y,x) already in this prefix cut off the search, since a grid square
    // can only be used once.
    if (std::find(prefixPath.begin(), prefixPath.end(),
                  std::pair<int, int>(y, x)) != prefixPath.end()) {
      return;
    }
    
    // If there exist no words > word is of length 2 or more.
    if (prefixPath.size() >= 2 && !mDictionary.hasPrefix(prefix)) {
      return;
    }
    
    prefix = std::string(prefix) + mGrid[y * kBoardWidth + x];
    prefixPath = CoordinateList(prefixPath);
    prefixPath.push_back(std::pair<int, int>(y, x));
    
    
    // Keep track of our word as (word, path) if it's a real word.
    if (mDictionary.hasWord(prefix)) {
      validWordPaths.push_back(std::pair<std::string, CoordinateList>(prefix, prefixPath));
      wordList.push_back(mLegalWordFactory.acquireWord(prefixPath, prefix, maximizerGoodness(prefixPath), minimizerGoodness(prefixPath)).mId);
    }
    
    // Vist all neighbors.
    findWordPaths(y - 1, x - 1, prefix, prefixPath, validWordPaths, wordList);
    findWordPaths(y - 1, x, prefix, prefixPath, validWordPaths, wordList);
    findWordPaths(y - 1, x + 1, prefix, prefixPath, validWordPaths, wordList);
    findWordPaths(y, x - 1, prefix, prefixPath, validWordPaths, wordList);
    findWordPaths(y, x + 1, prefix, prefixPath, validWordPaths, wordList);
    findWordPaths(y + 1, x - 1, prefix, prefixPath, validWordPaths, wordList);
    findWordPaths(y + 1, x, prefix, prefixPath, validWordPaths, wordList);
    findWordPaths(y + 1, x + 1, prefix, prefixPath, validWordPaths, wordList);
  }
};

#endif
