import React, { useState, useEffect } from 'react';
import { View, StyleSheet } from 'react-native';
import { StatusBar } from 'expo-status-bar';
import { HomeScreen } from './src/screens/HomeScreen';
import { GameScreen } from './src/screens/GameScreen';
import { dictionary } from './src/game/Dictionary';

type Screen = 'home' | 'game';

export default function App() {
  const [screen, setScreen] = useState<Screen>('home');
  const [dictionaryLoaded, setDictionaryLoaded] = useState(false);

  useEffect(() => {
    dictionary.load().then(() => {
      setDictionaryLoaded(true);
    });
  }, []);

  return (
    <View style={styles.container}>
      <StatusBar style="dark" />
      {screen === 'home' ? (
        <HomeScreen
          onStartGame={() => setScreen('game')}
          dictionaryLoaded={dictionaryLoaded}
        />
      ) : (
        <GameScreen onBack={() => setScreen('home')} />
      )}
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#E8E8E8',
  },
});
