#include "V3Global.h"
#include "V4VhdlFrontend.h"
#include <iostream>
#include <sstream>
#include <cstdio>

using namespace std;

V4VhdlFrontend::V4VhdlFrontend() {
  cout << "Creating parser" << endl;
}

void V4VhdlFrontend::parseFile(const string& filename) {
  cout << "Parsing " << filename << endl;
}

void V4VhdlFrontend::allocateTemp() {
  tempFilename = string(tmpnam(NULL));
}

string V4VhdlFrontend::getTempName() {
  return tempFilename;
}

void V4VhdlFrontend::parseFiles() {
  const V3StringList& vhdFiles = v3Global.opt.vhdFiles();
  // Skip if there are no VHDL files to analyze
  if (!vhdFiles.size())
    return;

  allocateTemp();
  ostringstream oss;
  oss << "nvc -a ";
  for (V3StringList::const_iterator it = vhdFiles.begin(); it != vhdFiles.end(); ++it) {
    string filename = *it;
    oss << filename << " " << getTempName();
    parseFile(*it);
  }
  oss << " --dump-json ";
  cout << oss.str() << endl;
}