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

}

V4VhdlTranslate::~V4VhdlTranslate()
{

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
    FileLine* newfl = new FileLine(fileline);
    newfl->warnOff(V3ErrorCode::WIDTH, true);
    AstNode* nodep = new AstConst(newfl, V3Number(newfl));
    // Adding a NOT is less work than figuring out how wide to make it
    if (value) nodep = new AstNot(newfl, nodep);
    nodep = new AstAssignW(newfl, new AstVarRef(fileline, name, true),
			   nodep);
    return nodep;
}

AstNodeDType *V4VhdlTranslate::translateType(Value::ConstObject item) {
    FileLine *fl2 = new FileLine("", 0);
    string type_name = item["name"].GetString();
    if (type_name == "STD_LOGIC%s") {
        return new AstBasicDType(fl2, AstBasicDTypeKwd::LOGIC_IMPLICIT);
    } else if (type_name == "STD_LOGIC_VECTOR%s" or type_name == "UNSIGNED%s" or type_name == "SIGNED%s") {
        AstNodeDType *base_type = new AstBasicDType(fl2, AstBasicDTypeKwd::LOGIC_IMPLICIT);
        FileLine *fl3 = new FileLine("", 0);
        Value::ConstObject range_o = item["range"].GetArray()[0].GetObject();
        AstRange *range = new AstRange(fl3, translateObject(range_o["l"].GetObject()), translateObject(range_o["r"].GetObject()));
        return createArray(base_type, range, true);

    } else {
        cout << "Unknown type" << endl;
        return nullptr;
    }

}

AstNode *V4VhdlTranslate::translateFcall(Value::ConstObject item) {
    string fname = item["name"].GetString();
    FileLine *fl = new FileLine("", 0);
    vector<AstNode*> params;
    Value::ConstArray stmts = item["params"].GetArray();
    for (Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
        AstNode *res = translateObject(m->GetObject()["value"].GetObject());
        params.push_back(res);
    }

    if (fname == "IEEE.STD_LOGIC_1164.\"and\"")
        return new AstAnd(fl, params[0], params[1]);
    else if (fname == "IEEE.STD_LOGIC_1164.\"or\"")
        return new AstOr(fl, params[0], params[1]);
    else if (fname == "IEEE.STD_LOGIC_1164.\"not\"")
        return new AstNot(fl, params[0]);
    else if (fname == "IEEE.STD_LOGIC_1164.\"nand\"") {
        FileLine *fl2 = new FileLine("", 0);
        return new AstNot(fl, new AstAnd(fl, params[0], params[1]));
    } else if (fname == "IEEE.STD_LOGIC_1164.\"nor\"") {
        FileLine *fl2 = new FileLine("", 0);
        return new AstNot(fl, new AstOr(fl, params[0], params[1]));
    } else if (fname == "IEEE.STD_LOGIC_1164.\"xor\"") {
        FileLine *fl2 = new FileLine("", 0);
        return new AstXor(fl, params[0], params[1]);
    } else if (fname == "IEEE.STD_LOGIC_1164.\"xnor\"") {
        FileLine *fl2 = new FileLine("", 0);
        return new AstNot(fl, new AstXor(fl, params[0], params[1]));
    } else if (fname == "IEEE.NUMERIC_STD.\"+\"") {
        FileLine *fl2 = new FileLine("", 0);
        return new AstAdd(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"-\"") {
        FileLine *fl2 = new FileLine("", 0);
        return new AstSub(fl, params[0], params[1]);
    } else if (fname == "IEEE.NUMERIC_STD.\"*\"") {
        FileLine *fl2 = new FileLine("", 0);
        return new AstMul(fl, params[0], params[1]);
    }
    return nullptr;
}

string convertName(string inName) { // TODO: clean this hack
    return inName.erase(0,5);
}

AstNode *V4VhdlTranslate::translateObject(Value::ConstObject item) {
    auto obj = item;
    if (obj["cls"] == "entity") {
        FileLine *fl = new FileLine("", 0);
        string module_name = convertName(obj["name"].GetString());
        AstModule *mod = new AstModule(fl, module_name);

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
            FileLine *fl = new FileLine("", 0);
            AstPort *port = new AstPort(fl, pinnum++, port_obj["name"].GetString());

            VARDTYPE(translateType(port_obj["type"].GetObject()));
            mod->addStmtp(port);
            mod->addStmtp(createVariable(fl, port->name(), NULL, NULL));
        }
        pinnum = 0;

        v3Global.rootp()->addModulep(mod);
        symt.pushNew(mod);
        m_entities.insert(pair<string,AstNode*>(module_name, mod));

    } else if (obj["cls"] == "architecture") {
        auto entity_mod = m_entities.find(convertName(obj["of"].GetString()));
        if(entity_mod != m_entities.end()) {
            Value::ConstArray decls = obj["decls"].GetArray();

            for (Value::ConstValueIterator m = decls.Begin(); m != decls.End(); ++m) {
                AstNode * res = translateObject(m->GetObject());
                if(res) ((AstModule*)(entity_mod->second))->addStmtp(res);
            }
            
            Value::ConstArray stmts = obj["stmts"].GetArray();
            for (Value::ConstValueIterator m = stmts.Begin(); m != stmts.End(); ++m) {
                AstNode * res = translateObject(m->GetObject());
                if(res) ((AstModule*)(entity_mod->second))->addStmtp(res);
            }
        }

    } else if (obj["cls"] == "process") {
        FileLine *fl_st = new FileLine("", 0);
        AstSenTree *st = new AstSenTree(fl_st, NULL);

        FileLine *fl = new FileLine("", 0);
        AstAlways *process = new AstAlways(fl, VAlwaysKwd::en::ALWAYS, st, NULL);
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
        return process;

    } else if (obj["cls"] == "wait") {
        
        Value::ConstArray on = obj["on"].GetArray();
        for (Value::ConstValueIterator m = on.Begin(); m != on.End(); ++m) {
            AstNode * ref = translateObject(m->GetObject());
            FileLine *fl2 = new FileLine("", 0);
            AstSenItem *si = new AstSenItem(fl2, AstEdgeType::ET_ANYEDGE, ref);
            if(ref) current_process->sensesp()->addSensesp(si);
        }

    } else if (obj["cls"] == "sigassign") {
        FileLine *fl = new FileLine("", 0);
        AstNode * lhsp = translateObject(obj["target"].GetObject());
        AstNode * rhsp = translateObject(obj["lhs"].GetObject());
        AstAssignDly * assign = new AstAssignDly(fl, lhsp, rhsp);
        return assign;

    } else if (obj["cls"] == "varassign") {
        FileLine *fl = new FileLine("", 0);
        AstNode * lhsp = translateObject(obj["target"].GetObject());
        AstNode * rhsp = translateObject(obj["lhs"].GetObject());
        AstAssign *assign = new AstAssign(fl, lhsp, rhsp);
        return assign;

    } else if (obj["cls"] == "ref") {
        FileLine *fl = new FileLine("", 0);
        AstVarRef *varrefp = new AstVarRef(fl, obj["name"].GetString(), false);
        return varrefp;
    
    } else if (obj["cls"] == "fcall") {
        FileLine *fl = new FileLine("", 0);
        return translateFcall(obj);

    } else if (obj["cls"] == "int") {
        FileLine *fl = new FileLine("", 0);
        return new AstConst(fl, AstConst::Unsized32(), obj["value"].GetUint());

    } else if (obj["cls"] == "sigdecl" or obj["cls"] == "vardecl") {
        VARRESET();
        VARDECL(VAR);
        FileLine *fl = new FileLine("", 0);
        VARDTYPE(translateType(obj["type"].GetObject()));
        return createVariable(fl, obj["name"].GetString(), NULL, NULL);

    } else {
        cout << "Unknown" << endl;
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
