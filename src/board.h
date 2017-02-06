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
  int mHeuristicValue;
};

class LegalWordFactory {
  int mNextId;
  std::unordered_map<int, LegalWord> mLegalWordMap;
  std::unordered_map<CoordinateList, LegalWord> mCoordinateListMap;

 public:
  LegalWordFactory() : mNextId(0) { }
  
  LegalWordId acquireWord(const CoordinateList& wordSequence, const std::string& word, int heuristicValue) {
    // Make sure this can be inserted (as in it was not already inserted)
    if (mCoordinateListMap.find(wordSequence) != mCoordinateListMap.end()) {
      throw;
    }

    LegalWord legalWord = {mNextId++, word, wordSequence, heuristicValue};
    // FIX-ME we're making copies of this LegalWord, perhaps consider shared_ptr or that other boost-y thing for dealing with a singel table.
    mLegalWordMap.insert(std::pair<int, LegalWord>(legalWord.mId, legalWord));
    mCoordinateListMap.insert(std::pair<CoordinateList, LegalWord>(wordSequence, legalWord));

    return legalWord.mId;
  }
  
  const LegalWord& getWord(LegalWordId id) const {
    auto legalWordIterator = mLegalWordMap.find(id);
    if (legalWordIterator != mLegalWordMap.end()) {
      return legalWordIterator->second;
    } else {
      throw;
    }
  }

  const LegalWord& getWord(const CoordinateList& coordinateList) const {
    auto legalWordIterator = mCoordinateListMap.find(coordinateList);
    if (legalWordIterator != mCoordinateListMap.end()) {
      return legalWordIterator->second;
    } else {
      throw;
    }
  }
};

typedef std::vector<int> LegalWordList;

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
  }

  const LegalWord& getLegalWord(int id) const {
    return mLegalWordFactory.getWord(id);
  }

  const LegalWord& getLegalWord(const CoordinateList& coordinateList) const {
    return mLegalWordFactory.getWord(coordinateList);
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
    
    // If there exist no words > len(2) starting with this cut off the search.
    if (prefixPath.size() > 2 && !mDictionary.hasPrefix(prefix)) {
      return;
    }
    
    prefix = std::string(prefix) + mGrid[y * kBoardWidth + x];
    prefixPath = CoordinateList(prefixPath);
    prefixPath.push_back(std::pair<int, int>(y, x));


    // Keep track of our word as (word, path) if it's a real word.
    if (mDictionary.hasWord(prefix)) {
      validWordPaths.push_back(std::pair<std::string, CoordinateList>(prefix, prefixPath));
      int legalWordId = mLegalWordFactory.acquireWord(prefixPath, prefix, 100);
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
