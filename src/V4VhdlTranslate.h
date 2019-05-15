#ifndef __V4_VHDL_TRANSLATE__
#define __V4_VHDL_TRANSLATE__

#include "V3Global.h"
#include "V3Ast.h"
#include <iostream>
#include <string>
#include <fstream>
#include <map>
#include <vector>
#include "rapidjson/include/rapidjson/document.h"
#include "rapidjson/include/rapidjson/istreamwrapper.h"

using namespace std;
using namespace rapidjson;

class V4VhdlTranslate
{
private:
    /* data */
    unsigned long pinnum;
    void iterateArray(Value::ConstArray arr, void(*add)(AstNode *newp));
    AstNode *translateObject(Value::ConstObject item);
    AstNode *translateFcall(Value::ConstObject item);
    map<string, AstNode*> m_entities;
    AstAlways *current_process;
public:
    V4VhdlTranslate(/* args */);
    ~V4VhdlTranslate();
    void translate(string filename);
};


#endif