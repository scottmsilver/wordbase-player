#ifndef SIMPLESQL_H
#define SIMPLESQL_H

#include <sqlite3.h>
#include <string>

namespace simplesql {
  class Sqlite3Exception {
    int mReturnCode;
    std::string mSqlite3ErrorMessage;
    std::string mMessage;
    
  public:
    Sqlite3Exception(const char* message, sqlite3* db, int returnCode) :
      mReturnCode(returnCode),
      mSqlite3ErrorMessage(::sqlite3_errmsg(db)),
      mMessage(message) {
    }
    
  };
  
  class Sqlite3Db {
    sqlite3* mDb;
    
  public:
    Sqlite3Db(const char* path) : mDb(NULL) {
      int returnCode = ::sqlite3_open(path, &mDb);
      if (returnCode != SQLITE_OK ) {
        throw Sqlite3Exception("Couldn't open db", mDb, returnCode);
      }
    }
    
    ~Sqlite3Db() {
      if (mDb != NULL) {
        int returnCode = ::sqlite3_close(mDb);
        if (returnCode != SQLITE_OK ) {
          throw Sqlite3Exception("Couldn't close db", mDb, returnCode);
        }
      }
    }
    
    void ExecSql(const char* sql, int (*callback)(void*,int,char**,char**), void* refCon) {
      int returnCode = ::sqlite3_exec(mDb, sql, callback, refCon, NULL);
      
      if (returnCode != SQLITE_OK){
        throw Sqlite3Exception((std::string("Couldn't execute: ") + sql).c_str(), mDb, returnCode);
      }
    }
  };
}

#endif
