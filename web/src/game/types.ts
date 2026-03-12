export const ROWS = 13;
export const COLS = 10;

export enum CellOwner {
  UNOWNED = 0,
  PLAYER_1 = 1, // Orange - top
  PLAYER_2 = 2, // Blue - bottom
  BOMB = 3,
  MEGABOMB = 4,
}

export interface Position {
  row: number;
  col: number;
}

export interface Cell {
  letter: string;
  owner: CellOwner;
}

export interface MoveRecord {
  player: CellOwner.PLAYER_1 | CellOwner.PLAYER_2;
  word: string;
}

export interface GameState {
  board: Cell[][];       // [row][col]
  currentPlayer: CellOwner.PLAYER_1 | CellOwner.PLAYER_2;
  gameOver: boolean;
  winner: CellOwner.PLAYER_1 | CellOwner.PLAYER_2 | null;
  lastWord: string | null;
  lastPath: Position[] | null;
  playedWords: Set<string>;
  moveHistory: MoveRecord[];
}
