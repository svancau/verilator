// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
// DESCRIPTION: Verilator: VHDL nodes conversion to Verilog
//
// Code available from: http://www.veripool.org/verilator
//
//*************************************************************************
//
// Copyright 2003-2019 by Wilson Snyder.  This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
//
// Verilator is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//*************************************************************************
// Vhdl Aggregates conversion TRANSFORMATIONS:
//      Top-down traversal
//          Aggregates conversion
//*************************************************************************

#include "config_build.h"
#include "verilatedos.h"

#include "V3Global.h"
#include "V4VhdlVisitor.h"
#include "V3Ast.h"

#include <algorithm>
#include <cstdarg>
#include <map>
#include <vector>

//######################################################################
// Link state, as a visitor of each AstNode

class VhdlAggregateVisitor : public AstNVisitor {
private:
    // NODE STATE

    // STATE

    // METHODS
    VL_DEBUG_FUNC;  // Declare debug()

    // VISITs
    // Result handing
    // TODO implement other aggregates
    virtual void visit(AstVHDAggregate *nodep) {
        AstAssign *assgn = (AstAssign*)(nodep->firstAbovep());
        AstVarRef *lhs = (AstVarRef*)(assgn->lhsp());
        AstVar *var = (AstVar*)(lhs->varp());
        AstPackArrayDType *dt;
        if (dt = dynamic_cast<AstPackArrayDType*>(var->childDTypep())) {
            AstRange * range = dt->rangep();        
            AstVHDAggregateItem *items = (AstVHDAggregateItem*)(nodep->aggritemsp());
            do {
                if (!items->lhsp())
                    break;
            } while (items = (AstVHDAggregateItem*)(items->nextp()));
            AstNode *value = items->rhsp()->unlinkFrBack();
            AstNode *rangel = range->leftp()->cloneTree(false);
            AstNode *ranger = range->rightp()->cloneTree(false);
            // Length = Left-Right+1 Only support (others -> 'x');
            AstNode *valLen = new AstAdd(nodep->fileline(), new AstConst(nodep->fileline(), 1), new AstSub(nodep->fileline(), rangel, ranger));
            nodep->replaceWith(new AstReplicate(items->fileline(), value, valLen));
        }
    }

    virtual void visit(AstNode* nodep) {
        // Default: Just iterate
        iterateChildren(nodep);
    }

public:
    // CONSTUCTORS
    VhdlAggregateVisitor(AstNode* nodep, bool start) {
        iterate(nodep);
    }
    virtual ~VhdlAggregateVisitor() {}
};

//######################################################################
// Link class functions

void V4VhdlAggregate::translateVhdlAggregates(AstNetlist* nodep) {
    UINFO(4,__FUNCTION__<<": "<<endl);
    {
        VhdlAggregateVisitor visitor(nodep, false);
    }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("vhdlaggregates", 0, v3Global.opt.dumpTreeLevel(__FILE__) >= 6);
}

