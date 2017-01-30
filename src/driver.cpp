/*
 driver.cpp
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
#include <boost/iterator/filter_iterator.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/range/algorithm.hpp>
#include <editline/readline.h>
#include <memory>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <vector>

#include "easylogging++.h"
#include "wordescape.cpp"

INITIALIZE_EASYLOGGINGPP

// Default dictionary file.
const char* kDefaultDictionaryPath = "/Users/ssilver/Google Drive/development/wordescape/c/gtsa/cpp/twl06.txt";

// The dictionary to use for this game.
std::unique_ptr<WordDictionary> gDictionary;

// The board to use.
std::unique_ptr<BoardStatic> gBoard;

// The state of the board.
std::unique_ptr<WordBaseState> gState;


// Do a single command from our command parser.
static bool doOneCommand(const char* dictionaryPath, const std::string& command) {
  bool notQuit = true;  

  // Break up command string into space delimited tokens.
  std::vector<std::string> tokens;
  std::istringstream iss(command);
  copy(std::istream_iterator<std::string>(iss),
       std::istream_iterator<std::string>(),
       back_inserter(tokens));
  
  if (tokens.size() > 0) {
    if (tokens[0].compare("psb") == 0) {
      // Print out the static board.
      std::cout << *gBoard;
    } else if (tokens[0].compare("words") == 0) {
      // Show all possible word paths from a given grid square.
      //
      // Usage:
      //  words Y X
      int y = std::stoi(tokens[1], nullptr, 0);
      int x = std::stoi(tokens[2], nullptr, 0);
      
      for (auto wordPaths : gBoard->findValidWordPaths(y, x)) {
        cout << wordPaths.first << ": " << wordPaths.second << endl;
      }
    } else if (tokens[0].compare("lm") == 0) {
      // Determine all legal moves for current state of board.
      //
      // Usage:
      //   lm
      for (auto move : gState->get_legal_moves()) {
        cout << gBoard->wordFromMove(move.getMoveSequence()) << ":" << move << endl;
      }
    } else if (tokens[0].compare("l") == 0) {
      // Load commands from file and execute them.
      //
      // Usage:
      //  l /foo/goo/roo.txt
      if (tokens.size() != 2) {
        cout << "file name required: l /foo/goo/roo" << endl;
      } else {
        std::ifstream input(tokens[1]);
        for (std::string line; getline(input, line);) {
          doOneCommand(dictionaryPath, line);
        }
      }
    } else if (tokens[0].compare("lwm") == 0) {
      // Determine legal locations for word requested.
      //
      // Usage:
      //  lwm chalk
      for (auto move : gState->get_legal_moves(INF, tokens[1].c_str())) {
        WordBaseState copy(*gState);
        copy.make_move(move);
        cout << move <<  ": h=" << copy.get_goodness() << endl;
      }
    } else if (tokens[0].compare("ap") == 0) {
      // Print out already played for given player
      for (auto word : gState->getAlreadyPlayed()) {
        cout << word << endl;
      }
    } else if (tokens[0].compare("bombs") == 0) {
      // Add bombs to the board.
      //
      // Usage
      //  bombs (3,3),(1,2)
      MoveSequence sequence(WordBaseMove::parsePath(tokens[1]));
      cout << "putting bombs at: " << sequence << endl;
      gState->putBomb(sequence, false);
    } else if (tokens[0].compare("mbombs") == 0) {
      // Add megabombs to the board.
      //
      // Usage
      //  mbombs (3,3),(1,2)
      MoveSequence sequence(WordBaseMove::parsePath(tokens[1]));
      cout << "putting bombs at: " << sequence << endl;
      gState->putBomb(sequence, true);
    } else if (tokens[0].compare("nb") == 0) {
      // New board. Pre-pending the letter with a "*" puts a bomb at that spot, a + is a "megabomb"
      //
      // Usage
      //   nb caorsorbafal*sutseidnercbnolecavksidlvrtselruamasiuxigdbrsyngoenerhaneodrosmtsihlaltdymecrescehudndmnefingelermaeamoksbaoflbdecuhlg
      //   nb bitumrahtrnsatesgoepevsrnpyes*insaewidanseimrufsgetmaugoitsnixtlherkpuodetsaficdascgatrfornihcnejustogteiryoachlobpengopobirbuscebd
      gBoard.reset(new BoardStatic(tokens[1], *gDictionary));
      gState.reset(new WordBaseState(gBoard.get(), PLAYER_1));
    } else if (tokens[0].compare("sm") == 0) {
      // Suggest a move.
      //
      // Usage:
      //  sm <seconds> <max depth to search> <use transposition table>
      double maxSeconds = 3;
      if (tokens.size() > 1) {
        maxSeconds = std::stod(tokens[1], nullptr);
      }
      
      int maxDepth = 20;
      if (tokens.size() > 2) {
        maxDepth = std::stoi(tokens[2], nullptr, 0);
      }
      
      bool useTranspositionTable = true;
      if (tokens.size() > 3) {
        useTranspositionTable = tokens[3].compare("true") == 0;
      }

      Minimax<WordBaseState, WordBaseMove> miniMax(maxSeconds, maxDepth);
      miniMax.setUseTranspositionTable(useTranspositionTable);
      WordBaseMove move = miniMax.get_move(gState.get());
      cout << "suggested move: " << gBoard->wordFromMove(move.getMoveSequence()) << endl << move << endl;
    } else if (tokens[0].compare("smmc") == 0) {
      // Use a montecarlo search tree method to suggest a move
      //
      // Usage:
      //   seconds - time in seconds to search.
      double maxSeconds = 3;
      if (tokens.size() > 1) {
        maxSeconds = std::stod(tokens[1], nullptr);
      }

      MonteCarloTreeSearch<WordBaseState, WordBaseMove> montecarlo(maxSeconds);

      WordBaseMove move = montecarlo.get_move(gState.get());
      cout << "suggested move: " << gBoard->wordFromMove(move.getMoveSequence()) << endl << move << endl;
    } else if (tokens[0].compare("h") == 0) {
      // Return the current h() / "goodness" of the current game.
      //
      // Usage:
      //  h
      cout << "h: " << gState->get_goodness() << endl;
    } else if (tokens[0].compare("ps") == 0) {
      // Print out the state of the current game.
      std::cout << *gState;
    } else if (tokens[0].compare("m") == 0) {
      // Make move. NB: Does not currently check if the move is legal.
      //
      // Usage:
      //  m (0,1),(1,2)
      if (tokens.size() > 1) {
        WordBaseMove move(WordBaseMove::parsePath(tokens[1]));
        cout << "making move: \"" << gBoard->wordFromMove(move.getMoveSequence()) << "\": " << move << std::endl;
        gState->make_move(move);
      } else {
        cout << "argument required: m (1,2),(2,3)" << endl;
      }
    } else if (tokens[0].compare("quit") == 0) {
      // Quit the game.
      //
      // Usage:
      //  quit
      notQuit = false;
    } else {
      cout << "unknown command" << std::endl;
    }
  }
  
  return notQuit;
}

int main(int argc, char** argv) {
  START_EASYLOGGINGPP(argc, argv);

  // Determine dictionary file to use and print out exception if not found.
  const char* dictionaryPath = argc > 1 ? argv[1] : kDefaultDictionaryPath;
  cout << "Using dictionary at '" << dictionaryPath << "'" << endl;

  std::ifstream input(dictionaryPath);
  if (!input.is_open()) {
    throw std::runtime_error("Could not open dictionary file: \"" + string(dictionaryPath) + "\"");
  }

  input.exceptions(std::ifstream::badbit);
  gDictionary.reset(new WordDictionary(input));
  
  // Configure readline to auto-complete paths when the tab key is hit.
  rl_bind_key('\t', rl_complete);
  bool notQuit = true;

  while (notQuit) {
    std::unique_ptr<char, decltype(free) *> input {readline("boardshell> "), free};
    
    // Check for EOF.
    if (!input)
      break;
    
    // Add input to history.
    add_history(input.get());
    
    notQuit = doOneCommand(dictionaryPath, input.get());
  }
  
  return 0;
}
