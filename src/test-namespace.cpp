#include <iostream>
#include <boost/sort/spreadsort/spreadsort.hpp>
#include <vector>
#include <boost/dynamic_bitset.hpp>
#include <array>

namespace a {
  namespace b {
    namespace c {
      enum {
	e1 = 3
      };}
  }
}

namespace a {
  namespace b {
    namespace c {

      int foo() {
	return e1;
      }
    }
  }
}
	

int main(int argc, char** arv) {
  std::array<int, 3> a1 = {1, 4, 9};
  std::array<int, 3> a2 = {5, 7, 10};

  int maxValue;
  boost::dynamic_bitset<> bits1(maxValue + 1); // all 0's by default

  for (auto x : a1) {
    bits1[x] = 1;
  }
  
  boost::dynamic_bitset<> bits2(maxValue + 1); // all 0's by default
  for (auto x : a2) {
    bits2[x] = 1;
  }

  bits1 |= bits2;

  for (int i = 0; i < maxValue + 1; i++) {
    if (bits1[i]) {
      std::cout << i << ",";
    }
  }

  std::cout << std::endl;
  
  return 0;
}
