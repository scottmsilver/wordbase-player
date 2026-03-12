import React, { useEffect, useRef, useState } from 'react';
import { View, Animated, StyleSheet } from 'react-native';

const PARTICLE_COUNT = 60;
const DURATION = 3000;
const COLORS = ['#F5A623', '#4ABFF7', '#66BB6A', '#AB47BC', '#FF5252', '#FFD740'];

interface Particle {
  x: number;
  peakY: number;
  endY: number;
  color: string;
  width: number;
  height: number;
  rotation: number;
  delay: number;
}

function createParticles(): Particle[] {
  return Array.from({ length: PARTICLE_COUNT }, () => {
    const angle = Math.random() * Math.PI * 2;
    const speed = 200 + Math.random() * 400;
    return {
      x: Math.cos(angle) * speed,
      peakY: -(150 + Math.random() * 250),
      endY: 300 + Math.random() * 500,
      color: COLORS[Math.floor(Math.random() * COLORS.length)],
      width: 6 + Math.random() * 8,
      height: 4 + Math.random() * 6,
      rotation: (Math.random() - 0.5) * 1080,
      delay: Math.random() * 0.12,
    };
  });
}

interface ConfettiProps {
  active: boolean;
}

export const Confetti: React.FC<ConfettiProps> = ({ active }) => {
  const anim = useRef(new Animated.Value(0)).current;
  const [particles, setParticles] = useState<Particle[]>([]);
  const [visible, setVisible] = useState(false);

  useEffect(() => {
    if (!active) {
      setVisible(false);
      return;
    }
    const newParticles = createParticles();
    setParticles(newParticles);
    setVisible(true);
    anim.setValue(0);
    Animated.timing(anim, {
      toValue: 1,
      duration: DURATION,
      useNativeDriver: false,
    }).start(() => {
      setVisible(false);
    });
  }, [active, anim]);

  if (!visible || particles.length === 0) return null;

  return (
    <View style={StyleSheet.absoluteFill} pointerEvents="none">
      {particles.map((p, i) => {
        const d = p.delay;
        const translateX = anim.interpolate({
          inputRange: [0, d, d + 0.3, 1],
          outputRange: [0, 0, p.x * 0.7, p.x],
          extrapolate: 'clamp',
        });
        const translateY = anim.interpolate({
          inputRange: [0, d, d + 0.15, d + 0.4, Math.min(d + 0.9, 0.99), 1],
          outputRange: [0, 0, p.peakY, p.peakY * 0.2, p.endY, p.endY],
          extrapolate: 'clamp',
        });
        const opacity = anim.interpolate({
          inputRange: [0, d, d + 0.05, 0.75, 1],
          outputRange: [0, 0, 1, 1, 0],
          extrapolate: 'clamp',
        });
        const rotate = anim.interpolate({
          inputRange: [0, 1],
          outputRange: ['0deg', `${p.rotation}deg`],
        });

        return (
          <Animated.View
            key={i}
            style={{
              position: 'absolute',
              left: '50%',
              top: '40%',
              width: p.width,
              height: p.height,
              backgroundColor: p.color,
              borderRadius: 2,
              transform: [{ translateX }, { translateY }, { rotate }],
              opacity,
            }}
          />
        );
      })}
    </View>
  );
};
