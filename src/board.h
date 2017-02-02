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

// A Wordbase board.
class BoardStatic {
  // A map, indexed by the "grid key" of all the valid word paths at the given square in the grid.
  // The key is a "grid key" which is y * kBoardWidth + x.
  // FIX-ME(ssilver): Change this to use a Grid<> type.
  std::unordered_map<int, std::vector<std::pair<std::string, CoordinateList>>> mValidWordPathsGrid;

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
      
      findWordPaths(y, x, "", CoordinateList(), validWordPaths);
      pathsAtGridIterator = mValidWordPathsGrid.insert(
        std::pair<int, std::vector<std::pair<std::string, CoordinateList>>>(
          gridKey, validWordPaths)).first;
    }
    
    return pathsAtGridIterator->second;
  }
  
  const CoordinateList& getBombs() const { return mBombs; }
  const CoordinateList& getMegabombs() const { return mMegabombs; }

 private:
  // Adds all valid words from given grid square to a map.
  void findWordPaths(int y, int x, std::string prefix, CoordinateList prefixPath,
                     std::vector<std::pair<std::string, CoordinateList>>& validWordPaths) {
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
    }
    
    // Vist all neighbors.
    findWordPaths(y - 1, x - 1, prefix, prefixPath, validWordPaths);
    findWordPaths(y - 1, x, prefix, prefixPath, validWordPaths);
    findWordPaths(y - 1, x + 1, prefix, prefixPath, validWordPaths);
    findWordPaths(y, x - 1, prefix, prefixPath, validWordPaths);
    findWordPaths(y, x + 1, prefix, prefixPath, validWordPaths);
    findWordPaths(y + 1, x - 1, prefix, prefixPath, validWordPaths);
    findWordPaths(y + 1, x, prefix, prefixPath, validWordPaths);
    findWordPaths(y + 1, x + 1, prefix, prefixPath, validWordPaths);
  }  
};

#endif
