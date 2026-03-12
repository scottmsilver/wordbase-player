import { Cell, CellOwner, GameState, Position, ROWS, COLS } from './types';
import { dictionary } from './Dictionary';
import { generateBoard } from './BoardGenerator';

export function createGame(): GameState {
  return {
    board: generateBoard(),
    currentPlayer: CellOwner.PLAYER_1,
    gameOver: false,
    winner: null,
    lastWord: null,
    lastPath: null,
    playedWords: new Set(),
    moveHistory: [],
  };
}

export function cloneBoard(board: Cell[][]): Cell[][] {
  return board.map(row => row.map(cell => ({ ...cell })));
}

export function isAdjacent(a: Position, b: Position): boolean {
  const dr = Math.abs(a.row - b.row);
  const dc = Math.abs(a.col - b.col);
  return dr <= 1 && dc <= 1 && (dr + dc > 0);
}

function inBounds(row: number, col: number): boolean {
  return row >= 0 && row < ROWS && col >= 0 && col < COLS;
}

export function isValidPath(path: Position[]): boolean {
  if (path.length < 2) return false;

  // Check all tiles are in bounds and adjacent
  const visited = new Set<string>();
  for (let i = 0; i < path.length; i++) {
    const pos = path[i];
    if (!inBounds(pos.row, pos.col)) return false;

    const key = `${pos.row},${pos.col}`;
    if (visited.has(key)) return false;
    visited.add(key);

    if (i > 0 && !isAdjacent(path[i - 1], pos)) return false;
  }
  return true;
}

function getEffectiveOwner(owner: CellOwner): CellOwner {
  if (owner === CellOwner.BOMB || owner === CellOwner.MEGABOMB) {
    return CellOwner.UNOWNED;
  }
  return owner;
}

export function canPlayWord(state: GameState, path: Position[]): boolean {
  if (!isValidPath(path)) return false;

  // Build the word from the path
  const word = path.map(p => state.board[p.row][p.col].letter).join('').toLowerCase();

  // Must be at least 2 letters
  if (word.length < 2) return false;

  // Must be a valid dictionary word
  if (!dictionary.hasWord(word)) return false;

  // Word must not have been played before
  if (state.playedWords.has(word)) return false;

  // At least one tile in the path must be owned by the current player
  const hasOwnedTile = path.some(p => {
    const owner = getEffectiveOwner(state.board[p.row][p.col].owner);
    return owner === state.currentPlayer;
  });
  if (!hasOwnedTile) return false;

  // The path must be connected to the player's territory
  // (the first owned tile found while tracing from the start must connect
  // back through adjacent owned tiles to the main territory)
  // Simplified: just require at least one tile is owned by current player
  return true;
}

export function getWordFromPath(board: Cell[][], path: Position[]): string {
  return path.map(p => board[p.row][p.col].letter).join('');
}

// 4-directional neighbors (for bomb explosion)
const DIRS_4: Position[] = [
  { row: -1, col: 0 }, { row: 1, col: 0 },
  { row: 0, col: -1 }, { row: 0, col: 1 },
];

// 8-directional neighbors (for megabomb explosion)
const DIRS_8: Position[] = [
  { row: -1, col: -1 }, { row: -1, col: 0 }, { row: -1, col: 1 },
  { row: 0, col: -1 },                        { row: 0, col: 1 },
  { row: 1, col: -1 },  { row: 1, col: 0 },  { row: 1, col: 1 },
];

function claimCell(
  board: Cell[][],
  row: number,
  col: number,
  player: CellOwner.PLAYER_1 | CellOwner.PLAYER_2,
  claimed: Set<string>
): void {
  const key = `${row},${col}`;
  if (claimed.has(key)) return;
  if (!inBounds(row, col)) return;

  const cell = board[row][col];
  const wasBomb = cell.owner === CellOwner.BOMB;
  const wasMegabomb = cell.owner === CellOwner.MEGABOMB;

  cell.owner = player;
  claimed.add(key);

  // Trigger bomb explosions
  if (wasBomb) {
    for (const dir of DIRS_4) {
      const nr = row + dir.row;
      const nc = col + dir.col;
      if (inBounds(nr, nc)) {
        claimCell(board, nr, nc, player, claimed);
      }
    }
  }

  if (wasMegabomb) {
    for (const dir of DIRS_8) {
      const nr = row + dir.row;
      const nc = col + dir.col;
      if (inBounds(nr, nc)) {
        claimCell(board, nr, nc, player, claimed);
      }
    }
  }
}

function floodFillConnected(
  board: Cell[][],
  player: CellOwner.PLAYER_1 | CellOwner.PLAYER_2
): Set<string> {
  const connected = new Set<string>();
  const queue: Position[] = [];

  // Start from home edge
  const homeRow = player === CellOwner.PLAYER_1 ? 0 : ROWS - 1;
  for (let col = 0; col < COLS; col++) {
    if (board[homeRow][col].owner === player) {
      const key = `${homeRow},${col}`;
      connected.add(key);
      queue.push({ row: homeRow, col });
    }
  }

  // BFS through all connected cells of this player
  let head = 0;
  while (head < queue.length) {
    const pos = queue[head++];
    for (const dir of DIRS_8) {
      const nr = pos.row + dir.row;
      const nc = pos.col + dir.col;
      if (!inBounds(nr, nc)) continue;
      const key = `${nr},${nc}`;
      if (connected.has(key)) continue;
      if (board[nr][nc].owner === player) {
        connected.add(key);
        queue.push({ row: nr, col: nc });
      }
    }
  }

  return connected;
}

function removeDisconnected(
  board: Cell[][],
  player: CellOwner.PLAYER_1 | CellOwner.PLAYER_2
): void {
  const connected = floodFillConnected(board, player);
  for (let row = 0; row < ROWS; row++) {
    for (let col = 0; col < COLS; col++) {
      if (board[row][col].owner === player) {
        const key = `${row},${col}`;
        if (!connected.has(key)) {
          board[row][col].owner = CellOwner.UNOWNED;
        }
      }
    }
  }
}

function checkWinner(board: Cell[][]): CellOwner.PLAYER_1 | CellOwner.PLAYER_2 | null {
  // Player 1 wins if they have a cell in the bottom row
  for (let col = 0; col < COLS; col++) {
    if (board[ROWS - 1][col].owner === CellOwner.PLAYER_1) return CellOwner.PLAYER_1;
  }
  // Player 2 wins if they have a cell in the top row
  for (let col = 0; col < COLS; col++) {
    if (board[0][col].owner === CellOwner.PLAYER_2) return CellOwner.PLAYER_2;
  }
  return null;
}

export function executeMove(state: GameState, path: Position[]): GameState {
  const newBoard = cloneBoard(state.board);
  const player = state.currentPlayer;
  const word = getWordFromPath(newBoard, path);

  // Claim all cells in the path (with bomb chain reactions)
  const claimed = new Set<string>();
  for (const pos of path) {
    claimCell(newBoard, pos.row, pos.col, player, claimed);
  }

  // Remove disconnected enemy cells
  const enemy = player === CellOwner.PLAYER_1 ? CellOwner.PLAYER_2 : CellOwner.PLAYER_1;
  removeDisconnected(newBoard, enemy);

  // Check for winner
  const winner = checkWinner(newBoard);

  const newPlayedWords = new Set(state.playedWords);
  newPlayedWords.add(word.toLowerCase());

  return {
    board: newBoard,
    currentPlayer: winner ? state.currentPlayer : enemy,
    gameOver: winner !== null,
    winner,
    lastWord: word,
    lastPath: path,
    playedWords: newPlayedWords,
    moveHistory: [...state.moveHistory, { player, word }],
  };
}

