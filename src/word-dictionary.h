#ifndef WORD_DICTIONARY_H
#define WORD_DICTIONARY_H

#include <iostream>
#include <istream>
#include <string>
#include <unordered_set>

#include "string-util.h"

class WordDictionary {
 private:
  std::unordered_set<std::string> mWords;
  std::unordered_set<std::string> mPrefixes;

 public:
  WordDictionary(std::istream& dictionaryStream) {
    std::string line;
    while (getline(dictionaryStream, line)) {
      std::string word(rtrim(line));
      mWords.insert(word);
      // Keep track of all possible prefixes of this word, starting at word length 2.
      for (int wordLength = 1; wordLength <= word.length(); wordLength++) {
	mPrefixes.insert(word.substr(0, wordLength));
      }
    }
  }

  bool hasWord(const std::string& word) { return mWords.find(word) != mWords.end(); }
  bool hasPrefix(const std::string& prefix) { return mPrefixes.find(prefix) != mPrefixes.end(); }
};

#endif
