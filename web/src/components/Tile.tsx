import React, { useEffect, useRef } from 'react';
import { View, Text, StyleSheet, Platform, Animated } from 'react-native';
import { CellOwner } from '../game/types';

interface TileProps {
  letter: string;
  owner: CellOwner;
  isSelected: boolean;
  isHint: boolean;
  size: number;
  celebrationDelay: number | null;
}

const COLORS: Record<number, string> = {
  [CellOwner.UNOWNED]: '#FAFAFA',
  [CellOwner.PLAYER_1]: '#F5A623',
  [CellOwner.PLAYER_2]: '#4ABFF7',
  [CellOwner.BOMB]: '#1A1A1A',
  [CellOwner.MEGABOMB]: '#1A1A1A',
};

const TEXT_COLORS: Record<number, string> = {
  [CellOwner.UNOWNED]: '#1A1A1A',
  [CellOwner.PLAYER_1]: '#FFFFFF',
  [CellOwner.PLAYER_2]: '#FFFFFF',
  [CellOwner.BOMB]: '#FFFFFF',
  [CellOwner.MEGABOMB]: '#FFFFFF',
};

export const Tile: React.FC<TileProps> = React.memo(({ letter, owner, isSelected, isHint, size, celebrationDelay }) => {
  const prevOwnerRef = useRef(owner);
  const flipAnim = useRef(new Animated.Value(1)).current;
  const celebAnim = useRef(new Animated.Value(1)).current;
  const celebTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    if (prevOwnerRef.current !== owner && Platform.OS === 'web') {
      flipAnim.setValue(0);
      Animated.timing(flipAnim, {
        toValue: 1,
        duration: 400,
        useNativeDriver: false,
      }).start();
    }
    prevOwnerRef.current = owner;
  }, [owner, flipAnim]);

  // Celebration wave: delayed scale pulse
  useEffect(() => {
    if (celebrationDelay === null) {
      celebAnim.stopAnimation();
      celebAnim.setValue(1);
      if (celebTimerRef.current) clearTimeout(celebTimerRef.current);
      return;
    }
    celebTimerRef.current = setTimeout(() => {
      celebAnim.setValue(0);
      Animated.timing(celebAnim, {
        toValue: 1,
        duration: 400,
        useNativeDriver: false,
      }).start();
    }, celebrationDelay);
    return () => {
      if (celebTimerRef.current) clearTimeout(celebTimerRef.current);
      celebAnim.stopAnimation();
    };
  }, [celebrationDelay, celebAnim]);

  let bgColor = COLORS[owner];
  let textColor = TEXT_COLORS[owner];

  if (isSelected) {
    bgColor = '#66BB6A';
    textColor = '#FFFFFF';
  } else if (isHint) {
    bgColor = '#AB47BC';
    textColor = '#FFFFFF';
  }

  const needsBorder = owner === CellOwner.UNOWNED && !isSelected && !isHint;

  // Interpolate: scale Y from 0 → 1 (squish and expand = flip effect)
  const scaleY = flipAnim.interpolate({
    inputRange: [0, 0.5, 1],
    outputRange: [0.05, 0.05, 1],
  });

  // Celebration: pop up then settle back
  const celebScale = celebAnim.interpolate({
    inputRange: [0, 0.4, 1],
    outputRange: [0.6, 1.15, 1],
  });

  const tileStyle = [
    styles.tile,
    {
      width: size,
      height: size,
      backgroundColor: bgColor,
      borderRightWidth: needsBorder ? StyleSheet.hairlineWidth : 0,
      borderBottomWidth: needsBorder ? StyleSheet.hairlineWidth : 0,
      borderColor: 'rgba(0,0,0,0.12)',
    },
  ];

  const content = (
    <>
      <Text
        style={[
          styles.letter,
          {
            color: textColor,
            fontSize: size * 0.5,
            lineHeight: size,
          },
        ]}
        selectable={false}
      >
        {letter}
      </Text>
      {owner === CellOwner.MEGABOMB && !isSelected && (
        <Text
          style={[
            styles.megabombMark,
            { fontSize: size * 0.2, bottom: 1, right: 3 },
          ]}
          selectable={false}
        >
          +
        </Text>
      )}
    </>
  );

  // Wrap in Animated.View for flip effect when ownership changed
  if (Platform.OS === 'web') {
    return (
      <Animated.View style={[...tileStyle, { transform: [{ scaleY }, { scale: celebScale }] }]}>
        {content}
      </Animated.View>
    );
  }

  return (
    <View style={tileStyle}>
      {content}
    </View>
  );
});

const styles = StyleSheet.create({
  tile: {
    justifyContent: 'center',
    alignItems: 'center',
  },
  letter: {
    fontWeight: '700',
    fontFamily: 'monospace',
    textAlign: 'center',
    userSelect: 'none',
  } as any,
  megabombMark: {
    position: 'absolute',
    color: '#FFFFFF',
    fontWeight: '700',
    fontFamily: 'monospace',
  },
});
