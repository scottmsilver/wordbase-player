#ifndef WORD_DICTIONARY_H
#define WORD_DICTIONARY_H

#include <iostream>
#include <istream>
#include <string>
#include <unordered_set>

#include "string-util.h"

// Simple dictionary loaded from a stream that can answer
// hasWord() and hasPrefix().
// Implemented via sets.
class WordDictionary {
 private:
  std::unordered_set<std::string> mWords;
  std::unordered_set<std::string> mPrefixes;

 public:
  // Construct a new WordDictionary from a stream.
  // Stream assumed to have one word in the dictionary per line.
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

  // Return true if word is in the dictionary.
  bool hasWord(const std::string& word) const { return mWords.find(word) != mWords.end(); }

  // Return true if prefix is a prefix of a word in the dictionary.
  // NB: prefix only returns true for words of length >= 2.
  bool hasPrefix(const std::string& prefix) const { return mPrefixes.find(prefix) != mPrefixes.end(); }
};

#endif
