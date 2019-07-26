#include "V3Global.h"
#include "V4VhdlFrontend.h"
#include "V4VhdlTranslate.h"
#include <iostream>
#include <sstream>
#include <cstdio>

using namespace std;
using namespace TinyProcessLib;

V4VhdlFrontend::V4VhdlFrontend(V3ParseSym &symtable) : symt(symtable) {

}

void V4VhdlFrontend::allocateTemp() {
  tempFilename = string(tmpnam(NULL));
}

string V4VhdlFrontend::getTempName() {
  return tempFilename;
}

void V4VhdlFrontend::parseFile(const string &filename) {
  V3StringList sl;
  sl.push_back(filename);
  parseFiles(sl);
}

void V4VhdlFrontend::parseFiles() {
  parseFiles(v3Global.opt.vhdFiles());
}

void V4VhdlFrontend::parseFiles(const V3StringList &fileList) {
  // Skip if there are no VHDL files to analyze
  if (!fileList.size())
    return;

  allocateTemp();
  ostringstream oss;
  oss << "nvc -a ";
  for (V3StringList::const_iterator it = fileList.begin(); it != fileList.end(); ++it) {
    string filename = *it;
    oss << filename << " ";
  }
  oss << "--dump-json " << getTempName();
  string command = oss.str();
  const char *cmd = command.c_str();

  Process nvc_process(cmd, "", [](const char *bytes, size_t n) {
    cout << string(bytes, n);
  }, [](const char *bytes, size_t n) {
    cout << string(bytes, n);
    if (bytes[n-1] != '\n')
      cout << endl;
  });

  // Check for missing sim or parse error
  if (nvc_process.get_exit_status() == 127) {
    v3fatal("nvc VHDL simulator is not properly installed or not in your $PATH");
  }
  else if (nvc_process.get_exit_status() != 0) {
    v3fatal("nvc failed to parse one of your input files");
  }

  V4VhdlTranslate xlate(symt);
  xlate.translate(getTempName());

  //if (unlink(getTempName().c_str()) == -1)
  //  v3error("Failed to remove file " << getTempName() << "error " << errno);

}