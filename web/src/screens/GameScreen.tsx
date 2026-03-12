import React, { useState, useCallback, useEffect, useRef } from 'react';
import { View, Text, StyleSheet, TouchableOpacity, useWindowDimensions, Platform } from 'react-native';
import { Board } from '../components/Board';
import { Header } from '../components/Header';
import { MoveHistory } from '../components/MoveHistory';
import { Cell, GameState, Position, CellOwner, ROWS, COLS } from '../game/types';
import { createGame, executeMove, canPlayWord, getWordFromPath } from '../game/GameEngine';
import { dictionary } from '../game/Dictionary';

// Convert board to 130-char lowercase letter string for the C++ engine
function boardToLetters(board: Cell[][]): string {
  return board.map(row => row.map(cell => cell.letter.toLowerCase()).join('')).join('');
}

// Convert board to 130-char ownership string (digits 0-4)
function boardToOwners(board: Cell[][]): string {
  return board.map(row => row.map(cell => cell.owner.toString()).join('')).join('');
}

// Derive engine server URL from current page (never hardcoded)
function getEngineUrl(): string {
  if (Platform.OS !== 'web') return '';
  const hostname = typeof window !== 'undefined' ? window.location.hostname : 'localhost';
  const protocol = typeof window !== 'undefined' ? window.location.protocol : 'http:';
  return `${protocol}//${hostname}:3001`;
}

async function requestEngineMove(
  game: GameState,
  seconds: number = 2.0,
  depth: number = 10,
  signal?: AbortSignal,
): Promise<{ word: string; path: Position[] } | null> {
  try {
    const url = getEngineUrl();
    const response = await fetch(`${url}/api/move`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        board: boardToLetters(game.board),
        owners: boardToOwners(game.board),
        played: Array.from(game.playedWords),
        player: game.currentPlayer,
        seconds,
        depth,
      }),
      signal,
    });
    const data = await response.json();
    if (data.error) {
      console.warn('Engine error:', data.error);
      return null;
    }
    // Convert [[row,col],...] to Position[]
    const path: Position[] = data.path.map((p: number[]) => ({ row: p[0], col: p[1] }));
    return { word: data.word, path };
  } catch (e) {
    if ((e as Error).name === 'AbortError') return null;
    console.warn('Engine request failed:', e);
    return null;
  }
}

const HEADER_HEIGHT = 36;
const BOTTOM_BAR_HEIGHT = 44;
const HISTORY_PANEL_WIDTH = 180;
const HISTORY_TAB_WIDTH = 24;

interface GameScreenProps {
  onBack: () => void;
}

export const GameScreen: React.FC<GameScreenProps> = ({ onBack }) => {
  const [game, setGame] = useState<GameState>(createGame);
  const [selectedPath, setSelectedPath] = useState<Position[]>([]);
  const [message, setMessage] = useState('');
  const [showHistory, setShowHistory] = useState(true);
  const { width: windowWidth, height: windowHeight } = useWindowDimensions();

  // Size the board to fill available height (board aspect = COLS/ROWS)
  const availableHeight = windowHeight - HEADER_HEIGHT - BOTTOM_BAR_HEIGHT;
  const boardFromHeight = availableHeight * (COLS / ROWS);
  const historyWidth = showHistory ? HISTORY_PANEL_WIDTH + HISTORY_TAB_WIDTH : HISTORY_TAB_WIDTH;
  const maxBoardWidth = windowWidth - historyWidth - 16;
  const boardWidth = Math.max(300, Math.min(boardFromHeight, maxBoardWidth));

  const isComputerTurn = game.currentPlayer === CellOwner.PLAYER_2 && !game.gameOver;

  const currentWord = selectedPath.length > 0
    ? getWordFromPath(game.board, selectedPath)
    : '';

  const isValidWord = currentWord.length >= 2 && dictionary.hasWord(currentWord.toLowerCase());
  const canSubmit = isValidWord && canPlayWord(game, selectedPath) && !isComputerTurn;

  const messageTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  const tryPlayWord = useCallback(
    (path: Position[]) => {
      if (game.currentPlayer === CellOwner.PLAYER_2) return; // computer's turn
      if (!dictionary.isLoaded()) {
        setMessage('Dictionary loading...');
        return;
      }

      if (canPlayWord(game, path)) {
        const newGame = executeMove(game, path);
        setGame(newGame);
        setSelectedPath([]);
        setMessage('');
      } else {
        const word = getWordFromPath(game.board, path);
        if (word.length < 2) {
          setMessage('Too short');
        } else if (!dictionary.hasWord(word.toLowerCase())) {
          setMessage(`"${word}" not in dictionary`);
        } else if (game.playedWords.has(word.toLowerCase())) {
          setMessage(`"${word}" already played`);
        } else {
          const hasOwned = path.some(p => game.board[p.row][p.col].owner === game.currentPlayer);
          if (!hasOwned) {
            setMessage('Must include your tile');
          } else {
            setMessage('Invalid move');
          }
        }
        setSelectedPath([]);
        if (messageTimerRef.current) clearTimeout(messageTimerRef.current);
        messageTimerRef.current = setTimeout(() => setMessage(''), 2000);
      }
    },
    [game]
  );

  const handlePathUpdate = useCallback((path: Position[]) => {
    setSelectedPath(path);
    setMessage(prev => prev === '' ? prev : '');
  }, []);

  const handleSubmit = useCallback(() => {
    if (selectedPath.length >= 2) {
      tryPlayWord(selectedPath);
    }
  }, [selectedPath, tryPlayWord]);

  const handleClear = useCallback(() => {
    setSelectedPath([]);
  }, []);

  const handleNewGame = useCallback(() => {
    if (messageTimerRef.current) clearTimeout(messageTimerRef.current);
    setGame(createGame());
    setSelectedPath([]);
    setMessage('');
  }, []);

  const toggleHistory = useCallback(() => {
    setShowHistory(v => !v);
  }, []);

  // Computer player (Player 2 / blue)
  useEffect(() => {
    if (game.currentPlayer !== CellOwner.PLAYER_2 || game.gameOver) return;

    const abortController = new AbortController();
    setMessage('Computer thinking...');

    requestEngineMove(game, 2.0, 10, abortController.signal).then((result) => {
      if (abortController.signal.aborted) return;
      if (!result) {
        setMessage('Engine unavailable');
        if (messageTimerRef.current) clearTimeout(messageTimerRef.current);
        messageTimerRef.current = setTimeout(() => setMessage(''), 3000);
        return;
      }

      // Apply the engine's move
      const newGame = executeMove(game, result.path);
      setGame(newGame);
      setSelectedPath([]);
      setMessage('');
    });

    return () => { abortController.abort(); };
  }, [game]);

  return (
    <View style={styles.container}>
      <View style={styles.mainRow}>
        <MoveHistory
          moves={game.moveHistory}
          visible={showHistory}
          onToggle={toggleHistory}
        />
        <View style={[styles.boardColumn, { width: boardWidth }]}>
          <Header
            currentPlayer={game.currentPlayer}
            gameOver={game.gameOver}
            winner={game.winner}
            currentWord={currentWord}
            onMenu={onBack}
          />
          {message ? (
            <View style={styles.messageBar}>
              <Text style={styles.messageText}>{message}</Text>
            </View>
          ) : null}
          <Board
            board={game.board}
            currentPlayer={game.currentPlayer}
            gameOver={game.gameOver}
            selectedPath={selectedPath}
            onPathUpdate={handlePathUpdate}
          />

          {/* Bottom bar: action bar, last word, or game over */}
          {selectedPath.length > 0 ? (
            <View style={styles.actionBar}>
              <TouchableOpacity style={styles.clearBtn} onPress={handleClear} activeOpacity={0.7}>
                <Text style={styles.clearBtnText}>CLEAR</Text>
              </TouchableOpacity>
              <Text style={[
                styles.wordPreview,
                isValidWord && styles.wordValid,
                (currentWord.length >= 2 && !isValidWord) && styles.wordInvalid,
              ]}>
                {currentWord}
              </Text>
              <TouchableOpacity
                style={[styles.submitBtn, canSubmit && styles.submitBtnActive]}
                onPress={handleSubmit}
                disabled={!canSubmit}
                activeOpacity={0.7}
              >
                <Text style={[styles.submitBtnText, canSubmit && styles.submitBtnTextActive]}>PLAY</Text>
              </TouchableOpacity>
            </View>
          ) : game.gameOver ? (
            <View style={styles.bottomBar}>
              <TouchableOpacity style={styles.newGameButton} onPress={handleNewGame}>
                <Text style={styles.newGameText}>NEW GAME</Text>
              </TouchableOpacity>
            </View>
          ) : game.lastWord && !message ? (
            <View style={styles.lastWordBar}>
              <Text style={styles.lastWordLabel}>
                {game.currentPlayer === CellOwner.PLAYER_1 ? 'BLUE' : 'ORANGE'} PLAYED
              </Text>
              <Text style={styles.lastWordHighlight}>{game.lastWord.toUpperCase()}</Text>
            </View>
          ) : null}
        </View>
      </View>
    </View>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#E8E8E8',
  },
  mainRow: {
    flex: 1,
    flexDirection: 'row',
    justifyContent: 'center',
  },
  boardColumn: {
    justifyContent: 'center',
  },
  messageBar: {
    backgroundColor: '#333',
    paddingHorizontal: 16,
    paddingVertical: 6,
    width: '100%',
    alignItems: 'center',
  },
  messageText: {
    color: '#FFF',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 13,
  },
  actionBar: {
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingVertical: 8,
    paddingHorizontal: 4,
  },
  clearBtn: {
    backgroundColor: '#888',
    paddingHorizontal: 14,
    paddingVertical: 8,
  },
  clearBtnText: {
    color: '#FFF',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 14,
  },
  wordPreview: {
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 18,
    color: '#999',
    flex: 1,
    textAlign: 'center',
  },
  wordValid: {
    color: '#2E7D32',
  },
  wordInvalid: {
    color: '#C62828',
  },
  submitBtn: {
    backgroundColor: '#CCC',
    paddingHorizontal: 14,
    paddingVertical: 8,
  },
  submitBtnActive: {
    backgroundColor: '#2E7D32',
  },
  submitBtnText: {
    color: '#FFF',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 14,
    opacity: 0.5,
  },
  submitBtnTextActive: {
    opacity: 1,
  },
  lastWordBar: {
    backgroundColor: '#333',
    paddingVertical: 10,
    paddingHorizontal: 16,
    alignItems: 'center',
    flexDirection: 'row',
    justifyContent: 'center',
    gap: 8,
  },
  lastWordLabel: {
    fontFamily: 'monospace',
    fontSize: 13,
    fontWeight: '700',
    color: '#AAA',
  },
  lastWordHighlight: {
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 18,
    color: '#FFF',
  },
  bottomBar: {
    paddingVertical: 10,
    alignItems: 'center',
  },
  newGameButton: {
    backgroundColor: '#222',
    paddingHorizontal: 24,
    paddingVertical: 12,
  },
  newGameText: {
    color: '#FFF',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 16,
  },
});
