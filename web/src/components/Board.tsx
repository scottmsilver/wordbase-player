import React, { useRef, useCallback, useState, useMemo, useEffect } from 'react';
import { View, StyleSheet, LayoutChangeEvent, Platform } from 'react-native';
import { Tile } from './Tile';
import { Cell, Position, ROWS, COLS, CellOwner } from '../game/types';
import { getWordFromPath, isAdjacent } from '../game/GameEngine';
import { dictionary } from '../game/Dictionary';

interface BoardProps {
  board: Cell[][];
  currentPlayer: CellOwner.PLAYER_1 | CellOwner.PLAYER_2;
  gameOver: boolean;
  selectedPath: Position[];
  onPathUpdate: (path: Position[]) => void;
}

export const Board: React.FC<BoardProps> = ({
  board,
  currentPlayer,
  gameOver,
  selectedPath,
  onPathUpdate,
}) => {
  const boardRef = useRef<View>(null);
  const [tileSize, setTileSize] = useState(30);
  const isDragging = useRef(false);
  const hasMoved = useRef(false);
  const pathRef = useRef<Position[]>([]);

  // Sync pathRef from prop (for when parent clears path)
  useEffect(() => {
    pathRef.current = selectedPath;
  }, [selectedPath]);

  // Stable refs for event handler access
  const boardDataRef = useRef(board);
  boardDataRef.current = board;
  const gameOverRef = useRef(gameOver);
  gameOverRef.current = gameOver;
  const onPathUpdateRef = useRef(onPathUpdate);
  onPathUpdateRef.current = onPathUpdate;

  const posToTile = useCallback((clientX: number, clientY: number): Position | null => {
    if (!boardRef.current) return null;
    const el = boardRef.current as any;
    if (typeof el.getBoundingClientRect !== 'function') return null;
    const rect = el.getBoundingClientRect();
    if (!rect || rect.width === 0) return null;
    const tw = rect.width / COLS;
    const th = rect.height / ROWS;
    const col = Math.floor((clientX - rect.left) / tw);
    const row = Math.floor((clientY - rect.top) / th);
    if (row >= 0 && row < ROWS && col >= 0 && col < COLS) {
      return { row, col };
    }
    return null;
  }, []);

  const samePos = (a: Position, b: Position) => a.row === b.row && a.col === b.col;

  const tryExtend = useCallback((pos: Position, currentPath: Position[]): Position[] | null => {
    // Backtracking
    if (currentPath.length >= 2) {
      const prev = currentPath[currentPath.length - 2];
      if (samePos(prev, pos)) {
        return currentPath.slice(0, -1);
      }
    }
    // Same tile
    if (currentPath.length > 0 && samePos(currentPath[currentPath.length - 1], pos)) {
      return null;
    }
    // Must be adjacent
    if (currentPath.length > 0 && !isAdjacent(currentPath[currentPath.length - 1], pos)) {
      return null;
    }
    // Must not revisit
    if (currentPath.some(p => samePos(p, pos))) return null;

    const newPath = [...currentPath, pos];
    const word = getWordFromPath(boardDataRef.current, newPath).toLowerCase();
    if (word.length <= 1 || dictionary.hasPrefix(word)) {
      return newPath;
    }
    return null;
  }, []);

  useEffect(() => {
    const el = boardRef.current as any;
    if (!el || Platform.OS !== 'web') return;

    // Helper: update path ref immediately AND notify parent
    const setPath = (newPath: Position[]) => {
      pathRef.current = newPath;
      onPathUpdateRef.current(newPath);
    };

    const onPointerDown = (e: PointerEvent) => {
      if (gameOverRef.current) return;
      e.preventDefault();
      e.stopPropagation();

      const pos = posToTile(e.clientX, e.clientY);
      if (!pos) return;

      isDragging.current = true;
      hasMoved.current = false;

      const cur = pathRef.current;

      // Tap mode: if we already have a path, try extending or backtracking
      if (cur.length > 0) {
        const last = cur[cur.length - 1];
        // Tap same tile = backtrack one
        if (samePos(last, pos)) {
          setPath(cur.length === 1 ? [] : cur.slice(0, -1));
          return;
        }
        // Tap adjacent = try extend; if fails (bad prefix/revisit), ignore
        if (isAdjacent(last, pos)) {
          const extended = tryExtend(pos, cur);
          if (extended) {
            setPath(extended);
          }
          return;
        }
        // Tap non-adjacent = start fresh
      }

      setPath([pos]);
    };

    const onPointerMove = (e: PointerEvent) => {
      if (!isDragging.current || gameOverRef.current) return;
      e.preventDefault();
      hasMoved.current = true;

      const pos = posToTile(e.clientX, e.clientY);
      if (!pos) return;

      const extended = tryExtend(pos, pathRef.current);
      if (extended) {
        setPath(extended);
      }
    };

    const onPointerUp = (e: PointerEvent) => {
      if (!isDragging.current) return;
      isDragging.current = false;
    };

    el.addEventListener('pointerdown', onPointerDown, { passive: false });
    document.addEventListener('pointermove', onPointerMove, { passive: false });
    document.addEventListener('pointerup', onPointerUp, { passive: false });

    return () => {
      el.removeEventListener('pointerdown', onPointerDown);
      document.removeEventListener('pointermove', onPointerMove);
      document.removeEventListener('pointerup', onPointerUp);
    };
  }, [posToTile, tryExtend]);

  const selectedSet = useMemo(
    () => new Set(selectedPath.map(p => `${p.row},${p.col}`)),
    [selectedPath]
  );

  const onLayout = useCallback((event: LayoutChangeEvent) => {
    const { width } = event.nativeEvent.layout;
    setTileSize(width / COLS);
  }, []);

  return (
    <View
      ref={boardRef}
      style={styles.board}
      onLayout={onLayout}
    >
      {board.map((row, rowIdx) => (
        <View key={rowIdx} style={styles.row}>
          {row.map((cell, colIdx) => {
            const key = `${rowIdx},${colIdx}`;
            return (
              <Tile
                key={key}
                letter={cell.letter}
                owner={cell.owner}
                isSelected={selectedSet.has(key)}
                size={tileSize}
              />
            );
          })}
        </View>
      ))}
    </View>
  );
};

const styles = StyleSheet.create({
  board: {
    width: '100%',
    aspectRatio: COLS / ROWS,
    cursor: 'default',
    userSelect: 'none',
    touchAction: 'none',
  } as any,
  row: {
    flexDirection: 'row',
  },
});
