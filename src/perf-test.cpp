#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "board.h"
#include "easylogging++.h"
#include "word-dictionary.h"
#include "wordescape.cpp"

#ifdef HAS_TORCH
#include "neural-eval.h"
#endif

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
  int warmupTurns = 0;
  int repeatSearches = 0;
  int selfplayGames = 0;
  std::string selfplayOutPath;
  bool useTranspositionTable = true;
  bool printBoards = false;
  std::string neuralModelPath;
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
    << "  --warmup-turns <count>   Play unmeasured warm-up plies before timing (default 0)\n"
    << "  --repeat-searches <n>    Re-search the post-warmup position N times without playing moves\n"
    << "  --selfplay-games <n>     Play N self-play games and emit JSONL (default 0)\n"
    << "  --selfplay-out <path>    Write self-play JSONL to this file\n"
    << "  --no-tt                  Disable the transposition table\n"
    << "  --print-boards           Print the board after each move\n"
#ifdef HAS_TORCH
    << "  --neural-model <path>    Use neural network for leaf evaluation\n"
#endif
    ;
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
    } else if (arg == "--warmup-turns" && index < argc) {
      options.warmupTurns = std::stoi(argv[index++], nullptr, 0);
    } else if (arg == "--repeat-searches" && index < argc) {
      options.repeatSearches = std::stoi(argv[index++], nullptr, 0);
    } else if (arg == "--selfplay-games" && index < argc) {
      options.selfplayGames = std::stoi(argv[index++], nullptr, 0);
    } else if (arg == "--selfplay-out" && index < argc) {
      options.selfplayOutPath = argv[index++];
    } else if (arg == "--neural-model" && index < argc) {
      options.neuralModelPath = argv[index++];
    } else if (arg == "--no-tt") {
      options.useTranspositionTable = false;
    } else if (arg == "--print-boards") {
      options.printBoards = true;
    } else {
      printUsage(argv[0]);
      throw std::invalid_argument("Unknown or incomplete argument: " + arg);
    }
  }

  if (options.warmupTurns < 0) {
    throw std::invalid_argument("--warmup-turns must be non-negative");
  }
  if (options.repeatSearches < 0) {
    throw std::invalid_argument("--repeat-searches must be non-negative");
  }
  if (options.selfplayGames < 0) {
    throw std::invalid_argument("--selfplay-games must be non-negative");
  }
  if (options.selfplayGames > 0 && options.selfplayOutPath.empty()) {
    throw std::invalid_argument("--selfplay-out is required when --selfplay-games is set");
  }

  return options;
}

std::string jsonEscape(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default: escaped += c; break;
    }
  }
  return escaped;
}

std::string serializeOwners(const WordBaseState& state) {
  std::string out;
  out.reserve(kBoardHeight * kBoardWidth);
  for (int y = 0; y < kBoardHeight; ++y) {
    for (int x = 0; x < kBoardWidth; ++x) {
      char owner = state.mState.get(y, x);
      out.push_back(static_cast<char>('0' + owner));
    }
  }
  return out;
}

std::string serializePath(const CoordinateList& path) {
  std::string out;
  for (size_t i = 0; i < path.size(); ++i) {
    out += std::to_string(path[i].first);
    out += ",";
    out += std::to_string(path[i].second);
    if (i + 1 < path.size()) {
      out += ";";
    }
  }
  return out;
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

void printSummary(const WordBaseState& state,
                  const AggregateStats& aggregateStats,
                  int measuredTurns,
                  int totalTurns,
                  int warmupTurns,
                  bool measurementStarted,
                  const Timer& gameTimer) {
  std::cout
    << "summary turns=" << measuredTurns
    << " total_turns=" << totalTurns
    << " warmup_turns=" << warmupTurns
    << " elapsed=" << (measurementStarted ? gameTimer.seconds_elapsed() : 0.0) << "s"
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

#ifdef HAS_TORCH
    std::unique_ptr<NeuralEvaluator> neuralEval;
    if (!options.neuralModelPath.empty()) {
      neuralEval = std::make_unique<NeuralEvaluator>(options.neuralModelPath);
      if (!neuralEval->isLoaded()) {
        throw std::runtime_error("Failed to load neural model: " + options.neuralModelPath);
      }
      std::cout << "neural_eval loaded=" << options.neuralModelPath
                << " gpu=" << (neuralEval->isGPU() ? "true" : "false") << std::endl;
      // GPU warmup: run multiple forward passes to fully initialize CUDA kernels
      neuralEval->warmup(state);
    }
#endif

    auto makeAlgorithm = [&options
#ifdef HAS_TORCH
                          , &neuralEval
#endif
                          ]() {
      Minimax<WordBaseState, WordBaseMove> algorithm(options.maxSecondsPerMove, options.maxMovesPerPosition);
      algorithm.setMaxDepth(options.maxDepth);
      algorithm.setUseTranspositionTable(options.useTranspositionTable);
      algorithm.setTraceStream(nullptr);
#ifdef HAS_TORCH
      if (neuralEval) {
        NeuralEvaluator* evalPtr = neuralEval.get();
        algorithm.setMoveReorderer([evalPtr](WordBaseState* state, std::vector<WordBaseMove>& moves) {
          // Single forward pass: use policy logits to score all moves
          auto scores = evalPtr->policyScoreMoves(*state, moves);
          // Sort all moves by policy score (highest first)
          std::vector<std::pair<int, size_t>> scored(moves.size());
          for (size_t i = 0; i < moves.size(); i++) {
            scored[i] = {scores[i], i};
          }
          std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
          });
          std::vector<WordBaseMove> reordered(moves.size());
          for (size_t i = 0; i < moves.size(); i++) {
            reordered[i] = moves[scored[i].second];
          }
          moves = std::move(reordered);
        });
      }
#endif
      return algorithm;
    };

    if (options.selfplayGames > 0) {
      std::ofstream out(options.selfplayOutPath);
      if (!out.is_open()) {
        throw std::runtime_error("Could not open selfplay output: \"" + options.selfplayOutPath + "\"");
      }

      for (int gameIndex = 0; gameIndex < options.selfplayGames; ++gameIndex) {
        auto algorithm = makeAlgorithm();
        WordBaseState gameState(&board, PLAYER_1);
        for (int ply = 0; ply < options.maxTurns && !gameState.is_terminal(); ++ply) {
          std::vector<WordBaseMove> legalMoves = gameState.get_legal_moves(options.maxMovesPerPosition);
          if (legalMoves.empty()) {
            break;
          }

          WordBaseState searchState(gameState);
          Timer moveTimer;
          moveTimer.start();
          WordBaseMove move = algorithm.get_move(&searchState);
          double moveSeconds = moveTimer.seconds_elapsed();
          const auto& searchStats = algorithm.getLastSearchStats();
          const LegalWord& legalWord = board.getLegalWord(move.mLegalWordId);

          out
            << "{"
            << "\"type\":\"move\","
            << "\"game\":" << gameIndex << ","
            << "\"ply\":" << ply << ","
            << "\"player\":" << int(gameState.player_to_move) << ","
            << "\"board\":\"" << jsonEscape(options.boardText) << "\","
            << "\"owners\":\"" << serializeOwners(gameState) << "\","
            << "\"word\":\"" << jsonEscape(legalWord.mWord) << "\","
            << "\"path\":\"" << serializePath(legalWord.mWordSequence) << "\","
            << "\"legal_moves\":" << legalMoves.size() << ","
            << "\"depth\":" << searchStats.max_depth << ","
            << "\"nodes\":" << searchStats.nodes << ","
            << "\"leafs\":" << searchStats.leafs << ","
            << "\"beta_cuts\":" << searchStats.beta_cuts << ","
            << "\"tt_hits\":" << searchStats.tt_hits << ","
            << "\"nps\":" << searchStats.nodes_per_second << ","
            << "\"seconds\":" << moveSeconds
            << "}"
            << "\n";

          gameState.make_move(move);
        }

        out
          << "{"
          << "\"type\":\"summary\","
          << "\"game\":" << gameIndex << ","
          << "\"winner\":\"" << winnerText(gameState) << "\","
          << "\"goodness\":" << gameState.get_goodness() << ","
          << "\"terminal\":" << (gameState.is_terminal() ? "true" : "false")
          << "}"
          << "\n";
      }

      return 0;
    }

    auto algorithm = makeAlgorithm();

    Timer gameTimer;
    AggregateStats aggregateStats;
    bool measuring = options.warmupTurns == 0;
    bool measurementStarted = measuring;
    if (measurementStarted) {
      gameTimer.start();
    }
    int measuredTurns = 0;

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

      if (measuring) {
        aggregateStats.record(searchStats, legalMoves.size());
      }

      const LegalWord& legalWord = board.getLegalWord(move.mLegalWordId);
      const bool isWarmupTurn = turn < options.warmupTurns;
      std::cout
        << (isWarmupTurn ? "warmup" : "turn")
        << " " << turn + 1
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
      if (measuring) {
        ++measuredTurns;
      } else if (turn >= options.warmupTurns) {
        measuring = true;
        measurementStarted = true;
        measuredTurns = 0;
        aggregateStats = AggregateStats();
        gameTimer.start();
      }

      if (options.printBoards) {
        std::cout << state << std::endl;
      }
    }

    if (options.repeatSearches > 0) {
      std::vector<WordBaseMove> legalMoves = state.get_legal_moves(options.maxMovesPerPosition);
      if (legalMoves.empty()) {
        std::cout << "profile no legal moves remain after warmup" << std::endl;
      } else {
        AggregateStats repeatedSearchStats;
        Timer repeatedSearchTimer;
        repeatedSearchTimer.start();
        for (int repeat = 0; repeat < options.repeatSearches; ++repeat) {
          auto repeatedAlgorithm = makeAlgorithm();
          WordBaseState searchState(state);
          Timer moveTimer;
          moveTimer.start();
          WordBaseMove move = repeatedAlgorithm.get_move(&searchState);
          double moveSeconds = moveTimer.seconds_elapsed();
          const auto& searchStats = repeatedAlgorithm.getLastSearchStats();
          repeatedSearchStats.record(searchStats, legalMoves.size());

          const LegalWord& legalWord = board.getLegalWord(move.mLegalWordId);
          std::cout
            << "profile " << repeat + 1
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
        }

        printSummary(state,
                     repeatedSearchStats,
                     options.repeatSearches,
                     turn,
                     options.warmupTurns,
                     true,
                     repeatedSearchTimer);
        return 0;
      }
    }

    printSummary(state, aggregateStats, measuredTurns, turn, options.warmupTurns, measurementStarted, gameTimer);
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }

  return 0;
}
