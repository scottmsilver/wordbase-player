#include <algorithm>
#include <boost/functional/hash.hpp>
#include <boost/format.hpp>
#include <cstddef>
#include <fstream>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gtsa.hpp"

const char PLAYER_1 = 1;
const char PLAYER_2 = 2;
const char PLAYER_UNOWNED = 0;
const char PLAYER_BOMB = 3;
const char PLAYER_MEGABOMB = 4;

const int kBoardHeight = 13;
const int kBoardWidth = 10;

const char kOwnerUnowned = 0;
const char kOwnerMaximizer = 1;
const char kOwnerMinimizer = 2;

typedef std::vector<std::pair<int, int>> MoveSequence;

struct WordEscapeMove : public Move<WordEscapeMove> {
  MoveSequence mMove;
  
  WordEscapeMove() { }
  WordEscapeMove(const MoveSequence &move) : mMove(move) { }
  
  // parsePath
  //
  // Returns a vector of int, int pairs from a string in the format
  // (2, 3), (3, 4) ...
  static MoveSequence parsePath(const std::string& s) {
    MoveSequence path;
    
    std::regex words_regex("(\\(([0-9]+)\\s*,\\s*([0-9]+)\\)),?\\s*");
    
    for (std::sregex_iterator i = std::sregex_iterator(s.begin(), s.end(), words_regex);
         i != std::sregex_iterator(); ++i) {
      std::smatch match = *i;
      
      int y = std::stoi(match[2], nullptr, 0);
      int x = std::stoi(match[3], nullptr, 0);
      path.push_back(std::pair<int, int>(y, x));
    }
    
    return path;
  }
  
  void read() override {
    cout << "Enter form of (Y, X),?+" << endl;
    
    std::string line;
    std::getline(std::cin, line);
    mMove = parsePath(line);
  }
  
  ostream &to_stream(ostream &os) const override {
    bool first = true;
    for (auto pathElement : mMove) {
      if (!first) {
        os << ",";
      }
      
      os << "(" << pathElement.first << "," << pathElement.second << ")";
      first = false;
    }
    
    return os;
  }
  
  bool operator==(const WordEscapeMove &rhs) const override {
    return std::equal(mMove.begin(), mMove.end(), rhs.mMove.begin());
  }
  
  size_t hash() const override {
    return boost::hash_range(mMove.begin(), mMove.end());
  }
};


// Return string with spaces roved from end of supplied string.
static inline std::string &rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}

// A simple two dimension grid of class T, height H and width W.
// Represented efficiently as a single row major array.
template<class T, int H, int W> class Grid {
public:
  typedef std::array<T, H * W> Container;
  
  // Initialize GridState by copying the underlying contents.
  Grid(const Grid& state) : mState(state.mState) {}
  
  // Underlying contents have undefined value.
  Grid() {}
  
  const T get(int y, int x) const { return mState[y * W + x]; }
  void set(int y, int x, const T &value) { mState[y * W + x] = value; }
  typename Container::const_iterator begin() const { return mState.begin(); }
  typename Container::const_iterator end() const { return mState.end(); }
  void fill(const T &value) { mState.fill(value); }
  bool operator==(const Grid<T, H, W>& rhs) const { return mState == rhs.mState;}
  
private:
  Container mState;
  const T& operator[](int pos) { return mState[pos]; }
};

class BoardStatic {
  std::unordered_set<std::string> mWords;
  std::unordered_set<std::string> mPrefixes;
  std::unordered_map<int, std::vector<std::pair<std::string, MoveSequence>>> mValidWordPathsGrid;
  MoveSequence mBombs;
  MoveSequence mMegabombs;

public:
  std::vector<char> mGrid;
  
  BoardStatic(const string& gridText, const string& wordsFileName) {
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
    
    // Read in all the valid words. Words must be of length 2 and contain letters from our grid.
    std::regex possibleWordsRe("[" + gridText + "]{2,}$");
    
    std::ifstream input(wordsFileName);
    input.exceptions(ifstream::badbit);
    
    if (!input.good()) {
      throw std::runtime_error("Could not open dictionary file: \"" + wordsFileName + "\"");
    }
    
    std::string line;
    while (getline(input, line)) {
      line = rtrim(line);
      for (std::sregex_iterator i = std::sregex_iterator(line.begin(), line.end(), possibleWordsRe);
           i != std::sregex_iterator(); ++i) {
        std::smatch match = *i;
        string word = line;
        mWords.insert(word);
        // Keep track of all possible prefixes of this word, starting at word length 2.
        for (int wordLength = 1; wordLength <= word.length(); wordLength++) {
          mPrefixes.insert(word.substr(0, wordLength));
        }
      }
    }
  }
  
  // Return the word represented by the passed in sequence.
  std::string wordFromMove(const WordEscapeMove& move) {
    std::stringstream wordText;
    
    for (auto pathElement : move.mMove) {
      wordText << mGrid[pathElement.first * kBoardWidth + pathElement.second];
    }
    
    return wordText.str();
  }
  
  // Return all the valid words for the given grid square.
  const std::vector<std::pair<std::string, MoveSequence> >& findValidWordPaths(int y, int x) {
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
  void findWordPaths(int y, int x, string prefix, MoveSequence prefixPath,
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
    if (prefixPath.size() > 2 && mPrefixes.find(prefix) == mPrefixes.end()) {
      return;
    }
    
    prefix = std::string(prefix) + mGrid[y * kBoardWidth + x];
    prefixPath = MoveSequence(prefixPath);
    prefixPath.push_back(std::pair<int, int>(y, x));
    
    // Keep track of our word as (word, path) if it's a real word.
    if (mWords.find(prefix) != mWords.end()) {
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


class WordEscapeGridState : public Grid<char, kBoardHeight, kBoardWidth> {
public:
  // Initialize GridState by copying the underlying contents.
  WordEscapeGridState(const WordEscapeGridState& state) : Grid<char, kBoardHeight, kBoardWidth>(state) {}
  
  // Initialize GridState to an unused board.
  WordEscapeGridState() : Grid() {
    fill(kOwnerUnowned);
    for (int x = 0; x < kBoardWidth; x++) {
      set(0, x, PLAYER_1);
      set(kBoardHeight - 1, x, PLAYER_2);
    }
  }
};

// Functor to help compare too moves on the basis of their heuristic score.
struct Goodness {
  char mPlayerToMove;
  
  Goodness(char playerToMove) : mPlayerToMove(playerToMove) {}
  
  bool operator()(const WordEscapeMove& i, const WordEscapeMove& j) const {
    return mPlayerToMove == PLAYER_1 ? goodnessSorterMax(i, j) :
    goodnessSorterMin(i, j);
  }
  
  bool goodnessSorterMin(const WordEscapeMove& i, const WordEscapeMove& j) const {
    int h_i = 0;
    for (auto x : i.mMove) {
      h_i += (x.first - kBoardHeight) * (x.first - kBoardHeight);
    }
    
    int h_j = 0;
    for (auto x : j.mMove) {
      h_j += (x.first - kBoardHeight) * (x.first - kBoardHeight);
    }
    
    return h_i > h_j;
  }
  
  bool goodnessSorterMax(const WordEscapeMove& i, const WordEscapeMove& j) const {
    int h_i = 0;
    
    for (auto x : i.mMove) {
      h_i += (x.first + 1) * (x.first + 1);
    }
    
    int h_j = 0;
    
    for (auto x : j.mMove) {
      h_j += (x.first + 1) * (x.first + 1);
    }
    
    return h_i > h_j;
  }
};


// State of WordEscape board.
struct WordEscapeState : public State<WordEscapeState, WordEscapeMove> {
  WordEscapeGridState mState;
  std::unordered_set<string> mPlayedWords;
  
  BoardStatic* mBoard;
  
  WordEscapeState(BoardStatic* board, char playerToMove) : State<WordEscapeState, WordEscapeMove>(playerToMove), mBoard(board) {
    putBomb(board->getBombs(), false);
    putBomb(board->getMegabombs(), true);
  }
  
  WordEscapeState(const WordEscapeState& rhs) : State<WordEscapeState, WordEscapeMove>(rhs.player_to_move), mBoard(rhs.mBoard), mState(rhs.mState) {
    for (auto x : rhs.mPlayedWords) {
      mPlayedWords.insert(x);
    }
  }
  
  WordEscapeState clone() const override {
    return WordEscapeState(*this);
  }
  
  // Place bombs at each point in the supplied sequence.
  void putBomb(MoveSequence sequence, bool megaBomb) {
    for (auto&& bombPlace : sequence) {
      mState.set(bombPlace.first, bombPlace.second, megaBomb ? PLAYER_MEGABOMB : PLAYER_BOMB);
    }
  }
  
  // Return the value of this board state from the perspective of the given player.
  // In other words, it should be positive if player_to_move has an advantage.
  int get_goodness() const override {
    int h = 0;
    
    // First look for the terminal conditions.
    // FIX-ME combine this with is_terminal, with one that can say which player was terminal.ps
    
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
  vector<WordEscapeMove> get_legal_moves(int max_moves = INF) const override {
    return get_legal_moves(max_moves, NULL);
  }
  
  // Return a list of legal moves filtered by an optional filter and sorted
  // by most likely to be the "best" move.
  vector<WordEscapeMove> get_legal_moves(int max_moves, const char* filter) const {
    // Maintain an ordered set, ordered by "goodness" which is a heuristic for whether
    // we think the move is likely to be very good.
    std::multiset<WordEscapeMove, Goodness> movesAsSet((Goodness(player_to_move)));
    
    // Look at all possible words from each grid and discard those
    // that are already used or should be filtered out by request.
    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        if (mState.get(y, x) == player_to_move) {
          for (auto&& validWordPathsEntry : mBoard->findValidWordPaths(y, x) ) {
            if (mPlayedWords.find(validWordPathsEntry.first) == mPlayedWords.end()) {
              if (filter == NULL || validWordPathsEntry.first.compare(filter) == 0) {
                movesAsSet.insert(WordEscapeMove(validWordPathsEntry.second));
              }
            }
          }
        }
      }
    }
    
    // Turn this set back into a vector. Perhaps consider changing the protocol
    // to return not a vector..
    std::vector<WordEscapeMove> moves(movesAsSet.size());
    std::copy(movesAsSet.begin(), movesAsSet.end(), moves.begin());
    
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
  
  void recordMove(const WordEscapeMove& move) {
    // FIX-ME OMG this is lame, just fix move so it has the string itself or an int representing the string.
    // The part that is that I end up rebuilding the word from the grid, which we
    // should know a priori.
    std::string word;
    
    if (move.mMove.size() > 0) {
      assert(mState.get(move.mMove[0].first, move.mMove[0].second) == player_to_move);
    }
    
    for (const auto& pathElement : move.mMove) {
      word += mBoard->mGrid[pathElement.first * kBoardWidth + pathElement.second];
      recordOne(pathElement.first, pathElement.second);
    }
    
    mPlayedWords.insert(word);
  }
  
  // Starting at (y, x) mark all grid squares that are connected to (y, x).
  // A grid square is connected to (y, x) if it is eventually connected to (y, x) through
  // any path and has the same owner as (y, x).
  // Mark it by setting the the the high bit.
  //
  // This should be followed by a called to clearNotConnected().
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
      markConnected(y + 1, x, owner);
      markConnected(y + 1, x + 1, owner);
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
  void make_move(const WordEscapeMove& move) override {
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
  
  void undo_move(const WordEscapeMove &move) override {
    // This doesn't work and is a weird anachronism of the
    // alpha-beta engine. We can't trivially undo our state
    // without essentially saving our old sate; so we have the
    // caller do that on its stack.
    assert(false);
  }
  
  ostream &to_stream(ostream &os) const override {
    return os;
  }
  
  bool operator==(const WordEscapeState &other) const override {
    return mState == other.mState && mPlayedWords == other.mPlayedWords;
  }
  
  size_t hash() const override {
    return boost::hash_range(mState.begin(), mState.end());
  }
  
  const std::unordered_set<string>& getAlreadyPlayed() const {
    return mPlayedWords;
  }
};


// Print out a move sequence.
std::ostream& operator<<(std::ostream& os, const MoveSequence& foo) {
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

// Print out the a WordEscapeState.
std::ostream& operator<<(std::ostream& os, const WordEscapeState& foo) {
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
