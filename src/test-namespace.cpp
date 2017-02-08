#include <iostream>
#include <boost/sort/spreadsort/spreadsort.hpp>
#include <vector>

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
  std::cout << a::b::c::foo();

  std::vector<int> moves;
  moves.push_back(9);
  moves.push_back(3);
  
  boost::sort::spreadsort::integer_sort(moves.begin(), moves.end());

  for (auto x : moves) {
    std::cout << x;
  }
  return 0;
}
