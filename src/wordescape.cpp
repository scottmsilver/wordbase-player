/*
 wordescape.cpp
 Copyright (C) 2016 Scott Silver.
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <boost/dynamic_bitset.hpp>
#include <boost/sort/spreadsort/spreadsort.hpp>
#include <cstddef>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "board.h"
#include "grid.h"
#include "gtsa.hpp"
#include "obstack/obstack.hpp"
#include "string-util.h"
#include "wordbase-move.h"

const char PLAYER_1 = 1;
const char PLAYER_2 = 2;
const char PLAYER_UNOWNED = 0;
const char PLAYER_BOMB = 3;
const char PLAYER_MEGABOMB = 4;

const char kOwnerUnowned = 0;
const char kOwnerMaximizer = 1;
const char kOwnerMinimizer = 2;

boost::arena::obstack gObstack(1024*1024*1024);

// The storage representation of state of a Wordbase game; essentially a grid
// the correct size and initialized to Player 1 owns first row and Player 2 owns
// second row.
class WordBaseGridState : public Grid<char, kBoardHeight, kBoardWidth> {
public:
  // Initialize GridState by copying the underlying contents.
  WordBaseGridState(const WordBaseGridState& state) : Grid<char, kBoardHeight, kBoardWidth>(state) {}
  
  // Initialize GridState to an unused board.
  WordBaseGridState() : Grid() {
    fill(kOwnerUnowned);
    for (int x = 0; x < kBoardWidth; x++) {
      set(0, x, PLAYER_1);
      set(kBoardHeight - 1, x, PLAYER_2);
    }
  }
};

// Functor to help compare two moves on the basis of their heuristic score (aka Goodness)
struct Goodness {
  const BoardStatic& mBoard;
  char mPlayerToMove;
  
  Goodness(const BoardStatic& board, char playerToMove) : mBoard(board), mPlayerToMove(playerToMove) {}
  
  // Return true if the i is a better more than j.
  bool operator()(const WordBaseMove& i, const WordBaseMove& j) const {
    // FIX-ME this is suspect. Why does > give better values than <
    return heuristicValue(i) > heuristicValue(j);
  }
  
  // Return the heuristic value (aka Goodness) of this move.
  int heuristicValue(const WordBaseMove& x) const {
    return (mPlayerToMove == PLAYER_1) ? mBoard.getLegalWord(x.mLegalWordId).mMaximizerGoodness : mBoard.getLegalWord(x.mLegalWordId).mMinimizerGoodness;
  }
  
  // Used in conjunction with spreadsort. Right shift the value. See spreadshort for documentation.
  inline int operator()(const WordBaseMove & x, unsigned offset) const {
    return heuristicValue(x) >> offset;
  }
};

// Functor to help compare two moves on the basis of their heuristic score (aka Goodness)
struct Goodness2 {
  const BoardStatic& mBoard;
  char mPlayerToMove;
  
  Goodness2(const BoardStatic& board, char playerToMove) : mBoard(board), mPlayerToMove(playerToMove) {}
  
  // Return true if the i is a better more than j.
  bool operator()(const WordBaseMove& i, const WordBaseMove& j) const {
    return heuristicValue(i) < heuristicValue(j);
  }
  
  // Return the heuristic value (aka Goodness) of this move.
  int heuristicValue(const WordBaseMove& x) const {
    return (mPlayerToMove == PLAYER_1) ? mBoard.getLegalWord(x.mLegalWordId).mRenumberedMaximizerGoodness : mBoard.getLegalWord(x.mLegalWordId).mRenumberedMinimizerGoodness;
  }
  
  // Used in conjunction with spreadsort. Right shift the value. See spreadshort for documentation.
  inline int operator()(const WordBaseMove & x, unsigned offset) const {
    return heuristicValue(x) >> offset;
  }
};


// State of Wordbase game.
struct WordBaseState : public State<WordBaseState, WordBaseMove> {
  BoardStatic* mBoard;
  WordBaseGridState mState;
  std::vector<bool> mPlayedWords;
  
  WordBaseState(BoardStatic* board, char playerToMove) : State<WordBaseState, WordBaseMove>(playerToMove), mBoard(board), mPlayedWords(mBoard->getLegalWordsSize(), false) {
    putBomb(board->getBombs(), false);
    putBomb(board->getMegabombs(), true);
  }
  
  // Copy constructor.
  WordBaseState(const WordBaseState& rhs) :
  State<WordBaseState, WordBaseMove>(rhs.player_to_move),
  mBoard(rhs.mBoard),
  mState(rhs.mState), mPlayedWords(rhs.mPlayedWords) {
  }
  
  WordBaseState clone() const override {
    return WordBaseState(*this);
  }
  
  // Place bombs at each point in the supplied sequence.
  void putBomb(CoordinateList sequence, bool megaBomb) {
    for (auto&& bombPlace : sequence) {
      mState.set(bombPlace.first, bombPlace.second, megaBomb ? PLAYER_MEGABOMB : PLAYER_BOMB);
    }
  }
  
  // Return the value of this board state from the perspective of the given player.
  // In other words, it should be positive if player_to_move has an advantage.
  int get_goodness() const override {
    int h = 0;
    
    // First look for the terminal conditions.
    // FIX-ME combine this with is_terminal, with one that can say which player was terminal.
    for (int x = 0; x < kBoardWidth; x++) {
      if (mState.get(0, x) == PLAYER_2) {
        h = -INF;
        break;
      }
      
      if (mState.get(kBoardHeight - 1, x) == PLAYER_1) {
        assert(h != -INF);
        h = INF;
        break;
      }
    }
    
    // Since messing with h if h == INF will cause it to overflow, don't touch it.
    if (h != INF && h != -INF) {
      for (int y = 1; y < kBoardHeight - 1; y++) {
        for (int x = 0; x < kBoardWidth; x++) {
          char state = mState.get(y, x);
          if (state == PLAYER_1) {
            h += (y + 1) * (y + 1);
          } else if (state == PLAYER_2) {
            h += -1 * (y - kBoardHeight) * (y - kBoardHeight);
          }
        }
      }
    }
    
    int color = player_to_move == PLAYER_1 ? 1 : -1;
    
    return h * color;
  }
  
  // Return all the legal moves from this current state, only consider a maximum
  // of max_moves moves.
  std::vector<WordBaseMove> get_legal_moves(int max_moves = INF) const override {
    return get_legal_moves2(max_moves, NULL);
  }
  
  // Return a list of legal moves filtered by an optional filter and sorted
  // by most likely to be the "best" move.
  // filter must exactly match the found move, keeping in mind that the same
  // word may appear through multiple paths.
  // Also note that words which are already played are excluded.
  std::vector<WordBaseMove> get_legal_moves(int max_moves, const char* filter) const {
    // Maintain an ordered set, ordered by "goodness" which is a heuristic for whether
    // we think the move is likely to be very good.
    std::vector<WordBaseMove> moves;
    
    // For each letter owned by the current player find candidate words and filter
    // them appropriately.
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (mState.get(y, x) == player_to_move) {
          auto legalWords = mBoard->getLegalWords(y, x);
          for (auto&& legalWordId : legalWords) {
            // Ensure already played words are ignored.
            if (!mPlayedWords[legalWordId]) {
              if (filter == NULL || mBoard->getLegalWord(legalWordId).mWord.compare(filter) == 0) {
                moves.push_back(WordBaseMove(legalWordId));
              }
            }
          }
        }
      }
    }
    
    // Sort moves in order of Goodness. NB: This is probably the slowest part in traversing to the next
    // ply. So we have tried to heavily optimize this sort.
    boost::sort::spreadsort::integer_sort(moves.begin(), moves.end(), Goodness(*mBoard, player_to_move), Goodness(*mBoard, player_to_move));
    
    return moves;
  }
  
  // Return a list of legal moves filtered by an optional filter and sorted
  // by most likely to be the "best" move.
  // filter must exactly match the found move, keeping in mind that the same
  // word may appear through multiple paths.
  // Also note that words which are already played are excluded.
  std::vector<WordBaseMove> get_legal_moves2(int max_moves, const char* filter) const {
    // Initialize all the valid words to zero.
    boost::dynamic_bitset<> validWordBits(mBoard->getLegalWordsSize());

    // For each letter owned by the current player find candidate words OR them all together
    // They are implicitly in "order" so this effectively sorts them too.
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (mState.get(y, x) == player_to_move) {
          auto legalWords = mBoard->getLegalWords(y, x);
          validWordBits |= legalWords.wordBits(this->player_to_move == PLAYER_1);
        }
      }
    }

    std::vector<WordBaseMove> moves(validWordBits.count());

    int moveIndex = 0;
    // Iterate from set bit to set bit, each representing a legal word in our set.
    for (int renumberedGoodness = validWordBits.find_next(0); renumberedGoodness != boost::dynamic_bitset<>::npos; renumberedGoodness = validWordBits.find_next(renumberedGoodness)) {
      LegalWordId legalWordId = mBoard->getLegalWordIdFromRenumberedGoodness(renumberedGoodness, this->player_to_move == PLAYER_1);

      // Ensure already played words are ignored.
      if (!mPlayedWords[legalWordId]) {
        if (filter == NULL || mBoard->getLegalWord(legalWordId).mWord.compare(filter) == 0) {
          moves[moveIndex].mLegalWordId = legalWordId;
          moveIndex++;
        }
      }
    }
    
    moves.resize(moveIndex);
    return moves;
  }

  char get_enemy(char player) const override {
    return (player == PLAYER_1) ? PLAYER_2 : PLAYER_1;
  }
  
  bool is_terminal() const override {
    for (int x = 0; x < kBoardWidth; x++) {
      if (mState.get(0, x) == PLAYER_2) {
        return true;
      }
      
      if (mState.get(kBoardHeight - 1, x) == PLAYER_1) {
        return true;
      }
    }
    
    // FIX-ME probably need to handle case where there are no more moves to make.
    return false;
  }
  
  // FIX-ME combine with a combined is_terminal.
  bool is_winner(char player) const override {
    for (int x = 0; x < kBoardWidth; x++) {
      if (player == PLAYER_2) {
        if (mState.get(0, x) == PLAYER_2) {
          return true;
        } else if (mState.get(kBoardHeight - 1, x) == PLAYER_1) {
          return true;
        }
      }
    }
    
    return false;
  }
  
  // Record the claiming of a single grid square.
  // Deal with the impacts of bombs by recursing, as appropriate.
  void recordOne(int y, int x) {
    if (y < 0 || y >= kBoardHeight || x < 0 || x >= kBoardWidth) {
      return;
    }
    
    bool hadBomb = (mState.get(y, x) == PLAYER_BOMB);
    bool hadMegabomb = (mState.get(y, x) == PLAYER_MEGABOMB);
    
    mState.set(y, x, player_to_move);
    
    // A bomb causes the player to get the grid squares North, South, East and
    // West of this grid square.
    if (hadBomb) {
      recordOne(y - 1, x);
      recordOne(y + 1, x);
      recordOne(y, x - 1);
      recordOne(y, x + 1);
    } else if (hadMegabomb) {
      recordOne(y - 1, x);
      recordOne(y - 1, x + 1);
      recordOne(y - 1, x - 1);
      recordOne(y, x - 1);
      recordOne(y, x + 1);
      recordOne(y + 1, x);
      recordOne(y + 1, x + 1);
      recordOne(y + 1, x - 1);
    }
  }
  
  // Record a single move in the game. Iterates through each grid square, claiming that square.
  void recordMove(const WordBaseMove& move) {
    // The first letter of the word must be owend by the current player.
    const CoordinateList& wordSequence = mBoard->getLegalWord(move.mLegalWordId).mWordSequence;
    if (wordSequence.size() > 0) {
      assert(mState.get(wordSequence[0].first, wordSequence[0].second) == player_to_move);
    }
    
    // Record each letter.
    for (const auto& pathElement : wordSequence) {
      recordOne(pathElement.first, pathElement.second);
    }
    
    // Mark this word as played.
    std::pair<std::multimap<std::string, LegalWordId>::iterator, std::multimap<std::string, LegalWordId>::iterator> legalIdsForWord(mBoard->getLegalWordIds(mBoard->getLegalWord(move.mLegalWordId).mWord));
    for (auto i = legalIdsForWord.first; i != legalIdsForWord.second; ++i) {
      mPlayedWords[i->second] = true;
    }
  }
  
  // Starting at (y, x) mark all grid squares that are connected to (y, x).
  // A grid square is connected to (y, x) if it is eventually connected to (y, x) through
  // any path and has the same owner as (y, x).
  // Mark it by setting the the the high bit.
  //
  // This must be followed by a call to clearNotConnected().
  // FIX-ME(ssilver): We should probably change the protocol here so that it is not
  // possible to mark without clearing.
  void markConnected(int y, int x, char owner) {
    if (y < 0 || y >= kBoardHeight || x < 0 || x >= kBoardWidth) {
      return;
    }
    
    char visitedOwner = mState.get(y, x);
    if ((visitedOwner & 0x8) == 0 && owner == visitedOwner) {
      mState.set(y, x, visitedOwner | 0x8);
      
      markConnected(y - 1, x - 1, owner);
      markConnected(y - 1, x, owner);
      markConnected(y - 1, x + 1, owner);
      markConnected(y, x - 1, owner);
      markConnected(y, x + 1, owner);
      markConnected(y + 1, x - 1, owner);
      markConnected(y + 1, x, owner);      markConnected(y + 1, x + 1, owner);
    }
  }
  
  // Clear any square not connected after a call to markConnected().
  // This has the impact of removing ownership of a square that was
  // previously owned.
  void clearNotConnected() {
    // FIX-ME move this to regular iterator...
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        char owner = mState.get(y, x);
        
        if (owner & 0x8) {
          mState.set(y, x, owner & 0x7);
        } else if (owner == PLAYER_BOMB || owner == PLAYER_MEGABOMB) {
          // Skip this one. We didn't visit it so it's definitionally unowned.
        } else {
          mState.set(y, x, PLAYER_UNOWNED);
        }
      }
    }
  }
  
  // Make a move, change the current player to the other after doing this.
  void make_move(const WordBaseMove& move) override {
    // Make the move.
    recordMove(move);
    
    // Mark from all possible roots; the two sides of the board.
    for (int y : {0, kBoardHeight - 1}) {
      for (int x = 0; x < kBoardWidth; x++) {
        markConnected(y, x, mState.get(y, x));
      }
    }
    
    // Clear out any that we didn't reach (as in the move we made cut off another person's line)
    clearNotConnected();
    
    // Change to the new player.
    player_to_move = get_enemy(player_to_move);
  }
  
  std::ostream &to_stream(std::ostream &os) const override {
    return os;
  }
  
  bool operator==(const WordBaseState &other) const override {
    return mState == other.mState && mPlayedWords == other.mPlayedWords;
  }
  
  size_t hash() const override {
    return boost::hash_range(mState.begin(), mState.end());
  }
  
  const std::vector<std::string> getAlreadyPlayed() const {
    std::vector<std::string> alreadyPlayed;
    
    for (int curId = 0; curId < mPlayedWords.size(); curId++) {
      if (mPlayedWords[curId]) {
        alreadyPlayed.push_back(mBoard->getLegalWord(curId).mWord);
      }
    }
    
    return alreadyPlayed;
  }
  
  // Add words to the already played this; this is used for testing
  // or for joining games already in progress.
  void addAlreadyPlayed(const std::string& alreadyPlayed) {
    for (int curId = 0; curId < mPlayedWords.size(); curId++) {
      if (mBoard->getLegalWord(curId).mWord.compare(alreadyPlayed) == 0) {
        mPlayedWords[curId] = true;
      }
    }
  }
};


// Print out a move sequence.
std::ostream& operator<<(std::ostream& os, const CoordinateList& foo) {
  bool first = true;
  for (auto&& sequenceElement : foo) {
    if (!first) {
      os << ",";
    }
    
    os << "(" << sequenceElement.first << "," << sequenceElement.second << ")";
    first = false;
  }
  
  return os;
}

// Print out a static board.
std::ostream& operator<<(std::ostream& os, const BoardStatic & foo) {
  for (int y = 0; y < kBoardHeight; y++) {
    for (int x = 0; x < kBoardWidth; x++) {
      os << foo.mGrid[y * kBoardWidth + x];
    }
    os << std::endl;
  }
  return os;
}

// Return a character representing the owned state of a square.
char ownerText(char owner) {
  char ownerText;
  
  switch (owner) {
    case PLAYER_1:
    case PLAYER_2:
      ownerText = '.';
      break;
    case PLAYER_BOMB:
      ownerText = '*';
      break;
    case PLAYER_MEGABOMB:
      ownerText = '+';
      break;
    case PLAYER_UNOWNED:
      ownerText = ' ';
      break;
    default:
      ownerText = '?';
  }
  
  return ownerText;
}

// Print out the a WordBaseState.
std::ostream& operator<<(std::ostream& os, const WordBaseState& foo) {
  os << boost::format("player(%d): h=%d") % int(foo.player_to_move) % foo.get_goodness() << std::endl;
  
  os << "  ";
  for (int x = 0; x < kBoardWidth; x++) {
    os << boost::format("%2d") % x;
  }
  
  os << std::endl;
  
  for (int y = 0; y < kBoardHeight; y++) {
    os << boost::format("%2d") % y;
    for (int x = 0; x < kBoardWidth; x++) {
      char owner = foo.mState.get(y, x);
      char letter = foo.mBoard->mGrid[y * kBoardWidth + x];
      if (owner == PLAYER_1) {
        letter = std::toupper(letter);
      }
      os << boost::format("%c%c") % ownerText(owner) % letter;
    }
    os << std::endl;
  }
  
  return os;
}
