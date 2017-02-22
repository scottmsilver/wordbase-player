#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <sstream>
#include <string>

// Return string with spaces roved from end of supplied string.
static inline std::string &rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}

// Return a string containing the contents of the passed in stream.
template <typename CharT, typename Traits = std::char_traits<CharT>,
typename Allocator = std::allocator<CharT> >
std::basic_string<CharT, Traits, Allocator> readStreamIntoString(std::basic_istream<CharT, Traits>& in, Allocator alloc = {}) {
  std::basic_ostringstream<CharT, Traits, Allocator> ss(std::basic_string<CharT, Traits, Allocator>(std::move(alloc)));
  if (!(ss << in.rdbuf()))
    throw std::ios_base::failure{ "error" };
  
  return ss.str();
}
  
#endif

