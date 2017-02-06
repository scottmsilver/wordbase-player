#include <sstream>
#include <string>
#include <unordered_set>

#include "board.h"
#include "word-dictionary.h"
#include "wordescape.cpp"
#include "easylogging++.h"

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv) {
  if (argc > 1) {
    std::ifstream input(argv[1]);
    if (!input.is_open()) {
      throw std::runtime_error("Could not open dictionary file: \"" + string(argv[1]) + "\"");
    }

    WordDictionary wd(input);
    BoardStatic board("caorsorbafal*sutseidnercbnolecavksidlvrtselruamasiuxigdbrsyngoenerhaneodrosmtsihlaltdymecrescehudndmnefingelermaeamoksbaoflbdecuhlg", wd);
    WordBaseState state(&board, PLAYER_1);
    WordBaseMove move = Minimax<WordBaseState, WordBaseMove>(10,10).get_move(&state);
  }
}
