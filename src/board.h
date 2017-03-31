#ifndef BOARD_H
#define BOARD_H

#include <boost/dynamic_bitset.hpp>

#include <unordered_map>
#include <vector>

#include "coordinate-list.h"
#include "grid.h"
#include "string-util.h"
#include "word-dictionary.h"

const int kBoardHeight = 13;
const int kBoardWidth = 10;

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
  
  // Return the beginning and end of range of all LegalWordIds with this same word.
  // FIX-ME(ssilver): We should be returning const ranges.
  std::pair<std::multimap<std::string, LegalWordId>::iterator, std::multimap<std::string, LegalWordId>::iterator> getLegalWordIds(const std::string& word) {
    return mWordToLegalWordIds.equal_range(word);
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
  
public:
  std::vector<char> mGrid;
  const WordDictionary& mDictionary;
  
  // Create a new board.
  //
  // gridText is string representing a grid in row major order of height kBoardHeight and
  //   width kBoardWidth. They should be lower case characters. A * or a + before an item
  //   means bomb and megabomb.
  // dictionary
  BoardStatic(const std::string& gridText, const WordDictionary& dictionary) : mDictionary(dictionary) {
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
  int maximizerGoodness(const CoordinateList& wordSequence) const {
    int maximizerGoodness = 0;
    for (auto x : wordSequence) {
      maximizerGoodness += (x.first + 1) * (x.first + 1);
    }
    
    return maximizerGoodness;
  }
  
  int minimizerGoodness(const CoordinateList& moveSequence) const {
    int minimizerGoodness = 0;
    for (auto x : moveSequence) {
      minimizerGoodness += (x.first - kBoardHeight) * (x.first - kBoardHeight);
    }
    
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
