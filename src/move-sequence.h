#ifndef MOVE_SEQUENCE_H
#define MOVE_SEQUENCE_H

#include <array>
#include <vector>

// Represents a sequence of moves. 
// The pairs are intended to be (y, x) coordinates.
class MoveSequence : public std::vector<std::pair<int, int>> {
 public:

  // Simple constructor used for unit tests.
  MoveSequence(const std::vector<std::pair<int, int>> & sequence) : std::vector<std::pair<int, int>>(sequence) {}
  
  MoveSequence() {}
};

#endif
