#include <string>

struct WordBaseMove : public Move<WordBaseMove> {
  LegalWordId mLegalWordId;

  WordBaseMove() { }
  WordBaseMove(const LegalWordId legalWordId) : mLegalWordId(legalWordId) { }

  void read() {
    // Read a legalWordId from the stdin.
    std::string legalWordIdAsLine;
    std::getline(std::cin,legalWordIdAsLine);
    mLegalWordId = std::stoi(legalWordIdAsLine, nullptr, 0);
  }

  std::ostream &to_stream(std::ostream &os) const {
    os << "lw(" << mLegalWordId << ")";
    return os;
  }

  bool operator==(const WordBaseMove &rhs) const {
    return mLegalWordId == rhs.mLegalWordId;
  }

  size_t hash() const {
    return std::hash<int>()(mLegalWordId);
  }
};
