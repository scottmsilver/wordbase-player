#include <regex>
#include <string>
#include <vector>

struct WordBaseMove : public Move<WordBaseMove> {
  CoordinateList mMove;
  
  WordBaseMove() { }
  WordBaseMove(const CoordinateList &move) : mMove(move) { }
  
  void read() override {
    cout << "Enter form of (Y, X),?+" << endl;
    
    std::string line;
    std::getline(std::cin, line);
    mMove = CoordinateList::parsePath(line);
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
