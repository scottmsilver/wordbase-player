import React from 'react';
import { View, Text, StyleSheet } from 'react-native';
import { CellOwner } from '../game/types';

interface TileProps {
  letter: string;
  owner: CellOwner;
  isSelected: boolean;
  size: number;
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

export const Tile: React.FC<TileProps> = React.memo(({ letter, owner, isSelected, size }) => {
  let bgColor = COLORS[owner];
  let textColor = TEXT_COLORS[owner];

  if (isSelected) {
    // Bright green highlight that stands out on any background
    bgColor = '#66BB6A';
    textColor = '#FFFFFF';
  }

  const needsBorder = owner === CellOwner.UNOWNED && !isSelected;

  return (
    <View
      style={[
        styles.tile,
        {
          width: size,
          height: size,
          backgroundColor: bgColor,
          borderRightWidth: needsBorder ? StyleSheet.hairlineWidth : 0,
          borderBottomWidth: needsBorder ? StyleSheet.hairlineWidth : 0,
          borderColor: 'rgba(0,0,0,0.12)',
        },
      ]}
    >
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
