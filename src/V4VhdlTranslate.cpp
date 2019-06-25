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
    currentLevel = 0;
}

V4VhdlTranslate::~V4VhdlTranslate()
{

}

string V4VhdlTranslate::indentString() {
    stringstream ss;
    for (int i = 0; i < currentLevel and i < 20; ++i) {
        ss << "  ";
    }
    return ss.str();
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

AstNodeDType *V4VhdlTranslate::translateType(FileLine *fl, Value::ConstObject item) {
    string type_name = item["name"].GetString();
    UINFO(9, indentString() << "Type " << type_name << endl);

    if (type_name == "STD_LOGIC") {
        return new AstBasicDType(fl, AstBasicDTypeKwd::LOGIC_IMPLICIT);
    } else if (type_name == "STD_LOGIC_VECTOR" or type_name == "UNSIGNED" or type_name == "SIGNED") {
        AstNodeDType *base_type = new AstBasicDType(fl, AstBasicDTypeKwd::LOGIC_IMPLICIT);
        FileLine *fl3 = new FileLine(currentFilename, 0);
        Value::ConstObject range_o = item["range"].GetArray()[0].GetObject();
        AstRange *range = new AstRange(fl3, translateObject(range_o["l"].GetObject()), translateObject(range_o["r"].GetObject()));
        return createArray(base_type, range, true);
    } else if (type_name == "INTEGER" or type_name == "NATURAL" or type_name == "POSITIVE") {
        return new AstBasicDType(fl, AstBasicDTypeKwd::INT);
    } else {
        return new AstRefDType(fl, type_name);
    }

}

AstNode *V4VhdlTranslate::translateFcall(Value::ConstObject item) {
    string fname = item["name"].GetString();
    UINFO(9, indentString() << "FCall " << fname << endl);
    FileLine *fl = new FileLine(currentFilename, getLine(item));
    vector<AstNode*> params;
    Value::ConstArray stmts = item["params"].GetArray();
    for (Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
        AstNode *res = translateObject(m->GetObject()["value"].GetObject());
        params.push_back(res);
    }

    if (fname == "IEEE.STD_LOGIC_1164.\"and\"" or fname == "\"and\"")
        return new AstAnd(fl, params[0], params[1]);
    else if (fname == "IEEE.STD_LOGIC_1164.\"or\"" or fname == "\"or\"")
        return new AstOr(fl, params[0], params[1]);
    else if (fname == "IEEE.STD_LOGIC_1164.\"not\"" or fname == "\"not\"")
        return new AstNot(fl, params[0]);
    else if (fname == "IEEE.STD_LOGIC_1164.\"nand\"" or fname == "\"nand\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstNot(fl, new AstAnd(fl, params[0], params[1]));
    } else if (fname == "IEEE.STD_LOGIC_1164.\"nor\"" or fname == "\"nor\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstNot(fl, new AstOr(fl, params[0], params[1]));
    } else if (fname == "IEEE.STD_LOGIC_1164.\"xor\"" or fname == "\"xor\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstXor(fl, params[0], params[1]);
    } else if (fname == "IEEE.STD_LOGIC_1164.\"xnor\"" or fname == "\"xnor\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstNot(fl, new AstXor(fl, params[0], params[1]));
    } else if (fname == "IEEE.NUMERIC_STD.\"+\"" or fname == "\"+\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstAdd(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"-\"" or fname == "\"-\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstSub(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"*\"" or fname == "\"*\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstMul(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"/\"" or fname == "\"/\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstDiv(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"<\"" or fname == "\"<\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstLt(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"<=\"" or fname == "\"<=\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstLte(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\">\"" or fname == "\">\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstGt(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\">=\"" or fname == "\">=\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstGte(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"=\"" or fname == "\"=\"") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstEq(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.TO_INTEGER") {
        return params[0];
    } else if (fname == "IEEE.MATH_REAL.CEIL") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstCeilD(fl2, params[0]);
    } else if (fname == "IEEE.MATH_REAL.LOG2") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        return new AstDivD(fl2, new AstLog10D(fl2, params[0]), new AstLog10D(fl2, new AstConst(fl2, 2)));
    } else if (fname == "IEEE.STD_LOGIC_1164.RISING_EDGE") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        m_sig_edges.insert(pair<string, AstEdgeType>(((AstVarRef*)params[0])->name(), AstEdgeType::ET_POSEDGE));
        // Return True, if removed by constify pass
        return new AstConst(fl2, AstConst::LogicTrue());
    } else if (fname == "IEEE.STD_LOGIC_1164.FALLING_EDGE") {
        FileLine *fl2 = new FileLine(currentFilename, 0);
        m_sig_edges.insert(pair<string, AstEdgeType>(((AstVarRef*)params[0])->name(), AstEdgeType::ET_NEGEDGE));
        // Return True, if removed by constify pass
        return new AstConst(fl2, AstConst::LogicTrue());
    }
    v3error("Failed to translate function " + fname);
    return nullptr;
}

string convertName(string inName) { // TODO: clean this hack
    return inName.erase(0,5);
}

AstNode *V4VhdlTranslate::translateObject(Value::ConstObject item) {
    Value::ConstObject obj = item;
    string object_name = "";

    if(obj.HasMember("name") and obj["name"].IsString())
        object_name = obj["name"].GetString();

    UINFO(9, indentString() << "Object " << obj["cls"].GetString() << " " << object_name << endl);
    currentLevel++;

    if (obj["cls"] == "entity") {
        string module_name = convertName(obj["name"].GetString());
        currentFilename = obj["filename"].GetString();
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstModule *mod = new AstModule(fl, module_name);
        symt.pushNew(mod);

        auto gen_array = obj["generic"].GetArray();
        for(Value::ConstValueIterator m = gen_array.Begin(); m != gen_array.End(); ++m) {
            auto gen_obj = m->GetObject();
            FileLine *fl = new FileLine(currentFilename, getLine(gen_obj));
            VARRESET();
            VARDECL(GPARAM);
            VARDTYPE(translateType(fl, gen_obj["type"].GetObject()));
            AstVar *generic_var = createVariable(fl, gen_obj["name"].GetString(), NULL, NULL);
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
            FileLine *fl = new FileLine(currentFilename, getLine(port_obj));
            AstPort *port = new AstPort(fl, pinnum++, port_obj["name"].GetString());

            VARDTYPE(translateType(fl, port_obj["type"].GetObject()));
            mod->addStmtp(port);
            AstVar *port_var = createVariable(fl, port->name(), NULL, NULL);
            symt.reinsert(port_var);
            if (port_obj.HasMember("val"))
                port_var->valuep(translateObject(obj["val"].GetObject()));
            mod->addStmtp(port_var);
        }
        pinnum = 0;

        v3Global.rootp()->addModulep(mod);
        currentLevel--;
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
        currentLevel--;

    } else if (obj["cls"] == "process") {
        FileLine *fl_st = new FileLine(currentFilename, getLine(obj));
        AstSenTree *st = new AstSenTree(fl_st, NULL);

        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstAlways *process = new AstAlways(fl, VAlwaysKwd::en::ALWAYS, st, NULL);
        symt.pushNew(process);
        current_process = process;
        Value::ConstArray decls = obj["decls"].GetArray();

        for (Value::ConstValueIterator m = decls.Begin(); m != decls.End(); ++m) {
            AstNode * res = translateObject(m->GetObject());
            if(res) process->addStmtp(res);
        }
        
        Value::ConstArray stmts = obj["stmts"].GetArray();
        for (Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            AstNode * res = translateObject(m->GetObject());
            if(res) process->addStmtp(res);
        }

        current_process = NULL;
        m_sig_edges.clear();
        currentLevel--;
        symt.popScope(process);
        return process;

    } else if (obj["cls"] == "wait") {
        
        Value::ConstArray on = obj["on"].GetArray();
        for (Value::ConstValueIterator m = on.Begin(); m != on.End(); ++m) {
            AstNode *ref = translateObject(m->GetObject());
            FileLine *fl2 = new FileLine(currentFilename, getLine(obj));
            string item_name = m->GetObject()["name"].GetString();
            AstEdgeType edge_type = AstEdgeType::ET_ANYEDGE;
            if (m_sig_edges[item_name])
                edge_type = m_sig_edges[item_name];

            AstSenItem *si = new AstSenItem(fl2, edge_type, ref);
            if(ref) current_process->sensesp()->addSensesp(si);
        }
        currentLevel--;

    } else if (obj["cls"] == "sigassign") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstNode * lhsp = translateObject(obj["target"].GetObject());
        AstNode * rhsp = translateObject(obj["lhs"].GetObject());
        AstAssignDly * assign = new AstAssignDly(fl, lhsp, rhsp);
        currentLevel--;
        return assign;

    } else if (obj["cls"] == "varassign") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstNode * lhsp = translateObject(obj["target"].GetObject());
        AstNode * rhsp = translateObject(obj["lhs"].GetObject());
        AstAssign *assign = new AstAssign(fl, lhsp, rhsp);
        currentLevel--;
        return assign;

    } else if (obj["cls"] == "ref") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        string refname = obj["name"].GetString();
        currentLevel--;
        if (refname[0] == '\'') {
            if (refname[1] == '0' or refname[1] == 'L')
                return new AstConst(fl, AstConst::LogicFalse());
            else if (refname[1] == '1' or refname[1] == 'H')
                return new AstConst(fl, AstConst::LogicTrue());
            else if (refname[1] == 'U' or refname[1] == 'X')
                return new AstConst(fl, AstConst::StringToParse(), "1'bX");
        }
        else {
            AstNode *refNode = symt.findEntUpward(refname);
            if (VN_IS(refNode, EnumItem)) {
                return new AstEnumItemRef(fl, VN_CAST(refNode, EnumItem), NULL);
            } else if (VN_IS(refNode, Var)) {
                return new AstVarRef(fl, refname, false);
            } else {
                v3fatal("Failed to find reference to " << refname);
            }
            return nullptr;
        }
    
    } else if (obj["cls"] == "fcall") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        currentLevel--;
        return translateFcall(obj);

    } else if (obj["cls"] == "int") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        currentLevel--;
        return new AstConst(fl, AstConst::Unsized32(), obj["value"].GetUint());

    } else if (obj["cls"] == "if") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        currentLevel--;
        AstIf *ifn = new AstIf(fl, translateObject(obj["cond"].GetObject()), NULL, NULL);

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
        currentLevel--;
        return ifn;

    } else if (obj["cls"] == "sigdecl" or obj["cls"] == "vardecl") {
        VARRESET();
        VARDECL(VAR);
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        VARDTYPE(translateType(fl, obj["type"].GetObject()));
        currentLevel--;
        string varName = obj["name"].GetString();
        AstVar *var = createVariable(fl, varName, NULL, NULL);
        symt.reinsert(var);
        if (obj.HasMember("val"))
            var->valuep(translateObject(obj["val"].GetObject()));
        return var;

    } else if (obj["cls"] == "constdecl") {
        VARRESET();
        VARDECL(LPARAM);
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        VARDTYPE(translateType(fl, obj["type"].GetObject()));
        currentLevel--;
        string varName = obj["name"].GetString();
        AstVar *var = createVariable(fl, varName, NULL, NULL);
        symt.reinsert(var);
        if (obj.HasMember("val"))
            var->valuep(translateObject(obj["val"].GetObject()));
        return var;

    } else if (obj["cls"] == "aref") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        Value::ConstArray params = obj["params"].GetArray();
        if (params[0]["name"].IsNull()) {
            return new AstSelBit(fl, translateObject(obj["of"].GetObject()), translateObject(params[0]["value"].GetObject()));    
        }
        currentLevel--;
        return NULL; // TODO fix this

    } else if (obj["cls"] == "aslice") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        Value::ConstObject rng = obj["range"].GetObject();
        currentLevel--;
        return new AstSelExtract(fl, translateObject(obj["of"].GetObject()), translateObject(rng["l"].GetObject()), translateObject(rng["r"].GetObject()));

    } else if (obj["cls"] == "concat") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        Value::ConstArray items = obj["items"].GetArray();
        AstNode *last_concat = new AstConcat(fl, translateObject(items[0].GetObject()), translateObject(items[1].GetObject()));
        for (int i = 2; i < items.Size(); ++i) {
            last_concat = new AstConcat(fl, last_concat, translateObject(items[i].GetObject()));
        }
        currentLevel--;
        return last_concat;

    } else if (obj["cls"] == "component") {
        currentLevel--;
        return nullptr;

    } else if (obj["cls"] == "next") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstNode *next = new AstContinue(fl);
        if(!obj["when"].IsNull()) {
            next = new AstIf(fl, translateObject(obj["when"].GetObject()), next);
        }
        currentLevel--;
        return next;

    } else if (obj["cls"] == "exit") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstNode *exit = new AstBreak(fl);
        if(!obj["when"].IsNull()) {
            exit = new AstIf(fl, translateObject(obj["when"].GetObject()), exit);
        }
        currentLevel--;
        return exit;

    } else if (obj["cls"] == "while") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstNode *whle = new AstWhile(fl, translateObject(obj["cond"].GetObject()), translateObject(obj["stmts"].GetObject()));
        currentLevel--;
        return whle;

    } else if (obj["cls"] == "typeconv") {
        currentLevel--;
        return translateObject(obj["expr"].GetObject());

    } else if (obj["cls"] == "typedecl") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));

        AstNodeDType *nodedtype = NULL;
        if ((type_kind_t)obj["kind"].GetInt() == T_ENUM) {
            nodedtype = new AstEnumDType(fl, VFlagChildDType(), new AstBasicDType(fl, AstBasicDTypeKwd::INT), NULL);
            Value::ConstArray enum_val = obj["enum_val"].GetArray();
            unsigned int indexValue = 0;
            for(Value::ConstValueIterator m = enum_val.Begin(); m != enum_val.End(); ++m) {
                AstEnumItem *enumItem = new AstEnumItem(fl, m->GetString(), NULL, new AstConst(fl, indexValue));
                symt.reinsert(enumItem);
                ((AstEnumDType*)nodedtype)->addValuesp(enumItem);
                indexValue ++;
            }
        }
        AstNode *tdef = new AstTypedef(fl, obj["name"].GetString(), NULL, VFlagChildDType(), nodedtype);
        symt.reinsert(tdef);
        currentLevel--;
        return tdef;

    } else if (obj["cls"] == "string") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        string value = obj["val"].GetString();
        stringstream ss;
        ss << value.length() << "'b" << value;
        currentLevel--;
        return new AstConst(fl, V3Number(V3Number::FileLined(), fl, ss.str().c_str()));

    } else if (obj["cls"] == "aggregate") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstVHDAggregate *aggr = new AstVHDAggregate(fl, NULL);
        Value::ConstArray aggr_elts = obj["elts"].GetArray();
        for(Value::ConstValueIterator m = aggr_elts.Begin(); m != aggr_elts.End(); ++m) {
            Value::ConstObject elt = m->GetObject();
            aggr->addAggritemsp(translateObject(elt));
        }
        currentLevel--;
        return aggr;

    } else if (obj["cls"] == "instance") {
        static long instanceCount = 0;
        string inst_base = convertName(obj["name"].GetString());
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstCell * instance = new AstCell(fl, obj["iname"].GetString(), inst_base, NULL, NULL, NULL);
        Value::ConstArray ports = obj["port"].GetArray();
        long pinNum = 1;
        Value::ConstArray generics = obj["generic"].GetArray();
        for(Value::ConstValueIterator m = generics.Begin(); m != generics.End(); ++m) {
            Value::ConstObject generic = m->GetObject();
            string generic_name = (generic["name"].GetObject())["name"].GetString();
            FileLine *pinfl = new FileLine(currentFilename, getLine(generic));
            instance->addParamsp(new AstPin(pinfl, pinNum++, generic_name, translateObject(generic["value"].GetObject())));
        }
        pinNum = 1;
        for(Value::ConstValueIterator m = ports.Begin(); m != ports.End(); ++m) {
            Value::ConstObject port = m->GetObject();
            string portname = (port["name"].GetObject())["name"].GetString();
            FileLine *pinfl = new FileLine(currentFilename, getLine(port));
            instance->addPinsp(new AstPin(pinfl, pinNum++, portname, translateObject(port["value"].GetObject())));
        }
        currentLevel--;
        return instance;
    } else if (obj["cls"] == "aggregate_others") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        return new AstVHDAggregateItem(fl, NULL, translateObject(obj["expr"].GetObject()));

    } else if (obj["cls"] == "attr") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        return new AstVHDPredefinedAttr(fl, obj["name"].GetString(), translateObject(obj["op"].GetObject()));

    } else if (obj["cls"] == "case") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstCase *cas = new AstCase(fl, VCaseType(), translateObject(obj["sel"].GetObject()), NULL);
        Value::ConstArray assoc = obj["assoc"].GetArray();
        for(Value::ConstValueIterator m = assoc.Begin(); m != assoc.End(); ++m) {
            Value::ConstObject assoc_obj = m->GetObject();
            AstNode *expr = NULL;
            if (assoc_obj.HasMember("expr"))
             expr = translateObject(assoc_obj["expr"].GetObject());

            cas->addItemsp(new AstCaseItem(fl, expr, translateObject(assoc_obj["to"].GetObject())));
        }
        return cas;

    } else if (obj["cls"] == "block") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        AstBegin *block = new AstBegin(fl, "", NULL);
        Value::ConstObject blk = obj["block"].GetObject();
        Value::ConstArray decls = blk["decl"].GetArray();
        for(Value::ConstValueIterator m = decls.Begin(); m != decls.End(); ++m) {
            block->addStmtsp(translateObject(m->GetObject()));
        }
        Value::ConstArray stmts = blk["stmts"].GetArray();
        for(Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
            block->addStmtsp(translateObject(m->GetObject()));
        }
        return block;

    } else if (obj["cls"] == "for_generate") {
        FileLine *fl = new FileLine(currentFilename, getLine(obj));
        return nullptr;


    } else if (obj["cls"] == "assert") {
        return nullptr;

    } else {
        v3error("Failed to translate object of class " << obj["cls"].GetString());
    }
    return nullptr;
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
