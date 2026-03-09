#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "board.h"
#include "easylogging++.h"
#include "word-dictionary.h"
#include "wordescape.cpp"

INITIALIZE_EASYLOGGINGPP

namespace {

const char* kDefaultDictionaryPath = "twl06_with_wordbase_additions.txt";
const char* kDefaultBoardText =
  "gregmiperslmavnetlaecaosrnowykosbrilfakosalagzl*eicveonredgmdamepumselomrtleipcradsndlnoihuiai*eoisatxerhctpteroustupsyalcopaeamhves";

struct PerfOptions {
  std::string dictionaryPath = kDefaultDictionaryPath;
  std::string boardText = kDefaultBoardText;
  double maxSecondsPerMove = 0.25;
  int maxMovesPerPosition = 200;
  int maxDepth = 4;
  int maxTurns = 200;
  bool useTranspositionTable = true;
  bool printBoards = false;
};

struct AggregateStats {
  int turns = 0;
  long long nodes = 0;
  long long leafs = 0;
  long long betaCuts = 0;
  long long ttHits = 0;
  long long ttExacts = 0;
  long long ttCuts = 0;
  long long legalMoves = 0;
  double moveSeconds = 0.0;
  int deepestCompletedDepth = 0;

  void record(const Minimax<WordBaseState, WordBaseMove>::SearchStats& stats, int moveCount) {
    ++turns;
    nodes += stats.nodes;
    leafs += stats.leafs;
    betaCuts += stats.beta_cuts;
    ttHits += stats.tt_hits;
    ttExacts += stats.tt_exacts;
    ttCuts += stats.tt_cuts;
    legalMoves += moveCount;
    moveSeconds += stats.elapsed_seconds;
    deepestCompletedDepth = std::max(deepestCompletedDepth, stats.max_depth);
  }
};

void printUsage(const char* argv0) {
  std::cout
    << "Usage: " << argv0 << " [dictionary-path] [options]\n"
    << "Options:\n"
    << "  --board <text>           Board text to play\n"
    << "  --seconds <value>        Time budget per move (default 0.25)\n"
    << "  --max-moves <count>      Max legal moves searched per position (default 200)\n"
    << "  --max-depth <depth>      Max iterative deepening depth (default 4)\n"
    << "  --max-turns <count>      Stop after this many plies (default 200)\n"
    << "  --no-tt                  Disable the transposition table\n"
    << "  --print-boards           Print the board after each move\n";
}

PerfOptions parseArgs(int argc, char** argv) {
  PerfOptions options;
  int index = 1;

  if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
    options.dictionaryPath = argv[index++];
  }

  while (index < argc) {
    std::string arg = argv[index++];
    if (arg == "--board" && index < argc) {
      options.boardText = argv[index++];
    } else if (arg == "--seconds" && index < argc) {
      options.maxSecondsPerMove = std::stod(argv[index++], nullptr);
    } else if (arg == "--max-moves" && index < argc) {
      options.maxMovesPerPosition = std::stoi(argv[index++], nullptr, 0);
    } else if (arg == "--max-depth" && index < argc) {
      options.maxDepth = std::stoi(argv[index++], nullptr, 0);
    } else if (arg == "--max-turns" && index < argc) {
      options.maxTurns = std::stoi(argv[index++], nullptr, 0);
    } else if (arg == "--no-tt") {
      options.useTranspositionTable = false;
    } else if (arg == "--print-boards") {
      options.printBoards = true;
    } else {
      printUsage(argv[0]);
      throw std::invalid_argument("Unknown or incomplete argument: " + arg);
    }
  }

  return options;
}

std::string winnerText(const WordBaseState& state) {
  if (state.is_winner(PLAYER_1)) {
    return "PLAYER_1";
  }
  if (state.is_winner(PLAYER_2)) {
    return "PLAYER_2";
  }
  return "draw";
}

}  // namespace

int main(int argc, char** argv) {
  START_EASYLOGGINGPP(argc, argv);

  try {
    el::Configurations loggingConfig;
    loggingConfig.setToDefault();
    loggingConfig.setGlobally(el::ConfigurationType::ToFile, "false");
    loggingConfig.set(el::Level::Debug, el::ConfigurationType::Enabled, "false");
    loggingConfig.set(el::Level::Verbose, el::ConfigurationType::Enabled, "false");
    loggingConfig.set(el::Level::Trace, el::ConfigurationType::Enabled, "false");
    el::Loggers::reconfigureAllLoggers(loggingConfig);

    PerfOptions options = parseArgs(argc, argv);

    std::ifstream input(options.dictionaryPath);
    if (!input.is_open()) {
      throw std::runtime_error("Could not open dictionary file: \"" + options.dictionaryPath + "\"");
    }

    WordDictionary dictionary(input);
    BoardStatic board(options.boardText, dictionary);
    WordBaseState state(&board, PLAYER_1);

    Minimax<WordBaseState, WordBaseMove> algorithm(options.maxSecondsPerMove, options.maxMovesPerPosition);
    algorithm.setMaxDepth(options.maxDepth);
    algorithm.setUseTranspositionTable(options.useTranspositionTable);
    algorithm.setTraceStream(nullptr);

    Timer gameTimer;
    gameTimer.start();
    AggregateStats aggregateStats;

    int turn = 0;
    while (turn < options.maxTurns && !state.is_terminal()) {
      std::vector<WordBaseMove> legalMoves = state.get_legal_moves(options.maxMovesPerPosition);
      if (legalMoves.empty()) {
        std::cout << "turn " << turn + 1 << " no legal moves remain" << std::endl;
        break;
      }

      WordBaseState searchState(state);
      Timer moveTimer;
      moveTimer.start();
      WordBaseMove move = algorithm.get_move(&searchState);
      double moveSeconds = moveTimer.seconds_elapsed();
      const auto& searchStats = algorithm.getLastSearchStats();
      aggregateStats.record(searchStats, legalMoves.size());

      const LegalWord& legalWord = board.getLegalWord(move.mLegalWordId);
      std::cout
        << "turn " << turn + 1
        << " player " << int(state.player_to_move)
        << " move " << legalWord.mWord
        << " path " << legalWord.mWordSequence
        << " elapsed " << moveSeconds << "s"
        << " legal_moves " << legalMoves.size()
        << " depth " << searchStats.max_depth
        << " nodes " << searchStats.nodes
        << " leafs " << searchStats.leafs
        << " beta_cuts " << searchStats.beta_cuts
        << " tt_hits " << searchStats.tt_hits
        << " nps " << searchStats.nodes_per_second
        << std::endl;

      state.make_move(move);
      ++turn;

      if (options.printBoards) {
        std::cout << state << std::endl;
      }
    }

    std::cout
      << "summary turns=" << turn
      << " elapsed=" << gameTimer.seconds_elapsed() << "s"
      << " terminal=" << (state.is_terminal() ? "true" : "false")
      << " winner=" << winnerText(state)
      << " goodness=" << state.get_goodness()
      << " total_nodes=" << aggregateStats.nodes
      << " total_leafs=" << aggregateStats.leafs
      << " total_beta_cuts=" << aggregateStats.betaCuts
      << " total_tt_hits=" << aggregateStats.ttHits
      << " total_tt_exacts=" << aggregateStats.ttExacts
      << " total_tt_cuts=" << aggregateStats.ttCuts
      << " avg_legal_moves=" << (aggregateStats.turns == 0 ? 0.0 : static_cast<double>(aggregateStats.legalMoves) / aggregateStats.turns)
      << " avg_nodes_per_turn=" << (aggregateStats.turns == 0 ? 0.0 : static_cast<double>(aggregateStats.nodes) / aggregateStats.turns)
      << " avg_seconds_per_turn=" << (aggregateStats.turns == 0 ? 0.0 : aggregateStats.moveSeconds / aggregateStats.turns)
      << " overall_nps=" << (aggregateStats.moveSeconds == 0.0 ? 0.0 : aggregateStats.nodes / aggregateStats.moveSeconds)
      << " deepest_completed_depth=" << aggregateStats.deepestCompletedDepth
      << std::endl;
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }

  return 0;
}
