#include <regex>
#include <string>
#include <vector>

struct WordBaseMove : public Move<WordBaseMove> {
  CoordinateList mMove;
  
  WordBaseMove() { }
  WordBaseMove(const CoordinateList &move) : mMove(move) { }
  
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
  
  void read() override {
    cout << "Enter form of (Y, X),?+" << endl;
    
    std::string line;
    std::getline(std::cin, line);
    mMove = parsePath(line);
  }
  
  ostream &to_stream(ostream &os) const override {
    bool first = true;
    for (auto pathElement : mMove) {
      if (!first) {
        os << ",";
      }
      
      os << "(" << pathElement.first << "," << pathElement.second << ")";
      first = false;
    }
    
    return os;
  }
  
  bool operator==(const WordBaseMove &rhs) const override {
    return std::equal(mMove.begin(), mMove.end(), rhs.mMove.begin());
  }
  
  size_t hash() const override {
    return boost::hash_range(mMove.begin(), mMove.end());
  }

  CoordinateList getCoordinateList() const { return mMove; }
};
