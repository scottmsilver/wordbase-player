#ifndef BOARD_H
#define BOARD_H

#include <unordered_map>
#include <vector>

#include "coordinate-list.h"
#include "grid.h"
#include "string-util.h"
#include "word-dictionary.h"

const int kBoardHeight = 13;
const int kBoardWidth = 10;

typedef int LegalWordId;

struct LegalWord {
  LegalWordId mId;
  std::string mWord;
  CoordinateList mWordSequence;
  int mMaximizerGoodness;
  int mMinimizerGoodness;
};

typedef std::vector<int> LegalWordList;

class LegalWordFactory {
  int mNextId;
  std::vector<std::shared_ptr<LegalWord>> mLegalWordMap;
  std::unordered_map<CoordinateList, std::shared_ptr<LegalWord>> mCoordinateListMap;
  std::multimap<std::string, LegalWordId> mWordToLegalWordIds;
    
 public:
  LegalWordFactory() : mNextId(0) { }
  
  LegalWordId acquireWord(const CoordinateList& wordSequence, const std::string& word, int maximizerGoodness, int minimizerGoodness) {
    // Make sure this can be inserted (as in it was not already inserted)
    if (mCoordinateListMap.find(wordSequence) != mCoordinateListMap.end()) {
      throw;
    }

    std::shared_ptr<LegalWord> legalWord(new LegalWord({mNextId++, word, wordSequence, maximizerGoodness, minimizerGoodness}));

    mLegalWordMap.resize(legalWord->mId + 1);
    mLegalWordMap[legalWord->mId] = legalWord;
    mCoordinateListMap.insert(std::pair<CoordinateList, std::shared_ptr<LegalWord>>(wordSequence, legalWord));
    mWordToLegalWordIds.insert(std::pair<std::string, LegalWordId>(word, legalWord->mId));
    
    return legalWord->mId;
  }
  
  const LegalWord& getWord(LegalWordId id) const {
    if (id < mNextId) {
      return *mLegalWordMap[id];
    } else {
      throw;
    }
  }

  // Return the beginning and end of range of all LegalWordIds with this same word.
  std::pair<std::multimap<std::string, LegalWordId>::iterator, std::multimap<std::string, LegalWordId>::iterator> getLegalWordIds(const std::string& word) {
    return mWordToLegalWordIds.equal_range(word);
  }
      
 
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
    for (auto legalWord : legalWordsCopy) {
      legalWord->mMinimizerGoodness = number++;
    }

    std::sort(legalWordsCopy.begin(), legalWordsCopy.end(), Goodness2(true));
    number = 0;
    for (auto legalWord : legalWordsCopy) {
      legalWord->mMaximizerGoodness = number++;
    }
  }
  
  int getSize() const { return mNextId; }
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
    //mLegalWordFactory.renumberByGoodness();
  }

  const LegalWord& getLegalWord(int id) const {
    return mLegalWordFactory.getWord(id);
  }

  const LegalWord& getLegalWord(const CoordinateList& coordinateList) const {
    return mLegalWordFactory.getWord(coordinateList);
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
      int legalWordId = mLegalWordFactory.acquireWord(prefixPath, prefix, maximizerGoodness(prefixPath), minimizerGoodness(prefixPath));
      wordList.push_back(legalWordId);
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
