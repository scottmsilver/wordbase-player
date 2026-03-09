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
#include <boost/format.hpp>
#include <boost/functional/hash.hpp>
#include <boost/sort/spreadsort/spreadsort.hpp>
#include <cstddef>
#include <cstdint>
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
// Fixed-capacity bitset that avoids heap allocation. Max 8192 bits (1KB).
struct InlineBitset {
  static constexpr int kMaxWords = 128;  // 128 * 64 = 8192 bits
  uint64_t words[kMaxWords];
  int size_bits;

  InlineBitset() : size_bits(0) { memset(words, 0, sizeof(words)); }
  explicit InlineBitset(int n) : size_bits(n) { memset(words, 0, sizeof(words)); }

  InlineBitset(const InlineBitset& rhs) : size_bits(rhs.size_bits) {
    int nwords = (size_bits + 63) >> 6;
    memcpy(words, rhs.words, nwords * 8);
  }
  InlineBitset& operator=(const InlineBitset& rhs) {
    size_bits = rhs.size_bits;
    int nwords = (size_bits + 63) >> 6;
    memcpy(words, rhs.words, nwords * 8);
    return *this;
  }

  bool operator[](int i) const { return (words[i >> 6] >> (i & 63)) & 1; }
  void set(int i, bool v) {
    if (v) words[i >> 6] |= (1ULL << (i & 63));
    else words[i >> 6] &= ~(1ULL << (i & 63));
  }
  int size() const { return size_bits; }
  bool operator==(const InlineBitset& rhs) const {
    if (size_bits != rhs.size_bits) return false;
    int nwords = (size_bits + 63) >> 6;
    return memcmp(words, rhs.words, nwords * 8) == 0;
  }
};

struct WordBaseState : public State<WordBaseState, WordBaseMove> {
  BoardStatic* mBoard;
  WordBaseGridState mState;
  InlineBitset mPlayedWords;
  size_t mHashValue;
  uint64_t mTtVerificationKey;
  bool mTookEnemyCell;

  WordBaseState(BoardStatic* board, char playerToMove)
    : State<WordBaseState, WordBaseMove>(playerToMove),
      mBoard(board),
      mPlayedWords(mBoard->getLegalWordsSize()),
      mHashValue(0),
      mTtVerificationKey(0),
      mTookEnemyCell(false) {
    putBomb(board->getBombs(), false);
    putBomb(board->getMegabombs(), true);
    mHashValue = computeHashFromState();
    mTtVerificationKey = computeVerificationKeyFromState();
  }

  // Copy constructor.
  WordBaseState(const WordBaseState& rhs) :
  State<WordBaseState, WordBaseMove>(rhs.player_to_move),
  mBoard(rhs.mBoard),
  mState(rhs.mState), mPlayedWords(rhs.mPlayedWords), mHashValue(rhs.mHashValue), mTtVerificationKey(rhs.mTtVerificationKey), mTookEnemyCell(false) {
  }

  WordBaseState clone() const override {
    return WordBaseState(*this);
  }

  const WordBaseGridState& getGridState() const { return mState; }
  const BoardStatic& getBoardStatic() const { return *mBoard; }

  // Place bombs at each point in the supplied sequence.
  void putBomb(CoordinateList sequence, bool megaBomb) {
    for (auto&& bombPlace : sequence) {
      setCellState(bombPlace.first, bombPlace.second, megaBomb ? PLAYER_MEGABOMB : PLAYER_BOMB);
    }
  }

  static size_t mixHashToken(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return static_cast<size_t>(value);
  }

  static uint64_t mixVerificationToken(uint64_t value) {
    value += 0x6a09e667f3bcc909ULL;
    value = (value ^ (value >> 33)) * 0xff51afd7ed558ccdULL;
    value = (value ^ (value >> 33)) * 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return value;
  }

  static size_t cellHashToken(int y, int x, char owner) {
    const uint64_t index = static_cast<uint64_t>(y * kBoardWidth + x);
    const uint64_t ownerValue = static_cast<uint64_t>(static_cast<unsigned char>(owner));
    return mixHashToken((index << 8) ^ ownerValue ^ 0x3141592653589793ULL);
  }

  static uint64_t cellVerificationToken(int y, int x, char owner) {
    const uint64_t index = static_cast<uint64_t>(y * kBoardWidth + x);
    const uint64_t ownerValue = static_cast<uint64_t>(static_cast<unsigned char>(owner));
    return mixVerificationToken((index << 8) ^ ownerValue ^ 0x243f6a8885a308d3ULL);
  }

  static size_t playedWordHashToken(LegalWordId legalWordId) {
    return mixHashToken(static_cast<uint64_t>(legalWordId) ^ 0x2718281828459045ULL);
  }

  static uint64_t playedWordVerificationToken(LegalWordId legalWordId) {
    return mixVerificationToken(static_cast<uint64_t>(legalWordId) ^ 0x13198a2e03707344ULL);
  }

  static size_t playerHashToken(char player) {
    return mixHashToken(static_cast<uint64_t>(static_cast<unsigned char>(player)) ^ 0xfeedfacecafebeefULL);
  }

  static uint64_t playerVerificationToken(char player) {
    return mixVerificationToken(static_cast<uint64_t>(static_cast<unsigned char>(player)) ^ 0xa4093822299f31d0ULL);
  }

  void setCellState(int y, int x, char owner) {
    const char currentOwner = mState.get(y, x);
    if (currentOwner == owner) {
      return;
    }

    mHashValue ^= cellHashToken(y, x, currentOwner);
    mTtVerificationKey ^= cellVerificationToken(y, x, currentOwner);
    mState.set(y, x, owner);
    mHashValue ^= cellHashToken(y, x, owner);
    mTtVerificationKey ^= cellVerificationToken(y, x, owner);
  }

  void setPlayedWord(LegalWordId legalWordId, bool played) {
    if (mPlayedWords[legalWordId] == played) {
      return;
    }

    mHashValue ^= playedWordHashToken(legalWordId);
    mTtVerificationKey ^= playedWordVerificationToken(legalWordId);
    mPlayedWords.set(legalWordId, played);
  }

  void setPlayerToMove(char player) {
    if (player_to_move == player) {
      return;
    }

    mHashValue ^= playerHashToken(player_to_move);
    mTtVerificationKey ^= playerVerificationToken(player_to_move);
    player_to_move = player;
    mHashValue ^= playerHashToken(player_to_move);
    mTtVerificationKey ^= playerVerificationToken(player_to_move);
  }

  size_t computeHashFromState() const {
    size_t seed = playerHashToken(player_to_move);
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        seed ^= cellHashToken(y, x, mState.get(y, x));
      }
    }
    for (size_t legalWordId = 0; legalWordId < mPlayedWords.size(); legalWordId++) {
      if (mPlayedWords[legalWordId]) {
        seed ^= playedWordHashToken(static_cast<LegalWordId>(legalWordId));
      }
    }
    return seed;
  }

  uint64_t computeVerificationKeyFromState() const {
    uint64_t seed = playerVerificationToken(player_to_move);
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        seed ^= cellVerificationToken(y, x, mState.get(y, x));
      }
    }
    for (size_t legalWordId = 0; legalWordId < mPlayedWords.size(); legalWordId++) {
      if (mPlayedWords[legalWordId]) {
        seed ^= playedWordVerificationToken(static_cast<LegalWordId>(legalWordId));
      }
    }
    return seed;
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

    if (max_moves != INF && moves.size() > static_cast<size_t>(max_moves)) {
      moves.resize(max_moves);
    }

    return moves;
  }

  // Return a list of legal moves filtered by an optional filter and sorted
  // by most likely to be the "best" move.
  // filter must exactly match the found move, keeping in mind that the same
  // word may appear through multiple paths.
  // Also note that words which are already played are excluded.
  std::vector<WordBaseMove> get_legal_moves2(int max_moves, const char* filter) const {
    // Reuse a static bitset to avoid heap allocation per call.
    static boost::dynamic_bitset<> validWordBits;
    const int legalWordsSize = mBoard->getLegalWordsSize();
    validWordBits.resize(legalWordsSize);
    validWordBits.reset();

    // For each letter owned by the current player find candidate words OR them all together
    // They are implicitly in "order" so this effectively sorts them too.
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (mState.get(y, x) == player_to_move) {
          const auto& legalWords = mBoard->getLegalWords(y, x);
          const auto& wordBits = legalWords.wordBits(this->player_to_move == PLAYER_1);
          if (wordBits.size() != 0) {
            validWordBits |= wordBits;
          }
        }
      }
    }

    const size_t totalCount = validWordBits.count();
    const size_t maxMoveCount = max_moves == INF
      ? totalCount
      : std::min(totalCount, static_cast<size_t>(max_moves));
    std::vector<WordBaseMove> moves(maxMoveCount);

    int moveIndex = 0;
    // Iterate from set bit to set bit, each representing a legal word in our set.
    for (size_t renumberedGoodness = validWordBits.find_first(); renumberedGoodness != boost::dynamic_bitset<>::npos; renumberedGoodness = validWordBits.find_next(renumberedGoodness)) {
      LegalWordId legalWordId = mBoard->getLegalWordIdFromRenumberedGoodness(renumberedGoodness, this->player_to_move == PLAYER_1);

      // Ensure already played words are ignored.
      if (!mPlayedWords[legalWordId]) {
        if (filter == NULL || mBoard->getLegalWord(legalWordId).mWord.compare(filter) == 0) {
          moves[moveIndex].mLegalWordId = legalWordId;
          moveIndex++;
          if (moveIndex == static_cast<int>(maxMoveCount)) {
            break;
          }
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
  // Sets mTookEnemyCell if an enemy cell is overwritten.
  void recordOne(int y, int x) {
    if (y < 0 || y >= kBoardHeight || x < 0 || x >= kBoardWidth) {
      return;
    }

    const char currentOwner = mState.get(y, x);
    bool hadBomb = (currentOwner == PLAYER_BOMB);
    bool hadMegabomb = (currentOwner == PLAYER_MEGABOMB);
    if (currentOwner == get_enemy(player_to_move)) {
      mTookEnemyCell = true;
    }

    setCellState(y, x, player_to_move);

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

  // Check whether a move is valid for the current player.
  bool isValidMove(const WordBaseMove& move) const override {
    if (move.mLegalWordId < 0 || move.mLegalWordId >= static_cast<int>(mPlayedWords.size())) {
      return false;
    }
    if (mPlayedWords[move.mLegalWordId]) {
      return false;
    }
    const CoordinateList& ws = mBoard->getLegalWord(move.mLegalWordId).mWordSequence;
    return !ws.empty() && mState.get(ws[0].first, ws[0].second) == player_to_move;
  }

  // Record a single move in the game. Iterates through each grid square, claiming that square.
  void recordMove(const WordBaseMove& move) {
    assert(isValidMove(move));
    mTookEnemyCell = false;
    const CoordinateList& wordSequence = mBoard->getLegalWord(move.mLegalWordId).mWordSequence;

    // Record each letter.
    for (const auto& pathElement : wordSequence) {
      recordOne(pathElement.first, pathElement.second);
    }

    // Mark this word as played.
    const std::vector<LegalWordId>& equivalentWordIds = mBoard->getEquivalentLegalWordIds(move.mLegalWordId);
    for (auto legalWordId : equivalentWordIds) {
      setPlayedWord(legalWordId, true);
    }
  }

  // Flood-fill from (y, x), marking all connected same-owner cells with bit 0x8.
  // Grid writes bypass hash updates since the 0x8 bit is transient.
  // Must be followed by clearing 0x8 bits and disconnecting unreached cells.
  void markConnected(int y, int x, char owner) {
    if (y < 0 || y >= kBoardHeight || x < 0 || x >= kBoardWidth) {
      return;
    }
    const char cellOwner = mState.get(y, x);
    if (cellOwner != owner || (cellOwner & 0x8)) {
      return;
    }

    // Pack (y, x) as (y << 4) | x. Works since x < 10 < 16 and y < 13.
    static std::vector<uint16_t> stack;
    stack.clear();
    mState.set(y, x, owner | 0x8);
    stack.push_back((y << 4) | x);
    while (!stack.empty()) {
      const uint16_t current = stack.back();
      stack.pop_back();

      const int currentY = current >> 4;
      const int currentX = current & 0xF;

      for (int deltaY = -1; deltaY <= 1; deltaY++) {
        for (int deltaX = -1; deltaX <= 1; deltaX++) {
          if (deltaY == 0 && deltaX == 0) continue;
          const int ny = currentY + deltaY;
          const int nx = currentX + deltaX;
          if (ny < 0 || ny >= kBoardHeight || nx < 0 || nx >= kBoardWidth) continue;
          const char neighborOwner = mState.get(ny, nx);
          if (neighborOwner == owner) {
            mState.set(ny, nx, owner | 0x8);
            stack.push_back((ny << 4) | nx);
          }
        }
      }
    }
  }

  // Make a move, change the current player to the other after doing this.
  void make_move(const WordBaseMove& move) override {
    // Make the move.
    recordMove(move);

    // Only check connectivity if enemy cells were taken (otherwise no disconnection possible).
    if (mTookEnemyCell) {
      // Mark from all possible roots; the two sides of the board.
      for (int y : {0, kBoardHeight - 1}) {
        for (int x = 0; x < kBoardWidth; x++) {
          markConnected(y, x, mState.get(y, x));
        }
      }

      // Clear visited marks and disconnect any unreached owned cells.
      for (int y = 0; y < kBoardHeight; y++) {
        for (int x = 0; x < kBoardWidth; x++) {
          char owner = mState.get(y, x);
          if (owner & 0x8) {
            mState.set(y, x, owner & 0x7);
          } else if (owner != PLAYER_UNOWNED && owner != PLAYER_BOMB && owner != PLAYER_MEGABOMB) {
            setCellState(y, x, PLAYER_UNOWNED);
          }
        }
      }
    }

    // Change to the new player.
    setPlayerToMove(get_enemy(player_to_move));
  }

  std::ostream &to_stream(std::ostream &os) const override {
    return os;
  }

  bool operator==(const WordBaseState &other) const override {
    return player_to_move == other.player_to_move && mState == other.mState && mPlayedWords == other.mPlayedWords;
  }

  size_t hash() const override {
    return mHashValue;
  }

  uint64_t tt_verification_key() const override {
    return mTtVerificationKey;
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
        setPlayedWord(curId, true);
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
