#ifndef _V4VHDLPARSER_H_
#define _V4VHDLPARSER_H_

#include <string>
#include "tiny-process-library/process.hpp"

using namespace std;

class V4VhdlFrontend {
public:
  V4VhdlFrontend();
  void parseFiles();
private:
  string tempFilename;
  void parseFile(const string& filename);
  void allocateTemp();
  string getTempName();
};

#endif
