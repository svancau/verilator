#ifndef _V4VHDLPARSER_H_
#define _V4VHDLPARSER_H_

#include <string>
#include "tiny-process-library/process.hpp"
#include "V3ParseSym.h"

using namespace std;

class V4VhdlFrontend {
public:
  V4VhdlFrontend(V3ParseSym &symtable);
  void parseFile(const string &filename);
  void parseFiles();
  void parseFiles(const V3StringList &fileList);
private:
  string tempFilename;
  void allocateTemp();
  string getTempName();
  V3ParseSym &symt;
};

#endif
