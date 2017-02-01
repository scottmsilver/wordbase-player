#ifndef COORDINATE_LIST_H
#define COORDINATE_LIST_H

#include <array>
#include <vector>

// Represents a move, which is sequence of coordinates.
// The coordinates are pairs in {y, x} (i.e row then column)
class CoordinateList : public std::vector<std::pair<int, int>> {
 public:
  // Construct a sequence from a vector. This is primarily used by unit tests.
  CoordinateList(const std::vector<std::pair<int, int>> & sequence) : std::vector<std::pair<int, int>>(sequence) {}

  // Construct an empty sequence.
  CoordinateList() {}
};

#endif
