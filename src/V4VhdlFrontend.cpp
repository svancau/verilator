#include "V3Global.h"
#include "V4VhdlFrontend.h"
#include "V4VhdlTranslate.h"
#include <iostream>
#include <sstream>
#include <cstdio>

using namespace std;
using namespace TinyProcessLib;

V4VhdlFrontend::V4VhdlFrontend() {}

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
    oss << filename << " ";
  }
  oss << "--dump-json " << getTempName();
  string command = oss.str();
  const char *cmd = command.c_str();

  //cout << string(cmd) << endl;
  Process nvc_process(cmd, "", [](const char *bytes, size_t n) {
    cout << string(bytes, n);
  }, [](const char *bytes, size_t n) {
    cout << string(bytes, n);
  });
  cout << endl;

  // Check for missing sim or parse error
  if (nvc_process.get_exit_status() == 127) {
    v3error("nvc VHDL simulator is not properly installed or not in your $PATH");
  }
  else if (nvc_process.get_exit_status() != 0) {
    v3error("nvc failed to parse one of your input files");
  }

  V4VhdlTranslate xlate;
  xlate.translate(getTempName());

  //unlink(getTempName().c_str());
}