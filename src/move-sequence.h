#ifndef MOVE_SEQUENCE_H
#define MOVE_SEQUENCE_H

#include <array>
#include <vector>

//typedef std::vector<std::pair<int, int>> MoveSequence;

class MoveSequence : public std::vector<std::pair<int, int>> {
 public:
  
 MoveSequence(const std::vector<std::pair<int, int>> & sequence) : std::vector<std::pair<int, int>>(sequence) {}
  
  MoveSequence() {}
};

#endif
