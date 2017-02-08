#include <string>

struct WordBaseMove : public Move<WordBaseMove> {
  //CoordinateList mMove;
  LegalWordId mLegalWordId;
  
  WordBaseMove() { }
  // WordBaseMove(const CoordinateList &move) : mMove(move) { }
 WordBaseMove(const LegalWordId legalWordId) : mLegalWordId(legalWordId) { }
  
  void read() override {
    assert(false);
        std::cout << "Enter form of (Y, X),?+" << std::endl;
    
    std::string line;
    std::getline(std::cin, line);
    //mMove = CoordinateList::parsePath(line);
  }
  
  std::ostream &to_stream(std::ostream &os) const override {
    os << "lw(" << mLegalWordId << ")";
    /*
    bool first = true;
    for (auto pathElement : mMove) {
      if (!first) {
        os << ",";
      }
      
      os << "(" << pathElement.first << "," << pathElement.second << ")";
      first = false;
    }
    */
    return os;
    }
  
  bool operator==(const WordBaseMove &rhs) const override {
    return mLegalWordId == rhs.mLegalWordId;
    //return std::equal(mMove.begin(), mMove.end(), rhs.mMove.begin());
  }
  
  size_t hash() const override {
    return mLegalWordId; // FIX-ME(ssilver) bad hash
    //    return boost::hash_range(mMove.begin(), mMove.end());
  }
};
