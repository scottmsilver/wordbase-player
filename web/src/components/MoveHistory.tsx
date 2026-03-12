import React from 'react';
import { View, Text, StyleSheet, TouchableOpacity, ScrollView } from 'react-native';
import { MoveRecord, CellOwner } from '../game/types';

interface MoveHistoryProps {
  moves: MoveRecord[];
  visible: boolean;
  onToggle: () => void;
}

export const MoveHistory: React.FC<MoveHistoryProps> = ({ moves, visible, onToggle }) => {
  return (
    <View style={styles.container}>
      <TouchableOpacity style={styles.tab} onPress={onToggle} activeOpacity={0.7}>
        <Text style={styles.tabText}>{visible ? '<' : '>'}</Text>
      </TouchableOpacity>
      {visible && (
        <View style={styles.panel}>
          <Text style={styles.title}>MOVES</Text>
          <ScrollView style={styles.list}>
            {moves.length === 0 ? (
              <Text style={styles.empty}>No moves yet</Text>
            ) : (
              moves.map((move, i) => {
                const isP1 = move.player === CellOwner.PLAYER_1;
                return (
                  <View key={i} style={styles.moveRow}>
                    <Text style={styles.moveNum}>{i + 1}.</Text>
                    <View style={[styles.dot, isP1 ? styles.dotP1 : styles.dotP2]} />
                    <Text style={styles.moveWord}>{move.word.toUpperCase()}</Text>
                  </View>
                );
              })
            )}
          </ScrollView>
        </View>
      )}
    </View>
  );
};

const styles = StyleSheet.create({
  container: {
    flexDirection: 'row',
    alignSelf: 'stretch',
  },
  tab: {
    backgroundColor: '#1A1A1A',
    width: 24,
    justifyContent: 'center',
    alignItems: 'center',
    alignSelf: 'stretch',
  },
  tabText: {
    color: '#AAA',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 14,
  },
  panel: {
    width: 180,
    backgroundColor: '#1A1A1A',
    paddingVertical: 8,
    paddingHorizontal: 10,
  },
  title: {
    color: '#888',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 11,
    letterSpacing: 1,
    marginBottom: 8,
  },
  list: {
    flex: 1,
  },
  empty: {
    color: '#555',
    fontFamily: 'monospace',
    fontSize: 12,
  },
  moveRow: {
    flexDirection: 'row',
    alignItems: 'center',
    paddingVertical: 3,
    gap: 6,
  },
  moveNum: {
    color: '#555',
    fontFamily: 'monospace',
    fontSize: 11,
    width: 24,
    textAlign: 'right',
  },
  dot: {
    width: 8,
    height: 8,
    borderRadius: 4,
  },
  dotP1: {
    backgroundColor: '#F5A623',
  },
  dotP2: {
    backgroundColor: '#4ABFF7',
  },
  moveWord: {
    color: '#EEE',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 13,
  },
});
