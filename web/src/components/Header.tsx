import React from 'react';
import { View, Text, StyleSheet, TouchableOpacity } from 'react-native';
import { CellOwner } from '../game/types';

interface HeaderProps {
  currentPlayer: CellOwner.PLAYER_1 | CellOwner.PLAYER_2;
  gameOver: boolean;
  winner: CellOwner.PLAYER_1 | CellOwner.PLAYER_2 | null;
  currentWord: string;
  onMenu: () => void;
}

export const Header: React.FC<HeaderProps> = ({
  currentPlayer,
  gameOver,
  winner,
  currentWord,
  onMenu,
}) => {
  const p1Active = currentPlayer === CellOwner.PLAYER_1;

  return (
    <View style={styles.container}>
      <View style={styles.row}>
        <TouchableOpacity style={styles.btn} onPress={onMenu} activeOpacity={0.7}>
          <Text style={styles.btnText}>MENU</Text>
        </TouchableOpacity>
        <View style={styles.center}>
          {currentWord ? (
            <Text style={styles.wordText}>{currentWord}</Text>
          ) : gameOver && winner ? (
            <Text style={styles.winText}>
              {winner === CellOwner.PLAYER_1 ? 'ORANGE' : 'BLUE'} WINS!
            </Text>
          ) : (
            <View style={styles.badges}>
              <View style={[styles.badge, styles.p1Badge, !p1Active && styles.inactive]}>
                <Text style={styles.badgeText}>orange</Text>
              </View>
              <Text style={styles.arrow}>{p1Active ? '>' : '<'}</Text>
              <View style={[styles.badge, styles.p2Badge, p1Active && styles.inactive]}>
                <Text style={styles.badgeText}>blue</Text>
              </View>
            </View>
          )}
        </View>
        <TouchableOpacity style={styles.btn} activeOpacity={0.7}>
          <Text style={styles.btnText}>HOW TO</Text>
        </TouchableOpacity>
      </View>
    </View>
  );
};

const styles = StyleSheet.create({
  container: {
    width: '100%',
  },
  row: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 4,
  },
  badges: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 4,
  },
  badge: {
    paddingHorizontal: 14,
    paddingVertical: 3,
  },
  p1Badge: {
    backgroundColor: '#F5A623',
  },
  p2Badge: {
    backgroundColor: '#4ABFF7',
  },
  inactive: {
    opacity: 0.45,
  },
  badgeText: {
    color: '#FFFFFF',
    fontWeight: '700',
    fontSize: 14,
    fontFamily: 'monospace',
  },
  arrow: {
    fontSize: 14,
    fontWeight: '700',
    color: '#555',
    fontFamily: 'monospace',
    marginHorizontal: 2,
  },
  btn: {
    backgroundColor: '#1A1A1A',
    paddingHorizontal: 10,
    paddingVertical: 5,
  },
  btnText: {
    color: '#FFFFFF',
    fontWeight: '700',
    fontSize: 11,
    fontFamily: 'monospace',
    letterSpacing: 1,
  },
  center: {
    flex: 1,
    alignItems: 'center',
  },
  wordText: {
    fontSize: 16,
    fontWeight: '700',
    fontFamily: 'monospace',
    color: '#333',
  },
  winText: {
    fontSize: 18,
    fontWeight: '700',
    fontFamily: 'monospace',
    color: '#222',
  },
});
