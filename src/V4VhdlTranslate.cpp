#include "V3Global.h"
#include "V3FileLine.h"
#include "V4VhdlTranslate.h"

#define VARRESET_LIST(decl)    { pinnum=1; VARRESET(); VARDECL(decl); }	// Start of pinlist
#define VARRESET_NONLIST(decl) { pinnum=0; VARRESET(); VARDECL(decl); }	// Not in a pinlist
#define VARRESET() { VARDECL(UNKNOWN); VARIO(NONE); VARDTYPE(NULL); }
#define VARDECL(type) { m_varDecl = AstVarType::type; }
#define VARIO(type) { m_varIO = VDirection::type; }
#define VARDTYPE(dtypep) { m_varDTypep = dtypep; }

#define VARDONEA(fl,name,array,attrs) GRAMMARP->createVariable((fl),(name),(array),(attrs))
#define VARDONEP(portp,array,attrs) GRAMMARP->createVariable((portp)->fileline(),(portp)->name(),(array),(attrs))
#define PINNUMINC() (pinnum++)

V4VhdlTranslate::V4VhdlTranslate(V3ParseSym &symtable) : symt(symtable)
{
    currentFilename = "";
}

V4VhdlTranslate::~V4VhdlTranslate()
{
}

long V4VhdlTranslate::getLine(Value::ConstObject obj) {
    return obj["ln"].GetInt();
}

AstNodeDType* V4VhdlTranslate::createArray(AstNodeDType* basep, AstNodeRange* nrangep, bool isPacked) {
    // Split RANGE0-RANGE1-RANGE2 into ARRAYDTYPE0(ARRAYDTYPE1(ARRAYDTYPE2(BASICTYPE3),RANGE),RANGE)
    AstNodeDType* arrayp = basep;
    if (nrangep) { // Maybe no range - return unmodified base type
        while (nrangep->nextp()) nrangep = VN_CAST(nrangep->nextp(), NodeRange);
        while (nrangep) {
            AstNodeRange* prevp = VN_CAST(nrangep->backp(), NodeRange);
            if (prevp) nrangep->unlinkFrBack();
            AstRange* rangep = VN_CAST(nrangep, Range);
            if (!rangep) {
                if (!VN_IS(nrangep, UnsizedRange)) nrangep->v3fatalSrc("Expected range or unsized range");
                arrayp = new AstUnsizedArrayDType(nrangep->fileline(), VFlagChildDType(), arrayp);
            } else if (isPacked) {
                arrayp = new AstPackArrayDType(rangep->fileline(), VFlagChildDType(), arrayp, rangep);
            } else {
                arrayp = new AstUnpackArrayDType(rangep->fileline(), VFlagChildDType(), arrayp, rangep);
            }
            nrangep = prevp;
        }
    }
    return arrayp;
}

AstVar* V4VhdlTranslate::createVariable(FileLine* fileline, string name, AstNodeRange* arrayp, AstNode* attrsp) {
    AstNodeDType* dtypep = m_varDTypep;
    //UINFO(5,"  creVar "<<name<<"  decl="<<GRAMMARP->m_varDecl<<"  io="<<GRAMMARP->m_varIO<<"  dt="<<(dtypep?"set":"")<<endl);
    if (m_varIO == VDirection::NONE
	&& m_varDecl == AstVarType::PORT) {
	// Just a port list with variable name (not v2k format); AstPort already created
	if (dtypep) fileline->v3error("Unsupported: Ranges ignored in port-lists");
	return NULL;
    }
    if (m_varDecl == AstVarType::WREAL) {
	// dtypep might not be null, might be implicit LOGIC before we knew better
	dtypep = new AstBasicDType(fileline,AstBasicDTypeKwd::DOUBLE);
    }
    if (!dtypep) {  // Created implicitly
	dtypep = new AstBasicDType(fileline, AstBasicDTypeKwd::LOGIC_IMPLICIT);
    } else {  // May make new variables with same type, so clone
	dtypep = dtypep->cloneTree(false);
    }
    //UINFO(0,"CREVAR "<<fileline->ascii()<<" decl="<<GRAMMARP->m_varDecl.ascii()<<" io="<<GRAMMARP->m_varIO.ascii()<<endl);
    AstVarType type = m_varDecl;
    if (type == AstVarType::UNKNOWN) {
        if (m_varIO.isAny()) {
            type = AstVarType::PORT;
        } else {
            fileline->v3fatalSrc("Unknown signal type declared");
        }
    }
    if (type == AstVarType::GENVAR) {
	if (arrayp) fileline->v3error("Genvars may not be arrayed: "<<name);
    }

    // Split RANGE0-RANGE1-RANGE2 into ARRAYDTYPE0(ARRAYDTYPE1(ARRAYDTYPE2(BASICTYPE3),RANGE),RANGE)
    AstNodeDType* arrayDTypep = createArray(dtypep, arrayp, false);

    AstVar* nodep = new AstVar(fileline, type, name, VFlagChildDType(), arrayDTypep);
    nodep->addAttrsp(attrsp);
    if (m_varDecl != AstVarType::UNKNOWN) nodep->combineType(m_varDecl);
    if (m_varIO != VDirection::NONE) {
        nodep->declDirection(m_varIO);
        nodep->direction(m_varIO);
    }

    if (m_varDecl == AstVarType::SUPPLY0) {
	nodep->addNext(createSupplyExpr(fileline, nodep->name(), 0));
    }
    if (m_varDecl == AstVarType::SUPPLY1) {
	nodep->addNext(createSupplyExpr(fileline, nodep->name(), 1));
    }
    if (VN_IS(dtypep, ParseTypeDType)) {
	// Parser needs to know what is a type
	AstNode* newp = new AstTypedefFwd(fileline, name);
	nodep->addNext(newp);
	symt.reinsert(newp);
    }
    // Don't set dtypep in the ranging;
    // We need to autosize parameters and integers separately
    //
    // Propagate from current module tracing state
    if (nodep->isGenVar()) nodep->trace(false);
    else if (nodep->isParam() && !v3Global.opt.traceParams()) nodep->trace(false);
    else nodep->trace(allTracingOn(nodep->fileline()));

    // Remember the last variable created, so we can attach attributes to it in later parsing
    //m_varAttrp = nodep;
    //tagNodep(m_varAttrp);
    return nodep;
}

AstNode* V4VhdlTranslate::createSupplyExpr(FileLine* fileline, string name, int value) {
    return new AstAssignW(fileline, new AstVarRef(fileline, name, true),
                          new AstConst(fileline, AstConst::StringToParse(),
				       (value ? "'1" : "'0")));
}

#define RET_NODE(node) return returnDebug(node);
#define RET_NODE_NODBG(node) return node;

void dumpTranslate(AstNode* nodep) {
    if (nodep) {
        UINFO(9, "\tAS: " << nodep << endl);
    }
    else {
        UINFO(9, "\tAS: NULL no translation performed" << endl);
    }
}

AstNode* returnDebug(AstNode* nodep) {
    dumpTranslate(nodep);
    return nodep;
}

AstNodeDType* returnDebug(AstNodeDType* nodep) {
    dumpTranslate(nodep);
    return nodep;
}

AstNodeDType *V4VhdlTranslate::translateType(FileLine *fl, Value::ConstObject item) {
    string type_name = item["name"].GetString();
    UINFO(9, "[" << fl->filebasename() << ":" << fl->lineno()
        << "] Type " << type_name << endl);

    if (type_name == "STD_LOGIC" or type_name == "STD_ULOGIC") {
        RET_NODE(new AstBasicDType(fl, AstBasicDTypeKwd::LOGIC_IMPLICIT));
    } else if (type_name == "STD_LOGIC_VECTOR" or type_name == "STD_ULOGIC_VECTOR"
        or type_name == "UNSIGNED" or type_name == "SIGNED") {
        AstNodeDType *base_type = new AstBasicDType(fl, AstBasicDTypeKwd::LOGIC_IMPLICIT);
        if (type_name == "SIGNED") {
            ((AstBasicDType*)base_type)->setSignedState(signedst_SIGNED);
        }
        else if (type_name == "UNSIGNED") {
            ((AstBasicDType*)base_type)->setSignedState(signedst_UNSIGNED);
        }
        else {
            ((AstBasicDType*)base_type)->setSignedState(signedst_NOSIGN);
        }

        AstNodeRange *range = NULL;
        if (item.HasMember("range")) {
            Value::ConstObject range_o = item["range"].GetArray()[0].GetObject();
            range = new AstRange(fl, translateObject(range_o["l"].GetObject()), translateObject(range_o["r"].GetObject()));
        } else {
            range = new AstUnsizedRange(fl);
        }
        RET_NODE(createArray(base_type, range, true));
    } else if (type_name == "INTEGER" or type_name == "NATURAL" or type_name == "POSITIVE") {
        RET_NODE(new AstBasicDType(fl, AstBasicDTypeKwd::INT));
    } else if (type_name == "STRING") {
        RET_NODE(new AstBasicDType(fl, AstBasicDTypeKwd::STRING));
    } else if (type_name == "REAL") {
        RET_NODE(new AstBasicDType(fl, AstBasicDTypeKwd::DOUBLE));
    } else if (type_name == "BOOLEAN") {
        RET_NODE(new AstBasicDType(fl, AstBasicDTypeKwd::LOGIC_IMPLICIT));
    } else if (type_name == "BIT") {
        RET_NODE(new AstBasicDType(fl, AstBasicDTypeKwd::BIT));
    } else if (type_name == "BIT_VECTOR") {
        AstNodeDType *base_type = new AstBasicDType(fl, AstBasicDTypeKwd::BIT);
        AstNodeRange *range = NULL;
        if (item.HasMember("range")) {
            Value::ConstObject range_o = item["range"].GetArray()[0].GetObject();
            range = new AstRange(fl, translateObject(range_o["l"].GetObject()), translateObject(range_o["r"].GetObject()));
        } else {
            range = new AstUnsizedRange(fl);
        }
        RET_NODE(createArray(base_type, range, true));
    } else {
        RET_NODE(new AstRefDType(fl, type_name));
    }
}

AstNode *V4VhdlTranslate::translateFcall(Value::ConstObject item) {
    string fname = item["name"].GetString();
    UINFO(9, "[" << currentFilename << ":" << item["ln"].GetInt()
        << "] FCall " << fname << endl);
    updateFL(item);
    vector<AstNode*> params;
    Value::ConstArray stmts = item["params"].GetArray();
    for (Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
        AstNode *res = translateObject(m->GetObject()["value"].GetObject());
        params.push_back(res);
    }

    if (fname == "IEEE.STD_LOGIC_1164.\"and\"" or fname == "\"and\"") {
        RET_NODE_NODBG(new AstAnd(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.\"or\"" or fname == "\"or\"") {
        RET_NODE_NODBG(new AstOr(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.\"not\"" or fname == "\"not\"") {
        RET_NODE_NODBG(new AstNot(fl(), params[0]));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.\"nand\"" or fname == "\"nand\"") {
        RET_NODE_NODBG(new AstNot(fl(), new AstAnd(fl(), params[0], params[1])));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.\"nor\"" or fname == "\"nor\"") {
        RET_NODE_NODBG(new AstNot(fl(), new AstOr(fl(), params[0], params[1])));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.\"xor\"" or fname == "\"xor\"") {
        RET_NODE_NODBG(new AstXor(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.\"xnor\"" or fname == "\"xnor\"") {
        RET_NODE_NODBG(new AstNot(fl(), new AstXor(fl(), params[0], params[1])));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"+\"" or fname == "\"+\"") {
        RET_NODE_NODBG(new AstAdd(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"-\"" or fname == "\"-\"") {
        RET_NODE_NODBG(new AstSub(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"*\"" or fname == "\"*\"") {
        RET_NODE_NODBG(new AstMul(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"**\"" or fname == "\"**\"") {
        RET_NODE_NODBG(new AstPow(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"/\"" or fname == "\"/\"") {
        RET_NODE_NODBG(new AstDiv(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"<\"" or fname == "\"<\"") {
        RET_NODE_NODBG(new AstLt(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"<=\"" or fname == "\"<=\"") {
        RET_NODE_NODBG(new AstLte(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\">\"" or fname == "\">\"") {
        RET_NODE_NODBG(new AstGt(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\">=\"" or fname == "\">=\"") {
        RET_NODE_NODBG(new AstGte(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"=\"" or fname == "\"=\"") {
        RET_NODE_NODBG(new AstEq(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.\"/=\"" or fname == "\"/=\"") {
        RET_NODE_NODBG(new AstNeq(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.NUMERIC_STD.TO_INTEGER") {
        RET_NODE_NODBG(new AstCast(fl(), params[0], new AstBasicDType(fl(), AstBasicDTypeKwd::INT)));
    }
    else if (fname == "IEEE.NUMERIC_STD.TO_UNSIGNED") {
        AstNodeDType *base_type = new AstBasicDType(fl(), AstBasicDTypeKwd::LOGIC_IMPLICIT);
        ((AstBasicDType*)base_type)->setSignedState(signedst_UNSIGNED);
        AstRange *range = new AstRange(fl(), new AstSub(fl(), params[1], new AstConst(fl(), 1)),
            new AstConst(fl(), 0));
        AstNodeDType *arr = createArray(base_type, range, true);
        RET_NODE_NODBG(new AstCast(fl(), params[0], arr));
    }
    else if (fname == "IEEE.NUMERIC_STD.TO_SIGNED") {
        AstNodeDType *base_type = new AstBasicDType(fl(), AstBasicDTypeKwd::LOGIC_IMPLICIT);
        ((AstBasicDType*)base_type)->setSignedState(signedst_SIGNED);
        AstRange *range = new AstRange(fl(), new AstSub(fl(), params[1], new AstConst(fl(), 1)),
            new AstConst(fl(), 0));
        AstNodeDType *arr = createArray(base_type, range, true);
        RET_NODE_NODBG(new AstCast(fl(), params[0], arr));
    }
    else if (fname == "IEEE.NUMERIC_STD.RESIZE") {
        RET_NODE_NODBG(new AstVHDResize(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.MATH_REAL.CEIL") {
        RET_NODE_NODBG(new AstCeilD(fl(), params[0]));
    }
    else if (fname == "IEEE.MATH_REAL.FLOOR") {
        RET_NODE_NODBG(new AstFloorD(fl(), params[0]));
    }
    else if (fname == "IEEE.MATH_REAL.LOG2") {
        RET_NODE_NODBG(new AstLog2D(fl(), params[0]));
    }
    else if (fname == "IEEE.MATH_REAL.MOD" or fname == "MOD") {
        RET_NODE_NODBG(new AstModDiv(fl(), params[0], params[1]));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.RISING_EDGE") {
        m_sig_edges.insert(pair<string, AstEdgeType>(((AstVarRef*)params[0])->name(), AstEdgeType::ET_POSEDGE));
        // Return True, if removed by constify pass
        RET_NODE_NODBG(new AstConst(fl(), AstConst::LogicTrue()));
    }
    else if (fname == "IEEE.STD_LOGIC_1164.FALLING_EDGE") {
        m_sig_edges.insert(pair<string, AstEdgeType>(((AstVarRef*)params[0])->name(), AstEdgeType::ET_NEGEDGE));
        // Return True, if removed by constify pass
        RET_NODE_NODBG(new AstConst(fl(), AstConst::LogicTrue()));
    }
    else {
        AstNode *functask = symt.findEntUpward(fname);
        if (not functask) {
            v3fatal("No reference to task or function " + fname + " in file "+ currentFilename
                + ":" << item["ln"].GetInt());
            RET_NODE_NODBG((AstNode*)nullptr);
        } else if (VN_CAST(functask, Func)) {
            AstFuncRef *ref = new AstFuncRef(fl(), fname, NULL);
            for(auto item : params){
                ref->addPinsp(new AstArg(fl(), "", item));
            }
            RET_NODE_NODBG(ref);
        } else if (VN_CAST(functask, Task)) {
            AstTaskRef *ref = new AstTaskRef(fl(), fname, NULL);
            for(auto item : params){
                ref->addPinsp(new AstArg(fl(), "", item));
            }
            RET_NODE_NODBG(ref);
        } else {
            v3fatal(fname + " is not a task or function in file "+ currentFilename + ":"
                << item["ln"].GetInt());
            RET_NODE_NODBG((AstNode*)nullptr);
        }
    }
    RET_NODE_NODBG((AstNode*)nullptr); // Return null if not oter matched
}

string convertName(string inName) {
    int startIndex = 0;
    for (startIndex = inName.length()-1; startIndex > 0; --startIndex) {
        if (inName[startIndex] == '.')
            break;
    }

    string outName;
    if (startIndex != 0)
        outName = inName.erase(0, startIndex+1);
    else
        outName = inName;

    return outName;
}

void V4VhdlTranslate::updateFL(Value::ConstObject item) {
    m_fl->lineno(getLine(item));
}

FileLine *V4VhdlTranslate::fl() {
    return m_fl->copyOrSameFileLine();
}

AstNode *V4VhdlTranslate::translateObject(Value::ConstObject item) {
    Value::ConstObject obj = item;
    string object_name = "";

    if(obj.HasMember("name") and obj["name"].IsString())
        object_name = obj["name"].GetString();

    UINFO(9, "[" << currentFilename << ":" << getLine(obj)
        << "] Object " << obj["cls"].GetString() << " " << object_name << endl);

    if (obj["cls"] == "entity") {
        V3Config::addIgnore(V3ErrorCode("COMBDLY"), false, "*", 0, 0);
        string module_name = convertName(obj["name"].GetString());
        currentFilename = obj["filename"].GetString();
        m_fl = new FileLine(currentFilename);
        m_fl->newContent();
        updateFL(obj);
        AstModule *mod = new AstModule(fl(), module_name);
        symt.pushNew(mod);

        auto gen_array = obj["generic"].GetArray();
        for(Value::ConstValueIterator m = gen_array.Begin(); m != gen_array.End(); ++m) {
            auto gen_obj = m->GetObject();
            updateFL(gen_obj);
            VARRESET();
            VARDECL(GPARAM);
            VARDTYPE(translateType(fl(), gen_obj["type"].GetObject()));
            AstVar *generic_var = createVariable(fl(), gen_obj["name"].GetString(), NULL, NULL);
            symt.reinsert(generic_var);
            if (gen_obj.HasMember("val"))
                generic_var->valuep(translateObject(gen_obj["val"].GetObject()));
            mod->addStmtp(generic_var);
        }
        // Handle Ports
        pinnum = 1;
        auto port_array = obj["port"].GetArray();
        for(Value::ConstValueIterator m = port_array.Begin(); m != port_array.End(); ++m) {
            auto port_obj = m->GetObject();
            string direction = port_obj["dir"].GetString();
            if (direction == "in") {
                VARIO(INPUT);
            } else if (direction == "out") {
                VARIO(OUTPUT);
            } else if (direction == "inout") {
                VARIO(INOUT);
            }
            VARDECL(PORT);
            updateFL(port_obj);
            AstPort *port = new AstPort(fl(), pinnum++, port_obj["name"].GetString());

            VARDTYPE(translateType(fl(), port_obj["type"].GetObject()));
            mod->addStmtp(port);
            AstVar *port_var = createVariable(fl(), port->name(), NULL, NULL);
            symt.reinsert(port_var);
            if (port_obj.HasMember("val"))
                port_var->valuep(translateObject(port_obj["val"].GetObject()));
            mod->addStmtp(port_var);
        }
        pinnum = 0;

        v3Global.rootp()->addModulep(mod);
        symt.popScope(mod);

    } else if (obj["cls"] == "architecture") {
        currentFilename = obj["filename"].GetString();
        AstModule *entity_mod = (AstModule*)symt.findEntUpward(convertName(obj["of"].GetString()));
        symt.pushNew(entity_mod);
        if(entity_mod != NULL) {
            Value::ConstArray decls = obj["decls"].GetArray();

            for (Value::ConstValueIterator m = decls.Begin(); m != decls.End(); ++m) {
                AstNode * res = translateObject(m->GetObject());
                if(res) ((AstModule*)(entity_mod))->addStmtp(res);
            }

            Value::ConstArray stmts = obj["stmts"].GetArray();
            for (Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
                AstNode * res = translateObject(m->GetObject());
                if(res) ((AstModule*)(entity_mod))->addStmtp(res);
            }
            symt.popScope(entity_mod);
        }

    } else if (obj["cls"] == "pkg") {
        currentFilename = obj["filename"].GetString();
        string package_name = convertName(obj["name"].GetString());
        updateFL(obj);
        AstPackage *pkg = new AstPackage(fl(), package_name);
        symt.pushNew(pkg);
        Value::ConstArray decls = obj["decls"].GetArray();
        for (Value::ConstValueIterator m = decls.Begin(); m != decls.End(); ++m) {
            AstNode * res = translateObject(m->GetObject());
            if(res) pkg->addStmtp(res);
        }
        symt.popScope(pkg);
        V3Config::addIgnore(V3ErrorCode("COMBDLY"), true, "*", 0, 0);

    } else if (obj["cls"] == "process") {
        updateFL(obj);
        AstSenTree *st = new AstSenTree(fl(), NULL);
        AstAlways *process = new AstAlways(fl(), VAlwaysKwd::en::ALWAYS, st, NULL);
        symt.pushNew(process);
        current_process = process;
        AstBegin *block = new AstBegin(fl(), "", NULL);
        process->addStmtp(block);
        Value::ConstArray decls = obj["decls"].GetArray();

        for (Value::ConstValueIterator m = decls.Begin(); m != decls.End(); ++m) {
            AstNode * res = translateObject(m->GetObject());
            if(res) block->addStmtsp(res);
        }

        Value::ConstArray stmts = obj["stmts"].GetArray();
        for (Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            AstNode * res = translateObject(m->GetObject());
            if(res) block->addStmtsp(res);
        }

        current_process = NULL;
        m_sig_edges.clear();
        symt.popScope(process);
        RET_NODE(process);

    } else if (obj["cls"] == "wait") {
        Value::ConstArray on = obj["on"].GetArray();
        for (Value::ConstValueIterator m = on.Begin(); m != on.End(); ++m) {
            AstNode *ref = translateObject(m->GetObject());
            updateFL(obj);
            string item_name = m->GetObject()["name"].GetString();
            AstEdgeType edge_type = AstEdgeType::ET_ANYEDGE;
            if (m_sig_edges[item_name])
                edge_type = m_sig_edges[item_name];

            AstSenItem *si = new AstSenItem(fl(), edge_type, ref);
            if(ref) current_process->sensesp()->addSensesp(si);
        }

    } else if (obj["cls"] == "sigassign") {
        updateFL(obj);
        AstNode * lhsp = translateObject(obj["target"].GetObject());
        AstNode * rhsp = translateObject(obj["lhs"].GetObject());
        AstAssignDly * assign = new AstAssignDly(fl(), lhsp, rhsp);
        RET_NODE(assign);

    } else if (obj["cls"] == "varassign") {
        updateFL(obj);
        AstNode * lhsp = translateObject(obj["target"].GetObject());
        AstNode * rhsp = translateObject(obj["lhs"].GetObject());
        AstAssign *assign = new AstAssign(fl(), lhsp, rhsp);
        RET_NODE(assign);

    } else if (obj["cls"] == "ref") {
        updateFL(obj);
        string refname = obj["name"].GetString();
        if (refname[0] == '\'') {
            if (refname[1] == '0' or refname[1] == 'L') {
                RET_NODE(new AstConst(fl(), AstConst::LogicFalse()));
            }
            else if (refname[1] == '1' or refname[1] == 'H') {
                RET_NODE(new AstConst(fl(), AstConst::LogicTrue()));
            }
            else if (refname[1] == 'U' or refname[1] == 'X') {
                RET_NODE(new AstConst(fl(), AstConst::StringToParse(), "1'sbX"));
            }
        } else if (refname == "TRUE") {
            RET_NODE(new AstConst(fl(), AstConst::LogicTrue()));
        } else if (refname == "FALSE") {
            RET_NODE(new AstConst(fl(), AstConst::LogicFalse()));
        }
        else {
            string name = convertName(refname);
            AstNode *refNode = symt.findEntUpward(name);
            if (VN_IS(refNode, EnumItem)) {
                RET_NODE(new AstEnumItemRef(fl(), VN_CAST(refNode, EnumItem), NULL));
            } else if (VN_IS(refNode, Var)) {
                RET_NODE(new AstVarRef(fl(), name, false));
            } else {
                v3fatal("Failed to find reference to " << name);
            }
            RET_NODE((AstNode*)nullptr);
        }

    } else if (obj["cls"] == "fcall") {
        RET_NODE(translateFcall(obj));

    } else if (obj["cls"] == "int") {
        updateFL(obj);
        RET_NODE(new AstConst(fl(), AstConst::Unsized32(), obj["value"].GetUint()));

    } else if (obj["cls"] == "real") {
        updateFL(obj);
        RET_NODE(new AstConst(fl(), AstConst::RealDouble(), obj["value"].GetDouble()));

    } else if (obj["cls"] == "if") {
        updateFL(obj);
        AstIf *ifn = new AstIf(fl(), translateObject(obj["cond"].GetObject()), NULL, NULL);

        Value::ConstArray thens = obj["then"].GetArray();
        for (Value::ConstValueIterator m = thens.Begin(); m != thens.End(); ++m) {
            AstNode *res = translateObject(m->GetObject());
            if(res) ifn->addIfsp(res);
        }

        Value::ConstArray elses = obj["else"].GetArray();
        for (Value::ConstValueIterator m = elses.Begin(); m != elses.End(); ++m) {
            AstNode *res = translateObject(m->GetObject());
            if(res) ifn->addElsesp(res);
        }
        RET_NODE(ifn);

    } else if (obj["cls"] == "sigdecl" or obj["cls"] == "vardecl") {
        VARRESET();
        VARDECL(VAR);
        updateFL(obj);
        VARDTYPE(translateType(fl(), obj["type"].GetObject()));
        string varName = convertName(obj["name"].GetString());
        AstVar *var = createVariable(fl(), varName, NULL, NULL);
        symt.reinsert(var);
        if (obj.HasMember("val"))
            var->valuep(translateObject(obj["val"].GetObject()));
        RET_NODE(var);

    } else if (obj["cls"] == "constdecl") {
        VARRESET();
        VARDECL(LPARAM);
        updateFL(obj);
        VARDTYPE(translateType(fl(), obj["type"].GetObject()));
        string varName = convertName(obj["name"].GetString());
        AstVar *var = createVariable(fl(), varName, NULL, NULL);
        symt.reinsert(var);
        if (obj.HasMember("val"))
            var->valuep(translateObject(obj["val"].GetObject()));
        RET_NODE(var);

    } else if (obj["cls"] == "aref") {
        updateFL(obj);
        Value::ConstArray params = obj["params"].GetArray();
        if (params[0]["name"].IsNull()) {
            RET_NODE(new AstSelBit(fl(), translateObject(obj["of"].GetObject()), translateObject(params[0]["value"].GetObject())));
        }
        RET_NODE((AstNode*)nullptr); // TODO fix this

    } else if (obj["cls"] == "aslice") {
        updateFL(obj);
        Value::ConstObject rng = obj["range"].GetObject();
        RET_NODE(new AstSelExtract(fl(), translateObject(obj["of"].GetObject()), translateObject(rng["l"].GetObject()), translateObject(rng["r"].GetObject())));

    } else if (obj["cls"] == "concat") {
        updateFL(obj);
        Value::ConstArray items = obj["items"].GetArray();
        AstNode *last_concat = new AstConcat(fl(), translateObject(items[0].GetObject()), translateObject(items[1].GetObject()));
        for (int i = 2; i < items.Size(); ++i) {
            updateFL(items[i].GetObject());
            last_concat = new AstConcat(fl(), last_concat, translateObject(items[i].GetObject()));
        }
        RET_NODE(last_concat);

    } else if (obj["cls"] == "component") {
        RET_NODE((AstNode*)nullptr);

    } else if (obj["cls"] == "next") {
        updateFL(obj);
        AstNode *next = new AstContinue(fl());
        if(!obj["when"].IsNull()) {
            next = new AstIf(fl(), translateObject(obj["when"].GetObject()), next);
        }
        RET_NODE(next);

    } else if (obj["cls"] == "exit") {
        updateFL(obj);
        AstNode *exit = new AstBreak(fl());
        if(!obj["when"].IsNull()) {
            exit = new AstIf(fl(), translateObject(obj["when"].GetObject()), exit);
        }
        RET_NODE(exit);

    } else if (obj["cls"] == "while") {
        updateFL(obj);
        AstNode *whle = new AstWhile(fl(), translateObject(obj["cond"].GetObject()), translateObject(obj["stmts"].GetObject()));
        RET_NODE(whle);

    } else if (obj["cls"] == "typeconv") {
        updateFL(obj);
        RET_NODE(new AstCast(fl(), translateObject(obj["expr"].GetObject()), translateType(fl(), obj["type"].GetObject())));

    } else if (obj["cls"] == "typedecl") {
        updateFL(obj);

        AstNodeDType *nodedtype = NULL;
        switch ((type_kind_t)obj["kind"].GetInt()) {
            case T_ENUM:
            {
                nodedtype = new AstEnumDType(fl(), VFlagChildDType(),
                    new AstBasicDType(fl(), AstBasicDTypeKwd::INT), NULL);
                Value::ConstArray enum_val = obj["enum_val"].GetArray();
                unsigned int indexValue = 0;
                for(Value::ConstValueIterator m = enum_val.Begin();
                    m != enum_val.End(); ++m) {
                    AstEnumItem *enumItem = new AstEnumItem(fl(), m->GetString(), NULL,
                        new AstConst(fl(), indexValue));
                    symt.reinsert(enumItem);
                    ((AstEnumDType*)nodedtype)->addValuesp(enumItem);
                    indexValue ++;
                }
                break;
            }

            case T_CARRAY:
            {
                AstNodeDType *basedtp = translateType(fl(), obj["of"].GetObject());
                AstNodeRange *rangep = nullptr;
                Value::ConstArray range_arr = obj["dims"].GetArray();

                for(Value::ConstValueIterator m = range_arr.Begin();
                    m != range_arr.End(); ++m) {
                        if (!rangep) {
                            rangep = VN_CAST(translateObject(m->GetObject()), NodeRange);
                        }
                        else {
                            rangep->addNext(VN_CAST(translateObject(m->GetObject()), NodeRange));
                        }
                }
                nodedtype = createArray(basedtp, rangep, true);
                break;
            }

            case T_RECORD:
            {
                AstStructDType *sdt = new AstStructDType(fl(), AstNumeric(AstNumeric::en::NOSIGN));
                symt.pushNew(sdt);
                Value::ConstArray fields = obj["fields"].GetArray();
                for(Value::ConstValueIterator m = fields.Begin(); m != fields.End(); ++m) {
                    Value::ConstObject memberObj = m->GetObject();
                    AstNode *member = new AstMemberDType(fl(), memberObj["name"].GetString(),
                        translateType(fl(), memberObj["type"].GetObject()));
                    symt.reinsert(member);
                    sdt->addMembersp(member);
                }
                symt.popScope(sdt);
                nodedtype = sdt;
                break;
            }

            default:
                v3fatalSrc("Failed to translate type definition" << endl);
                break;

        }

        AstNode *tdef = new AstTypedef(fl(), obj["name"].GetString(), NULL, VFlagChildDType(), nodedtype);
        symt.reinsert(tdef);
        RET_NODE(tdef);

    } else if (obj["cls"] == "string") {
        updateFL(obj);
        string value = obj["val"].GetString();
        RET_NODE(new AstConst(fl(), AstConst::String(), value));

    } else if (obj["cls"] == "string_lit") {
        updateFL(obj);
        string value = obj["val"].GetString();
        stringstream ss;
        ss << value.length() << "'sb" << value;
        string const verilogStr = ss.str();
        RET_NODE(new AstConst(fl(), V3Number(V3Number::FileLined(), fl(), verilogStr.c_str())));

    } else if (obj["cls"] == "aggregate") {
        updateFL(obj);
        AstVHDAggregate *aggr = new AstVHDAggregate(fl(), NULL);
        Value::ConstArray aggr_elts = obj["elts"].GetArray();
        for(Value::ConstValueIterator m = aggr_elts.Begin(); m != aggr_elts.End(); ++m) {
            Value::ConstObject elt = m->GetObject();
            aggr->addAggritemsp(translateObject(elt));
        }
        RET_NODE(aggr);

    } else if (obj["cls"] == "instance") {
        static long instanceCount = 0;
        string inst_base = convertName(obj["name"].GetString());
        updateFL(obj);
        AstCell * instance = new AstCell(fl(), fl(), obj["iname"].GetString(), inst_base, NULL, NULL, NULL);
        Value::ConstArray ports = obj["port"].GetArray();
        long pinNum = 1;
        Value::ConstArray generics = obj["generic"].GetArray();
        for(Value::ConstValueIterator m = generics.Begin(); m != generics.End(); ++m) {
            Value::ConstObject generic = m->GetObject();
            string generic_name = (generic["name"].GetObject())["name"].GetString();
            updateFL(generic);
            instance->addParamsp(new AstPin(fl(), pinNum++, generic_name, translateObject(generic["value"].GetObject())));
        }
        pinNum = 1;
        for(Value::ConstValueIterator m = ports.Begin(); m != ports.End(); ++m) {
            Value::ConstObject port = m->GetObject();
            string portname = (port["name"].GetObject())["name"].GetString();
            updateFL(port);
            instance->addPinsp(new AstPin(fl(), pinNum++, portname, translateObject(port["value"].GetObject())));
        }
        RET_NODE(instance);

    } else if (obj["cls"] == "aggregate_named") {
        updateFL(obj);
        RET_NODE(new AstVHDAggregateItem(fl(), translateObject(obj["l"].GetObject()), translateObject(obj["expr"].GetObject())));

    } else if (obj["cls"] == "aggregate_range") {
        updateFL(obj);
        RET_NODE(new AstVHDAggregateItem(fl(), translateObject(obj["range"].GetObject()), translateObject(obj["expr"].GetObject())));

    } else if (obj["cls"] == "aggregate_pos") {
        updateFL(obj);
        RET_NODE(new AstVHDAggregateItem(fl(), NULL, translateObject(obj["expr"].GetObject())));

    } else if (obj["cls"] == "aggregate_others") {
        updateFL(obj);
        RET_NODE(new AstVHDAggregateItem(fl(), NULL, translateObject(obj["expr"].GetObject())));

    } else if (obj["cls"] == "attr") {
        updateFL(obj);
        RET_NODE(new AstVHDPredefinedAttr(fl(), obj["name"].GetString(), translateObject(obj["op"].GetObject())));

    } else if (obj["cls"] == "case") {
        updateFL(obj);
        AstCase *cas = new AstCase(fl(), VCaseType(), translateObject(obj["sel"].GetObject()), NULL);
        Value::ConstArray assoc = obj["assoc"].GetArray();
        for(Value::ConstValueIterator m = assoc.Begin(); m != assoc.End(); ++m) {
            Value::ConstObject assoc_obj = m->GetObject();
            AstNode *expr = NULL;
            if (assoc_obj.HasMember("expr"))
             expr = translateObject(assoc_obj["expr"].GetObject());

            cas->addItemsp(new AstCaseItem(fl(), expr, translateObject(assoc_obj["to"].GetObject())));
        }
        RET_NODE(cas);

    } else if (obj["cls"] == "block") {
        updateFL(obj);
        AstBegin *block = new AstBegin(fl(), "", NULL);
        Value::ConstObject blk = obj["block"].GetObject();
        Value::ConstArray decls = blk["decl"].GetArray();
        for(Value::ConstValueIterator m = decls.Begin(); m != decls.End(); ++m) {
            block->addStmtsp(translateObject(m->GetObject()));
        }
        Value::ConstArray stmts = blk["stmts"].GetArray();
        for(Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            block->addStmtsp(translateObject(m->GetObject()));
        }
        RET_NODE(block);

    } else if (obj["cls"] == "if_generate") {
        updateFL(obj);
        AstGenerate *gen = new AstGenerate(fl(), NULL);
        AstIf *ifp = new AstIf(fl(), translateObject(obj["cond"].GetObject()), NULL);
        Value::ConstArray stmts = obj["stmts"].GetArray();
        for(Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            ifp->addIfsp(translateObject(m->GetObject()));
        }
        gen->addStmtp(ifp);
        RET_NODE(gen);

    } else if (obj["cls"] == "assert") {
        RET_NODE((AstNode*)nullptr);

    } else if (obj["cls"] == "fbody" or obj["cls"] == "fdecl") {
        updateFL(obj);
        AstNode *rettype = translateType(fl(), obj["ret_type"].GetObject());
        AstFunc *func = new AstFunc(fl(), obj["name"].GetString(), NULL, rettype);
        pinnum = 1;
        auto port_array = obj["ports"].GetArray();
        for(Value::ConstValueIterator m = port_array.Begin(); m != port_array.End(); ++m) {
            auto port_obj = m->GetObject();
            string direction = port_obj["dir"].GetString();
            if (direction == "in") {
                VARIO(INPUT);
            } else if (direction == "out" or direction == "buffer") {
                VARIO(OUTPUT);
            } else if (direction == "inout") {
                VARIO(INOUT);
            }
            VARDECL(PORT);
            updateFL(port_obj);
            VARDTYPE(translateType(fl(), port_obj["type"].GetObject()));
            AstVar *port_var = createVariable(fl(), port_obj["name"].GetString(), NULL, NULL);
            symt.reinsert(port_var);
            if (port_obj.HasMember("val"))
                port_var->valuep(translateObject(obj["val"].GetObject()));
            func->addStmtsp(port_var);
            PINNUMINC();
        }
        pinnum = 0;
        if (obj.HasMember("stmts")) {
            Value::ConstObject body = obj["stmts"].GetObject();
            Value::ConstArray body_decl = body["decl"].GetArray();
            for(Value::ConstValueIterator m = body_decl.Begin(); m != body_decl.End(); ++m) {
                func->addStmtsp(translateObject(m->GetObject()));
            }
            Value::ConstArray body_stmts = body["stmts"].GetArray();
                    for(Value::ConstValueIterator m = body_stmts.Begin(); m != body_stmts.End(); ++m) {
                func->addStmtsp(translateObject(m->GetObject()));
            }
            func->prototype(false);
        }
        else {
            func->prototype(true);
        }

        symt.reinsert(func);
        RET_NODE(func);

    } else if (obj["cls"] == "fcall") {
        updateFL(obj);
        AstTaskRef *funcref = new AstTaskRef(fl(), obj["name"].GetString(), NULL);
        Value::ConstArray stmts = obj["params"].GetArray();
        for(Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            funcref->addPinsp(translateObject(m->GetObject()));
        }
        RET_NODE(funcref);

    } else if (obj["cls"] == "pbody" or obj["cls"] == "pdecl") {
        updateFL(obj);
        AstTask *task = new AstTask(fl(), obj["name"].GetString(), NULL);
        auto port_array = obj["ports"].GetArray();
        pinnum = 1;
        for(Value::ConstValueIterator m = port_array.Begin(); m != port_array.End(); ++m) {
            auto port_obj = m->GetObject();
            string direction = port_obj["dir"].GetString();
            if (direction == "in") {
                VARIO(INPUT);
            } else if (direction == "out") {
                VARIO(OUTPUT);
            } else if (direction == "inout") {
                VARIO(INOUT);
            }
            VARDECL(PORT);
            updateFL(port_obj);
            VARDTYPE(translateType(fl(), port_obj["type"].GetObject()));
            AstVar *port_var = createVariable(fl(), port_obj["name"].GetString(), NULL, NULL);
            symt.reinsert(port_var);
            if (port_obj.HasMember("val"))
                port_var->valuep(translateObject(obj["val"].GetObject()));
            task->addStmtsp(port_var);
            PINNUMINC();
        }
        pinnum = 0;
        if (obj.HasMember("stmts")) {
            Value::ConstObject body = obj["stmts"].GetObject();
            Value::ConstArray body_decl = body["decl"].GetArray();
            for(Value::ConstValueIterator m = body_decl.Begin(); m != body_decl.End(); ++m) {
                task->addStmtsp(translateObject(m->GetObject()));
            }
            Value::ConstArray body_stmts = body["stmts"].GetArray();
                    for(Value::ConstValueIterator m = body_stmts.Begin(); m != body_stmts.End(); ++m) {
                task->addStmtsp(translateObject(m->GetObject()));
            }
            task->prototype(false);
        }
        else {
            task->prototype(true);
        }
        symt.reinsert(task);
        RET_NODE(task);

    } else if (obj["cls"] == "pcall") {
        updateFL(obj);
        AstTaskRef *taskref = new AstTaskRef(fl(), obj["name"].GetString(), NULL);
        Value::ConstArray stmts = obj["params"].GetArray();
        for(Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            taskref->addPinsp(translateObject(m->GetObject()));
        }
        RET_NODE(taskref);

    } else if (obj["cls"] == "for") {
        updateFL(obj);
        AstVarRef *varref = new AstVarRef(fl(), obj["name"].GetString(), true);
        Value::ConstArray stmts = obj["stmts"].GetArray();
        AstBegin *begin = new AstBegin(fl(), "", NULL, false);
        symt.pushNew(begin);
        VARRESET_NONLIST(VAR);
        VARDTYPE(new AstBasicDType(fl(), AstBasicDTypeKwd::INT));
        AstVar *iterateVar = createVariable(fl(), convertName(obj["name"].GetString()), NULL, NULL);
        begin->addStmtsp(iterateVar);
        symt.reinsert(iterateVar);
        AstVHDLFor *forp = new AstVHDLFor(fl(), varref,
            translateObject((obj["range"].GetObject())), NULL, false);
        for(Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            forp->addBodysp(translateObject(m->GetObject()));
        }

        begin->addStmtsp(forp);
        symt.popScope(begin);
        RET_NODE(begin);

    } else if (obj["cls"] == "for_generate") {
        updateFL(obj);
        AstGenerate *genp = new AstGenerate(fl(), NULL);
        AstBegin *beginp = new AstBegin(fl(), "", NULL, true);
        genp->addStmtp(beginp);
        symt.pushNew(beginp);
        string varname = obj["name"].GetString();
        VARRESET_NONLIST(GENVAR);
        VARDTYPE(new AstBasicDType(fl(), AstBasicDTypeKwd::INT));
        AstVar *iterateVar = createVariable(fl(), convertName(varname), NULL, NULL);
        symt.reinsert(iterateVar);
        AstVarRef *varref = new AstVarRef(fl(), varname, true);
        AstVHDLFor *forp = new AstVHDLFor(fl(), varref,
            translateObject(obj["range"].GetObject()), NULL, true);
        Value::ConstArray stmts = obj["stmts"].GetArray();
        for(Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            forp->addBodysp(translateObject(m->GetObject()));
        }
        symt.popScope(beginp);
        beginp->addStmtsp(forp);
        RET_NODE(genp);

    } else if (obj["cls"] == "range") {
        updateFL(obj);
        AstRange *range = new AstRange(fl(), translateObject(obj["l"].GetObject()),
            translateObject(obj["r"].GetObject()));
        range->littleEndian(obj["dir"].GetString() == string("to"));
        RET_NODE(range);

    } else if (obj["cls"] == "return") {
        updateFL(obj);
        AstReturn *ret = new AstReturn(fl(), translateObject(obj["expr"].GetObject()));
        RET_NODE(ret);

    } else {
        v3error("Failed to translate object of class " << obj["cls"].GetString());
    }
    RET_NODE((AstNode*)nullptr);
}

void V4VhdlTranslate::translate(string filename)
{
    ifstream fil(filename);
    if(fil) {
        Document document;
        IStreamWrapper sw(fil);
        document.ParseStream(sw);
        for (Value::ConstValueIterator m = document.Begin(); m != document.End(); ++m) {
            Value::ConstObject obj = m->GetObject();
            translateObject(obj);
        }
    }
    else
    {
        v3error("Failed to parse temporary file");
    }
}