#ifndef NEURAL_EVAL_H
#define NEURAL_EVAL_H

#ifdef HAS_TORCH

#include <torch/script.h>
#include <torch/torch.h>

#include <memory>
#include <string>
#include <vector>

#include "board.h"
#include "grid.h"

// Number of input feature channels matching the Python training code.
static const int kNeuralChannels = 7;

// Wraps a TorchScript policy/value model for use in Wordbase search.
//
// The model takes a (B, 7, 13, 10) tensor and returns:
//   - policy_logits: (B, 13, 10) per-square move likelihood
//   - value: (B,) position evaluation in [0, 1]
//
// Usage:
//   NeuralEvaluator eval("ml/checkpoints/wordbase_policy.pt");
//   int value = eval.evaluate(state);           // single position
//   auto values = eval.evaluateBatch(states);    // batched positions
class NeuralEvaluator {
public:
  NeuralEvaluator(const std::string& modelPath, bool useGPU = true)
    : mUseGPU(useGPU) {
    try {
      mModel = torch::jit::load(modelPath);
      if (useGPU && torch::cuda::is_available()) {
        mDevice = torch::kCUDA;
        mModel.to(mDevice);
        mGPUAvailable = true;
      } else {
        mDevice = torch::kCPU;
        mModel.to(mDevice);
        mGPUAvailable = false;
      }
      mModel.eval();
      mLoaded = true;
    } catch (const c10::Error& e) {
      mLoaded = false;
    }
  }

  bool isLoaded() const { return mLoaded; }
  bool isGPU() const { return mGPUAvailable; }

  // Build feature tensor for a single position.
  // Returns a (1, 7, 13, 10) tensor.
  torch::Tensor stateToTensor(const WordBaseState& state) const {
    auto features = torch::zeros({1, kNeuralChannels, kBoardHeight, kBoardWidth});
    auto acc = features.accessor<float, 4>();

    const auto& grid = state.getGridState();
    const auto& boardStatic = state.getBoardStatic();
    char player = state.player_to_move;

    for (int y = 0; y < kBoardHeight; y++) {
      for (int x = 0; x < kBoardWidth; x++) {
        // Channel 0: letter identity normalized
        char letter = boardStatic.getGridChar(y, x);
        acc[0][0][y][x] = static_cast<float>(letter - 'a' + 1) / 26.0f;

        char owner = grid.get(y, x);

        if (player == PLAYER_1) {
          acc[0][1][y][x] = (owner == PLAYER_1) ? 1.0f : 0.0f;
          acc[0][2][y][x] = (owner == PLAYER_2) ? 1.0f : 0.0f;
          acc[0][6][y][x] = static_cast<float>(y) / (kBoardHeight - 1);
        } else {
          acc[0][1][y][x] = (owner == PLAYER_2) ? 1.0f : 0.0f;
          acc[0][2][y][x] = (owner == PLAYER_1) ? 1.0f : 0.0f;
          acc[0][6][y][x] = 1.0f - static_cast<float>(y) / (kBoardHeight - 1);
        }

        // Bombs: owner codes 3 and 4 in the grid state
        acc[0][3][y][x] = (owner == 3) ? 1.0f : 0.0f;  // bomb
        acc[0][4][y][x] = (owner == 4) ? 1.0f : 0.0f;  // megabomb
        acc[0][5][y][x] = (owner == 0) ? 1.0f : 0.0f;  // unowned
      }
    }

    return features;
  }

  // Evaluate a single position. Returns a value in [-INF, INF] scaled
  // to be comparable with the existing heuristic goodness values.
  // The net outputs [0, 1] where 1 = current player winning.
  // We map this to roughly [-1000, 1000] range.
  int evaluate(const WordBaseState& state) {
    if (!mLoaded) return 0;

    try {
      torch::NoGradGuard no_grad;
      auto input = stateToTensor(state).to(mDevice);
      auto output = mModel.forward({input});
      auto outputs = output.toTuple();
      float value = outputs->elements()[1].toTensor().item<float>();

      // Map [0, 1] to [-1000, 1000]
      return static_cast<int>((value - 0.5f) * 2000.0f);
    } catch (const std::exception& e) {
      std::cerr << "Neural eval error: " << e.what() << std::endl;
      return 0;
    }
  }

  // Evaluate multiple positions in a single GPU batch.
  // Much more efficient than calling evaluate() in a loop.
  std::vector<int> evaluateBatch(const std::vector<const WordBaseState*>& states) {
    if (!mLoaded || states.empty()) {
      return std::vector<int>(states.size(), 0);
    }

    torch::NoGradGuard no_grad;
    const int batchSize = static_cast<int>(states.size());

    // Build batched tensor
    auto batchTensor = torch::zeros({batchSize, kNeuralChannels, kBoardHeight, kBoardWidth});
    auto acc = batchTensor.accessor<float, 4>();

    for (int b = 0; b < batchSize; b++) {
      const auto& state = *states[b];
      const auto& grid = state.getGridState();
      const auto& boardStatic = state.getBoardStatic();
      char player = state.player_to_move;

      for (int y = 0; y < kBoardHeight; y++) {
        for (int x = 0; x < kBoardWidth; x++) {
          char letter = boardStatic.getGridChar(y, x);
          acc[b][0][y][x] = static_cast<float>(letter - 'a' + 1) / 26.0f;

          char owner = grid.get(y, x);

          if (player == PLAYER_1) {
            acc[b][1][y][x] = (owner == PLAYER_1) ? 1.0f : 0.0f;
            acc[b][2][y][x] = (owner == PLAYER_2) ? 1.0f : 0.0f;
            acc[b][6][y][x] = static_cast<float>(y) / (kBoardHeight - 1);
          } else {
            acc[b][1][y][x] = (owner == PLAYER_2) ? 1.0f : 0.0f;
            acc[b][2][y][x] = (owner == PLAYER_1) ? 1.0f : 0.0f;
            acc[b][6][y][x] = 1.0f - static_cast<float>(y) / (kBoardHeight - 1);
          }

          acc[b][3][y][x] = (owner == 3) ? 1.0f : 0.0f;
          acc[b][4][y][x] = (owner == 4) ? 1.0f : 0.0f;
          acc[b][5][y][x] = (owner == 0) ? 1.0f : 0.0f;
        }
      }
    }

    auto input = batchTensor.to(mDevice);
    auto outputs = mModel.forward({input}).toTuple();
    auto values = outputs->elements()[1].toTensor().to(torch::kCPU);
    auto valAcc = values.accessor<float, 1>();

    std::vector<int> results(batchSize);
    for (int b = 0; b < batchSize; b++) {
      results[b] = static_cast<int>((valAcc[b] - 0.5f) * 2000.0f);
    }
    return results;
  }

  // Evaluate a batch of moves from a position by applying each move,
  // evaluating the resulting state, and returning scores.
  // This is the main integration point for move ordering.
  std::vector<int> evaluateMoves(
      WordBaseState& state,
      const std::vector<WordBaseMove>& moves) {
    if (!mLoaded || moves.empty()) {
      return std::vector<int>(moves.size(), 0);
    }

    // Build all child states
    std::vector<WordBaseState> childStates;
    std::vector<const WordBaseState*> childPtrs;
    childStates.reserve(moves.size());

    for (const auto& move : moves) {
      childStates.push_back(state);
      childStates.back().make_move(move);
      childPtrs.push_back(&childStates.back());
    }

    // Batch evaluate all children on GPU
    auto values = evaluateBatch(childPtrs);

    // Negate because child states are from opponent's perspective
    for (auto& v : values) {
      v = -v;
    }

    return values;
  }

  // Score moves using the policy head only (single forward pass, no state copying).
  // Returns a score for each move based on how much the policy head
  // likes the squares in that move's path. Much faster than evaluateMoves.
  std::vector<int> policyScoreMoves(
      const WordBaseState& state,
      const std::vector<WordBaseMove>& moves) {
    if (!mLoaded || moves.empty()) {
      return std::vector<int>(moves.size(), 0);
    }

    try {
      torch::NoGradGuard no_grad;
      auto input = stateToTensor(state).to(mDevice);
      auto outputs = mModel.forward({input}).toTuple();
      // policy_logits: (1, 13, 10)
      auto policyLogits = outputs->elements()[0].toTensor().to(torch::kCPU);
      auto pAcc = policyLogits.accessor<float, 3>();
      const auto& boardStatic = state.getBoardStatic();

      std::vector<int> scores(moves.size());
      for (size_t i = 0; i < moves.size(); i++) {
        const auto& path = boardStatic.getLegalWord(moves[i].mLegalWordId).mWordSequence;
        float score = 0.0f;
        for (const auto& cell : path) {
          score += pAcc[0][cell.first][cell.second];
        }
        // Scale to integer range comparable with heuristic goodness
        scores[i] = static_cast<int>(score * 100.0f);
      }
      return scores;
    } catch (const std::exception& e) {
      std::cerr << "Policy score error: " << e.what() << std::endl;
      return std::vector<int>(moves.size(), 0);
    }
  }

  // Warmup: run multiple forward passes to initialize all CUDA kernels.
  void warmup(const WordBaseState& state, int numPasses = 5) {
    if (!mLoaded) return;
    torch::NoGradGuard no_grad;
    for (int i = 0; i < numPasses; i++) {
      auto input = stateToTensor(state).to(mDevice);
      mModel.forward({input});
    }
  }

private:
  torch::jit::script::Module mModel;
  torch::Device mDevice = torch::kCPU;
  bool mLoaded = false;
  bool mGPUAvailable = false;
  bool mUseGPU;
};

#endif // HAS_TORCH

#endif // NEURAL_EVAL_H
