/*
** 2000-05-29
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Driver template for the LEMON parser generator.
**
** The "lemon" program processes an LALR(1) input grammar file, then uses
** this template to construct a parser.  The "lemon" program inserts text
** at each "%%" line.  Also, any "P-a-r-s-e" identifer prefix (without the
** interstitial "-" characters) contained in this template is changed into
** the value of the %name directive from the grammar.  Otherwise, the content
** of this template is copied straight through into the generate parser
** source file.
**
** The following is the concatenation of all %include directives from the
** input grammar file:
*/
#include <stdio.h>
#include <stdbool.h>
/************ Begin %include sections from the grammar ************************/
#line 52 "parse.y"

#include "sqliteInt.h"

/*
** Disable all error recovery processing in the parser push-down
** automaton.
*/
#define YYNOERRORRECOVERY 1

/*
** Make yytestcase() the same as testcase()
*/
#define yytestcase(X) testcase(X)

/*
** Indicate that sqlite3ParserFree() will never be called with a null
** pointer.
*/
#define YYPARSEFREENEVERNULL 1

/*
** Alternative datatype for the argument to the malloc() routine passed
** into sqlite3ParserAlloc().  The default is size_t.
*/
#define YYMALLOCARGTYPE  u64

/*
** An instance of this structure holds information about the
** LIMIT clause of a SELECT statement.
*/
struct LimitVal {
  Expr *pLimit;    /* The LIMIT expression.  NULL if there is no limit */
  Expr *pOffset;   /* The OFFSET expression.  NULL if there is none */
};

/*
** An instance of the following structure describes the event of a
** TRIGGER.  "a" is the event type, one of TK_UPDATE, TK_INSERT,
** TK_DELETE, or TK_INSTEAD.  If the event is of the form
**
**      UPDATE ON (a,b,c)
**
** Then the "b" IdList records the list "a,b,c".
*/
struct TrigEvent { int a; IdList * b; };

/*
** Disable lookaside memory allocation for objects that might be
** shared across database connections.
*/
static void disableLookaside(Parse *pParse){
  pParse->disableLookaside++;
  pParse->db->lookaside.bDisable++;
}

#line 402 "parse.y"

  /*
  ** For a compound SELECT statement, make sure p->pPrior->pNext==p for
  ** all elements in the list.  And make sure list length does not exceed
  ** SQLITE_LIMIT_COMPOUND_SELECT.
  */
  static void parserDoubleLinkSelect(Parse *pParse, Select *p){
    if( p->pPrior ){
      Select *pNext = 0, *pLoop;
      int mxSelect, cnt = 0;
      for(pLoop=p; pLoop; pNext=pLoop, pLoop=pLoop->pPrior, cnt++){
        pLoop->pNext = pNext;
        pLoop->selFlags |= SF_Compound;
      }
      if( (p->selFlags & SF_MultiValue)==0 && 
        (mxSelect = pParse->db->aLimit[SQLITE_LIMIT_COMPOUND_SELECT])>0 &&
        cnt>mxSelect
      ){
        sqlite3ErrorMsg(pParse, "Too many UNION or EXCEPT or INTERSECT operations");
      }
    }
  }
#line 841 "parse.y"

  /* This is a utility routine used to set the ExprSpan.zStart and
  ** ExprSpan.zEnd values of pOut so that the span covers the complete
  ** range of text beginning with pStart and going to the end of pEnd.
  */
  static void spanSet(ExprSpan *pOut, Token *pStart, Token *pEnd){
    pOut->zStart = pStart->z;
    pOut->zEnd = &pEnd->z[pEnd->n];
  }

  /* Construct a new Expr object from a single identifier.  Use the
  ** new Expr to populate pOut.  Set the span of pOut to be the identifier
  ** that created the expression.
  */
  static void spanExpr(ExprSpan *pOut, Parse *pParse, int op, Token t){
    Expr *p = sqlite3DbMallocRawNN(pParse->db, sizeof(Expr)+t.n+1);
    if( p ){
      memset(p, 0, sizeof(Expr));
      p->op = (u8)op;
      p->flags = EP_Leaf;
      p->iAgg = -1;
      p->u.zToken = (char*)&p[1];
      memcpy(p->u.zToken, t.z, t.n);
      p->u.zToken[t.n] = 0;
      if (op != TK_VARIABLE){
        sqlite3NormalizeName(p->u.zToken);
      }
#if SQLITE_MAX_EXPR_DEPTH>0
      p->nHeight = 1;
#endif  
    }
    pOut->pExpr = p;
    pOut->zStart = t.z;
    pOut->zEnd = &t.z[t.n];
  }
#line 949 "parse.y"

  /* This routine constructs a binary expression node out of two ExprSpan
  ** objects and uses the result to populate a new ExprSpan object.
  */
  static void spanBinaryExpr(
    Parse *pParse,      /* The parsing context.  Errors accumulate here */
    int op,             /* The binary operation */
    ExprSpan *pLeft,    /* The left operand, and output */
    ExprSpan *pRight    /* The right operand */
  ){
    pLeft->pExpr = sqlite3PExpr(pParse, op, pLeft->pExpr, pRight->pExpr);
    pLeft->zEnd = pRight->zEnd;
  }

  /* If doNot is true, then add a TK_NOT Expr-node wrapper around the
  ** outside of *ppExpr.
  */
  static void exprNot(Parse *pParse, int doNot, ExprSpan *pSpan){
    if( doNot ){
      pSpan->pExpr = sqlite3PExpr(pParse, TK_NOT, pSpan->pExpr, 0);
    }
  }
#line 1023 "parse.y"

  /* Construct an expression node for a unary postfix operator
  */
  static void spanUnaryPostfix(
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand, and output */
    Token *pPostOp         /* The operand token for setting the span */
  ){
    pOperand->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0);
    pOperand->zEnd = &pPostOp->z[pPostOp->n];
  }                           
#line 1040 "parse.y"

  /* A routine to convert a binary TK_IS or TK_ISNOT expression into a
  ** unary TK_ISNULL or TK_NOTNULL expression. */
  static void binaryToUnaryIfNull(Parse *pParse, Expr *pY, Expr *pA, int op){
    sqlite3 *db = pParse->db;
    if( pA && pY && pY->op==TK_NULL ){
      pA->op = (u8)op;
      sqlite3ExprDelete(db, pA->pRight);
      pA->pRight = 0;
    }
  }
#line 1068 "parse.y"

  /* Construct an expression node for a unary prefix operator
  */
  static void spanUnaryPrefix(
    ExprSpan *pOut,        /* Write the new expression node here */
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand */
    Token *pPreOp         /* The operand token for setting the span */
  ){
    pOut->zStart = pPreOp->z;
    pOut->pExpr = sqlite3PExpr(pParse, op, pOperand->pExpr, 0);
    pOut->zEnd = pOperand->zEnd;
  }
#line 1273 "parse.y"

  /* Add a single new term to an ExprList that is used to store a
  ** list of identifiers.  Report an error if the ID list contains
  ** a COLLATE clause or an ASC or DESC keyword, except ignore the
  ** error while parsing a legacy schema.
  */
  static ExprList *parserAddExprIdListTerm(
    Parse *pParse,
    ExprList *pPrior,
    Token *pIdToken,
    int hasCollate,
    int sortOrder
  ){
    ExprList *p = sqlite3ExprListAppend(pParse, pPrior, 0);
    if( (hasCollate || sortOrder!=SQLITE_SO_UNDEFINED)
        && pParse->db->init.busy==0
    ){
      sqlite3ErrorMsg(pParse, "syntax error after column name \"%.*s\"",
                         pIdToken->n, pIdToken->z);
    }
    sqlite3ExprListSetName(pParse, p, pIdToken, 1);
    return p;
  }
#line 231 "parse.c"
/**************** End of %include directives **********************************/
/* These constants specify the various numeric values for terminal symbols
** in a format understandable to "makeheaders".  This section is blank unless
** "lemon" is run with the "-m" command-line option.
***************** Begin makeheaders token definitions *************************/
/**************** End makeheaders token definitions ***************************/

/* The next sections is a series of control #defines.
** various aspects of the generated parser.
**    YYCODETYPE         is the data type used to store the integer codes
**                       that represent terminal and non-terminal symbols.
**                       "unsigned char" is used if there are fewer than
**                       256 symbols.  Larger types otherwise.
**    YYNOCODE           is a number of type YYCODETYPE that is not used for
**                       any terminal or nonterminal symbol.
**    YYFALLBACK         If defined, this indicates that one or more tokens
**                       (also known as: "terminal symbols") have fall-back
**                       values which should be used if the original symbol
**                       would not parse.  This permits keywords to sometimes
**                       be used as identifiers, for example.
**    YYACTIONTYPE       is the data type used for "action codes" - numbers
**                       that indicate what to do in response to the next
**                       token.
**    sqlite3ParserTOKENTYPE     is the data type used for minor type for terminal
**                       symbols.  Background: A "minor type" is a semantic
**                       value associated with a terminal or non-terminal
**                       symbols.  For example, for an "ID" terminal symbol,
**                       the minor type might be the name of the identifier.
**                       Each non-terminal can have a different minor type.
**                       Terminal symbols all have the same minor type, though.
**                       This macros defines the minor type for terminal 
**                       symbols.
**    YYMINORTYPE        is the data type used for all minor types.
**                       This is typically a union of many types, one of
**                       which is sqlite3ParserTOKENTYPE.  The entry in the union
**                       for terminal symbols is called "yy0".
**    YYSTACKDEPTH       is the maximum depth of the parser's stack.  If
**                       zero the stack is dynamically sized using realloc()
**    sqlite3ParserARG_SDECL     A static variable declaration for the %extra_argument
**    sqlite3ParserARG_PDECL     A parameter declaration for the %extra_argument
**    sqlite3ParserARG_STORE     Code to store %extra_argument into yypParser
**    sqlite3ParserARG_FETCH     Code to extract %extra_argument from yypParser
**    YYERRORSYMBOL      is the code number of the error symbol.  If not
**                       defined, then do no error processing.
**    YYNSTATE           the combined number of states.
**    YYNRULE            the number of rules in the grammar
**    YY_MAX_SHIFT       Maximum value for shift actions
**    YY_MIN_SHIFTREDUCE Minimum value for shift-reduce actions
**    YY_MAX_SHIFTREDUCE Maximum value for shift-reduce actions
**    YY_MIN_REDUCE      Maximum value for reduce actions
**    YY_ERROR_ACTION    The yy_action[] code for syntax error
**    YY_ACCEPT_ACTION   The yy_action[] code for accept
**    YY_NO_ACTION       The yy_action[] code for no-op
*/
#ifndef INTERFACE
# define INTERFACE 1
#endif
/************* Begin control #defines *****************************************/
#define YYCODETYPE unsigned char
#define YYNOCODE 236
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 75
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  int yy92;
  ExprList* yy93;
  struct {int value; int mask;} yy97;
  Expr* yy112;
  With* yy121;
  SrcList* yy167;
  TriggerStep* yy177;
  struct TrigEvent yy270;
  Select* yy329;
  IdList* yy330;
  ExprSpan yy412;
  struct LimitVal yy464;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYFALLBACK 1
#define YYNSTATE             419
#define YYNRULE              305
#define YY_MAX_SHIFT         418
#define YY_MIN_SHIFTREDUCE   617
#define YY_MAX_SHIFTREDUCE   921
#define YY_MIN_REDUCE        922
#define YY_MAX_REDUCE        1226
#define YY_ERROR_ACTION      1227
#define YY_ACCEPT_ACTION     1228
#define YY_NO_ACTION         1229
/************* End control #defines *******************************************/

/* Define the yytestcase() macro to be a no-op if is not already defined
** otherwise.
**
** Applications can choose to define yytestcase() in the %include section
** to a macro that can assist in verifying code coverage.  For production
** code the yytestcase() macro should be turned off.  But it is useful
** for testing.
*/
#ifndef yytestcase
# define yytestcase(X)
#endif


/* Next are the tables used to determine what action to take based on the
** current state and lookahead token.  These tables are used to implement
** functions that take a state number and lookahead value and return an
** action integer.  
**
** Suppose the action integer is N.  Then the action is determined as
** follows
**
**   0 <= N <= YY_MAX_SHIFT             Shift N.  That is, push the lookahead
**                                      token onto the stack and goto state N.
**
**   N between YY_MIN_SHIFTREDUCE       Shift to an arbitrary state then
**     and YY_MAX_SHIFTREDUCE           reduce by rule N-YY_MIN_SHIFTREDUCE.
**
**   N between YY_MIN_REDUCE            Reduce by rule N-YY_MIN_REDUCE
**     and YY_MAX_REDUCE
**
**   N == YY_ERROR_ACTION               A syntax error has occurred.
**
**   N == YY_ACCEPT_ACTION              The parser accepts its input.
**
**   N == YY_NO_ACTION                  No such action.  Denotes unused
**                                      slots in the yy_action[] table.
**
** The action table is constructed as a single large table named yy_action[].
** Given state S and lookahead X, the action is computed as either:
**
**    (A)   N = yy_action[ yy_shift_ofst[S] + X ]
**    (B)   N = yy_default[S]
**
** The (A) formula is preferred.  The B formula is used instead if:
**    (1)  The yy_shift_ofst[S]+X value is out of range, or
**    (2)  yy_lookahead[yy_shift_ofst[S]+X] is not equal to X, or
**    (3)  yy_shift_ofst[S] equal YY_SHIFT_USE_DFLT.
** (Implementation note: YY_SHIFT_USE_DFLT is chosen so that
** YY_SHIFT_USE_DFLT+X will be out of range for all possible lookaheads X.
** Hence only tests (1) and (2) need to be evaluated.)
**
** The formulas above are for computing the action when the lookahead is
** a terminal symbol.  If the lookahead is a non-terminal (as occurs after
** a reduce action) then the yy_reduce_ofst[] array is used in place of
** the yy_shift_ofst[] array and YY_REDUCE_USE_DFLT is used in place of
** YY_SHIFT_USE_DFLT.
**
** The following are the tables generated in this section:
**
**  yy_action[]        A single table containing all actions.
**  yy_lookahead[]     A table containing the lookahead for each entry in
**                     yy_action.  Used to detect hash collisions.
**  yy_shift_ofst[]    For each state, the offset into yy_action for
**                     shifting terminals.
**  yy_reduce_ofst[]   For each state, the offset into yy_action for
**                     shifting non-terminals after a reduce.
**  yy_default[]       Default action for each state.
**
*********** Begin parsing tables **********************************************/
#define YY_ACTTAB_COUNT (1416)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    91,   92,  293,   82,  786,  786,  798,  801,  790,  790,
 /*    10 */    89,   89,   90,   90,   90,   90,  315,   88,   88,   88,
 /*    20 */    88,   87,   87,   86,   86,   86,   85,  315,   90,   90,
 /*    30 */    90,   90,   83,   88,   88,   88,   88,   87,   87,   86,
 /*    40 */    86,   86,   85,  315,  212,  639,  639,  904,   90,   90,
 /*    50 */    90,   90,  124,   88,   88,   88,   88,   87,   87,   86,
 /*    60 */    86,   86,   85,  315,   87,   87,   86,   86,   86,   85,
 /*    70 */   315,  904,   86,   86,   86,   85,  315,   91,   92,  293,
 /*    80 */    82,  786,  786,  798,  801,  790,  790,   89,   89,   90,
 /*    90 */    90,   90,   90,  645,   88,   88,   88,   88,   87,   87,
 /*   100 */    86,   86,   86,   85,  315,   91,   92,  293,   82,  786,
 /*   110 */   786,  798,  801,  790,  790,   89,   89,   90,   90,   90,
 /*   120 */    90,  648,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   130 */    86,   85,  315,  313,  312,   91,   92,  293,   82,  786,
 /*   140 */   786,  798,  801,  790,  790,   89,   89,   90,   90,   90,
 /*   150 */    90,   67,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   160 */    86,   85,  315,  707, 1228,  418,    3,  271,   93,   84,
 /*   170 */    81,  178,  412,  412,  412,   84,   81,  178,  249,  905,
 /*   180 */   905,  736,  737,  647,   91,   92,  293,   82,  786,  786,
 /*   190 */   798,  801,  790,  790,   89,   89,   90,   90,   90,   90,
 /*   200 */   302,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   210 */    85,  315,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   220 */    86,   85,  315,  915,  776,  915,  769,  653,  270,  764,
 /*   230 */   838,  906,  723,   91,   92,  293,   82,  786,  786,  798,
 /*   240 */   801,  790,  790,   89,   89,   90,   90,   90,   90,  762,
 /*   250 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   260 */   315,  171,  758,  143,  417,  417,  768,  768,  770,  280,
 /*   270 */   223,  735,  121,  289,  734,  896,  646,  694,  242,  340,
 /*   280 */   241,  349,   91,   92,  293,   82,  786,  786,  798,  801,
 /*   290 */   790,  790,   89,   89,   90,   90,   90,   90,  664,   88,
 /*   300 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  315,
 /*   310 */    22,  203,  758,  334,  366,  363,  362,  408,   84,   81,
 /*   320 */   178,   84,   81,  178,   64,  777,  361,  124,  242,  340,
 /*   330 */   241,   91,   92,  293,   82,  786,  786,  798,  801,  790,
 /*   340 */   790,   89,   89,   90,   90,   90,   90,  860,   88,   88,
 /*   350 */    88,   88,   87,   87,   86,   86,   86,   85,  315,  681,
 /*   360 */   660,  714,  861,  314,  314,  314,  192,    5,  681,  212,
 /*   370 */   862,  687,  904,  375,  763,  699,  699,  124,  688,  663,
 /*   380 */    91,   92,  293,   82,  786,  786,  798,  801,  790,  790,
 /*   390 */    89,   89,   90,   90,   90,   90,  904,   88,   88,   88,
 /*   400 */    88,   87,   87,   86,   86,   86,   85,  315,   91,   92,
 /*   410 */   293,   82,  786,  786,  798,  801,  790,  790,   89,   89,
 /*   420 */    90,   90,   90,   90,  662,   88,   88,   88,   88,   87,
 /*   430 */    87,   86,   86,   86,   85,  315,   91,   92,  293,   82,
 /*   440 */   786,  786,  798,  801,  790,  790,   89,   89,   90,   90,
 /*   450 */    90,   90,  211,   88,   88,   88,   88,   87,   87,   86,
 /*   460 */    86,   86,   85,  315,   91,   92,  293,   82,  786,  786,
 /*   470 */   798,  801,  790,  790,   89,   89,   90,   90,   90,   90,
 /*   480 */   147,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   490 */    85,  315,  844,  844,  335, 1177, 1177,  386,   70,  293,
 /*   500 */    82,  786,  786,  798,  801,  790,  790,   89,   89,   90,
 /*   510 */    90,   90,   90,  349,   88,   88,   88,   88,   87,   87,
 /*   520 */    86,   86,   86,   85,  315,  166,   73,  787,  787,  799,
 /*   530 */   802,  124,   91,   80,  293,   82,  786,  786,  798,  801,
 /*   540 */   790,  790,   89,   89,   90,   90,   90,   90,  397,   88,
 /*   550 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  315,
 /*   560 */    92,  293,   82,  786,  786,  798,  801,  790,  790,   89,
 /*   570 */    89,   90,   90,   90,   90,   78,   88,   88,   88,   88,
 /*   580 */    87,   87,   86,   86,   86,   85,  315,  179,  179,  639,
 /*   590 */   639,  696,  307,  414,   75,   76,  338,  791,  240,  379,
 /*   600 */   287,   77,  379,  285,  284,  283,  225,  281,  414,   78,
 /*   610 */   631,   48,   48,  414,  410,    2, 1120,  304,  618,  318,
 /*   620 */   316,  316,  841,  758,  840,  857,   48,   48,   75,   76,
 /*   630 */   333,   10,   10,  220,  376,   77,  169,  399,  184,  242,
 /*   640 */   330,  230,  776,  345,  769,  301,  308,  764,  410,    2,
 /*   650 */   343,  414,  220,  376,  316,  316,   85,  315,  110,  294,
 /*   660 */   393,  387,  221,  157,  259,  369,  254,  368,  207,   48,
 /*   670 */    48,  399,  766,  145,  139,  252,  776,  261,  769,  639,
 /*   680 */   639,  764,  341,  889,  768,  768,  770,  771,  413,   18,
 /*   690 */   767,  414,  186,  110,  892,   23,  686,  686,  182,  322,
 /*   700 */   339,  124,  655,  124,  392,  394,  766,  889,  414,   47,
 /*   710 */    47,   74,  414,   72,   78,  187,  313,  312,  768,  768,
 /*   720 */   770,  771,  413,   18,  179,  179,   48,   48,  684,  684,
 /*   730 */    10,   10,  327,   75,   76,  708,  379,  124,  343,  720,
 /*   740 */    77,  325,  639,  639,  303,   78,  159,  158,  905,  905,
 /*   750 */   328,  414,  270,  410,    2,  709,  325,  324,  205,  316,
 /*   760 */   316,  392,  377,  269,   75,   76,  910,   95,  914,   48,
 /*   770 */    48,   77,  649,  649,  380,  912,  399,  913,  229,  639,
 /*   780 */   639,  776,  140,  769,  410,    2,  764,  639,  639,  124,
 /*   790 */   316,  316,    9,    9,  776,  354,  769,  414,  351,  764,
 /*   800 */   906,  722,  755,  193,  392,  382,  915,  399,  915,  143,
 /*   810 */   720,  766,  776,  414,  769,   10,   10,  764,  325,  288,
 /*   820 */   358,  414,  205,  768,  768,  770,  771,  413,   18,  202,
 /*   830 */   373,   48,   48,  414,  295,  110,  768,  768,  770,   30,
 /*   840 */    30,  404,  766,  639,  639,  263,  414,  762,  857,  188,
 /*   850 */    68,   48,   48,  673,  768,  768,  770,  771,  413,   18,
 /*   860 */   720,  271,  247,  110,   10,   10,  392,  391,  203,   75,
 /*   870 */    76,  366,  363,  362,  374,  371,   77,  326,  389,  720,
 /*   880 */   265,  110,  381,  361,  235,  174,  392,  372,  721,  410,
 /*   890 */     2,  414,  831,  833,  311,  316,  316,  143,  295,  189,
 /*   900 */   821,  720,  165,  252,  855,  341,  414,  292,  674,   34,
 /*   910 */    34,  298,  399,  246,  720,  110,  720,  776,  863,  769,
 /*   920 */   762,  860,  764,  350,   35,   35,  204,  667,  204,  349,
 /*   930 */   164,  176,  414,  367,  720,  234,  861,  110,  668,  414,
 /*   940 */   331,  167,  721,  704,  862,  398,  238,  766,  703,  692,
 /*   950 */    36,   36,  414,  693,  831,  689,  305,   37,   37,  768,
 /*   960 */   768,  770,  771,  413,   18,  268,  233,  414,  232,  414,
 /*   970 */    38,   38,  414,  237,  414,    7,  414,  635,  414,  163,
 /*   980 */   162,  161,  110,  414,  299,   26,   26,   27,   27,  414,
 /*   990 */    29,   29,   39,   39,   40,   40,   41,   41,  414,  148,
 /*  1000 */   414,   11,   11,  414,  383,  414,  347,   42,   42,  414,
 /*  1010 */   110,  414,  239,  414,  195,  414,   97,   97,   43,   43,
 /*  1020 */   414,   44,   44,   31,   31,  869,  219,   45,   45,   46,
 /*  1030 */    46,   32,   32,  113,  113,  414,   54,  414,  114,  114,
 /*  1040 */   868,  414,  401,  414,  720,  212,  414,  866,  904,  414,
 /*  1050 */   384,  414,  348,  115,  115,   52,   52,  762,  414,   33,
 /*  1060 */    33,   98,   98,  414,   49,   49,  762,   99,   99,  100,
 /*  1070 */   100,  414,  904,  414,  721,  110,   96,   96,  414,  750,
 /*  1080 */    24,  112,  112,  321,  414,  639,  639,  414,  258,  109,
 /*  1090 */   109,  104,  104,  414,  638,  296,  103,  103,  414,  257,
 /*  1100 */   414,  405,  101,  101,  414,  102,  102,  414,    1,   66,
 /*  1110 */   107,   51,   51,  639,  639,  643,   53,   53,   50,   50,
 /*  1120 */   903,  300,   25,   25,  409,   28,   28,  167,  721,   20,
 /*  1130 */   309,  319,  411,  296,  110, 1200,  177,  176,  676,  643,
 /*  1140 */   736,  737,  658,  704,  111,  180,  151,  760,  703,  342,
 /*  1150 */   210,  344,  210,  243,  210,  359,   66,  110,  216,  250,
 /*  1160 */    19,  837,   66,  837,  656,  656,  666,  665,  701,  323,
 /*  1170 */   730,   69,  155,  210,  824,  828,  828,  216,  836,  641,
 /*  1180 */   836,  835,  106,  297,  772,  772,  329,  231,  829,  170,
 /*  1190 */   854,  852,  346,  851,  352,  353,  245,  630,  248,  364,
 /*  1200 */   944,  677,  160,  256,  661,  660,  267,  400,  826,  253,
 /*  1210 */   728,  761,  710,  272,  137,  273,  278,  156,  644,  637,
 /*  1220 */   825,  628,  627,  885,  629,  882,  921,  125,  126,  119,
 /*  1230 */    64,  747,  332,  839,  236,  128,   55,  337,  357,  191,
 /*  1240 */   130,  149,  146,  131,  198,  199,  200,  370,  306,  658,
 /*  1250 */   385,  680,  355,  290,  132,  133,  679,   63,  678,    6,
 /*  1260 */    71,  141,  757,  671,  310,   94,  390,  652,  291,   65,
 /*  1270 */   856,  651,  388,  222,  255,  820,   21,  883,  227,  670,
 /*  1280 */   650,  623,  894,  620,  317,  228,  181,  320,  183,  108,
 /*  1290 */   834,  416,  224,  634,  415,  226,  832,  625,  624,  621,
 /*  1300 */   185,  403,  286,  127,  407,  756,  116,  117,  122,  129,
 /*  1310 */   690,  190,  210,  842,  134,  135,  850,  718,  136,  118,
 /*  1320 */   917,  336,  138,   56,  260,  105,  719,  262,   57,  206,
 /*  1330 */   276,  717,  716,  264,  266,  700,  274,  277,  275,   58,
 /*  1340 */    59,  123,  853,  196,  194,  806,  849,    8,  213,   12,
 /*  1350 */   197,  633,  244,  150,  356,  214,  215,  257,  201,  360,
 /*  1360 */   365,  142,  208,   60,   13,  669,  251,   14,   61,  120,
 /*  1370 */   775,  172,  698,  774,  804,   15,  209,  702,   62,    4,
 /*  1380 */   144,  378,  173,  175,  729,  724,  168,   69,   16,   66,
 /*  1390 */    17,  819,  805,  396,  803,  395,  859,  858,  808,  875,
 /*  1400 */   152,  402,  218,  876,  153,  217,  154,  406,  617,  807,
 /*  1410 */   773,  642,   79,  282,  279, 1182,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*    10 */    15,   16,   17,   18,   19,   20,   32,   22,   23,   24,
 /*    20 */    25,   26,   27,   28,   29,   30,   31,   32,   17,   18,
 /*    30 */    19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
 /*    40 */    29,   30,   31,   32,   49,   52,   53,   52,   17,   18,
 /*    50 */    19,   20,  135,   22,   23,   24,   25,   26,   27,   28,
 /*    60 */    29,   30,   31,   32,   26,   27,   28,   29,   30,   31,
 /*    70 */    32,   76,   28,   29,   30,   31,   32,    5,    6,    7,
 /*    80 */     8,    9,   10,   11,   12,   13,   14,   15,   16,   17,
 /*    90 */    18,   19,   20,  163,   22,   23,   24,   25,   26,   27,
 /*   100 */    28,   29,   30,   31,   32,    5,    6,    7,    8,    9,
 /*   110 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   120 */    20,  163,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   130 */    30,   31,   32,   26,   27,    5,    6,    7,    8,    9,
 /*   140 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   150 */    20,   51,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   160 */    30,   31,   32,  201,  138,  139,  140,  145,   68,  213,
 /*   170 */   214,  215,  159,  160,  161,  213,  214,  215,   48,   52,
 /*   180 */    53,  109,  110,  163,    5,    6,    7,    8,    9,   10,
 /*   190 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   200 */   178,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   210 */    31,   32,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   220 */    30,   31,   32,  116,   74,  118,   76,   48,  145,   79,
 /*   230 */    38,  104,  105,    5,    6,    7,    8,    9,   10,   11,
 /*   240 */    12,   13,   14,   15,   16,   17,   18,   19,   20,  145,
 /*   250 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   260 */    32,   51,   70,  145,  141,  142,  116,  117,  118,  151,
 /*   270 */   147,  166,  149,  155,  166,  176,   48,  154,   86,   87,
 /*   280 */    88,  145,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   290 */    13,   14,   15,   16,   17,   18,   19,   20,  172,   22,
 /*   300 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   310 */   187,   77,   70,  209,   80,   81,   82,  234,  213,  214,
 /*   320 */   215,  213,  214,  215,  114,   48,   92,  135,   86,   87,
 /*   330 */    88,    5,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   340 */    14,   15,   16,   17,   18,   19,   20,   39,   22,   23,
 /*   350 */    24,   25,   26,   27,   28,   29,   30,   31,   32,  170,
 /*   360 */   171,  204,   54,  159,  160,  161,  230,   47,  179,   49,
 /*   370 */    62,   63,   52,   95,   48,   97,   98,  135,   70,  172,
 /*   380 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*   390 */    15,   16,   17,   18,   19,   20,   76,   22,   23,   24,
 /*   400 */    25,   26,   27,   28,   29,   30,   31,   32,    5,    6,
 /*   410 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   420 */    17,   18,   19,   20,  172,   22,   23,   24,   25,   26,
 /*   430 */    27,   28,   29,   30,   31,   32,    5,    6,    7,    8,
 /*   440 */     9,   10,   11,   12,   13,   14,   15,   16,   17,   18,
 /*   450 */    19,   20,  201,   22,   23,   24,   25,   26,   27,   28,
 /*   460 */    29,   30,   31,   32,    5,    6,    7,    8,    9,   10,
 /*   470 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   480 */    49,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   490 */    31,   32,   86,   87,   88,   99,  100,  208,  123,    7,
 /*   500 */     8,    9,   10,   11,   12,   13,   14,   15,   16,   17,
 /*   510 */    18,   19,   20,  145,   22,   23,   24,   25,   26,   27,
 /*   520 */    28,   29,   30,   31,   32,  208,  123,    9,   10,   11,
 /*   530 */    12,  135,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   540 */    13,   14,   15,   16,   17,   18,   19,   20,  182,   22,
 /*   550 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   560 */     6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
 /*   570 */    16,   17,   18,   19,   20,    7,   22,   23,   24,   25,
 /*   580 */    26,   27,   28,   29,   30,   31,   32,  185,  186,   52,
 /*   590 */    53,  186,    7,  145,   26,   27,  226,   79,  230,  197,
 /*   600 */    34,   33,  197,   37,   38,   39,   40,   41,  145,    7,
 /*   610 */    44,  163,  164,  145,   46,   47,   48,   32,    1,    2,
 /*   620 */    52,   53,   57,   70,   59,  154,  163,  164,   26,   27,
 /*   630 */    65,  163,  164,   99,  100,   33,  225,   69,   72,   86,
 /*   640 */    87,   88,   74,  145,   76,  177,  198,   79,   46,   47,
 /*   650 */   145,  145,   99,  100,   52,   53,   31,   32,  187,   93,
 /*   660 */   154,  198,   77,   78,   79,   80,   81,   82,   83,  163,
 /*   670 */   164,   69,  104,  136,   47,   90,   74,  201,   76,   52,
 /*   680 */    53,   79,  211,   52,  116,  117,  118,  119,  120,  121,
 /*   690 */   145,  145,  126,  187,  162,  224,  181,  182,  132,  133,
 /*   700 */   229,  135,  170,  135,  198,  199,  104,   76,  145,  163,
 /*   710 */   164,  122,  145,  124,    7,  210,   26,   27,  116,  117,
 /*   720 */   118,  119,  120,  121,  185,  186,  163,  164,  181,  182,
 /*   730 */   163,  164,   78,   26,   27,   28,  197,  135,  145,  145,
 /*   740 */    33,  145,   52,   53,  177,    7,   26,   27,   52,   53,
 /*   750 */   211,  145,  145,   46,   47,   28,  160,  161,    9,   52,
 /*   760 */    53,  198,  199,  217,   26,   27,   76,   47,   78,  163,
 /*   770 */   164,   33,   52,   53,  145,   85,   69,   87,  184,   52,
 /*   780 */    53,   74,   47,   76,   46,   47,   79,   52,   53,  135,
 /*   790 */    52,   53,  163,  164,   74,  220,   76,  145,  223,   79,
 /*   800 */   104,  105,  154,  210,  198,  199,  116,   69,  118,  145,
 /*   810 */   145,  104,   74,  145,   76,  163,  164,   79,  222,  155,
 /*   820 */     7,  145,    9,  116,  117,  118,  119,  120,  121,  177,
 /*   830 */   154,  163,  164,  145,   85,  187,  116,  117,  118,  163,
 /*   840 */   164,  234,  104,   52,   53,  201,  145,  145,  154,  184,
 /*   850 */     7,  163,  164,   36,  116,  117,  118,  119,  120,  121,
 /*   860 */   145,  145,   43,  187,  163,  164,  198,  199,   77,   26,
 /*   870 */    27,   80,   81,   82,  198,   28,   33,  145,  177,  145,
 /*   880 */   201,  187,  154,   92,   43,   48,  198,  199,   51,   46,
 /*   890 */    47,  145,  160,  161,  178,   52,   53,  145,   85,  184,
 /*   900 */    81,  145,  145,   90,  154,  211,  145,  155,   91,  163,
 /*   910 */   164,  209,   69,   94,  145,  187,  145,   74,  184,   76,
 /*   920 */   145,   39,   79,  229,  163,  164,  174,   60,  176,  145,
 /*   930 */   202,  203,  145,   66,  145,   94,   54,  187,   71,  145,
 /*   940 */   184,  104,  105,   96,   62,   63,  127,  104,  101,  154,
 /*   950 */   163,  164,  145,  184,  222,  184,   89,  163,  164,  116,
 /*   960 */   117,  118,  119,  120,  121,  145,  125,  145,  127,  145,
 /*   970 */   163,  164,  145,  184,  145,  189,  145,  154,  145,   86,
 /*   980 */    87,   88,  187,  145,  209,  163,  164,  163,  164,  145,
 /*   990 */   163,  164,  163,  164,  163,  164,  163,  164,  145,  188,
 /*  1000 */   145,  163,  164,  145,    7,  145,    7,  163,  164,  145,
 /*  1010 */   187,  145,  231,  145,  230,  145,  163,  164,  163,  164,
 /*  1020 */   145,  163,  164,  163,  164,  145,  190,  163,  164,  163,
 /*  1030 */   164,  163,  164,  163,  164,  145,  200,  145,  163,  164,
 /*  1040 */   145,  145,  154,  145,  145,   49,  145,  145,   52,  145,
 /*  1050 */    53,  145,   53,  163,  164,  163,  164,  145,  145,  163,
 /*  1060 */   164,  163,  164,  145,  163,  164,  145,  163,  164,  163,
 /*  1070 */   164,  145,   76,  145,   51,  187,  163,  164,  145,  192,
 /*  1080 */    47,  163,  164,  184,  145,   52,   53,  145,   79,  163,
 /*  1090 */   164,  163,  164,  145,  157,  158,  163,  164,  145,   90,
 /*  1100 */   145,  154,  163,  164,  145,  163,  164,  145,   47,   51,
 /*  1110 */    47,  163,  164,   52,   53,   52,  163,  164,  163,  164,
 /*  1120 */    51,  209,  163,  164,  154,  163,  164,  104,  105,   16,
 /*  1130 */   209,  232,  157,  158,  187,   48,  202,  203,   51,   76,
 /*  1140 */   109,  110,   84,   96,   47,   47,   49,   48,  101,   48,
 /*  1150 */    51,   48,   51,   48,   51,   48,   51,  187,   51,   48,
 /*  1160 */    47,  116,   51,  118,   52,   53,   78,   79,   48,  145,
 /*  1170 */    48,   51,  103,   51,   48,   52,   53,   51,  116,   48,
 /*  1180 */   118,  145,   51,  145,   52,   53,  205,  205,  145,  145,
 /*  1190 */   192,  145,  231,  145,  145,  145,  145,  145,  145,  167,
 /*  1200 */   102,  145,  175,  166,  145,  171,  205,  219,  166,  145,
 /*  1210 */   145,  145,  145,  145,   47,  145,  191,  189,  145,  145,
 /*  1220 */   166,  145,  145,  148,  145,  145,  134,  233,  212,    5,
 /*  1230 */   114,  192,   45,  228,  227,  180,  122,  129,   45,  150,
 /*  1240 */   183,  212,   47,  183,  150,  150,  150,   85,   64,   84,
 /*  1250 */   107,  165,  168,  168,  183,  183,  165,   85,  165,   47,
 /*  1260 */   122,  180,  180,  173,   32,  113,  108,  165,  168,  112,
 /*  1270 */   192,  167,  111,   50,  165,  192,   51,   40,   35,  173,
 /*  1280 */   165,   36,  165,    4,    3,   56,   42,   73,   85,   43,
 /*  1290 */    48,  144,  146,  153,  152,  146,   48,  144,  144,  144,
 /*  1300 */   102,  168,  143,  115,  168,  100,  156,  156,   89,  103,
 /*  1310 */    46,   85,   51,  128,  128,   85,    1,  207,  103,  156,
 /*  1320 */   131,  130,  115,   16,  206,  169,  207,  206,   16,  169,
 /*  1330 */   193,  207,  207,  206,  206,  196,  195,  192,  194,   16,
 /*  1340 */    16,   89,   53,  102,  106,  216,    1,   34,  218,   47,
 /*  1350 */    85,   46,  125,   49,    7,  221,  221,   90,   83,   67,
 /*  1360 */    67,   47,   67,   47,   47,   55,   48,   47,   47,   61,
 /*  1370 */    48,  102,   96,   48,   48,   47,  106,   48,   51,   47,
 /*  1380 */    47,   51,   48,   48,   53,  105,   47,   51,  106,   51,
 /*  1390 */   106,   48,   48,   51,   48,   76,   48,   48,   38,   48,
 /*  1400 */    47,   49,  102,   48,   47,   51,   47,   49,    1,   48,
 /*  1410 */    48,   48,   47,   42,   48,    0,
};
#define YY_SHIFT_USE_DFLT (1416)
#define YY_SHIFT_COUNT    (418)
#define YY_SHIFT_MIN      (-83)
#define YY_SHIFT_MAX      (1415)
static const short yy_shift_ofst[] = {
 /*     0 */   617,  568,  602,  566,  738,  738,  738,  738,  242,   -5,
 /*    10 */    72,   72,  738,  738,  738,  738,  738,  738,  738,  690,
 /*    20 */   690,  791,  553,  192,  396,  100,  130,  179,  228,  277,
 /*    30 */   326,  375,  403,  431,  459,  459,  459,  459,  459,  459,
 /*    40 */   459,  459,  459,  459,  459,  459,  459,  459,  459,  527,
 /*    50 */   459,  554,  492,  492,  707,  738,  738,  738,  738,  738,
 /*    60 */   738,  738,  738,  738,  738,  738,  738,  738,  738,  738,
 /*    70 */   738,  738,  738,  738,  738,  738,  738,  738,  738,  738,
 /*    80 */   738,  738,  843,  738,  738,  738,  738,  738,  738,  738,
 /*    90 */   738,  738,  738,  738,  738,  738,   11,   31,   31,   31,
 /*   100 */    31,   31,  190,   38,   44,  813,  107,  107,   -7,  625,
 /*   110 */   534,   -7,  -16, 1416, 1416, 1416,  585,  585,  585,  308,
 /*   120 */   308,  819,  627,  627,  537,   -7,  654,   -7,   -7,   -7,
 /*   130 */    -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,
 /*   140 */    -7,   -7,   -7,  631,   -7,   -7,   -7,  631,  534,  -83,
 /*   150 */   -83,  -83,  -83,  -83,  -83, 1416, 1416,  720,  150,  150,
 /*   160 */   234,  867,  867,  867,  837,  320,  127,  696,  882,  406,
 /*   170 */   565,  735,  727,  996,  996,  996, 1033, 1023, 1061,  278,
 /*   180 */   847,   -7,   -7,   -7,   -7,   -7,   -7,  210,  997,  997,
 /*   190 */    -7,   -7,  999,  210,   -7,  999,   -7,   -7,   -7,   -7,
 /*   200 */    -7,   -7, 1058,   -7, 1087,   -7,  749,   -7, 1031,   -7,
 /*   210 */    -7,  997,   -7,  589, 1031, 1031,   -7,   -7,   -7, 1069,
 /*   220 */  1047,   -7,   -7, 1097,   -7,   -7,   -7,   -7, 1092, 1167,
 /*   230 */  1224, 1116, 1187, 1187, 1187, 1187, 1114, 1108, 1193, 1116,
 /*   240 */  1167, 1224, 1224, 1116, 1193, 1195, 1193, 1193, 1195, 1162,
 /*   250 */  1162, 1162, 1184, 1195, 1162, 1165, 1162, 1184, 1162, 1162,
 /*   260 */  1143, 1172, 1143, 1172, 1143, 1172, 1143, 1172, 1212, 1138,
 /*   270 */  1195, 1232, 1232, 1195, 1152, 1158, 1157, 1161, 1116, 1223,
 /*   280 */  1225, 1237, 1237, 1243, 1243, 1243, 1243, 1245, 1416, 1416,
 /*   290 */  1416, 1416, 1416,  518,  841,  893, 1063, 1113, 1099, 1101,
 /*   300 */  1103, 1105, 1107, 1111, 1112, 1088,  817, 1009, 1120, 1122,
 /*   310 */  1123, 1126, 1045, 1062, 1131, 1132, 1098, 1279, 1281, 1229,
 /*   320 */  1244, 1214, 1246, 1203, 1242, 1248, 1198, 1205, 1188, 1219,
 /*   330 */  1206, 1226, 1264, 1185, 1261, 1186, 1189, 1191, 1230, 1315,
 /*   340 */  1215, 1207, 1307, 1312, 1323, 1324, 1252, 1289, 1238, 1241,
 /*   350 */  1345, 1313, 1302, 1265, 1227, 1304, 1305, 1347, 1267, 1275,
 /*   360 */  1314, 1292, 1316, 1317, 1318, 1320, 1293, 1310, 1321, 1295,
 /*   370 */  1308, 1322, 1325, 1326, 1327, 1276, 1328, 1329, 1332, 1330,
 /*   380 */  1269, 1334, 1335, 1331, 1270, 1333, 1280, 1336, 1282, 1338,
 /*   390 */  1284, 1343, 1336, 1344, 1346, 1348, 1319, 1342, 1349, 1339,
 /*   400 */  1360, 1351, 1353, 1352, 1354, 1355, 1357, 1358, 1354, 1361,
 /*   410 */  1359, 1362, 1363, 1365, 1300, 1366, 1371, 1407, 1415,
};
#define YY_REDUCE_USE_DFLT (-71)
#define YY_REDUCE_COUNT (292)
#define YY_REDUCE_MIN   (-70)
#define YY_REDUCE_MAX   (1163)
static const short yy_reduce_ofst[] = {
 /*     0 */    26,  506,  676,  123,  563,  606,  668,  688,  471,  -38,
 /*    10 */   105,  108,  468,  567,  652,  448,  463,  701,  546,  596,
 /*    20 */   732,  752,  539,  694,  728,  -44,  -44,  -44,  -44,  -44,
 /*    30 */   -44,  -44,  -44,  -44,  -44,  -44,  -44,  -44,  -44,  -44,
 /*    40 */   -44,  -44,  -44,  -44,  -44,  -44,  -44,  -44,  -44,  -44,
 /*    50 */   -44,  -44,  -44,  -44,  629,  746,  761,  787,  794,  807,
 /*    60 */   822,  824,  827,  829,  831,  833,  838,  844,  853,  855,
 /*    70 */   858,  860,  864,  866,  868,  870,  875,  890,  892,  896,
 /*    80 */   898,  901,  904,  906,  913,  918,  926,  928,  933,  939,
 /*    90 */   942,  948,  953,  955,  959,  962,  -44,  -44,  -44,  -44,
 /*   100 */   -44,  -44,  -44,  -44,  -44,  189,   13,  204,  899,  -44,
 /*   110 */   402,  118,  -44,  -44,  -44,  -44,  532,  532,  532,  515,
 /*   120 */   547,  575,  505,  593,   83,  664,  648,  594,  665,  715,
 /*   130 */   734,  756,  769,  771,  104,  789,  136,  702,  368,  775,
 /*   140 */   912,  784,   22,  937,  921,  607,  716,  975,  405,  750,
 /*   150 */   795,  823,  888,  947,  970,  934,  836,  -70,  -42,   20,
 /*   160 */    99,  126,  207,  252,  157,  251,  289,  317,  366,  370,
 /*   170 */   411,  498,  545,  476,  644,  679,  757,  157,  820,  811,
 /*   180 */   786,  880,  895,  902, 1024, 1036, 1038,  887,  981,  982,
 /*   190 */  1043, 1044,  781,  998, 1046,  961, 1048, 1049, 1050, 1051,
 /*   200 */  1052, 1053, 1032, 1056, 1027, 1059, 1034, 1064, 1037, 1065,
 /*   210 */  1066, 1001, 1067,  988, 1042, 1054, 1068, 1070,  545, 1025,
 /*   220 */  1028, 1073, 1074, 1075, 1076, 1077, 1079, 1080,  994, 1016,
 /*   230 */  1055, 1039, 1057, 1060, 1071, 1072, 1005, 1007, 1089, 1078,
 /*   240 */  1029, 1081, 1082, 1083, 1094, 1084, 1095, 1096, 1085, 1086,
 /*   250 */  1091, 1093, 1090, 1100, 1102, 1104, 1109, 1106, 1115, 1117,
 /*   260 */  1110, 1118, 1119, 1121, 1124, 1127, 1125, 1128, 1129, 1130,
 /*   270 */  1133, 1134, 1135, 1136, 1139, 1141, 1144, 1137, 1145, 1140,
 /*   280 */  1142, 1146, 1149, 1147, 1153, 1154, 1155, 1159, 1150, 1151,
 /*   290 */  1156, 1160, 1163,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1183, 1177, 1177, 1177, 1120, 1120, 1120, 1120, 1177, 1016,
 /*    10 */  1043, 1043, 1227, 1227, 1227, 1227, 1227, 1227, 1119, 1227,
 /*    20 */  1227, 1227, 1227, 1177, 1020, 1049, 1227, 1227, 1227, 1121,
 /*    30 */  1122, 1227, 1227, 1227, 1153, 1059, 1058, 1057, 1056, 1030,
 /*    40 */  1054, 1047, 1051, 1121, 1115, 1116, 1114, 1118, 1122, 1227,
 /*    50 */  1050, 1084, 1099, 1083, 1227, 1227, 1227, 1227, 1227, 1227,
 /*    60 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*    70 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*    80 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*    90 */  1227, 1227, 1227, 1227, 1227, 1227, 1093, 1098, 1105, 1097,
 /*   100 */  1094, 1086, 1085, 1087, 1088,  987, 1227, 1227, 1227, 1089,
 /*   110 */  1227, 1227, 1090, 1102, 1101, 1100, 1175, 1192, 1191, 1227,
 /*   120 */  1227, 1127, 1227, 1227, 1227, 1227, 1177, 1227, 1227, 1227,
 /*   130 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*   140 */  1227, 1227, 1227,  945, 1227, 1227, 1227,  945, 1227, 1177,
 /*   150 */  1177, 1177, 1177, 1177, 1177, 1020, 1011, 1227, 1227, 1227,
 /*   160 */  1227, 1227, 1227, 1227, 1227, 1016, 1227, 1227, 1227, 1227,
 /*   170 */  1148, 1227, 1227, 1016, 1016, 1016, 1227, 1018, 1227, 1000,
 /*   180 */  1010, 1227, 1172, 1227, 1169, 1227, 1227, 1053, 1032, 1032,
 /*   190 */  1227, 1227, 1224, 1053, 1227, 1224, 1227, 1227, 1227, 1227,
 /*   200 */  1227, 1227,  962, 1227, 1203, 1227,  959, 1227, 1043, 1227,
 /*   210 */  1227, 1032, 1227, 1117, 1043, 1043, 1227, 1227, 1227, 1017,
 /*   220 */  1010, 1227, 1227, 1227, 1227, 1227, 1227, 1186, 1225, 1064,
 /*   230 */   990, 1053,  996,  996,  996,  996, 1152, 1221,  937, 1053,
 /*   240 */  1064,  990,  990, 1053,  937, 1128,  937,  937, 1128,  988,
 /*   250 */   988,  988,  977, 1128,  988,  962,  988,  977,  988,  988,
 /*   260 */  1036, 1031, 1036, 1031, 1036, 1031, 1036, 1031, 1123, 1227,
 /*   270 */  1128, 1132, 1132, 1128, 1048, 1037, 1046, 1044, 1053,  941,
 /*   280 */   980, 1189, 1189, 1185, 1185, 1185, 1185,  927, 1198, 1198,
 /*   290 */   964,  964, 1198, 1227, 1227, 1227, 1193, 1135, 1227, 1227,
 /*   300 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*   310 */  1227, 1227, 1227, 1227, 1227, 1227, 1070, 1227,  924, 1227,
 /*   320 */  1227, 1176, 1227, 1170, 1227, 1227, 1216, 1227, 1227, 1227,
 /*   330 */  1227, 1227, 1227, 1227, 1151, 1150, 1227, 1227, 1227, 1227,
 /*   340 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1223,
 /*   350 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*   360 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*   370 */  1227, 1227, 1227, 1227, 1227, 1002, 1227, 1227, 1227, 1207,
 /*   380 */  1227, 1227, 1227, 1227, 1227, 1227, 1227, 1045, 1227, 1038,
 /*   390 */  1227, 1227, 1213, 1227, 1227, 1227, 1227, 1227, 1227, 1227,
 /*   400 */  1227, 1227, 1227, 1227, 1179, 1227, 1227, 1227, 1178, 1227,
 /*   410 */  1227, 1227, 1227, 1227, 1227, 1227,  931, 1227, 1227,
};
/********** End of lemon-generated parsing tables *****************************/

/* The next table maps tokens (terminal symbols) into fallback tokens.  
** If a construct like the following:
** 
**      %fallback ID X Y Z.
**
** appears in the grammar, then ID becomes a fallback token for X, Y,
** and Z.  Whenever one of the tokens X, Y, or Z is input to the parser
** but it does not parse, the type of the token is changed to ID and
** the parse is retried before an error is thrown.
**
** This feature can be used, for example, to cause some keywords in a language
** to revert to identifiers if they keyword does not apply in the context where
** it appears.
*/
#ifdef YYFALLBACK
static const YYCODETYPE yyFallback[] = {
    0,  /*          $ => nothing */
    0,  /*       SEMI => nothing */
    0,  /*    EXPLAIN => nothing */
   52,  /*      QUERY => ID */
   52,  /*       PLAN => ID */
    0,  /*         OR => nothing */
    0,  /*        AND => nothing */
    0,  /*        NOT => nothing */
    0,  /*         IS => nothing */
   52,  /*      MATCH => ID */
    0,  /*    LIKE_KW => nothing */
    0,  /*    BETWEEN => nothing */
    0,  /*         IN => nothing */
   52,  /*     ISNULL => ID */
   52,  /*    NOTNULL => ID */
    0,  /*         NE => nothing */
    0,  /*         EQ => nothing */
    0,  /*         GT => nothing */
    0,  /*         LE => nothing */
    0,  /*         LT => nothing */
    0,  /*         GE => nothing */
    0,  /*     ESCAPE => nothing */
    0,  /*     BITAND => nothing */
    0,  /*      BITOR => nothing */
    0,  /*     LSHIFT => nothing */
    0,  /*     RSHIFT => nothing */
    0,  /*       PLUS => nothing */
    0,  /*      MINUS => nothing */
    0,  /*       STAR => nothing */
    0,  /*      SLASH => nothing */
    0,  /*        REM => nothing */
    0,  /*     CONCAT => nothing */
    0,  /*    COLLATE => nothing */
    0,  /*     BITNOT => nothing */
    0,  /*      BEGIN => nothing */
    0,  /* TRANSACTION => nothing */
   52,  /*   DEFERRED => ID */
    0,  /*     COMMIT => nothing */
   52,  /*        END => ID */
    0,  /*   ROLLBACK => nothing */
    0,  /*  SAVEPOINT => nothing */
   52,  /*    RELEASE => ID */
    0,  /*         TO => nothing */
    0,  /*      TABLE => nothing */
    0,  /*     CREATE => nothing */
   52,  /*         IF => ID */
    0,  /*     EXISTS => nothing */
    0,  /*         LP => nothing */
    0,  /*         RP => nothing */
    0,  /*         AS => nothing */
    0,  /*    WITHOUT => nothing */
    0,  /*      COMMA => nothing */
    0,  /*         ID => nothing */
   52,  /*    INDEXED => ID */
   52,  /*      ABORT => ID */
   52,  /*     ACTION => ID */
   52,  /*        ADD => ID */
   52,  /*      AFTER => ID */
   52,  /* AUTOINCREMENT => ID */
   52,  /*     BEFORE => ID */
   52,  /*    CASCADE => ID */
   52,  /*   CONFLICT => ID */
   52,  /*       FAIL => ID */
   52,  /*     IGNORE => ID */
   52,  /*  INITIALLY => ID */
   52,  /*    INSTEAD => ID */
   52,  /*         NO => ID */
   52,  /*        KEY => ID */
   52,  /*     OFFSET => ID */
   52,  /*      RAISE => ID */
   52,  /*    REPLACE => ID */
   52,  /*   RESTRICT => ID */
   52,  /*    REINDEX => ID */
   52,  /*     RENAME => ID */
   52,  /*   CTIME_KW => ID */
};
#endif /* YYFALLBACK */

/* The following structure represents a single element of the
** parser's stack.  Information stored includes:
**
**   +  The state number for the parser at this level of the stack.
**
**   +  The value of the token stored at this level of the stack.
**      (In other words, the "major" token.)
**
**   +  The semantic value stored at this level of the stack.  This is
**      the information used by the action routines in the grammar.
**      It is sometimes called the "minor" token.
**
** After the "shift" half of a SHIFTREDUCE action, the stateno field
** actually contains the reduce action for the second half of the
** SHIFTREDUCE.
*/
struct yyStackEntry {
  YYACTIONTYPE stateno;  /* The state-number, or reduce action in SHIFTREDUCE */
  YYCODETYPE major;      /* The major token value.  This is the code
                         ** number for the token at this stack level */
  YYMINORTYPE minor;     /* The user-supplied minor token value.  This
                         ** is the value of the token  */
};
typedef struct yyStackEntry yyStackEntry;

/* The state of the parser is completely contained in an instance of
** the following structure */
struct yyParser {
  yyStackEntry *yytos;          /* Pointer to top element of the stack */
#ifdef YYTRACKMAXSTACKDEPTH
  int yyhwm;                    /* High-water mark of the stack */
#endif
#ifndef YYNOERRORRECOVERY
  int yyerrcnt;                 /* Shifts left before out of the error */
#endif
  bool is_fallback_failed;      /* Shows if fallback failed or not */
  sqlite3ParserARG_SDECL                /* A place to hold %extra_argument */
#if YYSTACKDEPTH<=0
  int yystksz;                  /* Current side of the stack */
  yyStackEntry *yystack;        /* The parser's stack */
  yyStackEntry yystk0;          /* First stack entry */
#else
  yyStackEntry yystack[YYSTACKDEPTH];  /* The parser's stack */
#endif
};
typedef struct yyParser yyParser;

#ifndef NDEBUG
#include <stdio.h>
static FILE *yyTraceFILE = 0;
static char *yyTracePrompt = 0;
#endif /* NDEBUG */

#ifndef NDEBUG
/* 
** Turn parser tracing on by giving a stream to which to write the trace
** and a prompt to preface each trace message.  Tracing is turned off
** by making either argument NULL 
**
** Inputs:
** <ul>
** <li> A FILE* to which trace output should be written.
**      If NULL, then tracing is turned off.
** <li> A prefix string written at the beginning of every
**      line of trace output.  If NULL, then tracing is
**      turned off.
** </ul>
**
** Outputs:
** None.
*/
void sqlite3ParserTrace(FILE *TraceFILE, char *zTracePrompt){
  yyTraceFILE = TraceFILE;
  yyTracePrompt = zTracePrompt;
  if( yyTraceFILE==0 ) yyTracePrompt = 0;
  else if( yyTracePrompt==0 ) yyTraceFILE = 0;
}
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing shifts, the names of all terminals and nonterminals
** are required.  The following table supplies these names */
static const char *const yyTokenName[] = { 
  "$",             "SEMI",          "EXPLAIN",       "QUERY",       
  "PLAN",          "OR",            "AND",           "NOT",         
  "IS",            "MATCH",         "LIKE_KW",       "BETWEEN",     
  "IN",            "ISNULL",        "NOTNULL",       "NE",          
  "EQ",            "GT",            "LE",            "LT",          
  "GE",            "ESCAPE",        "BITAND",        "BITOR",       
  "LSHIFT",        "RSHIFT",        "PLUS",          "MINUS",       
  "STAR",          "SLASH",         "REM",           "CONCAT",      
  "COLLATE",       "BITNOT",        "BEGIN",         "TRANSACTION", 
  "DEFERRED",      "COMMIT",        "END",           "ROLLBACK",    
  "SAVEPOINT",     "RELEASE",       "TO",            "TABLE",       
  "CREATE",        "IF",            "EXISTS",        "LP",          
  "RP",            "AS",            "WITHOUT",       "COMMA",       
  "ID",            "INDEXED",       "ABORT",         "ACTION",      
  "ADD",           "AFTER",         "AUTOINCREMENT",  "BEFORE",      
  "CASCADE",       "CONFLICT",      "FAIL",          "IGNORE",      
  "INITIALLY",     "INSTEAD",       "NO",            "KEY",         
  "OFFSET",        "RAISE",         "REPLACE",       "RESTRICT",    
  "REINDEX",       "RENAME",        "CTIME_KW",      "ANY",         
  "STRING",        "CONSTRAINT",    "DEFAULT",       "NULL",        
  "PRIMARY",       "UNIQUE",        "CHECK",         "REFERENCES",  
  "AUTOINCR",      "ON",            "INSERT",        "DELETE",      
  "UPDATE",        "SET",           "DEFERRABLE",    "IMMEDIATE",   
  "FOREIGN",       "DROP",          "VIEW",          "UNION",       
  "ALL",           "EXCEPT",        "INTERSECT",     "SELECT",      
  "VALUES",        "DISTINCT",      "DOT",           "FROM",        
  "JOIN_KW",       "JOIN",          "BY",            "USING",       
  "ORDER",         "ASC",           "DESC",          "GROUP",       
  "HAVING",        "LIMIT",         "WHERE",         "INTO",        
  "FLOAT",         "BLOB",          "INTEGER",       "VARIABLE",    
  "CAST",          "CASE",          "WHEN",          "THEN",        
  "ELSE",          "INDEX",         "PRAGMA",        "TRIGGER",     
  "OF",            "FOR",           "EACH",          "ROW",         
  "ANALYZE",       "ALTER",         "COLUMNKW",      "WITH",        
  "RECURSIVE",     "error",         "input",         "ecmd",        
  "explain",       "cmdx",          "cmd",           "transtype",   
  "trans_opt",     "nm",            "savepoint_opt",  "create_table",
  "create_table_args",  "createkw",      "ifnotexists",   "columnlist",  
  "conslist_opt",  "table_options",  "select",        "columnname",  
  "carglist",      "typetoken",     "typename",      "signed",      
  "plus_num",      "minus_num",     "ccons",         "term",        
  "expr",          "onconf",        "sortorder",     "autoinc",     
  "eidlist_opt",   "refargs",       "defer_subclause",  "refarg",      
  "refact",        "init_deferred_pred_opt",  "conslist",      "tconscomma",  
  "tcons",         "sortlist",      "eidlist",       "defer_subclause_opt",
  "orconf",        "resolvetype",   "raisetype",     "ifexists",    
  "fullname",      "selectnowith",  "oneselect",     "with",        
  "multiselect_op",  "distinct",      "selcollist",    "from",        
  "where_opt",     "groupby_opt",   "having_opt",    "orderby_opt", 
  "limit_opt",     "values",        "nexprlist",     "exprlist",    
  "sclp",          "as",            "seltablist",    "stl_prefix",  
  "joinop",        "indexed_opt",   "on_opt",        "using_opt",   
  "join_nm",       "idlist",        "setlist",       "insert_cmd",  
  "idlist_opt",    "likeop",        "between_op",    "in_op",       
  "paren_exprlist",  "case_operand",  "case_exprlist",  "case_else",   
  "uniqueflag",    "collate",       "nmnum",         "trigger_decl",
  "trigger_cmd_list",  "trigger_time",  "trigger_event",  "foreach_clause",
  "when_clause",   "trigger_cmd",   "trnm",          "tridxby",     
  "add_column_fullname",  "kwcolumn_opt",  "wqlist",      
};
#endif /* NDEBUG */

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
 /*  17 */ "create_table_args ::= LP columnlist conslist_opt RP table_options",
 /*  18 */ "create_table_args ::= AS select",
 /*  19 */ "table_options ::=",
 /*  20 */ "table_options ::= WITHOUT nm",
 /*  21 */ "columnname ::= nm typetoken",
 /*  22 */ "nm ::= ID|INDEXED",
 /*  23 */ "typetoken ::=",
 /*  24 */ "typetoken ::= typename LP signed RP",
 /*  25 */ "typetoken ::= typename LP signed COMMA signed RP",
 /*  26 */ "typename ::= typename ID|STRING",
 /*  27 */ "ccons ::= CONSTRAINT nm",
 /*  28 */ "ccons ::= DEFAULT term",
 /*  29 */ "ccons ::= DEFAULT LP expr RP",
 /*  30 */ "ccons ::= DEFAULT PLUS term",
 /*  31 */ "ccons ::= DEFAULT MINUS term",
 /*  32 */ "ccons ::= DEFAULT ID|INDEXED",
 /*  33 */ "ccons ::= NOT NULL onconf",
 /*  34 */ "ccons ::= PRIMARY KEY sortorder onconf autoinc",
 /*  35 */ "ccons ::= UNIQUE onconf",
 /*  36 */ "ccons ::= CHECK LP expr RP",
 /*  37 */ "ccons ::= REFERENCES nm eidlist_opt refargs",
 /*  38 */ "ccons ::= defer_subclause",
 /*  39 */ "ccons ::= COLLATE ID|INDEXED",
 /*  40 */ "autoinc ::=",
 /*  41 */ "autoinc ::= AUTOINCR",
 /*  42 */ "refargs ::=",
 /*  43 */ "refargs ::= refargs refarg",
 /*  44 */ "refarg ::= MATCH nm",
 /*  45 */ "refarg ::= ON INSERT refact",
 /*  46 */ "refarg ::= ON DELETE refact",
 /*  47 */ "refarg ::= ON UPDATE refact",
 /*  48 */ "refact ::= SET NULL",
 /*  49 */ "refact ::= SET DEFAULT",
 /*  50 */ "refact ::= CASCADE",
 /*  51 */ "refact ::= RESTRICT",
 /*  52 */ "refact ::= NO ACTION",
 /*  53 */ "defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt",
 /*  54 */ "defer_subclause ::= DEFERRABLE init_deferred_pred_opt",
 /*  55 */ "init_deferred_pred_opt ::=",
 /*  56 */ "init_deferred_pred_opt ::= INITIALLY DEFERRED",
 /*  57 */ "init_deferred_pred_opt ::= INITIALLY IMMEDIATE",
 /*  58 */ "conslist_opt ::=",
 /*  59 */ "tconscomma ::= COMMA",
 /*  60 */ "tcons ::= CONSTRAINT nm",
 /*  61 */ "tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf",
 /*  62 */ "tcons ::= UNIQUE LP sortlist RP onconf",
 /*  63 */ "tcons ::= CHECK LP expr RP onconf",
 /*  64 */ "tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt",
 /*  65 */ "defer_subclause_opt ::=",
 /*  66 */ "onconf ::=",
 /*  67 */ "onconf ::= ON CONFLICT resolvetype",
 /*  68 */ "orconf ::=",
 /*  69 */ "orconf ::= OR resolvetype",
 /*  70 */ "resolvetype ::= IGNORE",
 /*  71 */ "resolvetype ::= REPLACE",
 /*  72 */ "cmd ::= DROP TABLE ifexists fullname",
 /*  73 */ "ifexists ::= IF EXISTS",
 /*  74 */ "ifexists ::=",
 /*  75 */ "cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select",
 /*  76 */ "cmd ::= DROP VIEW ifexists fullname",
 /*  77 */ "cmd ::= select",
 /*  78 */ "select ::= with selectnowith",
 /*  79 */ "selectnowith ::= selectnowith multiselect_op oneselect",
 /*  80 */ "multiselect_op ::= UNION",
 /*  81 */ "multiselect_op ::= UNION ALL",
 /*  82 */ "multiselect_op ::= EXCEPT|INTERSECT",
 /*  83 */ "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
 /*  84 */ "values ::= VALUES LP nexprlist RP",
 /*  85 */ "values ::= values COMMA LP exprlist RP",
 /*  86 */ "distinct ::= DISTINCT",
 /*  87 */ "distinct ::= ALL",
 /*  88 */ "distinct ::=",
 /*  89 */ "sclp ::=",
 /*  90 */ "selcollist ::= sclp expr as",
 /*  91 */ "selcollist ::= sclp STAR",
 /*  92 */ "selcollist ::= sclp nm DOT STAR",
 /*  93 */ "as ::= AS nm",
 /*  94 */ "as ::=",
 /*  95 */ "from ::=",
 /*  96 */ "from ::= FROM seltablist",
 /*  97 */ "stl_prefix ::= seltablist joinop",
 /*  98 */ "stl_prefix ::=",
 /*  99 */ "seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt",
 /* 100 */ "seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt",
 /* 101 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
 /* 102 */ "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
 /* 103 */ "fullname ::= nm",
 /* 104 */ "joinop ::= COMMA|JOIN",
 /* 105 */ "joinop ::= JOIN_KW JOIN",
 /* 106 */ "joinop ::= JOIN_KW join_nm JOIN",
 /* 107 */ "joinop ::= JOIN_KW join_nm join_nm JOIN",
 /* 108 */ "on_opt ::= ON expr",
 /* 109 */ "on_opt ::=",
 /* 110 */ "indexed_opt ::=",
 /* 111 */ "indexed_opt ::= INDEXED BY nm",
 /* 112 */ "indexed_opt ::= NOT INDEXED",
 /* 113 */ "using_opt ::= USING LP idlist RP",
 /* 114 */ "using_opt ::=",
 /* 115 */ "orderby_opt ::=",
 /* 116 */ "orderby_opt ::= ORDER BY sortlist",
 /* 117 */ "sortlist ::= sortlist COMMA expr sortorder",
 /* 118 */ "sortlist ::= expr sortorder",
 /* 119 */ "sortorder ::= ASC",
 /* 120 */ "sortorder ::= DESC",
 /* 121 */ "sortorder ::=",
 /* 122 */ "groupby_opt ::=",
 /* 123 */ "groupby_opt ::= GROUP BY nexprlist",
 /* 124 */ "having_opt ::=",
 /* 125 */ "having_opt ::= HAVING expr",
 /* 126 */ "limit_opt ::=",
 /* 127 */ "limit_opt ::= LIMIT expr",
 /* 128 */ "limit_opt ::= LIMIT expr OFFSET expr",
 /* 129 */ "limit_opt ::= LIMIT expr COMMA expr",
 /* 130 */ "cmd ::= with DELETE FROM fullname indexed_opt where_opt",
 /* 131 */ "where_opt ::=",
 /* 132 */ "where_opt ::= WHERE expr",
 /* 133 */ "cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt",
 /* 134 */ "setlist ::= setlist COMMA nm EQ expr",
 /* 135 */ "setlist ::= setlist COMMA LP idlist RP EQ expr",
 /* 136 */ "setlist ::= nm EQ expr",
 /* 137 */ "setlist ::= LP idlist RP EQ expr",
 /* 138 */ "cmd ::= with insert_cmd INTO fullname idlist_opt select",
 /* 139 */ "cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES",
 /* 140 */ "insert_cmd ::= INSERT orconf",
 /* 141 */ "insert_cmd ::= REPLACE",
 /* 142 */ "idlist_opt ::=",
 /* 143 */ "idlist_opt ::= LP idlist RP",
 /* 144 */ "idlist ::= idlist COMMA nm",
 /* 145 */ "idlist ::= nm",
 /* 146 */ "expr ::= LP expr RP",
 /* 147 */ "term ::= NULL",
 /* 148 */ "expr ::= ID|INDEXED",
 /* 149 */ "expr ::= JOIN_KW",
 /* 150 */ "expr ::= nm DOT nm",
 /* 151 */ "term ::= FLOAT|BLOB",
 /* 152 */ "term ::= STRING",
 /* 153 */ "term ::= INTEGER",
 /* 154 */ "expr ::= VARIABLE",
 /* 155 */ "expr ::= expr COLLATE ID|INDEXED",
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
 /* 203 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt",
 /* 204 */ "uniqueflag ::= UNIQUE",
 /* 205 */ "uniqueflag ::=",
 /* 206 */ "eidlist_opt ::=",
 /* 207 */ "eidlist_opt ::= LP eidlist RP",
 /* 208 */ "eidlist ::= eidlist COMMA nm collate sortorder",
 /* 209 */ "eidlist ::= nm collate sortorder",
 /* 210 */ "collate ::=",
 /* 211 */ "collate ::= COLLATE ID|INDEXED",
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
 /* 222 */ "trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause",
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
 /* 237 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 238 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
 /* 239 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 240 */ "trigger_cmd ::= select",
 /* 241 */ "expr ::= RAISE LP IGNORE RP",
 /* 242 */ "expr ::= RAISE LP raisetype COMMA STRING RP",
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
 /* 253 */ "cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist",
 /* 254 */ "add_column_fullname ::= fullname",
 /* 255 */ "with ::=",
 /* 256 */ "with ::= WITH wqlist",
 /* 257 */ "with ::= WITH RECURSIVE wqlist",
 /* 258 */ "wqlist ::= nm eidlist_opt AS LP select RP",
 /* 259 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
 /* 260 */ "input ::= ecmd",
 /* 261 */ "explain ::=",
 /* 262 */ "cmdx ::= cmd",
 /* 263 */ "trans_opt ::=",
 /* 264 */ "trans_opt ::= TRANSACTION",
 /* 265 */ "trans_opt ::= TRANSACTION nm",
 /* 266 */ "savepoint_opt ::= SAVEPOINT",
 /* 267 */ "savepoint_opt ::=",
 /* 268 */ "cmd ::= create_table create_table_args",
 /* 269 */ "columnlist ::= columnlist COMMA columnname carglist",
 /* 270 */ "columnlist ::= columnname carglist",
 /* 271 */ "typetoken ::= typename",
 /* 272 */ "typename ::= ID|STRING",
 /* 273 */ "signed ::= plus_num",
 /* 274 */ "signed ::= minus_num",
 /* 275 */ "carglist ::= carglist ccons",
 /* 276 */ "carglist ::=",
 /* 277 */ "ccons ::= NULL onconf",
 /* 278 */ "conslist_opt ::= COMMA conslist",
 /* 279 */ "conslist ::= conslist tconscomma tcons",
 /* 280 */ "conslist ::= tcons",
 /* 281 */ "tconscomma ::=",
 /* 282 */ "defer_subclause_opt ::= defer_subclause",
 /* 283 */ "resolvetype ::= raisetype",
 /* 284 */ "selectnowith ::= oneselect",
 /* 285 */ "oneselect ::= values",
 /* 286 */ "sclp ::= selcollist COMMA",
 /* 287 */ "as ::= ID|STRING",
 /* 288 */ "join_nm ::= ID|INDEXED",
 /* 289 */ "join_nm ::= JOIN_KW",
 /* 290 */ "expr ::= term",
 /* 291 */ "exprlist ::= nexprlist",
 /* 292 */ "nmnum ::= plus_num",
 /* 293 */ "nmnum ::= STRING",
 /* 294 */ "nmnum ::= nm",
 /* 295 */ "nmnum ::= ON",
 /* 296 */ "nmnum ::= DELETE",
 /* 297 */ "nmnum ::= DEFAULT",
 /* 298 */ "plus_num ::= INTEGER|FLOAT",
 /* 299 */ "foreach_clause ::=",
 /* 300 */ "foreach_clause ::= FOR EACH ROW",
 /* 301 */ "trnm ::= nm",
 /* 302 */ "tridxby ::=",
 /* 303 */ "kwcolumn_opt ::=",
 /* 304 */ "kwcolumn_opt ::= COLUMNKW",
};
#endif /* NDEBUG */


#if YYSTACKDEPTH<=0
/*
** Try to increase the size of the parser stack.  Return the number
** of errors.  Return 0 on success.
*/
static int yyGrowStack(yyParser *p){
  int newSize;
  int idx;
  yyStackEntry *pNew;

  newSize = p->yystksz*2 + 100;
  idx = p->yytos ? (int)(p->yytos - p->yystack) : 0;
  if( p->yystack==&p->yystk0 ){
    pNew = malloc(newSize*sizeof(pNew[0]));
    if( pNew ) pNew[0] = p->yystk0;
  }else{
    pNew = realloc(p->yystack, newSize*sizeof(pNew[0]));
  }
  if( pNew ){
    p->yystack = pNew;
    p->yytos = &p->yystack[idx];
#ifndef NDEBUG
    if( yyTraceFILE ){
      fprintf(yyTraceFILE,"%sStack grows from %d to %d entries.\n",
              yyTracePrompt, p->yystksz, newSize);
    }
#endif
    p->yystksz = newSize;
  }
  return pNew==0; 
}
#endif

/* Datatype of the argument to the memory allocated passed as the
** second argument to sqlite3ParserAlloc() below.  This can be changed by
** putting an appropriate #define in the %include section of the input
** grammar.
*/
#ifndef YYMALLOCARGTYPE
# define YYMALLOCARGTYPE size_t
#endif

/* 
** This function allocates a new parser.
** The only argument is a pointer to a function which works like
** malloc.
**
** Inputs:
** A pointer to the function used to allocate memory.
**
** Outputs:
** A pointer to a parser.  This pointer is used in subsequent calls
** to sqlite3Parser and sqlite3ParserFree.
*/
void *sqlite3ParserAlloc(void *(*mallocProc)(YYMALLOCARGTYPE)){
  yyParser *pParser;
  pParser = (yyParser*)(*mallocProc)( (YYMALLOCARGTYPE)sizeof(yyParser) );
  if( pParser ){
#ifdef YYTRACKMAXSTACKDEPTH
    pParser->yyhwm = 0;
    pParser->is_fallback_failed = false;
#endif
#if YYSTACKDEPTH<=0
    pParser->yytos = NULL;
    pParser->yystack = NULL;
    pParser->yystksz = 0;
    if( yyGrowStack(pParser) ){
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
** associated with a symbol.  The symbol can be either a terminal
** or nonterminal. "yymajor" is the symbol code, and "yypminor" is
** a pointer to the value to be deleted.  The code used to do the 
** deletions is derived from the %destructor and/or %token_destructor
** directives of the input grammar.
*/
static void yy_destructor(
  yyParser *yypParser,    /* The parser */
  YYCODETYPE yymajor,     /* Type code for object to destroy */
  YYMINORTYPE *yypminor   /* The object to be destroyed */
){
  sqlite3ParserARG_FETCH;
  switch( yymajor ){
    /* Here is inserted the actions which take place when a
    ** terminal or non-terminal is destroyed.  This can happen
    ** when the symbol is popped from the stack during a
    ** reduce or during error processing or when a parser is 
    ** being destroyed before it is finished parsing.
    **
    ** Note: during a reduce, the only symbols destroyed are those
    ** which appear on the RHS of the rule, but which are *not* used
    ** inside the C code.
    */
/********* Begin destructor definitions ***************************************/
    case 154: /* select */
    case 185: /* selectnowith */
    case 186: /* oneselect */
    case 197: /* values */
{
#line 396 "parse.y"
sqlite3SelectDelete(pParse->db, (yypminor->yy329));
#line 1484 "parse.c"
}
      break;
    case 163: /* term */
    case 164: /* expr */
{
#line 839 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy412).pExpr);
#line 1492 "parse.c"
}
      break;
    case 168: /* eidlist_opt */
    case 177: /* sortlist */
    case 178: /* eidlist */
    case 190: /* selcollist */
    case 193: /* groupby_opt */
    case 195: /* orderby_opt */
    case 198: /* nexprlist */
    case 199: /* exprlist */
    case 200: /* sclp */
    case 210: /* setlist */
    case 216: /* paren_exprlist */
    case 218: /* case_exprlist */
{
#line 1271 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy93));
#line 1510 "parse.c"
}
      break;
    case 184: /* fullname */
    case 191: /* from */
    case 202: /* seltablist */
    case 203: /* stl_prefix */
{
#line 623 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy167));
#line 1520 "parse.c"
}
      break;
    case 187: /* with */
    case 234: /* wqlist */
{
#line 1516 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy121));
#line 1528 "parse.c"
}
      break;
    case 192: /* where_opt */
    case 194: /* having_opt */
    case 206: /* on_opt */
    case 217: /* case_operand */
    case 219: /* case_else */
    case 228: /* when_clause */
{
#line 748 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy112));
#line 1540 "parse.c"
}
      break;
    case 207: /* using_opt */
    case 209: /* idlist */
    case 212: /* idlist_opt */
{
#line 660 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy330));
#line 1549 "parse.c"
}
      break;
    case 224: /* trigger_cmd_list */
    case 229: /* trigger_cmd */
{
#line 1391 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy177));
#line 1557 "parse.c"
}
      break;
    case 226: /* trigger_event */
{
#line 1377 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy270).b);
#line 1564 "parse.c"
}
      break;
/********* End destructor definitions *****************************************/
    default:  break;   /* If no destructor action specified: do nothing */
  }
}

/*
** Pop the parser's stack once.
**
** If there is a destructor routine associated with the token which
** is popped from the stack, then call it.
*/
static void yy_pop_parser_stack(yyParser *pParser){
  yyStackEntry *yytos;
  assert( pParser->yytos!=0 );
  assert( pParser->yytos > pParser->yystack );
  yytos = pParser->yytos--;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sPopping %s\n",
      yyTracePrompt,
      yyTokenName[yytos->major]);
  }
#endif
  yy_destructor(pParser, yytos->major, &yytos->minor);
}

/* 
** Deallocate and destroy a parser.  Destructors are called for
** all stack elements before shutting the parser down.
**
** If the YYPARSEFREENEVERNULL macro exists (for example because it
** is defined in a %include section of the input grammar) then it is
** assumed that the input pointer is never NULL.
*/
void sqlite3ParserFree(
  void *p,                    /* The parser to be deleted */
  void (*freeProc)(void*)     /* Function used to reclaim memory */
){
  yyParser *pParser = (yyParser*)p;
#ifndef YYPARSEFREENEVERNULL
  if( pParser==0 ) return;
#endif
  while( pParser->yytos>pParser->yystack ) yy_pop_parser_stack(pParser);
#if YYSTACKDEPTH<=0
  if( pParser->yystack!=&pParser->yystk0 ) free(pParser->yystack);
#endif
  (*freeProc)((void*)pParser);
}

/*
** Return the peak depth of the stack for a parser.
*/
#ifdef YYTRACKMAXSTACKDEPTH
int sqlite3ParserStackPeak(void *p){
  yyParser *pParser = (yyParser*)p;
  return pParser->yyhwm;
}
#endif

/*
** Find the appropriate action for a parser given the terminal
** look-ahead token iLookAhead.
*/
static unsigned int yy_find_shift_action(
  yyParser *pParser,        /* The parser */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
  int stateno = pParser->yytos->stateno;
 
  if( stateno>=YY_MIN_REDUCE ) return stateno;
  assert( stateno <= YY_SHIFT_COUNT );
  do{
    i = yy_shift_ofst[stateno];
    assert( iLookAhead!=YYNOCODE );
    i += iLookAhead;
    if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
#ifdef YYFALLBACK
      YYCODETYPE iFallback = -1;            /* Fallback token */
      if( iLookAhead<sizeof(yyFallback)/sizeof(yyFallback[0])
             && (iFallback = yyFallback[iLookAhead])!=0 ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE, "%sFALLBACK %s => %s\n",
             yyTracePrompt, yyTokenName[iLookAhead], yyTokenName[iFallback]);
        }
#endif
        assert( yyFallback[iFallback]==0 ); /* Fallback loop must terminate */
        iLookAhead = iFallback;
        continue;
      } else if ( iFallback==0 ) {
        pParser->is_fallback_failed = true;
      }
#endif
#ifdef YYWILDCARD
      {
        int j = i - iLookAhead + YYWILDCARD;
        if( 
#if YY_SHIFT_MIN+YYWILDCARD<0
          j>=0 &&
#endif
#if YY_SHIFT_MAX+YYWILDCARD>=YY_ACTTAB_COUNT
          j<YY_ACTTAB_COUNT &&
#endif
          yy_lookahead[j]==YYWILDCARD && iLookAhead>0
        ){
#ifndef NDEBUG
          if( yyTraceFILE ){
            fprintf(yyTraceFILE, "%sWILDCARD %s => %s\n",
               yyTracePrompt, yyTokenName[iLookAhead],
               yyTokenName[YYWILDCARD]);
          }
#endif /* NDEBUG */
          return yy_action[j];
        }
      }
#endif /* YYWILDCARD */
      return yy_default[stateno];
    }else{
      return yy_action[i];
    }
  }while(1);
}

/*
** Find the appropriate action for a parser given the non-terminal
** look-ahead token iLookAhead.
*/
static int yy_find_reduce_action(
  int stateno,              /* Current state number */
  YYCODETYPE iLookAhead     /* The look-ahead token */
){
  int i;
#ifdef YYERRORSYMBOL
  if( stateno>YY_REDUCE_COUNT ){
    return yy_default[stateno];
  }
#else
  assert( stateno<=YY_REDUCE_COUNT );
#endif
  i = yy_reduce_ofst[stateno];
  assert( i!=YY_REDUCE_USE_DFLT );
  assert( iLookAhead!=YYNOCODE );
  i += iLookAhead;
#ifdef YYERRORSYMBOL
  if( i<0 || i>=YY_ACTTAB_COUNT || yy_lookahead[i]!=iLookAhead ){
    return yy_default[stateno];
  }
#else
  assert( i>=0 && i<YY_ACTTAB_COUNT );
  assert( yy_lookahead[i]==iLookAhead );
#endif
  return yy_action[i];
}

/*
** The following routine is called if the stack overflows.
*/
static void yyStackOverflow(yyParser *yypParser){
   sqlite3ParserARG_FETCH;
#ifndef NDEBUG
   if( yyTraceFILE ){
     fprintf(yyTraceFILE,"%sStack Overflow!\n",yyTracePrompt);
   }
#endif
   while( yypParser->yytos>yypParser->yystack ) yy_pop_parser_stack(yypParser);
   /* Here code is inserted which will execute if the parser
   ** stack every overflows */
/******** Begin %stack_overflow code ******************************************/
#line 41 "parse.y"

  sqlite3ErrorMsg(pParse, "parser stack overflow");
#line 1739 "parse.c"
/******** End %stack_overflow code ********************************************/
   sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument var */
}

/*
** Print tracing information for a SHIFT action
*/
#ifndef NDEBUG
static void yyTraceShift(yyParser *yypParser, int yyNewState){
  if( yyTraceFILE ){
    if( yyNewState<YYNSTATE ){
      fprintf(yyTraceFILE,"%sShift '%s', go to state %d\n",
         yyTracePrompt,yyTokenName[yypParser->yytos->major],
         yyNewState);
    }else{
      fprintf(yyTraceFILE,"%sShift '%s'\n",
         yyTracePrompt,yyTokenName[yypParser->yytos->major]);
    }
  }
}
#else
# define yyTraceShift(X,Y)
#endif

/*
** Perform a shift action.
*/
static void yy_shift(
  yyParser *yypParser,          /* The parser to be shifted */
  int yyNewState,               /* The new state to shift in */
  int yyMajor,                  /* The major token to shift in */
  sqlite3ParserTOKENTYPE yyMinor        /* The minor token to shift in */
){
  yyStackEntry *yytos;
  yypParser->yytos++;
#ifdef YYTRACKMAXSTACKDEPTH
  if( (int)(yypParser->yytos - yypParser->yystack)>yypParser->yyhwm ){
    yypParser->yyhwm++;
    assert( yypParser->yyhwm == (int)(yypParser->yytos - yypParser->yystack) );
  }
#endif
#if YYSTACKDEPTH>0 
  if( yypParser->yytos>=&yypParser->yystack[YYSTACKDEPTH] ){
    yypParser->yytos--;
    yyStackOverflow(yypParser);
    return;
  }
#else
  if( yypParser->yytos>=&yypParser->yystack[yypParser->yystksz] ){
    if( yyGrowStack(yypParser) ){
      yypParser->yytos--;
      yyStackOverflow(yypParser);
      return;
    }
  }
#endif
  if( yyNewState > YY_MAX_SHIFT ){
    yyNewState += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
  }
  yytos = yypParser->yytos;
  yytos->stateno = (YYACTIONTYPE)yyNewState;
  yytos->major = (YYCODETYPE)yyMajor;
  yytos->minor.yy0 = yyMinor;
  yyTraceShift(yypParser, yyNewState);
}

/* The following table contains information about every rule that
** is used during the reduce.
*/
static const struct {
  YYCODETYPE lhs;         /* Symbol on the left-hand side of the rule */
  unsigned char nrhs;     /* Number of right-hand side symbols in the rule */
} yyRuleInfo[] = {
  { 139, 3 },
  { 139, 1 },
  { 140, 1 },
  { 140, 3 },
  { 142, 3 },
  { 143, 0 },
  { 143, 1 },
  { 142, 2 },
  { 142, 2 },
  { 142, 2 },
  { 142, 2 },
  { 142, 3 },
  { 142, 5 },
  { 147, 4 },
  { 149, 1 },
  { 150, 0 },
  { 150, 3 },
  { 148, 5 },
  { 148, 2 },
  { 153, 0 },
  { 153, 2 },
  { 155, 2 },
  { 145, 1 },
  { 157, 0 },
  { 157, 4 },
  { 157, 6 },
  { 158, 2 },
  { 162, 2 },
  { 162, 2 },
  { 162, 4 },
  { 162, 3 },
  { 162, 3 },
  { 162, 2 },
  { 162, 3 },
  { 162, 5 },
  { 162, 2 },
  { 162, 4 },
  { 162, 4 },
  { 162, 1 },
  { 162, 2 },
  { 167, 0 },
  { 167, 1 },
  { 169, 0 },
  { 169, 2 },
  { 171, 2 },
  { 171, 3 },
  { 171, 3 },
  { 171, 3 },
  { 172, 2 },
  { 172, 2 },
  { 172, 1 },
  { 172, 1 },
  { 172, 2 },
  { 170, 3 },
  { 170, 2 },
  { 173, 0 },
  { 173, 2 },
  { 173, 2 },
  { 152, 0 },
  { 175, 1 },
  { 176, 2 },
  { 176, 7 },
  { 176, 5 },
  { 176, 5 },
  { 176, 10 },
  { 179, 0 },
  { 165, 0 },
  { 165, 3 },
  { 180, 0 },
  { 180, 2 },
  { 181, 1 },
  { 181, 1 },
  { 142, 4 },
  { 183, 2 },
  { 183, 0 },
  { 142, 7 },
  { 142, 4 },
  { 142, 1 },
  { 154, 2 },
  { 185, 3 },
  { 188, 1 },
  { 188, 2 },
  { 188, 1 },
  { 186, 9 },
  { 197, 4 },
  { 197, 5 },
  { 189, 1 },
  { 189, 1 },
  { 189, 0 },
  { 200, 0 },
  { 190, 3 },
  { 190, 2 },
  { 190, 4 },
  { 201, 2 },
  { 201, 0 },
  { 191, 0 },
  { 191, 2 },
  { 203, 2 },
  { 203, 0 },
  { 202, 6 },
  { 202, 8 },
  { 202, 7 },
  { 202, 7 },
  { 184, 1 },
  { 204, 1 },
  { 204, 2 },
  { 204, 3 },
  { 204, 4 },
  { 206, 2 },
  { 206, 0 },
  { 205, 0 },
  { 205, 3 },
  { 205, 2 },
  { 207, 4 },
  { 207, 0 },
  { 195, 0 },
  { 195, 3 },
  { 177, 4 },
  { 177, 2 },
  { 166, 1 },
  { 166, 1 },
  { 166, 0 },
  { 193, 0 },
  { 193, 3 },
  { 194, 0 },
  { 194, 2 },
  { 196, 0 },
  { 196, 2 },
  { 196, 4 },
  { 196, 4 },
  { 142, 6 },
  { 192, 0 },
  { 192, 2 },
  { 142, 8 },
  { 210, 5 },
  { 210, 7 },
  { 210, 3 },
  { 210, 5 },
  { 142, 6 },
  { 142, 7 },
  { 211, 2 },
  { 211, 1 },
  { 212, 0 },
  { 212, 3 },
  { 209, 3 },
  { 209, 1 },
  { 164, 3 },
  { 163, 1 },
  { 164, 1 },
  { 164, 1 },
  { 164, 3 },
  { 163, 1 },
  { 163, 1 },
  { 163, 1 },
  { 164, 1 },
  { 164, 3 },
  { 164, 6 },
  { 164, 5 },
  { 164, 4 },
  { 163, 1 },
  { 164, 5 },
  { 164, 3 },
  { 164, 3 },
  { 164, 3 },
  { 164, 3 },
  { 164, 3 },
  { 164, 3 },
  { 164, 3 },
  { 164, 3 },
  { 213, 1 },
  { 213, 2 },
  { 164, 3 },
  { 164, 5 },
  { 164, 2 },
  { 164, 3 },
  { 164, 3 },
  { 164, 4 },
  { 164, 2 },
  { 164, 2 },
  { 164, 2 },
  { 164, 2 },
  { 214, 1 },
  { 214, 2 },
  { 164, 5 },
  { 215, 1 },
  { 215, 2 },
  { 164, 5 },
  { 164, 3 },
  { 164, 5 },
  { 164, 4 },
  { 164, 4 },
  { 164, 5 },
  { 218, 5 },
  { 218, 4 },
  { 219, 2 },
  { 219, 0 },
  { 217, 1 },
  { 217, 0 },
  { 199, 0 },
  { 198, 3 },
  { 198, 1 },
  { 216, 0 },
  { 216, 3 },
  { 142, 11 },
  { 220, 1 },
  { 220, 0 },
  { 168, 0 },
  { 168, 3 },
  { 178, 5 },
  { 178, 3 },
  { 221, 0 },
  { 221, 2 },
  { 142, 6 },
  { 142, 2 },
  { 142, 4 },
  { 142, 5 },
  { 142, 4 },
  { 142, 5 },
  { 142, 6 },
  { 160, 2 },
  { 161, 2 },
  { 142, 5 },
  { 223, 9 },
  { 225, 1 },
  { 225, 1 },
  { 225, 2 },
  { 225, 0 },
  { 226, 1 },
  { 226, 1 },
  { 226, 3 },
  { 228, 0 },
  { 228, 2 },
  { 224, 3 },
  { 224, 2 },
  { 230, 3 },
  { 231, 3 },
  { 231, 2 },
  { 229, 7 },
  { 229, 5 },
  { 229, 5 },
  { 229, 1 },
  { 164, 4 },
  { 164, 6 },
  { 182, 1 },
  { 182, 1 },
  { 182, 1 },
  { 142, 4 },
  { 142, 1 },
  { 142, 2 },
  { 142, 4 },
  { 142, 1 },
  { 142, 2 },
  { 142, 6 },
  { 142, 7 },
  { 232, 1 },
  { 187, 0 },
  { 187, 2 },
  { 187, 3 },
  { 234, 6 },
  { 234, 8 },
  { 138, 1 },
  { 140, 0 },
  { 141, 1 },
  { 144, 0 },
  { 144, 1 },
  { 144, 2 },
  { 146, 1 },
  { 146, 0 },
  { 142, 2 },
  { 151, 4 },
  { 151, 2 },
  { 157, 1 },
  { 158, 1 },
  { 159, 1 },
  { 159, 1 },
  { 156, 2 },
  { 156, 0 },
  { 162, 2 },
  { 152, 2 },
  { 174, 3 },
  { 174, 1 },
  { 175, 0 },
  { 179, 1 },
  { 181, 1 },
  { 185, 1 },
  { 186, 1 },
  { 200, 2 },
  { 201, 1 },
  { 208, 1 },
  { 208, 1 },
  { 164, 1 },
  { 199, 1 },
  { 222, 1 },
  { 222, 1 },
  { 222, 1 },
  { 222, 1 },
  { 222, 1 },
  { 222, 1 },
  { 160, 1 },
  { 227, 0 },
  { 227, 3 },
  { 230, 1 },
  { 231, 0 },
  { 233, 0 },
  { 233, 1 },
};

static void yy_accept(yyParser*);  /* Forward Declaration */

/*
** Perform a reduce action and the shift that must immediately
** follow the reduce.
*/
static void yy_reduce(
  yyParser *yypParser,         /* The parser */
  unsigned int yyruleno        /* Number of the rule by which to reduce */
){
  int yygoto;                     /* The next state */
  int yyact;                      /* The next action */
  yyStackEntry *yymsp;            /* The top of the parser's stack */
  int yysize;                     /* Amount to pop the stack */
  sqlite3ParserARG_FETCH;
  yymsp = yypParser->yytos;
#ifndef NDEBUG
  if( yyTraceFILE && yyruleno<(int)(sizeof(yyRuleName)/sizeof(yyRuleName[0])) ){
    yysize = yyRuleInfo[yyruleno].nrhs;
    fprintf(yyTraceFILE, "%sReduce [%s], go to state %d.\n", yyTracePrompt,
      yyRuleName[yyruleno], yymsp[-yysize].stateno);
  }
#endif /* NDEBUG */

  /* Check that the stack is large enough to grow by a single entry
  ** if the RHS of the rule is empty.  This ensures that there is room
  ** enough on the stack to push the LHS value */
  if( yyRuleInfo[yyruleno].nrhs==0 ){
#ifdef YYTRACKMAXSTACKDEPTH
    if( (int)(yypParser->yytos - yypParser->yystack)>yypParser->yyhwm ){
      yypParser->yyhwm++;
      assert( yypParser->yyhwm == (int)(yypParser->yytos - yypParser->yystack));
    }
#endif
#if YYSTACKDEPTH>0 
    if( yypParser->yytos>=&yypParser->yystack[YYSTACKDEPTH-1] ){
      yyStackOverflow(yypParser);
      return;
    }
#else
    if( yypParser->yytos>=&yypParser->yystack[yypParser->yystksz-1] ){
      if( yyGrowStack(yypParser) ){
        yyStackOverflow(yypParser);
        return;
      }
      yymsp = yypParser->yytos;
    }
#endif
  }

  switch( yyruleno ){
  /* Beginning here are the reduction cases.  A typical example
  ** follows:
  **   case 0:
  **  #line <lineno> <grammarfile>
  **     { ... }           // User supplied code
  **  #line <lineno> <thisfile>
  **     break;
  */
/********** Begin reduce actions **********************************************/
        YYMINORTYPE yylhsminor;
      case 0: /* ecmd ::= explain cmdx SEMI */
#line 111 "parse.y"
{ sqlite3FinishCoding(pParse); }
#line 2184 "parse.c"
        break;
      case 1: /* ecmd ::= SEMI */
#line 112 "parse.y"
{
  sqlite3ErrorMsg(pParse, "syntax error: empty request");
}
#line 2191 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN */
#line 117 "parse.y"
{ pParse->explain = 1; }
#line 2196 "parse.c"
        break;
      case 3: /* explain ::= EXPLAIN QUERY PLAN */
#line 118 "parse.y"
{ pParse->explain = 2; }
#line 2201 "parse.c"
        break;
      case 4: /* cmd ::= BEGIN transtype trans_opt */
#line 150 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy92);}
#line 2206 "parse.c"
        break;
      case 5: /* transtype ::= */
#line 155 "parse.y"
{yymsp[1].minor.yy92 = TK_DEFERRED;}
#line 2211 "parse.c"
        break;
      case 6: /* transtype ::= DEFERRED */
#line 156 "parse.y"
{yymsp[0].minor.yy92 = yymsp[0].major; /*A-overwrites-X*/}
#line 2216 "parse.c"
        break;
      case 7: /* cmd ::= COMMIT trans_opt */
      case 8: /* cmd ::= END trans_opt */ yytestcase(yyruleno==8);
#line 157 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2222 "parse.c"
        break;
      case 9: /* cmd ::= ROLLBACK trans_opt */
#line 159 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2227 "parse.c"
        break;
      case 10: /* cmd ::= SAVEPOINT nm */
#line 163 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2234 "parse.c"
        break;
      case 11: /* cmd ::= RELEASE savepoint_opt nm */
#line 166 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2241 "parse.c"
        break;
      case 12: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 169 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2248 "parse.c"
        break;
      case 13: /* create_table ::= createkw TABLE ifnotexists nm */
#line 176 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[0].minor.yy0,yymsp[-1].minor.yy92);
}
#line 2255 "parse.c"
        break;
      case 14: /* createkw ::= CREATE */
#line 179 "parse.y"
{disableLookaside(pParse);}
#line 2260 "parse.c"
        break;
      case 15: /* ifnotexists ::= */
      case 19: /* table_options ::= */ yytestcase(yyruleno==19);
      case 40: /* autoinc ::= */ yytestcase(yyruleno==40);
      case 55: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==55);
      case 65: /* defer_subclause_opt ::= */ yytestcase(yyruleno==65);
      case 74: /* ifexists ::= */ yytestcase(yyruleno==74);
      case 88: /* distinct ::= */ yytestcase(yyruleno==88);
      case 210: /* collate ::= */ yytestcase(yyruleno==210);
#line 182 "parse.y"
{yymsp[1].minor.yy92 = 0;}
#line 2272 "parse.c"
        break;
      case 16: /* ifnotexists ::= IF NOT EXISTS */
#line 183 "parse.y"
{yymsp[-2].minor.yy92 = 1;}
#line 2277 "parse.c"
        break;
      case 17: /* create_table_args ::= LP columnlist conslist_opt RP table_options */
#line 185 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,yymsp[0].minor.yy92,0);
}
#line 2284 "parse.c"
        break;
      case 18: /* create_table_args ::= AS select */
#line 188 "parse.y"
{
  sqlite3EndTable(pParse,0,0,0,yymsp[0].minor.yy329);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy329);
}
#line 2292 "parse.c"
        break;
      case 20: /* table_options ::= WITHOUT nm */
#line 194 "parse.y"
{
  if( yymsp[0].minor.yy0.n==5 && sqlite3_strnicmp(yymsp[0].minor.yy0.z,"rowid",5)==0 ){
    yymsp[-1].minor.yy92 = TF_WithoutRowid | TF_NoVisibleRowid;
  }else{
    yymsp[-1].minor.yy92 = 0;
    sqlite3ErrorMsg(pParse, "unknown table option: %.*s", yymsp[0].minor.yy0.n, yymsp[0].minor.yy0.z);
  }
}
#line 2304 "parse.c"
        break;
      case 21: /* columnname ::= nm typetoken */
#line 204 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2309 "parse.c"
        break;
      case 22: /* nm ::= ID|INDEXED */
#line 235 "parse.y"
{
  if(yymsp[0].minor.yy0.isReserved) {
    sqlite3ErrorMsg(pParse, "keyword \"%T\" is reserved", &yymsp[0].minor.yy0);
  }
}
#line 2318 "parse.c"
        break;
      case 23: /* typetoken ::= */
      case 58: /* conslist_opt ::= */ yytestcase(yyruleno==58);
      case 94: /* as ::= */ yytestcase(yyruleno==94);
#line 246 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2325 "parse.c"
        break;
      case 24: /* typetoken ::= typename LP signed RP */
#line 248 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2332 "parse.c"
        break;
      case 25: /* typetoken ::= typename LP signed COMMA signed RP */
#line 251 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2339 "parse.c"
        break;
      case 26: /* typename ::= typename ID|STRING */
#line 256 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2344 "parse.c"
        break;
      case 27: /* ccons ::= CONSTRAINT nm */
      case 60: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==60);
#line 265 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2350 "parse.c"
        break;
      case 28: /* ccons ::= DEFAULT term */
      case 30: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==30);
#line 266 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy412);}
#line 2356 "parse.c"
        break;
      case 29: /* ccons ::= DEFAULT LP expr RP */
#line 267 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy412);}
#line 2361 "parse.c"
        break;
      case 31: /* ccons ::= DEFAULT MINUS term */
#line 269 "parse.y"
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy412.pExpr, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy412.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2372 "parse.c"
        break;
      case 32: /* ccons ::= DEFAULT ID|INDEXED */
#line 276 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2381 "parse.c"
        break;
      case 33: /* ccons ::= NOT NULL onconf */
#line 286 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy92);}
#line 2386 "parse.c"
        break;
      case 34: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 288 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy92,yymsp[0].minor.yy92,yymsp[-2].minor.yy92);}
#line 2391 "parse.c"
        break;
      case 35: /* ccons ::= UNIQUE onconf */
#line 289 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[0].minor.yy92,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2397 "parse.c"
        break;
      case 36: /* ccons ::= CHECK LP expr RP */
#line 291 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy412.pExpr);}
#line 2402 "parse.c"
        break;
      case 37: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 293 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy93,yymsp[0].minor.yy92);}
#line 2407 "parse.c"
        break;
      case 38: /* ccons ::= defer_subclause */
#line 294 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy92);}
#line 2412 "parse.c"
        break;
      case 39: /* ccons ::= COLLATE ID|INDEXED */
#line 295 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2417 "parse.c"
        break;
      case 41: /* autoinc ::= AUTOINCR */
#line 300 "parse.y"
{yymsp[0].minor.yy92 = 1;}
#line 2422 "parse.c"
        break;
      case 42: /* refargs ::= */
#line 308 "parse.y"
{ yymsp[1].minor.yy92 = OE_None*0x0101; /* EV: R-19803-45884 */}
#line 2427 "parse.c"
        break;
      case 43: /* refargs ::= refargs refarg */
#line 309 "parse.y"
{ yymsp[-1].minor.yy92 = (yymsp[-1].minor.yy92 & ~yymsp[0].minor.yy97.mask) | yymsp[0].minor.yy97.value; }
#line 2432 "parse.c"
        break;
      case 44: /* refarg ::= MATCH nm */
#line 311 "parse.y"
{ yymsp[-1].minor.yy97.value = 0;     yymsp[-1].minor.yy97.mask = 0x000000; }
#line 2437 "parse.c"
        break;
      case 45: /* refarg ::= ON INSERT refact */
#line 312 "parse.y"
{ yymsp[-2].minor.yy97.value = 0;     yymsp[-2].minor.yy97.mask = 0x000000; }
#line 2442 "parse.c"
        break;
      case 46: /* refarg ::= ON DELETE refact */
#line 313 "parse.y"
{ yymsp[-2].minor.yy97.value = yymsp[0].minor.yy92;     yymsp[-2].minor.yy97.mask = 0x0000ff; }
#line 2447 "parse.c"
        break;
      case 47: /* refarg ::= ON UPDATE refact */
#line 314 "parse.y"
{ yymsp[-2].minor.yy97.value = yymsp[0].minor.yy92<<8;  yymsp[-2].minor.yy97.mask = 0x00ff00; }
#line 2452 "parse.c"
        break;
      case 48: /* refact ::= SET NULL */
#line 316 "parse.y"
{ yymsp[-1].minor.yy92 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2457 "parse.c"
        break;
      case 49: /* refact ::= SET DEFAULT */
#line 317 "parse.y"
{ yymsp[-1].minor.yy92 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2462 "parse.c"
        break;
      case 50: /* refact ::= CASCADE */
#line 318 "parse.y"
{ yymsp[0].minor.yy92 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2467 "parse.c"
        break;
      case 51: /* refact ::= RESTRICT */
#line 319 "parse.y"
{ yymsp[0].minor.yy92 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2472 "parse.c"
        break;
      case 52: /* refact ::= NO ACTION */
#line 320 "parse.y"
{ yymsp[-1].minor.yy92 = OE_None;     /* EV: R-33326-45252 */}
#line 2477 "parse.c"
        break;
      case 53: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 322 "parse.y"
{yymsp[-2].minor.yy92 = 0;}
#line 2482 "parse.c"
        break;
      case 54: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 69: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==69);
      case 140: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==140);
#line 323 "parse.y"
{yymsp[-1].minor.yy92 = yymsp[0].minor.yy92;}
#line 2489 "parse.c"
        break;
      case 56: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 73: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==73);
      case 182: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==182);
      case 185: /* in_op ::= NOT IN */ yytestcase(yyruleno==185);
      case 211: /* collate ::= COLLATE ID|INDEXED */ yytestcase(yyruleno==211);
#line 326 "parse.y"
{yymsp[-1].minor.yy92 = 1;}
#line 2498 "parse.c"
        break;
      case 57: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 327 "parse.y"
{yymsp[-1].minor.yy92 = 0;}
#line 2503 "parse.c"
        break;
      case 59: /* tconscomma ::= COMMA */
#line 333 "parse.y"
{pParse->constraintName.n = 0;}
#line 2508 "parse.c"
        break;
      case 61: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 337 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy93,yymsp[0].minor.yy92,yymsp[-2].minor.yy92,0);}
#line 2513 "parse.c"
        break;
      case 62: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 339 "parse.y"
{sqlite3CreateIndex(pParse,0,0,yymsp[-2].minor.yy93,yymsp[0].minor.yy92,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2519 "parse.c"
        break;
      case 63: /* tcons ::= CHECK LP expr RP onconf */
#line 342 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy412.pExpr);}
#line 2524 "parse.c"
        break;
      case 64: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 344 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy93, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy93, yymsp[-1].minor.yy92);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy92);
}
#line 2532 "parse.c"
        break;
      case 66: /* onconf ::= */
      case 68: /* orconf ::= */ yytestcase(yyruleno==68);
#line 358 "parse.y"
{yymsp[1].minor.yy92 = OE_Default;}
#line 2538 "parse.c"
        break;
      case 67: /* onconf ::= ON CONFLICT resolvetype */
#line 359 "parse.y"
{yymsp[-2].minor.yy92 = yymsp[0].minor.yy92;}
#line 2543 "parse.c"
        break;
      case 70: /* resolvetype ::= IGNORE */
#line 363 "parse.y"
{yymsp[0].minor.yy92 = OE_Ignore;}
#line 2548 "parse.c"
        break;
      case 71: /* resolvetype ::= REPLACE */
      case 141: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==141);
#line 364 "parse.y"
{yymsp[0].minor.yy92 = OE_Replace;}
#line 2554 "parse.c"
        break;
      case 72: /* cmd ::= DROP TABLE ifexists fullname */
#line 368 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy167, 0, yymsp[-1].minor.yy92);
}
#line 2561 "parse.c"
        break;
      case 75: /* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 379 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-6].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy93, yymsp[0].minor.yy329, yymsp[-4].minor.yy92);
}
#line 2568 "parse.c"
        break;
      case 76: /* cmd ::= DROP VIEW ifexists fullname */
#line 382 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy167, 1, yymsp[-1].minor.yy92);
}
#line 2575 "parse.c"
        break;
      case 77: /* cmd ::= select */
#line 389 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy329, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy329);
}
#line 2584 "parse.c"
        break;
      case 78: /* select ::= with selectnowith */
#line 426 "parse.y"
{
  Select *p = yymsp[0].minor.yy329;
  if( p ){
    p->pWith = yymsp[-1].minor.yy121;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, yymsp[-1].minor.yy121);
  }
  yymsp[-1].minor.yy329 = p; /*A-overwrites-W*/
}
#line 2598 "parse.c"
        break;
      case 79: /* selectnowith ::= selectnowith multiselect_op oneselect */
#line 439 "parse.y"
{
  Select *pRhs = yymsp[0].minor.yy329;
  Select *pLhs = yymsp[-2].minor.yy329;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,&x,pRhs,0,0);
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)yymsp[-1].minor.yy92;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( yymsp[-1].minor.yy92!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  yymsp[-2].minor.yy329 = pRhs;
}
#line 2624 "parse.c"
        break;
      case 80: /* multiselect_op ::= UNION */
      case 82: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==82);
#line 462 "parse.y"
{yymsp[0].minor.yy92 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2630 "parse.c"
        break;
      case 81: /* multiselect_op ::= UNION ALL */
#line 463 "parse.y"
{yymsp[-1].minor.yy92 = TK_ALL;}
#line 2635 "parse.c"
        break;
      case 83: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 467 "parse.y"
{
#if SELECTTRACE_ENABLED
  Token s = yymsp[-8].minor.yy0; /*A-overwrites-S*/
#endif
  yymsp[-8].minor.yy329 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy93,yymsp[-5].minor.yy167,yymsp[-4].minor.yy112,yymsp[-3].minor.yy93,yymsp[-2].minor.yy112,yymsp[-1].minor.yy93,yymsp[-7].minor.yy92,yymsp[0].minor.yy464.pLimit,yymsp[0].minor.yy464.pOffset);
#if SELECTTRACE_ENABLED
  /* Populate the Select.zSelName[] string that is used to help with
  ** query planner debugging, to differentiate between multiple Select
  ** objects in a complex query.
  **
  ** If the SELECT keyword is immediately followed by a C-style comment
  ** then extract the first few alphanumeric characters from within that
  ** comment to be the zSelName value.  Otherwise, the label is #N where
  ** is an integer that is incremented with each SELECT statement seen.
  */
  if( yymsp[-8].minor.yy329!=0 ){
    const char *z = s.z+6;
    int i;
    sqlite3_snprintf(sizeof(yymsp[-8].minor.yy329->zSelName), yymsp[-8].minor.yy329->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(yymsp[-8].minor.yy329->zSelName), yymsp[-8].minor.yy329->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
#line 2669 "parse.c"
        break;
      case 84: /* values ::= VALUES LP nexprlist RP */
#line 501 "parse.y"
{
  yymsp[-3].minor.yy329 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy93,0,0,0,0,0,SF_Values,0,0);
}
#line 2676 "parse.c"
        break;
      case 85: /* values ::= values COMMA LP exprlist RP */
#line 504 "parse.y"
{
  Select *pRight, *pLeft = yymsp[-4].minor.yy329;
  pRight = sqlite3SelectNew(pParse,yymsp[-1].minor.yy93,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    yymsp[-4].minor.yy329 = pRight;
  }else{
    yymsp[-4].minor.yy329 = pLeft;
  }
}
#line 2692 "parse.c"
        break;
      case 86: /* distinct ::= DISTINCT */
#line 521 "parse.y"
{yymsp[0].minor.yy92 = SF_Distinct;}
#line 2697 "parse.c"
        break;
      case 87: /* distinct ::= ALL */
#line 522 "parse.y"
{yymsp[0].minor.yy92 = SF_All;}
#line 2702 "parse.c"
        break;
      case 89: /* sclp ::= */
      case 115: /* orderby_opt ::= */ yytestcase(yyruleno==115);
      case 122: /* groupby_opt ::= */ yytestcase(yyruleno==122);
      case 198: /* exprlist ::= */ yytestcase(yyruleno==198);
      case 201: /* paren_exprlist ::= */ yytestcase(yyruleno==201);
      case 206: /* eidlist_opt ::= */ yytestcase(yyruleno==206);
#line 535 "parse.y"
{yymsp[1].minor.yy93 = 0;}
#line 2712 "parse.c"
        break;
      case 90: /* selcollist ::= sclp expr as */
#line 536 "parse.y"
{
   yymsp[-2].minor.yy93 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy93, yymsp[-1].minor.yy412.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy93, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy93,&yymsp[-1].minor.yy412);
}
#line 2721 "parse.c"
        break;
      case 91: /* selcollist ::= sclp STAR */
#line 541 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy93 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy93, p);
}
#line 2729 "parse.c"
        break;
      case 92: /* selcollist ::= sclp nm DOT STAR */
#line 545 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy93, pDot);
}
#line 2739 "parse.c"
        break;
      case 93: /* as ::= AS nm */
      case 219: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==219);
      case 220: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==220);
#line 556 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2746 "parse.c"
        break;
      case 95: /* from ::= */
#line 570 "parse.y"
{yymsp[1].minor.yy167 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy167));}
#line 2751 "parse.c"
        break;
      case 96: /* from ::= FROM seltablist */
#line 571 "parse.y"
{
  yymsp[-1].minor.yy167 = yymsp[0].minor.yy167;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy167);
}
#line 2759 "parse.c"
        break;
      case 97: /* stl_prefix ::= seltablist joinop */
#line 579 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy167 && yymsp[-1].minor.yy167->nSrc>0) ) yymsp[-1].minor.yy167->a[yymsp[-1].minor.yy167->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy92;
}
#line 2766 "parse.c"
        break;
      case 98: /* stl_prefix ::= */
#line 582 "parse.y"
{yymsp[1].minor.yy167 = 0;}
#line 2771 "parse.c"
        break;
      case 99: /* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 584 "parse.y"
{
  yymsp[-5].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-5].minor.yy167,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
  sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy167, &yymsp[-2].minor.yy0);
}
#line 2779 "parse.c"
        break;
      case 100: /* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 589 "parse.y"
{
  yymsp[-7].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-7].minor.yy167,&yymsp[-6].minor.yy0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
  sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy167, yymsp[-4].minor.yy93);
}
#line 2787 "parse.c"
        break;
      case 101: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 595 "parse.y"
{
    yymsp[-6].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy167,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy329,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
  }
#line 2794 "parse.c"
        break;
      case 102: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 599 "parse.y"
{
    if( yymsp[-6].minor.yy167==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy112==0 && yymsp[0].minor.yy330==0 ){
      yymsp[-6].minor.yy167 = yymsp[-4].minor.yy167;
    }else if( yymsp[-4].minor.yy167->nSrc==1 ){
      yymsp[-6].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy167,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
      if( yymsp[-6].minor.yy167 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy167->a[yymsp[-6].minor.yy167->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy167->a;
        pNew->zName = pOld->zName;
        pNew->pSelect = pOld->pSelect;
        pOld->zName =  0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy167);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy167);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy167,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy167,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
    }
  }
#line 2819 "parse.c"
        break;
      case 103: /* fullname ::= nm */
#line 625 "parse.y"
{yymsp[0].minor.yy167 = sqlite3SrcListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 2824 "parse.c"
        break;
      case 104: /* joinop ::= COMMA|JOIN */
#line 631 "parse.y"
{ yymsp[0].minor.yy92 = JT_INNER; }
#line 2829 "parse.c"
        break;
      case 105: /* joinop ::= JOIN_KW JOIN */
#line 633 "parse.y"
{yymsp[-1].minor.yy92 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2834 "parse.c"
        break;
      case 106: /* joinop ::= JOIN_KW join_nm JOIN */
#line 635 "parse.y"
{yymsp[-2].minor.yy92 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2839 "parse.c"
        break;
      case 107: /* joinop ::= JOIN_KW join_nm join_nm JOIN */
#line 637 "parse.y"
{yymsp[-3].minor.yy92 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2844 "parse.c"
        break;
      case 108: /* on_opt ::= ON expr */
      case 125: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==125);
      case 132: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==132);
      case 194: /* case_else ::= ELSE expr */ yytestcase(yyruleno==194);
#line 641 "parse.y"
{yymsp[-1].minor.yy112 = yymsp[0].minor.yy412.pExpr;}
#line 2852 "parse.c"
        break;
      case 109: /* on_opt ::= */
      case 124: /* having_opt ::= */ yytestcase(yyruleno==124);
      case 131: /* where_opt ::= */ yytestcase(yyruleno==131);
      case 195: /* case_else ::= */ yytestcase(yyruleno==195);
      case 197: /* case_operand ::= */ yytestcase(yyruleno==197);
#line 642 "parse.y"
{yymsp[1].minor.yy112 = 0;}
#line 2861 "parse.c"
        break;
      case 110: /* indexed_opt ::= */
#line 655 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2866 "parse.c"
        break;
      case 111: /* indexed_opt ::= INDEXED BY nm */
#line 656 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2871 "parse.c"
        break;
      case 112: /* indexed_opt ::= NOT INDEXED */
#line 657 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2876 "parse.c"
        break;
      case 113: /* using_opt ::= USING LP idlist RP */
#line 661 "parse.y"
{yymsp[-3].minor.yy330 = yymsp[-1].minor.yy330;}
#line 2881 "parse.c"
        break;
      case 114: /* using_opt ::= */
      case 142: /* idlist_opt ::= */ yytestcase(yyruleno==142);
#line 662 "parse.y"
{yymsp[1].minor.yy330 = 0;}
#line 2887 "parse.c"
        break;
      case 116: /* orderby_opt ::= ORDER BY sortlist */
      case 123: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==123);
#line 676 "parse.y"
{yymsp[-2].minor.yy93 = yymsp[0].minor.yy93;}
#line 2893 "parse.c"
        break;
      case 117: /* sortlist ::= sortlist COMMA expr sortorder */
#line 677 "parse.y"
{
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy93,yymsp[-1].minor.yy412.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy93,yymsp[0].minor.yy92);
}
#line 2901 "parse.c"
        break;
      case 118: /* sortlist ::= expr sortorder */
#line 681 "parse.y"
{
  yymsp[-1].minor.yy93 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy412.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy93,yymsp[0].minor.yy92);
}
#line 2909 "parse.c"
        break;
      case 119: /* sortorder ::= ASC */
#line 688 "parse.y"
{yymsp[0].minor.yy92 = SQLITE_SO_ASC;}
#line 2914 "parse.c"
        break;
      case 120: /* sortorder ::= DESC */
#line 689 "parse.y"
{yymsp[0].minor.yy92 = SQLITE_SO_DESC;}
#line 2919 "parse.c"
        break;
      case 121: /* sortorder ::= */
#line 690 "parse.y"
{yymsp[1].minor.yy92 = SQLITE_SO_UNDEFINED;}
#line 2924 "parse.c"
        break;
      case 126: /* limit_opt ::= */
#line 715 "parse.y"
{yymsp[1].minor.yy464.pLimit = 0; yymsp[1].minor.yy464.pOffset = 0;}
#line 2929 "parse.c"
        break;
      case 127: /* limit_opt ::= LIMIT expr */
#line 716 "parse.y"
{yymsp[-1].minor.yy464.pLimit = yymsp[0].minor.yy412.pExpr; yymsp[-1].minor.yy464.pOffset = 0;}
#line 2934 "parse.c"
        break;
      case 128: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 718 "parse.y"
{yymsp[-3].minor.yy464.pLimit = yymsp[-2].minor.yy412.pExpr; yymsp[-3].minor.yy464.pOffset = yymsp[0].minor.yy412.pExpr;}
#line 2939 "parse.c"
        break;
      case 129: /* limit_opt ::= LIMIT expr COMMA expr */
#line 720 "parse.y"
{yymsp[-3].minor.yy464.pOffset = yymsp[-2].minor.yy412.pExpr; yymsp[-3].minor.yy464.pLimit = yymsp[0].minor.yy412.pExpr;}
#line 2944 "parse.c"
        break;
      case 130: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 737 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy121, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy167, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy167,yymsp[0].minor.yy112);
}
#line 2956 "parse.c"
        break;
      case 133: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 770 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy121, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy167, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy93,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Update(pParse,yymsp[-4].minor.yy167,yymsp[-1].minor.yy93,yymsp[0].minor.yy112,yymsp[-5].minor.yy92);
}
#line 2969 "parse.c"
        break;
      case 134: /* setlist ::= setlist COMMA nm EQ expr */
#line 784 "parse.y"
{
  yymsp[-4].minor.yy93 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy93, yymsp[0].minor.yy412.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy93, &yymsp[-2].minor.yy0, 1);
}
#line 2977 "parse.c"
        break;
      case 135: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 788 "parse.y"
{
  yymsp[-6].minor.yy93 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy93, yymsp[-3].minor.yy330, yymsp[0].minor.yy412.pExpr);
}
#line 2984 "parse.c"
        break;
      case 136: /* setlist ::= nm EQ expr */
#line 791 "parse.y"
{
  yylhsminor.yy93 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy412.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy93, &yymsp[-2].minor.yy0, 1);
}
#line 2992 "parse.c"
  yymsp[-2].minor.yy93 = yylhsminor.yy93;
        break;
      case 137: /* setlist ::= LP idlist RP EQ expr */
#line 795 "parse.y"
{
  yymsp[-4].minor.yy93 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy330, yymsp[0].minor.yy412.pExpr);
}
#line 3000 "parse.c"
        break;
      case 138: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 801 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy121, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-2].minor.yy167, yymsp[0].minor.yy329, yymsp[-1].minor.yy330, yymsp[-4].minor.yy92);
}
#line 3011 "parse.c"
        break;
      case 139: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 809 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy121, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-3].minor.yy167, 0, yymsp[-2].minor.yy330, yymsp[-5].minor.yy92);
}
#line 3022 "parse.c"
        break;
      case 143: /* idlist_opt ::= LP idlist RP */
#line 827 "parse.y"
{yymsp[-2].minor.yy330 = yymsp[-1].minor.yy330;}
#line 3027 "parse.c"
        break;
      case 144: /* idlist ::= idlist COMMA nm */
#line 829 "parse.y"
{yymsp[-2].minor.yy330 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy330,&yymsp[0].minor.yy0);}
#line 3032 "parse.c"
        break;
      case 145: /* idlist ::= nm */
#line 831 "parse.y"
{yymsp[0].minor.yy330 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3037 "parse.c"
        break;
      case 146: /* expr ::= LP expr RP */
#line 880 "parse.y"
{spanSet(&yymsp[-2].minor.yy412,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy412.pExpr = yymsp[-1].minor.yy412.pExpr;}
#line 3042 "parse.c"
        break;
      case 147: /* term ::= NULL */
      case 151: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==151);
      case 152: /* term ::= STRING */ yytestcase(yyruleno==152);
#line 881 "parse.y"
{spanExpr(&yymsp[0].minor.yy412,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3049 "parse.c"
        break;
      case 148: /* expr ::= ID|INDEXED */
      case 149: /* expr ::= JOIN_KW */ yytestcase(yyruleno==149);
#line 882 "parse.y"
{spanExpr(&yymsp[0].minor.yy412,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3055 "parse.c"
        break;
      case 150: /* expr ::= nm DOT nm */
#line 884 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy412,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3065 "parse.c"
        break;
      case 153: /* term ::= INTEGER */
#line 892 "parse.y"
{
  yylhsminor.yy412.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy412.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy412.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy412.pExpr ) yylhsminor.yy412.pExpr->flags |= EP_Leaf;
}
#line 3075 "parse.c"
  yymsp[0].minor.yy412 = yylhsminor.yy412;
        break;
      case 154: /* expr ::= VARIABLE */
#line 898 "parse.y"
{
  if( !(yymsp[0].minor.yy0.z[0]=='#' && sqlite3Isdigit(yymsp[0].minor.yy0.z[1])) ){
    u32 n = yymsp[0].minor.yy0.n;
    spanExpr(&yymsp[0].minor.yy412, pParse, TK_VARIABLE, yymsp[0].minor.yy0);
    sqlite3ExprAssignVarNumber(pParse, yymsp[0].minor.yy412.pExpr, n);
  }else{
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    Token t = yymsp[0].minor.yy0; /*A-overwrites-X*/
    assert( t.n>=2 );
    spanSet(&yymsp[0].minor.yy412, &t, &t);
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &t);
      yymsp[0].minor.yy412.pExpr = 0;
    }else{
      yymsp[0].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0);
      if( yymsp[0].minor.yy412.pExpr ) sqlite3GetInt32(&t.z[1], &yymsp[0].minor.yy412.pExpr->iTable);
    }
  }
}
#line 3101 "parse.c"
        break;
      case 155: /* expr ::= expr COLLATE ID|INDEXED */
#line 919 "parse.y"
{
  yymsp[-2].minor.yy412.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy412.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy412.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3109 "parse.c"
        break;
      case 156: /* expr ::= CAST LP expr AS typetoken RP */
#line 924 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy412,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy412.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy412.pExpr, yymsp[-3].minor.yy412.pExpr, 0);
}
#line 3118 "parse.c"
        break;
      case 157: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 930 "parse.y"
{
  if( yymsp[-1].minor.yy93 && yymsp[-1].minor.yy93->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yylhsminor.yy412.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy93, &yymsp[-4].minor.yy0);
  spanSet(&yylhsminor.yy412,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy92==SF_Distinct && yylhsminor.yy412.pExpr ){
    yylhsminor.yy412.pExpr->flags |= EP_Distinct;
  }
}
#line 3132 "parse.c"
  yymsp[-4].minor.yy412 = yylhsminor.yy412;
        break;
      case 158: /* expr ::= ID|INDEXED LP STAR RP */
#line 940 "parse.y"
{
  yylhsminor.yy412.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy412,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3141 "parse.c"
  yymsp[-3].minor.yy412 = yylhsminor.yy412;
        break;
      case 159: /* term ::= CTIME_KW */
#line 944 "parse.y"
{
  yylhsminor.yy412.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy412, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3150 "parse.c"
  yymsp[0].minor.yy412 = yylhsminor.yy412;
        break;
      case 160: /* expr ::= LP nexprlist COMMA expr RP */
#line 973 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy93, yymsp[-1].minor.yy412.pExpr);
  yylhsminor.yy412.pExpr = sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
  if( yylhsminor.yy412.pExpr ){
    yylhsminor.yy412.pExpr->x.pList = pList;
    spanSet(&yylhsminor.yy412, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  }
}
#line 3165 "parse.c"
  yymsp[-4].minor.yy412 = yylhsminor.yy412;
        break;
      case 161: /* expr ::= expr AND expr */
      case 162: /* expr ::= expr OR expr */ yytestcase(yyruleno==162);
      case 163: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==163);
      case 164: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==164);
      case 165: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==165);
      case 166: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==166);
      case 167: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==167);
      case 168: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==168);
#line 984 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy412,&yymsp[0].minor.yy412);}
#line 3178 "parse.c"
        break;
      case 169: /* likeop ::= LIKE_KW|MATCH */
#line 997 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3183 "parse.c"
        break;
      case 170: /* likeop ::= NOT LIKE_KW|MATCH */
#line 998 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3188 "parse.c"
        break;
      case 171: /* expr ::= expr likeop expr */
#line 999 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
  yymsp[-1].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy412.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy412.pExpr);
  yymsp[-2].minor.yy412.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-2].minor.yy412);
  yymsp[-2].minor.yy412.zEnd = yymsp[0].minor.yy412.zEnd;
  if( yymsp[-2].minor.yy412.pExpr ) yymsp[-2].minor.yy412.pExpr->flags |= EP_InfixFunc;
}
#line 3203 "parse.c"
        break;
      case 172: /* expr ::= expr likeop expr ESCAPE expr */
#line 1010 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
  yymsp[-3].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy412.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy412.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy412.pExpr);
  yymsp[-4].minor.yy412.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-4].minor.yy412);
  yymsp[-4].minor.yy412.zEnd = yymsp[0].minor.yy412.zEnd;
  if( yymsp[-4].minor.yy412.pExpr ) yymsp[-4].minor.yy412.pExpr->flags |= EP_InfixFunc;
}
#line 3219 "parse.c"
        break;
      case 173: /* expr ::= expr ISNULL|NOTNULL */
#line 1037 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy412,&yymsp[0].minor.yy0);}
#line 3224 "parse.c"
        break;
      case 174: /* expr ::= expr NOT NULL */
#line 1038 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy412,&yymsp[0].minor.yy0);}
#line 3229 "parse.c"
        break;
      case 175: /* expr ::= expr IS expr */
#line 1059 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy412,&yymsp[0].minor.yy412);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy412.pExpr, yymsp[-2].minor.yy412.pExpr, TK_ISNULL);
}
#line 3237 "parse.c"
        break;
      case 176: /* expr ::= expr IS NOT expr */
#line 1063 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy412,&yymsp[0].minor.yy412);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy412.pExpr, yymsp[-3].minor.yy412.pExpr, TK_NOTNULL);
}
#line 3245 "parse.c"
        break;
      case 177: /* expr ::= NOT expr */
      case 178: /* expr ::= BITNOT expr */ yytestcase(yyruleno==178);
#line 1087 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy412,pParse,yymsp[-1].major,&yymsp[0].minor.yy412,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3251 "parse.c"
        break;
      case 179: /* expr ::= MINUS expr */
#line 1091 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy412,pParse,TK_UMINUS,&yymsp[0].minor.yy412,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3256 "parse.c"
        break;
      case 180: /* expr ::= PLUS expr */
#line 1093 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy412,pParse,TK_UPLUS,&yymsp[0].minor.yy412,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3261 "parse.c"
        break;
      case 181: /* between_op ::= BETWEEN */
      case 184: /* in_op ::= IN */ yytestcase(yyruleno==184);
#line 1096 "parse.y"
{yymsp[0].minor.yy92 = 0;}
#line 3267 "parse.c"
        break;
      case 183: /* expr ::= expr between_op expr AND expr */
#line 1098 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy412.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy412.pExpr);
  yymsp[-4].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy412.pExpr, 0);
  if( yymsp[-4].minor.yy412.pExpr ){
    yymsp[-4].minor.yy412.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, yymsp[-3].minor.yy92, &yymsp[-4].minor.yy412);
  yymsp[-4].minor.yy412.zEnd = yymsp[0].minor.yy412.zEnd;
}
#line 3283 "parse.c"
        break;
      case 186: /* expr ::= expr in_op LP exprlist RP */
#line 1114 "parse.y"
{
    if( yymsp[-1].minor.yy93==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      sqlite3ExprDelete(pParse->db, yymsp[-4].minor.yy412.pExpr);
      yymsp[-4].minor.yy412.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER,&sqlite3IntTokens[yymsp[-3].minor.yy92],1);
    }else if( yymsp[-1].minor.yy93->nExpr==1 ){
      /* Expressions of the form:
      **
      **      expr1 IN (?1)
      **      expr1 NOT IN (?2)
      **
      ** with exactly one value on the RHS can be simplified to something
      ** like this:
      **
      **      expr1 == ?1
      **      expr1 <> ?2
      **
      ** But, the RHS of the == or <> is marked with the EP_Generic flag
      ** so that it may not contribute to the computation of comparison
      ** affinity or the collating sequence to use for comparison.  Otherwise,
      ** the semantics would be subtly different from IN or NOT IN.
      */
      Expr *pRHS = yymsp[-1].minor.yy93->a[0].pExpr;
      yymsp[-1].minor.yy93->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy93);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      yymsp[-4].minor.yy412.pExpr = sqlite3PExpr(pParse, yymsp[-3].minor.yy92 ? TK_NE : TK_EQ, yymsp[-4].minor.yy412.pExpr, pRHS);
    }else{
      yymsp[-4].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy412.pExpr, 0);
      if( yymsp[-4].minor.yy412.pExpr ){
        yymsp[-4].minor.yy412.pExpr->x.pList = yymsp[-1].minor.yy93;
        sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy412.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy93);
      }
      exprNot(pParse, yymsp[-3].minor.yy92, &yymsp[-4].minor.yy412);
    }
    yymsp[-4].minor.yy412.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3338 "parse.c"
        break;
      case 187: /* expr ::= LP select RP */
#line 1165 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy412,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy412.pExpr, yymsp[-1].minor.yy329);
  }
#line 3347 "parse.c"
        break;
      case 188: /* expr ::= expr in_op LP select RP */
#line 1170 "parse.y"
{
    yymsp[-4].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy412.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy412.pExpr, yymsp[-1].minor.yy329);
    exprNot(pParse, yymsp[-3].minor.yy92, &yymsp[-4].minor.yy412);
    yymsp[-4].minor.yy412.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3357 "parse.c"
        break;
      case 189: /* expr ::= expr in_op nm paren_exprlist */
#line 1176 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy93 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy93);
    yymsp[-3].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy412.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-3].minor.yy412.pExpr, pSelect);
    exprNot(pParse, yymsp[-2].minor.yy92, &yymsp[-3].minor.yy412);
    yymsp[-3].minor.yy412.zEnd = &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
#line 3370 "parse.c"
        break;
      case 190: /* expr ::= EXISTS LP select RP */
#line 1185 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy412,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy329);
  }
#line 3380 "parse.c"
        break;
      case 191: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1194 "parse.y"
{
  spanSet(&yymsp[-4].minor.yy412,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-C*/
  yymsp[-4].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy112, 0);
  if( yymsp[-4].minor.yy412.pExpr ){
    yymsp[-4].minor.yy412.pExpr->x.pList = yymsp[-1].minor.yy112 ? sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy93,yymsp[-1].minor.yy112) : yymsp[-2].minor.yy93;
    sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy412.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy93);
    sqlite3ExprDelete(pParse->db, yymsp[-1].minor.yy112);
  }
}
#line 3395 "parse.c"
        break;
      case 192: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1207 "parse.y"
{
  yymsp[-4].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy93, yymsp[-2].minor.yy412.pExpr);
  yymsp[-4].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy93, yymsp[0].minor.yy412.pExpr);
}
#line 3403 "parse.c"
        break;
      case 193: /* case_exprlist ::= WHEN expr THEN expr */
#line 1211 "parse.y"
{
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy412.pExpr);
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy93, yymsp[0].minor.yy412.pExpr);
}
#line 3411 "parse.c"
        break;
      case 196: /* case_operand ::= expr */
#line 1221 "parse.y"
{yymsp[0].minor.yy112 = yymsp[0].minor.yy412.pExpr; /*A-overwrites-X*/}
#line 3416 "parse.c"
        break;
      case 199: /* nexprlist ::= nexprlist COMMA expr */
#line 1232 "parse.y"
{yymsp[-2].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy93,yymsp[0].minor.yy412.pExpr);}
#line 3421 "parse.c"
        break;
      case 200: /* nexprlist ::= expr */
#line 1234 "parse.y"
{yymsp[0].minor.yy93 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy412.pExpr); /*A-overwrites-Y*/}
#line 3426 "parse.c"
        break;
      case 202: /* paren_exprlist ::= LP exprlist RP */
      case 207: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==207);
#line 1242 "parse.y"
{yymsp[-2].minor.yy93 = yymsp[-1].minor.yy93;}
#line 3432 "parse.c"
        break;
      case 203: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1249 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0), yymsp[-2].minor.yy93, yymsp[-9].minor.yy92,
                      &yymsp[-10].minor.yy0, yymsp[0].minor.yy112, SQLITE_SO_ASC, yymsp[-7].minor.yy92, SQLITE_IDXTYPE_APPDEF);
}
#line 3441 "parse.c"
        break;
      case 204: /* uniqueflag ::= UNIQUE */
      case 244: /* raisetype ::= ABORT */ yytestcase(yyruleno==244);
#line 1256 "parse.y"
{yymsp[0].minor.yy92 = OE_Abort;}
#line 3447 "parse.c"
        break;
      case 205: /* uniqueflag ::= */
#line 1257 "parse.y"
{yymsp[1].minor.yy92 = OE_None;}
#line 3452 "parse.c"
        break;
      case 208: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1300 "parse.y"
{
  yymsp[-4].minor.yy93 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy93, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy92, yymsp[0].minor.yy92);
}
#line 3459 "parse.c"
        break;
      case 209: /* eidlist ::= nm collate sortorder */
#line 1303 "parse.y"
{
  yymsp[-2].minor.yy93 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy92, yymsp[0].minor.yy92); /*A-overwrites-Y*/
}
#line 3466 "parse.c"
        break;
      case 212: /* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1314 "parse.y"
{
    sqlite3DropIndex(pParse, yymsp[-2].minor.yy167, &yymsp[0].minor.yy0, yymsp[-3].minor.yy92);
}
#line 3473 "parse.c"
        break;
      case 213: /* cmd ::= PRAGMA nm */
#line 1321 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[0].minor.yy0,0,0,0,0);
}
#line 3480 "parse.c"
        break;
      case 214: /* cmd ::= PRAGMA nm EQ nmnum */
#line 1324 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,0);
}
#line 3487 "parse.c"
        break;
      case 215: /* cmd ::= PRAGMA nm LP nmnum RP */
#line 1327 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,0);
}
#line 3494 "parse.c"
        break;
      case 216: /* cmd ::= PRAGMA nm EQ minus_num */
#line 1330 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,1);
}
#line 3501 "parse.c"
        break;
      case 217: /* cmd ::= PRAGMA nm LP minus_num RP */
#line 1333 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,1);
}
#line 3508 "parse.c"
        break;
      case 218: /* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1336 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,0,&yymsp[0].minor.yy0,&yymsp[-2].minor.yy0,0);
}
#line 3515 "parse.c"
        break;
      case 221: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1356 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy177, &all);
}
#line 3525 "parse.c"
        break;
      case 222: /* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1365 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy92, yymsp[-4].minor.yy270.a, yymsp[-4].minor.yy270.b, yymsp[-2].minor.yy167, yymsp[0].minor.yy112, yymsp[-7].minor.yy92);
  yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0; /*yymsp[-8].minor.yy0-overwrites-T*/
}
#line 3533 "parse.c"
        break;
      case 223: /* trigger_time ::= BEFORE */
#line 1371 "parse.y"
{ yymsp[0].minor.yy92 = TK_BEFORE; }
#line 3538 "parse.c"
        break;
      case 224: /* trigger_time ::= AFTER */
#line 1372 "parse.y"
{ yymsp[0].minor.yy92 = TK_AFTER;  }
#line 3543 "parse.c"
        break;
      case 225: /* trigger_time ::= INSTEAD OF */
#line 1373 "parse.y"
{ yymsp[-1].minor.yy92 = TK_INSTEAD;}
#line 3548 "parse.c"
        break;
      case 226: /* trigger_time ::= */
#line 1374 "parse.y"
{ yymsp[1].minor.yy92 = TK_BEFORE; }
#line 3553 "parse.c"
        break;
      case 227: /* trigger_event ::= DELETE|INSERT */
      case 228: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==228);
#line 1378 "parse.y"
{yymsp[0].minor.yy270.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy270.b = 0;}
#line 3559 "parse.c"
        break;
      case 229: /* trigger_event ::= UPDATE OF idlist */
#line 1380 "parse.y"
{yymsp[-2].minor.yy270.a = TK_UPDATE; yymsp[-2].minor.yy270.b = yymsp[0].minor.yy330;}
#line 3564 "parse.c"
        break;
      case 230: /* when_clause ::= */
#line 1387 "parse.y"
{ yymsp[1].minor.yy112 = 0; }
#line 3569 "parse.c"
        break;
      case 231: /* when_clause ::= WHEN expr */
#line 1388 "parse.y"
{ yymsp[-1].minor.yy112 = yymsp[0].minor.yy412.pExpr; }
#line 3574 "parse.c"
        break;
      case 232: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1392 "parse.y"
{
  assert( yymsp[-2].minor.yy177!=0 );
  yymsp[-2].minor.yy177->pLast->pNext = yymsp[-1].minor.yy177;
  yymsp[-2].minor.yy177->pLast = yymsp[-1].minor.yy177;
}
#line 3583 "parse.c"
        break;
      case 233: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1397 "parse.y"
{ 
  assert( yymsp[-1].minor.yy177!=0 );
  yymsp[-1].minor.yy177->pLast = yymsp[-1].minor.yy177;
}
#line 3591 "parse.c"
        break;
      case 234: /* trnm ::= nm DOT nm */
#line 1408 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3601 "parse.c"
        break;
      case 235: /* tridxby ::= INDEXED BY nm */
#line 1420 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3610 "parse.c"
        break;
      case 236: /* tridxby ::= NOT INDEXED */
#line 1425 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3619 "parse.c"
        break;
      case 237: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1438 "parse.y"
{yymsp[-6].minor.yy177 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy93, yymsp[0].minor.yy112, yymsp[-5].minor.yy92);}
#line 3624 "parse.c"
        break;
      case 238: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1442 "parse.y"
{yymsp[-4].minor.yy177 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy330, yymsp[0].minor.yy329, yymsp[-4].minor.yy92);/*A-overwrites-R*/}
#line 3629 "parse.c"
        break;
      case 239: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1446 "parse.y"
{yymsp[-4].minor.yy177 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy112);}
#line 3634 "parse.c"
        break;
      case 240: /* trigger_cmd ::= select */
#line 1450 "parse.y"
{yymsp[0].minor.yy177 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy329); /*A-overwrites-X*/}
#line 3639 "parse.c"
        break;
      case 241: /* expr ::= RAISE LP IGNORE RP */
#line 1453 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy412,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy412.pExpr ){
    yymsp[-3].minor.yy412.pExpr->affinity = OE_Ignore;
  }
}
#line 3650 "parse.c"
        break;
      case 242: /* expr ::= RAISE LP raisetype COMMA STRING RP */
#line 1460 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy412,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy412.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy412.pExpr ) {
    yymsp[-5].minor.yy412.pExpr->affinity = (char)yymsp[-3].minor.yy92;
  }
}
#line 3661 "parse.c"
        break;
      case 243: /* raisetype ::= ROLLBACK */
#line 1470 "parse.y"
{yymsp[0].minor.yy92 = OE_Rollback;}
#line 3666 "parse.c"
        break;
      case 245: /* raisetype ::= FAIL */
#line 1472 "parse.y"
{yymsp[0].minor.yy92 = OE_Fail;}
#line 3671 "parse.c"
        break;
      case 246: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1477 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy167,yymsp[-1].minor.yy92);
}
#line 3678 "parse.c"
        break;
      case 247: /* cmd ::= REINDEX */
#line 1484 "parse.y"
{sqlite3Reindex(pParse, 0, 0);}
#line 3683 "parse.c"
        break;
      case 248: /* cmd ::= REINDEX nm */
#line 1485 "parse.y"
{sqlite3Reindex(pParse, &yymsp[0].minor.yy0, 0);}
#line 3688 "parse.c"
        break;
      case 249: /* cmd ::= REINDEX nm ON nm */
#line 1486 "parse.y"
{sqlite3Reindex(pParse, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);}
#line 3693 "parse.c"
        break;
      case 250: /* cmd ::= ANALYZE */
#line 1491 "parse.y"
{sqlite3Analyze(pParse, 0);}
#line 3698 "parse.c"
        break;
      case 251: /* cmd ::= ANALYZE nm */
#line 1492 "parse.y"
{sqlite3Analyze(pParse, &yymsp[0].minor.yy0);}
#line 3703 "parse.c"
        break;
      case 252: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1497 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy167,&yymsp[0].minor.yy0);
}
#line 3710 "parse.c"
        break;
      case 253: /* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist */
#line 1501 "parse.y"
{
  yymsp[-1].minor.yy0.n = (int)(pParse->sLastToken.z-yymsp[-1].minor.yy0.z) + pParse->sLastToken.n;
  sqlite3AlterFinishAddColumn(pParse, &yymsp[-1].minor.yy0);
}
#line 3718 "parse.c"
        break;
      case 254: /* add_column_fullname ::= fullname */
#line 1505 "parse.y"
{
  disableLookaside(pParse);
  sqlite3AlterBeginAddColumn(pParse, yymsp[0].minor.yy167);
}
#line 3726 "parse.c"
        break;
      case 255: /* with ::= */
#line 1519 "parse.y"
{yymsp[1].minor.yy121 = 0;}
#line 3731 "parse.c"
        break;
      case 256: /* with ::= WITH wqlist */
#line 1521 "parse.y"
{ yymsp[-1].minor.yy121 = yymsp[0].minor.yy121; }
#line 3736 "parse.c"
        break;
      case 257: /* with ::= WITH RECURSIVE wqlist */
#line 1522 "parse.y"
{ yymsp[-2].minor.yy121 = yymsp[0].minor.yy121; }
#line 3741 "parse.c"
        break;
      case 258: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1524 "parse.y"
{
  yymsp[-5].minor.yy121 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy93, yymsp[-1].minor.yy329); /*A-overwrites-X*/
}
#line 3748 "parse.c"
        break;
      case 259: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1527 "parse.y"
{
  yymsp[-7].minor.yy121 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy121, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy93, yymsp[-1].minor.yy329);
}
#line 3755 "parse.c"
        break;
      default:
      /* (260) input ::= ecmd */ yytestcase(yyruleno==260);
      /* (261) explain ::= */ yytestcase(yyruleno==261);
      /* (262) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno!=262);
      /* (263) trans_opt ::= */ yytestcase(yyruleno==263);
      /* (264) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==264);
      /* (265) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==265);
      /* (266) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==266);
      /* (267) savepoint_opt ::= */ yytestcase(yyruleno==267);
      /* (268) cmd ::= create_table create_table_args */ yytestcase(yyruleno==268);
      /* (269) columnlist ::= columnlist COMMA columnname carglist */ yytestcase(yyruleno==269);
      /* (270) columnlist ::= columnname carglist */ yytestcase(yyruleno==270);
      /* (271) typetoken ::= typename */ yytestcase(yyruleno==271);
      /* (272) typename ::= ID|STRING */ yytestcase(yyruleno==272);
      /* (273) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=273);
      /* (274) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=274);
      /* (275) carglist ::= carglist ccons */ yytestcase(yyruleno==275);
      /* (276) carglist ::= */ yytestcase(yyruleno==276);
      /* (277) ccons ::= NULL onconf */ yytestcase(yyruleno==277);
      /* (278) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==278);
      /* (279) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==279);
      /* (280) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=280);
      /* (281) tconscomma ::= */ yytestcase(yyruleno==281);
      /* (282) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=282);
      /* (283) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=283);
      /* (284) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=284);
      /* (285) oneselect ::= values */ yytestcase(yyruleno==285);
      /* (286) sclp ::= selcollist COMMA */ yytestcase(yyruleno==286);
      /* (287) as ::= ID|STRING */ yytestcase(yyruleno==287);
      /* (288) join_nm ::= ID|INDEXED */ yytestcase(yyruleno==288);
      /* (289) join_nm ::= JOIN_KW */ yytestcase(yyruleno==289);
      /* (290) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=290);
      /* (291) exprlist ::= nexprlist */ yytestcase(yyruleno==291);
      /* (292) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=292);
      /* (293) nmnum ::= STRING */ yytestcase(yyruleno==293);
      /* (294) nmnum ::= nm */ yytestcase(yyruleno==294);
      /* (295) nmnum ::= ON */ yytestcase(yyruleno==295);
      /* (296) nmnum ::= DELETE */ yytestcase(yyruleno==296);
      /* (297) nmnum ::= DEFAULT */ yytestcase(yyruleno==297);
      /* (298) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno==298);
      /* (299) foreach_clause ::= */ yytestcase(yyruleno==299);
      /* (300) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==300);
      /* (301) trnm ::= nm */ yytestcase(yyruleno==301);
      /* (302) tridxby ::= */ yytestcase(yyruleno==302);
      /* (303) kwcolumn_opt ::= */ yytestcase(yyruleno==303);
      /* (304) kwcolumn_opt ::= COLUMNKW */ yytestcase(yyruleno==304);
        break;
/********** End reduce actions ************************************************/
  };
  assert( yyruleno<sizeof(yyRuleInfo)/sizeof(yyRuleInfo[0]) );
  yygoto = yyRuleInfo[yyruleno].lhs;
  yysize = yyRuleInfo[yyruleno].nrhs;
  yyact = yy_find_reduce_action(yymsp[-yysize].stateno,(YYCODETYPE)yygoto);
  if( yyact <= YY_MAX_SHIFTREDUCE ){
    if( yyact>YY_MAX_SHIFT ){
      yyact += YY_MIN_REDUCE - YY_MIN_SHIFTREDUCE;
    }
    yymsp -= yysize-1;
    yypParser->yytos = yymsp;
    yymsp->stateno = (YYACTIONTYPE)yyact;
    yymsp->major = (YYCODETYPE)yygoto;
    yyTraceShift(yypParser, yyact);
  }else{
    assert( yyact == YY_ACCEPT_ACTION );
    yypParser->yytos -= yysize;
    yy_accept(yypParser);
  }
}

/*
** The following code executes when the parse fails
*/
#ifndef YYNOERRORRECOVERY
static void yy_parse_failed(
  yyParser *yypParser           /* The parser */
){
  sqlite3ParserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sFail!\n",yyTracePrompt);
  }
#endif
  while( yypParser->yytos>yypParser->yystack ) yy_pop_parser_stack(yypParser);
  /* Here code is inserted which will be executed whenever the
  ** parser fails */
/************ Begin %parse_failure code ***************************************/
/************ End %parse_failure code *****************************************/
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}
#endif /* YYNOERRORRECOVERY */

/*
** The following code executes when a syntax error first occurs.
*/
static void yy_syntax_error(
  yyParser *yypParser,           /* The parser */
  int yymajor,                   /* The major type of the error token */
  sqlite3ParserTOKENTYPE yyminor         /* The minor type of the error token */
){
  sqlite3ParserARG_FETCH;
#define TOKEN yyminor
/************ Begin %syntax_error code ****************************************/
#line 32 "parse.y"

  UNUSED_PARAMETER(yymajor);  /* Silence some compiler warnings */
  assert( TOKEN.z[0] );  /* The tokenizer always gives us a token */
  if (yypParser->is_fallback_failed && TOKEN.isReserved) {
    sqlite3ErrorMsg(pParse, "keyword \"%T\" is reserved", &TOKEN);
  } else {
    sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &TOKEN);
  }
#line 3868 "parse.c"
/************ End %syntax_error code ******************************************/
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/*
** The following is executed when the parser accepts
*/
static void yy_accept(
  yyParser *yypParser           /* The parser */
){
  sqlite3ParserARG_FETCH;
#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sAccept!\n",yyTracePrompt);
  }
#endif
#ifndef YYNOERRORRECOVERY
  yypParser->yyerrcnt = -1;
#endif
  assert( yypParser->yytos==yypParser->yystack );
  /* Here code is inserted which will be executed whenever the
  ** parser accepts */
/*********** Begin %parse_accept code *****************************************/
/*********** End %parse_accept code *******************************************/
  sqlite3ParserARG_STORE; /* Suppress warning about unused %extra_argument variable */
}

/* The main parser program.
** The first argument is a pointer to a structure obtained from
** "sqlite3ParserAlloc" which describes the current state of the parser.
** The second argument is the major token number.  The third is
** the minor token.  The fourth optional argument is whatever the
** user wants (and specified in the grammar) and is available for
** use by the action routines.
**
** Inputs:
** <ul>
** <li> A pointer to the parser (an opaque structure.)
** <li> The major token number.
** <li> The minor token number.
** <li> An option argument of a grammar-specified type.
** </ul>
**
** Outputs:
** None.
*/
void sqlite3Parser(
  void *yyp,                   /* The parser */
  int yymajor,                 /* The major token code number */
  sqlite3ParserTOKENTYPE yyminor       /* The value for the token */
  sqlite3ParserARG_PDECL               /* Optional %extra_argument parameter */
){
  YYMINORTYPE yyminorunion;
  unsigned int yyact;   /* The parser action. */
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  int yyendofinput;     /* True if we are at the end of input */
#endif
#ifdef YYERRORSYMBOL
  int yyerrorhit = 0;   /* True if yymajor has invoked an error */
#endif
  yyParser *yypParser;  /* The parser */

  yypParser = (yyParser*)yyp;
  assert( yypParser->yytos!=0 );
#if !defined(YYERRORSYMBOL) && !defined(YYNOERRORRECOVERY)
  yyendofinput = (yymajor==0);
#endif
  sqlite3ParserARG_STORE;

#ifndef NDEBUG
  if( yyTraceFILE ){
    fprintf(yyTraceFILE,"%sInput '%s'\n",yyTracePrompt,yyTokenName[yymajor]);
  }
#endif

  do{
    yyact = yy_find_shift_action(yypParser,(YYCODETYPE)yymajor);
    if( yyact <= YY_MAX_SHIFTREDUCE ){
      yy_shift(yypParser,yyact,yymajor,yyminor);
#ifndef YYNOERRORRECOVERY
      yypParser->yyerrcnt--;
#endif
      yymajor = YYNOCODE;
    }else if( yyact <= YY_MAX_REDUCE ){
      yy_reduce(yypParser,yyact-YY_MIN_REDUCE);
    }else{
      assert( yyact == YY_ERROR_ACTION );
      yyminorunion.yy0 = yyminor;
#ifdef YYERRORSYMBOL
      int yymx;
#endif
#ifndef NDEBUG
      if( yyTraceFILE ){
        fprintf(yyTraceFILE,"%sSyntax Error!\n",yyTracePrompt);
      }
#endif
#ifdef YYERRORSYMBOL
      /* A syntax error has occurred.
      ** The response to an error depends upon whether or not the
      ** grammar defines an error token "ERROR".  
      **
      ** This is what we do if the grammar does define ERROR:
      **
      **  * Call the %syntax_error function.
      **
      **  * Begin popping the stack until we enter a state where
      **    it is legal to shift the error symbol, then shift
      **    the error symbol.
      **
      **  * Set the error count to three.
      **
      **  * Begin accepting and shifting new tokens.  No new error
      **    processing will occur until three tokens have been
      **    shifted successfully.
      **
      */
      if( yypParser->yyerrcnt<0 ){
        yy_syntax_error(yypParser,yymajor,yyminor);
      }
      yymx = yypParser->yytos->major;
      if( yymx==YYERRORSYMBOL || yyerrorhit ){
#ifndef NDEBUG
        if( yyTraceFILE ){
          fprintf(yyTraceFILE,"%sDiscard input token %s\n",
             yyTracePrompt,yyTokenName[yymajor]);
        }
#endif
        yy_destructor(yypParser, (YYCODETYPE)yymajor, &yyminorunion);
        yymajor = YYNOCODE;
      }else{
        while( yypParser->yytos >= yypParser->yystack
            && yymx != YYERRORSYMBOL
            && (yyact = yy_find_reduce_action(
                        yypParser->yytos->stateno,
                        YYERRORSYMBOL)) >= YY_MIN_REDUCE
        ){
          yy_pop_parser_stack(yypParser);
        }
        if( yypParser->yytos < yypParser->yystack || yymajor==0 ){
          yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
          yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
          yypParser->yyerrcnt = -1;
#endif
          yymajor = YYNOCODE;
        }else if( yymx!=YYERRORSYMBOL ){
          yy_shift(yypParser,yyact,YYERRORSYMBOL,yyminor);
        }
      }
      yypParser->yyerrcnt = 3;
      yyerrorhit = 1;
#elif defined(YYNOERRORRECOVERY)
      /* If the YYNOERRORRECOVERY macro is defined, then do not attempt to
      ** do any kind of error recovery.  Instead, simply invoke the syntax
      ** error routine and continue going as if nothing had happened.
      **
      ** Applications can set this macro (for example inside %include) if
      ** they intend to abandon the parse upon the first syntax error seen.
      */
      yy_syntax_error(yypParser,yymajor, yyminor);
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      yymajor = YYNOCODE;
      
#else  /* YYERRORSYMBOL is not defined */
      /* This is what we do if the grammar does not define ERROR:
      **
      **  * Report an error message, and throw away the input token.
      **
      **  * If the input token is $, then fail the parse.
      **
      ** As before, subsequent error messages are suppressed until
      ** three input tokens have been successfully shifted.
      */
      if( yypParser->yyerrcnt<=0 ){
        yy_syntax_error(yypParser,yymajor, yyminor);
      }
      yypParser->yyerrcnt = 3;
      yy_destructor(yypParser,(YYCODETYPE)yymajor,&yyminorunion);
      if( yyendofinput ){
        yy_parse_failed(yypParser);
#ifndef YYNOERRORRECOVERY
        yypParser->yyerrcnt = -1;
#endif
      }
      yymajor = YYNOCODE;
#endif
    }
  }while( yymajor!=YYNOCODE && yypParser->yytos>yypParser->yystack );
#ifndef NDEBUG
  if( yyTraceFILE ){
    yyStackEntry *i;
    char cDiv = '[';
    fprintf(yyTraceFILE,"%sReturn. Stack=",yyTracePrompt);
    for(i=&yypParser->yystack[1]; i<=yypParser->yytos; i++){
      fprintf(yyTraceFILE,"%c%s", cDiv, yyTokenName[i->major]);
      cDiv = ' ';
    }
    fprintf(yyTraceFILE,"]\n");
  }
#endif
  return;
}
