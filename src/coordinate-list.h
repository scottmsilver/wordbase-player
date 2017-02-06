#ifndef COORDINATE_LIST_H
#define COORDINATE_LIST_H

#include <array>
#include <vector>
#include <regex>
#include <boost/functional/hash/hash.hpp>

// Represents a move, which is sequence of coordinates.
// The coordinates are pairs in {y, x} (i.e row then column)
class CoordinateList : public std::vector<std::pair<int, int>> {
 public:
  // Construct a sequence from a vector. This is primarily used by unit tests.
  CoordinateList(const std::vector<std::pair<int, int>> & sequence) : std::vector<std::pair<int, int>>(sequence) {}

  // Construct an empty sequence.
  CoordinateList() {}

  // parsePath
  //
  // Returns a vector of int, int pairs from a string in the format
  // (2, 3), (3, 4) ...
  static CoordinateList parsePath(const std::string& s) {
    CoordinateList path;
    
    std::regex words_regex("(\\(([0-9]+)\\s*,\\s*([0-9]+)\\)),?\\s*");
    
    for (std::sregex_iterator i = std::sregex_iterator(s.begin(), s.end(), words_regex);
         i != std::sregex_iterator(); ++i) {
      std::smatch match = *i;
      
      int y = std::stoi(match[2], nullptr, 0);
      int x = std::stoi(match[3], nullptr, 0);
      path.push_back(std::pair<int, int>(y, x));
    }
    
    return path;
  }

};

namespace std {
  template <> struct hash<CoordinateList>
  {
    size_t operator()(const CoordinateList& x) const
    {
      return boost::hash_range(x.begin(), x.end());
    }
  };
}
#endif
