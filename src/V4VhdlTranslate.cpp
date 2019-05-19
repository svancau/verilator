#include "V3Global.h"
#include "V3FileLine.h"
#include "V4VhdlTranslate.h"

V4VhdlTranslate::V4VhdlTranslate(V3ParseSym &symtable) : symt(symtable)
{

}

V4VhdlTranslate::~V4VhdlTranslate()
{

}

AstNodeDType *V4VhdlTranslate::translateType(Value::ConstObject item) {
    return nullptr;
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
    }
    return nullptr;
}

AstNode *V4VhdlTranslate::translateObject(Value::ConstObject item) {
    auto obj = item;
    if (obj["cls"] == "entity") {
        FileLine *fl = new FileLine("", 0);
        AstModule *mod = new AstModule(fl, obj["name"].GetString());

        // Handle Ports
        pinnum = 0;
        auto port_array = obj["port"].GetArray();
        for(Value::ConstValueIterator m = port_array.Begin(); m != port_array.End(); ++m) {
            auto port_obj = m->GetObject();
            FileLine *fl = new FileLine("", 0);
            AstPort *port = new AstPort(fl, pinnum++, port_obj["name"].GetString());
            mod->addStmtp(port);
            FileLine *fl2 = new FileLine("", 0);
            AstBasicDType *dtypep = new AstBasicDType(fl2, AstBasicDTypeKwd::BIT);
            cout << dtypep << endl;
            FileLine *fl3 = new FileLine("", 0);
            AstVar* var = new AstVar(fl3, AstVarType::PORT, port_obj["name"].GetString(), dtypep);
            mod->addStmtp(var);
        }
        v3Global.rootp()->addModulep(mod);
        symt.pushNew(mod);
        m_entities.insert(pair<string,AstNode*>(obj["name"].GetString(), mod));


    } else if (obj["cls"] == "architecture") {
        auto entity_mod = m_entities.find(obj["of"].GetString());
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
        AstNode * rhsp = translateObject(obj["lhs"].GetObject()); // FIX this
        AstAssign * assign = new AstAssign(fl, lhsp, rhsp);
        return assign;

    } else if (obj["cls"] == "ref") {
        FileLine *fl = new FileLine("", 0);
        AstVarRef *varrefp = new AstVarRef(fl, obj["name"].GetString(), false);
        return varrefp;
    
    } else if (obj["cls"] == "fcall") {
        FileLine *fl = new FileLine("", 0);
        return translateFcall(obj);

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
