#ifndef STRING_UTIL_H
#define STRING_UTIL_H

#include <string>

// Return string with spaces roved from end of supplied string.
static inline std::string &rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}

#endif

