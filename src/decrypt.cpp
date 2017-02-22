#include "wordbaseapp-db.h"

int main (void) {
  WordbaseAppDb db2("/Users/ssilver/Google Drive/wordbase.db.encrypted");
  for (auto board : db2.getBoards()) {
    std::cout << board << std::endl;
    for (auto word : board.extractWords()) {
      std::cout << word << std::endl;
    }
  }
  
  return 0;
}
