/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver template for the LEMON parser generator.
 *
 * The "lemon" program processes an LALR(1) input grammar file, then uses
 * this template to construct a parser.  The "lemon" program inserts text
 * at each "%%" line.  Also, any "P-a-r-s-e" identifer prefix (without the
 * interstitial "-" characters) contained in this template is changed into
 * the value of the %name directive from the grammar.  Otherwise, the content
 * of this template is copied straight through into the generate parser
 * source file.
 *
 * The following is the concatenation of all %include directives from the
 * input grammar file:
 */
#include <stdio.h>
/************ Begin %include sections from the grammar ************************/
#line 48 "parse.y"

#include "sqliteInt.h"

/*
 * Disable all error recovery processing in the parser push-down
 * automaton.
 */
#define YYNOERRORRECOVERY 1

/*
 * Make yytestcase() the same as testcase()
 */
#define yytestcase(X) testcase(X)

/*
 * Indicate that sqlite3ParserFree() will never be called with a null
 * pointer.
 */
#define YYPARSEFREENEVERNULL 1

/*
 * Alternative datatype for the argument to the malloc() routine passed
 * into sqlite3ParserAlloc().  The default is size_t.
 */
#define YYMALLOCARGTYPE  u64

/*
 * An instance of this structure holds information about the
 * LIMIT clause of a SELECT statement.
 */
struct LimitVal {
	Expr *pLimit;		/* The LIMIT expression.  NULL if there is no limit */
	Expr *pOffset;		/* The OFFSET expression.  NULL if there is none */
};

/*
 * An instance of the following structure describes the event of a
 * TRIGGER.  "a" is the event type, one of TK_UPDATE, TK_INSERT,
 * TK_DELETE, or TK_INSTEAD.  If the event is of the form
 *
 *      UPDATE ON (a,b,c)
 *
 * Then the "b" IdList records the list "a,b,c".
 */
struct TrigEvent {
	int a;
	IdList *b;
};

/*
 * Disable lookaside memory allocation for objects that might be
 * shared across database connections.
 */
static void
disableLookaside(Parse * pParse)
{
	pParse->disableLookaside++;
	pParse->db->lookaside.bDisable++;
}

#line 397 "parse.y"

  /*
   * For a compound SELECT statement, make sure p->pPrior->pNext==p for
   * all elements in the list.  And make sure list length does not exceed
   * SQLITE_LIMIT_COMPOUND_SELECT.
   */
static void
parserDoubleLinkSelect(Parse * pParse, Select * p)
{
	if (p->pPrior) {
		Select *pNext = 0, *pLoop;
		int mxSelect, cnt = 0;
		for (pLoop = p; pLoop;
		     pNext = pLoop, pLoop = pLoop->pPrior, cnt++) {
			pLoop->pNext = pNext;
			pLoop->selFlags |= SF_Compound;
		}
		if ((p->selFlags & SF_MultiValue) == 0 &&
		    (mxSelect =
		     pParse->db->aLimit[SQLITE_LIMIT_COMPOUND_SELECT]) > 0
		    && cnt > mxSelect) {
			sqlite3ErrorMsg(pParse,
					"Too many UNION or EXCEPT or INTERSECT operations");
		}
	}
}

#line 834 "parse.y"

  /* This is a utility routine used to set the ExprSpan.zStart and
   * ExprSpan.zEnd values of pOut so that the span covers the complete
   * range of text beginning with pStart and going to the end of pEnd.
   */
static void
spanSet(ExprSpan * pOut, Token * pStart, Token * pEnd)
{
	pOut->zStart = pStart->z;
	pOut->zEnd = &pEnd->z[pEnd->n];
}

  /* Construct a new Expr object from a single identifier.  Use the
   * new Expr to populate pOut.  Set the span of pOut to be the identifier
   * that created the expression.
   */
static void
spanExpr(ExprSpan * pOut, Parse * pParse, int op, Token t)
{
	Expr *p = sqlite3DbMallocRawNN(pParse->db, sizeof(Expr) + t.n + 1);
	if (p) {
		memset(p, 0, sizeof(Expr));
		p->op = (u8) op;
		p->flags = EP_Leaf;
		p->iAgg = -1;
		p->u.zToken = (char *)&p[1];
		memcpy(p->u.zToken, t.z, t.n);
		p->u.zToken[t.n] = 0;
		if (sqlite3Isquote(p->u.zToken[0])) {
			if (p->u.zToken[0] == '"')
				p->flags |= EP_DblQuoted;
			sqlite3Dequote(p->u.zToken);
		}
#if SQLITE_MAX_EXPR_DEPTH>0
		p->nHeight = 1;
#endif
	}
	pOut->pExpr = p;
	pOut->zStart = t.z;
	pOut->zEnd = &t.z[t.n];
}

#line 951 "parse.y"

  /* This routine constructs a binary expression node out of two ExprSpan
   * objects and uses the result to populate a new ExprSpan object.
   */
static void
spanBinaryExpr(Parse * pParse,	/* The parsing context.  Errors accumulate here */
	       int op,		/* The binary operation */
	       ExprSpan * pLeft,	/* The left operand, and output */
	       ExprSpan * pRight	/* The right operand */
    )
{
	pLeft->pExpr = sqlite3PExpr(pParse, op, pLeft->pExpr, pRight->pExpr);
	pLeft->zEnd = pRight->zEnd;
}

  /* If doNot is true, then add a TK_NOT Expr-node wrapper around the
   * outside of *ppExpr.
   */
static void
exprNot(Parse * pParse, int doNot, ExprSpan * pSpan)
{
	if (doNot) {
		pSpan->pExpr = sqlite3PExpr(pParse, TK_NOT, pSpan->pExpr, 0);
	}
}

#line 1025 "parse.y"

  /* Construct an expression node for a unary postfix operator
   */
static void
spanUnaryPostfix(Parse * pParse,	/* Parsing context to record errors */
		 int op,	/* The operator */
		 ExprSpan * pOperand,	/* The operand, and output */
		 Token * pPostOp	/* The operand token for setting the span */
    )
{
	pOperand->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0);
	pOperand->zEnd = &pPostOp->z[pPostOp->n];
}

#line 1042 "parse.y"

  /* A routine to convert a binary TK_IS or TK_ISNOT expression into a
   * unary TK_ISNULL or TK_NOTNULL expression.
   */
static void
binaryToUnaryIfNull(Parse * pParse, Expr * pY, Expr * pA, int op)
{
	sqlite3 *db = pParse->db;
	if (pA && pY && pY->op == TK_NULL) {
		pA->op = (u8) op;
		sqlite3ExprDelete(db, pA->pRight);
		pA->pRight = 0;
	}
}

#line 1070 "parse.y"

  /* Construct an expression node for a unary prefix operator
   */
static void
spanUnaryPrefix(ExprSpan * pOut,	/* Write the new expression node here */
		Parse * pParse,	/* Parsing context to record errors */
		int op,		/* The operator */
		ExprSpan * pOperand,	/* The operand */
		Token * pPreOp	/* The operand token for setting the span */
    )
{
	pOut->zStart = pPreOp->z;
	pOut->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0);
	pOut->zEnd = pOperand->zEnd;
}

#line 1282 "parse.y"

  /* Add a single new term to an ExprList that is used to store a
   * list of identifiers.  Report an error if the ID list contains
   * a COLLATE clause or an ASC or DESC keyword, except ignore the
   * error while parsing a legacy schema.
   */
static ExprList *
parserAddExprIdListTerm(Parse * pParse,
			ExprList * pPrior,
			Token * pIdToken, int hasCollate, int sortOrder)
{
	ExprList *p = sqlite3ExprListAppend(pParse, pPrior, 0);
	if ((hasCollate || sortOrder != SQLITE_SO_UNDEFINED)
	    && pParse->db->init.busy == 0) {
		sqlite3ErrorMsg(pParse,
				"syntax error after column name \"%.*s\"",
				pIdToken->n, pIdToken->z);
	}
	sqlite3ExprListSetName(pParse, p, pIdToken, 1);
	return p;
}

#line 231 "parse.c"
/**************** End of %include directives **********************************/
/* These constants specify the various numeric values for terminal symbols
 * in a format understandable to "makeheaders".  This section is blank unless
 * "lemon" is run with the "-m" command-line option.
 **************** Begin makeheaders token definitions ************************
 */
/**************** End makeheaders token definitions ***************************/

/* The next sections is a series of control #defines.
 * various aspects of the generated parser.
 *    YYCODETYPE         is the data type used to store the integer codes
 *                       that represent terminal and non-terminal symbols.
 *                       "unsigned char" is used if there are fewer than
 *                       256 symbols.  Larger types otherwise.
 *    YYNOCODE           is a number of type YYCODETYPE that is not used for
 *                       any terminal or nonterminal symbol.
 *    YYFALLBACK         If defined, this indicates that one or more tokens
 *                       (also known as: "terminal symbols") have fall-back
 *                       values which should be used if the original symbol
 *                       would not parse.  This permits keywords to sometimes
 *                       be used as identifiers, for example.
 *    YYACTIONTYPE       is the data type used for "action codes" - numbers
 *                       that indicate what to do in response to the next
 *                       token.
 *    sqlite3ParserTOKENTYPE     is the data type used for minor type for terminal
 *                       symbols.  Background: A "minor type" is a semantic
 *                       value associated with a terminal or non-terminal
 *                       symbols.  For example, for an "ID" terminal symbol,
 *                       the minor type might be the name of the identifier.
 *                       Each non-terminal can have a different minor type.
 *                       Terminal symbols all have the same minor type, though.
 *                       This macros defines the minor type for terminal
 *                       symbols.
 *    YYMINORTYPE        is the data type used for all minor types.
 *                       This is typically a union of many types, one of
 *                       which is sqlite3ParserTOKENTYPE.  The entry in the union
 *                       for terminal symbols is called "yy0".
 *    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
 *                       zero the stack is dynamically sized using realloc()
 *    sqlite3ParserARG_SDECL     A static variable declaration for the %extra_argument
 *    sqlite3ParserARG_PDECL     A parameter declaration for the %extra_argument
 *    sqlite3ParserARG_STORE     Code to store %extra_argument into yypParser
 *    sqlite3ParserARG_FETCH     Code to extract %extra_argument from yypParser
 *    YYERRORSYMBOL      is the code number of the error symbol.  If not
 *                       defined, then do no error processing.
 *    YYNSTATE           the combined number of states.
 *    YYNRULE            the number of rules in the grammar
 *    YY_MAX_SHIFT       Maximum value for shift actions
 *    YY_MIN_SHIFTREDUCE Minimum value for shift-reduce actions
 *    YY_MAX_SHIFTREDUCE Maximum value for shift-reduce actions
 *    YY_MIN_REDUCE      Maximum value for reduce actions
 *    YY_ERROR_ACTION    The yy_action[] code for syntax error
 *    YY_ACCEPT_ACTION   The yy_action[] code for accept
 *    YY_NO_ACTION       The yy_action[] code for no-op
 */
#ifndef INTERFACE
#define INTERFACE 1
#endif
/************* Begin control #defines *****************************************/
#define YYCODETYPE unsigned char
#define YYNOCODE 244
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 93
#define sqlite3ParserTOKENTYPE Token
typedef union {
	int yyinit;
	sqlite3ParserTOKENTYPE yy0;
	struct {
		int value;
		int mask;
	} yy35;
	Expr *yy44;
	int yy58;
	With *yy91;
	Select *yy99;
	struct LimitVal yy112;
	ExprSpan yy190;
	TriggerStep *yy203;
	struct TrigEvent yy234;
	IdList *yy258;
	SrcList *yy367;
	ExprList *yy412;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYFALLBACK 1
#define YYNSTATE             438
#define YYNRULE              318
#define YY_MAX_SHIFT         437
#define YY_MIN_SHIFTREDUCE   642
#define YY_MAX_SHIFTREDUCE   959
#define YY_MIN_REDUCE        960
#define YY_MAX_REDUCE        1277
#define YY_ERROR_ACTION      1278
#define YY_ACCEPT_ACTION     1279
#define YY_NO_ACTION         1280
/************* End control #defines *******************************************/

/* Define the yytestcase() macro to be a no-op if is not already defined
 * otherwise.
 *
 * Applications can choose to define yytestcase() in the %include section
 * to a macro that can assist in verifying code coverage.  For production
 * code the yytestcase() macro should be turned off.  But it is useful
 * for testing.
 */
#ifndef yytestcase
#define yytestcase(X)
#endif

/* Next are the tables used to determine what action to take based on the
 * current state and lookahead token.  These tables are used to implement
 * functions that take a state number and lookahead value and return an
 * action integer.
 *
 * Suppose the action integer is N.  Then the action is determined as
 * follows
 *
 *   0 <= N <= YY_MAX_SHIFT             Shift N.  That is, push the lookahead
 *                                      token onto the stack and goto state N.
 *
 *   N between YY_MIN_SHIFTREDUCE       Shift to an arbitrary state then
 *     and YY_MAX_SHIFTREDUCE           reduce by rule N-YY_MIN_SHIFTREDUCE.
 *
 *   N between YY_MIN_REDUCE            Reduce by rule N-YY_MIN_REDUCE
 *     and YY_MAX_REDUCE
 *
 *   N == YY_ERROR_ACTION               A syntax error has occurred.
 *
 *   N == YY_ACCEPT_ACTION              The parser accepts its input.
 *
 *   N == YY_NO_ACTION                  No such action.  Denotes unused
 *                                      slots in the yy_action[] table.
 *
 * The action table is constructed as a single large table named yy_action[].
 * Given state S and lookahead X, the action is computed as either:
 *
 *    (A)   N = yy_action[ yy_shift_ofst[S] + X ]
 *    (B)   N = yy_default[S]
 *
 * The (A) formula is preferred.  The B formula is used instead if:
 *    (1)  The yy_shift_ofst[S]+X value is out of range, or
 *    (2)  yy_lookahead[yy_shift_ofst[S]+X] is not equal to X, or
 *    (3)  yy_shift_ofst[S] equal YY_SHIFT_USE_DFLT.
 * (Implementation note: YY_SHIFT_USE_DFLT is chosen so that
 * YY_SHIFT_USE_DFLT+X will be out of range for all possible lookaheads X.
 * Hence only tests (1) and (2) need to be evaluated.)
 *
 * The formulas above are for computing the action when the lookahead is
 * a terminal symbol.  If the lookahead is a non-terminal (as occurs after
 * a reduce action) then the yy_reduce_ofst[] array is used in place of
 * the yy_shift_ofst[] array and YY_REDUCE_USE_DFLT is used in place of
 * YY_SHIFT_USE_DFLT.
 *
 * The following are the tables generated in this section:
 *
 *  yy_action[]        A single table containing all actions.
 *  yy_lookahead[]     A table containing the lookahead for each entry in
 *                     yy_action.  Used to detect hash collisions.
 *  yy_shift_ofst[]    For each state, the offset into yy_action for
 *                     shifting terminals.
 *  yy_reduce_ofst[]   For each state, the offset into yy_action for
 *                     shifting non-terminals after a reduce.
 *  yy_default[]       Default action for each state.
 *
 ********** Begin parsing tables *********************************************
 */
#define YY_ACTTAB_COUNT (1472)
static const YYACTIONTYPE yy_action[] = {
	/*     0 */ 91, 92, 304, 82, 811, 811, 823, 826, 815, 815,
	/*    10 */ 89, 89, 90, 90, 90, 90, 329, 88, 88, 88,
	/*    20 */ 88, 87, 87, 86, 86, 86, 85, 329, 90, 90,
	/*    30 */ 90, 90, 83, 88, 88, 88, 88, 87, 87, 86,
	/*    40 */ 86, 86, 85, 329, 196, 786, 392, 939, 723, 723,
	/*    50 */ 91, 92, 304, 82, 811, 811, 823, 826, 815, 815,
	/*    60 */ 89, 89, 90, 90, 90, 90, 124, 88, 88, 88,
	/*    70 */ 88, 87, 87, 86, 86, 86, 85, 329, 87, 87,
	/*    80 */ 86, 86, 86, 85, 329, 90, 90, 90, 90, 939,
	/*    90 */ 88, 88, 88, 88, 87, 87, 86, 86, 86, 85,
	/*   100 */ 329, 436, 436, 760, 731, 643, 332, 234, 351, 122,
	/*   110 */ 85, 329, 761, 279, 718, 84, 81, 169, 91, 92,
	/*   120 */ 304, 82, 811, 811, 823, 826, 815, 815, 89, 89,
	/*   130 */ 90, 90, 90, 90, 669, 88, 88, 88, 88, 87,
	/*   140 */ 87, 86, 86, 86, 85, 329, 316, 22, 91, 92,
	/*   150 */ 304, 82, 811, 811, 823, 826, 815, 815, 89, 89,
	/*   160 */ 90, 90, 90, 90, 67, 88, 88, 88, 88, 87,
	/*   170 */ 87, 86, 86, 86, 85, 329, 88, 88, 88, 88,
	/*   180 */ 87, 87, 86, 86, 86, 85, 329, 759, 321, 920,
	/*   190 */ 920, 257, 93, 333, 1279, 437, 2, 91, 92, 304,
	/*   200 */ 82, 811, 811, 823, 826, 815, 815, 89, 89, 90,
	/*   210 */ 90, 90, 90, 318, 88, 88, 88, 88, 87, 87,
	/*   220 */ 86, 86, 86, 85, 329, 397, 86, 86, 86, 85,
	/*   230 */ 329, 921, 922, 84, 81, 169, 801, 744, 794, 672,
	/*   240 */ 677, 366, 788, 9, 9, 91, 92, 304, 82, 811,
	/*   250 */ 811, 823, 826, 815, 815, 89, 89, 90, 90, 90,
	/*   260 */ 90, 671, 88, 88, 88, 88, 87, 87, 86, 86,
	/*   270 */ 86, 85, 329, 793, 793, 795, 336, 204, 160, 267,
	/*   280 */ 386, 262, 385, 192, 233, 393, 140, 375, 670, 191,
	/*   290 */ 260, 920, 920, 278, 91, 92, 304, 82, 811, 811,
	/*   300 */ 823, 826, 815, 815, 89, 89, 90, 90, 90, 90,
	/*   310 */ 362, 88, 88, 88, 88, 87, 87, 86, 86, 86,
	/*   320 */ 85, 329, 431, 334, 187, 221, 148, 383, 380, 379,
	/*   330 */ 920, 920, 288, 921, 922, 758, 300, 802, 378, 371,
	/*   340 */ 48, 48, 368, 91, 92, 304, 82, 811, 811, 823,
	/*   350 */ 826, 815, 815, 89, 89, 90, 90, 90, 90, 931,
	/*   360 */ 88, 88, 88, 88, 87, 87, 86, 86, 86, 85,
	/*   370 */ 329, 697, 921, 922, 187, 409, 394, 383, 380, 379,
	/*   380 */ 366, 84, 81, 169, 308, 430, 787, 424, 378, 260,
	/*   390 */ 710, 710, 91, 92, 304, 82, 811, 811, 823, 826,
	/*   400 */ 815, 815, 89, 89, 90, 90, 90, 90, 698, 88,
	/*   410 */ 88, 88, 88, 87, 87, 86, 86, 86, 85, 329,
	/*   420 */ 91, 92, 304, 82, 811, 811, 823, 826, 815, 815,
	/*   430 */ 89, 89, 90, 90, 90, 90, 197, 88, 88, 88,
	/*   440 */ 88, 87, 87, 86, 86, 86, 85, 329, 91, 92,
	/*   450 */ 304, 82, 811, 811, 823, 826, 815, 815, 89, 89,
	/*   460 */ 90, 90, 90, 90, 248, 88, 88, 88, 88, 87,
	/*   470 */ 87, 86, 86, 86, 85, 329, 91, 92, 304, 82,
	/*   480 */ 811, 811, 823, 826, 815, 815, 89, 89, 90, 90,
	/*   490 */ 90, 90, 149, 88, 88, 88, 88, 87, 87, 86,
	/*   500 */ 86, 86, 85, 329, 708, 708, 91, 80, 304, 82,
	/*   510 */ 811, 811, 823, 826, 815, 815, 89, 89, 90, 90,
	/*   520 */ 90, 90, 70, 88, 88, 88, 88, 87, 87, 86,
	/*   530 */ 86, 86, 85, 329, 403, 92, 304, 82, 811, 811,
	/*   540 */ 823, 826, 815, 815, 89, 89, 90, 90, 90, 90,
	/*   550 */ 73, 88, 88, 88, 88, 87, 87, 86, 86, 86,
	/*   560 */ 85, 329, 304, 82, 811, 811, 823, 826, 815, 815,
	/*   570 */ 89, 89, 90, 90, 90, 90, 78, 88, 88, 88,
	/*   580 */ 88, 87, 87, 86, 86, 86, 85, 329, 78, 340,
	/*   590 */ 956, 297, 255, 141, 278, 75, 76, 167, 920, 920,
	/*   600 */ 431, 276, 77, 66, 340, 339, 431, 75, 76, 410,
	/*   610 */ 148, 431, 327, 326, 77, 426, 3, 1158, 48, 48,
	/*   620 */ 299, 330, 330, 782, 48, 48, 863, 426, 3, 10,
	/*   630 */ 10, 388, 429, 330, 330, 246, 254, 349, 920, 920,
	/*   640 */ 921, 922, 111, 315, 429, 250, 345, 237, 163, 846,
	/*   650 */ 415, 894, 744, 409, 411, 682, 233, 393, 124, 409,
	/*   660 */ 399, 801, 415, 433, 432, 340, 431, 788, 663, 309,
	/*   670 */ 124, 782, 20, 801, 927, 433, 432, 124, 431, 788,
	/*   680 */ 921, 922, 679, 946, 48, 48, 431, 390, 420, 279,
	/*   690 */ 944, 236, 945, 250, 357, 249, 30, 30, 793, 793,
	/*   700 */ 795, 796, 18, 19, 48, 48, 78, 162, 161, 744,
	/*   710 */ 793, 793, 795, 796, 18, 947, 728, 947, 78, 322,
	/*   720 */ 111, 727, 325, 431, 64, 75, 76, 732, 95, 920,
	/*   730 */ 920, 391, 77, 673, 673, 216, 216, 75, 76, 409,
	/*   740 */ 408, 48, 48, 782, 77, 426, 3, 396, 219, 124,
	/*   750 */ 893, 330, 330, 885, 885, 296, 958, 426, 3, 145,
	/*   760 */ 343, 124, 429, 330, 330, 250, 357, 249, 886, 886,
	/*   770 */ 366, 921, 922, 801, 429, 794, 409, 389, 891, 788,
	/*   780 */ 415, 779, 338, 882, 887, 887, 414, 711, 1222, 1222,
	/*   790 */ 24, 801, 415, 433, 432, 920, 920, 788, 712, 360,
	/*   800 */ 733, 959, 1, 801, 760, 433, 432, 920, 920, 788,
	/*   810 */ 793, 793, 795, 761, 111, 431, 111, 920, 920, 428,
	/*   820 */ 428, 428, 920, 920, 920, 920, 860, 431, 793, 793,
	/*   830 */ 795, 796, 18, 48, 48, 738, 68, 921, 922, 358,
	/*   840 */ 793, 793, 795, 796, 18, 47, 47, 427, 309, 921,
	/*   850 */ 922, 431, 23, 431, 223, 75, 76, 356, 1248, 921,
	/*   860 */ 922, 700, 77, 218, 921, 922, 921, 922, 404, 10,
	/*   870 */ 10, 10, 10, 327, 326, 426, 3, 812, 812, 824,
	/*   880 */ 827, 330, 330, 317, 398, 224, 747, 431, 341, 148,
	/*   890 */ 295, 746, 429, 293, 292, 291, 207, 289, 277, 303,
	/*   900 */ 656, 882, 744, 856, 858, 10, 10, 903, 1272, 213,
	/*   910 */ 415, 1272, 745, 171, 328, 328, 328, 111, 225, 406,
	/*   920 */ 225, 801, 400, 433, 432, 903, 1273, 788, 691, 1273,
	/*   930 */ 108, 938, 193, 166, 111, 667, 175, 242, 5, 360,
	/*   940 */ 196, 220, 384, 939, 266, 124, 173, 84, 81, 169,
	/*   950 */ 692, 431, 744, 901, 431, 265, 168, 358, 793, 793,
	/*   960 */ 795, 796, 18, 311, 856, 431, 816, 305, 401, 34,
	/*   970 */ 34, 901, 35, 35, 319, 367, 947, 667, 947, 431,
	/*   980 */ 239, 241, 745, 36, 36, 939, 431, 744, 431, 744,
	/*   990 */ 431, 888, 431, 196, 337, 688, 939, 37, 37, 431,
	/*  1000 */ 158, 431, 786, 222, 38, 38, 26, 26, 27, 27,
	/*  1010 */ 29, 29, 216, 216, 124, 431, 228, 39, 39, 40,
	/*  1020 */ 40, 431, 342, 431, 396, 431, 346, 431, 717, 431,
	/*  1030 */ 854, 240, 431, 41, 41, 431, 364, 431, 939, 11,
	/*  1040 */ 11, 42, 42, 97, 97, 43, 43, 44, 44, 431,
	/*  1050 */ 31, 31, 431, 45, 45, 46, 46, 431, 744, 431,
	/*  1060 */ 786, 431, 869, 869, 352, 312, 431, 32, 32, 431,
	/*  1070 */ 113, 113, 431, 191, 431, 114, 114, 115, 115, 52,
	/*  1080 */ 52, 431, 365, 431, 33, 33, 431, 98, 98, 431,
	/*  1090 */ 49, 49, 99, 99, 431, 744, 431, 713, 431, 100,
	/*  1100 */ 100, 96, 96, 431, 112, 112, 431, 110, 110, 431,
	/*  1110 */ 786, 431, 104, 104, 103, 103, 101, 101, 431, 745,
	/*  1120 */ 431, 102, 102, 313, 51, 51, 899, 53, 53, 50,
	/*  1130 */ 50, 866, 705, 684, 245, 865, 25, 25, 28, 28,
	/*  1140 */ 786, 705, 296, 902, 190, 189, 188, 880, 924, 720,
	/*  1150 */ 350, 716, 660, 74, 417, 72, 310, 125, 217, 421,
	/*  1160 */ 396, 425, 728, 168, 232, 680, 898, 727, 308, 298,
	/*  1170 */ 215, 166, 853, 314, 54, 109, 348, 153, 784, 359,
	/*  1180 */ 111, 195, 195, 361, 111, 111, 195, 111, 959, 745,
	/*  1190 */ 924, 797, 111, 251, 111, 376, 66, 258, 200, 211,
	/*  1200 */ 66, 690, 689, 323, 877, 725, 754, 680, 69, 195,
	/*  1210 */ 849, 876, 687, 200, 853, 862, 861, 862, 861, 665,
	/*  1220 */ 369, 370, 107, 686, 253, 655, 256, 701, 685, 261,
	/*  1230 */ 1238, 752, 785, 797, 355, 413, 734, 412, 280, 281,
	/*  1240 */ 792, 668, 662, 210, 269, 653, 652, 654, 914, 271,
	/*  1250 */ 273, 150, 774, 7, 247, 344, 363, 879, 159, 416,
	/*  1260 */ 238, 275, 953, 164, 286, 917, 127, 138, 147, 121,
	/*  1270 */ 64, 381, 264, 347, 851, 850, 684, 771, 374, 178,
	/*  1280 */ 55, 354, 179, 151, 146, 864, 183, 244, 387, 184,
	/*  1290 */ 129, 185, 372, 301, 131, 132, 133, 320, 134, 142,
	/*  1300 */ 781, 695, 704, 302, 682, 703, 63, 402, 702, 676,
	/*  1310 */ 881, 71, 675, 6, 845, 324, 263, 94, 674, 929,
	/*  1320 */ 694, 65, 407, 205, 405, 659, 21, 434, 915, 206,
	/*  1330 */ 208, 209, 419, 742, 743, 435, 268, 423, 648, 650,
	/*  1340 */ 649, 646, 116, 294, 645, 117, 118, 331, 157, 170,
	/*  1350 */ 106, 226, 235, 335, 105, 172, 859, 857, 174, 780,
	/*  1360 */ 128, 270, 283, 741, 272, 282, 740, 724, 274, 284,
	/*  1370 */ 285, 119, 831, 130, 176, 714, 177, 243, 867, 195,
	/*  1380 */ 229, 135, 949, 353, 136, 875, 230, 231, 137, 139,
	/*  1390 */ 56, 57, 58, 59, 120, 878, 180, 181, 874, 8,
	/*  1400 */ 12, 152, 182, 658, 252, 373, 186, 143, 265, 377,
	/*  1410 */ 60, 306, 259, 13, 126, 307, 14, 693, 382, 61,
	/*  1420 */ 227, 123, 62, 722, 395, 165, 800, 799, 829, 15,
	/*  1430 */ 642, 726, 4, 748, 212, 214, 144, 202, 203, 1240,
	/*  1440 */ 1239, 753, 194, 69, 66, 198, 833, 844, 830, 828,
	/*  1450 */ 884, 16, 17, 883, 199, 418, 1227, 907, 154, 290,
	/*  1460 */ 908, 201, 155, 422, 832, 156, 962, 798, 666, 79,
	/*  1470 */ 962, 287,
};

static const YYCODETYPE yy_lookahead[] = {
	/*     0 */ 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/*    10 */ 15, 16, 17, 18, 19, 20, 32, 22, 23, 24,
	/*    20 */ 25, 26, 27, 28, 29, 30, 31, 32, 17, 18,
	/*    30 */ 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
	/*    40 */ 29, 30, 31, 32, 49, 148, 112, 52, 114, 115,
	/*    50 */ 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/*    60 */ 15, 16, 17, 18, 19, 20, 89, 22, 23, 24,
	/*    70 */ 25, 26, 27, 28, 29, 30, 31, 32, 26, 27,
	/*    80 */ 28, 29, 30, 31, 32, 17, 18, 19, 20, 94,
	/*    90 */ 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	/*   100 */ 32, 144, 145, 58, 204, 1, 2, 150, 211, 152,
	/*   110 */ 31, 32, 67, 148, 157, 215, 216, 217, 5, 6,
	/*   120 */ 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	/*   130 */ 17, 18, 19, 20, 166, 22, 23, 24, 25, 26,
	/*   140 */ 27, 28, 29, 30, 31, 32, 181, 190, 5, 6,
	/*   150 */ 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	/*   160 */ 17, 18, 19, 20, 51, 22, 23, 24, 25, 26,
	/*   170 */ 27, 28, 29, 30, 31, 32, 22, 23, 24, 25,
	/*   180 */ 26, 27, 28, 29, 30, 31, 32, 169, 7, 52,
	/*   190 */ 53, 48, 79, 236, 141, 142, 143, 5, 6, 7,
	/*   200 */ 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
	/*   210 */ 18, 19, 20, 32, 22, 23, 24, 25, 26, 27,
	/*   220 */ 28, 29, 30, 31, 32, 148, 28, 29, 30, 31,
	/*   230 */ 32, 94, 95, 215, 216, 217, 92, 148, 94, 166,
	/*   240 */ 48, 148, 98, 166, 167, 5, 6, 7, 8, 9,
	/*   250 */ 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	/*   260 */ 20, 166, 22, 23, 24, 25, 26, 27, 28, 29,
	/*   270 */ 30, 31, 32, 129, 130, 131, 187, 96, 97, 98,
	/*   280 */ 99, 100, 101, 102, 116, 117, 47, 7, 48, 9,
	/*   290 */ 109, 52, 53, 148, 5, 6, 7, 8, 9, 10,
	/*   300 */ 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	/*   310 */ 148, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	/*   320 */ 31, 32, 148, 234, 96, 232, 148, 99, 100, 101,
	/*   330 */ 52, 53, 154, 94, 95, 169, 158, 48, 110, 222,
	/*   340 */ 166, 167, 225, 5, 6, 7, 8, 9, 10, 11,
	/*   350 */ 12, 13, 14, 15, 16, 17, 18, 19, 20, 179,
	/*   360 */ 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	/*   370 */ 32, 36, 94, 95, 96, 201, 202, 99, 100, 101,
	/*   380 */ 148, 215, 216, 217, 104, 148, 48, 242, 110, 109,
	/*   390 */ 184, 185, 5, 6, 7, 8, 9, 10, 11, 12,
	/*   400 */ 13, 14, 15, 16, 17, 18, 19, 20, 73, 22,
	/*   410 */ 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
	/*   420 */ 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/*   430 */ 15, 16, 17, 18, 19, 20, 148, 22, 23, 24,
	/*   440 */ 25, 26, 27, 28, 29, 30, 31, 32, 5, 6,
	/*   450 */ 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	/*   460 */ 17, 18, 19, 20, 232, 22, 23, 24, 25, 26,
	/*   470 */ 27, 28, 29, 30, 31, 32, 5, 6, 7, 8,
	/*   480 */ 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	/*   490 */ 19, 20, 49, 22, 23, 24, 25, 26, 27, 28,
	/*   500 */ 29, 30, 31, 32, 184, 185, 5, 6, 7, 8,
	/*   510 */ 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
	/*   520 */ 19, 20, 135, 22, 23, 24, 25, 26, 27, 28,
	/*   530 */ 29, 30, 31, 32, 148, 6, 7, 8, 9, 10,
	/*   540 */ 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	/*   550 */ 135, 22, 23, 24, 25, 26, 27, 28, 29, 30,
	/*   560 */ 31, 32, 7, 8, 9, 10, 11, 12, 13, 14,
	/*   570 */ 15, 16, 17, 18, 19, 20, 7, 22, 23, 24,
	/*   580 */ 25, 26, 27, 28, 29, 30, 31, 32, 7, 148,
	/*   590 */ 239, 240, 43, 47, 148, 26, 27, 148, 52, 53,
	/*   600 */ 148, 148, 33, 51, 163, 164, 148, 26, 27, 157,
	/*   610 */ 148, 148, 26, 27, 33, 46, 47, 48, 166, 167,
	/*   620 */ 158, 52, 53, 83, 166, 167, 38, 46, 47, 166,
	/*   630 */ 167, 28, 63, 52, 53, 86, 87, 88, 52, 53,
	/*   640 */ 94, 95, 190, 180, 63, 105, 106, 107, 51, 100,
	/*   650 */ 81, 148, 148, 201, 202, 103, 116, 117, 89, 201,
	/*   660 */ 202, 92, 81, 94, 95, 224, 148, 98, 160, 161,
	/*   670 */ 89, 83, 16, 92, 165, 94, 95, 89, 148, 98,
	/*   680 */ 94, 95, 173, 97, 166, 167, 148, 157, 242, 148,
	/*   690 */ 104, 187, 106, 105, 106, 107, 166, 167, 129, 130,
	/*   700 */ 131, 132, 133, 47, 166, 167, 7, 26, 27, 148,
	/*   710 */ 129, 130, 131, 132, 133, 129, 113, 131, 7, 201,
	/*   720 */ 190, 118, 181, 148, 127, 26, 27, 28, 47, 52,
	/*   730 */ 53, 201, 33, 52, 53, 188, 189, 26, 27, 201,
	/*   740 */ 202, 166, 167, 83, 33, 46, 47, 200, 187, 89,
	/*   750 */ 148, 52, 53, 39, 39, 47, 48, 46, 47, 82,
	/*   760 */ 213, 89, 63, 52, 53, 105, 106, 107, 54, 54,
	/*   770 */ 148, 94, 95, 92, 63, 94, 201, 202, 148, 98,
	/*   780 */ 81, 157, 148, 157, 70, 70, 72, 72, 116, 117,
	/*   790 */ 47, 92, 81, 94, 95, 52, 53, 98, 83, 148,
	/*   800 */ 28, 93, 47, 92, 58, 94, 95, 52, 53, 98,
	/*   810 */ 129, 130, 131, 67, 190, 148, 190, 52, 53, 162,
	/*   820 */ 163, 164, 52, 53, 52, 53, 148, 148, 129, 130,
	/*   830 */ 131, 132, 133, 166, 167, 207, 7, 94, 95, 213,
	/*   840 */ 129, 130, 131, 132, 133, 166, 167, 160, 161, 94,
	/*   850 */ 95, 148, 226, 148, 232, 26, 27, 231, 48, 94,
	/*   860 */ 95, 51, 33, 212, 94, 95, 94, 95, 201, 166,
	/*   870 */ 167, 166, 167, 26, 27, 46, 47, 9, 10, 11,
	/*   880 */ 12, 52, 53, 180, 157, 180, 121, 148, 148, 148,
	/*   890 */ 34, 121, 63, 37, 38, 39, 40, 41, 219, 158,
	/*   900 */ 44, 157, 148, 163, 164, 166, 167, 47, 48, 48,
	/*   910 */ 81, 51, 51, 57, 162, 163, 164, 190, 177, 180,
	/*   920 */ 179, 92, 7, 94, 95, 47, 48, 98, 62, 51,
	/*   930 */ 47, 51, 205, 206, 190, 52, 80, 43, 47, 148,
	/*   940 */ 49, 187, 76, 52, 98, 89, 90, 215, 216, 217,
	/*   950 */ 84, 148, 148, 93, 148, 109, 95, 213, 129, 130,
	/*   960 */ 131, 132, 133, 148, 224, 148, 98, 111, 53, 166,
	/*   970 */ 167, 93, 166, 167, 108, 231, 129, 94, 131, 148,
	/*   980 */ 86, 87, 121, 166, 167, 94, 148, 148, 148, 148,
	/*   990 */ 148, 187, 148, 49, 138, 175, 52, 166, 167, 148,
	/*  1000 */ 120, 148, 148, 212, 166, 167, 166, 167, 166, 167,
	/*  1010 */ 166, 167, 188, 189, 89, 148, 204, 166, 167, 166,
	/*  1020 */ 167, 148, 97, 148, 200, 148, 187, 148, 187, 148,
	/*  1030 */ 148, 137, 148, 166, 167, 148, 7, 148, 94, 166,
	/*  1040 */ 167, 166, 167, 166, 167, 166, 167, 166, 167, 148,
	/*  1050 */ 166, 167, 148, 166, 167, 166, 167, 148, 148, 148,
	/*  1060 */ 148, 148, 105, 106, 107, 211, 148, 166, 167, 148,
	/*  1070 */ 166, 167, 148, 9, 148, 166, 167, 166, 167, 166,
	/*  1080 */ 167, 148, 53, 148, 166, 167, 148, 166, 167, 148,
	/*  1090 */ 166, 167, 166, 167, 148, 148, 148, 187, 148, 166,
	/*  1100 */ 167, 166, 167, 148, 166, 167, 148, 166, 167, 148,
	/*  1110 */ 148, 148, 166, 167, 166, 167, 166, 167, 148, 51,
	/*  1120 */ 148, 166, 167, 211, 166, 167, 148, 166, 167, 166,
	/*  1130 */ 167, 56, 173, 174, 187, 60, 166, 167, 166, 167,
	/*  1140 */ 148, 182, 47, 48, 105, 106, 107, 157, 52, 189,
	/*  1150 */ 75, 157, 157, 134, 157, 136, 237, 238, 47, 157,
	/*  1160 */ 200, 157, 113, 95, 193, 52, 48, 118, 104, 51,
	/*  1170 */ 205, 206, 52, 211, 203, 47, 148, 49, 48, 48,
	/*  1180 */ 190, 51, 51, 48, 190, 190, 51, 190, 93, 121,
	/*  1190 */ 94, 52, 190, 48, 190, 48, 51, 48, 51, 148,
	/*  1200 */ 51, 97, 98, 211, 148, 48, 48, 94, 51, 51,
	/*  1210 */ 48, 148, 175, 51, 94, 129, 129, 131, 131, 48,
	/*  1220 */ 148, 148, 51, 175, 148, 148, 148, 148, 148, 148,
	/*  1230 */ 119, 148, 148, 94, 228, 185, 148, 148, 148, 148,
	/*  1240 */ 148, 148, 148, 227, 204, 148, 148, 148, 148, 204,
	/*  1250 */ 204, 191, 195, 192, 233, 208, 233, 195, 192, 221,
	/*  1260 */ 208, 208, 64, 178, 194, 151, 235, 47, 214, 5,
	/*  1270 */ 127, 170, 169, 45, 169, 169, 174, 195, 45, 153,
	/*  1280 */ 134, 71, 153, 214, 47, 230, 153, 229, 104, 153,
	/*  1290 */ 183, 153, 171, 171, 186, 186, 186, 74, 186, 183,
	/*  1300 */ 183, 176, 168, 171, 103, 168, 104, 122, 168, 168,
	/*  1310 */ 195, 134, 170, 47, 195, 32, 168, 126, 168, 168,
	/*  1320 */ 176, 125, 123, 50, 124, 156, 51, 155, 40, 149,
	/*  1330 */ 149, 35, 171, 210, 210, 147, 209, 171, 36, 147,
	/*  1340 */ 147, 147, 159, 146, 4, 159, 159, 3, 47, 42,
	/*  1350 */ 172, 172, 139, 91, 43, 104, 48, 48, 119, 117,
	/*  1360 */ 128, 209, 197, 210, 209, 198, 210, 199, 209, 196,
	/*  1370 */ 195, 108, 218, 120, 104, 46, 122, 43, 78, 51,
	/*  1380 */ 220, 78, 85, 69, 104, 1, 223, 223, 120, 128,
	/*  1390 */ 16, 16, 16, 16, 108, 53, 61, 119, 1, 34,
	/*  1400 */ 47, 49, 104, 46, 137, 7, 102, 47, 109, 77,
	/*  1410 */ 47, 241, 48, 47, 238, 241, 47, 55, 77, 47,
	/*  1420 */ 77, 65, 51, 113, 51, 119, 48, 48, 48, 47,
	/*  1430 */ 1, 48, 47, 121, 48, 48, 47, 119, 119, 119,
	/*  1440 */ 119, 53, 61, 51, 51, 51, 38, 48, 48, 48,
	/*  1450 */ 48, 61, 61, 48, 47, 49, 0, 48, 47, 42,
	/*  1460 */ 48, 51, 47, 49, 48, 47, 243, 48, 48, 47,
	/*  1470 */ 243, 48,
};

#define YY_SHIFT_USE_DFLT (1472)
#define YY_SHIFT_COUNT    (437)
#define YY_SHIFT_MIN      (-66)
#define YY_SHIFT_MAX      (1456)
static const short yy_shift_ofst[] = {
	/*     0 */ 104, 569, 856, 581, 711, 711, 711, 711, 660, -5,
	/*    10 */ 45, 45, 711, 711, 711, 711, 711, 711, 711, 586,
	/*    20 */ 586, 278, 540, 588, 672, 113, 143, 192, 240, 289,
	/*    30 */ 338, 387, 415, 443, 471, 471, 471, 471, 471, 471,
	/*    40 */ 471, 471, 471, 471, 471, 471, 471, 471, 471, 501,
	/*    50 */ 471, 529, 555, 555, 699, 711, 711, 711, 711, 711,
	/*    60 */ 711, 711, 711, 711, 711, 711, 711, 711, 711, 711,
	/*    70 */ 711, 711, 711, 711, 711, 711, 711, 711, 711, 711,
	/*    80 */ 711, 711, 829, 711, 711, 711, 711, 711, 711, 711,
	/*    90 */ 711, 711, 711, 711, 711, 711, 11, 68, 68, 68,
	/*   100 */ 68, 68, 154, 52, 198, 137, 280, 847, 847, 137,
	/*   110 */ 79, 168, -16, 1472, 1472, 1472, 181, 181, 181, 239,
	/*   120 */ 239, 715, 549, 715, 677, 860, 878, 137, 137, 137,
	/*   130 */ 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,
	/*   140 */ 137, 137, 137, 137, 137, 137, 137, 925, 1096, 1096,
	/*   150 */ 168, -23, -23, -23, -23, -23, -23, 1472, 1472, 1472,
	/*   160 */ 681, 144, 144, 546, 228, 772, 743, 765, 770, 755,
	/*   170 */ 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,
	/*   180 */ 137, 137, 137, 137, 137, 137, 137, 137, 866, 866,
	/*   190 */ 866, 137, 137, 861, 137, 137, 137, 891, 137, 714,
	/*   200 */ 137, 137, 137, 137, 137, 137, 137, 137, 137, 137,
	/*   210 */ 957, 1075, 944, 944, 944, 1068, -66, 603, 597, 915,
	/*   220 */ 915, 1029, 597, 1029, 552, 810, 1064, 746, 915, 1019,
	/*   230 */ 746, 746, 880, 1049, 1128, 1198, 1220, 1264, 1143, 1228,
	/*   240 */ 1228, 1228, 1228, 1233, 1146, 1210, 1233, 1143, 1220, 1264,
	/*   250 */ 1264, 1143, 1233, 1237, 1233, 1233, 1237, 1184, 1184, 1184,
	/*   260 */ 1223, 1237, 1184, 1201, 1184, 1223, 1184, 1184, 1185, 1202,
	/*   270 */ 1185, 1202, 1185, 1202, 1185, 1202, 1266, 1177, 1237, 1283,
	/*   280 */ 1283, 1237, 1191, 1199, 1196, 1200, 1143, 1273, 1275, 1288,
	/*   290 */ 1288, 1296, 1296, 1296, 1296, 1302, 1472, 1472, 1472, 1472,
	/*   300 */ 1472, 1472, 1472, 1472, 868, 894, 708, 1095, 1039, 883,
	/*   310 */ 1118, 656, 1130, 1131, 1135, 1145, 1147, 1149, 1113, 1104,
	/*   320 */ 335, 846, 1157, 1158, 1120, 1162, 1086, 1087, 1171, 1139,
	/*   330 */ 1111, 1340, 1344, 1301, 1213, 1307, 1262, 1311, 1251, 1308,
	/*   340 */ 1309, 1239, 1242, 1232, 1263, 1253, 1270, 1329, 1254, 1334,
	/*   350 */ 1300, 1328, 1303, 1297, 1314, 1280, 1384, 1268, 1261, 1374,
	/*   360 */ 1375, 1376, 1377, 1286, 1342, 1335, 1278, 1397, 1365, 1353,
	/*   370 */ 1298, 1267, 1352, 1357, 1398, 1299, 1304, 1360, 1332, 1363,
	/*   380 */ 1366, 1364, 1369, 1341, 1362, 1372, 1343, 1356, 1378, 1379,
	/*   390 */ 1380, 1371, 1310, 1382, 1383, 1385, 1373, 1306, 1386, 1387,
	/*   400 */ 1388, 1381, 1389, 1312, 1392, 1390, 1393, 1391, 1399, 1392,
	/*   410 */ 1400, 1401, 1402, 1394, 1405, 1407, 1408, 1409, 1411, 1406,
	/*   420 */ 1410, 1412, 1415, 1414, 1410, 1416, 1418, 1419, 1420, 1422,
	/*   430 */ 1318, 1319, 1320, 1321, 1423, 1417, 1429, 1456,
};

#define YY_REDUCE_USE_DFLT (-104)
#define YY_REDUCE_COUNT (303)
#define YY_REDUCE_MIN   (-103)
#define YY_REDUCE_MAX   (1197)
static const short yy_reduce_ofst[] = {
	/*     0 */ 53, 452, -43, 530, 174, 458, 538, 575, 626, -100,
	/*    10 */ 18, 166, 463, 703, 705, 518, 667, 739, 679, 441,
	/*    20 */ 740, 741, 547, 744, 727, 732, 732, 732, 732, 732,
	/*    30 */ 732, 732, 732, 732, 732, 732, 732, 732, 732, 732,
	/*    40 */ 732, 732, 732, 732, 732, 732, 732, 732, 732, 732,
	/*    50 */ 732, 732, 732, 732, 77, 803, 806, 817, 831, 838,
	/*    60 */ 840, 842, 844, 851, 853, 867, 873, 875, 877, 879,
	/*    70 */ 881, 884, 887, 889, 901, 904, 909, 911, 913, 918,
	/*    80 */ 921, 924, 926, 933, 935, 938, 941, 946, 948, 950,
	/*    90 */ 955, 958, 961, 963, 970, 972, 732, 732, 732, 732,
	/*   100 */ 732, 732, 732, 732, 732, 89, 959, 657, 752, 178,
	/*   110 */ 732, 824, 732, 732, 732, 732, 509, 509, 509, 651,
	/*   120 */ 791, 206, 117, 320, 145, 351, 351, 462, 504, 561,
	/*   130 */ 754, 804, 839, 841, 910, -103, 947, 93, 854, 232,
	/*   140 */ 912, 962, 622, -35, 992, 446, 541, 624, 508, 687,
	/*   150 */ 960, 990, 994, 995, 997, 1002, 1004, 919, 965, 971,
	/*   160 */ -32, 73, 95, 162, 180, 237, 288, 386, 449, 453,
	/*   170 */ 503, 602, 630, 634, 678, 815, 882, 978, 1028, 1051,
	/*   180 */ 1056, 1063, 1072, 1073, 1076, 1077, 1078, 1079, 820, 1037,
	/*   190 */ 1048, 1080, 1081, 628, 1083, 1084, 1088, 812, 1089, 1050,
	/*   200 */ 1090, 1091, 1092, 237, 1093, 1094, 1097, 1098, 1099, 1100,
	/*   210 */ 1006, 1016, 1040, 1045, 1046, 628, 1060, 1061, 1057, 1047,
	/*   220 */ 1052, 1021, 1062, 1023, 1101, 1085, 1102, 1103, 1053, 1038,
	/*   230 */ 1105, 1106, 1070, 1066, 1114, 1031, 1054, 1107, 1082, 1108,
	/*   240 */ 1109, 1110, 1112, 1126, 1055, 1058, 1129, 1115, 1069, 1116,
	/*   250 */ 1117, 1119, 1133, 1121, 1136, 1138, 1122, 1134, 1137, 1140,
	/*   260 */ 1125, 1132, 1141, 1142, 1148, 1144, 1150, 1151, 1123, 1127,
	/*   270 */ 1124, 1152, 1153, 1155, 1156, 1159, 1154, 1160, 1161, 1163,
	/*   280 */ 1164, 1166, 1168, 1167, 1165, 1173, 1175, 1169, 1172, 1180,
	/*   290 */ 1181, 1188, 1192, 1193, 1194, 1197, 1170, 1174, 1176, 1183,
	/*   300 */ 1186, 1178, 1179, 1187,
};

static const YYACTIONTYPE yy_default[] = {
	/*     0 */ 1228, 1222, 1222, 1222, 1158, 1158, 1158, 1158, 1222, 1053,
	/*    10 */ 1080, 1080, 1278, 1278, 1278, 1278, 1278, 1278, 1157, 1278,
	/*    20 */ 1278, 1278, 1278, 1222, 1057, 1086, 1278, 1278, 1278, 1159,
	/*    30 */ 1160, 1278, 1278, 1278, 1191, 1096, 1095, 1094, 1093, 1067,
	/*    40 */ 1091, 1084, 1088, 1159, 1153, 1154, 1152, 1156, 1160, 1278,
	/*    50 */ 1087, 1122, 1137, 1121, 1278, 1278, 1278, 1278, 1278, 1278,
	/*    60 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*    70 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*    80 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*    90 */ 1278, 1278, 1278, 1278, 1278, 1278, 1131, 1136, 1143, 1135,
	/*   100 */ 1132, 1124, 1123, 1125, 1126, 1278, 1024, 1278, 1278, 1278,
	/*   110 */ 1127, 1278, 1128, 1140, 1139, 1138, 1213, 1237, 1236, 1278,
	/*   120 */ 1278, 1278, 1165, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   130 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   140 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1222, 982, 982,
	/*   150 */ 1278, 1222, 1222, 1222, 1222, 1222, 1222, 1218, 1057, 1048,
	/*   160 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   170 */ 1278, 1210, 1278, 1207, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   180 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   190 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1053, 1278, 1278,
	/*   200 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1231,
	/*   210 */ 1278, 1186, 1053, 1053, 1053, 1055, 1037, 1047, 1090, 1069,
	/*   220 */ 1069, 1269, 1090, 1269, 999, 1251, 996, 1080, 1069, 1155,
	/*   230 */ 1080, 1080, 1054, 1047, 1278, 1270, 1101, 1027, 1090, 1033,
	/*   240 */ 1033, 1033, 1033, 975, 1190, 1266, 975, 1090, 1101, 1027,
	/*   250 */ 1027, 1090, 975, 1166, 975, 975, 1166, 1025, 1025, 1025,
	/*   260 */ 1014, 1166, 1025, 999, 1025, 1014, 1025, 1025, 1073, 1068,
	/*   270 */ 1073, 1068, 1073, 1068, 1073, 1068, 1161, 1278, 1166, 1170,
	/*   280 */ 1170, 1166, 1085, 1074, 1083, 1081, 1090, 979, 1017, 1234,
	/*   290 */ 1234, 1230, 1230, 1230, 1230, 965, 1275, 1275, 1218, 1246,
	/*   300 */ 1246, 1001, 1001, 1246, 1278, 1278, 1278, 1278, 1278, 1241,
	/*   310 */ 1278, 1173, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   320 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   330 */ 1107, 1278, 962, 1215, 1278, 1278, 1214, 1278, 1208, 1278,
	/*   340 */ 1278, 1261, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   350 */ 1278, 1189, 1188, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   360 */ 1278, 1278, 1278, 1278, 1278, 1278, 1268, 1278, 1278, 1278,
	/*   370 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   380 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   390 */ 1278, 1278, 1039, 1278, 1278, 1278, 1255, 1278, 1278, 1278,
	/*   400 */ 1278, 1278, 1278, 1278, 1082, 1278, 1075, 1278, 1278, 1259,
	/*   410 */ 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278, 1278,
	/*   420 */ 1224, 1278, 1278, 1278, 1223, 1278, 1278, 1278, 1278, 1278,
	/*   430 */ 1109, 1278, 1108, 1112, 1278, 969, 1278, 1278,
};

/********** End of lemon-generated parsing tables *****************************/

/* The next table maps tokens (terminal symbols) into fallback tokens.
 * If a construct like the following:
 *
 *      %fallback ID X Y Z.
 *
 * appears in the grammar, then ID becomes a fallback token for X, Y,
 * and Z.  Whenever one of the tokens X, Y, or Z is input to the parser
 * but it does not parse, the type of the token is changed to ID and
 * the parse is retried before an error is thrown.
 *
 * This feature can be used, for example, to cause some keywords in a language
 * to revert to identifiers if they keyword does not apply in the context where
 * it appears.
 */
#ifdef YYFALLBACK
static const YYCODETYPE yyFallback[] = {
	0,			/*          $ => nothing */
	0,			/*       SEMI => nothing */
	52,			/*    EXPLAIN => ID */
	52,			/*      QUERY => ID */
	52,			/*       PLAN => ID */
	0,			/*         OR => nothing */
	0,			/*        AND => nothing */
	0,			/*        NOT => nothing */
	0,			/*         IS => nothing */
	52,			/*      MATCH => ID */
	52,			/*    LIKE_KW => ID */
	0,			/*    BETWEEN => nothing */
	0,			/*         IN => nothing */
	0,			/*     ISNULL => nothing */
	0,			/*    NOTNULL => nothing */
	0,			/*         NE => nothing */
	0,			/*         EQ => nothing */
	0,			/*         GT => nothing */
	0,			/*         LE => nothing */
	0,			/*         LT => nothing */
	0,			/*         GE => nothing */
	0,			/*     ESCAPE => nothing */
	0,			/*     BITAND => nothing */
	0,			/*      BITOR => nothing */
	0,			/*     LSHIFT => nothing */
	0,			/*     RSHIFT => nothing */
	0,			/*       PLUS => nothing */
	0,			/*      MINUS => nothing */
	0,			/*       STAR => nothing */
	0,			/*      SLASH => nothing */
	0,			/*        REM => nothing */
	0,			/*     CONCAT => nothing */
	0,			/*    COLLATE => nothing */
	0,			/*     BITNOT => nothing */
	52,			/*      BEGIN => ID */
	0,			/* TRANSACTION => nothing */
	52,			/*   DEFERRED => ID */
	0,			/*     COMMIT => nothing */
	52,			/*        END => ID */
	52,			/*   ROLLBACK => ID */
	52,			/*  SAVEPOINT => ID */
	52,			/*    RELEASE => ID */
	0,			/*         TO => nothing */
	0,			/*      TABLE => nothing */
	0,			/*     CREATE => nothing */
	52,			/*         IF => ID */
	0,			/*     EXISTS => nothing */
	0,			/*         LP => nothing */
	0,			/*         RP => nothing */
	0,			/*         AS => nothing */
	52,			/*    WITHOUT => ID */
	0,			/*      COMMA => nothing */
	0,			/*         ID => nothing */
	0,			/*    INDEXED => nothing */
	52,			/*      ABORT => ID */
	52,			/*     ACTION => ID */
	52,			/*      AFTER => ID */
	52,			/*    ANALYZE => ID */
	52,			/*        ASC => ID */
	52,			/*     ATTACH => ID */
	52,			/*     BEFORE => ID */
	52,			/*         BY => ID */
	52,			/*    CASCADE => ID */
	52,			/*       CAST => ID */
	52,			/*   COLUMNKW => ID */
	52,			/*   CONFLICT => ID */
	52,			/*   DATABASE => ID */
	52,			/*       DESC => ID */
	52,			/*     DETACH => ID */
	52,			/*       EACH => ID */
	52,			/*       FAIL => ID */
	52,			/*        FOR => ID */
	52,			/*     IGNORE => ID */
	52,			/*  IMMEDIATE => ID */
	52,			/*  INITIALLY => ID */
	52,			/*    INSTEAD => ID */
	52,			/*         NO => ID */
	52,			/*        KEY => ID */
	52,			/*         OF => ID */
	52,			/*     OFFSET => ID */
	52,			/*     PRAGMA => ID */
	52,			/*      RAISE => ID */
	52,			/*  RECURSIVE => ID */
	52,			/*    REPLACE => ID */
	52,			/*   RESTRICT => ID */
	52,			/*        ROW => ID */
	52,			/*    TRIGGER => ID */
	52,			/*       VIEW => ID */
	52,			/*    VIRTUAL => ID */
	52,			/*       WITH => ID */
	52,			/*    REINDEX => ID */
	52,			/*     RENAME => ID */
	52,			/*   CTIME_KW => ID */
};
#endif				/* YYFALLBACK */

/* The following structure represents a single element of the
 * parser's stack.  Information stored includes:
 *
 *   +  The state number for the parser at this level of the stack.
 *
 *   +  The value of the token stored at this level of the stack.
 *      (In other words, the "major" token.)
 *
 *   +  The semantic value stored at this level of the stack.  This is
 *      the information used by the action routines in the grammar.
 *      It is sometimes called the "minor" token.
 *
 * After the "shift" half of a SHIFTREDUCE action, the stateno field
 * actually contains the reduce action for the second half of the
 * SHIFTREDUCE.
 */
struct yyStackEntry {
	YYACTIONTYPE stateno;	/* The state-number, or reduce action in SHIFTREDUCE */
	YYCODETYPE major;	/* The major token value.  This is the code
				 * number for the token at this stack level
				 */
	YYMINORTYPE minor;	/* The user-supplied minor token value.  This
				 * is the value of the token
				 */
};
typedef struct yyStackEntry yyStackEntry;

/* The state of the parser is completely contained in an instance of
 * the following structure
 */
struct yyParser {
	yyStackEntry *yytos;	/* Pointer to top element of the stack */
#ifdef YYTRACKMAXSTACKDEPTH
	int yyhwm;		/* High-water mark of the stack */
#endif
#ifndef YYNOERRORRECOVERY
	int yyerrcnt;		/* Shifts left before out of the error */
#endif
	 sqlite3ParserARG_SDECL	/* A place to hold %extra_argument */
#if YYSTACKDEPTH<=0
	int yystksz;		/* Current side of the stack */
	yyStackEntry *yystack;	/* The parser's stack */
	yyStackEntry yystk0;	/* First stack entry */
#else
	 yyStackEntry yystack[YYSTACKDEPTH];	/* The parser's stack */
#endif
};
typedef struct yyParser yyParser;

#ifndef NDEBUG
#include <stdio.h>
static FILE *yyTraceFILE = 0;
static char *yyTracePrompt = 0;
#endif				/* NDEBUG */

#ifndef NDEBUG
/*
 * Turn parser tracing on by giving a stream to which to write the trace
 * and a prompt to preface each trace message.  Tracing is turned off
 * by making either argument NULL
 *
 * Inputs:
 * <ul>
 * <li> A FILE* to which trace output should be written.
 *      If NULL, then tracing is turned off.
 * <li> A prefix string written at the beginning of every
 *      line of trace output.  If NULL, then tracing is
 *      turned off.
 * </ul>
 *
 * Outputs:
 * None.
 */
void
sqlite3ParserTrace(FILE * TraceFILE, char *zTracePrompt)
{
	yyTraceFILE = TraceFILE;
	yyTracePrompt = zTracePrompt;
	if (yyTraceFILE == 0)
		yyTracePrompt = 0;
	else if (yyTracePrompt == 0)
		yyTraceFILE = 0;
}
#endif				/* NDEBUG */

#ifndef NDEBUG
/* For tracing shifts, the names of all terminals and nonterminals
 * are required.  The following table supplies these names
 */
static const char *const yyTokenName[] = {
	"$", "SEMI", "EXPLAIN", "QUERY",
	"PLAN", "OR", "AND", "NOT",
	"IS", "MATCH", "LIKE_KW", "BETWEEN",
	"IN", "ISNULL", "NOTNULL", "NE",
	"EQ", "GT", "LE", "LT",
	"GE", "ESCAPE", "BITAND", "BITOR",
	"LSHIFT", "RSHIFT", "PLUS", "MINUS",
	"STAR", "SLASH", "REM", "CONCAT",
	"COLLATE", "BITNOT", "BEGIN", "TRANSACTION",
	"DEFERRED", "COMMIT", "END", "ROLLBACK",
	"SAVEPOINT", "RELEASE", "TO", "TABLE",
	"CREATE", "IF", "EXISTS", "LP",
	"RP", "AS", "WITHOUT", "COMMA",
	"ID", "INDEXED", "ABORT", "ACTION",
	"AFTER", "ANALYZE", "ASC", "ATTACH",
	"BEFORE", "BY", "CASCADE", "CAST",
	"COLUMNKW", "CONFLICT", "DATABASE", "DESC",
	"DETACH", "EACH", "FAIL", "FOR",
	"IGNORE", "IMMEDIATE", "INITIALLY", "INSTEAD",
	"NO", "KEY", "OF", "OFFSET",
	"PRAGMA", "RAISE", "RECURSIVE", "REPLACE",
	"RESTRICT", "ROW", "TRIGGER", "VIEW",
	"VIRTUAL", "WITH", "REINDEX", "RENAME",
	"CTIME_KW", "ANY", "STRING", "JOIN_KW",
	"CONSTRAINT", "DEFAULT", "NULL", "PRIMARY",
	"UNIQUE", "CHECK", "REFERENCES", "AUTOINCR",
	"ON", "INSERT", "DELETE", "UPDATE",
	"SET", "DEFERRABLE", "FOREIGN", "DROP",
	"UNION", "ALL", "EXCEPT", "INTERSECT",
	"SELECT", "VALUES", "DISTINCT", "DOT",
	"FROM", "JOIN", "USING", "ORDER",
	"GROUP", "HAVING", "LIMIT", "WHERE",
	"INTO", "FLOAT", "BLOB", "INTEGER",
	"VARIABLE", "CASE", "WHEN", "THEN",
	"ELSE", "INDEX", "ALTER", "ADD",
	"error", "input", "ecmd", "explain",
	"cmdx", "cmd", "transtype", "trans_opt",
	"nm", "savepoint_opt", "create_table", "create_table_args",
	"createkw", "ifnotexists", "columnlist", "conslist_opt",
	"table_options", "select", "columnname", "carglist",
	"typetoken", "typename", "signed", "plus_num",
	"minus_num", "ccons", "term", "expr",
	"onconf", "sortorder", "autoinc", "eidlist_opt",
	"refargs", "defer_subclause", "refarg", "refact",
	"init_deferred_pred_opt", "conslist", "tconscomma", "tcons",
	"sortlist", "eidlist", "defer_subclause_opt", "orconf",
	"resolvetype", "raisetype", "ifexists", "fullname",
	"selectnowith", "oneselect", "with", "multiselect_op",
	"distinct", "selcollist", "from", "where_opt",
	"groupby_opt", "having_opt", "orderby_opt", "limit_opt",
	"values", "nexprlist", "exprlist", "sclp",
	"as", "seltablist", "stl_prefix", "joinop",
	"indexed_opt", "on_opt", "using_opt", "idlist",
	"setlist", "insert_cmd", "idlist_opt", "likeop",
	"between_op", "in_op", "paren_exprlist", "case_operand",
	"case_exprlist", "case_else", "uniqueflag", "collate",
	"nmnum", "trigger_decl", "trigger_cmd_list", "trigger_time",
	"trigger_event", "foreach_clause", "when_clause", "trigger_cmd",
	"trnm", "tridxby", "add_column_fullname", "kwcolumn_opt",
	"create_vtab", "vtabarglist", "vtabarg", "vtabargtoken",
	"lp", "anylist", "wqlist",
};
#endif				/* NDEBUG */

#ifndef NDEBUG
/* For tracing reduce actions, the names of all rules are required.
 */
static const char *const yyRuleName[] = {
	/*   0 */ "ecmd ::= explain cmdx SEMI",
	/*   1 */ "ecmd ::= SEMI",
	/*   2 */ "explain ::= EXPLAIN",
	/*   3 */ "explain ::= EXPLAIN QUERY PLAN",
	/*   4 */ "cmd ::= BEGIN transtype trans_opt",
	/*   5 */ "transtype ::=",
	/*   6 */ "transtype ::= DEFERRED",
	/*   7 */ "cmd ::= COMMIT trans_opt",
	/*   8 */ "cmd ::= END trans_opt",
	/*   9 */ "cmd ::= ROLLBACK trans_opt",
	/*  10 */ "cmd ::= SAVEPOINT nm",
	/*  11 */ "cmd ::= RELEASE savepoint_opt nm",
	/*  12 */ "cmd ::= ROLLBACK trans_opt TO savepoint_opt nm",
	/*  13 */ "create_table ::= createkw TABLE ifnotexists nm",
	/*  14 */ "createkw ::= CREATE",
	/*  15 */ "ifnotexists ::=",
	/*  16 */ "ifnotexists ::= IF NOT EXISTS",
	/*  17 */
	    "create_table_args ::= LP columnlist conslist_opt RP table_options",
	/*  18 */ "create_table_args ::= AS select",
	/*  19 */ "table_options ::=",
	/*  20 */ "table_options ::= WITHOUT nm",
	/*  21 */ "columnname ::= nm typetoken",
	/*  22 */ "typetoken ::=",
	/*  23 */ "typetoken ::= typename LP signed RP",
	/*  24 */ "typetoken ::= typename LP signed COMMA signed RP",
	/*  25 */ "typename ::= typename ID|STRING",
	/*  26 */ "ccons ::= CONSTRAINT nm",
	/*  27 */ "ccons ::= DEFAULT term",
	/*  28 */ "ccons ::= DEFAULT LP expr RP",
	/*  29 */ "ccons ::= DEFAULT PLUS term",
	/*  30 */ "ccons ::= DEFAULT MINUS term",
	/*  31 */ "ccons ::= DEFAULT ID|INDEXED",
	/*  32 */ "ccons ::= NOT NULL onconf",
	/*  33 */ "ccons ::= PRIMARY KEY sortorder onconf autoinc",
	/*  34 */ "ccons ::= UNIQUE onconf",
	/*  35 */ "ccons ::= CHECK LP expr RP",
	/*  36 */ "ccons ::= REFERENCES nm eidlist_opt refargs",
	/*  37 */ "ccons ::= defer_subclause",
	/*  38 */ "ccons ::= COLLATE ID|STRING",
	/*  39 */ "autoinc ::=",
	/*  40 */ "autoinc ::= AUTOINCR",
	/*  41 */ "refargs ::=",
	/*  42 */ "refargs ::= refargs refarg",
	/*  43 */ "refarg ::= MATCH nm",
	/*  44 */ "refarg ::= ON INSERT refact",
	/*  45 */ "refarg ::= ON DELETE refact",
	/*  46 */ "refarg ::= ON UPDATE refact",
	/*  47 */ "refact ::= SET NULL",
	/*  48 */ "refact ::= SET DEFAULT",
	/*  49 */ "refact ::= CASCADE",
	/*  50 */ "refact ::= RESTRICT",
	/*  51 */ "refact ::= NO ACTION",
	/*  52 */ "defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt",
	/*  53 */ "defer_subclause ::= DEFERRABLE init_deferred_pred_opt",
	/*  54 */ "init_deferred_pred_opt ::=",
	/*  55 */ "init_deferred_pred_opt ::= INITIALLY DEFERRED",
	/*  56 */ "init_deferred_pred_opt ::= INITIALLY IMMEDIATE",
	/*  57 */ "conslist_opt ::=",
	/*  58 */ "tconscomma ::= COMMA",
	/*  59 */ "tcons ::= CONSTRAINT nm",
	/*  60 */ "tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf",
	/*  61 */ "tcons ::= UNIQUE LP sortlist RP onconf",
	/*  62 */ "tcons ::= CHECK LP expr RP onconf",
	/*  63 */
	    "tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt",
	/*  64 */ "defer_subclause_opt ::=",
	/*  65 */ "onconf ::=",
	/*  66 */ "onconf ::= ON CONFLICT resolvetype",
	/*  67 */ "orconf ::=",
	/*  68 */ "orconf ::= OR resolvetype",
	/*  69 */ "resolvetype ::= IGNORE",
	/*  70 */ "resolvetype ::= REPLACE",
	/*  71 */ "cmd ::= DROP TABLE ifexists fullname",
	/*  72 */ "ifexists ::= IF EXISTS",
	/*  73 */ "ifexists ::=",
	/*  74 */ "cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select",
	/*  75 */ "cmd ::= DROP VIEW ifexists fullname",
	/*  76 */ "cmd ::= select",
	/*  77 */ "select ::= with selectnowith",
	/*  78 */ "selectnowith ::= selectnowith multiselect_op oneselect",
	/*  79 */ "multiselect_op ::= UNION",
	/*  80 */ "multiselect_op ::= UNION ALL",
	/*  81 */ "multiselect_op ::= EXCEPT|INTERSECT",
	/*  82 */
	    "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
	/*  83 */ "values ::= VALUES LP nexprlist RP",
	/*  84 */ "values ::= values COMMA LP exprlist RP",
	/*  85 */ "distinct ::= DISTINCT",
	/*  86 */ "distinct ::= ALL",
	/*  87 */ "distinct ::=",
	/*  88 */ "sclp ::=",
	/*  89 */ "selcollist ::= sclp expr as",
	/*  90 */ "selcollist ::= sclp STAR",
	/*  91 */ "selcollist ::= sclp nm DOT STAR",
	/*  92 */ "as ::= AS nm",
	/*  93 */ "as ::=",
	/*  94 */ "from ::=",
	/*  95 */ "from ::= FROM seltablist",
	/*  96 */ "stl_prefix ::= seltablist joinop",
	/*  97 */ "stl_prefix ::=",
	/*  98 */
	    "seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt",
	/*  99 */
	    "seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt",
	/* 100 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
	/* 101 */
	    "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
	/* 102 */ "fullname ::= nm",
	/* 103 */ "joinop ::= COMMA|JOIN",
	/* 104 */ "joinop ::= JOIN_KW JOIN",
	/* 105 */ "joinop ::= JOIN_KW nm JOIN",
	/* 106 */ "joinop ::= JOIN_KW nm nm JOIN",
	/* 107 */ "on_opt ::= ON expr",
	/* 108 */ "on_opt ::=",
	/* 109 */ "indexed_opt ::=",
	/* 110 */ "indexed_opt ::= INDEXED BY nm",
	/* 111 */ "indexed_opt ::= NOT INDEXED",
	/* 112 */ "using_opt ::= USING LP idlist RP",
	/* 113 */ "using_opt ::=",
	/* 114 */ "orderby_opt ::=",
	/* 115 */ "orderby_opt ::= ORDER BY sortlist",
	/* 116 */ "sortlist ::= sortlist COMMA expr sortorder",
	/* 117 */ "sortlist ::= expr sortorder",
	/* 118 */ "sortorder ::= ASC",
	/* 119 */ "sortorder ::= DESC",
	/* 120 */ "sortorder ::=",
	/* 121 */ "groupby_opt ::=",
	/* 122 */ "groupby_opt ::= GROUP BY nexprlist",
	/* 123 */ "having_opt ::=",
	/* 124 */ "having_opt ::= HAVING expr",
	/* 125 */ "limit_opt ::=",
	/* 126 */ "limit_opt ::= LIMIT expr",
	/* 127 */ "limit_opt ::= LIMIT expr OFFSET expr",
	/* 128 */ "limit_opt ::= LIMIT expr COMMA expr",
	/* 129 */ "cmd ::= with DELETE FROM fullname indexed_opt where_opt",
	/* 130 */ "where_opt ::=",
	/* 131 */ "where_opt ::= WHERE expr",
	/* 132 */
	    "cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt",
	/* 133 */ "setlist ::= setlist COMMA nm EQ expr",
	/* 134 */ "setlist ::= setlist COMMA LP idlist RP EQ expr",
	/* 135 */ "setlist ::= nm EQ expr",
	/* 136 */ "setlist ::= LP idlist RP EQ expr",
	/* 137 */ "cmd ::= with insert_cmd INTO fullname idlist_opt select",
	/* 138 */
	    "cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES",
	/* 139 */ "insert_cmd ::= INSERT orconf",
	/* 140 */ "insert_cmd ::= REPLACE",
	/* 141 */ "idlist_opt ::=",
	/* 142 */ "idlist_opt ::= LP idlist RP",
	/* 143 */ "idlist ::= idlist COMMA nm",
	/* 144 */ "idlist ::= nm",
	/* 145 */ "expr ::= LP expr RP",
	/* 146 */ "term ::= NULL",
	/* 147 */ "expr ::= ID|INDEXED",
	/* 148 */ "expr ::= JOIN_KW",
	/* 149 */ "expr ::= nm DOT nm",
	/* 150 */ "expr ::= nm DOT nm DOT nm",
	/* 151 */ "term ::= FLOAT|BLOB",
	/* 152 */ "term ::= STRING",
	/* 153 */ "term ::= INTEGER",
	/* 154 */ "expr ::= VARIABLE",
	/* 155 */ "expr ::= expr COLLATE ID|STRING",
	/* 156 */ "expr ::= CAST LP expr AS typetoken RP",
	/* 157 */ "expr ::= ID|INDEXED LP distinct exprlist RP",
	/* 158 */ "expr ::= ID|INDEXED LP STAR RP",
	/* 159 */ "term ::= CTIME_KW",
	/* 160 */ "expr ::= LP nexprlist COMMA expr RP",
	/* 161 */ "expr ::= expr AND expr",
	/* 162 */ "expr ::= expr OR expr",
	/* 163 */ "expr ::= expr LT|GT|GE|LE expr",
	/* 164 */ "expr ::= expr EQ|NE expr",
	/* 165 */ "expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr",
	/* 166 */ "expr ::= expr PLUS|MINUS expr",
	/* 167 */ "expr ::= expr STAR|SLASH|REM expr",
	/* 168 */ "expr ::= expr CONCAT expr",
	/* 169 */ "likeop ::= LIKE_KW|MATCH",
	/* 170 */ "likeop ::= NOT LIKE_KW|MATCH",
	/* 171 */ "expr ::= expr likeop expr",
	/* 172 */ "expr ::= expr likeop expr ESCAPE expr",
	/* 173 */ "expr ::= expr ISNULL|NOTNULL",
	/* 174 */ "expr ::= expr NOT NULL",
	/* 175 */ "expr ::= expr IS expr",
	/* 176 */ "expr ::= expr IS NOT expr",
	/* 177 */ "expr ::= NOT expr",
	/* 178 */ "expr ::= BITNOT expr",
	/* 179 */ "expr ::= MINUS expr",
	/* 180 */ "expr ::= PLUS expr",
	/* 181 */ "between_op ::= BETWEEN",
	/* 182 */ "between_op ::= NOT BETWEEN",
	/* 183 */ "expr ::= expr between_op expr AND expr",
	/* 184 */ "in_op ::= IN",
	/* 185 */ "in_op ::= NOT IN",
	/* 186 */ "expr ::= expr in_op LP exprlist RP",
	/* 187 */ "expr ::= LP select RP",
	/* 188 */ "expr ::= expr in_op LP select RP",
	/* 189 */ "expr ::= expr in_op nm paren_exprlist",
	/* 190 */ "expr ::= EXISTS LP select RP",
	/* 191 */ "expr ::= CASE case_operand case_exprlist case_else END",
	/* 192 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
	/* 193 */ "case_exprlist ::= WHEN expr THEN expr",
	/* 194 */ "case_else ::= ELSE expr",
	/* 195 */ "case_else ::=",
	/* 196 */ "case_operand ::= expr",
	/* 197 */ "case_operand ::=",
	/* 198 */ "exprlist ::=",
	/* 199 */ "nexprlist ::= nexprlist COMMA expr",
	/* 200 */ "nexprlist ::= expr",
	/* 201 */ "paren_exprlist ::=",
	/* 202 */ "paren_exprlist ::= LP exprlist RP",
	/* 203 */
	    "cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt",
	/* 204 */ "uniqueflag ::= UNIQUE",
	/* 205 */ "uniqueflag ::=",
	/* 206 */ "eidlist_opt ::=",
	/* 207 */ "eidlist_opt ::= LP eidlist RP",
	/* 208 */ "eidlist ::= eidlist COMMA nm collate sortorder",
	/* 209 */ "eidlist ::= nm collate sortorder",
	/* 210 */ "collate ::=",
	/* 211 */ "collate ::= COLLATE ID|STRING",
	/* 212 */ "cmd ::= DROP INDEX ifexists fullname ON nm",
	/* 213 */ "cmd ::= PRAGMA nm",
	/* 214 */ "cmd ::= PRAGMA nm EQ nmnum",
	/* 215 */ "cmd ::= PRAGMA nm LP nmnum RP",
	/* 216 */ "cmd ::= PRAGMA nm EQ minus_num",
	/* 217 */ "cmd ::= PRAGMA nm LP minus_num RP",
	/* 218 */ "cmd ::= PRAGMA nm EQ nm DOT nm",
	/* 219 */ "plus_num ::= PLUS INTEGER|FLOAT",
	/* 220 */ "minus_num ::= MINUS INTEGER|FLOAT",
	/* 221 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
	/* 222 */
	    "trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause",
	/* 223 */ "trigger_time ::= BEFORE",
	/* 224 */ "trigger_time ::= AFTER",
	/* 225 */ "trigger_time ::= INSTEAD OF",
	/* 226 */ "trigger_time ::=",
	/* 227 */ "trigger_event ::= DELETE|INSERT",
	/* 228 */ "trigger_event ::= UPDATE",
	/* 229 */ "trigger_event ::= UPDATE OF idlist",
	/* 230 */ "when_clause ::=",
	/* 231 */ "when_clause ::= WHEN expr",
	/* 232 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
	/* 233 */ "trigger_cmd_list ::= trigger_cmd SEMI",
	/* 234 */ "trnm ::= nm DOT nm",
	/* 235 */ "tridxby ::= INDEXED BY nm",
	/* 236 */ "tridxby ::= NOT INDEXED",
	/* 237 */
	    "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
	/* 238 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
	/* 239 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
	/* 240 */ "trigger_cmd ::= select",
	/* 241 */ "expr ::= RAISE LP IGNORE RP",
	/* 242 */ "expr ::= RAISE LP raisetype COMMA nm RP",
	/* 243 */ "raisetype ::= ROLLBACK",
	/* 244 */ "raisetype ::= ABORT",
	/* 245 */ "raisetype ::= FAIL",
	/* 246 */ "cmd ::= DROP TRIGGER ifexists fullname",
	/* 247 */ "cmd ::= REINDEX",
	/* 248 */ "cmd ::= REINDEX nm",
	/* 249 */ "cmd ::= REINDEX nm ON nm",
	/* 250 */ "cmd ::= ANALYZE",
	/* 251 */ "cmd ::= ANALYZE nm",
	/* 252 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
	/* 253 */
	    "cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist",
	/* 254 */ "add_column_fullname ::= fullname",
	/* 255 */ "cmd ::= create_vtab",
	/* 256 */ "cmd ::= create_vtab LP vtabarglist RP",
	/* 257 */
	    "create_vtab ::= createkw VIRTUAL TABLE ifnotexists nm USING nm",
	/* 258 */ "vtabarg ::=",
	/* 259 */ "vtabargtoken ::= ANY",
	/* 260 */ "vtabargtoken ::= lp anylist RP",
	/* 261 */ "lp ::= LP",
	/* 262 */ "with ::=",
	/* 263 */ "with ::= WITH wqlist",
	/* 264 */ "with ::= WITH RECURSIVE wqlist",
	/* 265 */ "wqlist ::= nm eidlist_opt AS LP select RP",
	/* 266 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
	/* 267 */ "input ::= ecmd",
	/* 268 */ "explain ::=",
	/* 269 */ "cmdx ::= cmd",
	/* 270 */ "trans_opt ::=",
	/* 271 */ "trans_opt ::= TRANSACTION",
	/* 272 */ "trans_opt ::= TRANSACTION nm",
	/* 273 */ "savepoint_opt ::= SAVEPOINT",
	/* 274 */ "savepoint_opt ::=",
	/* 275 */ "cmd ::= create_table create_table_args",
	/* 276 */ "columnlist ::= columnlist COMMA columnname carglist",
	/* 277 */ "columnlist ::= columnname carglist",
	/* 278 */ "nm ::= ID|INDEXED",
	/* 279 */ "nm ::= STRING",
	/* 280 */ "nm ::= JOIN_KW",
	/* 281 */ "typetoken ::= typename",
	/* 282 */ "typename ::= ID|STRING",
	/* 283 */ "signed ::= plus_num",
	/* 284 */ "signed ::= minus_num",
	/* 285 */ "carglist ::= carglist ccons",
	/* 286 */ "carglist ::=",
	/* 287 */ "ccons ::= NULL onconf",
	/* 288 */ "conslist_opt ::= COMMA conslist",
	/* 289 */ "conslist ::= conslist tconscomma tcons",
	/* 290 */ "conslist ::= tcons",
	/* 291 */ "tconscomma ::=",
	/* 292 */ "defer_subclause_opt ::= defer_subclause",
	/* 293 */ "resolvetype ::= raisetype",
	/* 294 */ "selectnowith ::= oneselect",
	/* 295 */ "oneselect ::= values",
	/* 296 */ "sclp ::= selcollist COMMA",
	/* 297 */ "as ::= ID|STRING",
	/* 298 */ "expr ::= term",
	/* 299 */ "exprlist ::= nexprlist",
	/* 300 */ "nmnum ::= plus_num",
	/* 301 */ "nmnum ::= nm",
	/* 302 */ "nmnum ::= ON",
	/* 303 */ "nmnum ::= DELETE",
	/* 304 */ "nmnum ::= DEFAULT",
	/* 305 */ "plus_num ::= INTEGER|FLOAT",
	/* 306 */ "foreach_clause ::=",
	/* 307 */ "foreach_clause ::= FOR EACH ROW",
	/* 308 */ "trnm ::= nm",
	/* 309 */ "tridxby ::=",
	/* 310 */ "kwcolumn_opt ::=",
	/* 311 */ "kwcolumn_opt ::= COLUMNKW",
	/* 312 */ "vtabarglist ::= vtabarg",
	/* 313 */ "vtabarglist ::= vtabarglist COMMA vtabarg",
	/* 314 */ "vtabarg ::= vtabarg vtabargtoken",
	/* 315 */ "anylist ::=",
	/* 316 */ "anylist ::= anylist LP anylist RP",
	/* 317 */ "anylist ::= anylist ANY",
};
#endif				/* NDEBUG */

#if YYSTACKDEPTH<=0
/*
 * Try to increase the size of the parser stack.  Return the number
 * of errors.  Return 0 on success.
 */
static int
yyGrowStack(yyParser * p)
{
	int newSize;
	int idx;
	yyStackEntry *pNew;

	newSize = p->yystksz * 2 + 100;
	idx = p->yytos ? (int)(p->yytos - p->yystack) : 0;
	if (p->yystack == &p->yystk0) {
		pNew = malloc(newSize * sizeof(pNew[0]));
		if (pNew)
			pNew[0] = p->yystk0;
	} else {
		pNew = realloc(p->yystack, newSize * sizeof(pNew[0]));
	}
	if (pNew) {
		p->yystack = pNew;
		p->yytos = &p->yystack[idx];
#ifndef NDEBUG
		if (yyTraceFILE) {
			fprintf(yyTraceFILE,
				"%sStack grows from %d to %d entries.\n",
				yyTracePrompt, p->yystksz, newSize);
		}
#endif
		p->yystksz = newSize;
	}
	return pNew == 0;
}
#endif

/* Datatype of the argument to the memory allocated passed as the
 * second argument to sqlite3ParserAlloc() below.  This can be changed by
 * putting an appropriate #define in the %include section of the input
 * grammar.
 */
#ifndef YYMALLOCARGTYPE
#define YYMALLOCARGTYPE size_t
#endif

/*
 * This function allocates a new parser.
 * The only argument is a pointer to a function which works like
 * malloc.
 *
 * Inputs:
 * A pointer to the function used to allocate memory.
 *
 * Outputs:
 * A pointer to a parser.  This pointer is used in subsequent calls
 * to sqlite3Parser and sqlite3ParserFree.
 */
void *
sqlite3ParserAlloc(void *(*mallocProc) (YYMALLOCARGTYPE))
{
	yyParser *pParser;
	pParser =
	    (yyParser *) (*mallocProc) ((YYMALLOCARGTYPE) sizeof(yyParser));
	if (pParser) {
#ifdef YYTRACKMAXSTACKDEPTH
		pParser->yyhwm = 0;
#endif
#if YYSTACKDEPTH<=0
		pParser->yytos = NULL;
		pParser->yystack = NULL;
		pParser->yystksz = 0;
		if (yyGrowStack(pParser)) {
			pParser->yystack = &pParser->yystk0;
			pParser->yystksz = 1;
		}
#endif
#ifndef YYNOERRORRECOVERY
		pParser->yyerrcnt = -1;
#endif
		pParser->yytos = pParser->yystack;
		pParser->yystack[0].stateno = 0;
		pParser->yystack[0].major = 0;
	}
	return pParser;
}

/* The following function deletes the "minor type" or semantic value
 * associated with a symbol.  The symbol can be either a terminal
 * or nonterminal. "yymajor" is the symbol code, and "yypminor" is
 * a pointer to the value to be deleted.  The code used to do the
 * deletions is derived from the %destructor and/or %token_destructor
 * directives of the input grammar.
 */
static void
yy_destructor(yyParser * yypParser,	/* The parser */
	      YYCODETYPE yymajor,	/* Type code for object to destroy */
	      YYMINORTYPE * yypminor	/* The object to be destroyed */
    )
{
	sqlite3ParserARG_FETCH;
	switch (yymajor) {
		/* Here is inserted the actions which take place when a
		 * terminal or non-terminal is destroyed.  This can happen
		 * when the symbol is popped from the stack during a
		 * reduce or during error processing or when a parser is
		 * being destroyed before it is finished parsing.
		 *
		 * Note: during a reduce, the only symbols destroyed are those
		 * which appear on the RHS of the rule, but which are *not* used
		 * inside the C code.
		 */
/********* Begin destructor definitions ***************************************/
	case 157:		/* select */
	case 188:		/* selectnowith */
	case 189:		/* oneselect */
	case 200:		/* values */
		{
#line 391 "parse.y"
			sqlite3SelectDelete(pParse->db, (yypminor->yy99));
#line 1532 "parse.c"
		}
		break;
	case 166:		/* term */
	case 167:		/* expr */
		{
#line 832 "parse.y"
			sqlite3ExprDelete(pParse->db, (yypminor->yy190).pExpr);
#line 1540 "parse.c"
		}
		break;
	case 171:		/* eidlist_opt */
	case 180:		/* sortlist */
	case 181:		/* eidlist */
	case 193:		/* selcollist */
	case 196:		/* groupby_opt */
	case 198:		/* orderby_opt */
	case 201:		/* nexprlist */
	case 202:		/* exprlist */
	case 203:		/* sclp */
	case 212:		/* setlist */
	case 218:		/* paren_exprlist */
	case 220:		/* case_exprlist */
		{
#line 1280 "parse.y"
			sqlite3ExprListDelete(pParse->db, (yypminor->yy412));
#line 1558 "parse.c"
		}
		break;
	case 187:		/* fullname */
	case 194:		/* from */
	case 205:		/* seltablist */
	case 206:		/* stl_prefix */
		{
#line 619 "parse.y"
			sqlite3SrcListDelete(pParse->db, (yypminor->yy367));
#line 1568 "parse.c"
		}
		break;
	case 190:		/* with */
	case 242:		/* wqlist */
		{
#line 1545 "parse.y"
			sqlite3WithDelete(pParse->db, (yypminor->yy91));
#line 1576 "parse.c"
		}
		break;
	case 195:		/* where_opt */
	case 197:		/* having_opt */
	case 209:		/* on_opt */
	case 219:		/* case_operand */
	case 221:		/* case_else */
	case 230:		/* when_clause */
		{
#line 741 "parse.y"
			sqlite3ExprDelete(pParse->db, (yypminor->yy44));
#line 1588 "parse.c"
		}
		break;
	case 210:		/* using_opt */
	case 211:		/* idlist */
	case 214:		/* idlist_opt */
		{
#line 653 "parse.y"
			sqlite3IdListDelete(pParse->db, (yypminor->yy258));
#line 1597 "parse.c"
		}
		break;
	case 226:		/* trigger_cmd_list */
	case 231:		/* trigger_cmd */
		{
#line 1399 "parse.y"
			sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy203));
#line 1605 "parse.c"
		}
		break;
	case 228:		/* trigger_event */
		{
#line 1385 "parse.y"
			sqlite3IdListDelete(pParse->db, (yypminor->yy234).b);
#line 1612 "parse.c"
		}
		break;
/********* End destructor definitions *****************************************/
	default:
		break;		/* If no destructor action specified: do nothing */
	}
}

/*
 * Pop the parser's stack once.
 *
 * If there is a destructor routine associated with the token which
 * is popped from the stack, then call it.
 */
static void
yy_pop_parser_stack(yyParser * pParser)
{
	yyStackEntry *yytos;
	assert(pParser->yytos != 0);
	assert(pParser->yytos > pParser->yystack);
	yytos = pParser->yytos--;
#ifndef NDEBUG
	if (yyTraceFILE) {
		fprintf(yyTraceFILE, "%sPopping %s\n",
			yyTracePrompt, yyTokenName[yytos->major]);
	}
#endif
	yy_destructor(pParser, yytos->major, &yytos->minor);
}

/*
 * Deallocate and destroy a parser.  Destructors are called for
 * all stack elements before shutting the parser down.
 *
 * If the YYPARSEFREENEVERNULL macro exists (for example because it
 * is defined in a %include section of the input grammar) then it is
 * assumed that the input pointer is never NULL.
 */
void
sqlite3ParserFree(void *p,	/* The parser to be deleted */
		  void (*freeProc) (void *)	/* Function used to reclaim memory */
    )
{
	yyParser *pParser = (yyParser *) p;
#ifndef YYPARSEFREENEVERNULL
	if (pParser == 0)
		return;
#endif
	while (pParser->yytos > pParser->yystack)
		yy_pop_parser_stack(pParser);
#if YYSTACKDEPTH<=0
	if (pParser->yystack != &pParser->yystk0)
		free(pParser->yystack);
#endif
	(*freeProc) ((void *)pParser);
}

/*
 * Return the peak depth of the stack for a parser.
 */
#ifdef YYTRACKMAXSTACKDEPTH
int
sqlite3ParserStackPeak(void *p)
{
	yyParser *pParser = (yyParser *) p;
	return pParser->yyhwm;
}
#endif

/*
 * Find the appropriate action for a parser given the terminal
 * look-ahead token iLookAhead.
 */
static unsigned int
yy_find_shift_action(yyParser * pParser,	/* The parser */
		     YYCODETYPE iLookAhead	/* The look-ahead token */
    )
{
	int i;
	int stateno = pParser->yytos->stateno;

	if (stateno >= YY_MIN_REDUCE)
		return stateno;
	assert(stateno <= YY_SHIFT_COUNT);
	do {
		i = yy_shift_ofst[stateno];
		assert(iLookAhead != YYNOCODE);
		i += iLookAhead;
		if (i < 0 || i >= YY_ACTTAB_COUNT
		    || yy_lookahead[i] != iLookAhead) {
#ifdef YYFALLBACK
			YYCODETYPE iFallback;	/* Fallback token */
			if (iLookAhead <
			    sizeof(yyFallback) / sizeof(yyFallback[0])
			    && (iFallback = yyFallback[iLookAhead]) != 0) {
#ifndef NDEBUG
				if (yyTraceFILE) {
					fprintf(yyTraceFILE,
						"%sFALLBACK %s => %s\n",
						yyTracePrompt,
						yyTokenName[iLookAhead],
						yyTokenName[iFallback]);
				}
#endif
				assert(yyFallback[iFallback] == 0);	/* Fallback loop must terminate */
				iLookAhead = iFallback;
				continue;
			}
#endif
#ifdef YYWILDCARD
			{
				int j = i - iLookAhead + YYWILDCARD;
				if (
#if YY_SHIFT_MIN+YYWILDCARD<0
					   j >= 0 &&
#endif
#if YY_SHIFT_MAX+YYWILDCARD>=YY_ACTTAB_COUNT
					   j < YY_ACTTAB_COUNT &&
#endif
					   yy_lookahead[j] == YYWILDCARD
					   && iLookAhead > 0) {
#ifndef NDEBUG
					if (yyTraceFILE) {
						fprintf(yyTraceFILE,
							"%sWILDCARD %s => %s\n",
							yyTracePrompt,
							yyTokenName[iLookAhead],
							yyTokenName
							[YYWILDCARD]);
					}
#endif				/* NDEBUG */
					return yy_action[j];
				}
			}
#endif				/* YYWILDCARD */
			return yy_default[stateno];
		} else {
			return yy_action[i];
		}
	} while (1);
}

/*
 * Find the appropriate action for a parser given the non-terminal
 * look-ahead token iLookAhead.
 */
static int
yy_find_reduce_action(int stateno,	/* Current state number */
		      YYCODETYPE iLookAhead	/* The look-ahead token */
    )
{
	int i;
#ifdef YYERRORSYMBOL
	if (stateno > YY_REDUCE_COUNT) {
		return yy_default[stateno];
	}
#else
	assert(stateno <= YY_REDUCE_COUNT);
#endif
	i = yy_reduce_ofst[stateno];
	assert(i != YY_REDUCE_USE_DFLT);
	assert(iLookAhead != YYNOCODE);
	i += iLookAhead;
#ifdef YYERRORSYMBOL
	if (i < 0 || i >= YY_ACTTAB_COUNT || yy_lookahead[i] != iLookAhead) {
		return yy_default[stateno];
	}
#else
	assert(i >= 0 && i < YY_ACTTAB_COUNT);
	assert(yy_lookahead[i] == iLookAhead);
#endif
	return yy_action[i];
}

/*
 * The following routine is called if the stack overflows.
 */
static void
yyStackOverflow(yyParser * yypParser)
{
	sqlite3ParserARG_FETCH;
#ifndef NDEBUG
	if (yyTraceFILE) {
		fprintf(yyTraceFILE, "%sStack Overflow!\n", yyTracePrompt);
	}
#endif
	while (yypParser->yytos > yypParser->yystack)
		yy_pop_parser_stack(yypParser);
	/* Here code is inserted which will execute if the parser
	 * stack every overflows
	 */
/******** Begin %stack_overflow code ******************************************/
#line 37 "parse.y"

	sqlite3ErrorMsg(pParse, "parser stack overflow");
#line 1785 "parse.c"
/******** End %stack_overflow code ********************************************/
	sqlite3ParserARG_STORE;	/* Suppress warning about unused %extra_argument var */
}

/*
 * Print tracing information for a SHIFT action
 */
#ifndef NDEBUG
static void
yyTraceShift(yyParser * yypParser, int yyNewState)
{
	if (yyTraceFILE) {
		if (yyNewState < YYNSTATE) {
			fprintf(yyTraceFILE, "%sShift '%s', go to state %d\n",
				yyTracePrompt,
				yyTokenName[yypParser->yytos->major],
				yyNewState);
		} else {
			fprintf(yyTraceFILE, "%sShift '%s'\n",
				yyTracePrompt,
				yyTokenName[yypParser->yytos->major]);
		}
	}
}
#else
#define yyTraceShift(X,Y)
#endif

/*
 * Perform a shift action.
 */
static void
yy_shift(yyParser * yypParser,	/* The parser to be shifted */
	 int yyNewState,	/* The new state to shift in */
	 int yyMajor,		/* The major token to shift in */
	 sqlite3ParserTOKENTYPE yyMinor	/* The minor token to shift in */
    )
{
	yyStackEntry *yytos;
	yypParser->yytos++;
#ifdef YYTRACKMAXSTACKDEPTH
	if ((int)(yypParser->yytos - yypParser->yystack) > yypParser->yyhwm) {
		yypParser->yyhwm++;
		assert(yypParser->yyhwm ==
		       (int)(yypParser->yytos - yypParser->yystack));
	}
#endif
#if YYSTACKDEPTH>0
	if (yypParser->yytos >= &yypParser->yystack[YYSTACKDEPTH]) {
		yypParser->yytos--;
		yyStackOverflow(yypParser);
		return;
	}
#else
	if (yypParser->yytos >= &yypParser->yystack[yypParser->yystksz]) {
		if (yyGrowStack(yypParser)) {
			yypParser->yytos--;
			yyStackOverflow(yypParser);
			return;
		}
	}
#endif
	if (yyNewState > YY_MAX_SHIFT) {
		yyNewState += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
	}
	yytos = yypParser->yytos;
	yytos->stateno = (YYACTIONTYPE) yyNewState;
	yytos->major = (YYCODETYPE) yyMajor;
	yytos->minor.yy0 = yyMinor;
	yyTraceShift(yypParser, yyNewState);
}

/* The following table contains information about every rule that
 * is used during the reduce.
 */
static const struct {
	YYCODETYPE lhs;		/* Symbol on the left-hand side of the rule */
	unsigned char nrhs;	/* Number of right-hand side symbols in the rule */
} yyRuleInfo[] = {
	{
	142, 3}, {
	142, 1}, {
	143, 1}, {
	143, 3}, {
	145, 3}, {
	146, 0}, {
	146, 1}, {
	145, 2}, {
	145, 2}, {
	145, 2}, {
	145, 2}, {
	145, 3}, {
	145, 5}, {
	150, 4}, {
	152, 1}, {
	153, 0}, {
	153, 3}, {
	151, 5}, {
	151, 2}, {
	156, 0}, {
	156, 2}, {
	158, 2}, {
	160, 0}, {
	160, 4}, {
	160, 6}, {
	161, 2}, {
	165, 2}, {
	165, 2}, {
	165, 4}, {
	165, 3}, {
	165, 3}, {
	165, 2}, {
	165, 3}, {
	165, 5}, {
	165, 2}, {
	165, 4}, {
	165, 4}, {
	165, 1}, {
	165, 2}, {
	170, 0}, {
	170, 1}, {
	172, 0}, {
	172, 2}, {
	174, 2}, {
	174, 3}, {
	174, 3}, {
	174, 3}, {
	175, 2}, {
	175, 2}, {
	175, 1}, {
	175, 1}, {
	175, 2}, {
	173, 3}, {
	173, 2}, {
	176, 0}, {
	176, 2}, {
	176, 2}, {
	155, 0}, {
	178, 1}, {
	179, 2}, {
	179, 7}, {
	179, 5}, {
	179, 5}, {
	179, 10}, {
	182, 0}, {
	168, 0}, {
	168, 3}, {
	183, 0}, {
	183, 2}, {
	184, 1}, {
	184, 1}, {
	145, 4}, {
	186, 2}, {
	186, 0}, {
	145, 7}, {
	145, 4}, {
	145, 1}, {
	157, 2}, {
	188, 3}, {
	191, 1}, {
	191, 2}, {
	191, 1}, {
	189, 9}, {
	200, 4}, {
	200, 5}, {
	192, 1}, {
	192, 1}, {
	192, 0}, {
	203, 0}, {
	193, 3}, {
	193, 2}, {
	193, 4}, {
	204, 2}, {
	204, 0}, {
	194, 0}, {
	194, 2}, {
	206, 2}, {
	206, 0}, {
	205, 6}, {
	205, 8}, {
	205, 7}, {
	205, 7}, {
	187, 1}, {
	207, 1}, {
	207, 2}, {
	207, 3}, {
	207, 4}, {
	209, 2}, {
	209, 0}, {
	208, 0}, {
	208, 3}, {
	208, 2}, {
	210, 4}, {
	210, 0}, {
	198, 0}, {
	198, 3}, {
	180, 4}, {
	180, 2}, {
	169, 1}, {
	169, 1}, {
	169, 0}, {
	196, 0}, {
	196, 3}, {
	197, 0}, {
	197, 2}, {
	199, 0}, {
	199, 2}, {
	199, 4}, {
	199, 4}, {
	145, 6}, {
	195, 0}, {
	195, 2}, {
	145, 8}, {
	212, 5}, {
	212, 7}, {
	212, 3}, {
	212, 5}, {
	145, 6}, {
	145, 7}, {
	213, 2}, {
	213, 1}, {
	214, 0}, {
	214, 3}, {
	211, 3}, {
	211, 1}, {
	167, 3}, {
	166, 1}, {
	167, 1}, {
	167, 1}, {
	167, 3}, {
	167, 5}, {
	166, 1}, {
	166, 1}, {
	166, 1}, {
	167, 1}, {
	167, 3}, {
	167, 6}, {
	167, 5}, {
	167, 4}, {
	166, 1}, {
	167, 5}, {
	167, 3}, {
	167, 3}, {
	167, 3}, {
	167, 3}, {
	167, 3}, {
	167, 3}, {
	167, 3}, {
	167, 3}, {
	215, 1}, {
	215, 2}, {
	167, 3}, {
	167, 5}, {
	167, 2}, {
	167, 3}, {
	167, 3}, {
	167, 4}, {
	167, 2}, {
	167, 2}, {
	167, 2}, {
	167, 2}, {
	216, 1}, {
	216, 2}, {
	167, 5}, {
	217, 1}, {
	217, 2}, {
	167, 5}, {
	167, 3}, {
	167, 5}, {
	167, 4}, {
	167, 4}, {
	167, 5}, {
	220, 5}, {
	220, 4}, {
	221, 2}, {
	221, 0}, {
	219, 1}, {
	219, 0}, {
	202, 0}, {
	201, 3}, {
	201, 1}, {
	218, 0}, {
	218, 3}, {
	145, 11}, {
	222, 1}, {
	222, 0}, {
	171, 0}, {
	171, 3}, {
	181, 5}, {
	181, 3}, {
	223, 0}, {
	223, 2}, {
	145, 6}, {
	145, 2}, {
	145, 4}, {
	145, 5}, {
	145, 4}, {
	145, 5}, {
	145, 6}, {
	163, 2}, {
	164, 2}, {
	145, 5}, {
	225, 9}, {
	227, 1}, {
	227, 1}, {
	227, 2}, {
	227, 0}, {
	228, 1}, {
	228, 1}, {
	228, 3}, {
	230, 0}, {
	230, 2}, {
	226, 3}, {
	226, 2}, {
	232, 3}, {
	233, 3}, {
	233, 2}, {
	231, 7}, {
	231, 5}, {
	231, 5}, {
	231, 1}, {
	167, 4}, {
	167, 6}, {
	185, 1}, {
	185, 1}, {
	185, 1}, {
	145, 4}, {
	145, 1}, {
	145, 2}, {
	145, 4}, {
	145, 1}, {
	145, 2}, {
	145, 6}, {
	145, 7}, {
	234, 1}, {
	145, 1}, {
	145, 4}, {
	236, 7}, {
	238, 0}, {
	239, 1}, {
	239, 3}, {
	240, 1}, {
	190, 0}, {
	190, 2}, {
	190, 3}, {
	242, 6}, {
	242, 8}, {
	141, 1}, {
	143, 0}, {
	144, 1}, {
	147, 0}, {
	147, 1}, {
	147, 2}, {
	149, 1}, {
	149, 0}, {
	145, 2}, {
	154, 4}, {
	154, 2}, {
	148, 1}, {
	148, 1}, {
	148, 1}, {
	160, 1}, {
	161, 1}, {
	162, 1}, {
	162, 1}, {
	159, 2}, {
	159, 0}, {
	165, 2}, {
	155, 2}, {
	177, 3}, {
	177, 1}, {
	178, 0}, {
	182, 1}, {
	184, 1}, {
	188, 1}, {
	189, 1}, {
	203, 2}, {
	204, 1}, {
	167, 1}, {
	202, 1}, {
	224, 1}, {
	224, 1}, {
	224, 1}, {
	224, 1}, {
	224, 1}, {
	163, 1}, {
	229, 0}, {
	229, 3}, {
	232, 1}, {
	233, 0}, {
	235, 0}, {
	235, 1}, {
	237, 1}, {
	237, 3}, {
	238, 2}, {
	241, 0}, {
	241, 4}, {
241, 2},};

static void yy_accept(yyParser *);	/* Forward Declaration */

/*
 * Perform a reduce action and the shift that must immediately
 * follow the reduce.
 */
static void
yy_reduce(yyParser * yypParser,	/* The parser */
	  unsigned int yyruleno	/* Number of the rule by which to reduce */
    )
{
	int yygoto;		/* The next state */
	int yyact;		/* The next action */
	yyStackEntry *yymsp;	/* The top of the parser's stack */
	int yysize;		/* Amount to pop the stack */
	sqlite3ParserARG_FETCH;
	yymsp = yypParser->yytos;
#ifndef NDEBUG
	if (yyTraceFILE
	    && yyruleno < (int)(sizeof(yyRuleName) / sizeof(yyRuleName[0]))) {
		yysize = yyRuleInfo[yyruleno].nrhs;
		fprintf(yyTraceFILE, "%sReduce [%s], go to state %d.\n",
			yyTracePrompt, yyRuleName[yyruleno],
			yymsp[-yysize].stateno);
	}
#endif				/* NDEBUG */

	/* Check that the stack is large enough to grow by a single entry
	 * if the RHS of the rule is empty.  This ensures that there is room
	 * enough on the stack to push the LHS value
	 */
	if (yyRuleInfo[yyruleno].nrhs == 0) {
#ifdef YYTRACKMAXSTACKDEPTH
		if ((int)(yypParser->yytos - yypParser->yystack) >
		    yypParser->yyhwm) {
			yypParser->yyhwm++;
			assert(yypParser->yyhwm ==
			       (int)(yypParser->yytos - yypParser->yystack));
		}
#endif
#if YYSTACKDEPTH>0
		if (yypParser->yytos >= &yypParser->yystack[YYSTACKDEPTH - 1]) {
			yyStackOverflow(yypParser);
			return;
		}
#else
		if (yypParser->yytos >=
		    &yypParser->yystack[yypParser->yystksz - 1]) {
			if (yyGrowStack(yypParser)) {
				yyStackOverflow(yypParser);
				return;
			}
			yymsp = yypParser->yytos;
		}
#endif
	}

	switch (yyruleno) {
		/* Beginning here are the reduction cases.  A typical example
		 * follows:
		 *   case 0:
		 *  #line <lineno> <grammarfile>
		 *     { ... }           // User supplied code
		 *  #line <lineno> <thisfile>
		 *     break;
		 */
/********** Begin reduce actions **********************************************/
		YYMINORTYPE yylhsminor;
	case 0:		/* ecmd ::= explain cmdx SEMI */
#line 107 "parse.y"
		{
			sqlite3FinishCoding(pParse);
		}
#line 2243 "parse.c"
		break;
	case 1:		/* ecmd ::= SEMI */
#line 108 "parse.y"
		{
			sqlite3ErrorMsg(pParse, "syntax error: empty request");
		}
#line 2250 "parse.c"
		break;
	case 2:		/* explain ::= EXPLAIN */
#line 113 "parse.y"
		{
			pParse->explain = 1;
		}
#line 2255 "parse.c"
		break;
	case 3:		/* explain ::= EXPLAIN QUERY PLAN */
#line 114 "parse.y"
		{
			pParse->explain = 2;
		}
#line 2260 "parse.c"
		break;
	case 4:		/* cmd ::= BEGIN transtype trans_opt */
#line 146 "parse.y"
		{
			sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy58);
		}
#line 2265 "parse.c"
		break;
	case 5:		/* transtype ::= */
#line 151 "parse.y"
		{
			yymsp[1].minor.yy58 = TK_DEFERRED;
		}
#line 2270 "parse.c"
		break;
	case 6:		/* transtype ::= DEFERRED */
#line 152 "parse.y"
		{
			yymsp[0].minor.yy58 = yymsp[0].major;	/*A-overwrites-X */
		}
#line 2275 "parse.c"
		break;
	case 7:		/* cmd ::= COMMIT trans_opt */
	case 8:		/* cmd ::= END trans_opt */
		yytestcase(yyruleno == 8);
#line 153 "parse.y"
		{
			sqlite3CommitTransaction(pParse);
		}
#line 2281 "parse.c"
		break;
	case 9:		/* cmd ::= ROLLBACK trans_opt */
#line 155 "parse.y"
		{
			sqlite3RollbackTransaction(pParse);
		}
#line 2286 "parse.c"
		break;
	case 10:		/* cmd ::= SAVEPOINT nm */
#line 159 "parse.y"
		{
			sqlite3Savepoint(pParse, SAVEPOINT_BEGIN,
					 &yymsp[0].minor.yy0);
		}
#line 2293 "parse.c"
		break;
	case 11:		/* cmd ::= RELEASE savepoint_opt nm */
#line 162 "parse.y"
		{
			sqlite3Savepoint(pParse, SAVEPOINT_RELEASE,
					 &yymsp[0].minor.yy0);
		}
#line 2300 "parse.c"
		break;
	case 12:		/* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 165 "parse.y"
		{
			sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK,
					 &yymsp[0].minor.yy0);
		}
#line 2307 "parse.c"
		break;
	case 13:		/* create_table ::= createkw TABLE ifnotexists nm */
#line 172 "parse.y"
		{
			sqlite3StartTable(pParse, &yymsp[0].minor.yy0, 0, 0, 0,
					  yymsp[-1].minor.yy58);
		}
#line 2314 "parse.c"
		break;
	case 14:		/* createkw ::= CREATE */
#line 175 "parse.y"
		{
			disableLookaside(pParse);
		}
#line 2319 "parse.c"
		break;
	case 15:		/* ifnotexists ::= */
	case 19:		/* table_options ::= */
		yytestcase(yyruleno == 19);
	case 39:		/* autoinc ::= */
		yytestcase(yyruleno == 39);
	case 54:		/* init_deferred_pred_opt ::= */
		yytestcase(yyruleno == 54);
	case 64:		/* defer_subclause_opt ::= */
		yytestcase(yyruleno == 64);
	case 73:		/* ifexists ::= */
		yytestcase(yyruleno == 73);
	case 87:		/* distinct ::= */
		yytestcase(yyruleno == 87);
	case 210:		/* collate ::= */
		yytestcase(yyruleno == 210);
#line 178 "parse.y"
		{
			yymsp[1].minor.yy58 = 0;
		}
#line 2331 "parse.c"
		break;
	case 16:		/* ifnotexists ::= IF NOT EXISTS */
#line 179 "parse.y"
		{
			yymsp[-2].minor.yy58 = 1;
		}
#line 2336 "parse.c"
		break;
	case 17:		/* create_table_args ::= LP columnlist conslist_opt RP table_options */
#line 181 "parse.y"
		{
			sqlite3EndTable(pParse, &yymsp[-2].minor.yy0,
					&yymsp[-1].minor.yy0,
					yymsp[0].minor.yy58, 0);
		}
#line 2343 "parse.c"
		break;
	case 18:		/* create_table_args ::= AS select */
#line 184 "parse.y"
		{
			sqlite3EndTable(pParse, 0, 0, 0, yymsp[0].minor.yy99);
			sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy99);
		}
#line 2351 "parse.c"
		break;
	case 20:		/* table_options ::= WITHOUT nm */
#line 190 "parse.y"
		{
			if (yymsp[0].minor.yy0.n == 5
			    && sqlite3_strnicmp(yymsp[0].minor.yy0.z, "rowid",
						5) == 0) {
				yymsp[-1].minor.yy58 =
				    TF_WithoutRowid | TF_NoVisibleRowid;
			} else {
				yymsp[-1].minor.yy58 = 0;
				sqlite3ErrorMsg(pParse,
						"unknown table option: %.*s",
						yymsp[0].minor.yy0.n,
						yymsp[0].minor.yy0.z);
			}
		}
#line 2363 "parse.c"
		break;
	case 21:		/* columnname ::= nm typetoken */
#line 200 "parse.y"
		{
			sqlite3AddColumn(pParse, &yymsp[-1].minor.yy0,
					 &yymsp[0].minor.yy0);
		}
#line 2368 "parse.c"
		break;
	case 22:		/* typetoken ::= */
	case 57:		/* conslist_opt ::= */
		yytestcase(yyruleno == 57);
	case 93:		/* as ::= */
		yytestcase(yyruleno == 93);
#line 241 "parse.y"
		{
			yymsp[1].minor.yy0.n = 0;
			yymsp[1].minor.yy0.z = 0;
		}
#line 2375 "parse.c"
		break;
	case 23:		/* typetoken ::= typename LP signed RP */
#line 243 "parse.y"
		{
			yymsp[-3].minor.yy0.n =
			    (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] -
				  yymsp[-3].minor.yy0.z);
		}
#line 2382 "parse.c"
		break;
	case 24:		/* typetoken ::= typename LP signed COMMA signed RP */
#line 246 "parse.y"
		{
			yymsp[-5].minor.yy0.n =
			    (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] -
				  yymsp[-5].minor.yy0.z);
		}
#line 2389 "parse.c"
		break;
	case 25:		/* typename ::= typename ID|STRING */
#line 251 "parse.y"
		{
			yymsp[-1].minor.yy0.n =
			    yymsp[0].minor.yy0.n + (int)(yymsp[0].minor.yy0.z -
							 yymsp[-1].minor.yy0.z);
		}
#line 2394 "parse.c"
		break;
	case 26:		/* ccons ::= CONSTRAINT nm */
	case 59:		/* tcons ::= CONSTRAINT nm */
		yytestcase(yyruleno == 59);
#line 260 "parse.y"
		{
			pParse->constraintName = yymsp[0].minor.yy0;
		}
#line 2400 "parse.c"
		break;
	case 27:		/* ccons ::= DEFAULT term */
	case 29:		/* ccons ::= DEFAULT PLUS term */
		yytestcase(yyruleno == 29);
#line 261 "parse.y"
		{
			sqlite3AddDefaultValue(pParse, &yymsp[0].minor.yy190);
		}
#line 2406 "parse.c"
		break;
	case 28:		/* ccons ::= DEFAULT LP expr RP */
#line 262 "parse.y"
		{
			sqlite3AddDefaultValue(pParse, &yymsp[-1].minor.yy190);
		}
#line 2411 "parse.c"
		break;
	case 30:		/* ccons ::= DEFAULT MINUS term */
#line 264 "parse.y"
		{
			ExprSpan v;
			v.pExpr =
			    sqlite3PExpr(pParse, TK_UMINUS,
					 yymsp[0].minor.yy190.pExpr, 0);
			v.zStart = yymsp[-1].minor.yy0.z;
			v.zEnd = yymsp[0].minor.yy190.zEnd;
			sqlite3AddDefaultValue(pParse, &v);
		}
#line 2422 "parse.c"
		break;
	case 31:		/* ccons ::= DEFAULT ID|INDEXED */
#line 271 "parse.y"
		{
			ExprSpan v;
			spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
			sqlite3AddDefaultValue(pParse, &v);
		}
#line 2431 "parse.c"
		break;
	case 32:		/* ccons ::= NOT NULL onconf */
#line 281 "parse.y"
		{
			sqlite3AddNotNull(pParse, yymsp[0].minor.yy58);
		}
#line 2436 "parse.c"
		break;
	case 33:		/* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 283 "parse.y"
		{
			sqlite3AddPrimaryKey(pParse, 0, yymsp[-1].minor.yy58,
					     yymsp[0].minor.yy58,
					     yymsp[-2].minor.yy58);
		}
#line 2441 "parse.c"
		break;
	case 34:		/* ccons ::= UNIQUE onconf */
#line 284 "parse.y"
		{
			sqlite3CreateIndex(pParse, 0, 0, 0, yymsp[0].minor.yy58,
					   0, 0, 0, 0, SQLITE_IDXTYPE_UNIQUE);
		}
#line 2447 "parse.c"
		break;
	case 35:		/* ccons ::= CHECK LP expr RP */
#line 286 "parse.y"
		{
			sqlite3AddCheckConstraint(pParse,
						  yymsp[-1].minor.yy190.pExpr);
		}
#line 2452 "parse.c"
		break;
	case 36:		/* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 288 "parse.y"
		{
			sqlite3CreateForeignKey(pParse, 0, &yymsp[-2].minor.yy0,
						yymsp[-1].minor.yy412,
						yymsp[0].minor.yy58);
		}
#line 2457 "parse.c"
		break;
	case 37:		/* ccons ::= defer_subclause */
#line 289 "parse.y"
		{
			sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy58);
		}
#line 2462 "parse.c"
		break;
	case 38:		/* ccons ::= COLLATE ID|STRING */
#line 290 "parse.y"
		{
			sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);
		}
#line 2467 "parse.c"
		break;
	case 40:		/* autoinc ::= AUTOINCR */
#line 295 "parse.y"
		{
			yymsp[0].minor.yy58 = 1;
		}
#line 2472 "parse.c"
		break;
	case 41:		/* refargs ::= */
#line 303 "parse.y"
		{
			yymsp[1].minor.yy58 = OE_None * 0x0101;	/* EV: R-19803-45884 */
		}
#line 2477 "parse.c"
		break;
	case 42:		/* refargs ::= refargs refarg */
#line 304 "parse.y"
		{
			yymsp[-1].minor.yy58 =
			    (yymsp[-1].minor.yy58 & ~yymsp[0].minor.yy35.
			     mask) | yymsp[0].minor.yy35.value;
		}
#line 2482 "parse.c"
		break;
	case 43:		/* refarg ::= MATCH nm */
#line 306 "parse.y"
		{
			yymsp[-1].minor.yy35.value = 0;
			yymsp[-1].minor.yy35.mask = 0x000000;
		}
#line 2487 "parse.c"
		break;
	case 44:		/* refarg ::= ON INSERT refact */
#line 307 "parse.y"
		{
			yymsp[-2].minor.yy35.value = 0;
			yymsp[-2].minor.yy35.mask = 0x000000;
		}
#line 2492 "parse.c"
		break;
	case 45:		/* refarg ::= ON DELETE refact */
#line 308 "parse.y"
		{
			yymsp[-2].minor.yy35.value = yymsp[0].minor.yy58;
			yymsp[-2].minor.yy35.mask = 0x0000ff;
		}
#line 2497 "parse.c"
		break;
	case 46:		/* refarg ::= ON UPDATE refact */
#line 309 "parse.y"
		{
			yymsp[-2].minor.yy35.value = yymsp[0].minor.yy58 << 8;
			yymsp[-2].minor.yy35.mask = 0x00ff00;
		}
#line 2502 "parse.c"
		break;
	case 47:		/* refact ::= SET NULL */
#line 311 "parse.y"
		{
			yymsp[-1].minor.yy58 = OE_SetNull;	/* EV: R-33326-45252 */
		}
#line 2507 "parse.c"
		break;
	case 48:		/* refact ::= SET DEFAULT */
#line 312 "parse.y"
		{
			yymsp[-1].minor.yy58 = OE_SetDflt;	/* EV: R-33326-45252 */
		}
#line 2512 "parse.c"
		break;
	case 49:		/* refact ::= CASCADE */
#line 313 "parse.y"
		{
			yymsp[0].minor.yy58 = OE_Cascade;	/* EV: R-33326-45252 */
		}
#line 2517 "parse.c"
		break;
	case 50:		/* refact ::= RESTRICT */
#line 314 "parse.y"
		{
			yymsp[0].minor.yy58 = OE_Restrict;	/* EV: R-33326-45252 */
		}
#line 2522 "parse.c"
		break;
	case 51:		/* refact ::= NO ACTION */
#line 315 "parse.y"
		{
			yymsp[-1].minor.yy58 = OE_None;	/* EV: R-33326-45252 */
		}
#line 2527 "parse.c"
		break;
	case 52:		/* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 317 "parse.y"
		{
			yymsp[-2].minor.yy58 = 0;
		}
#line 2532 "parse.c"
		break;
	case 53:		/* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
	case 68:		/* orconf ::= OR resolvetype */
		yytestcase(yyruleno == 68);
	case 139:		/* insert_cmd ::= INSERT orconf */
		yytestcase(yyruleno == 139);
#line 318 "parse.y"
		{
			yymsp[-1].minor.yy58 = yymsp[0].minor.yy58;
		}
#line 2539 "parse.c"
		break;
	case 55:		/* init_deferred_pred_opt ::= INITIALLY DEFERRED */
	case 72:		/* ifexists ::= IF EXISTS */
		yytestcase(yyruleno == 72);
	case 182:		/* between_op ::= NOT BETWEEN */
		yytestcase(yyruleno == 182);
	case 185:		/* in_op ::= NOT IN */
		yytestcase(yyruleno == 185);
	case 211:		/* collate ::= COLLATE ID|STRING */
		yytestcase(yyruleno == 211);
#line 321 "parse.y"
		{
			yymsp[-1].minor.yy58 = 1;
		}
#line 2548 "parse.c"
		break;
	case 56:		/* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 322 "parse.y"
		{
			yymsp[-1].minor.yy58 = 0;
		}
#line 2553 "parse.c"
		break;
	case 58:		/* tconscomma ::= COMMA */
#line 328 "parse.y"
		{
			pParse->constraintName.n = 0;
		}
#line 2558 "parse.c"
		break;
	case 60:		/* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 332 "parse.y"
		{
			sqlite3AddPrimaryKey(pParse, yymsp[-3].minor.yy412,
					     yymsp[0].minor.yy58,
					     yymsp[-2].minor.yy58, 0);
		}
#line 2563 "parse.c"
		break;
	case 61:		/* tcons ::= UNIQUE LP sortlist RP onconf */
#line 334 "parse.y"
		{
			sqlite3CreateIndex(pParse, 0, 0, yymsp[-2].minor.yy412,
					   yymsp[0].minor.yy58, 0, 0, 0, 0,
					   SQLITE_IDXTYPE_UNIQUE);
		}
#line 2569 "parse.c"
		break;
	case 62:		/* tcons ::= CHECK LP expr RP onconf */
#line 337 "parse.y"
		{
			sqlite3AddCheckConstraint(pParse,
						  yymsp[-2].minor.yy190.pExpr);
		}
#line 2574 "parse.c"
		break;
	case 63:		/* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 339 "parse.y"
		{
			sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy412,
						&yymsp[-3].minor.yy0,
						yymsp[-2].minor.yy412,
						yymsp[-1].minor.yy58);
			sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy58);
		}
#line 2582 "parse.c"
		break;
	case 65:		/* onconf ::= */
	case 67:		/* orconf ::= */
		yytestcase(yyruleno == 67);
#line 353 "parse.y"
		{
			yymsp[1].minor.yy58 = OE_Default;
		}
#line 2588 "parse.c"
		break;
	case 66:		/* onconf ::= ON CONFLICT resolvetype */
#line 354 "parse.y"
		{
			yymsp[-2].minor.yy58 = yymsp[0].minor.yy58;
		}
#line 2593 "parse.c"
		break;
	case 69:		/* resolvetype ::= IGNORE */
#line 358 "parse.y"
		{
			yymsp[0].minor.yy58 = OE_Ignore;
		}
#line 2598 "parse.c"
		break;
	case 70:		/* resolvetype ::= REPLACE */
	case 140:		/* insert_cmd ::= REPLACE */
		yytestcase(yyruleno == 140);
#line 359 "parse.y"
		{
			yymsp[0].minor.yy58 = OE_Replace;
		}
#line 2604 "parse.c"
		break;
	case 71:		/* cmd ::= DROP TABLE ifexists fullname */
#line 363 "parse.y"
		{
			sqlite3DropTable(pParse, yymsp[0].minor.yy367, 0,
					 yymsp[-1].minor.yy58);
		}
#line 2611 "parse.c"
		break;
	case 74:		/* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 374 "parse.y"
		{
			sqlite3CreateView(pParse, &yymsp[-6].minor.yy0,
					  &yymsp[-3].minor.yy0,
					  yymsp[-2].minor.yy412,
					  yymsp[0].minor.yy99,
					  yymsp[-4].minor.yy58);
		}
#line 2618 "parse.c"
		break;
	case 75:		/* cmd ::= DROP VIEW ifexists fullname */
#line 377 "parse.y"
		{
			sqlite3DropTable(pParse, yymsp[0].minor.yy367, 1,
					 yymsp[-1].minor.yy58);
		}
#line 2625 "parse.c"
		break;
	case 76:		/* cmd ::= select */
#line 384 "parse.y"
		{
			SelectDest dest = { SRT_Output, 0, 0, 0, 0, 0 };
			sqlite3Select(pParse, yymsp[0].minor.yy99, &dest);
			sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy99);
		}
#line 2634 "parse.c"
		break;
	case 77:		/* select ::= with selectnowith */
#line 421 "parse.y"
		{
			Select *p = yymsp[0].minor.yy99;
			if (p) {
				p->pWith = yymsp[-1].minor.yy91;
				parserDoubleLinkSelect(pParse, p);
			} else {
				sqlite3WithDelete(pParse->db,
						  yymsp[-1].minor.yy91);
			}
			yymsp[-1].minor.yy99 = p;	/*A-overwrites-W */
		}
#line 2648 "parse.c"
		break;
	case 78:		/* selectnowith ::= selectnowith multiselect_op oneselect */
#line 434 "parse.y"
		{
			Select *pRhs = yymsp[0].minor.yy99;
			Select *pLhs = yymsp[-2].minor.yy99;
			if (pRhs && pRhs->pPrior) {
				SrcList *pFrom;
				Token x;
				x.n = 0;
				parserDoubleLinkSelect(pParse, pRhs);
				pFrom =
				    sqlite3SrcListAppendFromTerm(pParse, 0, 0,
								 0, &x, pRhs, 0,
								 0);
				pRhs =
				    sqlite3SelectNew(pParse, 0, pFrom, 0, 0, 0,
						     0, 0, 0, 0);
			}
			if (pRhs) {
				pRhs->op = (u8) yymsp[-1].minor.yy58;
				pRhs->pPrior = pLhs;
				if (ALWAYS(pLhs))
					pLhs->selFlags &= ~SF_MultiValue;
				pRhs->selFlags &= ~SF_MultiValue;
				if (yymsp[-1].minor.yy58 != TK_ALL)
					pParse->hasCompound = 1;
			} else {
				sqlite3SelectDelete(pParse->db, pLhs);
			}
			yymsp[-2].minor.yy99 = pRhs;
		}
#line 2674 "parse.c"
		break;
	case 79:		/* multiselect_op ::= UNION */
	case 81:		/* multiselect_op ::= EXCEPT|INTERSECT */
		yytestcase(yyruleno == 81);
#line 457 "parse.y"
		{
			yymsp[0].minor.yy58 = yymsp[0].major;	/*A-overwrites-OP */
		}
#line 2680 "parse.c"
		break;
	case 80:		/* multiselect_op ::= UNION ALL */
#line 458 "parse.y"
		{
			yymsp[-1].minor.yy58 = TK_ALL;
		}
#line 2685 "parse.c"
		break;
	case 82:		/* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 462 "parse.y"
		{
#if SELECTTRACE_ENABLED
			Token s = yymsp[-8].minor.yy0;	/*A-overwrites-S */
#endif
			yymsp[-8].minor.yy99 =
			    sqlite3SelectNew(pParse, yymsp[-6].minor.yy412,
					     yymsp[-5].minor.yy367,
					     yymsp[-4].minor.yy44,
					     yymsp[-3].minor.yy412,
					     yymsp[-2].minor.yy44,
					     yymsp[-1].minor.yy412,
					     yymsp[-7].minor.yy58,
					     yymsp[0].minor.yy112.pLimit,
					     yymsp[0].minor.yy112.pOffset);
#if SELECTTRACE_ENABLED
			/* Populate the Select.zSelName[] string that is used to help with
			 * query planner debugging, to differentiate between multiple Select
			 * objects in a complex query.
			 *
			 * If the SELECT keyword is immediately followed by a C-style comment
			 * then extract the first few alphanumeric characters from within that
			 * comment to be the zSelName value.  Otherwise, the label is #N where
			 * is an integer that is incremented with each SELECT statement seen.
			 */
			if (yymsp[-8].minor.yy99 != 0) {
				const char *z = s.z + 6;
				int i;
				sqlite3_snprintf(sizeof
						 (yymsp[-8].minor.yy99->
						  zSelName),
						 yymsp[-8].minor.yy99->zSelName,
						 "#%d", ++pParse->nSelect);
				while (z[0] == ' ')
					z++;
				if (z[0] == '/' && z[1] == '*') {
					z += 2;
					while (z[0] == ' ')
						z++;
					for (i = 0; sqlite3Isalnum(z[i]); i++) {
					}
					sqlite3_snprintf(sizeof
							 (yymsp[-8].minor.yy99->
							  zSelName),
							 yymsp[-8].minor.yy99->
							 zSelName, "%.*s", i,
							 z);
				}
			}
#endif				/* SELECTRACE_ENABLED */
		}
#line 2719 "parse.c"
		break;
	case 83:		/* values ::= VALUES LP nexprlist RP */
#line 496 "parse.y"
		{
			yymsp[-3].minor.yy99 =
			    sqlite3SelectNew(pParse, yymsp[-1].minor.yy412, 0,
					     0, 0, 0, 0, SF_Values, 0, 0);
		}
#line 2726 "parse.c"
		break;
	case 84:		/* values ::= values COMMA LP exprlist RP */
#line 499 "parse.y"
		{
			Select *pRight, *pLeft = yymsp[-4].minor.yy99;
			pRight =
			    sqlite3SelectNew(pParse, yymsp[-1].minor.yy412, 0,
					     0, 0, 0, 0,
					     SF_Values | SF_MultiValue, 0, 0);
			if (ALWAYS(pLeft))
				pLeft->selFlags &= ~SF_MultiValue;
			if (pRight) {
				pRight->op = TK_ALL;
				pRight->pPrior = pLeft;
				yymsp[-4].minor.yy99 = pRight;
			} else {
				yymsp[-4].minor.yy99 = pLeft;
			}
		}
#line 2742 "parse.c"
		break;
	case 85:		/* distinct ::= DISTINCT */
#line 516 "parse.y"
		{
			yymsp[0].minor.yy58 = SF_Distinct;
		}
#line 2747 "parse.c"
		break;
	case 86:		/* distinct ::= ALL */
#line 517 "parse.y"
		{
			yymsp[0].minor.yy58 = SF_All;
		}
#line 2752 "parse.c"
		break;
	case 88:		/* sclp ::= */
	case 114:		/* orderby_opt ::= */
		yytestcase(yyruleno == 114);
	case 121:		/* groupby_opt ::= */
		yytestcase(yyruleno == 121);
	case 198:		/* exprlist ::= */
		yytestcase(yyruleno == 198);
	case 201:		/* paren_exprlist ::= */
		yytestcase(yyruleno == 201);
	case 206:		/* eidlist_opt ::= */
		yytestcase(yyruleno == 206);
#line 530 "parse.y"
		{
			yymsp[1].minor.yy412 = 0;
		}
#line 2762 "parse.c"
		break;
	case 89:		/* selcollist ::= sclp expr as */
#line 531 "parse.y"
		{
			yymsp[-2].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy412,
						  yymsp[-1].minor.yy190.pExpr);
			if (yymsp[0].minor.yy0.n > 0)
				sqlite3ExprListSetName(pParse,
						       yymsp[-2].minor.yy412,
						       &yymsp[0].minor.yy0, 1);
			sqlite3ExprListSetSpan(pParse, yymsp[-2].minor.yy412,
					       &yymsp[-1].minor.yy190);
		}
#line 2771 "parse.c"
		break;
	case 90:		/* selcollist ::= sclp STAR */
#line 536 "parse.y"
		{
			Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
			yymsp[-1].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy412,
						  p);
		}
#line 2779 "parse.c"
		break;
	case 91:		/* selcollist ::= sclp nm DOT STAR */
#line 540 "parse.y"
		{
			Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
			Expr *pLeft =
			    sqlite3ExprAlloc(pParse->db, TK_ID,
					     &yymsp[-2].minor.yy0, 1);
			Expr *pDot =
			    sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
			yymsp[-3].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy412,
						  pDot);
		}
#line 2789 "parse.c"
		break;
	case 92:		/* as ::= AS nm */
	case 219:		/* plus_num ::= PLUS INTEGER|FLOAT */
		yytestcase(yyruleno == 219);
	case 220:		/* minus_num ::= MINUS INTEGER|FLOAT */
		yytestcase(yyruleno == 220);
#line 551 "parse.y"
		{
			yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;
		}
#line 2796 "parse.c"
		break;
	case 94:		/* from ::= */
#line 565 "parse.y"
		{
			yymsp[1].minor.yy367 =
			    sqlite3DbMallocZero(pParse->db,
						sizeof(*yymsp[1].minor.yy367));
		}
#line 2801 "parse.c"
		break;
	case 95:		/* from ::= FROM seltablist */
#line 566 "parse.y"
		{
			yymsp[-1].minor.yy367 = yymsp[0].minor.yy367;
			sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy367);
		}
#line 2809 "parse.c"
		break;
	case 96:		/* stl_prefix ::= seltablist joinop */
#line 574 "parse.y"
		{
			if (ALWAYS
			    (yymsp[-1].minor.yy367
			     && yymsp[-1].minor.yy367->nSrc > 0))
				yymsp[-1].minor.yy367->a[yymsp[-1].minor.yy367->
							 nSrc - 1].fg.jointype =
				    (u8) yymsp[0].minor.yy58;
		}
#line 2816 "parse.c"
		break;
	case 97:		/* stl_prefix ::= */
#line 577 "parse.y"
		{
			yymsp[1].minor.yy367 = 0;
		}
#line 2821 "parse.c"
		break;
	case 98:		/* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 579 "parse.y"
		{
			yymsp[-5].minor.yy367 =
			    sqlite3SrcListAppendFromTerm(pParse,
							 yymsp[-5].minor.yy367,
							 &yymsp[-4].minor.yy0,
							 0,
							 &yymsp[-3].minor.yy0,
							 0,
							 yymsp[-1].minor.yy44,
							 yymsp[0].minor.yy258);
			sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy367,
						&yymsp[-2].minor.yy0);
		}
#line 2829 "parse.c"
		break;
	case 99:		/* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 584 "parse.y"
		{
			yymsp[-7].minor.yy367 =
			    sqlite3SrcListAppendFromTerm(pParse,
							 yymsp[-7].minor.yy367,
							 &yymsp[-6].minor.yy0,
							 0,
							 &yymsp[-2].minor.yy0,
							 0,
							 yymsp[-1].minor.yy44,
							 yymsp[0].minor.yy258);
			sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy367,
					       yymsp[-4].minor.yy412);
		}
#line 2837 "parse.c"
		break;
	case 100:		/* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 590 "parse.y"
		{
			yymsp[-6].minor.yy367 =
			    sqlite3SrcListAppendFromTerm(pParse,
							 yymsp[-6].minor.yy367,
							 0, 0,
							 &yymsp[-2].minor.yy0,
							 yymsp[-4].minor.yy99,
							 yymsp[-1].minor.yy44,
							 yymsp[0].minor.yy258);
		}
#line 2844 "parse.c"
		break;
	case 101:		/* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 594 "parse.y"
		{
			if (yymsp[-6].minor.yy367 == 0
			    && yymsp[-2].minor.yy0.n == 0
			    && yymsp[-1].minor.yy44 == 0
			    && yymsp[0].minor.yy258 == 0) {
				yymsp[-6].minor.yy367 = yymsp[-4].minor.yy367;
			} else if (yymsp[-4].minor.yy367->nSrc == 1) {
				yymsp[-6].minor.yy367 =
				    sqlite3SrcListAppendFromTerm(pParse,
								 yymsp[-6].
								 minor.yy367, 0,
								 0,
								 &yymsp[-2].
								 minor.yy0, 0,
								 yymsp[-1].
								 minor.yy44,
								 yymsp[0].minor.
								 yy258);
				if (yymsp[-6].minor.yy367) {
					struct SrcList_item *pNew =
					    &yymsp[-6].minor.yy367->a[yymsp[-6].
								      minor.
								      yy367->
								      nSrc - 1];
					struct SrcList_item *pOld =
					    yymsp[-4].minor.yy367->a;
					pNew->zName = pOld->zName;
					pNew->zDatabase = pOld->zDatabase;
					pNew->pSelect = pOld->pSelect;
					pOld->zName = pOld->zDatabase = 0;
					pOld->pSelect = 0;
				}
				sqlite3SrcListDelete(pParse->db,
						     yymsp[-4].minor.yy367);
			} else {
				Select *pSubquery;
				sqlite3SrcListShiftJoinType(yymsp[-4].minor.
							    yy367);
				pSubquery =
				    sqlite3SelectNew(pParse, 0,
						     yymsp[-4].minor.yy367, 0,
						     0, 0, 0, SF_NestedFrom, 0,
						     0);
				yymsp[-6].minor.yy367 =
				    sqlite3SrcListAppendFromTerm(pParse,
								 yymsp[-6].
								 minor.yy367, 0,
								 0,
								 &yymsp[-2].
								 minor.yy0,
								 pSubquery,
								 yymsp[-1].
								 minor.yy44,
								 yymsp[0].minor.
								 yy258);
			}
		}
#line 2870 "parse.c"
		break;
	case 102:		/* fullname ::= nm */
#line 621 "parse.y"
		{
			yymsp[0].minor.yy367 = sqlite3SrcListAppend(pParse->db, 0, &yymsp[0].minor.yy0, 0);	/*A-overwrites-X */
		}
#line 2875 "parse.c"
		break;
	case 103:		/* joinop ::= COMMA|JOIN */
#line 624 "parse.y"
		{
			yymsp[0].minor.yy58 = JT_INNER;
		}
#line 2880 "parse.c"
		break;
	case 104:		/* joinop ::= JOIN_KW JOIN */
#line 626 "parse.y"
		{
			yymsp[-1].minor.yy58 = sqlite3JoinType(pParse, &yymsp[-1].minor.yy0, 0, 0);	/*X-overwrites-A */
		}
#line 2885 "parse.c"
		break;
	case 105:		/* joinop ::= JOIN_KW nm JOIN */
#line 628 "parse.y"
		{
			yymsp[-2].minor.yy58 = sqlite3JoinType(pParse, &yymsp[-2].minor.yy0, &yymsp[-1].minor.yy0, 0);	/*X-overwrites-A */
		}
#line 2890 "parse.c"
		break;
	case 106:		/* joinop ::= JOIN_KW nm nm JOIN */
#line 630 "parse.y"
		{
			yymsp[-3].minor.yy58 = sqlite3JoinType(pParse, &yymsp[-3].minor.yy0, &yymsp[-2].minor.yy0, &yymsp[-1].minor.yy0);	/*X-overwrites-A */
		}
#line 2895 "parse.c"
		break;
	case 107:		/* on_opt ::= ON expr */
	case 124:		/* having_opt ::= HAVING expr */
		yytestcase(yyruleno == 124);
	case 131:		/* where_opt ::= WHERE expr */
		yytestcase(yyruleno == 131);
	case 194:		/* case_else ::= ELSE expr */
		yytestcase(yyruleno == 194);
#line 634 "parse.y"
		{
			yymsp[-1].minor.yy44 = yymsp[0].minor.yy190.pExpr;
		}
#line 2903 "parse.c"
		break;
	case 108:		/* on_opt ::= */
	case 123:		/* having_opt ::= */
		yytestcase(yyruleno == 123);
	case 130:		/* where_opt ::= */
		yytestcase(yyruleno == 130);
	case 195:		/* case_else ::= */
		yytestcase(yyruleno == 195);
	case 197:		/* case_operand ::= */
		yytestcase(yyruleno == 197);
#line 635 "parse.y"
		{
			yymsp[1].minor.yy44 = 0;
		}
#line 2912 "parse.c"
		break;
	case 109:		/* indexed_opt ::= */
#line 648 "parse.y"
		{
			yymsp[1].minor.yy0.z = 0;
			yymsp[1].minor.yy0.n = 0;
		}
#line 2917 "parse.c"
		break;
	case 110:		/* indexed_opt ::= INDEXED BY nm */
#line 649 "parse.y"
		{
			yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
		}
#line 2922 "parse.c"
		break;
	case 111:		/* indexed_opt ::= NOT INDEXED */
#line 650 "parse.y"
		{
			yymsp[-1].minor.yy0.z = 0;
			yymsp[-1].minor.yy0.n = 1;
		}
#line 2927 "parse.c"
		break;
	case 112:		/* using_opt ::= USING LP idlist RP */
#line 654 "parse.y"
		{
			yymsp[-3].minor.yy258 = yymsp[-1].minor.yy258;
		}
#line 2932 "parse.c"
		break;
	case 113:		/* using_opt ::= */
	case 141:		/* idlist_opt ::= */
		yytestcase(yyruleno == 141);
#line 655 "parse.y"
		{
			yymsp[1].minor.yy258 = 0;
		}
#line 2938 "parse.c"
		break;
	case 115:		/* orderby_opt ::= ORDER BY sortlist */
	case 122:		/* groupby_opt ::= GROUP BY nexprlist */
		yytestcase(yyruleno == 122);
#line 669 "parse.y"
		{
			yymsp[-2].minor.yy412 = yymsp[0].minor.yy412;
		}
#line 2944 "parse.c"
		break;
	case 116:		/* sortlist ::= sortlist COMMA expr sortorder */
#line 670 "parse.y"
		{
			yymsp[-3].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy412,
						  yymsp[-1].minor.yy190.pExpr);
			sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy412,
						    yymsp[0].minor.yy58);
		}
#line 2952 "parse.c"
		break;
	case 117:		/* sortlist ::= expr sortorder */
#line 674 "parse.y"
		{
			yymsp[-1].minor.yy412 = sqlite3ExprListAppend(pParse, 0, yymsp[-1].minor.yy190.pExpr);	/*A-overwrites-Y */
			sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy412,
						    yymsp[0].minor.yy58);
		}
#line 2960 "parse.c"
		break;
	case 118:		/* sortorder ::= ASC */
#line 681 "parse.y"
		{
			yymsp[0].minor.yy58 = SQLITE_SO_ASC;
		}
#line 2965 "parse.c"
		break;
	case 119:		/* sortorder ::= DESC */
#line 682 "parse.y"
		{
			yymsp[0].minor.yy58 = SQLITE_SO_DESC;
		}
#line 2970 "parse.c"
		break;
	case 120:		/* sortorder ::= */
#line 683 "parse.y"
		{
			yymsp[1].minor.yy58 = SQLITE_SO_UNDEFINED;
		}
#line 2975 "parse.c"
		break;
	case 125:		/* limit_opt ::= */
#line 708 "parse.y"
		{
			yymsp[1].minor.yy112.pLimit = 0;
			yymsp[1].minor.yy112.pOffset = 0;
		}
#line 2980 "parse.c"
		break;
	case 126:		/* limit_opt ::= LIMIT expr */
#line 709 "parse.y"
		{
			yymsp[-1].minor.yy112.pLimit =
			    yymsp[0].minor.yy190.pExpr;
			yymsp[-1].minor.yy112.pOffset = 0;
		}
#line 2985 "parse.c"
		break;
	case 127:		/* limit_opt ::= LIMIT expr OFFSET expr */
#line 711 "parse.y"
		{
			yymsp[-3].minor.yy112.pLimit =
			    yymsp[-2].minor.yy190.pExpr;
			yymsp[-3].minor.yy112.pOffset =
			    yymsp[0].minor.yy190.pExpr;
		}
#line 2990 "parse.c"
		break;
	case 128:		/* limit_opt ::= LIMIT expr COMMA expr */
#line 713 "parse.y"
		{
			yymsp[-3].minor.yy112.pOffset =
			    yymsp[-2].minor.yy190.pExpr;
			yymsp[-3].minor.yy112.pLimit =
			    yymsp[0].minor.yy190.pExpr;
		}
#line 2995 "parse.c"
		break;
	case 129:		/* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 730 "parse.y"
		{
			sqlite3WithPush(pParse, yymsp[-5].minor.yy91, 1);
			sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy367,
						&yymsp[-1].minor.yy0);
			sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
			/* Instruct SQL to initate Tarantool's transaction.  */
			pParse->initiateTTrans = true;
			sqlite3DeleteFrom(pParse, yymsp[-2].minor.yy367,
					  yymsp[0].minor.yy44);
		}
#line 3007 "parse.c"
		break;
	case 132:		/* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 763 "parse.y"
		{
			sqlite3WithPush(pParse, yymsp[-7].minor.yy91, 1);
			sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy367,
						&yymsp[-3].minor.yy0);
			sqlite3ExprListCheckLength(pParse,
						   yymsp[-1].minor.yy412,
						   "set list");
			sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
			/* Instruct SQL to initate Tarantool's transaction.  */
			pParse->initiateTTrans = true;
			sqlite3Update(pParse, yymsp[-4].minor.yy367,
				      yymsp[-1].minor.yy412,
				      yymsp[0].minor.yy44,
				      yymsp[-5].minor.yy58);
		}
#line 3020 "parse.c"
		break;
	case 133:		/* setlist ::= setlist COMMA nm EQ expr */
#line 777 "parse.y"
		{
			yymsp[-4].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy412,
						  yymsp[0].minor.yy190.pExpr);
			sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy412,
					       &yymsp[-2].minor.yy0, 1);
		}
#line 3028 "parse.c"
		break;
	case 134:		/* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 781 "parse.y"
		{
			yymsp[-6].minor.yy412 =
			    sqlite3ExprListAppendVector(pParse,
							yymsp[-6].minor.yy412,
							yymsp[-3].minor.yy258,
							yymsp[0].minor.yy190.
							pExpr);
		}
#line 3035 "parse.c"
		break;
	case 135:		/* setlist ::= nm EQ expr */
#line 784 "parse.y"
		{
			yylhsminor.yy412 =
			    sqlite3ExprListAppend(pParse, 0,
						  yymsp[0].minor.yy190.pExpr);
			sqlite3ExprListSetName(pParse, yylhsminor.yy412,
					       &yymsp[-2].minor.yy0, 1);
		}
#line 3043 "parse.c"
		yymsp[-2].minor.yy412 = yylhsminor.yy412;
		break;
	case 136:		/* setlist ::= LP idlist RP EQ expr */
#line 788 "parse.y"
		{
			yymsp[-4].minor.yy412 =
			    sqlite3ExprListAppendVector(pParse, 0,
							yymsp[-3].minor.yy258,
							yymsp[0].minor.yy190.
							pExpr);
		}
#line 3051 "parse.c"
		break;
	case 137:		/* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 794 "parse.y"
		{
			sqlite3WithPush(pParse, yymsp[-5].minor.yy91, 1);
			sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
			/* Instruct SQL to initate Tarantool's transaction.  */
			pParse->initiateTTrans = true;
			sqlite3Insert(pParse, yymsp[-2].minor.yy367,
				      yymsp[0].minor.yy99,
				      yymsp[-1].minor.yy258,
				      yymsp[-4].minor.yy58);
		}
#line 3062 "parse.c"
		break;
	case 138:		/* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 802 "parse.y"
		{
			sqlite3WithPush(pParse, yymsp[-6].minor.yy91, 1);
			sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
			/* Instruct SQL to initate Tarantool's transaction.  */
			pParse->initiateTTrans = true;
			sqlite3Insert(pParse, yymsp[-3].minor.yy367, 0,
				      yymsp[-2].minor.yy258,
				      yymsp[-5].minor.yy58);
		}
#line 3073 "parse.c"
		break;
	case 142:		/* idlist_opt ::= LP idlist RP */
#line 820 "parse.y"
		{
			yymsp[-2].minor.yy258 = yymsp[-1].minor.yy258;
		}
#line 3078 "parse.c"
		break;
	case 143:		/* idlist ::= idlist COMMA nm */
#line 822 "parse.y"
		{
			yymsp[-2].minor.yy258 =
			    sqlite3IdListAppend(pParse->db,
						yymsp[-2].minor.yy258,
						&yymsp[0].minor.yy0);
		}
#line 3083 "parse.c"
		break;
	case 144:		/* idlist ::= nm */
#line 824 "parse.y"
		{
			yymsp[0].minor.yy258 = sqlite3IdListAppend(pParse->db, 0, &yymsp[0].minor.yy0);	/*A-overwrites-Y */
		}
#line 3088 "parse.c"
		break;
	case 145:		/* expr ::= LP expr RP */
#line 874 "parse.y"
		{
			spanSet(&yymsp[-2].minor.yy190, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-B */
			yymsp[-2].minor.yy190.pExpr =
			    yymsp[-1].minor.yy190.pExpr;
		}
#line 3093 "parse.c"
		break;
	case 146:		/* term ::= NULL */
	case 151:		/* term ::= FLOAT|BLOB */
		yytestcase(yyruleno == 151);
	case 152:		/* term ::= STRING */
		yytestcase(yyruleno == 152);
#line 875 "parse.y"
		{
			spanExpr(&yymsp[0].minor.yy190, pParse, yymsp[0].major, yymsp[0].minor.yy0);	/*A-overwrites-X */
		}
#line 3100 "parse.c"
		break;
	case 147:		/* expr ::= ID|INDEXED */
	case 148:		/* expr ::= JOIN_KW */
		yytestcase(yyruleno == 148);
#line 876 "parse.y"
		{
			spanExpr(&yymsp[0].minor.yy190, pParse, TK_ID, yymsp[0].minor.yy0);	/*A-overwrites-X */
		}
#line 3106 "parse.c"
		break;
	case 149:		/* expr ::= nm DOT nm */
#line 878 "parse.y"
		{
			Expr *temp1 =
			    sqlite3ExprAlloc(pParse->db, TK_ID,
					     &yymsp[-2].minor.yy0, 1);
			Expr *temp2 =
			    sqlite3ExprAlloc(pParse->db, TK_ID,
					     &yymsp[0].minor.yy0, 1);
			spanSet(&yymsp[-2].minor.yy190, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-X */
			yymsp[-2].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
		}
#line 3116 "parse.c"
		break;
	case 150:		/* expr ::= nm DOT nm DOT nm */
#line 884 "parse.y"
		{
			Expr *temp1 =
			    sqlite3ExprAlloc(pParse->db, TK_ID,
					     &yymsp[-4].minor.yy0, 1);
			Expr *temp2 =
			    sqlite3ExprAlloc(pParse->db, TK_ID,
					     &yymsp[-2].minor.yy0, 1);
			Expr *temp3 =
			    sqlite3ExprAlloc(pParse->db, TK_ID,
					     &yymsp[0].minor.yy0, 1);
			Expr *temp4 =
			    sqlite3PExpr(pParse, TK_DOT, temp2, temp3);
			spanSet(&yymsp[-4].minor.yy190, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-X */
			yymsp[-4].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_DOT, temp1, temp4);
		}
#line 3128 "parse.c"
		break;
	case 153:		/* term ::= INTEGER */
#line 894 "parse.y"
		{
			yylhsminor.yy190.pExpr =
			    sqlite3ExprAlloc(pParse->db, TK_INTEGER,
					     &yymsp[0].minor.yy0, 1);
			yylhsminor.yy190.zStart = yymsp[0].minor.yy0.z;
			yylhsminor.yy190.zEnd =
			    yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
			if (yylhsminor.yy190.pExpr)
				yylhsminor.yy190.pExpr->flags |= EP_Leaf;
		}
#line 3138 "parse.c"
		yymsp[0].minor.yy190 = yylhsminor.yy190;
		break;
	case 154:		/* expr ::= VARIABLE */
#line 900 "parse.y"
		{
			if (!
			    (yymsp[0].minor.yy0.z[0] == '#'
			     && sqlite3Isdigit(yymsp[0].minor.yy0.z[1]))) {
				u32 n = yymsp[0].minor.yy0.n;
				spanExpr(&yymsp[0].minor.yy190, pParse,
					 TK_VARIABLE, yymsp[0].minor.yy0);
				sqlite3ExprAssignVarNumber(pParse,
							   yymsp[0].minor.yy190.
							   pExpr, n);
			} else {
				/* When doing a nested parse, one can include terms in an expression
				 * that look like this:   #1 #2 ...  These terms refer to registers
				 * in the virtual machine.  #N is the N-th register.
				 */
				Token t = yymsp[0].minor.yy0;	/*A-overwrites-X */
				assert(t.n >= 2);
				spanSet(&yymsp[0].minor.yy190, &t, &t);
				if (pParse->nested == 0) {
					sqlite3ErrorMsg(pParse,
							"near \"%T\": syntax error",
							&t);
					yymsp[0].minor.yy190.pExpr = 0;
				} else {
					yymsp[0].minor.yy190.pExpr =
					    sqlite3PExpr(pParse, TK_REGISTER, 0,
							 0);
					if (yymsp[0].minor.yy190.pExpr)
						sqlite3GetInt32(&t.z[1],
								&yymsp[0].minor.
								yy190.pExpr->
								iTable);
				}
			}
		}
#line 3164 "parse.c"
		break;
	case 155:		/* expr ::= expr COLLATE ID|STRING */
#line 921 "parse.y"
		{
			yymsp[-2].minor.yy190.pExpr =
			    sqlite3ExprAddCollateToken(pParse,
						       yymsp[-2].minor.yy190.
						       pExpr,
						       &yymsp[0].minor.yy0, 1);
			yymsp[-2].minor.yy190.zEnd =
			    &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
		}
#line 3172 "parse.c"
		break;
	case 156:		/* expr ::= CAST LP expr AS typetoken RP */
#line 926 "parse.y"
		{
			spanSet(&yymsp[-5].minor.yy190, &yymsp[-5].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-X */
			yymsp[-5].minor.yy190.pExpr =
			    sqlite3ExprAlloc(pParse->db, TK_CAST,
					     &yymsp[-1].minor.yy0, 1);
			sqlite3ExprAttachSubtrees(pParse->db,
						  yymsp[-5].minor.yy190.pExpr,
						  yymsp[-3].minor.yy190.pExpr,
						  0);
		}
#line 3181 "parse.c"
		break;
	case 157:		/* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 932 "parse.y"
		{
			if (yymsp[-1].minor.yy412
			    && yymsp[-1].minor.yy412->nExpr >
			    pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG]) {
				sqlite3ErrorMsg(pParse,
						"too many arguments on function %T",
						&yymsp[-4].minor.yy0);
			}
			yylhsminor.yy190.pExpr =
			    sqlite3ExprFunction(pParse, yymsp[-1].minor.yy412,
						&yymsp[-4].minor.yy0);
			spanSet(&yylhsminor.yy190, &yymsp[-4].minor.yy0,
				&yymsp[0].minor.yy0);
			if (yymsp[-2].minor.yy58 == SF_Distinct
			    && yylhsminor.yy190.pExpr) {
				yylhsminor.yy190.pExpr->flags |= EP_Distinct;
			}
		}
#line 3195 "parse.c"
		yymsp[-4].minor.yy190 = yylhsminor.yy190;
		break;
	case 158:		/* expr ::= ID|INDEXED LP STAR RP */
#line 942 "parse.y"
		{
			yylhsminor.yy190.pExpr =
			    sqlite3ExprFunction(pParse, 0,
						&yymsp[-3].minor.yy0);
			spanSet(&yylhsminor.yy190, &yymsp[-3].minor.yy0,
				&yymsp[0].minor.yy0);
		}
#line 3204 "parse.c"
		yymsp[-3].minor.yy190 = yylhsminor.yy190;
		break;
	case 159:		/* term ::= CTIME_KW */
#line 946 "parse.y"
		{
			yylhsminor.yy190.pExpr =
			    sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
			spanSet(&yylhsminor.yy190, &yymsp[0].minor.yy0,
				&yymsp[0].minor.yy0);
		}
#line 3213 "parse.c"
		yymsp[0].minor.yy190 = yylhsminor.yy190;
		break;
	case 160:		/* expr ::= LP nexprlist COMMA expr RP */
#line 975 "parse.y"
		{
			ExprList *pList =
			    sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy412,
						  yymsp[-1].minor.yy190.pExpr);
			yylhsminor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
			if (yylhsminor.yy190.pExpr) {
				yylhsminor.yy190.pExpr->x.pList = pList;
				spanSet(&yylhsminor.yy190, &yymsp[-4].minor.yy0,
					&yymsp[0].minor.yy0);
			} else {
				sqlite3ExprListDelete(pParse->db, pList);
			}
		}
#line 3228 "parse.c"
		yymsp[-4].minor.yy190 = yylhsminor.yy190;
		break;
	case 161:		/* expr ::= expr AND expr */
	case 162:		/* expr ::= expr OR expr */
		yytestcase(yyruleno == 162);
	case 163:		/* expr ::= expr LT|GT|GE|LE expr */
		yytestcase(yyruleno == 163);
	case 164:		/* expr ::= expr EQ|NE expr */
		yytestcase(yyruleno == 164);
	case 165:		/* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */
		yytestcase(yyruleno == 165);
	case 166:		/* expr ::= expr PLUS|MINUS expr */
		yytestcase(yyruleno == 166);
	case 167:		/* expr ::= expr STAR|SLASH|REM expr */
		yytestcase(yyruleno == 167);
	case 168:		/* expr ::= expr CONCAT expr */
		yytestcase(yyruleno == 168);
#line 986 "parse.y"
		{
			spanBinaryExpr(pParse, yymsp[-1].major,
				       &yymsp[-2].minor.yy190,
				       &yymsp[0].minor.yy190);
		}
#line 3241 "parse.c"
		break;
	case 169:		/* likeop ::= LIKE_KW|MATCH */
#line 999 "parse.y"
		{
			yymsp[0].minor.yy0 = yymsp[0].minor.yy0;	/*A-overwrites-X */
		}
#line 3246 "parse.c"
		break;
	case 170:		/* likeop ::= NOT LIKE_KW|MATCH */
#line 1000 "parse.y"
		{
			yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;
			yymsp[-1].minor.yy0.n |= 0x80000000;	/*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0 */
		}
#line 3251 "parse.c"
		break;
	case 171:		/* expr ::= expr likeop expr */
#line 1001 "parse.y"
		{
			ExprList *pList;
			int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
			yymsp[-1].minor.yy0.n &= 0x7fffffff;
			pList =
			    sqlite3ExprListAppend(pParse, 0,
						  yymsp[0].minor.yy190.pExpr);
			pList =
			    sqlite3ExprListAppend(pParse, pList,
						  yymsp[-2].minor.yy190.pExpr);
			yymsp[-2].minor.yy190.pExpr =
			    sqlite3ExprFunction(pParse, pList,
						&yymsp[-1].minor.yy0);
			exprNot(pParse, bNot, &yymsp[-2].minor.yy190);
			yymsp[-2].minor.yy190.zEnd = yymsp[0].minor.yy190.zEnd;
			if (yymsp[-2].minor.yy190.pExpr)
				yymsp[-2].minor.yy190.pExpr->flags |=
				    EP_InfixFunc;
		}
#line 3266 "parse.c"
		break;
	case 172:		/* expr ::= expr likeop expr ESCAPE expr */
#line 1012 "parse.y"
		{
			ExprList *pList;
			int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
			yymsp[-3].minor.yy0.n &= 0x7fffffff;
			pList =
			    sqlite3ExprListAppend(pParse, 0,
						  yymsp[-2].minor.yy190.pExpr);
			pList =
			    sqlite3ExprListAppend(pParse, pList,
						  yymsp[-4].minor.yy190.pExpr);
			pList =
			    sqlite3ExprListAppend(pParse, pList,
						  yymsp[0].minor.yy190.pExpr);
			yymsp[-4].minor.yy190.pExpr =
			    sqlite3ExprFunction(pParse, pList,
						&yymsp[-3].minor.yy0);
			exprNot(pParse, bNot, &yymsp[-4].minor.yy190);
			yymsp[-4].minor.yy190.zEnd = yymsp[0].minor.yy190.zEnd;
			if (yymsp[-4].minor.yy190.pExpr)
				yymsp[-4].minor.yy190.pExpr->flags |=
				    EP_InfixFunc;
		}
#line 3282 "parse.c"
		break;
	case 173:		/* expr ::= expr ISNULL|NOTNULL */
#line 1039 "parse.y"
		{
			spanUnaryPostfix(pParse, yymsp[0].major,
					 &yymsp[-1].minor.yy190,
					 &yymsp[0].minor.yy0);
		}
#line 3287 "parse.c"
		break;
	case 174:		/* expr ::= expr NOT NULL */
#line 1040 "parse.y"
		{
			spanUnaryPostfix(pParse, TK_NOTNULL,
					 &yymsp[-2].minor.yy190,
					 &yymsp[0].minor.yy0);
		}
#line 3292 "parse.c"
		break;
	case 175:		/* expr ::= expr IS expr */
#line 1061 "parse.y"
		{
			spanBinaryExpr(pParse, TK_IS, &yymsp[-2].minor.yy190,
				       &yymsp[0].minor.yy190);
			binaryToUnaryIfNull(pParse, yymsp[0].minor.yy190.pExpr,
					    yymsp[-2].minor.yy190.pExpr,
					    TK_ISNULL);
		}
#line 3300 "parse.c"
		break;
	case 176:		/* expr ::= expr IS NOT expr */
#line 1065 "parse.y"
		{
			spanBinaryExpr(pParse, TK_ISNOT, &yymsp[-3].minor.yy190,
				       &yymsp[0].minor.yy190);
			binaryToUnaryIfNull(pParse, yymsp[0].minor.yy190.pExpr,
					    yymsp[-3].minor.yy190.pExpr,
					    TK_NOTNULL);
		}
#line 3308 "parse.c"
		break;
	case 177:		/* expr ::= NOT expr */
	case 178:		/* expr ::= BITNOT expr */
		yytestcase(yyruleno == 178);
#line 1089 "parse.y"
		{
			spanUnaryPrefix(&yymsp[-1].minor.yy190, pParse, yymsp[-1].major, &yymsp[0].minor.yy190, &yymsp[-1].minor.yy0);	/*A-overwrites-B */
		}
#line 3314 "parse.c"
		break;
	case 179:		/* expr ::= MINUS expr */
#line 1093 "parse.y"
		{
			spanUnaryPrefix(&yymsp[-1].minor.yy190, pParse, TK_UMINUS, &yymsp[0].minor.yy190, &yymsp[-1].minor.yy0);	/*A-overwrites-B */
		}
#line 3319 "parse.c"
		break;
	case 180:		/* expr ::= PLUS expr */
#line 1095 "parse.y"
		{
			spanUnaryPrefix(&yymsp[-1].minor.yy190, pParse, TK_UPLUS, &yymsp[0].minor.yy190, &yymsp[-1].minor.yy0);	/*A-overwrites-B */
		}
#line 3324 "parse.c"
		break;
	case 181:		/* between_op ::= BETWEEN */
	case 184:		/* in_op ::= IN */
		yytestcase(yyruleno == 184);
#line 1098 "parse.y"
		{
			yymsp[0].minor.yy58 = 0;
		}
#line 3330 "parse.c"
		break;
	case 183:		/* expr ::= expr between_op expr AND expr */
#line 1100 "parse.y"
		{
			ExprList *pList =
			    sqlite3ExprListAppend(pParse, 0,
						  yymsp[-2].minor.yy190.pExpr);
			pList =
			    sqlite3ExprListAppend(pParse, pList,
						  yymsp[0].minor.yy190.pExpr);
			yymsp[-4].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_BETWEEN,
					 yymsp[-4].minor.yy190.pExpr, 0);
			if (yymsp[-4].minor.yy190.pExpr) {
				yymsp[-4].minor.yy190.pExpr->x.pList = pList;
			} else {
				sqlite3ExprListDelete(pParse->db, pList);
			}
			exprNot(pParse, yymsp[-3].minor.yy58,
				&yymsp[-4].minor.yy190);
			yymsp[-4].minor.yy190.zEnd = yymsp[0].minor.yy190.zEnd;
		}
#line 3346 "parse.c"
		break;
	case 186:		/* expr ::= expr in_op LP exprlist RP */
#line 1116 "parse.y"
		{
			if (yymsp[-1].minor.yy412 == 0) {
				/* Expressions of the form
				 *
				 *      expr1 IN ()
				 *      expr1 NOT IN ()
				 *
				 * simplify to constants 0 (false) and 1 (true), respectively,
				 * regardless of the value of expr1.
				 */
				sqlite3ExprDelete(pParse->db,
						  yymsp[-4].minor.yy190.pExpr);
				yymsp[-4].minor.yy190.pExpr =
				    sqlite3ExprAlloc(pParse->db, TK_INTEGER,
						     &sqlite3IntTokens[yymsp
								       [-3].
								       minor.
								       yy58],
						     1);
			} else if (yymsp[-1].minor.yy412->nExpr == 1) {
				/* Expressions of the form:
				 *
				 *      expr1 IN (?1)
				 *      expr1 NOT IN (?2)
				 *
				 * with exactly one value on the RHS can be simplified to something
				 * like this:
				 *
				 *      expr1 == ?1
				 *      expr1 <> ?2
				 *
				 * But, the RHS of the == or <> is marked with the EP_Generic flag
				 * so that it may not contribute to the computation of comparison
				 * affinity or the collating sequence to use for comparison.  Otherwise,
				 * the semantics would be subtly different from IN or NOT IN.
				 */
				Expr *pRHS = yymsp[-1].minor.yy412->a[0].pExpr;
				yymsp[-1].minor.yy412->a[0].pExpr = 0;
				sqlite3ExprListDelete(pParse->db,
						      yymsp[-1].minor.yy412);
				/* pRHS cannot be NULL because a malloc error would have been detected
				 * before now and control would have never reached this point
				 */
				if (ALWAYS(pRHS)) {
					pRHS->flags &= ~EP_Collate;
					pRHS->flags |= EP_Generic;
				}
				yymsp[-4].minor.yy190.pExpr =
				    sqlite3PExpr(pParse,
						 yymsp[-3].minor.
						 yy58 ? TK_NE : TK_EQ,
						 yymsp[-4].minor.yy190.pExpr,
						 pRHS);
			} else {
				yymsp[-4].minor.yy190.pExpr =
				    sqlite3PExpr(pParse, TK_IN,
						 yymsp[-4].minor.yy190.pExpr,
						 0);
				if (yymsp[-4].minor.yy190.pExpr) {
					yymsp[-4].minor.yy190.pExpr->x.pList =
					    yymsp[-1].minor.yy412;
					sqlite3ExprSetHeightAndFlags(pParse,
								     yymsp[-4].
								     minor.
								     yy190.
								     pExpr);
				} else {
					sqlite3ExprListDelete(pParse->db,
							      yymsp[-1].minor.
							      yy412);
				}
				exprNot(pParse, yymsp[-3].minor.yy58,
					&yymsp[-4].minor.yy190);
			}
			yymsp[-4].minor.yy190.zEnd =
			    &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
		}
#line 3401 "parse.c"
		break;
	case 187:		/* expr ::= LP select RP */
#line 1167 "parse.y"
		{
			spanSet(&yymsp[-2].minor.yy190, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-B */
			yymsp[-2].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_SELECT, 0, 0);
			sqlite3PExprAddSelect(pParse,
					      yymsp[-2].minor.yy190.pExpr,
					      yymsp[-1].minor.yy99);
		}
#line 3410 "parse.c"
		break;
	case 188:		/* expr ::= expr in_op LP select RP */
#line 1172 "parse.y"
		{
			yymsp[-4].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_IN,
					 yymsp[-4].minor.yy190.pExpr, 0);
			sqlite3PExprAddSelect(pParse,
					      yymsp[-4].minor.yy190.pExpr,
					      yymsp[-1].minor.yy99);
			exprNot(pParse, yymsp[-3].minor.yy58,
				&yymsp[-4].minor.yy190);
			yymsp[-4].minor.yy190.zEnd =
			    &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
		}
#line 3420 "parse.c"
		break;
	case 189:		/* expr ::= expr in_op nm paren_exprlist */
#line 1178 "parse.y"
		{
			SrcList *pSrc =
			    sqlite3SrcListAppend(pParse->db, 0,
						 &yymsp[-1].minor.yy0, 0);
			Select *pSelect =
			    sqlite3SelectNew(pParse, 0, pSrc, 0, 0, 0, 0, 0, 0,
					     0);
			if (yymsp[0].minor.yy412)
				sqlite3SrcListFuncArgs(pParse,
						       pSelect ? pSrc : 0,
						       yymsp[0].minor.yy412);
			yymsp[-3].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_IN,
					 yymsp[-3].minor.yy190.pExpr, 0);
			sqlite3PExprAddSelect(pParse,
					      yymsp[-3].minor.yy190.pExpr,
					      pSelect);
			exprNot(pParse, yymsp[-2].minor.yy58,
				&yymsp[-3].minor.yy190);
			yymsp[-3].minor.yy190.zEnd =
			    &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
		}
#line 3433 "parse.c"
		break;
	case 190:		/* expr ::= EXISTS LP select RP */
#line 1187 "parse.y"
		{
			Expr *p;
			spanSet(&yymsp[-3].minor.yy190, &yymsp[-3].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-B */
			p = yymsp[-3].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
			sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy99);
		}
#line 3443 "parse.c"
		break;
	case 191:		/* expr ::= CASE case_operand case_exprlist case_else END */
#line 1196 "parse.y"
		{
			spanSet(&yymsp[-4].minor.yy190, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-C */
			yymsp[-4].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy44,
					 0);
			if (yymsp[-4].minor.yy190.pExpr) {
				yymsp[-4].minor.yy190.pExpr->x.pList =
				    yymsp[-1].minor.
				    yy44 ? sqlite3ExprListAppend(pParse,
								 yymsp[-2].
								 minor.yy412,
								 yymsp[-1].
								 minor.
								 yy44) :
				    yymsp[-2].minor.yy412;
				sqlite3ExprSetHeightAndFlags(pParse,
							     yymsp[-4].minor.
							     yy190.pExpr);
			} else {
				sqlite3ExprListDelete(pParse->db,
						      yymsp[-2].minor.yy412);
				sqlite3ExprDelete(pParse->db,
						  yymsp[-1].minor.yy44);
			}
		}
#line 3458 "parse.c"
		break;
	case 192:		/* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1209 "parse.y"
		{
			yymsp[-4].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy412,
						  yymsp[-2].minor.yy190.pExpr);
			yymsp[-4].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy412,
						  yymsp[0].minor.yy190.pExpr);
		}
#line 3466 "parse.c"
		break;
	case 193:		/* case_exprlist ::= WHEN expr THEN expr */
#line 1213 "parse.y"
		{
			yymsp[-3].minor.yy412 =
			    sqlite3ExprListAppend(pParse, 0,
						  yymsp[-2].minor.yy190.pExpr);
			yymsp[-3].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy412,
						  yymsp[0].minor.yy190.pExpr);
		}
#line 3474 "parse.c"
		break;
	case 196:		/* case_operand ::= expr */
#line 1223 "parse.y"
		{
			yymsp[0].minor.yy44 = yymsp[0].minor.yy190.pExpr;	/*A-overwrites-X */
		}
#line 3479 "parse.c"
		break;
	case 199:		/* nexprlist ::= nexprlist COMMA expr */
#line 1234 "parse.y"
		{
			yymsp[-2].minor.yy412 =
			    sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy412,
						  yymsp[0].minor.yy190.pExpr);
		}
#line 3484 "parse.c"
		break;
	case 200:		/* nexprlist ::= expr */
#line 1236 "parse.y"
		{
			yymsp[0].minor.yy412 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy190.pExpr);	/*A-overwrites-Y */
		}
#line 3489 "parse.c"
		break;
	case 202:		/* paren_exprlist ::= LP exprlist RP */
	case 207:		/* eidlist_opt ::= LP eidlist RP */
		yytestcase(yyruleno == 207);
#line 1244 "parse.y"
		{
			yymsp[-2].minor.yy412 = yymsp[-1].minor.yy412;
		}
#line 3495 "parse.c"
		break;
	case 203:		/* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1251 "parse.y"
		{
			sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0,
					   sqlite3SrcListAppend(pParse->db, 0,
								&yymsp[-4].
								minor.yy0, 0),
					   yymsp[-2].minor.yy412,
					   yymsp[-9].minor.yy58,
					   &yymsp[-10].minor.yy0,
					   yymsp[0].minor.yy44, SQLITE_SO_ASC,
					   yymsp[-7].minor.yy58,
					   SQLITE_IDXTYPE_APPDEF);
		}
#line 3504 "parse.c"
		break;
	case 204:		/* uniqueflag ::= UNIQUE */
	case 244:		/* raisetype ::= ABORT */
		yytestcase(yyruleno == 244);
#line 1258 "parse.y"
		{
			yymsp[0].minor.yy58 = OE_Abort;
		}
#line 3510 "parse.c"
		break;
	case 205:		/* uniqueflag ::= */
#line 1259 "parse.y"
		{
			yymsp[1].minor.yy58 = OE_None;
		}
#line 3515 "parse.c"
		break;
	case 208:		/* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1309 "parse.y"
		{
			yymsp[-4].minor.yy412 =
			    parserAddExprIdListTerm(pParse,
						    yymsp[-4].minor.yy412,
						    &yymsp[-2].minor.yy0,
						    yymsp[-1].minor.yy58,
						    yymsp[0].minor.yy58);
		}
#line 3522 "parse.c"
		break;
	case 209:		/* eidlist ::= nm collate sortorder */
#line 1312 "parse.y"
		{
			yymsp[-2].minor.yy412 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy58, yymsp[0].minor.yy58);	/*A-overwrites-Y */
		}
#line 3529 "parse.c"
		break;
	case 212:		/* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1323 "parse.y"
		{
			sqlite3DropIndex(pParse, yymsp[-2].minor.yy367,
					 &yymsp[0].minor.yy0,
					 yymsp[-3].minor.yy58);
		}
#line 3536 "parse.c"
		break;
	case 213:		/* cmd ::= PRAGMA nm */
#line 1330 "parse.y"
		{
			sqlite3Pragma(pParse, &yymsp[0].minor.yy0, 0, 0, 0, 0);
		}
#line 3543 "parse.c"
		break;
	case 214:		/* cmd ::= PRAGMA nm EQ nmnum */
#line 1333 "parse.y"
		{
			sqlite3Pragma(pParse, &yymsp[-2].minor.yy0, 0,
				      &yymsp[0].minor.yy0, 0, 0);
		}
#line 3550 "parse.c"
		break;
	case 215:		/* cmd ::= PRAGMA nm LP nmnum RP */
#line 1336 "parse.y"
		{
			sqlite3Pragma(pParse, &yymsp[-3].minor.yy0, 0,
				      &yymsp[-1].minor.yy0, 0, 0);
		}
#line 3557 "parse.c"
		break;
	case 216:		/* cmd ::= PRAGMA nm EQ minus_num */
#line 1339 "parse.y"
		{
			sqlite3Pragma(pParse, &yymsp[-2].minor.yy0, 0,
				      &yymsp[0].minor.yy0, 0, 1);
		}
#line 3564 "parse.c"
		break;
	case 217:		/* cmd ::= PRAGMA nm LP minus_num RP */
#line 1342 "parse.y"
		{
			sqlite3Pragma(pParse, &yymsp[-3].minor.yy0, 0,
				      &yymsp[-1].minor.yy0, 0, 1);
		}
#line 3571 "parse.c"
		break;
	case 218:		/* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1345 "parse.y"
		{
			sqlite3Pragma(pParse, &yymsp[-4].minor.yy0, 0,
				      &yymsp[0].minor.yy0, &yymsp[-2].minor.yy0,
				      0);
		}
#line 3578 "parse.c"
		break;
	case 221:		/* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1364 "parse.y"
		{
			Token all;
			all.z = yymsp[-3].minor.yy0.z;
			all.n =
			    (int)(yymsp[0].minor.yy0.z -
				  yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
			sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy203,
					     &all);
		}
#line 3588 "parse.c"
		break;
	case 222:		/* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1373 "parse.y"
		{
			sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0,
					    yymsp[-5].minor.yy58,
					    yymsp[-4].minor.yy234.a,
					    yymsp[-4].minor.yy234.b,
					    yymsp[-2].minor.yy367,
					    yymsp[0].minor.yy44,
					    yymsp[-7].minor.yy58);
			yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0;	/*yymsp[-8].minor.yy0-overwrites-T */
		}
#line 3596 "parse.c"
		break;
	case 223:		/* trigger_time ::= BEFORE */
#line 1379 "parse.y"
		{
			yymsp[0].minor.yy58 = TK_BEFORE;
		}
#line 3601 "parse.c"
		break;
	case 224:		/* trigger_time ::= AFTER */
#line 1380 "parse.y"
		{
			yymsp[0].minor.yy58 = TK_AFTER;
		}
#line 3606 "parse.c"
		break;
	case 225:		/* trigger_time ::= INSTEAD OF */
#line 1381 "parse.y"
		{
			yymsp[-1].minor.yy58 = TK_INSTEAD;
		}
#line 3611 "parse.c"
		break;
	case 226:		/* trigger_time ::= */
#line 1382 "parse.y"
		{
			yymsp[1].minor.yy58 = TK_BEFORE;
		}
#line 3616 "parse.c"
		break;
	case 227:		/* trigger_event ::= DELETE|INSERT */
	case 228:		/* trigger_event ::= UPDATE */
		yytestcase(yyruleno == 228);
#line 1386 "parse.y"
		{
			yymsp[0].minor.yy234.a = yymsp[0].major;	/*A-overwrites-X */
			yymsp[0].minor.yy234.b = 0;
		}
#line 3622 "parse.c"
		break;
	case 229:		/* trigger_event ::= UPDATE OF idlist */
#line 1388 "parse.y"
		{
			yymsp[-2].minor.yy234.a = TK_UPDATE;
			yymsp[-2].minor.yy234.b = yymsp[0].minor.yy258;
		}
#line 3627 "parse.c"
		break;
	case 230:		/* when_clause ::= */
#line 1395 "parse.y"
		{
			yymsp[1].minor.yy44 = 0;
		}
#line 3632 "parse.c"
		break;
	case 231:		/* when_clause ::= WHEN expr */
#line 1396 "parse.y"
		{
			yymsp[-1].minor.yy44 = yymsp[0].minor.yy190.pExpr;
		}
#line 3637 "parse.c"
		break;
	case 232:		/* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1400 "parse.y"
		{
			assert(yymsp[-2].minor.yy203 != 0);
			yymsp[-2].minor.yy203->pLast->pNext =
			    yymsp[-1].minor.yy203;
			yymsp[-2].minor.yy203->pLast = yymsp[-1].minor.yy203;
		}
#line 3646 "parse.c"
		break;
	case 233:		/* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1405 "parse.y"
		{
			assert(yymsp[-1].minor.yy203 != 0);
			yymsp[-1].minor.yy203->pLast = yymsp[-1].minor.yy203;
		}
#line 3654 "parse.c"
		break;
	case 234:		/* trnm ::= nm DOT nm */
#line 1416 "parse.y"
		{
			yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
			sqlite3ErrorMsg(pParse,
					"qualified table names are not allowed on INSERT, UPDATE, and DELETE "
					"statements within triggers");
		}
#line 3664 "parse.c"
		break;
	case 235:		/* tridxby ::= INDEXED BY nm */
#line 1428 "parse.y"
		{
			sqlite3ErrorMsg(pParse,
					"the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
					"within triggers");
		}
#line 3673 "parse.c"
		break;
	case 236:		/* tridxby ::= NOT INDEXED */
#line 1433 "parse.y"
		{
			sqlite3ErrorMsg(pParse,
					"the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
					"within triggers");
		}
#line 3682 "parse.c"
		break;
	case 237:		/* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1446 "parse.y"
		{
			yymsp[-6].minor.yy203 =
			    sqlite3TriggerUpdateStep(pParse->db,
						     &yymsp[-4].minor.yy0,
						     yymsp[-1].minor.yy412,
						     yymsp[0].minor.yy44,
						     yymsp[-5].minor.yy58);
		}
#line 3687 "parse.c"
		break;
	case 238:		/* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1450 "parse.y"
		{
			yymsp[-4].minor.yy203 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy258, yymsp[0].minor.yy99, yymsp[-4].minor.yy58);	/*A-overwrites-R */
		}
#line 3692 "parse.c"
		break;
	case 239:		/* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1454 "parse.y"
		{
			yymsp[-4].minor.yy203 =
			    sqlite3TriggerDeleteStep(pParse->db,
						     &yymsp[-2].minor.yy0,
						     yymsp[0].minor.yy44);
		}
#line 3697 "parse.c"
		break;
	case 240:		/* trigger_cmd ::= select */
#line 1458 "parse.y"
		{
			yymsp[0].minor.yy203 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy99);	/*A-overwrites-X */
		}
#line 3702 "parse.c"
		break;
	case 241:		/* expr ::= RAISE LP IGNORE RP */
#line 1461 "parse.y"
		{
			spanSet(&yymsp[-3].minor.yy190, &yymsp[-3].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-X */
			yymsp[-3].minor.yy190.pExpr =
			    sqlite3PExpr(pParse, TK_RAISE, 0, 0);
			if (yymsp[-3].minor.yy190.pExpr) {
				yymsp[-3].minor.yy190.pExpr->affinity =
				    OE_Ignore;
			}
		}
#line 3713 "parse.c"
		break;
	case 242:		/* expr ::= RAISE LP raisetype COMMA nm RP */
#line 1468 "parse.y"
		{
			spanSet(&yymsp[-5].minor.yy190, &yymsp[-5].minor.yy0, &yymsp[0].minor.yy0);	/*A-overwrites-X */
			yymsp[-5].minor.yy190.pExpr =
			    sqlite3ExprAlloc(pParse->db, TK_RAISE,
					     &yymsp[-1].minor.yy0, 1);
			if (yymsp[-5].minor.yy190.pExpr) {
				yymsp[-5].minor.yy190.pExpr->affinity =
				    (char)yymsp[-3].minor.yy58;
			}
		}
#line 3724 "parse.c"
		break;
	case 243:		/* raisetype ::= ROLLBACK */
#line 1478 "parse.y"
		{
			yymsp[0].minor.yy58 = OE_Rollback;
		}
#line 3729 "parse.c"
		break;
	case 245:		/* raisetype ::= FAIL */
#line 1480 "parse.y"
		{
			yymsp[0].minor.yy58 = OE_Fail;
		}
#line 3734 "parse.c"
		break;
	case 246:		/* cmd ::= DROP TRIGGER ifexists fullname */
#line 1485 "parse.y"
		{
			sqlite3DropTrigger(pParse, yymsp[0].minor.yy367,
					   yymsp[-1].minor.yy58);
		}
#line 3741 "parse.c"
		break;
	case 247:		/* cmd ::= REINDEX */
#line 1492 "parse.y"
		{
			sqlite3Reindex(pParse, 0, 0);
		}
#line 3746 "parse.c"
		break;
	case 248:		/* cmd ::= REINDEX nm */
#line 1493 "parse.y"
		{
			sqlite3Reindex(pParse, &yymsp[0].minor.yy0, 0);
		}
#line 3751 "parse.c"
		break;
	case 249:		/* cmd ::= REINDEX nm ON nm */
#line 1494 "parse.y"
		{
			sqlite3Reindex(pParse, &yymsp[-2].minor.yy0,
				       &yymsp[0].minor.yy0);
		}
#line 3756 "parse.c"
		break;
	case 250:		/* cmd ::= ANALYZE */
#line 1499 "parse.y"
		{
			sqlite3Analyze(pParse, 0);
		}
#line 3761 "parse.c"
		break;
	case 251:		/* cmd ::= ANALYZE nm */
#line 1500 "parse.y"
		{
			sqlite3Analyze(pParse, &yymsp[0].minor.yy0);
		}
#line 3766 "parse.c"
		break;
	case 252:		/* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1505 "parse.y"
		{
			sqlite3AlterRenameTable(pParse, yymsp[-3].minor.yy367,
						&yymsp[0].minor.yy0);
		}
#line 3773 "parse.c"
		break;
	case 253:		/* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist */
#line 1509 "parse.y"
		{
			yymsp[-1].minor.yy0.n =
			    (int)(pParse->sLastToken.z -
				  yymsp[-1].minor.yy0.z) + pParse->sLastToken.n;
			sqlite3AlterFinishAddColumn(pParse,
						    &yymsp[-1].minor.yy0);
		}
#line 3781 "parse.c"
		break;
	case 254:		/* add_column_fullname ::= fullname */
#line 1513 "parse.y"
		{
			disableLookaside(pParse);
			sqlite3AlterBeginAddColumn(pParse,
						   yymsp[0].minor.yy367);
		}
#line 3789 "parse.c"
		break;
	case 255:		/* cmd ::= create_vtab */
#line 1523 "parse.y"
		{
			sqlite3VtabFinishParse(pParse, 0);
		}
#line 3794 "parse.c"
		break;
	case 256:		/* cmd ::= create_vtab LP vtabarglist RP */
#line 1524 "parse.y"
		{
			sqlite3VtabFinishParse(pParse, &yymsp[0].minor.yy0);
		}
#line 3799 "parse.c"
		break;
	case 257:		/* create_vtab ::= createkw VIRTUAL TABLE ifnotexists nm USING nm */
#line 1526 "parse.y"
		{
			sqlite3VtabBeginParse(pParse, &yymsp[-2].minor.yy0, 0,
					      &yymsp[0].minor.yy0,
					      yymsp[-3].minor.yy58);
		}
#line 3806 "parse.c"
		break;
	case 258:		/* vtabarg ::= */
#line 1531 "parse.y"
		{
			sqlite3VtabArgInit(pParse);
		}
#line 3811 "parse.c"
		break;
	case 259:		/* vtabargtoken ::= ANY */
	case 260:		/* vtabargtoken ::= lp anylist RP */
		yytestcase(yyruleno == 260);
	case 261:		/* lp ::= LP */
		yytestcase(yyruleno == 261);
#line 1533 "parse.y"
		{
			sqlite3VtabArgExtend(pParse, &yymsp[0].minor.yy0);
		}
#line 3818 "parse.c"
		break;
	case 262:		/* with ::= */
#line 1548 "parse.y"
		{
			yymsp[1].minor.yy91 = 0;
		}
#line 3823 "parse.c"
		break;
	case 263:		/* with ::= WITH wqlist */
#line 1550 "parse.y"
		{
			yymsp[-1].minor.yy91 = yymsp[0].minor.yy91;
		}
#line 3828 "parse.c"
		break;
	case 264:		/* with ::= WITH RECURSIVE wqlist */
#line 1551 "parse.y"
		{
			yymsp[-2].minor.yy91 = yymsp[0].minor.yy91;
		}
#line 3833 "parse.c"
		break;
	case 265:		/* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1553 "parse.y"
		{
			yymsp[-5].minor.yy91 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy412, yymsp[-1].minor.yy99);	/*A-overwrites-X */
		}
#line 3840 "parse.c"
		break;
	case 266:		/* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1556 "parse.y"
		{
			yymsp[-7].minor.yy91 =
			    sqlite3WithAdd(pParse, yymsp[-7].minor.yy91,
					   &yymsp[-5].minor.yy0,
					   yymsp[-4].minor.yy412,
					   yymsp[-1].minor.yy99);
		}
#line 3847 "parse.c"
		break;
	default:
		/* (267) input ::= ecmd */ yytestcase(yyruleno == 267);
		/* (268) explain ::= */ yytestcase(yyruleno == 268);
		/* (269) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno !=
								269);
		/* (270) trans_opt ::= */ yytestcase(yyruleno == 270);
		/* (271) trans_opt ::= TRANSACTION */ yytestcase(yyruleno ==
								 271);
		/* (272) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno ==
								    272);
		/* (273) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno ==
								   273);
		/* (274) savepoint_opt ::= */ yytestcase(yyruleno == 274);
		/* (275) cmd ::= create_table create_table_args */
		    yytestcase(yyruleno == 275);
		/* (276) columnlist ::= columnlist COMMA columnname carglist */
		    yytestcase(yyruleno == 276);
		/* (277) columnlist ::= columnname carglist */
		    yytestcase(yyruleno == 277);
		/* (278) nm ::= ID|INDEXED */ yytestcase(yyruleno == 278);
		/* (279) nm ::= STRING */ yytestcase(yyruleno == 279);
		/* (280) nm ::= JOIN_KW */ yytestcase(yyruleno == 280);
		/* (281) typetoken ::= typename */ yytestcase(yyruleno == 281);
		/* (282) typename ::= ID|STRING */ yytestcase(yyruleno == 282);
		/* (283) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno
								       != 283);
		/* (284) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno
									!= 284);
		/* (285) carglist ::= carglist ccons */ yytestcase(yyruleno ==
								   285);
		/* (286) carglist ::= */ yytestcase(yyruleno == 286);
		/* (287) ccons ::= NULL onconf */ yytestcase(yyruleno == 287);
		/* (288) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno
								       == 288);
		/* (289) conslist ::= conslist tconscomma tcons */
		    yytestcase(yyruleno == 289);
		/* (290) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno
								      != 290);
		/* (291) tconscomma ::= */ yytestcase(yyruleno == 291);
		/* (292) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */
		    assert(yyruleno != 292);
		/* (293) resolvetype ::= raisetype (OPTIMIZED OUT) */
		    assert(yyruleno != 293);
		/* (294) selectnowith ::= oneselect (OPTIMIZED OUT) */
		    assert(yyruleno != 294);
		/* (295) oneselect ::= values */ yytestcase(yyruleno == 295);
		/* (296) sclp ::= selcollist COMMA */ yytestcase(yyruleno ==
								 296);
		/* (297) as ::= ID|STRING */ yytestcase(yyruleno == 297);
		/* (298) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno !=
								 298);
		/* (299) exprlist ::= nexprlist */ yytestcase(yyruleno == 299);
		/* (300) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno
								      != 300);
		/* (301) nmnum ::= nm */ yytestcase(yyruleno == 301);
		/* (302) nmnum ::= ON */ yytestcase(yyruleno == 302);
		/* (303) nmnum ::= DELETE */ yytestcase(yyruleno == 303);
		/* (304) nmnum ::= DEFAULT */ yytestcase(yyruleno == 304);
		/* (305) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno ==
								  305);
		/* (306) foreach_clause ::= */ yytestcase(yyruleno == 306);
		/* (307) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno
								       == 307);
		/* (308) trnm ::= nm */ yytestcase(yyruleno == 308);
		/* (309) tridxby ::= */ yytestcase(yyruleno == 309);
		/* (310) kwcolumn_opt ::= */ yytestcase(yyruleno == 310);
		/* (311) kwcolumn_opt ::= COLUMNKW */ yytestcase(yyruleno ==
								 311);
		/* (312) vtabarglist ::= vtabarg */ yytestcase(yyruleno == 312);
		/* (313) vtabarglist ::= vtabarglist COMMA vtabarg */
		    yytestcase(yyruleno == 313);
		/* (314) vtabarg ::= vtabarg vtabargtoken */ yytestcase(yyruleno
									== 314);
		/* (315) anylist ::= */ yytestcase(yyruleno == 315);
		/* (316) anylist ::= anylist LP anylist RP */
		    yytestcase(yyruleno == 316);
		/* (317) anylist ::= anylist ANY */ yytestcase(yyruleno == 317);
		break;
/********** End reduce actions ************************************************/
	};
	assert(yyruleno < sizeof(yyRuleInfo) / sizeof(yyRuleInfo[0]));
	yygoto = yyRuleInfo[yyruleno].lhs;
	yysize = yyRuleInfo[yyruleno].nrhs;
	yyact =
	    yy_find_reduce_action(yymsp[-yysize].stateno, (YYCODETYPE) yygoto);
	if (yyact <= YY_MAX_SHIFTREDUCE) {
		if (yyact > YY_MAX_SHIFT) {
			yyact += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
		}
		yymsp -= yysize - 1;
		yypParser->yytos = yymsp;
		yymsp->stateno = (YYACTIONTYPE) yyact;
		yymsp->major = (YYCODETYPE) yygoto;
		yyTraceShift(yypParser, yyact);
	} else {
		assert(yyact == YY_ACCEPT_ACTION);
		yypParser->yytos -= yysize;
		yy_accept(yypParser);
	}
}

/*
 * The following code executes when the parse fails
 */
#ifndef YYNOERRORRECOVERY
static void
yy_parse_failed(yyParser * yypParser	/* The parser */
    )
{
	sqlite3ParserARG_FETCH;
#ifndef NDEBUG
	if (yyTraceFILE) {
		fprintf(yyTraceFILE, "%sFail!\n", yyTracePrompt);
	}
#endif
	while (yypParser->yytos > yypParser->yystack)
		yy_pop_parser_stack(yypParser);
	/* Here code is inserted which will be executed whenever the
	 * parser fails
	 */
/************ Begin %parse_failure code ***************************************/
/************ End %parse_failure code *****************************************/
	sqlite3ParserARG_STORE;	/* Suppress warning about unused %extra_argument variable */
}
#endif				/* YYNOERRORRECOVERY */

/*
 * The following code executes when a syntax error first occurs.
 */
static void
yy_syntax_error(yyParser * yypParser,	/* The parser */
		int yymajor,	/* The major type of the error token */
		sqlite3ParserTOKENTYPE yyminor	/* The minor type of the error token */
    )
{
	sqlite3ParserARG_FETCH;
#define TOKEN yyminor
/************ Begin %syntax_error code ****************************************/
#line 32 "parse.y"

	UNUSED_PARAMETER(yymajor);	/* Silence some compiler warnings */
	assert(TOKEN.z[0]);	/* The tokenizer always gives us a token */
	sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &TOKEN);
#line 3962 "parse.c"
/************ End %syntax_error code ******************************************/
	sqlite3ParserARG_STORE;	/* Suppress warning about unused %extra_argument variable */
}

/*
 * The following is executed when the parser accepts
 */
static void
yy_accept(yyParser * yypParser	/* The parser */
    )
{
	sqlite3ParserARG_FETCH;
#ifndef NDEBUG
	if (yyTraceFILE) {
		fprintf(yyTraceFILE, "%sAccept!\n", yyTracePrompt);
	}
#endif
#ifndef YYNOERRORRECOVERY
	yypParser->yyerrcnt = -1;
#endif
	assert(yypParser->yytos == yypParser->yystack);
	/* Here code is inserted which will be executed whenever the
	 * parser accepts
	 */
/*********** Begin %parse_accept code *****************************************/
/*********** End %parse_accept code *******************************************/
	sqlite3ParserARG_STORE;	/* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
 * The first argument is a pointer to a structure obtained from
 * "sqlite3ParserAlloc" which describes the current state of the parser.
 * The second argument is the major token number.  The third is
 * the minor token.  The fourth optional argument is whatever the
 * user wants (and specified in the grammar) and is available for
 * use by the action routines.
 *
 * Inputs:
 * <ul>
 * <li> A pointer to the parser (an opaque structure.)
 * <li> The major token number.
 * <li> The minor token number.
 * <li> An option argument of a grammar-specified type.
 * </ul>
 *
 * Outputs:
 * None.
 */
void
sqlite3Parser(void *yyp,	/* The parser */
	      int yymajor,	/* The major token code number */
	      sqlite3ParserTOKENTYPE yyminor	/* The value for the token */
	      sqlite3ParserARG_PDECL	/* Optional %extra_argument parameter */
    )
{
	YYMINORTYPE yyminorunion;
	unsigned int yyact;	/* The parser action. */
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
	int yyendofinput;	/* True if we are at the end of input */
#endif
#ifdef YYERRORSYMBOL
	int yyerrorhit = 0;	/* True if yymajor has invoked an error */
#endif
	yyParser *yypParser;	/* The parser */

	yypParser = (yyParser *) yyp;
	assert(yypParser->yytos != 0);
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
	yyendofinput = (yymajor == 0);
#endif
	sqlite3ParserARG_STORE;

#ifndef NDEBUG
	if (yyTraceFILE) {
		fprintf(yyTraceFILE, "%sInput '%s'\n", yyTracePrompt,
			yyTokenName[yymajor]);
	}
#endif

	do {
		yyact = yy_find_shift_action(yypParser, (YYCODETYPE) yymajor);
		if (yyact <= YY_MAX_SHIFTREDUCE) {
			yy_shift(yypParser, yyact, yymajor, yyminor);
#ifndef YYNOERRORRECOVERY
			yypParser->yyerrcnt--;
#endif
			yymajor = YYNOCODE;
		} else if (yyact <= YY_MAX_REDUCE) {
			yy_reduce(yypParser, yyact - YY_MIN_REDUCE);
		} else {
			assert(yyact == YY_ERROR_ACTION);
			yyminorunion.yy0 = yyminor;
#ifdef YYERRORSYMBOL
			int yymx;
#endif
#ifndef NDEBUG
			if (yyTraceFILE) {
				fprintf(yyTraceFILE, "%sSyntax Error!\n",
					yyTracePrompt);
			}
#endif
#ifdef YYERRORSYMBOL
			/* A syntax error has occurred.
			 * The response to an error depends upon whether or not the
			 * grammar defines an error token "ERROR".
			 *
			 * This is what we do if the grammar does define ERROR:
			 *
			 *  * Call the %syntax_error function.
			 *
			 *  * Begin popping the stack until we enter a state where
			 *    it is legal to shift the error symbol, then shift
			 *    the error symbol.
			 *
			 *  * Set the error count to three.
			 *
			 *  * Begin accepting and shifting new tokens.  No new error
			 *    processing will occur until three tokens have been
			 *    shifted successfully.
			 *
			 */
			if (yypParser->yyerrcnt < 0) {
				yy_syntax_error(yypParser, yymajor, yyminor);
			}
			yymx = yypParser->yytos->major;
			if (yymx == YYERRORSYMBOL || yyerrorhit) {
#ifndef NDEBUG
				if (yyTraceFILE) {
					fprintf(yyTraceFILE,
						"%sDiscard input token %s\n",
						yyTracePrompt,
						yyTokenName[yymajor]);
				}
#endif
				yy_destructor(yypParser, (YYCODETYPE) yymajor,
					      &yyminorunion);
				yymajor = YYNOCODE;
			} else {
				while (yypParser->yytos >= yypParser->yystack
				       && yymx != YYERRORSYMBOL
				       && (yyact =
					   yy_find_reduce_action(yypParser->
								 yytos->stateno,
								 YYERRORSYMBOL))
				       >= YY_MIN_REDUCE) {
					yy_pop_parser_stack(yypParser);
				}
				if (yypParser->yytos < yypParser->yystack
				    || yymajor == 0) {
					yy_destructor(yypParser,
						      (YYCODETYPE) yymajor,
						      &yyminorunion);
					yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
					yypParser->yyerrcnt = -1;
#endif
					yymajor = YYNOCODE;
				} else if (yymx != YYERRORSYMBOL) {
					yy_shift(yypParser, yyact,
						 YYERRORSYMBOL, yyminor);
				}
			}
			yypParser->yyerrcnt = 3;
			yyerrorhit = 1;
#elif defined(YYNOERRORRECOVERY)
			/* If the YYNOERRORRECOVERY macro is defined, then do not attempt to
			 * do any kind of error recovery.  Instead, simply invoke the syntax
			 * error routine and continue going as if nothing had happened.
			 *
			 * Applications can set this macro (for example inside %include) if
			 * they intend to abandon the parse upon the first syntax error seen.
			 */
			yy_syntax_error(yypParser, yymajor, yyminor);
			yy_destructor(yypParser, (YYCODETYPE) yymajor,
				      &yyminorunion);
			yymajor = YYNOCODE;

#else				/* YYERRORSYMBOL is not defined */
			/* This is what we do if the grammar does not define ERROR:
			 *
			 *  * Report an error message, and throw away the input token.
			 *
			 *  * If the input token is $, then fail the parse.
			 *
			 * As before, subsequent error messages are suppressed until
			 * three input tokens have been successfully shifted.
			 */
			if (yypParser->yyerrcnt <= 0) {
				yy_syntax_error(yypParser, yymajor, yyminor);
			}
			yypParser->yyerrcnt = 3;
			yy_destructor(yypParser, (YYCODETYPE) yymajor,
				      &yyminorunion);
			if (yyendofinput) {
				yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
				yypParser->yyerrcnt = -1;
#endif
			}
			yymajor = YYNOCODE;
#endif
		}
	} while (yymajor != YYNOCODE && yypParser->yytos > yypParser->yystack);
#ifndef NDEBUG
	if (yyTraceFILE) {
		yyStackEntry *i;
		char cDiv = '[';
		fprintf(yyTraceFILE, "%sReturn. Stack=", yyTracePrompt);
		for (i = &yypParser->yystack[1]; i <= yypParser->yytos; i++) {
			fprintf(yyTraceFILE, "%c%s", cDiv,
				yyTokenName[i->major]);
			cDiv = ' ';
		}
		fprintf(yyTraceFILE, "]\n");
	}
#endif
	return;
}
