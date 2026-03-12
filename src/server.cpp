#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "board.h"
#include "easylogging++.h"
#include "word-dictionary.h"
#include "wordescape.cpp"

INITIALIZE_EASYLOGGINGPP

namespace {

// ---- Minimal JSON helpers (schema-specific, not general-purpose) ----

// Find position right after "key": (skipping whitespace after colon)
size_t findValueStart(const std::string& json, const std::string& key) {
  std::string search = "\"" + key + "\"";
  auto pos = json.find(search);
  if (pos == std::string::npos) return std::string::npos;
  pos += search.size();
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  if (pos >= json.size() || json[pos] != ':') return std::string::npos;
  pos++;  // skip ':'
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
  return pos;
}

std::string extractString(const std::string& json, const std::string& key) {
  auto pos = findValueStart(json, key);
  if (pos == std::string::npos || pos >= json.size() || json[pos] != '"') return "";
  pos++;  // skip opening quote
  auto end = json.find('"', pos);
  if (end == std::string::npos) return "";
  return json.substr(pos, end - pos);
}

double extractDouble(const std::string& json, const std::string& key) {
  auto pos = findValueStart(json, key);
  if (pos == std::string::npos) return 0.0;
  try { return std::stod(json.substr(pos)); }
  catch (...) { return 0.0; }
}

int extractInt(const std::string& json, const std::string& key) {
  return static_cast<int>(extractDouble(json, key));
}

std::vector<std::string> extractStringArray(const std::string& json, const std::string& key) {
  std::vector<std::string> result;
  auto pos = findValueStart(json, key);
  if (pos == std::string::npos || pos >= json.size() || json[pos] != '[') return result;
  pos++;  // skip '['
  while (pos < json.size() && json[pos] != ']') {
    if (json[pos] == '"') {
      pos++;
      auto end = json.find('"', pos);
      if (end == std::string::npos) break;
      result.push_back(json.substr(pos, end - pos));
      pos = end + 1;
    } else {
      pos++;
    }
  }
  return result;
}

std::string jsonEscape(const std::string& input) {
  std::string escaped;
  escaped.reserve(input.size() + 8);
  for (char c : input) {
    switch (c) {
      case '\\': escaped += "\\\\"; break;
      case '"': escaped += "\\\""; break;
      case '\n': escaped += "\\n"; break;
      default: escaped += c; break;
    }
  }
  return escaped;
}

}  // namespace

int main(int argc, char** argv) {
  START_EASYLOGGINGPP(argc, argv);

  try {
    // Suppress all logging to keep stdout clean for JSON protocol.
    el::Configurations loggingConfig;
    loggingConfig.setToDefault();
    loggingConfig.setGlobally(el::ConfigurationType::ToFile, "false");
    loggingConfig.setGlobally(el::ConfigurationType::ToStandardOutput, "false");
    loggingConfig.set(el::Level::Debug, el::ConfigurationType::Enabled, "false");
    loggingConfig.set(el::Level::Verbose, el::ConfigurationType::Enabled, "false");
    loggingConfig.set(el::Level::Trace, el::ConfigurationType::Enabled, "false");
    loggingConfig.set(el::Level::Info, el::ConfigurationType::Enabled, "false");
    loggingConfig.set(el::Level::Warning, el::ConfigurationType::Enabled, "false");
    el::Loggers::reconfigureAllLoggers(loggingConfig);

    std::string dictPath = (argc > 1) ? argv[1] : "twl06_with_wordbase_additions.txt";
    std::ifstream dictFile(dictPath);
    if (!dictFile.is_open()) {
      std::cerr << "Could not open dictionary: " << dictPath << std::endl;
      return 1;
    }

    WordDictionary dictionary(dictFile);
    dictFile.close();

    // Signal ready on stdout
    std::cout << "{\"status\":\"ready\"}" << std::endl;

    // Cache BoardStatic for the current board text (expensive to build).
    std::string cachedBoardText;
    std::unique_ptr<BoardStatic> cachedBoard;

    // Read JSON requests line by line from stdin.
    std::string line;
    while (std::getline(std::cin, line)) {
      if (line.empty()) continue;

      try {
        std::string boardText = extractString(line, "board");
        std::string owners = extractString(line, "owners");
        int player = extractInt(line, "player");
        double seconds = extractDouble(line, "seconds");
        int maxDepth = extractInt(line, "depth");
        auto playedWords = extractStringArray(line, "played");

        if (boardText.size() != kBoardHeight * kBoardWidth) {
          std::cout << "{\"error\":\"board must be "
                    << kBoardHeight * kBoardWidth << " chars, got "
                    << boardText.size() << "\"}" << std::endl;
          continue;
        }
        if (owners.size() != kBoardHeight * kBoardWidth) {
          std::cout << "{\"error\":\"owners must be "
                    << kBoardHeight * kBoardWidth << " chars, got "
                    << owners.size() << "\"}" << std::endl;
          continue;
        }

        // Create or reuse BoardStatic (word index).
        if (boardText != cachedBoardText) {
          cachedBoard = std::make_unique<BoardStatic>(boardText, dictionary);
          cachedBoardText = boardText;
          std::cerr << "Built BoardStatic: " << cachedBoard->getLegalWordsSize()
                    << " legal words" << std::endl;
        }

        // Create initial state, then override ownership.
        char playerToMove = (player == 2) ? PLAYER_2 : PLAYER_1;
        WordBaseState state(cachedBoard.get(), playerToMove);

        // Set each cell's ownership from the owners string.
        for (int i = 0; i < kBoardHeight * kBoardWidth; i++) {
          int y = i / kBoardWidth;
          int x = i % kBoardWidth;
          char owner = owners[i] - '0';
          if (state.getGridState().get(y, x) != owner) {
            state.setCellState(y, x, owner);
          }
        }

        // Mark played words.
        for (const auto& word : playedWords) {
          state.addAlreadyPlayed(word);
        }

        // Check terminal.
        if (state.is_terminal()) {
          std::cout << "{\"error\":\"game is terminal\"}" << std::endl;
          continue;
        }

        // Search.
        if (seconds <= 0) seconds = 2.0;
        if (maxDepth <= 0) maxDepth = 10;

        Minimax<WordBaseState, WordBaseMove> algorithm(seconds, 200);
        algorithm.setMaxDepth(maxDepth);
        algorithm.setUseTranspositionTable(true);
        algorithm.setTTSizeBits(18);
        algorithm.setTraceStream(nullptr);

        WordBaseState searchState(state);
        WordBaseMove move = algorithm.get_move(&searchState);

        const LegalWord& legalWord = cachedBoard->getLegalWord(move.mLegalWordId);
        const auto& stats = algorithm.getLastSearchStats();

        // Build path JSON array: [[row, col], ...]
        std::string pathJson = "[";
        for (size_t i = 0; i < legalWord.mWordSequence.size(); i++) {
          if (i > 0) pathJson += ",";
          pathJson += "[" + std::to_string(legalWord.mWordSequence[i].first) + ","
                        + std::to_string(legalWord.mWordSequence[i].second) + "]";
        }
        pathJson += "]";

        std::cout << "{"
                  << "\"word\":\"" << jsonEscape(legalWord.mWord) << "\","
                  << "\"path\":" << pathJson << ","
                  << "\"depth\":" << stats.max_depth << ","
                  << "\"nodes\":" << stats.nodes << ","
                  << "\"nps\":" << static_cast<long long>(stats.nodes_per_second) << ","
                  << "\"seconds\":" << stats.elapsed_seconds
                  << "}" << std::endl;

      } catch (const std::exception& e) {
        std::cout << "{\"error\":\"" << jsonEscape(e.what()) << "\"}" << std::endl;
      }
    }

  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 1;
  }

  return 0;
}
