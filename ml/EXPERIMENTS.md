# Neural Move Ordering Experiments

## Architecture

Dual-head ResNet for Wordbase move ordering:
- **Input:** 7 channels x 13 rows x 10 cols (letter identity, ownership, bombs, progress)
- **Trunk:** Input conv + N residual blocks (each: 2x conv3x3 + BN + skip connection)
- **Policy head:** Conv 1x1 -> per-square logits (which squares are in the chosen move's path)
- **Value head:** Conv 1x1 -> FC -> sigmoid (win probability)
- **Loss:** BCEWithLogitsLoss (policy) + MSELoss (value), weighted sum

## Training Data

Self-play JSONL from `perf-test --selfplay-games N --selfplay-out file.jsonl`.
Each record has board text, owner string, chosen move path, search depth/nodes.
Game outcome (win/loss/draw) used as value target.

Data sources:
- `selfplay-500g-0.5s.jsonl` — 500 games on default board, 0.5s/move
- `selfplay-{board}-100g.jsonl` — 100 games per board, 0.5s/move
- `selfplay-{board}-200g-v2.jsonl` — 200 games per board, 0.5s/move
- `selfplay-{board}-200g-5s.jsonl` — 200 games per board, 5s/move (higher quality)

Total: ~40K samples at time of best model training.

## Benchmark Suite

6 boards in `scripts/benchmark-board-suite.txt`, measured with:
```
--seconds 0.2 --max-depth 4 --max-moves 200 --max-turns 2 --warmup-turns 2 --repeat-searches 5
```

Metric: `avg_nodes_per_turn` (lower = better move ordering, more pruning).

## Experiment Results (2026-03-09)

### Adopted: 4 residual blocks (was 6)

| Board | Baseline | 6-res (517K) | 4-res (369K) |
|-------|----------|-------------|-------------|
| readme-main | 3507 | 3143 | 3143 |
| driver-sample-a | 1699 | 1380 | 1380 |
| driver-sample-b | 402 | 402 | 402 |
| generated-1 | 1293 | 680 | 680 |
| generated-2 | 1334 | 538 | 538 |
| generated-3 | 742 | 544 | 544 |

**Result:** Identical performance, 28% fewer parameters. Adopted as default.

### Rejected: Horizontal flip augmentation

Flipping the board horizontally before training. Letters are not symmetric under
horizontal flip (different words exist), so this creates invalid training signal.
driver-sample-b regressed from 402 to 1810 nodes.

### Rejected: Value weight 0.1 (was 1.0)

Reducing value head weight to deprioritize it. driver-sample-b regressed from
402 to 1770. The value head contributes useful move ordering information.

### Rejected: 2 residual blocks

Too small — driver-sample-b regressed from 402 to 1430. Insufficient capacity.

### Rejected: 128 channels (was 64)

4 res blocks with 128 channels (1.26M params). driver-sample-b regressed from
402 to 1465. Likely overfitting with current data size.

### Rejected: Retraining with additional data

Retraining with ~43K samples (vs ~40K) shifted the distribution and caused
driver-sample-b to regress from 402 to 3670. Data composition matters — the
incoming 5s data changed the balance. The model checkpoint from the earlier
training run was restored.

## Key Observations

1. **driver-sample-b is the canary** — most changes that look neutral on other
   boards degrade this one. It appears to require precise move ordering.
2. **Model capacity sweet spot** is 4 res blocks x 64 channels (369K params)
   for ~40K training samples.
3. **Data quality vs quantity** — more data isn't always better if it shifts
   the training distribution. Need to be careful about data composition.
4. **Value head matters** — reducing its weight hurts, suggesting the value
   prediction provides complementary signal to the policy head.

## Current Best Model

- **Architecture:** 4 res blocks, 64 channels, 369,322 params
- **Training:** 100 epochs, batch 256, lr 1e-3, AdamW + CosineAnnealing
- **Data:** ~40K samples from 0.5s and 5s self-play
- **Checkpoint:** `ml/checkpoints/wordbase_policy.pt` (TorchScript)
