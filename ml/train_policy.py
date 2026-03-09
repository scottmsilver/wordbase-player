#!/usr/bin/env python3
"""
Train a move-ordering policy network for Wordbase.

The network learns to predict which move the minimax search would choose,
given a board state. This can be used to order moves better than the
hand-crafted goodness heuristic, leading to better alpha-beta pruning.

Input features (per board position, 13x10 grid):
  - Channel 0: letter identity (a=1..z=26, normalized)
  - Channel 1: owned by current player (1.0 or 0.0)
  - Channel 2: owned by opponent (1.0 or 0.0)
  - Channel 3: bomb location (1.0 or 0.0)
  - Channel 4: megabomb location (1.0 or 0.0)
  - Channel 5: unowned (1.0 or 0.0)
  - Channel 6: row normalized (0.0 at current player's home, 1.0 at goal)

Output: value head predicting the search evaluation (goodness).

For move ordering we'll eventually use the value network to score
each candidate move's resulting position, but we start with value
prediction as the foundation.
"""

import argparse
import json
import os
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, Dataset, random_split

BOARD_HEIGHT = 13
BOARD_WIDTH = 10
NUM_CHANNELS = 7


def parse_board_and_owners(board_text: str, owners_text: str, player: int):
    """Convert board text + owner string into a feature tensor.

    Returns a (NUM_CHANNELS, BOARD_HEIGHT, BOARD_WIDTH) float tensor.
    """
    features = np.zeros((NUM_CHANNELS, BOARD_HEIGHT, BOARD_WIDTH), dtype=np.float32)

    # Parse board text: letters with optional */+ bomb prefixes
    letters = []
    bombs = set()
    megabombs = set()
    idx = 0
    for ch in board_text:
        if ch == "*":
            bombs.add(idx)
        elif ch == "+":
            megabombs.add(idx)
        elif ch != " ":
            letters.append(ch)
            idx += 1

    if len(letters) != BOARD_HEIGHT * BOARD_WIDTH:
        return None

    # Channel 0: letter identity (a=1..z=26, normalized to 0..1)
    for i, letter in enumerate(letters):
        y, x = divmod(i, BOARD_WIDTH)
        features[0, y, x] = (ord(letter.lower()) - ord("a") + 1) / 26.0

    # Parse owners
    if len(owners_text) != BOARD_HEIGHT * BOARD_WIDTH:
        return None

    for i, owner_ch in enumerate(owners_text):
        y, x = divmod(i, BOARD_WIDTH)
        owner = int(owner_ch)

        if player == 1:
            # Player 1 moves top→bottom
            features[1, y, x] = 1.0 if owner == 1 else 0.0  # current player
            features[2, y, x] = 1.0 if owner == 2 else 0.0  # opponent
            features[6, y, x] = y / (BOARD_HEIGHT - 1)  # progress toward bottom
        else:
            # Player 2 moves bottom→top
            features[1, y, x] = 1.0 if owner == 2 else 0.0  # current player
            features[2, y, x] = 1.0 if owner == 1 else 0.0  # opponent
            features[6, y, x] = 1.0 - y / (BOARD_HEIGHT - 1)  # progress toward top

        features[3, y, x] = 1.0 if owner == 3 else 0.0  # bomb
        features[4, y, x] = 1.0 if owner == 4 else 0.0  # megabomb
        features[5, y, x] = 1.0 if owner == 0 else 0.0  # unowned

    # Also mark bombs from board text (bombs parsed from */+ markers)
    for bomb_idx in bombs:
        y, x = divmod(bomb_idx, BOARD_WIDTH)
        features[3, y, x] = 1.0
    for megabomb_idx in megabombs:
        y, x = divmod(megabomb_idx, BOARD_WIDTH)
        features[4, y, x] = 1.0

    return features


def parse_move_path(path_str: str):
    """Convert path string 'y1,x1;y2,x2;...' to a binary mask."""
    mask = np.zeros((BOARD_HEIGHT, BOARD_WIDTH), dtype=np.float32)
    for coord in path_str.split(";"):
        parts = coord.strip().split(",")
        if len(parts) == 2:
            y, x = int(parts[0]), int(parts[1])
            if 0 <= y < BOARD_HEIGHT and 0 <= x < BOARD_WIDTH:
                mask[y, x] = 1.0
    return mask


class WordbaseSelfPlayDataset(Dataset):
    """Dataset of (board_state, move_path, search_value) from self-play JSONL."""

    def __init__(self, jsonl_paths, min_depth=2):
        self.samples = []

        for jsonl_path in jsonl_paths:
            game_outcomes = {}  # game_id -> winner
            moves = []

            with open(jsonl_path) as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        record = json.loads(line)
                    except json.JSONDecodeError:
                        continue  # skip partial lines from in-progress writes
                    if record["type"] == "summary":
                        game_outcomes[record["game"]] = record
                    elif record["type"] == "move":
                        if record.get("depth", 0) >= min_depth:
                            moves.append(record)

            for move in moves:
                features = parse_board_and_owners(move["board"], move["owners"], move["player"])
                if features is None:
                    continue

                move_mask = parse_move_path(move["path"])

                # Normalize goodness relative to current player
                # The search already returns from current player's perspective
                # Use the game outcome as a training signal too
                game_id = move["game"]
                outcome = game_outcomes.get(game_id, {})
                winner = outcome.get("winner", "draw")

                # Target: 1.0 for win, 0.0 for loss, 0.5 for draw
                if winner == "draw":
                    outcome_value = 0.5
                elif (winner == "PLAYER_1" and move["player"] == 1) or (winner == "PLAYER_2" and move["player"] == 2):
                    outcome_value = 1.0
                else:
                    outcome_value = 0.0

                self.samples.append(
                    {
                        "features": features,
                        "move_mask": move_mask,
                        "outcome": np.float32(outcome_value),
                        "depth": move["depth"],
                        "nodes": move["nodes"],
                        "ply": move["ply"],
                    }
                )

        print(f"Loaded {len(self.samples)} samples from {len(jsonl_paths)} files")

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        s = self.samples[idx]
        return (
            torch.from_numpy(s["features"]),
            torch.from_numpy(s["move_mask"]),
            torch.tensor(s["outcome"]),
        )


class WordbaseResBlock(nn.Module):
    """Residual block with two conv layers."""

    def __init__(self, channels):
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn1 = nn.BatchNorm2d(channels)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1, bias=False)
        self.bn2 = nn.BatchNorm2d(channels)

    def forward(self, x):
        residual = x
        out = F.relu(self.bn1(self.conv1(x)))
        out = self.bn2(self.conv2(out))
        out += residual
        return F.relu(out)


class WordbaseNet(nn.Module):
    """
    Dual-head network for Wordbase:
    - Policy head: predicts probability of each square being part of the chosen move
    - Value head: predicts game outcome from current position

    Architecture inspired by AlphaGo/AlphaZero but much smaller
    (this is a 13x10 board, not 19x19).
    """

    def __init__(self, num_res_blocks=6, channels=64):
        super().__init__()

        # Input convolution
        self.input_conv = nn.Sequential(
            nn.Conv2d(NUM_CHANNELS, channels, 3, padding=1, bias=False),
            nn.BatchNorm2d(channels),
            nn.ReLU(),
        )

        # Residual tower
        self.res_blocks = nn.Sequential(*[WordbaseResBlock(channels) for _ in range(num_res_blocks)])

        # Policy head: per-square probability of being in the move path
        self.policy_head = nn.Sequential(
            nn.Conv2d(channels, 32, 1, bias=False),
            nn.BatchNorm2d(32),
            nn.ReLU(),
            nn.Conv2d(32, 1, 1),
        )

        # Value head: scalar position evaluation
        self.value_head = nn.Sequential(
            nn.Conv2d(channels, 4, 1, bias=False),
            nn.BatchNorm2d(4),
            nn.ReLU(),
            nn.Flatten(),
            nn.Linear(4 * BOARD_HEIGHT * BOARD_WIDTH, 128),
            nn.ReLU(),
            nn.Linear(128, 1),
            nn.Sigmoid(),  # output in [0, 1]
        )

    def forward(self, x):
        # Shared trunk
        out = self.input_conv(x)
        out = self.res_blocks(out)

        # Policy: per-square logits
        policy_logits = self.policy_head(out).squeeze(1)  # (B, 13, 10)

        # Value: scalar
        value = self.value_head(out).squeeze(1)  # (B,)

        return policy_logits, value


def train(args):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    print(f"Using device: {device}")

    # Find all JSONL files
    data_dir = Path(args.data_dir)
    jsonl_files = sorted(data_dir.glob("selfplay-*.jsonl"))
    if not jsonl_files:
        print(f"No selfplay JSONL files found in {data_dir}")
        sys.exit(1)
    print(f"Found {len(jsonl_files)} data files: {[f.name for f in jsonl_files]}")

    # Load dataset
    dataset = WordbaseSelfPlayDataset(
        jsonl_files,
        min_depth=args.min_depth,
    )
    if len(dataset) == 0:
        print("No valid samples found")
        sys.exit(1)

    # Split train/val
    val_size = max(1, int(len(dataset) * 0.1))
    train_size = len(dataset) - val_size
    train_dataset, val_dataset = random_split(
        dataset, [train_size, val_size], generator=torch.Generator().manual_seed(42)
    )
    print(f"Train: {train_size}, Val: {val_size}")

    train_loader = DataLoader(train_dataset, batch_size=args.batch_size, shuffle=True, num_workers=2, pin_memory=True)
    val_loader = DataLoader(val_dataset, batch_size=args.batch_size, shuffle=False, num_workers=2, pin_memory=True)

    # Model
    model = WordbaseNet(num_res_blocks=args.res_blocks, channels=args.channels).to(device)
    param_count = sum(p.numel() for p in model.parameters())
    print(f"Model: {args.res_blocks} res blocks, {args.channels} channels, {param_count:,} params")

    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    # Loss functions
    policy_loss_fn = nn.BCEWithLogitsLoss()
    value_loss_fn = nn.MSELoss()

    best_val_loss = float("inf")
    save_dir = Path(args.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    for epoch in range(args.epochs):
        # Train
        model.train()
        train_policy_loss = 0.0
        train_value_loss = 0.0
        train_count = 0

        for features, move_mask, outcome in train_loader:
            features = features.to(device)
            move_mask = move_mask.to(device)
            outcome = outcome.to(device)

            policy_logits, value = model(features)

            p_loss = policy_loss_fn(policy_logits, move_mask)
            v_loss = value_loss_fn(value, outcome)
            loss = p_loss + args.value_weight * v_loss

            optimizer.zero_grad()
            loss.backward()
            nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            batch_size = features.size(0)
            train_policy_loss += p_loss.item() * batch_size
            train_value_loss += v_loss.item() * batch_size
            train_count += batch_size

        scheduler.step()

        train_policy_loss /= train_count
        train_value_loss /= train_count

        # Validate
        model.eval()
        val_policy_loss = 0.0
        val_value_loss = 0.0
        val_policy_acc = 0.0
        val_count = 0

        with torch.no_grad():
            for features, move_mask, outcome in val_loader:
                features = features.to(device)
                move_mask = move_mask.to(device)
                outcome = outcome.to(device)

                policy_logits, value = model(features)

                p_loss = policy_loss_fn(policy_logits, move_mask)
                v_loss = value_loss_fn(value, outcome)

                # Policy accuracy: fraction of move squares correctly predicted
                policy_pred = (torch.sigmoid(policy_logits) > 0.5).float()
                correct_squares = (policy_pred == move_mask).float().mean()

                batch_size = features.size(0)
                val_policy_loss += p_loss.item() * batch_size
                val_value_loss += v_loss.item() * batch_size
                val_policy_acc += correct_squares.item() * batch_size
                val_count += batch_size

        val_policy_loss /= val_count
        val_value_loss /= val_count
        val_policy_acc /= val_count
        val_total = val_policy_loss + args.value_weight * val_value_loss

        lr = optimizer.param_groups[0]["lr"]
        print(
            f"Epoch {epoch+1:3d}/{args.epochs}  "
            f"train_p={train_policy_loss:.4f} train_v={train_value_loss:.4f}  "
            f"val_p={val_policy_loss:.4f} val_v={val_value_loss:.4f} "
            f"val_acc={val_policy_acc:.4f}  lr={lr:.6f}"
        )

        if val_total < best_val_loss:
            best_val_loss = val_total
            checkpoint_path = save_dir / "best_model.pt"
            torch.save(
                {
                    "epoch": epoch,
                    "model_state_dict": model.state_dict(),
                    "optimizer_state_dict": optimizer.state_dict(),
                    "val_policy_loss": val_policy_loss,
                    "val_value_loss": val_value_loss,
                    "args": vars(args),
                },
                checkpoint_path,
            )
            print(f"  -> saved best model (val_total={val_total:.4f})")

    # Export to TorchScript for C++ inference
    model.eval()
    model_cpu = model.cpu()
    example_input = torch.randn(1, NUM_CHANNELS, BOARD_HEIGHT, BOARD_WIDTH)
    scripted = torch.jit.trace(model_cpu, example_input)
    script_path = save_dir / "wordbase_policy.pt"
    scripted.save(str(script_path))
    print(f"Exported TorchScript model to {script_path}")

    # Also export ONNX
    try:
        onnx_path = save_dir / "wordbase_policy.onnx"
        torch.onnx.export(
            model_cpu,
            example_input,
            str(onnx_path),
            input_names=["board_state"],
            output_names=["policy_logits", "value"],
            dynamic_axes={"board_state": {0: "batch"}},
            opset_version=17,
        )
        print(f"Exported ONNX model to {onnx_path}")
    except Exception as e:
        print(f"ONNX export failed (non-critical): {e}")


def main():
    parser = argparse.ArgumentParser(description="Train Wordbase policy/value network")
    parser.add_argument("--data-dir", default="data", help="Directory with selfplay JSONL files")
    parser.add_argument("--save-dir", default="ml/checkpoints", help="Where to save models")
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=128)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--res-blocks", type=int, default=6, help="Number of residual blocks")
    parser.add_argument("--channels", type=int, default=64, help="Conv channel width")
    parser.add_argument("--value-weight", type=float, default=1.0, help="Weight for value loss")
    parser.add_argument("--min-depth", type=int, default=2, help="Min search depth to include sample")
    args = parser.parse_args()
    train(args)


if __name__ == "__main__":
    main()
