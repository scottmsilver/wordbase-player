#include <sstream>
#include <string>
#include <unordered_set>

#include "board.h"
#include "word-dictionary.h"
#include "wordescape.cpp"
#include "easylogging++.h"
#include "obstack/obstack.hpp"

#include <boost/timer/timer.hpp>

INITIALIZE_EASYLOGGINGPP

int main(int argc, char **argv) {
  if (argc > 1) {
    std::ifstream input(argv[1]);
    if (!input.is_open()) {
      throw std::runtime_error("Could not open dictionary file: \"" + std::string(argv[1]) + "\"");
    }

    WordDictionary wd(input);
    BoardStatic board("temenoldhpiaipclraer*sodhtitvsttlasmuhinauahvomagiesceolinyrmkedrnatslaidroerdeimlyodsngntagntiairagtwievuedlonaludsgpy*hlbetinagmac", wd);
    WordBaseState state(&board, PLAYER_1);

    boost::arena::obstack vs(100);

    std::vector<int>* x = vs.alloc<std::vector<int>>(100);
    
    x->push_back(3);
    
    {
      boost::timer::auto_cpu_timer t;

      for (int i = 0; i < 100; i++) {
        std::vector<WordBaseMove> moves1 = state.get_legal_moves(INF, NULL);
      }
    }
    {
      boost::timer::auto_cpu_timer t;
      
      for (int i = 0; i < 100; i++) {
        std::vector<WordBaseMove> moves1 = state.get_legal_moves2(INF, NULL);
      }
    }
    
/*
    for (auto it1 = moves1.begin(), it2 = moves2.begin(); it1 != moves1.end(); it1++, it2++ ) {
      std::cout << *it1 << "\t" << board.getLegalWord(it1->mLegalWordId).mMaximizerGoodness << "\t" << *it2 << "\t" << board.getLegalWord(it2->mLegalWordId).mMaximizerGoodness << std::endl;
    }*/
    
    WordBaseMove move = Minimax<WordBaseState, WordBaseMove>(30, 10).get_move(&state);
  }
}
