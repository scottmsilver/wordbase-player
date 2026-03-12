import { Cell, CellOwner, ROWS, COLS } from './types';

// English letter frequency weights
const LETTER_WEIGHTS: [string, number][] = [
  ['A', 8.2], ['B', 1.5], ['C', 2.8], ['D', 4.3], ['E', 12.7],
  ['F', 2.2], ['G', 2.0], ['H', 6.1], ['I', 7.0], ['J', 0.2],
  ['K', 0.8], ['L', 4.0], ['M', 2.4], ['N', 6.7], ['O', 7.5],
  ['P', 1.9], ['Q', 0.1], ['R', 6.0], ['S', 6.3], ['T', 9.1],
  ['U', 2.8], ['V', 1.0], ['W', 2.4], ['X', 0.2], ['Y', 2.0],
  ['Z', 0.1],
];

function weightedRandomLetter(): string {
  const totalWeight = LETTER_WEIGHTS.reduce((sum, [, w]) => sum + w, 0);
  let r = Math.random() * totalWeight;
  for (const [letter, weight] of LETTER_WEIGHTS) {
    r -= weight;
    if (r <= 0) return letter;
  }
  return 'E';
}

export function generateBoard(): Cell[][] {
  const board: Cell[][] = [];

  for (let row = 0; row < ROWS; row++) {
    const rowCells: Cell[] = [];
    for (let col = 0; col < COLS; col++) {
      let owner = CellOwner.UNOWNED;
      if (row === 0) owner = CellOwner.PLAYER_1;
      else if (row === ROWS - 1) owner = CellOwner.PLAYER_2;

      rowCells.push({
        letter: weightedRandomLetter(),
        owner,
      });
    }
    board.push(rowCells);
  }

  // Place bombs in middle rows (rows 2-10), avoid top/bottom 2 rows
  const numBombs = 4 + Math.floor(Math.random() * 3); // 4-6 bombs
  const bombPositions = new Set<string>();
  while (bombPositions.size < numBombs) {
    const row = 2 + Math.floor(Math.random() * 9); // rows 2-10
    const col = Math.floor(Math.random() * COLS);
    const key = `${row},${col}`;
    if (!bombPositions.has(key)) {
      bombPositions.add(key);
      board[row][col].owner = CellOwner.BOMB;
    }
  }

  // Place 1-2 megabombs
  const numMegabombs = Math.random() < 0.5 ? 1 : 2;
  let megabombsPlaced = 0;
  while (megabombsPlaced < numMegabombs) {
    const row = 3 + Math.floor(Math.random() * 7); // rows 3-9
    const col = Math.floor(Math.random() * COLS);
    const key = `${row},${col}`;
    if (!bombPositions.has(key)) {
      bombPositions.add(key);
      board[row][col].owner = CellOwner.MEGABOMB;
      megabombsPlaced++;
    }
  }

  return board;
}
