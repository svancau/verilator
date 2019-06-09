#ifndef __V4_VHDL_TRANSLATE__
#define __V4_VHDL_TRANSLATE__

#include "V3Global.h"
#include "V3Ast.h"
#include "V3ParseSym.h"
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
    AstNodeDType* createArray(AstNodeDType* basep, AstNodeRange* rangep, bool isPacked);
    AstVar*  createVariable(FileLine* fileline, string name, AstNodeRange* arrayp, AstNode* attrsp);
    AstNode* createSupplyExpr(FileLine* fileline, string name, int value);
    AstNodeDType* m_varDTypep;	// Pointer to data type for next signal declaration
    AstVarType	m_varDecl;	// Type for next signal declaration (reg/wire/etc)
    VDirection  m_varIO;        // Direction for next signal declaration (reg/wire/etc)
    AstVar*	m_varAttrp;	// Current variable for attribute adding
    string currentFilename; // current filename being translated
    bool allTracingOn(FileLine* fl) {
	return v3Global.opt.trace() && fl->tracingOn();
    }

    unsigned long pinnum;
    void iterateArray(Value::ConstArray arr, void(*add)(AstNode *newp));
    AstNodeDType *translateType(Value::ConstObject item);
    AstNode *translateObject(Value::ConstObject item);
    AstNode *translateFcall(Value::ConstObject item);
    map<string, AstNode*> m_entities;
    map<string, AstEdgeType> m_sig_edges;
    AstAlways *current_process;
    V3ParseSym &symt;
public:
    V4VhdlTranslate(V3ParseSym &symtable);
    ~V4VhdlTranslate();
    void translate(string filename);
};

#endif