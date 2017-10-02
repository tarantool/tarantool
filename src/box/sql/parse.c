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
#line 842 "parse.y"

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
      if( sqlite3Isquote(p->u.zToken[0]) ){
        if( p->u.zToken[0]=='"' ) p->flags |= EP_DblQuoted;
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
#line 959 "parse.y"

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
#line 1033 "parse.y"

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
#line 1050 "parse.y"

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
#line 1078 "parse.y"

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
#line 1283 "parse.y"

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
#line 232 "parse.c"
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
#define YYNSTATE             421
#define YYNRULE              306
#define YY_MAX_SHIFT         420
#define YY_MIN_SHIFTREDUCE   619
#define YY_MAX_SHIFTREDUCE   924
#define YY_MIN_REDUCE        925
#define YY_MAX_REDUCE        1230
#define YY_ERROR_ACTION      1231
#define YY_ACCEPT_ACTION     1232
#define YY_NO_ACTION         1233
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
#define YY_ACTTAB_COUNT (1438)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    91,   92,  294,   82,  789,  789,  801,  804,  793,  793,
 /*    10 */    89,   89,   90,   90,   90,   90,  316,   88,   88,   88,
 /*    20 */    88,   87,   87,   86,   86,   86,   85,  316,   90,   90,
 /*    30 */    90,   90,   83,   88,   88,   88,   88,   87,   87,   86,
 /*    40 */    86,   86,   85,  316,  212,  641,  641,  907,   90,   90,
 /*    50 */    90,   90,  124,   88,   88,   88,   88,   87,   87,   86,
 /*    60 */    86,   86,   85,  316,   87,   87,   86,   86,   86,   85,
 /*    70 */   316,  907,   86,   86,   86,   85,  316,   91,   92,  294,
 /*    80 */    82,  789,  789,  801,  804,  793,  793,   89,   89,   90,
 /*    90 */    90,   90,   90,  647,   88,   88,   88,   88,   87,   87,
 /*   100 */    86,   86,   86,   85,  316,   91,   92,  294,   82,  789,
 /*   110 */   789,  801,  804,  793,  793,   89,   89,   90,   90,   90,
 /*   120 */    90,  650,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   130 */    86,   85,  316,  314,  313,   91,   92,  294,   82,  789,
 /*   140 */   789,  801,  804,  793,  793,   89,   89,   90,   90,   90,
 /*   150 */    90,   67,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   160 */    86,   85,  316,  709, 1232,  420,    3,  272,   93,   84,
 /*   170 */    81,  178,  413,  413,  413,   84,   81,  178,  250,  908,
 /*   180 */   908,  738,  739,  649,   91,   92,  294,   82,  789,  789,
 /*   190 */   801,  804,  793,  793,   89,   89,   90,   90,   90,   90,
 /*   200 */   303,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   210 */    85,  316,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   220 */    86,   85,  316,  918,  779,  918,  772,  655,  271,  766,
 /*   230 */   841,  909,  725,   91,   92,  294,   82,  789,  789,  801,
 /*   240 */   804,  793,  793,   89,   89,   90,   90,   90,   90,  764,
 /*   250 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   260 */   316,  171,  760,  143,  419,  419,  771,  771,  773,  281,
 /*   270 */   224,  737,  121,  290,  736,  899,  648,  696,  243,  341,
 /*   280 */   242,  350,   91,   92,  294,   82,  789,  789,  801,  804,
 /*   290 */   793,  793,   89,   89,   90,   90,   90,   90,  666,   88,
 /*   300 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  316,
 /*   310 */    22,  203,  760,  335,  367,  364,  363,  409,   84,   81,
 /*   320 */   178,   84,   81,  178,   64,  780,  362,  124,  243,  341,
 /*   330 */   242,   91,   92,  294,   82,  789,  789,  801,  804,  793,
 /*   340 */   793,   89,   89,   90,   90,   90,   90,  863,   88,   88,
 /*   350 */    88,   88,   87,   87,   86,   86,   86,   85,  316,  683,
 /*   360 */   662,  716,  864,  315,  315,  315,  192,    5,  683,  212,
 /*   370 */   865,  689,  907,  376,  765,  701,  701,  124,  690,  665,
 /*   380 */    91,   92,  294,   82,  789,  789,  801,  804,  793,  793,
 /*   390 */    89,   89,   90,   90,   90,   90,  907,   88,   88,   88,
 /*   400 */    88,   87,   87,   86,   86,   86,   85,  316,   91,   92,
 /*   410 */   294,   82,  789,  789,  801,  804,  793,  793,   89,   89,
 /*   420 */    90,   90,   90,   90,  664,   88,   88,   88,   88,   87,
 /*   430 */    87,   86,   86,   86,   85,  316,   91,   92,  294,   82,
 /*   440 */   789,  789,  801,  804,  793,  793,   89,   89,   90,   90,
 /*   450 */    90,   90,  211,   88,   88,   88,   88,   87,   87,   86,
 /*   460 */    86,   86,   85,  316,   91,   92,  294,   82,  789,  789,
 /*   470 */   801,  804,  793,  793,   89,   89,   90,   90,   90,   90,
 /*   480 */   147,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   490 */    85,  316,  847,  847,  336, 1181, 1181,  387,   70,  294,
 /*   500 */    82,  789,  789,  801,  804,  793,  793,   89,   89,   90,
 /*   510 */    90,   90,   90,  350,   88,   88,   88,   88,   87,   87,
 /*   520 */    86,   86,   86,   85,  316,  166,   73,  790,  790,  802,
 /*   530 */   805,  124,   91,   80,  294,   82,  789,  789,  801,  804,
 /*   540 */   793,  793,   89,   89,   90,   90,   90,   90,  398,   88,
 /*   550 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  316,
 /*   560 */    92,  294,   82,  789,  789,  801,  804,  793,  793,   89,
 /*   570 */    89,   90,   90,   90,   90,   78,   88,   88,   88,   88,
 /*   580 */    87,   87,   86,   86,   86,   85,  316,  179,  179,  641,
 /*   590 */   641,  698,  308,  416,   75,   76,  339,  794,  241,  380,
 /*   600 */   288,   77,  380,  286,  285,  284,  226,  282,  416,   78,
 /*   610 */   633,   48,   48,  416,  411,    2, 1124,  305,  620,  319,
 /*   620 */   317,  317,  844,  760,  843,  860,   48,   48,   75,   76,
 /*   630 */   334,   10,   10,  221,  377,   77,  169,  400,  184,  243,
 /*   640 */   331,  231,  779,  346,  772,  302,  309,  766,  411,    2,
 /*   650 */   344,  416,  221,  377,  317,  317,   85,  316,  110,  295,
 /*   660 */   394,  388,  222,  157,  260,  370,  255,  369,  207,   48,
 /*   670 */    48,  400,  768,  145,  139,  253,  779,  262,  772,  641,
 /*   680 */   641,  766,  342,  892,  771,  771,  773,  774,  414,   18,
 /*   690 */   415,  416,  186,  110,  895,   23,  688,  688,  182,  323,
 /*   700 */   340,  124,  657,  124,  393,  395,  768,  892,  416,   47,
 /*   710 */    47,   74,  416,   72,   78,  187,  314,  313,  771,  771,
 /*   720 */   773,  774,  414,   18,  179,  179,   48,   48,  686,  686,
 /*   730 */    10,   10,  328,   75,   76,  710,  380,  124,  344,  722,
 /*   740 */    77,  326,  641,  641,  304,   78,  159,  158,  908,  908,
 /*   750 */   329,  416,  271,  411,    2,  711,  326,  325,  205,  317,
 /*   760 */   317,  393,  378,  270,   75,   76,  913,   95,  917,   48,
 /*   770 */    48,   77,  651,  651,  381,  915,  400,  916,  230,  641,
 /*   780 */   641,  779,  140,  772,  411,    2,  766,  641,  641,  124,
 /*   790 */   317,  317,    9,    9,  779,  355,  772,  416,  352,  766,
 /*   800 */   909,  724,  757,  193,  393,  383,  918,  400,  918,  143,
 /*   810 */   722,  768,  779,  416,  772,   10,   10,  766,  326,  289,
 /*   820 */   359,  416,  205,  771,  771,  773,  774,  414,   18,  202,
 /*   830 */   374,   48,   48,  416,  296,  110,  771,  771,  773,   30,
 /*   840 */    30,  405,  768,  641,  641,  264,  416,  764,  860,  188,
 /*   850 */    68,   48,   48,  675,  771,  771,  773,  774,  414,   18,
 /*   860 */   722,  272,  248,  110,   10,   10,  393,  392,  203,   75,
 /*   870 */    76,  367,  364,  363,  375,  372,   77,  327,  390,  722,
 /*   880 */   266,  110,  382,  362,  236,  174,  393,  373,  723,  411,
 /*   890 */     2,  416,  834,  836,  312,  317,  317,  143,  296,  189,
 /*   900 */   824,  722,  165,  253,  858,  342,  416,  293,  676,   34,
 /*   910 */    34,  299,  400,  247,  722,  110,  722,  779,  866,  772,
 /*   920 */   764,  863,  766,  351,   35,   35,  204,  669,  204,  350,
 /*   930 */   164,  176,  416,  368,  722,  235,  864,  110,  670,  416,
 /*   940 */   332,  167,  723,  706,  865,  399,  239,  768,  705,  694,
 /*   950 */    36,   36,  416,  695,  834,  691,  306,   37,   37,  771,
 /*   960 */   771,  773,  774,  414,   18,  269,  234,  416,  233,  416,
 /*   970 */    38,   38,  416,  238,  416,  658,  416,  637,  416,  163,
 /*   980 */   162,  161,  110,  416,  300,   26,   26,   27,   27,  416,
 /*   990 */    29,   29,   39,   39,   40,   40,   41,   41,  416,  658,
 /*  1000 */   416,   11,   11,  416,  384,  416,  348,   42,   42,  416,
 /*  1010 */   110,  416,  240,  416,  195,  416,   97,   97,   43,   43,
 /*  1020 */   416,   44,   44,   31,   31,  872,  220,   45,   45,   46,
 /*  1030 */    46,   32,   32,  113,  113,  416,   54,  416,  114,  114,
 /*  1040 */   148,  416,  402,  416,  722,  212,  416,  871,  907,  416,
 /*  1050 */   385,  416,  349,  115,  115,   52,   52,  764,  416,   33,
 /*  1060 */    33,   98,   98,  416,   49,   49,  764,   99,   99,  100,
 /*  1070 */   100,  416,  907,  416,  723,  110,   96,   96,  416,  752,
 /*  1080 */    24,  112,  112,  322,  416,  641,  641,  416,  259,  109,
 /*  1090 */   109,  104,  104,  416,  640,  297,  103,  103,  416,  258,
 /*  1100 */   416,  406,  101,  101,  416,  102,  102,  416,    1,   66,
 /*  1110 */   107,   51,   51,  641,  641,  645,   53,   53,   50,   50,
 /*  1120 */   906,  301,   25,   25,  410,   28,   28,  167,  723,   20,
 /*  1130 */   310,  320,  412,  297,  110, 1204,  177,  176,  678,  645,
 /*  1140 */   738,  739,  660,  706,  111,  180,  151,  762,  705,  343,
 /*  1150 */   210,  345,  210,  244,  210,  360,   66,  110,  216,  251,
 /*  1160 */    19,  831,   66,  668,  667,  703,  732,  827,   69,  210,
 /*  1170 */   216,  840,  155,  840,  775,  839,  643,  839,    7,  106,
 /*  1180 */   869,  324,  838,  298,  330,  831,  232,  832,  170,  855,
 /*  1190 */   857,  347,  365,  854,  353,  354,  246,  632,  775,  249,
 /*  1200 */   947,  679,  160,  662,  401,  663,  257,  254,  268,  730,
 /*  1210 */   763,  712,  829,  273,  137,  274,  770,  279,  156,  828,
 /*  1220 */   646,  639,  888,  630,  629,  631,  885,  924,  125,  126,
 /*  1230 */   119,  749,  333,  842,   64,  338,   55,  128,  237,  358,
 /*  1240 */   191,  198,  149,  146,  199,  200,  371,  307,  660,  386,
 /*  1250 */   130,   63,  720,  682,    6,  131,  132,  356,   71,  213,
 /*  1260 */   133,  681,  311,  141,  759,  673,   94,  680,  291,   65,
 /*  1270 */   391,  389,  292,  654,  859,   21,  223,  653,  823,  886,
 /*  1280 */   256,  652,  228,  622,  418,  672,  897,  625,  318,  287,
 /*  1290 */   225,  229,  636,  627,  227,  417,  626,  181,  623,  404,
 /*  1300 */   321,  108,  183,  408,  837,  835,  116,  117,  185,  758,
 /*  1310 */   127,  122,  692,  129,  118,  210,  190,  845,  134,  920,
 /*  1320 */   853,  337,  135,  136,  105,  206,  138,   56,   57,  261,
 /*  1330 */   278,  721,  263,  702,  719,  265,  275,  718,  267,  276,
 /*  1340 */   277,  809,   58,   59,  856,  196,  194,  852,  123,    8,
 /*  1350 */    12,  150,  197,  245,  214,  215,  635,  357,  258,  201,
 /*  1360 */   142,  361,  252,   60,   13,   14,  366,  208,   61,  120,
 /*  1370 */   778,  777,  807,  700,  172,  671,   62,   15,  726,  704,
 /*  1380 */     4,  731,  173,  379,  175,  144,  209,   16,   69,   17,
 /*  1390 */    66,  822,  808,  806,  811,  862,  619,  861,  168,  397,
 /*  1400 */   878,  152,  403,  218,  153,  396,  217,  879,  154,  407,
 /*  1410 */   810,  776,  644,   79,  283,  280, 1186,  927,  927,  927,
 /*  1420 */   927,  927,  927,  927,  927,  927,  927,  927,  927,  927,
 /*  1430 */   927,  927,  927,  927,  927,  927,  927,  219,
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
 /*   970 */   163,  164,  145,  184,  145,   52,  145,  154,  145,   86,
 /*   980 */    87,   88,  187,  145,  209,  163,  164,  163,  164,  145,
 /*   990 */   163,  164,  163,  164,  163,  164,  163,  164,  145,   76,
 /*  1000 */   145,  163,  164,  145,    7,  145,    7,  163,  164,  145,
 /*  1010 */   187,  145,  231,  145,  230,  145,  163,  164,  163,  164,
 /*  1020 */   145,  163,  164,  163,  164,  145,  190,  163,  164,  163,
 /*  1030 */   164,  163,  164,  163,  164,  145,  200,  145,  163,  164,
 /*  1040 */   188,  145,  154,  145,  145,   49,  145,  145,   52,  145,
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
 /*  1160 */    47,   52,   51,   78,   79,   48,   48,   48,   51,   51,
 /*  1170 */    51,  116,  103,  118,   52,  116,   48,  118,  189,   51,
 /*  1180 */   145,  145,  145,  145,  205,   76,  205,  145,  145,  145,
 /*  1190 */   192,  231,  167,  145,  145,  145,  145,  145,   76,  145,
 /*  1200 */   102,  145,  175,  171,  219,  145,  166,  145,  205,  145,
 /*  1210 */   145,  145,  166,  145,   47,  145,  145,  191,  189,  166,
 /*  1220 */   145,  145,  148,  145,  145,  145,  145,  134,  233,  212,
 /*  1230 */     5,  192,   45,  228,  114,  129,  122,  180,  227,   45,
 /*  1240 */   150,  150,  212,   47,  150,  150,   85,   64,   84,  107,
 /*  1250 */   183,   85,  207,  165,   47,  183,  183,  168,  122,  218,
 /*  1260 */   183,  165,   32,  180,  180,  173,  113,  165,  168,  112,
 /*  1270 */   108,  111,  168,  165,  192,   51,   50,  167,  192,   40,
 /*  1280 */   165,  165,   35,    4,  144,  173,  165,   36,    3,  143,
 /*  1290 */   146,   56,  153,  144,  146,  152,  144,   42,  144,  168,
 /*  1300 */    73,   43,   85,  168,   48,   48,  156,  156,  102,  100,
 /*  1310 */   115,   89,   46,  103,  156,   51,   85,  128,  128,  131,
 /*  1320 */     1,  130,   85,  103,  169,  169,  115,   16,   16,  206,
 /*  1330 */   192,  207,  206,  196,  207,  206,  195,  207,  206,  194,
 /*  1340 */   193,  216,   16,   16,   53,  102,  106,    1,   89,   34,
 /*  1350 */    47,   49,   85,  125,  221,  221,   46,    7,   90,   83,
 /*  1360 */    47,   67,   48,   47,   47,   47,   67,   67,   47,   61,
 /*  1370 */    48,   48,   48,   96,  102,   55,   51,   47,  105,   48,
 /*  1380 */    47,   53,   48,   51,   48,   47,  106,  106,   51,  106,
 /*  1390 */    51,   48,   48,   48,   38,   48,    1,   48,   47,   51,
 /*  1400 */    48,   47,   49,  102,   47,   76,   51,   48,   47,   49,
 /*  1410 */    48,   48,   48,   47,   42,   48,    0,  235,  235,  235,
 /*  1420 */   235,  235,  235,  235,  235,  235,  235,  235,  235,  235,
 /*  1430 */   235,  235,  235,  235,  235,  235,  235,  102,
};
#define YY_SHIFT_USE_DFLT (1438)
#define YY_SHIFT_COUNT    (420)
#define YY_SHIFT_MIN      (-83)
#define YY_SHIFT_MAX      (1416)
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
 /*   110 */   534,   -7,  -16, 1438, 1438, 1438,  585,  585,  585,  308,
 /*   120 */   308,  819,  627,  627,  537,   -7,  654,   -7,   -7,   -7,
 /*   130 */    -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,
 /*   140 */    -7,   -7,   -7,  631,   -7,   -7,   -7,  631,  534,  -83,
 /*   150 */   -83,  -83,  -83,  -83,  -83, 1438, 1438,  720,  150,  150,
 /*   160 */   234,  867,  867,  867,  837,  320,  127,  696,  882,  406,
 /*   170 */   565,  735,  727,  996,  996,  996, 1033, 1023, 1061,  278,
 /*   180 */   847,   -7,   -7,   -7,   -7,   -7,   -7,  210,  997,  997,
 /*   190 */    -7,   -7,  999,  210,   -7,  999,   -7,   -7,   -7,   -7,
 /*   200 */    -7,   -7, 1058,   -7, 1087,   -7,  749,   -7, 1031,   -7,
 /*   210 */    -7,  997,   -7,  589, 1031, 1031,   -7,   -7,   -7,   -7,
 /*   220 */  1069, 1047,   -7,   -7, 1097,   -7,   -7,   -7,   -7, 1093,
 /*   230 */  1167, 1225, 1120, 1187, 1187, 1187, 1187, 1114, 1106, 1194,
 /*   240 */  1120, 1167, 1225, 1225, 1120, 1194, 1196, 1194, 1194, 1196,
 /*   250 */  1161, 1161, 1161, 1183, 1196, 1161, 1164, 1161, 1183, 1161,
 /*   260 */  1161, 1142, 1166, 1142, 1166, 1142, 1166, 1142, 1166, 1207,
 /*   270 */  1136, 1196, 1230, 1230, 1196, 1153, 1162, 1157, 1160, 1120,
 /*   280 */  1226, 1224, 1239, 1239, 1247, 1247, 1247, 1247, 1251, 1438,
 /*   290 */  1438, 1438, 1438, 1438,  518,  841,  893, 1063, 1113, 1099,
 /*   300 */  1101, 1103, 1105, 1107, 1111,  923, 1085,  817, 1009, 1117,
 /*   310 */  1118, 1109, 1119, 1055, 1059, 1128, 1122, 1098, 1279, 1285,
 /*   320 */  1235, 1255, 1227, 1258, 1217, 1256, 1257, 1206, 1209, 1195,
 /*   330 */  1222, 1210, 1231, 1266, 1189, 1264, 1190, 1188, 1191, 1237,
 /*   340 */  1319, 1220, 1211, 1311, 1312, 1326, 1327, 1259, 1291, 1240,
 /*   350 */  1243, 1346, 1315, 1303, 1267, 1228, 1302, 1310, 1350, 1268,
 /*   360 */  1276, 1313, 1294, 1316, 1317, 1314, 1318, 1299, 1320, 1321,
 /*   370 */  1300, 1308, 1322, 1323, 1324, 1325, 1277, 1330, 1331, 1333,
 /*   380 */  1332, 1272, 1334, 1336, 1328, 1280, 1338, 1273, 1337, 1281,
 /*   390 */  1339, 1283, 1343, 1337, 1344, 1345, 1347, 1329, 1348, 1349,
 /*   400 */  1351, 1356, 1352, 1354, 1353, 1355, 1359, 1357, 1360, 1355,
 /*   410 */  1362, 1361, 1363, 1364, 1366, 1301, 1335, 1367, 1372, 1395,
 /*   420 */  1416,
};
#define YY_REDUCE_USE_DFLT (-71)
#define YY_REDUCE_COUNT (293)
#define YY_REDUCE_MIN   (-70)
#define YY_REDUCE_MAX   (1158)
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
 /*   170 */   411,  498,  545,  476,  644,  679,  757,  157,  820,  852,
 /*   180 */   989,  880,  902, 1035, 1036, 1037, 1038,  887,  979,  981,
 /*   190 */  1042, 1043,  781,  998, 1044,  960, 1048, 1049, 1050, 1051,
 /*   200 */  1052, 1054, 1025, 1056, 1027, 1060, 1032, 1062, 1040, 1064,
 /*   210 */  1065, 1003, 1066,  985, 1046, 1053, 1068, 1070, 1071,  545,
 /*   220 */  1026, 1029, 1075, 1076, 1074, 1078, 1079, 1080, 1081,  995,
 /*   230 */  1017, 1057, 1039, 1067, 1072, 1073, 1077, 1005, 1011, 1090,
 /*   240 */  1082, 1030, 1083, 1084, 1086, 1091, 1089, 1094, 1095, 1100,
 /*   250 */  1088, 1096, 1102, 1092, 1104, 1108, 1110, 1115, 1112, 1116,
 /*   260 */  1121, 1045, 1123, 1124, 1126, 1127, 1129, 1130, 1132, 1125,
 /*   270 */  1041, 1131, 1133, 1134, 1135, 1137, 1141, 1145, 1147, 1138,
 /*   280 */  1139, 1143, 1144, 1148, 1140, 1149, 1152, 1154, 1146, 1150,
 /*   290 */  1151, 1155, 1156, 1158,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1187, 1181, 1181, 1181, 1124, 1124, 1124, 1124, 1181, 1019,
 /*    10 */  1046, 1046, 1231, 1231, 1231, 1231, 1231, 1231, 1123, 1231,
 /*    20 */  1231, 1231, 1231, 1181, 1023, 1052, 1231, 1231, 1231, 1125,
 /*    30 */  1126, 1231, 1231, 1231, 1157, 1062, 1061, 1060, 1059, 1033,
 /*    40 */  1057, 1050, 1054, 1125, 1119, 1120, 1118, 1122, 1126, 1231,
 /*    50 */  1053, 1088, 1103, 1087, 1231, 1231, 1231, 1231, 1231, 1231,
 /*    60 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*    70 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*    80 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*    90 */  1231, 1231, 1231, 1231, 1231, 1231, 1097, 1102, 1109, 1101,
 /*   100 */  1098, 1090, 1089, 1091, 1092,  990, 1231, 1231, 1231, 1093,
 /*   110 */  1231, 1231, 1094, 1106, 1105, 1104, 1179, 1196, 1195, 1231,
 /*   120 */  1231, 1131, 1231, 1231, 1231, 1231, 1181, 1231, 1231, 1231,
 /*   130 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*   140 */  1231, 1231, 1231,  948, 1231, 1231, 1231,  948, 1231, 1181,
 /*   150 */  1181, 1181, 1181, 1181, 1181, 1023, 1014, 1231, 1231, 1231,
 /*   160 */  1231, 1231, 1231, 1231, 1231, 1019, 1231, 1231, 1231, 1231,
 /*   170 */  1152, 1231, 1231, 1019, 1019, 1019, 1231, 1021, 1231, 1003,
 /*   180 */  1013, 1231, 1176, 1231, 1173, 1231, 1231, 1056, 1035, 1035,
 /*   190 */  1231, 1231, 1228, 1056, 1231, 1228, 1231, 1231, 1231, 1231,
 /*   200 */  1231, 1231,  965, 1231, 1207, 1231,  962, 1231, 1046, 1231,
 /*   210 */  1231, 1035, 1231, 1121, 1046, 1046, 1231, 1231, 1231, 1231,
 /*   220 */  1020, 1013, 1231, 1231, 1231, 1231, 1231, 1231, 1190, 1229,
 /*   230 */  1067,  993, 1056,  999,  999,  999,  999, 1156, 1225,  940,
 /*   240 */  1056, 1067,  993,  993, 1056,  940, 1132,  940,  940, 1132,
 /*   250 */   991,  991,  991,  980, 1132,  991,  965,  991,  980,  991,
 /*   260 */   991, 1039, 1034, 1039, 1034, 1039, 1034, 1039, 1034, 1127,
 /*   270 */  1231, 1132, 1136, 1136, 1132, 1051, 1040, 1049, 1047, 1056,
 /*   280 */   944,  983, 1193, 1193, 1189, 1189, 1189, 1189,  930, 1202,
 /*   290 */  1202,  967,  967, 1202, 1231, 1231, 1231, 1197, 1139, 1231,
 /*   300 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*   310 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1073, 1231,  927,
 /*   320 */  1231, 1231, 1180, 1231, 1174, 1231, 1231, 1220, 1231, 1231,
 /*   330 */  1231, 1231, 1231, 1231, 1231, 1155, 1154, 1231, 1231, 1231,
 /*   340 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*   350 */  1227, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*   360 */  1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1231,
 /*   370 */  1231, 1231, 1231, 1231, 1231, 1231, 1005, 1231, 1231, 1231,
 /*   380 */  1211, 1231, 1231, 1231, 1231, 1231, 1231, 1231, 1048, 1231,
 /*   390 */  1041, 1231, 1231, 1217, 1231, 1231, 1231, 1231, 1231, 1231,
 /*   400 */  1231, 1231, 1231, 1231, 1231, 1183, 1231, 1231, 1231, 1182,
 /*   410 */  1231, 1231, 1231, 1231, 1231, 1075, 1231, 1231,  934, 1231,
 /*   420 */  1231,
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
 /*  39 */ "ccons ::= COLLATE ID|STRING",
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
 /* 151 */ "expr ::= nm DOT nm DOT nm",
 /* 152 */ "term ::= FLOAT|BLOB",
 /* 153 */ "term ::= STRING",
 /* 154 */ "term ::= INTEGER",
 /* 155 */ "expr ::= VARIABLE",
 /* 156 */ "expr ::= expr COLLATE ID|STRING",
 /* 157 */ "expr ::= CAST LP expr AS typetoken RP",
 /* 158 */ "expr ::= ID|INDEXED LP distinct exprlist RP",
 /* 159 */ "expr ::= ID|INDEXED LP STAR RP",
 /* 160 */ "term ::= CTIME_KW",
 /* 161 */ "expr ::= LP nexprlist COMMA expr RP",
 /* 162 */ "expr ::= expr AND expr",
 /* 163 */ "expr ::= expr OR expr",
 /* 164 */ "expr ::= expr LT|GT|GE|LE expr",
 /* 165 */ "expr ::= expr EQ|NE expr",
 /* 166 */ "expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr",
 /* 167 */ "expr ::= expr PLUS|MINUS expr",
 /* 168 */ "expr ::= expr STAR|SLASH|REM expr",
 /* 169 */ "expr ::= expr CONCAT expr",
 /* 170 */ "likeop ::= LIKE_KW|MATCH",
 /* 171 */ "likeop ::= NOT LIKE_KW|MATCH",
 /* 172 */ "expr ::= expr likeop expr",
 /* 173 */ "expr ::= expr likeop expr ESCAPE expr",
 /* 174 */ "expr ::= expr ISNULL|NOTNULL",
 /* 175 */ "expr ::= expr NOT NULL",
 /* 176 */ "expr ::= expr IS expr",
 /* 177 */ "expr ::= expr IS NOT expr",
 /* 178 */ "expr ::= NOT expr",
 /* 179 */ "expr ::= BITNOT expr",
 /* 180 */ "expr ::= MINUS expr",
 /* 181 */ "expr ::= PLUS expr",
 /* 182 */ "between_op ::= BETWEEN",
 /* 183 */ "between_op ::= NOT BETWEEN",
 /* 184 */ "expr ::= expr between_op expr AND expr",
 /* 185 */ "in_op ::= IN",
 /* 186 */ "in_op ::= NOT IN",
 /* 187 */ "expr ::= expr in_op LP exprlist RP",
 /* 188 */ "expr ::= LP select RP",
 /* 189 */ "expr ::= expr in_op LP select RP",
 /* 190 */ "expr ::= expr in_op nm paren_exprlist",
 /* 191 */ "expr ::= EXISTS LP select RP",
 /* 192 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 193 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 194 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 195 */ "case_else ::= ELSE expr",
 /* 196 */ "case_else ::=",
 /* 197 */ "case_operand ::= expr",
 /* 198 */ "case_operand ::=",
 /* 199 */ "exprlist ::=",
 /* 200 */ "nexprlist ::= nexprlist COMMA expr",
 /* 201 */ "nexprlist ::= expr",
 /* 202 */ "paren_exprlist ::=",
 /* 203 */ "paren_exprlist ::= LP exprlist RP",
 /* 204 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt",
 /* 205 */ "uniqueflag ::= UNIQUE",
 /* 206 */ "uniqueflag ::=",
 /* 207 */ "eidlist_opt ::=",
 /* 208 */ "eidlist_opt ::= LP eidlist RP",
 /* 209 */ "eidlist ::= eidlist COMMA nm collate sortorder",
 /* 210 */ "eidlist ::= nm collate sortorder",
 /* 211 */ "collate ::=",
 /* 212 */ "collate ::= COLLATE ID|STRING",
 /* 213 */ "cmd ::= DROP INDEX ifexists fullname ON nm",
 /* 214 */ "cmd ::= PRAGMA nm",
 /* 215 */ "cmd ::= PRAGMA nm EQ nmnum",
 /* 216 */ "cmd ::= PRAGMA nm LP nmnum RP",
 /* 217 */ "cmd ::= PRAGMA nm EQ minus_num",
 /* 218 */ "cmd ::= PRAGMA nm LP minus_num RP",
 /* 219 */ "cmd ::= PRAGMA nm EQ nm DOT nm",
 /* 220 */ "plus_num ::= PLUS INTEGER|FLOAT",
 /* 221 */ "minus_num ::= MINUS INTEGER|FLOAT",
 /* 222 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
 /* 223 */ "trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause",
 /* 224 */ "trigger_time ::= BEFORE",
 /* 225 */ "trigger_time ::= AFTER",
 /* 226 */ "trigger_time ::= INSTEAD OF",
 /* 227 */ "trigger_time ::=",
 /* 228 */ "trigger_event ::= DELETE|INSERT",
 /* 229 */ "trigger_event ::= UPDATE",
 /* 230 */ "trigger_event ::= UPDATE OF idlist",
 /* 231 */ "when_clause ::=",
 /* 232 */ "when_clause ::= WHEN expr",
 /* 233 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
 /* 234 */ "trigger_cmd_list ::= trigger_cmd SEMI",
 /* 235 */ "trnm ::= nm DOT nm",
 /* 236 */ "tridxby ::= INDEXED BY nm",
 /* 237 */ "tridxby ::= NOT INDEXED",
 /* 238 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 239 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
 /* 240 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 241 */ "trigger_cmd ::= select",
 /* 242 */ "expr ::= RAISE LP IGNORE RP",
 /* 243 */ "expr ::= RAISE LP raisetype COMMA STRING RP",
 /* 244 */ "raisetype ::= ROLLBACK",
 /* 245 */ "raisetype ::= ABORT",
 /* 246 */ "raisetype ::= FAIL",
 /* 247 */ "cmd ::= DROP TRIGGER ifexists fullname",
 /* 248 */ "cmd ::= REINDEX",
 /* 249 */ "cmd ::= REINDEX nm",
 /* 250 */ "cmd ::= REINDEX nm ON nm",
 /* 251 */ "cmd ::= ANALYZE",
 /* 252 */ "cmd ::= ANALYZE nm",
 /* 253 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
 /* 254 */ "cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist",
 /* 255 */ "add_column_fullname ::= fullname",
 /* 256 */ "with ::=",
 /* 257 */ "with ::= WITH wqlist",
 /* 258 */ "with ::= WITH RECURSIVE wqlist",
 /* 259 */ "wqlist ::= nm eidlist_opt AS LP select RP",
 /* 260 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
 /* 261 */ "input ::= ecmd",
 /* 262 */ "explain ::=",
 /* 263 */ "cmdx ::= cmd",
 /* 264 */ "trans_opt ::=",
 /* 265 */ "trans_opt ::= TRANSACTION",
 /* 266 */ "trans_opt ::= TRANSACTION nm",
 /* 267 */ "savepoint_opt ::= SAVEPOINT",
 /* 268 */ "savepoint_opt ::=",
 /* 269 */ "cmd ::= create_table create_table_args",
 /* 270 */ "columnlist ::= columnlist COMMA columnname carglist",
 /* 271 */ "columnlist ::= columnname carglist",
 /* 272 */ "typetoken ::= typename",
 /* 273 */ "typename ::= ID|STRING",
 /* 274 */ "signed ::= plus_num",
 /* 275 */ "signed ::= minus_num",
 /* 276 */ "carglist ::= carglist ccons",
 /* 277 */ "carglist ::=",
 /* 278 */ "ccons ::= NULL onconf",
 /* 279 */ "conslist_opt ::= COMMA conslist",
 /* 280 */ "conslist ::= conslist tconscomma tcons",
 /* 281 */ "conslist ::= tcons",
 /* 282 */ "tconscomma ::=",
 /* 283 */ "defer_subclause_opt ::= defer_subclause",
 /* 284 */ "resolvetype ::= raisetype",
 /* 285 */ "selectnowith ::= oneselect",
 /* 286 */ "oneselect ::= values",
 /* 287 */ "sclp ::= selcollist COMMA",
 /* 288 */ "as ::= ID|STRING",
 /* 289 */ "join_nm ::= ID|INDEXED",
 /* 290 */ "join_nm ::= JOIN_KW",
 /* 291 */ "expr ::= term",
 /* 292 */ "exprlist ::= nexprlist",
 /* 293 */ "nmnum ::= plus_num",
 /* 294 */ "nmnum ::= STRING",
 /* 295 */ "nmnum ::= nm",
 /* 296 */ "nmnum ::= ON",
 /* 297 */ "nmnum ::= DELETE",
 /* 298 */ "nmnum ::= DEFAULT",
 /* 299 */ "plus_num ::= INTEGER|FLOAT",
 /* 300 */ "foreach_clause ::=",
 /* 301 */ "foreach_clause ::= FOR EACH ROW",
 /* 302 */ "trnm ::= nm",
 /* 303 */ "tridxby ::=",
 /* 304 */ "kwcolumn_opt ::=",
 /* 305 */ "kwcolumn_opt ::= COLUMNKW",
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
#line 1492 "parse.c"
}
      break;
    case 163: /* term */
    case 164: /* expr */
{
#line 840 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy412).pExpr);
#line 1500 "parse.c"
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
#line 1281 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy93));
#line 1518 "parse.c"
}
      break;
    case 184: /* fullname */
    case 191: /* from */
    case 202: /* seltablist */
    case 203: /* stl_prefix */
{
#line 624 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy167));
#line 1528 "parse.c"
}
      break;
    case 187: /* with */
    case 234: /* wqlist */
{
#line 1526 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy121));
#line 1536 "parse.c"
}
      break;
    case 192: /* where_opt */
    case 194: /* having_opt */
    case 206: /* on_opt */
    case 217: /* case_operand */
    case 219: /* case_else */
    case 228: /* when_clause */
{
#line 749 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy112));
#line 1548 "parse.c"
}
      break;
    case 207: /* using_opt */
    case 209: /* idlist */
    case 212: /* idlist_opt */
{
#line 661 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy330));
#line 1557 "parse.c"
}
      break;
    case 224: /* trigger_cmd_list */
    case 229: /* trigger_cmd */
{
#line 1401 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy177));
#line 1565 "parse.c"
}
      break;
    case 226: /* trigger_event */
{
#line 1387 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy270).b);
#line 1572 "parse.c"
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
#line 1747 "parse.c"
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
  { 164, 5 },
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
#line 2193 "parse.c"
        break;
      case 1: /* ecmd ::= SEMI */
#line 112 "parse.y"
{
  sqlite3ErrorMsg(pParse, "syntax error: empty request");
}
#line 2200 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN */
#line 117 "parse.y"
{ pParse->explain = 1; }
#line 2205 "parse.c"
        break;
      case 3: /* explain ::= EXPLAIN QUERY PLAN */
#line 118 "parse.y"
{ pParse->explain = 2; }
#line 2210 "parse.c"
        break;
      case 4: /* cmd ::= BEGIN transtype trans_opt */
#line 150 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy92);}
#line 2215 "parse.c"
        break;
      case 5: /* transtype ::= */
#line 155 "parse.y"
{yymsp[1].minor.yy92 = TK_DEFERRED;}
#line 2220 "parse.c"
        break;
      case 6: /* transtype ::= DEFERRED */
#line 156 "parse.y"
{yymsp[0].minor.yy92 = yymsp[0].major; /*A-overwrites-X*/}
#line 2225 "parse.c"
        break;
      case 7: /* cmd ::= COMMIT trans_opt */
      case 8: /* cmd ::= END trans_opt */ yytestcase(yyruleno==8);
#line 157 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2231 "parse.c"
        break;
      case 9: /* cmd ::= ROLLBACK trans_opt */
#line 159 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2236 "parse.c"
        break;
      case 10: /* cmd ::= SAVEPOINT nm */
#line 163 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2243 "parse.c"
        break;
      case 11: /* cmd ::= RELEASE savepoint_opt nm */
#line 166 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2250 "parse.c"
        break;
      case 12: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 169 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2257 "parse.c"
        break;
      case 13: /* create_table ::= createkw TABLE ifnotexists nm */
#line 176 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[0].minor.yy0,yymsp[-1].minor.yy92);
}
#line 2264 "parse.c"
        break;
      case 14: /* createkw ::= CREATE */
#line 179 "parse.y"
{disableLookaside(pParse);}
#line 2269 "parse.c"
        break;
      case 15: /* ifnotexists ::= */
      case 19: /* table_options ::= */ yytestcase(yyruleno==19);
      case 40: /* autoinc ::= */ yytestcase(yyruleno==40);
      case 55: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==55);
      case 65: /* defer_subclause_opt ::= */ yytestcase(yyruleno==65);
      case 74: /* ifexists ::= */ yytestcase(yyruleno==74);
      case 88: /* distinct ::= */ yytestcase(yyruleno==88);
      case 211: /* collate ::= */ yytestcase(yyruleno==211);
#line 182 "parse.y"
{yymsp[1].minor.yy92 = 0;}
#line 2281 "parse.c"
        break;
      case 16: /* ifnotexists ::= IF NOT EXISTS */
#line 183 "parse.y"
{yymsp[-2].minor.yy92 = 1;}
#line 2286 "parse.c"
        break;
      case 17: /* create_table_args ::= LP columnlist conslist_opt RP table_options */
#line 185 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,yymsp[0].minor.yy92,0);
}
#line 2293 "parse.c"
        break;
      case 18: /* create_table_args ::= AS select */
#line 188 "parse.y"
{
  sqlite3EndTable(pParse,0,0,0,yymsp[0].minor.yy329);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy329);
}
#line 2301 "parse.c"
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
#line 2313 "parse.c"
        break;
      case 21: /* columnname ::= nm typetoken */
#line 204 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2318 "parse.c"
        break;
      case 22: /* nm ::= ID|INDEXED */
#line 235 "parse.y"
{
  if(yymsp[0].minor.yy0.isReserved) {
    sqlite3ErrorMsg(pParse, "keyword \"%T\" is reserved", &yymsp[0].minor.yy0);
  }
}
#line 2327 "parse.c"
        break;
      case 23: /* typetoken ::= */
      case 58: /* conslist_opt ::= */ yytestcase(yyruleno==58);
      case 94: /* as ::= */ yytestcase(yyruleno==94);
#line 246 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2334 "parse.c"
        break;
      case 24: /* typetoken ::= typename LP signed RP */
#line 248 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2341 "parse.c"
        break;
      case 25: /* typetoken ::= typename LP signed COMMA signed RP */
#line 251 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2348 "parse.c"
        break;
      case 26: /* typename ::= typename ID|STRING */
#line 256 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2353 "parse.c"
        break;
      case 27: /* ccons ::= CONSTRAINT nm */
      case 60: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==60);
#line 265 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2359 "parse.c"
        break;
      case 28: /* ccons ::= DEFAULT term */
      case 30: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==30);
#line 266 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy412);}
#line 2365 "parse.c"
        break;
      case 29: /* ccons ::= DEFAULT LP expr RP */
#line 267 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy412);}
#line 2370 "parse.c"
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
#line 2381 "parse.c"
        break;
      case 32: /* ccons ::= DEFAULT ID|INDEXED */
#line 276 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2390 "parse.c"
        break;
      case 33: /* ccons ::= NOT NULL onconf */
#line 286 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy92);}
#line 2395 "parse.c"
        break;
      case 34: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 288 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy92,yymsp[0].minor.yy92,yymsp[-2].minor.yy92);}
#line 2400 "parse.c"
        break;
      case 35: /* ccons ::= UNIQUE onconf */
#line 289 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[0].minor.yy92,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2406 "parse.c"
        break;
      case 36: /* ccons ::= CHECK LP expr RP */
#line 291 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy412.pExpr);}
#line 2411 "parse.c"
        break;
      case 37: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 293 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy93,yymsp[0].minor.yy92);}
#line 2416 "parse.c"
        break;
      case 38: /* ccons ::= defer_subclause */
#line 294 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy92);}
#line 2421 "parse.c"
        break;
      case 39: /* ccons ::= COLLATE ID|STRING */
#line 295 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2426 "parse.c"
        break;
      case 41: /* autoinc ::= AUTOINCR */
#line 300 "parse.y"
{yymsp[0].minor.yy92 = 1;}
#line 2431 "parse.c"
        break;
      case 42: /* refargs ::= */
#line 308 "parse.y"
{ yymsp[1].minor.yy92 = OE_None*0x0101; /* EV: R-19803-45884 */}
#line 2436 "parse.c"
        break;
      case 43: /* refargs ::= refargs refarg */
#line 309 "parse.y"
{ yymsp[-1].minor.yy92 = (yymsp[-1].minor.yy92 & ~yymsp[0].minor.yy97.mask) | yymsp[0].minor.yy97.value; }
#line 2441 "parse.c"
        break;
      case 44: /* refarg ::= MATCH nm */
#line 311 "parse.y"
{ yymsp[-1].minor.yy97.value = 0;     yymsp[-1].minor.yy97.mask = 0x000000; }
#line 2446 "parse.c"
        break;
      case 45: /* refarg ::= ON INSERT refact */
#line 312 "parse.y"
{ yymsp[-2].minor.yy97.value = 0;     yymsp[-2].minor.yy97.mask = 0x000000; }
#line 2451 "parse.c"
        break;
      case 46: /* refarg ::= ON DELETE refact */
#line 313 "parse.y"
{ yymsp[-2].minor.yy97.value = yymsp[0].minor.yy92;     yymsp[-2].minor.yy97.mask = 0x0000ff; }
#line 2456 "parse.c"
        break;
      case 47: /* refarg ::= ON UPDATE refact */
#line 314 "parse.y"
{ yymsp[-2].minor.yy97.value = yymsp[0].minor.yy92<<8;  yymsp[-2].minor.yy97.mask = 0x00ff00; }
#line 2461 "parse.c"
        break;
      case 48: /* refact ::= SET NULL */
#line 316 "parse.y"
{ yymsp[-1].minor.yy92 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2466 "parse.c"
        break;
      case 49: /* refact ::= SET DEFAULT */
#line 317 "parse.y"
{ yymsp[-1].minor.yy92 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2471 "parse.c"
        break;
      case 50: /* refact ::= CASCADE */
#line 318 "parse.y"
{ yymsp[0].minor.yy92 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2476 "parse.c"
        break;
      case 51: /* refact ::= RESTRICT */
#line 319 "parse.y"
{ yymsp[0].minor.yy92 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2481 "parse.c"
        break;
      case 52: /* refact ::= NO ACTION */
#line 320 "parse.y"
{ yymsp[-1].minor.yy92 = OE_None;     /* EV: R-33326-45252 */}
#line 2486 "parse.c"
        break;
      case 53: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 322 "parse.y"
{yymsp[-2].minor.yy92 = 0;}
#line 2491 "parse.c"
        break;
      case 54: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 69: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==69);
      case 140: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==140);
#line 323 "parse.y"
{yymsp[-1].minor.yy92 = yymsp[0].minor.yy92;}
#line 2498 "parse.c"
        break;
      case 56: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 73: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==73);
      case 183: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==183);
      case 186: /* in_op ::= NOT IN */ yytestcase(yyruleno==186);
      case 212: /* collate ::= COLLATE ID|STRING */ yytestcase(yyruleno==212);
#line 326 "parse.y"
{yymsp[-1].minor.yy92 = 1;}
#line 2507 "parse.c"
        break;
      case 57: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 327 "parse.y"
{yymsp[-1].minor.yy92 = 0;}
#line 2512 "parse.c"
        break;
      case 59: /* tconscomma ::= COMMA */
#line 333 "parse.y"
{pParse->constraintName.n = 0;}
#line 2517 "parse.c"
        break;
      case 61: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 337 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy93,yymsp[0].minor.yy92,yymsp[-2].minor.yy92,0);}
#line 2522 "parse.c"
        break;
      case 62: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 339 "parse.y"
{sqlite3CreateIndex(pParse,0,0,yymsp[-2].minor.yy93,yymsp[0].minor.yy92,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2528 "parse.c"
        break;
      case 63: /* tcons ::= CHECK LP expr RP onconf */
#line 342 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy412.pExpr);}
#line 2533 "parse.c"
        break;
      case 64: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 344 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy93, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy93, yymsp[-1].minor.yy92);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy92);
}
#line 2541 "parse.c"
        break;
      case 66: /* onconf ::= */
      case 68: /* orconf ::= */ yytestcase(yyruleno==68);
#line 358 "parse.y"
{yymsp[1].minor.yy92 = OE_Default;}
#line 2547 "parse.c"
        break;
      case 67: /* onconf ::= ON CONFLICT resolvetype */
#line 359 "parse.y"
{yymsp[-2].minor.yy92 = yymsp[0].minor.yy92;}
#line 2552 "parse.c"
        break;
      case 70: /* resolvetype ::= IGNORE */
#line 363 "parse.y"
{yymsp[0].minor.yy92 = OE_Ignore;}
#line 2557 "parse.c"
        break;
      case 71: /* resolvetype ::= REPLACE */
      case 141: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==141);
#line 364 "parse.y"
{yymsp[0].minor.yy92 = OE_Replace;}
#line 2563 "parse.c"
        break;
      case 72: /* cmd ::= DROP TABLE ifexists fullname */
#line 368 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy167, 0, yymsp[-1].minor.yy92);
}
#line 2570 "parse.c"
        break;
      case 75: /* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 379 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-6].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy93, yymsp[0].minor.yy329, yymsp[-4].minor.yy92);
}
#line 2577 "parse.c"
        break;
      case 76: /* cmd ::= DROP VIEW ifexists fullname */
#line 382 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy167, 1, yymsp[-1].minor.yy92);
}
#line 2584 "parse.c"
        break;
      case 77: /* cmd ::= select */
#line 389 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy329, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy329);
}
#line 2593 "parse.c"
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
#line 2607 "parse.c"
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
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,0,&x,pRhs,0,0);
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
#line 2633 "parse.c"
        break;
      case 80: /* multiselect_op ::= UNION */
      case 82: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==82);
#line 462 "parse.y"
{yymsp[0].minor.yy92 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2639 "parse.c"
        break;
      case 81: /* multiselect_op ::= UNION ALL */
#line 463 "parse.y"
{yymsp[-1].minor.yy92 = TK_ALL;}
#line 2644 "parse.c"
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
#line 2678 "parse.c"
        break;
      case 84: /* values ::= VALUES LP nexprlist RP */
#line 501 "parse.y"
{
  yymsp[-3].minor.yy329 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy93,0,0,0,0,0,SF_Values,0,0);
}
#line 2685 "parse.c"
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
#line 2701 "parse.c"
        break;
      case 86: /* distinct ::= DISTINCT */
#line 521 "parse.y"
{yymsp[0].minor.yy92 = SF_Distinct;}
#line 2706 "parse.c"
        break;
      case 87: /* distinct ::= ALL */
#line 522 "parse.y"
{yymsp[0].minor.yy92 = SF_All;}
#line 2711 "parse.c"
        break;
      case 89: /* sclp ::= */
      case 115: /* orderby_opt ::= */ yytestcase(yyruleno==115);
      case 122: /* groupby_opt ::= */ yytestcase(yyruleno==122);
      case 199: /* exprlist ::= */ yytestcase(yyruleno==199);
      case 202: /* paren_exprlist ::= */ yytestcase(yyruleno==202);
      case 207: /* eidlist_opt ::= */ yytestcase(yyruleno==207);
#line 535 "parse.y"
{yymsp[1].minor.yy93 = 0;}
#line 2721 "parse.c"
        break;
      case 90: /* selcollist ::= sclp expr as */
#line 536 "parse.y"
{
   yymsp[-2].minor.yy93 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy93, yymsp[-1].minor.yy412.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy93, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy93,&yymsp[-1].minor.yy412);
}
#line 2730 "parse.c"
        break;
      case 91: /* selcollist ::= sclp STAR */
#line 541 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy93 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy93, p);
}
#line 2738 "parse.c"
        break;
      case 92: /* selcollist ::= sclp nm DOT STAR */
#line 545 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy93, pDot);
}
#line 2748 "parse.c"
        break;
      case 93: /* as ::= AS nm */
      case 220: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==220);
      case 221: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==221);
#line 556 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2755 "parse.c"
        break;
      case 95: /* from ::= */
#line 570 "parse.y"
{yymsp[1].minor.yy167 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy167));}
#line 2760 "parse.c"
        break;
      case 96: /* from ::= FROM seltablist */
#line 571 "parse.y"
{
  yymsp[-1].minor.yy167 = yymsp[0].minor.yy167;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy167);
}
#line 2768 "parse.c"
        break;
      case 97: /* stl_prefix ::= seltablist joinop */
#line 579 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy167 && yymsp[-1].minor.yy167->nSrc>0) ) yymsp[-1].minor.yy167->a[yymsp[-1].minor.yy167->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy92;
}
#line 2775 "parse.c"
        break;
      case 98: /* stl_prefix ::= */
#line 582 "parse.y"
{yymsp[1].minor.yy167 = 0;}
#line 2780 "parse.c"
        break;
      case 99: /* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 584 "parse.y"
{
  yymsp[-5].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-5].minor.yy167,&yymsp[-4].minor.yy0,0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
  sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy167, &yymsp[-2].minor.yy0);
}
#line 2788 "parse.c"
        break;
      case 100: /* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 589 "parse.y"
{
  yymsp[-7].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-7].minor.yy167,&yymsp[-6].minor.yy0,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
  sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy167, yymsp[-4].minor.yy93);
}
#line 2796 "parse.c"
        break;
      case 101: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 595 "parse.y"
{
    yymsp[-6].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy167,0,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy329,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
  }
#line 2803 "parse.c"
        break;
      case 102: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 599 "parse.y"
{
    if( yymsp[-6].minor.yy167==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy112==0 && yymsp[0].minor.yy330==0 ){
      yymsp[-6].minor.yy167 = yymsp[-4].minor.yy167;
    }else if( yymsp[-4].minor.yy167->nSrc==1 ){
      yymsp[-6].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy167,0,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
      if( yymsp[-6].minor.yy167 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy167->a[yymsp[-6].minor.yy167->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy167->a;
        pNew->zName = pOld->zName;
        pNew->zDatabase = pOld->zDatabase;
        pNew->pSelect = pOld->pSelect;
        pOld->zName = pOld->zDatabase = 0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy167);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy167);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy167,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy167 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy167,0,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy112,yymsp[0].minor.yy330);
    }
  }
#line 2829 "parse.c"
        break;
      case 103: /* fullname ::= nm */
#line 626 "parse.y"
{yymsp[0].minor.yy167 = sqlite3SrcListAppend(pParse->db,0,&yymsp[0].minor.yy0,0); /*A-overwrites-X*/}
#line 2834 "parse.c"
        break;
      case 104: /* joinop ::= COMMA|JOIN */
#line 632 "parse.y"
{ yymsp[0].minor.yy92 = JT_INNER; }
#line 2839 "parse.c"
        break;
      case 105: /* joinop ::= JOIN_KW JOIN */
#line 634 "parse.y"
{yymsp[-1].minor.yy92 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2844 "parse.c"
        break;
      case 106: /* joinop ::= JOIN_KW join_nm JOIN */
#line 636 "parse.y"
{yymsp[-2].minor.yy92 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2849 "parse.c"
        break;
      case 107: /* joinop ::= JOIN_KW join_nm join_nm JOIN */
#line 638 "parse.y"
{yymsp[-3].minor.yy92 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2854 "parse.c"
        break;
      case 108: /* on_opt ::= ON expr */
      case 125: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==125);
      case 132: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==132);
      case 195: /* case_else ::= ELSE expr */ yytestcase(yyruleno==195);
#line 642 "parse.y"
{yymsp[-1].minor.yy112 = yymsp[0].minor.yy412.pExpr;}
#line 2862 "parse.c"
        break;
      case 109: /* on_opt ::= */
      case 124: /* having_opt ::= */ yytestcase(yyruleno==124);
      case 131: /* where_opt ::= */ yytestcase(yyruleno==131);
      case 196: /* case_else ::= */ yytestcase(yyruleno==196);
      case 198: /* case_operand ::= */ yytestcase(yyruleno==198);
#line 643 "parse.y"
{yymsp[1].minor.yy112 = 0;}
#line 2871 "parse.c"
        break;
      case 110: /* indexed_opt ::= */
#line 656 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2876 "parse.c"
        break;
      case 111: /* indexed_opt ::= INDEXED BY nm */
#line 657 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2881 "parse.c"
        break;
      case 112: /* indexed_opt ::= NOT INDEXED */
#line 658 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2886 "parse.c"
        break;
      case 113: /* using_opt ::= USING LP idlist RP */
#line 662 "parse.y"
{yymsp[-3].minor.yy330 = yymsp[-1].minor.yy330;}
#line 2891 "parse.c"
        break;
      case 114: /* using_opt ::= */
      case 142: /* idlist_opt ::= */ yytestcase(yyruleno==142);
#line 663 "parse.y"
{yymsp[1].minor.yy330 = 0;}
#line 2897 "parse.c"
        break;
      case 116: /* orderby_opt ::= ORDER BY sortlist */
      case 123: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==123);
#line 677 "parse.y"
{yymsp[-2].minor.yy93 = yymsp[0].minor.yy93;}
#line 2903 "parse.c"
        break;
      case 117: /* sortlist ::= sortlist COMMA expr sortorder */
#line 678 "parse.y"
{
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy93,yymsp[-1].minor.yy412.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy93,yymsp[0].minor.yy92);
}
#line 2911 "parse.c"
        break;
      case 118: /* sortlist ::= expr sortorder */
#line 682 "parse.y"
{
  yymsp[-1].minor.yy93 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy412.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy93,yymsp[0].minor.yy92);
}
#line 2919 "parse.c"
        break;
      case 119: /* sortorder ::= ASC */
#line 689 "parse.y"
{yymsp[0].minor.yy92 = SQLITE_SO_ASC;}
#line 2924 "parse.c"
        break;
      case 120: /* sortorder ::= DESC */
#line 690 "parse.y"
{yymsp[0].minor.yy92 = SQLITE_SO_DESC;}
#line 2929 "parse.c"
        break;
      case 121: /* sortorder ::= */
#line 691 "parse.y"
{yymsp[1].minor.yy92 = SQLITE_SO_UNDEFINED;}
#line 2934 "parse.c"
        break;
      case 126: /* limit_opt ::= */
#line 716 "parse.y"
{yymsp[1].minor.yy464.pLimit = 0; yymsp[1].minor.yy464.pOffset = 0;}
#line 2939 "parse.c"
        break;
      case 127: /* limit_opt ::= LIMIT expr */
#line 717 "parse.y"
{yymsp[-1].minor.yy464.pLimit = yymsp[0].minor.yy412.pExpr; yymsp[-1].minor.yy464.pOffset = 0;}
#line 2944 "parse.c"
        break;
      case 128: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 719 "parse.y"
{yymsp[-3].minor.yy464.pLimit = yymsp[-2].minor.yy412.pExpr; yymsp[-3].minor.yy464.pOffset = yymsp[0].minor.yy412.pExpr;}
#line 2949 "parse.c"
        break;
      case 129: /* limit_opt ::= LIMIT expr COMMA expr */
#line 721 "parse.y"
{yymsp[-3].minor.yy464.pOffset = yymsp[-2].minor.yy412.pExpr; yymsp[-3].minor.yy464.pLimit = yymsp[0].minor.yy412.pExpr;}
#line 2954 "parse.c"
        break;
      case 130: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 738 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy121, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy167, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy167,yymsp[0].minor.yy112);
}
#line 2966 "parse.c"
        break;
      case 133: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 771 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy121, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy167, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy93,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Update(pParse,yymsp[-4].minor.yy167,yymsp[-1].minor.yy93,yymsp[0].minor.yy112,yymsp[-5].minor.yy92);
}
#line 2979 "parse.c"
        break;
      case 134: /* setlist ::= setlist COMMA nm EQ expr */
#line 785 "parse.y"
{
  yymsp[-4].minor.yy93 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy93, yymsp[0].minor.yy412.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy93, &yymsp[-2].minor.yy0, 1);
}
#line 2987 "parse.c"
        break;
      case 135: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 789 "parse.y"
{
  yymsp[-6].minor.yy93 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy93, yymsp[-3].minor.yy330, yymsp[0].minor.yy412.pExpr);
}
#line 2994 "parse.c"
        break;
      case 136: /* setlist ::= nm EQ expr */
#line 792 "parse.y"
{
  yylhsminor.yy93 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy412.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy93, &yymsp[-2].minor.yy0, 1);
}
#line 3002 "parse.c"
  yymsp[-2].minor.yy93 = yylhsminor.yy93;
        break;
      case 137: /* setlist ::= LP idlist RP EQ expr */
#line 796 "parse.y"
{
  yymsp[-4].minor.yy93 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy330, yymsp[0].minor.yy412.pExpr);
}
#line 3010 "parse.c"
        break;
      case 138: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 802 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy121, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-2].minor.yy167, yymsp[0].minor.yy329, yymsp[-1].minor.yy330, yymsp[-4].minor.yy92);
}
#line 3021 "parse.c"
        break;
      case 139: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 810 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy121, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-3].minor.yy167, 0, yymsp[-2].minor.yy330, yymsp[-5].minor.yy92);
}
#line 3032 "parse.c"
        break;
      case 143: /* idlist_opt ::= LP idlist RP */
#line 828 "parse.y"
{yymsp[-2].minor.yy330 = yymsp[-1].minor.yy330;}
#line 3037 "parse.c"
        break;
      case 144: /* idlist ::= idlist COMMA nm */
#line 830 "parse.y"
{yymsp[-2].minor.yy330 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy330,&yymsp[0].minor.yy0);}
#line 3042 "parse.c"
        break;
      case 145: /* idlist ::= nm */
#line 832 "parse.y"
{yymsp[0].minor.yy330 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3047 "parse.c"
        break;
      case 146: /* expr ::= LP expr RP */
#line 882 "parse.y"
{spanSet(&yymsp[-2].minor.yy412,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy412.pExpr = yymsp[-1].minor.yy412.pExpr;}
#line 3052 "parse.c"
        break;
      case 147: /* term ::= NULL */
      case 152: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==152);
      case 153: /* term ::= STRING */ yytestcase(yyruleno==153);
#line 883 "parse.y"
{spanExpr(&yymsp[0].minor.yy412,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3059 "parse.c"
        break;
      case 148: /* expr ::= ID|INDEXED */
      case 149: /* expr ::= JOIN_KW */ yytestcase(yyruleno==149);
#line 884 "parse.y"
{spanExpr(&yymsp[0].minor.yy412,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3065 "parse.c"
        break;
      case 150: /* expr ::= nm DOT nm */
#line 886 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy412,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3075 "parse.c"
        break;
      case 151: /* expr ::= nm DOT nm DOT nm */
#line 892 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-4].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp3 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  Expr *temp4 = sqlite3PExpr(pParse, TK_DOT, temp2, temp3);
  spanSet(&yymsp[-4].minor.yy412,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-4].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp4);
}
#line 3087 "parse.c"
        break;
      case 154: /* term ::= INTEGER */
#line 902 "parse.y"
{
  yylhsminor.yy412.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy412.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy412.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy412.pExpr ) yylhsminor.yy412.pExpr->flags |= EP_Leaf;
}
#line 3097 "parse.c"
  yymsp[0].minor.yy412 = yylhsminor.yy412;
        break;
      case 155: /* expr ::= VARIABLE */
#line 908 "parse.y"
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
#line 3123 "parse.c"
        break;
      case 156: /* expr ::= expr COLLATE ID|STRING */
#line 929 "parse.y"
{
  yymsp[-2].minor.yy412.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy412.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy412.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3131 "parse.c"
        break;
      case 157: /* expr ::= CAST LP expr AS typetoken RP */
#line 934 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy412,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy412.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy412.pExpr, yymsp[-3].minor.yy412.pExpr, 0);
}
#line 3140 "parse.c"
        break;
      case 158: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 940 "parse.y"
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
#line 3154 "parse.c"
  yymsp[-4].minor.yy412 = yylhsminor.yy412;
        break;
      case 159: /* expr ::= ID|INDEXED LP STAR RP */
#line 950 "parse.y"
{
  yylhsminor.yy412.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy412,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3163 "parse.c"
  yymsp[-3].minor.yy412 = yylhsminor.yy412;
        break;
      case 160: /* term ::= CTIME_KW */
#line 954 "parse.y"
{
  yylhsminor.yy412.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy412, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3172 "parse.c"
  yymsp[0].minor.yy412 = yylhsminor.yy412;
        break;
      case 161: /* expr ::= LP nexprlist COMMA expr RP */
#line 983 "parse.y"
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
#line 3187 "parse.c"
  yymsp[-4].minor.yy412 = yylhsminor.yy412;
        break;
      case 162: /* expr ::= expr AND expr */
      case 163: /* expr ::= expr OR expr */ yytestcase(yyruleno==163);
      case 164: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==164);
      case 165: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==165);
      case 166: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==166);
      case 167: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==167);
      case 168: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==168);
      case 169: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==169);
#line 994 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy412,&yymsp[0].minor.yy412);}
#line 3200 "parse.c"
        break;
      case 170: /* likeop ::= LIKE_KW|MATCH */
#line 1007 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3205 "parse.c"
        break;
      case 171: /* likeop ::= NOT LIKE_KW|MATCH */
#line 1008 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3210 "parse.c"
        break;
      case 172: /* expr ::= expr likeop expr */
#line 1009 "parse.y"
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
#line 3225 "parse.c"
        break;
      case 173: /* expr ::= expr likeop expr ESCAPE expr */
#line 1020 "parse.y"
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
#line 3241 "parse.c"
        break;
      case 174: /* expr ::= expr ISNULL|NOTNULL */
#line 1047 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy412,&yymsp[0].minor.yy0);}
#line 3246 "parse.c"
        break;
      case 175: /* expr ::= expr NOT NULL */
#line 1048 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy412,&yymsp[0].minor.yy0);}
#line 3251 "parse.c"
        break;
      case 176: /* expr ::= expr IS expr */
#line 1069 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy412,&yymsp[0].minor.yy412);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy412.pExpr, yymsp[-2].minor.yy412.pExpr, TK_ISNULL);
}
#line 3259 "parse.c"
        break;
      case 177: /* expr ::= expr IS NOT expr */
#line 1073 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy412,&yymsp[0].minor.yy412);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy412.pExpr, yymsp[-3].minor.yy412.pExpr, TK_NOTNULL);
}
#line 3267 "parse.c"
        break;
      case 178: /* expr ::= NOT expr */
      case 179: /* expr ::= BITNOT expr */ yytestcase(yyruleno==179);
#line 1097 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy412,pParse,yymsp[-1].major,&yymsp[0].minor.yy412,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3273 "parse.c"
        break;
      case 180: /* expr ::= MINUS expr */
#line 1101 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy412,pParse,TK_UMINUS,&yymsp[0].minor.yy412,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3278 "parse.c"
        break;
      case 181: /* expr ::= PLUS expr */
#line 1103 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy412,pParse,TK_UPLUS,&yymsp[0].minor.yy412,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3283 "parse.c"
        break;
      case 182: /* between_op ::= BETWEEN */
      case 185: /* in_op ::= IN */ yytestcase(yyruleno==185);
#line 1106 "parse.y"
{yymsp[0].minor.yy92 = 0;}
#line 3289 "parse.c"
        break;
      case 184: /* expr ::= expr between_op expr AND expr */
#line 1108 "parse.y"
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
#line 3305 "parse.c"
        break;
      case 187: /* expr ::= expr in_op LP exprlist RP */
#line 1124 "parse.y"
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
#line 3360 "parse.c"
        break;
      case 188: /* expr ::= LP select RP */
#line 1175 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy412,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy412.pExpr, yymsp[-1].minor.yy329);
  }
#line 3369 "parse.c"
        break;
      case 189: /* expr ::= expr in_op LP select RP */
#line 1180 "parse.y"
{
    yymsp[-4].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy412.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy412.pExpr, yymsp[-1].minor.yy329);
    exprNot(pParse, yymsp[-3].minor.yy92, &yymsp[-4].minor.yy412);
    yymsp[-4].minor.yy412.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3379 "parse.c"
        break;
      case 190: /* expr ::= expr in_op nm paren_exprlist */
#line 1186 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0,0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy93 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy93);
    yymsp[-3].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy412.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-3].minor.yy412.pExpr, pSelect);
    exprNot(pParse, yymsp[-2].minor.yy92, &yymsp[-3].minor.yy412);
    yymsp[-3].minor.yy412.zEnd = &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
#line 3392 "parse.c"
        break;
      case 191: /* expr ::= EXISTS LP select RP */
#line 1195 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy412,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy329);
  }
#line 3402 "parse.c"
        break;
      case 192: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1204 "parse.y"
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
#line 3417 "parse.c"
        break;
      case 193: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1217 "parse.y"
{
  yymsp[-4].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy93, yymsp[-2].minor.yy412.pExpr);
  yymsp[-4].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy93, yymsp[0].minor.yy412.pExpr);
}
#line 3425 "parse.c"
        break;
      case 194: /* case_exprlist ::= WHEN expr THEN expr */
#line 1221 "parse.y"
{
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy412.pExpr);
  yymsp[-3].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy93, yymsp[0].minor.yy412.pExpr);
}
#line 3433 "parse.c"
        break;
      case 197: /* case_operand ::= expr */
#line 1231 "parse.y"
{yymsp[0].minor.yy112 = yymsp[0].minor.yy412.pExpr; /*A-overwrites-X*/}
#line 3438 "parse.c"
        break;
      case 200: /* nexprlist ::= nexprlist COMMA expr */
#line 1242 "parse.y"
{yymsp[-2].minor.yy93 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy93,yymsp[0].minor.yy412.pExpr);}
#line 3443 "parse.c"
        break;
      case 201: /* nexprlist ::= expr */
#line 1244 "parse.y"
{yymsp[0].minor.yy93 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy412.pExpr); /*A-overwrites-Y*/}
#line 3448 "parse.c"
        break;
      case 203: /* paren_exprlist ::= LP exprlist RP */
      case 208: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==208);
#line 1252 "parse.y"
{yymsp[-2].minor.yy93 = yymsp[-1].minor.yy93;}
#line 3454 "parse.c"
        break;
      case 204: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1259 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0,0), yymsp[-2].minor.yy93, yymsp[-9].minor.yy92,
                      &yymsp[-10].minor.yy0, yymsp[0].minor.yy112, SQLITE_SO_ASC, yymsp[-7].minor.yy92, SQLITE_IDXTYPE_APPDEF);
}
#line 3463 "parse.c"
        break;
      case 205: /* uniqueflag ::= UNIQUE */
      case 245: /* raisetype ::= ABORT */ yytestcase(yyruleno==245);
#line 1266 "parse.y"
{yymsp[0].minor.yy92 = OE_Abort;}
#line 3469 "parse.c"
        break;
      case 206: /* uniqueflag ::= */
#line 1267 "parse.y"
{yymsp[1].minor.yy92 = OE_None;}
#line 3474 "parse.c"
        break;
      case 209: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1310 "parse.y"
{
  yymsp[-4].minor.yy93 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy93, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy92, yymsp[0].minor.yy92);
}
#line 3481 "parse.c"
        break;
      case 210: /* eidlist ::= nm collate sortorder */
#line 1313 "parse.y"
{
  yymsp[-2].minor.yy93 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy92, yymsp[0].minor.yy92); /*A-overwrites-Y*/
}
#line 3488 "parse.c"
        break;
      case 213: /* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1324 "parse.y"
{
    sqlite3DropIndex(pParse, yymsp[-2].minor.yy167, &yymsp[0].minor.yy0, yymsp[-3].minor.yy92);
}
#line 3495 "parse.c"
        break;
      case 214: /* cmd ::= PRAGMA nm */
#line 1331 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[0].minor.yy0,0,0,0,0);
}
#line 3502 "parse.c"
        break;
      case 215: /* cmd ::= PRAGMA nm EQ nmnum */
#line 1334 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,0);
}
#line 3509 "parse.c"
        break;
      case 216: /* cmd ::= PRAGMA nm LP nmnum RP */
#line 1337 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,0);
}
#line 3516 "parse.c"
        break;
      case 217: /* cmd ::= PRAGMA nm EQ minus_num */
#line 1340 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,1);
}
#line 3523 "parse.c"
        break;
      case 218: /* cmd ::= PRAGMA nm LP minus_num RP */
#line 1343 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,1);
}
#line 3530 "parse.c"
        break;
      case 219: /* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1346 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,0,&yymsp[0].minor.yy0,&yymsp[-2].minor.yy0,0);
}
#line 3537 "parse.c"
        break;
      case 222: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1366 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy177, &all);
}
#line 3547 "parse.c"
        break;
      case 223: /* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1375 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy92, yymsp[-4].minor.yy270.a, yymsp[-4].minor.yy270.b, yymsp[-2].minor.yy167, yymsp[0].minor.yy112, yymsp[-7].minor.yy92);
  yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0; /*yymsp[-8].minor.yy0-overwrites-T*/
}
#line 3555 "parse.c"
        break;
      case 224: /* trigger_time ::= BEFORE */
#line 1381 "parse.y"
{ yymsp[0].minor.yy92 = TK_BEFORE; }
#line 3560 "parse.c"
        break;
      case 225: /* trigger_time ::= AFTER */
#line 1382 "parse.y"
{ yymsp[0].minor.yy92 = TK_AFTER;  }
#line 3565 "parse.c"
        break;
      case 226: /* trigger_time ::= INSTEAD OF */
#line 1383 "parse.y"
{ yymsp[-1].minor.yy92 = TK_INSTEAD;}
#line 3570 "parse.c"
        break;
      case 227: /* trigger_time ::= */
#line 1384 "parse.y"
{ yymsp[1].minor.yy92 = TK_BEFORE; }
#line 3575 "parse.c"
        break;
      case 228: /* trigger_event ::= DELETE|INSERT */
      case 229: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==229);
#line 1388 "parse.y"
{yymsp[0].minor.yy270.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy270.b = 0;}
#line 3581 "parse.c"
        break;
      case 230: /* trigger_event ::= UPDATE OF idlist */
#line 1390 "parse.y"
{yymsp[-2].minor.yy270.a = TK_UPDATE; yymsp[-2].minor.yy270.b = yymsp[0].minor.yy330;}
#line 3586 "parse.c"
        break;
      case 231: /* when_clause ::= */
#line 1397 "parse.y"
{ yymsp[1].minor.yy112 = 0; }
#line 3591 "parse.c"
        break;
      case 232: /* when_clause ::= WHEN expr */
#line 1398 "parse.y"
{ yymsp[-1].minor.yy112 = yymsp[0].minor.yy412.pExpr; }
#line 3596 "parse.c"
        break;
      case 233: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1402 "parse.y"
{
  assert( yymsp[-2].minor.yy177!=0 );
  yymsp[-2].minor.yy177->pLast->pNext = yymsp[-1].minor.yy177;
  yymsp[-2].minor.yy177->pLast = yymsp[-1].minor.yy177;
}
#line 3605 "parse.c"
        break;
      case 234: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1407 "parse.y"
{ 
  assert( yymsp[-1].minor.yy177!=0 );
  yymsp[-1].minor.yy177->pLast = yymsp[-1].minor.yy177;
}
#line 3613 "parse.c"
        break;
      case 235: /* trnm ::= nm DOT nm */
#line 1418 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3623 "parse.c"
        break;
      case 236: /* tridxby ::= INDEXED BY nm */
#line 1430 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3632 "parse.c"
        break;
      case 237: /* tridxby ::= NOT INDEXED */
#line 1435 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3641 "parse.c"
        break;
      case 238: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1448 "parse.y"
{yymsp[-6].minor.yy177 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy93, yymsp[0].minor.yy112, yymsp[-5].minor.yy92);}
#line 3646 "parse.c"
        break;
      case 239: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1452 "parse.y"
{yymsp[-4].minor.yy177 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy330, yymsp[0].minor.yy329, yymsp[-4].minor.yy92);/*A-overwrites-R*/}
#line 3651 "parse.c"
        break;
      case 240: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1456 "parse.y"
{yymsp[-4].minor.yy177 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy112);}
#line 3656 "parse.c"
        break;
      case 241: /* trigger_cmd ::= select */
#line 1460 "parse.y"
{yymsp[0].minor.yy177 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy329); /*A-overwrites-X*/}
#line 3661 "parse.c"
        break;
      case 242: /* expr ::= RAISE LP IGNORE RP */
#line 1463 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy412,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy412.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy412.pExpr ){
    yymsp[-3].minor.yy412.pExpr->affinity = OE_Ignore;
  }
}
#line 3672 "parse.c"
        break;
      case 243: /* expr ::= RAISE LP raisetype COMMA STRING RP */
#line 1470 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy412,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy412.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy412.pExpr ) {
    yymsp[-5].minor.yy412.pExpr->affinity = (char)yymsp[-3].minor.yy92;
  }
}
#line 3683 "parse.c"
        break;
      case 244: /* raisetype ::= ROLLBACK */
#line 1480 "parse.y"
{yymsp[0].minor.yy92 = OE_Rollback;}
#line 3688 "parse.c"
        break;
      case 246: /* raisetype ::= FAIL */
#line 1482 "parse.y"
{yymsp[0].minor.yy92 = OE_Fail;}
#line 3693 "parse.c"
        break;
      case 247: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1487 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy167,yymsp[-1].minor.yy92);
}
#line 3700 "parse.c"
        break;
      case 248: /* cmd ::= REINDEX */
#line 1494 "parse.y"
{sqlite3Reindex(pParse, 0, 0);}
#line 3705 "parse.c"
        break;
      case 249: /* cmd ::= REINDEX nm */
#line 1495 "parse.y"
{sqlite3Reindex(pParse, &yymsp[0].minor.yy0, 0);}
#line 3710 "parse.c"
        break;
      case 250: /* cmd ::= REINDEX nm ON nm */
#line 1496 "parse.y"
{sqlite3Reindex(pParse, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);}
#line 3715 "parse.c"
        break;
      case 251: /* cmd ::= ANALYZE */
#line 1501 "parse.y"
{sqlite3Analyze(pParse, 0);}
#line 3720 "parse.c"
        break;
      case 252: /* cmd ::= ANALYZE nm */
#line 1502 "parse.y"
{sqlite3Analyze(pParse, &yymsp[0].minor.yy0);}
#line 3725 "parse.c"
        break;
      case 253: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1507 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy167,&yymsp[0].minor.yy0);
}
#line 3732 "parse.c"
        break;
      case 254: /* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist */
#line 1511 "parse.y"
{
  yymsp[-1].minor.yy0.n = (int)(pParse->sLastToken.z-yymsp[-1].minor.yy0.z) + pParse->sLastToken.n;
  sqlite3AlterFinishAddColumn(pParse, &yymsp[-1].minor.yy0);
}
#line 3740 "parse.c"
        break;
      case 255: /* add_column_fullname ::= fullname */
#line 1515 "parse.y"
{
  disableLookaside(pParse);
  sqlite3AlterBeginAddColumn(pParse, yymsp[0].minor.yy167);
}
#line 3748 "parse.c"
        break;
      case 256: /* with ::= */
#line 1529 "parse.y"
{yymsp[1].minor.yy121 = 0;}
#line 3753 "parse.c"
        break;
      case 257: /* with ::= WITH wqlist */
#line 1531 "parse.y"
{ yymsp[-1].minor.yy121 = yymsp[0].minor.yy121; }
#line 3758 "parse.c"
        break;
      case 258: /* with ::= WITH RECURSIVE wqlist */
#line 1532 "parse.y"
{ yymsp[-2].minor.yy121 = yymsp[0].minor.yy121; }
#line 3763 "parse.c"
        break;
      case 259: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1534 "parse.y"
{
  yymsp[-5].minor.yy121 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy93, yymsp[-1].minor.yy329); /*A-overwrites-X*/
}
#line 3770 "parse.c"
        break;
      case 260: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1537 "parse.y"
{
  yymsp[-7].minor.yy121 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy121, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy93, yymsp[-1].minor.yy329);
}
#line 3777 "parse.c"
        break;
      default:
      /* (261) input ::= ecmd */ yytestcase(yyruleno==261);
      /* (262) explain ::= */ yytestcase(yyruleno==262);
      /* (263) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno!=263);
      /* (264) trans_opt ::= */ yytestcase(yyruleno==264);
      /* (265) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==265);
      /* (266) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==266);
      /* (267) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==267);
      /* (268) savepoint_opt ::= */ yytestcase(yyruleno==268);
      /* (269) cmd ::= create_table create_table_args */ yytestcase(yyruleno==269);
      /* (270) columnlist ::= columnlist COMMA columnname carglist */ yytestcase(yyruleno==270);
      /* (271) columnlist ::= columnname carglist */ yytestcase(yyruleno==271);
      /* (272) typetoken ::= typename */ yytestcase(yyruleno==272);
      /* (273) typename ::= ID|STRING */ yytestcase(yyruleno==273);
      /* (274) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=274);
      /* (275) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=275);
      /* (276) carglist ::= carglist ccons */ yytestcase(yyruleno==276);
      /* (277) carglist ::= */ yytestcase(yyruleno==277);
      /* (278) ccons ::= NULL onconf */ yytestcase(yyruleno==278);
      /* (279) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==279);
      /* (280) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==280);
      /* (281) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=281);
      /* (282) tconscomma ::= */ yytestcase(yyruleno==282);
      /* (283) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=283);
      /* (284) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=284);
      /* (285) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=285);
      /* (286) oneselect ::= values */ yytestcase(yyruleno==286);
      /* (287) sclp ::= selcollist COMMA */ yytestcase(yyruleno==287);
      /* (288) as ::= ID|STRING */ yytestcase(yyruleno==288);
      /* (289) join_nm ::= ID|INDEXED */ yytestcase(yyruleno==289);
      /* (290) join_nm ::= JOIN_KW */ yytestcase(yyruleno==290);
      /* (291) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=291);
      /* (292) exprlist ::= nexprlist */ yytestcase(yyruleno==292);
      /* (293) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=293);
      /* (294) nmnum ::= STRING */ yytestcase(yyruleno==294);
      /* (295) nmnum ::= nm */ yytestcase(yyruleno==295);
      /* (296) nmnum ::= ON */ yytestcase(yyruleno==296);
      /* (297) nmnum ::= DELETE */ yytestcase(yyruleno==297);
      /* (298) nmnum ::= DEFAULT */ yytestcase(yyruleno==298);
      /* (299) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno==299);
      /* (300) foreach_clause ::= */ yytestcase(yyruleno==300);
      /* (301) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==301);
      /* (302) trnm ::= nm */ yytestcase(yyruleno==302);
      /* (303) tridxby ::= */ yytestcase(yyruleno==303);
      /* (304) kwcolumn_opt ::= */ yytestcase(yyruleno==304);
      /* (305) kwcolumn_opt ::= COLUMNKW */ yytestcase(yyruleno==305);
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
#line 3890 "parse.c"
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
