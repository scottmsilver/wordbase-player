import React from 'react';
import { View, Text, StyleSheet, TouchableOpacity } from 'react-native';

interface HomeScreenProps {
  onStartGame: () => void;
  dictionaryLoaded: boolean;
}

export const HomeScreen: React.FC<HomeScreenProps> = ({ onStartGame, dictionaryLoaded }) => {
  return (
    <View style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>/WORDBASE</Text>
      </View>
      <View style={styles.divider} />
      <View style={styles.content}>
        <TouchableOpacity
          style={[styles.button, !dictionaryLoaded && styles.buttonDisabled]}
          onPress={onStartGame}
          disabled={!dictionaryLoaded}
        >
          <Text style={styles.buttonText}>local game</Text>
        </TouchableOpacity>

        <TouchableOpacity style={[styles.button, styles.buttonDisabled]} disabled>
          <Text style={styles.buttonText}>online game</Text>
        </TouchableOpacity>

        {!dictionaryLoaded && (
          <Text style={styles.loadingText}>loading dictionary...</Text>
        )}

        <View style={styles.spacer} />

        <Text style={styles.howToTitle}>how to play</Text>
        <View style={styles.rulesBox}>
          <Text style={styles.ruleText}>
            Wordbase is a 2-player word game on a 13x10 grid.
          </Text>
          <Text style={styles.ruleText}>
            Orange starts at the top, blue at the bottom.
          </Text>
          <Text style={styles.ruleText}>
            Drag through adjacent letters to spell words. Your word must include at least one of your tiles.
          </Text>
          <Text style={styles.ruleText}>
            All tiles in your word become yours. Reach the opponent's home row to win!
          </Text>
          <Text style={styles.ruleText}>
            Black tiles are bombs - claim them to capture surrounding tiles too.
          </Text>
        </View>
      </View>
    </View>
  );
};

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#E8E8E8',
    alignItems: 'center',
  },
  header: {
    width: '100%',
    maxWidth: 500,
    paddingHorizontal: 12,
    paddingVertical: 10,
    flexDirection: 'row',
    alignItems: 'center',
  },
  title: {
    fontSize: 18,
    fontWeight: '700',
    fontFamily: 'monospace',
    color: '#222',
  },
  divider: {
    width: '100%',
    maxWidth: 500,
    height: 2,
    backgroundColor: '#222',
  },
  content: {
    width: '100%',
    maxWidth: 500,
    padding: 16,
  },
  button: {
    backgroundColor: '#222',
    paddingHorizontal: 16,
    paddingVertical: 10,
    alignSelf: 'flex-start',
    marginBottom: 8,
  },
  buttonDisabled: {
    backgroundColor: '#888',
  },
  buttonText: {
    color: '#FFF',
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 16,
  },
  loadingText: {
    fontFamily: 'monospace',
    fontSize: 13,
    color: '#666',
    marginTop: 4,
  },
  spacer: {
    height: 40,
  },
  howToTitle: {
    fontFamily: 'monospace',
    fontWeight: '700',
    fontSize: 16,
    color: '#222',
    backgroundColor: '#FFF',
    paddingHorizontal: 10,
    paddingVertical: 6,
    alignSelf: 'center',
    marginBottom: 16,
    borderWidth: 2,
    borderColor: '#222',
  },
  rulesBox: {
    backgroundColor: '#FFF',
    padding: 16,
    gap: 10,
  },
  ruleText: {
    fontFamily: 'monospace',
    fontSize: 13,
    color: '#333',
    lineHeight: 20,
  },
});
