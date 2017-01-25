#ifndef BOARD_H
#define BOARD_H

#include <unordered_map>
#include <vector>

#include "grid.h"
#include "move-sequence.h"
#include "string-util.h"
#include "word-dictionary.h"

const int kBoardHeight = 13;
const int kBoardWidth = 10;

// A Wordbase board with no state.
class BoardStatic {
  std::unordered_map<int, std::vector<std::pair<std::string, MoveSequence>>> mValidWordPathsGrid;
  MoveSequence mBombs;
  MoveSequence mMegabombs;
  
 public:
  std::vector<char> mGrid;
  WordDictionary& mDictionary;
  
  // Create a new board.
  //
  // gridText is string representing a grid in row major order of height kBoardHeight and
  //   width kBoardWidth. They should be lower case characters. A * or a + before an item
  //   means bomb and megabomb.
  // dictionary 
  BoardStatic(const std::string& gridText, WordDictionary& dictionary) : mDictionary(dictionary) {
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
  std::string wordFromMove(const MoveSequence& move) {
    std::stringstream wordText;
    
    for (auto pathElement : move) {
      wordText << mGrid[pathElement.first * kBoardWidth + pathElement.second];
    }
    
    return wordText.str();
  }
  
  // Return all the valid words for the given grid square.
  const std::vector<std::pair<std::string, MoveSequence>>& findValidWordPaths(int y, int x) {
    int gridKey = y * kBoardWidth + x;
    auto pathsAtGridIterator = mValidWordPathsGrid.find(gridKey);
    
    if (pathsAtGridIterator == mValidWordPathsGrid.end()) {
      std::vector<std::pair<std::string, MoveSequence>> validWordPaths;
      
      findWordPaths(y, x, "", MoveSequence(), validWordPaths);
      pathsAtGridIterator = mValidWordPathsGrid.insert(
        std::pair<int, std::vector<std::pair<std::string, MoveSequence>>>(
          gridKey, validWordPaths)).first;
    }
    
    return pathsAtGridIterator->second;
  }
  
  // Adds all valid words from given grid square to a map.
  void findWordPaths(int y, int x, std::string prefix, MoveSequence prefixPath,
                     std::vector<std::pair<std::string, MoveSequence>>& validWordPaths) {
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
    prefixPath = MoveSequence(prefixPath);
    prefixPath.push_back(std::pair<int, int>(y, x));
    
    // Keep track of our word as (word, path) if it's a real word.
    if (mDictionary.hasWord(prefix)) {
      validWordPaths.push_back(std::pair<std::string, MoveSequence>(prefix, prefixPath));
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
  
  const MoveSequence& getBombs() const { return mBombs; }
  const MoveSequence& getMegabombs() const { return mMegabombs; }

};

#endif