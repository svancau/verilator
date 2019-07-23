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
        AstNodeAssign *translatedAssign = nullptr;
        if (AstNodeAssign * assign = VN_CAST(nodep->backp(), NodeAssign)) {
            AstNode *target = assign->lhsp();
            AstVHDAggregateItem *itemp = VN_CAST(nodep->aggritemsp(), VHDAggregateItem);
            if (itemp) {
                do {
                    AstNode *lval = itemp->lhsp();
                    AstNode *base_val = itemp->rhsp();
                    AstNode *rval = nullptr;

                    int left_width = nodep->width();
                    if (base_val->width() == 1) { // When single bit slice
                        rval = new AstReplicate(base_val->fileline(), base_val->unlinkFrBack(),
                            new AstConst(base_val->fileline(), left_width));
                    }
                    else { // Otherwise keep as is
                        rval = base_val->unlinkFrBack();
                    }
                    if (!lval) {
                        lval = target->unlinkFrBack();
                    }
                    UINFO(9, "VHDL conversion r=" << rval << " " << base_val
                        <<  " l="<< lval << endl);
                    if(VN_IS(assign, Assign)) {
                        translatedAssign = new AstAssign(assign->fileline(), lval, rval);
                    }
                    else if(VN_IS(assign, AssignDly)) {
                        translatedAssign = new AstAssignDly(assign->fileline(), lval, rval);
                    }
                } while(itemp = VN_CAST(itemp->nextp(), VHDAggregateItem));
                assign->replaceWith(translatedAssign);
            }
        }
        else {
            v3fatalSrc("Unsupported: VHDL aggregate not under an assignment");
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

class VhdlForVisitor : public AstNVisitor {
private:
    // NODE STATE

    // STATE

    // METHODS
    VL_DEBUG_FUNC;  // Declare debug()

    // VISITs
    // Result handing
    virtual void visit(AstVHDLFor *nodep) {
        AstNodeVarRef *target = VN_CAST(nodep->varp()->unlinkFrBack(), NodeVarRef);
        AstRange *rangeL = VN_CAST(nodep->rangep()->unlinkFrBack(), Range);
        AstNodeAssign *init_stmt = new AstAssign(nodep->fileline(), target,
            rangeL->msbp()->cloneTree(true));
        AstNodeVarRef *target_rhs = target->cloneTree(true); target_rhs->lvalue(false);

        int increment = 0;
        AstNode *end_cond = nullptr;
        if(rangeL->littleEndian()) {
            increment = -1;
            end_cond = new AstGt(nodep->fileline(), target_rhs,
                rangeL->lsbp()->cloneTree(true));
        }
        else {
            increment = 1;
            end_cond = new AstLt(nodep->fileline(), target_rhs,
                rangeL->lsbp()->cloneTree(true));
        }

        AstNode *incr = new AstAssign(nodep->fileline(), target->cloneTree(true),
             new AstAdd(nodep->fileline(), target_rhs->cloneTree(true), new AstConst(nodep->fileline(), increment)));
        AstWhile *whl = new AstWhile(nodep->fileline(), end_cond, nodep->bodysp()->unlinkFrBack(), incr);
        init_stmt->addNextStmt(whl, nullptr);
        nodep->replaceWith(init_stmt);
    }

    virtual void visit(AstNode* nodep) {
        // Default: Just iterate
        iterateChildren(nodep);
    }

public:
    // CONSTUCTORS
    VhdlForVisitor(AstNode* nodep, bool start) {
        iterate(nodep);
    }
    virtual ~VhdlForVisitor() {}
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

void V4VhdlFor::translateVhdlFor(AstNetlist* nodep) {
    UINFO(4,__FUNCTION__<<": "<<endl);
    {
        VhdlForVisitor visitor(nodep, false);
    }  // Destruct before checking
    V3Global::dumpCheckGlobalTree("vhdlfor", 0, v3Global.opt.dumpTreeLevel(__FILE__) >= 6);
}
