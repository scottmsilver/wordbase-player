#ifndef WORDBASEAPP_DB_H
#define WORDBASEAPP_DB_H

// Code that knows how to open up an encrypted Wordbase database.

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <fstream>
#include <iostream>
#include <string>

#include "decrypt.h"
#include "simplesql.h"
#include "string-util.h"


// Persistent representation of a board row in the database.
struct PersistentBoard {
  std::string mId;
  std::string mLanguage;
  std::string mRows;
  std::string mWords;
  
  // Return a vector of the words separated out.
  // mWords is a string such as [XXX, YYY]
  // This splits them and lowercases them.
  std::vector<std::string> extractWords() {
    std::string wordsWithoutBrackets(mWords.begin() + 1, mWords.end() - 1);
    boost::to_lower(wordsWithoutBrackets);
    std::vector<std::string> strs;
    boost::split(strs, wordsWithoutBrackets, boost::is_any_of(", "), boost::token_compress_on);
    return strs;
  }
};

// Print out a PersistentBoard.
std::ostream& operator<<(std::ostream& os, const PersistentBoard& foo) {
  os << foo.mId << "(" << foo.mLanguage << "): " << foo.mRows << " - " << "wordsLength = " << foo.mWords.size();
  return os;
}

// FIX-ME move to real temporary.
const char* kTempUnecryptedDatabaseFilePath = "/tmp/foo.db";

class WordbaseAppDb {
  std::shared_ptr<simplesql::Sqlite3Db> mDb;

  // This is called during row rehydration from ExecSql.
  // This is expected to be called on each row from a query that select all the columns from the table.
  static int getBoardsCallBack(void *boardsRefCon, int colCount, char **colValues, char **colNames){
    PersistentBoard board;
    
    for(int colIndex = 0; colIndex < colCount; colIndex++) {
      const char* colName = colNames[colIndex];
      const char* colValue = colValues[colIndex];
      
      // Map column value to correct field.
      if (std::strcmp(colName, "_id") == 0) {
        board.mId.assign(colValue);
      } else if (std::strcmp(colName, "language") == 0) {
        board.mLanguage.assign(colValue);
      } else if (std::strcmp(colName, "rows") == 0) {
        board.mRows.assign(colValue);
      } else if (std::strcmp(colName, "words") == 0) {
        board.mWords.assign(colValue);
      } else {
        // Hmm, something unexpected. FIX-ME
      }
    }
    
    
    std::vector<PersistentBoard>* boards = static_cast<std::vector<PersistentBoard> *>(boardsRefCon);
    boards->push_back(board);
    
    return 0;
  }
  
public:
  
  WordbaseAppDb(const std::string& encryptedDatabasePath) {
    std::ifstream in(encryptedDatabasePath, std::ifstream::binary);
    auto ciphertext = readStreamIntoString<char>(in);
    std::string key(sha1("abc123def456").c_str(), 16);
    
    // Initialized OpenSsl
    ERR_load_crypto_strings();
    OpenSSL_add_all_algorithms();
    OPENSSL_config(NULL);
    
    // Decrypt the text.
    std::shared_ptr<char> decryptedtext(new char[ciphertext.size()]);
    int decryptedtext_len = decrypt((unsigned char*)ciphertext.c_str(), ciphertext.size(), (unsigned char*) key.c_str(), NULL,
                                    (unsigned char*) decryptedtext.get());
    
    // Clean everything up.
    EVP_cleanup();
    ERR_free_strings();
    
    // Write out the unencrypted database to a file.
    std::ofstream outfile (kTempUnecryptedDatabaseFilePath, std::ofstream::binary);
    outfile.write(decryptedtext.get(), decryptedtext_len);
    outfile.close();
    
    mDb.reset(new simplesql::Sqlite3Db(kTempUnecryptedDatabaseFilePath));
  }
  
  std::vector<PersistentBoard> getBoards() {
    std::vector<PersistentBoard> boards;
    mDb->ExecSql("select _id, language, rows, words from boards", getBoardsCallBack, &boards);
    
    return boards;
  }
};

#endif
