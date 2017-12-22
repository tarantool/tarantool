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

#line 392 "parse.y"

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
#line 831 "parse.y"

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
#line 939 "parse.y"

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
#line 1013 "parse.y"

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
#line 1030 "parse.y"

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
#line 1058 "parse.y"

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
#line 1263 "parse.y"

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
#define YYNOCODE 234
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 74
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  struct TrigEvent yy60;
  SrcList* yy73;
  ExprSpan yy200;
  TriggerStep* yy251;
  ExprList* yy272;
  Expr* yy294;
  Select* yy309;
  int yy322;
  struct {int value; int mask;} yy369;
  struct LimitVal yy400;
  IdList* yy436;
  With* yy445;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYFALLBACK 1
#define YYNSTATE             417
#define YYNRULE              303
#define YY_MAX_SHIFT         416
#define YY_MIN_SHIFTREDUCE   614
#define YY_MAX_SHIFTREDUCE   916
#define YY_MIN_REDUCE        917
#define YY_MAX_REDUCE        1219
#define YY_ERROR_ACTION      1220
#define YY_ACCEPT_ACTION     1221
#define YY_NO_ACTION         1222
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
#define YY_ACTTAB_COUNT (1413)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    91,   92,  291,   82,  781,  781,  793,  796,  785,  785,
 /*    10 */    89,   89,   90,   90,   90,   90,  313,   88,   88,   88,
 /*    20 */    88,   87,   87,   86,   86,   86,   85,  313,   90,   90,
 /*    30 */    90,   90,   83,   88,   88,   88,   88,   87,   87,   86,
 /*    40 */    86,   86,   85,  313,  212,  124,  899,   90,   90,   90,
 /*    50 */    90,  640,   88,   88,   88,   88,   87,   87,   86,   86,
 /*    60 */    86,   85,  313,   87,   87,   86,   86,   86,   85,  313,
 /*    70 */   899,   86,   86,   86,   85,  313,   91,   92,  291,   82,
 /*    80 */   781,  781,  793,  796,  785,  785,   89,   89,   90,   90,
 /*    90 */    90,   90,  643,   88,   88,   88,   88,   87,   87,   86,
 /*   100 */    86,   86,   85,  313,   91,   92,  291,   82,  781,  781,
 /*   110 */   793,  796,  785,  785,   89,   89,   90,   90,   90,   90,
 /*   120 */   730,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   130 */    85,  313,  750,   91,   92,  291,   82,  781,  781,  793,
 /*   140 */   796,  785,  785,   89,   89,   90,   90,   90,   90,   67,
 /*   150 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   160 */   313,  782,  782,  794,  797,  110,   93,   84,   81,  178,
 /*   170 */   311,  310,  676,  655,  729,  373,  248,  694,  694,  731,
 /*   180 */   732,  676,   91,   92,  291,   82,  781,  781,  793,  796,
 /*   190 */   785,  785,   89,   89,   90,   90,   90,   90,  341,   88,
 /*   200 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  313,
 /*   210 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   220 */   313,   84,   81,  178,  642,  648, 1221,  416,    3,  833,
 /*   230 */   786,   91,   92,  291,   82,  781,  781,  793,  796,  785,
 /*   240 */   785,   89,   89,   90,   90,   90,   90,  369,   88,   88,
 /*   250 */    88,   88,   87,   87,   86,   86,   86,   85,  313,  910,
 /*   260 */   753,  910,  187,  415,  415,  174,  891,  716,  771,  222,
 /*   270 */   764,  121,  884,  759,  641,  689,  241,  338,  240,  659,
 /*   280 */    91,   92,  291,   82,  781,  781,  793,  796,  785,  785,
 /*   290 */    89,   89,   90,   90,   90,   90,  884,   88,   88,   88,
 /*   300 */    88,   87,   87,   86,   86,   86,   85,  313,   22,  378,
 /*   310 */   763,  763,  765,  203,  699,  143,  364,  361,  360,  698,
 /*   320 */   167,  716,  709,  772,  286,  124,    9,    9,  359,   91,
 /*   330 */    92,  291,   82,  781,  781,  793,  796,  785,  785,   89,
 /*   340 */    89,   90,   90,   90,   90,  753,   88,   88,   88,   88,
 /*   350 */    87,   87,   86,   86,   86,   85,  313,  702, 1170, 1170,
 /*   360 */   887,  241,  328,  229,   84,   81,  178,  143,  650,   84,
 /*   370 */    81,  178,  758,  278,  220,  374,  287,  381,   91,   92,
 /*   380 */   291,   82,  781,  781,  793,  796,  785,  785,   89,   89,
 /*   390 */    90,   90,   90,   90,  124,   88,   88,   88,   88,   87,
 /*   400 */    87,   86,   86,   86,   85,  313,   91,   92,  291,   82,
 /*   410 */   781,  781,  793,  796,  785,  785,   89,   89,   90,   90,
 /*   420 */    90,   90,  382,   88,   88,   88,   88,   87,   87,   86,
 /*   430 */    86,   86,   85,  313,   91,   92,  291,   82,  781,  781,
 /*   440 */   793,  796,  785,  785,   89,   89,   90,   90,   90,   90,
 /*   450 */   658,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   460 */    85,  313,   91,   92,  291,   82,  781,  781,  793,  796,
 /*   470 */   785,  785,   89,   89,   90,   90,   90,   90,  147,   88,
 /*   480 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  313,
 /*   490 */   410,  410,  410,  615,  316,   70,   92,  291,   82,  781,
 /*   500 */   781,  793,  796,  785,  785,   89,   89,   90,   90,   90,
 /*   510 */    90,  657,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   520 */    86,   85,  313,   73,  312,  312,  312,  634,  634,   91,
 /*   530 */    80,  291,   82,  781,  781,  793,  796,  785,  785,   89,
 /*   540 */    89,   90,   90,   90,   90,  211,   88,   88,   88,   88,
 /*   550 */    87,   87,   86,   86,   86,   85,  313,  291,   82,  781,
 /*   560 */   781,  793,  796,  785,  785,   89,   89,   90,   90,   90,
 /*   570 */    90,   78,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   580 */    86,   85,  313,  179,  179,  839,  839,  333,  305,  139,
 /*   590 */    75,   76,  715,  634,  634,  377,  285,   77,  269,  283,
 /*   600 */   282,  281,  224,  279,  753,   78,  628,   85,  313,  412,
 /*   610 */   408,    2, 1113,  302,  323,  314,  314,  412,  634,  634,
 /*   620 */   241,  338,  240,  850,   75,   76,   10,   10,  323,  322,
 /*   630 */   319,   77,  397,  184,   10,   10,  384,  771,  352,  764,
 /*   640 */   299,  349,  759,  270,  408,    2,  269,  412,  301,  314,
 /*   650 */   314,  212,  166,  899,  292,  391,  110,  221,  157,  258,
 /*   660 */   367,  253,  366,  207,   48,   48,  397,  761,  395,  124,
 /*   670 */   251,  771,  347,  764,  412,  300,  759,  899,  317,  763,
 /*   680 */   763,  765,  766,  411,   18,  716,  406,  186,  110,  169,
 /*   690 */   323,   48,   48,  182,  320,  325,  124,  347,  124,  390,
 /*   700 */   392,  761,  145,  412,  836,  412,  835,  412,  356,   78,
 /*   710 */   205,  371,  331,  763,  763,  765,  766,  411,   18,  412,
 /*   720 */    30,   30,   48,   48,   48,   48,  390,  375,   75,   76,
 /*   730 */   703,  347,  124,  412,  402,   77,   48,   48,  167,  716,
 /*   740 */   311,  310,  140,   78,  110,  757,  634,  634,  408,    2,
 /*   750 */    10,   10,  124,  314,  314,  372,  192,  390,  380,  390,
 /*   760 */   389,  412,   75,   76,  202,  634,  634,  634,  634,   77,
 /*   770 */   397,  390,  370,  852,  341,  771,  412,  764,   48,   48,
 /*   780 */   759,  239,  408,    2,   74,  293,   72,  314,  314,  905,
 /*   790 */   251,  909,  203,   48,   48,  364,  361,  360,  907, 1193,
 /*   800 */   908,  671,  855,  336,  397,  761,  110,  359,  332,  771,
 /*   810 */   270,  764,  715,  306,  759,  195,  856,  763,  763,  765,
 /*   820 */   766,  411,   18,  898,  857,  682,  324,  412,  385,  910,
 /*   830 */   339,  910,  683,  343,  179,  179,  159,  158,  193,  761,
 /*   840 */   826,  828,  309,   23,   10,   10,  377,   68,  337,  852,
 /*   850 */   228,  763,  763,  765,  766,  411,   18,   95,  387,  412,
 /*   860 */   326,  644,  644,  260,  246,  143,   75,   76,  163,  162,
 /*   870 */   161,  379,  762,   77,  290,  155,   47,   47,  662,  900,
 /*   880 */   900,  165,  110,  771,  365,  764,  408,    2,  759,  663,
 /*   890 */   262,  314,  314,  204,    5,  204,  212,  264,  899,  900,
 /*   900 */   900,  816,  826,  234,  110,   24,  339,  303,  397,  634,
 /*   910 */   634,  412,  715,  771,  245,  764,  412,  704,  759,  164,
 /*   920 */   176,  412,  899,  855,  348,  763,  763,  765,   34,   34,
 /*   930 */   268,  901,  718,   35,   35,  220,  374,  856,   36,   36,
 /*   940 */   634,  634,  267,  761,  107,  857,  396,  237,  638,  412,
 /*   950 */   188,  901,  717,  233,  691,  763,  763,  765,  766,  411,
 /*   960 */    18,  412,  715,    7,  412,  377,   37,   37,  681,  681,
 /*   970 */   412,  699,  638,  412,  715,  412,  698,  412,   38,   38,
 /*   980 */   412,   26,   26,  757,  232,  412,  231,   27,   27,  412,
 /*   990 */    29,   29,   39,   39,   40,   40,  412,   41,   41,  412,
 /*  1000 */   189,  412,   11,   11,   66,  412,   42,   42,  864,  412,
 /*  1010 */   219,  412,  858,   97,   97,  863,   43,   43,   44,   44,
 /*  1020 */    54,  412,   31,   31,  412,  861,   45,   45,   46,   46,
 /*  1030 */   412,  715,  412,  111,  412,  151,  412,  653,   32,   32,
 /*  1040 */   412,  113,  113,  412,  257,  412,  296,  114,  114,  115,
 /*  1050 */   115,   52,   52,   33,   33,  256,  412,   98,   98,  412,
 /*  1060 */    49,   49,   99,   99,  412,  757,  412,  715,  412,  329,
 /*  1070 */   412,  757,  412,  100,  100,  412,   96,   96,  412,  679,
 /*  1080 */   679,  112,  112,  109,  109,  104,  104,  103,  103,  101,
 /*  1090 */   101,  412,  102,  102,  412,   51,   51,  412,    1,  171,
 /*  1100 */   412,  715,  634,  634,  715,  688,  633,  294,   53,   53,
 /*  1110 */   687,   50,   50,  757,   25,   25,  345,   28,   28,  632,
 /*  1120 */   409,  294,  399,  403,  205,  407,  731,  732,  297,  177,
 /*  1130 */   176,  755,   20,  210,  298,  340,  342,  210,  210,  684,
 /*  1140 */   651,  651,  236,  110,  242,  357,   66,  216,  249,  668,
 /*  1150 */    66,  180,  110,  661,  660,  110,  110,  321,  110,  823,
 /*  1160 */   823,  346,   64,   19,  148,  696,  725,   69,  210,  819,
 /*  1170 */   832,  216,  832,  831,  830,  831,  307,  636,  295,  106,
 /*  1180 */   745,  767,  767,  327,  230,  824,  170,  849,  238,  847,
 /*  1190 */   344,  846,  350,  362,  351,  160,  244,  627,  247,  293,
 /*  1200 */   672,  656,  655,  669,  252,  937,  723,  255,  756,  266,
 /*  1210 */   705,  398,  271,  272,  277,  639,  156,  880,  625,  624,
 /*  1220 */   125,  626,  877,  821,  916,  820,  137,  126,  119,  742,
 /*  1230 */   330,   55,   64,  834,  335,  235,  355,  149,  368,  191,
 /*  1240 */   146,  128,  198,  199,  200,  304,  130,  653,  131,  675,
 /*  1250 */   353,  288,  383,  132,  133,  674,   63,    6,  141,  752,
 /*  1260 */   801,  673,   71,  308,   94,  647,  388,  289,  646,  851,
 /*  1270 */   666,   65,  815,  386,  254,  645,  665,  889,   21,  878,
 /*  1280 */   226,  617,  620,  223,  315,  225,  414,  622,  413,  227,
 /*  1290 */   621,  618,  401,  284,  181,  183,  318,  108,  829,  827,
 /*  1300 */   405,  185,  751,  116,  117,  122,  127,  129,  685,  190,
 /*  1310 */   118,  837,  713,  210,  134,  845,  912,  334,  136,  135,
 /*  1320 */   105,  138,  206,   56,  259,   57,  276,  714,  261,  695,
 /*  1330 */   712,  263,  273,  711,  274,  265,  275,   58,   59,  213,
 /*  1340 */   848,  123,  194,  196,  844,    8,   12,  150,  197,  630,
 /*  1350 */   243,  214,  215,  354,  256,  201,  358,  142,  363,   60,
 /*  1360 */    13,  208,  664,  250,   14,   61,  120,  770,  769,  693,
 /*  1370 */   172,  799,   15,    4,   62,  697,  209,  173,  376,  175,
 /*  1380 */   144,   16,  724,  719,   69,   66,  814,   17,  800,  798,
 /*  1390 */   854,  393,  803,  853,  394,  168,  400,  870,  152,  153,
 /*  1400 */   217,  871,  218,  154,  404,  802,  768,  637,   79,  280,
 /*  1410 */   631,  614, 1175,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*    10 */    15,   16,   17,   18,   19,   20,   32,   22,   23,   24,
 /*    20 */    25,   26,   27,   28,   29,   30,   31,   32,   17,   18,
 /*    30 */    19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
 /*    40 */    29,   30,   31,   32,   49,  134,   51,   17,   18,   19,
 /*    50 */    20,  161,   22,   23,   24,   25,   26,   27,   28,   29,
 /*    60 */    30,   31,   32,   26,   27,   28,   29,   30,   31,   32,
 /*    70 */    75,   28,   29,   30,   31,   32,    5,    6,    7,    8,
 /*    80 */     9,   10,   11,   12,   13,   14,   15,   16,   17,   18,
 /*    90 */    19,   20,  161,   22,   23,   24,   25,   26,   27,   28,
 /*   100 */    29,   30,   31,   32,    5,    6,    7,    8,    9,   10,
 /*   110 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   120 */   164,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   130 */    31,   32,  152,    5,    6,    7,    8,    9,   10,   11,
 /*   140 */    12,   13,   14,   15,   16,   17,   18,   19,   20,   50,
 /*   150 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   160 */    32,    9,   10,   11,   12,  185,   67,  211,  212,  213,
 /*   170 */    26,   27,  168,  169,  164,   94,   48,   96,   97,  108,
 /*   180 */   109,  177,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   190 */    13,   14,   15,   16,   17,   18,   19,   20,  144,   22,
 /*   200 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   210 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   220 */    32,  211,  212,  213,  161,   48,  137,  138,  139,   38,
 /*   230 */    78,    5,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   240 */    14,   15,   16,   17,   18,   19,   20,   28,   22,   23,
 /*   250 */    24,   25,   26,   27,   28,   29,   30,   31,   32,  115,
 /*   260 */    69,  117,  208,  140,  141,   48,  174,   50,   73,  146,
 /*   270 */    75,  148,   51,   78,   48,  152,   85,   86,   87,  170,
 /*   280 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*   290 */    15,   16,   17,   18,   19,   20,   75,   22,   23,   24,
 /*   300 */    25,   26,   27,   28,   29,   30,   31,   32,  185,  144,
 /*   310 */   115,  116,  117,   76,   95,  144,   79,   80,   81,  100,
 /*   320 */   103,  104,  202,   48,  153,  134,  161,  162,   91,    5,
 /*   330 */     6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
 /*   340 */    16,   17,   18,   19,   20,   69,   22,   23,   24,   25,
 /*   350 */    26,   27,   28,   29,   30,   31,   32,  199,   98,   99,
 /*   360 */   160,   85,   86,   87,  211,  212,  213,  144,  168,  211,
 /*   370 */   212,  213,   48,  150,   98,   99,  153,    7,    5,    6,
 /*   380 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   390 */    17,   18,   19,   20,  134,   22,   23,   24,   25,   26,
 /*   400 */    27,   28,   29,   30,   31,   32,    5,    6,    7,    8,
 /*   410 */     9,   10,   11,   12,   13,   14,   15,   16,   17,   18,
 /*   420 */    19,   20,   52,   22,   23,   24,   25,   26,   27,   28,
 /*   430 */    29,   30,   31,   32,    5,    6,    7,    8,    9,   10,
 /*   440 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   450 */   170,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   460 */    31,   32,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   470 */    13,   14,   15,   16,   17,   18,   19,   20,   49,   22,
 /*   480 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   490 */   157,  158,  159,    1,    2,  122,    6,    7,    8,    9,
 /*   500 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   510 */    20,  170,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   520 */    30,   31,   32,  122,  157,  158,  159,   51,   52,    5,
 /*   530 */     6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
 /*   540 */    16,   17,   18,   19,   20,  199,   22,   23,   24,   25,
 /*   550 */    26,   27,   28,   29,   30,   31,   32,    7,    8,    9,
 /*   560 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   570 */    20,    7,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   580 */    30,   31,   32,  183,  184,   85,   86,   87,    7,   47,
 /*   590 */    26,   27,  144,   51,   52,  195,   34,   33,  144,   37,
 /*   600 */    38,   39,   40,   41,   69,    7,   44,   31,   32,  144,
 /*   610 */    46,   47,   48,   32,  144,   51,   52,  144,   51,   52,
 /*   620 */    85,   86,   87,  152,   26,   27,  161,  162,  158,  159,
 /*   630 */   182,   33,   68,   71,  161,  162,  206,   73,  218,   75,
 /*   640 */   175,  221,   78,  144,   46,   47,  144,  144,  175,   51,
 /*   650 */    52,   49,  206,   51,   92,  152,  185,   76,   77,   78,
 /*   660 */    79,   80,   81,   82,  161,  162,   68,  103,  180,  134,
 /*   670 */    89,   73,  144,   75,  144,  176,   78,   75,  230,  115,
 /*   680 */   116,  117,  118,  119,  120,   50,  232,  125,  185,  223,
 /*   690 */   220,  161,  162,  131,  132,   77,  134,  144,  134,  196,
 /*   700 */   197,  103,  135,  144,   56,  144,   58,  144,    7,    7,
 /*   710 */     9,  152,   64,  115,  116,  117,  118,  119,  120,  144,
 /*   720 */   161,  162,  161,  162,  161,  162,  196,  197,   26,   27,
 /*   730 */    28,  144,  134,  144,  232,   33,  161,  162,  103,  104,
 /*   740 */    26,   27,   47,    7,  185,  144,   51,   52,   46,   47,
 /*   750 */   161,  162,  134,   51,   52,  196,  228,  196,  197,  196,
 /*   760 */   197,  144,   26,   27,  175,   51,   52,   51,   52,   33,
 /*   770 */    68,  196,  197,  152,  144,   73,  144,   75,  161,  162,
 /*   780 */    78,  228,   46,   47,  121,   84,  123,   51,   52,   75,
 /*   790 */    89,   77,   76,  161,  162,   79,   80,   81,   84,   48,
 /*   800 */    86,   50,   39,  224,   68,  103,  185,   91,  207,   73,
 /*   810 */   144,   75,  144,  196,   78,  228,   53,  115,  116,  117,
 /*   820 */   118,  119,  120,   50,   61,   62,  144,  144,  196,  115,
 /*   830 */   209,  117,   69,  144,  183,  184,   26,   27,  208,  103,
 /*   840 */   158,  159,  176,  222,  161,  162,  195,    7,  227,  152,
 /*   850 */   182,  115,  116,  117,  118,  119,  120,   47,  175,  144,
 /*   860 */   209,   51,   52,  199,   43,  144,   26,   27,   85,   86,
 /*   870 */    87,  152,  144,   33,  153,  102,  161,  162,   59,   51,
 /*   880 */    52,  144,  185,   73,   65,   75,   46,   47,   78,   70,
 /*   890 */   199,   51,   52,  172,   47,  174,   49,  199,   51,   51,
 /*   900 */    52,   80,  220,   43,  185,   47,  209,   88,   68,   51,
 /*   910 */    52,  144,  144,   73,   93,   75,  144,   28,   78,  200,
 /*   920 */   201,  144,   75,   39,  227,  115,  116,  117,  161,  162,
 /*   930 */   215,  103,  104,  161,  162,   98,   99,   53,  161,  162,
 /*   940 */    51,   52,  144,  103,   47,   61,   62,  126,   51,  144,
 /*   950 */   182,  103,  104,   93,  184,  115,  116,  117,  118,  119,
 /*   960 */   120,  144,  144,  187,  144,  195,  161,  162,  179,  180,
 /*   970 */   144,   95,   75,  144,  144,  144,  100,  144,  161,  162,
 /*   980 */   144,  161,  162,  144,  124,  144,  126,  161,  162,  144,
 /*   990 */   161,  162,  161,  162,  161,  162,  144,  161,  162,  144,
 /*  1000 */   182,  144,  161,  162,   50,  144,  161,  162,  144,  144,
 /*  1010 */   188,  144,  182,  161,  162,  144,  161,  162,  161,  162,
 /*  1020 */   198,  144,  161,  162,  144,  144,  161,  162,  161,  162,
 /*  1030 */   144,  144,  144,   47,  144,   49,  144,   83,  161,  162,
 /*  1040 */   144,  161,  162,  144,   78,  144,  207,  161,  162,  161,
 /*  1050 */   162,  161,  162,  161,  162,   89,  144,  161,  162,  144,
 /*  1060 */   161,  162,  161,  162,  144,  144,  144,  144,  144,  182,
 /*  1070 */   144,  144,  144,  161,  162,  144,  161,  162,  144,  179,
 /*  1080 */   180,  161,  162,  161,  162,  161,  162,  161,  162,  161,
 /*  1090 */   162,  144,  161,  162,  144,  161,  162,  144,   47,   50,
 /*  1100 */   144,  144,   51,   52,  144,  182,  155,  156,  161,  162,
 /*  1110 */   152,  161,  162,  144,  161,  162,    7,  161,  162,  152,
 /*  1120 */   155,  156,  152,  152,    9,  152,  108,  109,  207,  200,
 /*  1130 */   201,   48,   16,   50,  207,   48,   48,   50,   50,  182,
 /*  1140 */    51,   52,  182,  185,   48,   48,   50,   50,   48,   36,
 /*  1150 */    50,   47,  185,   77,   78,  185,  185,  144,  185,   51,
 /*  1160 */    52,   52,  113,   47,  186,   48,   48,   50,   50,   48,
 /*  1170 */   115,   50,  117,  115,  144,  117,  207,   48,  144,   50,
 /*  1180 */   190,   51,   52,  203,  203,  144,  144,  190,  229,  144,
 /*  1190 */   229,  144,  144,  165,  144,  173,  144,  144,  144,   84,
 /*  1200 */   144,  144,  169,   90,  144,  101,  144,  164,  144,  203,
 /*  1210 */   144,  217,  144,  144,  189,  144,  187,  147,  144,  144,
 /*  1220 */   231,  144,  144,  164,  133,  164,   47,  210,    5,  190,
 /*  1230 */    45,  121,  113,  226,  128,  225,   45,  210,   84,  149,
 /*  1240 */    47,  178,  149,  149,  149,   63,  181,   83,  181,  163,
 /*  1250 */   166,  166,  106,  181,  181,  163,   84,   47,  178,  178,
 /*  1260 */   214,  163,  121,   32,  112,  163,  107,  166,  165,  190,
 /*  1270 */   171,  111,  190,  110,  163,  163,  171,  163,   50,   40,
 /*  1280 */    35,    4,   36,  145,    3,  145,  143,  143,  151,   55,
 /*  1290 */   143,  143,  166,  142,   42,   84,   72,   43,   48,   48,
 /*  1300 */   166,  101,   99,  154,  154,   88,  114,  102,   46,   84,
 /*  1310 */   154,  127,  205,   50,  127,    1,  130,  129,  102,   84,
 /*  1320 */   167,  114,  167,   16,  204,   16,  190,  205,  204,  194,
 /*  1330 */   205,  204,  193,  205,  192,  204,  191,   16,   16,  216,
 /*  1340 */    52,   88,  105,  101,    1,   34,   47,   49,   84,   46,
 /*  1350 */   124,  219,  219,    7,   89,   82,   66,   47,   66,   47,
 /*  1360 */    47,   66,   54,   48,   47,   47,   60,   48,   48,   95,
 /*  1370 */   101,   48,   47,   47,   50,   48,  105,   48,   50,   48,
 /*  1380 */    47,  105,   52,  104,   50,   50,   48,  105,   48,   48,
 /*  1390 */    48,   75,   38,   48,   50,   47,   49,   48,   47,   47,
 /*  1400 */    50,   48,  101,   47,   49,   48,   48,   48,   47,   42,
 /*  1410 */    48,    1,    0,
};
#define YY_SHIFT_USE_DFLT (1413)
#define YY_SHIFT_COUNT    (416)
#define YY_SHIFT_MIN      (-89)
#define YY_SHIFT_MAX      (1412)
static const short yy_shift_ofst[] = {
 /*     0 */   492,  564,  598,  562,  736,  736,  736,  736,  535,   -5,
 /*    10 */    71,   71,  736,  736,  736,  736,  736,  736,  736,  714,
 /*    20 */   714,  716,  276,  191,  260,   99,  128,  177,  226,  275,
 /*    30 */   324,  373,  401,  429,  457,  457,  457,  457,  457,  457,
 /*    40 */   457,  457,  457,  457,  457,  457,  457,  457,  457,  524,
 /*    50 */   457,  490,  550,  550,  702,  736,  736,  736,  736,  736,
 /*    60 */   736,  736,  736,  736,  736,  736,  736,  736,  736,  736,
 /*    70 */   736,  736,  736,  736,  736,  736,  736,  736,  736,  736,
 /*    80 */   736,  736,  840,  736,  736,  736,  736,  736,  736,  736,
 /*    90 */   736,  736,  736,  736,  736,  736,   11,   30,   30,   30,
 /*   100 */    30,   30,  188,   37,   43,  701,  144,  144,  476,  576,
 /*   110 */   837,  476,  -16, 1413, 1413, 1413,  581,  581,  581,  763,
 /*   120 */   763,  821,  542,  542,  567,  476,  618,  476,  476,  476,
 /*   130 */   476,  476,  476,  476,  476,  476,  476,  476,  476,  476,
 /*   140 */   476,  476,  476,  221,  476,  476,  476,  221,  837,  -89,
 /*   150 */   -89,  -89,  -89,  -89,  -89, 1413, 1413,  810,  195,  195,
 /*   160 */   237,  819,  819,  819,  217,  847,  828,  848,  884,  500,
 /*   170 */   648,  695,  889,  602,  602,  602,  858,  635, 1051,   81,
 /*   180 */   219,  476,  476,  476,  476,  476,  476, 1049,  370,  370,
 /*   190 */   476,  476, 1109, 1049,  476, 1109,  476,  476,  476,  476,
 /*   200 */   476,  476,  954,  476,  751,  476, 1115,  476, 1018,  476,
 /*   210 */   476,  370,  476,  663, 1018, 1018,  476,  476,  476,  773,
 /*   220 */   876,  476,  986,  476,  476,  476,  476, 1091, 1179, 1223,
 /*   230 */  1119, 1185, 1185, 1185, 1185, 1110, 1106, 1191, 1119, 1179,
 /*   240 */  1223, 1223, 1119, 1191, 1193, 1191, 1191, 1193, 1154, 1154,
 /*   250 */  1154, 1182, 1193, 1154, 1164, 1154, 1182, 1154, 1154, 1146,
 /*   260 */  1172, 1146, 1172, 1146, 1172, 1146, 1172, 1210, 1141, 1193,
 /*   270 */  1231, 1231, 1193, 1152, 1159, 1160, 1163, 1119, 1228, 1239,
 /*   280 */  1239, 1245, 1245, 1245, 1245, 1246, 1413, 1413, 1413, 1413,
 /*   290 */  1413,  152,  860,  783,  897, 1116, 1083, 1087, 1088, 1096,
 /*   300 */  1097, 1100, 1089, 1076, 1113,  966, 1117, 1118, 1108, 1121,
 /*   310 */  1055, 1058, 1129, 1130, 1104, 1277, 1281, 1234, 1252, 1224,
 /*   320 */  1254, 1211, 1250, 1251, 1200, 1203, 1192, 1217, 1205, 1225,
 /*   330 */  1262, 1184, 1263, 1187, 1186, 1188, 1235, 1314, 1216, 1207,
 /*   340 */  1307, 1309, 1321, 1322, 1253, 1288, 1237, 1242, 1343, 1311,
 /*   350 */  1299, 1264, 1226, 1298, 1303, 1346, 1265, 1273, 1310, 1290,
 /*   360 */  1312, 1313, 1315, 1317, 1292, 1308, 1318, 1295, 1306, 1319,
 /*   370 */  1320, 1323, 1324, 1274, 1325, 1327, 1326, 1328, 1269, 1329,
 /*   380 */  1331, 1330, 1271, 1333, 1279, 1334, 1276, 1335, 1282, 1338,
 /*   390 */  1334, 1340, 1341, 1342, 1316, 1344, 1345, 1348, 1354, 1349,
 /*   400 */  1351, 1347, 1350, 1353, 1352, 1355, 1350, 1357, 1356, 1358,
 /*   410 */  1359, 1361, 1301, 1362, 1367, 1410, 1412,
};
#define YY_REDUCE_USE_DFLT (-111)
#define YY_REDUCE_COUNT (290)
#define YY_REDUCE_MIN   (-110)
#define YY_REDUCE_MAX   (1156)
static const short yy_reduce_ofst[] = {
 /*     0 */    89,  503,  559,  123,  530,  561,  563,  575,  621,  158,
 /*    10 */   -44,   10,  465,  473,  589,  617,  632,  683,  715,  470,
 /*    20 */   682,  721,  651,  697,  719,  153,  153,  153,  153,  153,
 /*    30 */   153,  153,  153,  153,  153,  153,  153,  153,  153,  153,
 /*    40 */   153,  153,  153,  153,  153,  153,  153,  153,  153,  153,
 /*    50 */   153,  153,  153,  153,  165,  767,  772,  777,  805,  817,
 /*    60 */   820,  826,  829,  831,  833,  836,  841,  845,  852,  855,
 /*    70 */   857,  861,  865,  867,  877,  880,  886,  888,  890,  892,
 /*    80 */   896,  899,  901,  912,  915,  920,  922,  924,  926,  928,
 /*    90 */   931,  934,  947,  950,  953,  956,  153,  153,  153,  153,
 /*   100 */   153,  153,  153,  153,  153,    4,  333,  367,  448,  153,
 /*   110 */   400,  223,  153,  153,  153,  153,  200,  200,  200,  789,
 /*   120 */   900,  420,   54,  630,  454,  171,  -20,  668,  768,  818,
 /*   130 */   830,  887,  923,  957,  601,  960,  528,  839,  553,  921,
 /*   140 */   927,  587,  499,  951,  969,  502,  666,  965,  770,  471,
 /*   150 */   958,  967,  970,  971,  973,  929,  822, -110,  -69,   63,
 /*   160 */    92,  109,  280,  341,  120,  346,  430,  446,  488,  579,
 /*   170 */   466,  689,  728,  664,  691,  698,  737,  120,  798,  978,
 /*   180 */   776,  864,  871,  881, 1013, 1030, 1034,  990,  980,  981,
 /*   190 */  1041, 1042,  959,  997, 1045,  961, 1047, 1048, 1050, 1052,
 /*   200 */  1053, 1054, 1028, 1056, 1022, 1057, 1033, 1060, 1043, 1062,
 /*   210 */  1064, 1006, 1066,  994, 1059, 1061, 1068, 1069,  728, 1025,
 /*   220 */  1029, 1071, 1070, 1074, 1075, 1077, 1078,  989, 1017, 1063,
 /*   230 */  1039, 1065, 1067, 1072, 1073, 1007, 1010, 1090, 1079, 1027,
 /*   240 */  1080, 1081, 1082, 1093, 1084, 1094, 1095, 1085, 1086, 1092,
 /*   250 */  1098, 1099, 1101, 1102, 1103, 1111, 1105, 1112, 1114, 1107,
 /*   260 */  1120, 1122, 1124, 1125, 1127, 1128, 1131, 1046, 1123, 1126,
 /*   270 */  1132, 1133, 1134, 1135, 1139, 1142, 1145, 1136, 1137, 1138,
 /*   280 */  1140, 1143, 1144, 1147, 1148, 1151, 1149, 1150, 1153, 1155,
 /*   290 */  1156,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1176, 1170, 1170, 1170, 1113, 1113, 1113, 1113, 1170, 1009,
 /*    10 */  1036, 1036, 1220, 1220, 1220, 1220, 1220, 1220, 1112, 1220,
 /*    20 */  1220, 1220, 1220, 1170, 1013, 1042, 1220, 1220, 1220, 1114,
 /*    30 */  1115, 1220, 1220, 1220, 1146, 1052, 1051, 1050, 1049, 1023,
 /*    40 */  1047, 1040, 1044, 1114, 1108, 1109, 1107, 1111, 1115, 1220,
 /*    50 */  1043, 1077, 1092, 1076, 1220, 1220, 1220, 1220, 1220, 1220,
 /*    60 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*    70 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*    80 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*    90 */  1220, 1220, 1220, 1220, 1220, 1220, 1086, 1091, 1098, 1090,
 /*   100 */  1087, 1079, 1078, 1080, 1081,  980, 1220, 1220, 1220, 1082,
 /*   110 */  1220, 1220, 1083, 1095, 1094, 1093, 1168, 1185, 1184, 1220,
 /*   120 */  1220, 1120, 1220, 1220, 1220, 1220, 1170, 1220, 1220, 1220,
 /*   130 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*   140 */  1220, 1220, 1220,  938, 1220, 1220, 1220,  938, 1220, 1170,
 /*   150 */  1170, 1170, 1170, 1170, 1170, 1013, 1004, 1220, 1220, 1220,
 /*   160 */  1220, 1220, 1220, 1220, 1220, 1009, 1220, 1220, 1220, 1220,
 /*   170 */  1141, 1220, 1220, 1009, 1009, 1009, 1220, 1011, 1220,  993,
 /*   180 */  1003, 1220, 1165, 1220, 1162, 1220, 1220, 1046, 1025, 1025,
 /*   190 */  1220, 1220, 1217, 1046, 1220, 1217, 1220, 1220, 1220, 1220,
 /*   200 */  1220, 1220,  955, 1220, 1196, 1220,  952, 1220, 1036, 1220,
 /*   210 */  1220, 1025, 1220, 1110, 1036, 1036, 1220, 1220, 1220, 1010,
 /*   220 */  1003, 1220, 1220, 1220, 1220, 1220, 1179, 1218, 1057,  983,
 /*   230 */  1046,  989,  989,  989,  989, 1145, 1214,  932, 1046, 1057,
 /*   240 */   983,  983, 1046,  932, 1121,  932,  932, 1121,  981,  981,
 /*   250 */   981,  970, 1121,  981,  955,  981,  970,  981,  981, 1029,
 /*   260 */  1024, 1029, 1024, 1029, 1024, 1029, 1024, 1116, 1220, 1121,
 /*   270 */  1125, 1125, 1121, 1041, 1030, 1039, 1037, 1046,  973, 1182,
 /*   280 */  1182, 1178, 1178, 1178, 1178,  922, 1191, 1191,  957,  957,
 /*   290 */  1191, 1220, 1220, 1220, 1186, 1128, 1220, 1220, 1220, 1220,
 /*   300 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*   310 */  1220, 1220, 1220, 1220, 1063, 1220,  919, 1220, 1220, 1169,
 /*   320 */  1220, 1163, 1220, 1220, 1209, 1220, 1220, 1220, 1220, 1220,
 /*   330 */  1220, 1220, 1144, 1143, 1220, 1220, 1220, 1220, 1220, 1220,
 /*   340 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1216, 1220, 1220,
 /*   350 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*   360 */  1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*   370 */  1220, 1220, 1220,  995, 1220, 1220, 1220, 1200, 1220, 1220,
 /*   380 */  1220, 1220, 1220, 1220, 1220, 1038, 1220, 1031, 1220, 1220,
 /*   390 */  1206, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220, 1220,
 /*   400 */  1220, 1220, 1172, 1220, 1220, 1220, 1171, 1220, 1220, 1220,
 /*   410 */  1220, 1220, 1220, 1220,  926, 1220, 1220,
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
   51,  /*      QUERY => ID */
   51,  /*       PLAN => ID */
    0,  /*         OR => nothing */
    0,  /*        AND => nothing */
    0,  /*        NOT => nothing */
    0,  /*         IS => nothing */
   51,  /*      MATCH => ID */
    0,  /*    LIKE_KW => nothing */
    0,  /*    BETWEEN => nothing */
    0,  /*         IN => nothing */
   51,  /*     ISNULL => ID */
   51,  /*    NOTNULL => ID */
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
   51,  /*   DEFERRED => ID */
    0,  /*     COMMIT => nothing */
   51,  /*        END => ID */
    0,  /*   ROLLBACK => nothing */
    0,  /*  SAVEPOINT => nothing */
   51,  /*    RELEASE => ID */
    0,  /*         TO => nothing */
    0,  /*      TABLE => nothing */
    0,  /*     CREATE => nothing */
   51,  /*         IF => ID */
    0,  /*     EXISTS => nothing */
    0,  /*         LP => nothing */
    0,  /*         RP => nothing */
    0,  /*         AS => nothing */
    0,  /*      COMMA => nothing */
    0,  /*         ID => nothing */
   51,  /*    INDEXED => ID */
   51,  /*      ABORT => ID */
   51,  /*     ACTION => ID */
   51,  /*        ADD => ID */
   51,  /*      AFTER => ID */
   51,  /* AUTOINCREMENT => ID */
   51,  /*     BEFORE => ID */
   51,  /*    CASCADE => ID */
   51,  /*   CONFLICT => ID */
   51,  /*       FAIL => ID */
   51,  /*     IGNORE => ID */
   51,  /*  INITIALLY => ID */
   51,  /*    INSTEAD => ID */
   51,  /*         NO => ID */
   51,  /*        KEY => ID */
   51,  /*     OFFSET => ID */
   51,  /*      RAISE => ID */
   51,  /*    REPLACE => ID */
   51,  /*   RESTRICT => ID */
   51,  /*    REINDEX => ID */
   51,  /*     RENAME => ID */
   51,  /*   CTIME_KW => ID */
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
  "RP",            "AS",            "COMMA",         "ID",          
  "INDEXED",       "ABORT",         "ACTION",        "ADD",         
  "AFTER",         "AUTOINCREMENT",  "BEFORE",        "CASCADE",     
  "CONFLICT",      "FAIL",          "IGNORE",        "INITIALLY",   
  "INSTEAD",       "NO",            "KEY",           "OFFSET",      
  "RAISE",         "REPLACE",       "RESTRICT",      "REINDEX",     
  "RENAME",        "CTIME_KW",      "ANY",           "STRING",      
  "CONSTRAINT",    "DEFAULT",       "NULL",          "PRIMARY",     
  "UNIQUE",        "CHECK",         "REFERENCES",    "AUTOINCR",    
  "ON",            "INSERT",        "DELETE",        "UPDATE",      
  "SET",           "DEFERRABLE",    "IMMEDIATE",     "FOREIGN",     
  "DROP",          "VIEW",          "UNION",         "ALL",         
  "EXCEPT",        "INTERSECT",     "SELECT",        "VALUES",      
  "DISTINCT",      "DOT",           "FROM",          "JOIN_KW",     
  "JOIN",          "BY",            "USING",         "ORDER",       
  "ASC",           "DESC",          "GROUP",         "HAVING",      
  "LIMIT",         "WHERE",         "INTO",          "FLOAT",       
  "BLOB",          "INTEGER",       "VARIABLE",      "CAST",        
  "CASE",          "WHEN",          "THEN",          "ELSE",        
  "INDEX",         "PRAGMA",        "TRIGGER",       "OF",          
  "FOR",           "EACH",          "ROW",           "ANALYZE",     
  "ALTER",         "COLUMNKW",      "WITH",          "RECURSIVE",   
  "error",         "input",         "ecmd",          "explain",     
  "cmdx",          "cmd",           "transtype",     "trans_opt",   
  "nm",            "savepoint_opt",  "create_table",  "create_table_args",
  "createkw",      "ifnotexists",   "columnlist",    "conslist_opt",
  "select",        "columnname",    "carglist",      "typetoken",   
  "typename",      "signed",        "plus_num",      "minus_num",   
  "ccons",         "term",          "expr",          "onconf",      
  "sortorder",     "autoinc",       "eidlist_opt",   "refargs",     
  "defer_subclause",  "refarg",        "refact",        "init_deferred_pred_opt",
  "conslist",      "tconscomma",    "tcons",         "sortlist",    
  "eidlist",       "defer_subclause_opt",  "orconf",        "resolvetype", 
  "raisetype",     "ifexists",      "fullname",      "selectnowith",
  "oneselect",     "with",          "multiselect_op",  "distinct",    
  "selcollist",    "from",          "where_opt",     "groupby_opt", 
  "having_opt",    "orderby_opt",   "limit_opt",     "values",      
  "nexprlist",     "exprlist",      "sclp",          "as",          
  "seltablist",    "stl_prefix",    "joinop",        "indexed_opt", 
  "on_opt",        "using_opt",     "join_nm",       "idlist",      
  "setlist",       "insert_cmd",    "idlist_opt",    "likeop",      
  "between_op",    "in_op",         "paren_exprlist",  "case_operand",
  "case_exprlist",  "case_else",     "uniqueflag",    "collate",     
  "nmnum",         "trigger_decl",  "trigger_cmd_list",  "trigger_time",
  "trigger_event",  "foreach_clause",  "when_clause",   "trigger_cmd", 
  "trnm",          "tridxby",       "add_column_fullname",  "kwcolumn_opt",
  "wqlist",      
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
 /*  17 */ "create_table_args ::= LP columnlist conslist_opt RP",
 /*  18 */ "create_table_args ::= AS select",
 /*  19 */ "columnname ::= nm typetoken",
 /*  20 */ "nm ::= ID|INDEXED",
 /*  21 */ "typetoken ::=",
 /*  22 */ "typetoken ::= typename LP signed RP",
 /*  23 */ "typetoken ::= typename LP signed COMMA signed RP",
 /*  24 */ "typename ::= typename ID|STRING",
 /*  25 */ "ccons ::= CONSTRAINT nm",
 /*  26 */ "ccons ::= DEFAULT term",
 /*  27 */ "ccons ::= DEFAULT LP expr RP",
 /*  28 */ "ccons ::= DEFAULT PLUS term",
 /*  29 */ "ccons ::= DEFAULT MINUS term",
 /*  30 */ "ccons ::= DEFAULT ID|INDEXED",
 /*  31 */ "ccons ::= NOT NULL onconf",
 /*  32 */ "ccons ::= PRIMARY KEY sortorder onconf autoinc",
 /*  33 */ "ccons ::= UNIQUE onconf",
 /*  34 */ "ccons ::= CHECK LP expr RP",
 /*  35 */ "ccons ::= REFERENCES nm eidlist_opt refargs",
 /*  36 */ "ccons ::= defer_subclause",
 /*  37 */ "ccons ::= COLLATE ID|INDEXED",
 /*  38 */ "autoinc ::=",
 /*  39 */ "autoinc ::= AUTOINCR",
 /*  40 */ "refargs ::=",
 /*  41 */ "refargs ::= refargs refarg",
 /*  42 */ "refarg ::= MATCH nm",
 /*  43 */ "refarg ::= ON INSERT refact",
 /*  44 */ "refarg ::= ON DELETE refact",
 /*  45 */ "refarg ::= ON UPDATE refact",
 /*  46 */ "refact ::= SET NULL",
 /*  47 */ "refact ::= SET DEFAULT",
 /*  48 */ "refact ::= CASCADE",
 /*  49 */ "refact ::= RESTRICT",
 /*  50 */ "refact ::= NO ACTION",
 /*  51 */ "defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt",
 /*  52 */ "defer_subclause ::= DEFERRABLE init_deferred_pred_opt",
 /*  53 */ "init_deferred_pred_opt ::=",
 /*  54 */ "init_deferred_pred_opt ::= INITIALLY DEFERRED",
 /*  55 */ "init_deferred_pred_opt ::= INITIALLY IMMEDIATE",
 /*  56 */ "conslist_opt ::=",
 /*  57 */ "tconscomma ::= COMMA",
 /*  58 */ "tcons ::= CONSTRAINT nm",
 /*  59 */ "tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf",
 /*  60 */ "tcons ::= UNIQUE LP sortlist RP onconf",
 /*  61 */ "tcons ::= CHECK LP expr RP onconf",
 /*  62 */ "tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt",
 /*  63 */ "defer_subclause_opt ::=",
 /*  64 */ "onconf ::=",
 /*  65 */ "onconf ::= ON CONFLICT resolvetype",
 /*  66 */ "orconf ::=",
 /*  67 */ "orconf ::= OR resolvetype",
 /*  68 */ "resolvetype ::= IGNORE",
 /*  69 */ "resolvetype ::= REPLACE",
 /*  70 */ "cmd ::= DROP TABLE ifexists fullname",
 /*  71 */ "ifexists ::= IF EXISTS",
 /*  72 */ "ifexists ::=",
 /*  73 */ "cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select",
 /*  74 */ "cmd ::= DROP VIEW ifexists fullname",
 /*  75 */ "cmd ::= select",
 /*  76 */ "select ::= with selectnowith",
 /*  77 */ "selectnowith ::= selectnowith multiselect_op oneselect",
 /*  78 */ "multiselect_op ::= UNION",
 /*  79 */ "multiselect_op ::= UNION ALL",
 /*  80 */ "multiselect_op ::= EXCEPT|INTERSECT",
 /*  81 */ "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
 /*  82 */ "values ::= VALUES LP nexprlist RP",
 /*  83 */ "values ::= values COMMA LP exprlist RP",
 /*  84 */ "distinct ::= DISTINCT",
 /*  85 */ "distinct ::= ALL",
 /*  86 */ "distinct ::=",
 /*  87 */ "sclp ::=",
 /*  88 */ "selcollist ::= sclp expr as",
 /*  89 */ "selcollist ::= sclp STAR",
 /*  90 */ "selcollist ::= sclp nm DOT STAR",
 /*  91 */ "as ::= AS nm",
 /*  92 */ "as ::=",
 /*  93 */ "from ::=",
 /*  94 */ "from ::= FROM seltablist",
 /*  95 */ "stl_prefix ::= seltablist joinop",
 /*  96 */ "stl_prefix ::=",
 /*  97 */ "seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt",
 /*  98 */ "seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt",
 /*  99 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
 /* 100 */ "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
 /* 101 */ "fullname ::= nm",
 /* 102 */ "joinop ::= COMMA|JOIN",
 /* 103 */ "joinop ::= JOIN_KW JOIN",
 /* 104 */ "joinop ::= JOIN_KW join_nm JOIN",
 /* 105 */ "joinop ::= JOIN_KW join_nm join_nm JOIN",
 /* 106 */ "on_opt ::= ON expr",
 /* 107 */ "on_opt ::=",
 /* 108 */ "indexed_opt ::=",
 /* 109 */ "indexed_opt ::= INDEXED BY nm",
 /* 110 */ "indexed_opt ::= NOT INDEXED",
 /* 111 */ "using_opt ::= USING LP idlist RP",
 /* 112 */ "using_opt ::=",
 /* 113 */ "orderby_opt ::=",
 /* 114 */ "orderby_opt ::= ORDER BY sortlist",
 /* 115 */ "sortlist ::= sortlist COMMA expr sortorder",
 /* 116 */ "sortlist ::= expr sortorder",
 /* 117 */ "sortorder ::= ASC",
 /* 118 */ "sortorder ::= DESC",
 /* 119 */ "sortorder ::=",
 /* 120 */ "groupby_opt ::=",
 /* 121 */ "groupby_opt ::= GROUP BY nexprlist",
 /* 122 */ "having_opt ::=",
 /* 123 */ "having_opt ::= HAVING expr",
 /* 124 */ "limit_opt ::=",
 /* 125 */ "limit_opt ::= LIMIT expr",
 /* 126 */ "limit_opt ::= LIMIT expr OFFSET expr",
 /* 127 */ "limit_opt ::= LIMIT expr COMMA expr",
 /* 128 */ "cmd ::= with DELETE FROM fullname indexed_opt where_opt",
 /* 129 */ "where_opt ::=",
 /* 130 */ "where_opt ::= WHERE expr",
 /* 131 */ "cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt",
 /* 132 */ "setlist ::= setlist COMMA nm EQ expr",
 /* 133 */ "setlist ::= setlist COMMA LP idlist RP EQ expr",
 /* 134 */ "setlist ::= nm EQ expr",
 /* 135 */ "setlist ::= LP idlist RP EQ expr",
 /* 136 */ "cmd ::= with insert_cmd INTO fullname idlist_opt select",
 /* 137 */ "cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES",
 /* 138 */ "insert_cmd ::= INSERT orconf",
 /* 139 */ "insert_cmd ::= REPLACE",
 /* 140 */ "idlist_opt ::=",
 /* 141 */ "idlist_opt ::= LP idlist RP",
 /* 142 */ "idlist ::= idlist COMMA nm",
 /* 143 */ "idlist ::= nm",
 /* 144 */ "expr ::= LP expr RP",
 /* 145 */ "term ::= NULL",
 /* 146 */ "expr ::= ID|INDEXED",
 /* 147 */ "expr ::= JOIN_KW",
 /* 148 */ "expr ::= nm DOT nm",
 /* 149 */ "term ::= FLOAT|BLOB",
 /* 150 */ "term ::= STRING",
 /* 151 */ "term ::= INTEGER",
 /* 152 */ "expr ::= VARIABLE",
 /* 153 */ "expr ::= expr COLLATE ID|INDEXED",
 /* 154 */ "expr ::= CAST LP expr AS typetoken RP",
 /* 155 */ "expr ::= ID|INDEXED LP distinct exprlist RP",
 /* 156 */ "expr ::= ID|INDEXED LP STAR RP",
 /* 157 */ "term ::= CTIME_KW",
 /* 158 */ "expr ::= LP nexprlist COMMA expr RP",
 /* 159 */ "expr ::= expr AND expr",
 /* 160 */ "expr ::= expr OR expr",
 /* 161 */ "expr ::= expr LT|GT|GE|LE expr",
 /* 162 */ "expr ::= expr EQ|NE expr",
 /* 163 */ "expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr",
 /* 164 */ "expr ::= expr PLUS|MINUS expr",
 /* 165 */ "expr ::= expr STAR|SLASH|REM expr",
 /* 166 */ "expr ::= expr CONCAT expr",
 /* 167 */ "likeop ::= LIKE_KW|MATCH",
 /* 168 */ "likeop ::= NOT LIKE_KW|MATCH",
 /* 169 */ "expr ::= expr likeop expr",
 /* 170 */ "expr ::= expr likeop expr ESCAPE expr",
 /* 171 */ "expr ::= expr ISNULL|NOTNULL",
 /* 172 */ "expr ::= expr NOT NULL",
 /* 173 */ "expr ::= expr IS expr",
 /* 174 */ "expr ::= expr IS NOT expr",
 /* 175 */ "expr ::= NOT expr",
 /* 176 */ "expr ::= BITNOT expr",
 /* 177 */ "expr ::= MINUS expr",
 /* 178 */ "expr ::= PLUS expr",
 /* 179 */ "between_op ::= BETWEEN",
 /* 180 */ "between_op ::= NOT BETWEEN",
 /* 181 */ "expr ::= expr between_op expr AND expr",
 /* 182 */ "in_op ::= IN",
 /* 183 */ "in_op ::= NOT IN",
 /* 184 */ "expr ::= expr in_op LP exprlist RP",
 /* 185 */ "expr ::= LP select RP",
 /* 186 */ "expr ::= expr in_op LP select RP",
 /* 187 */ "expr ::= expr in_op nm paren_exprlist",
 /* 188 */ "expr ::= EXISTS LP select RP",
 /* 189 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 190 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 191 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 192 */ "case_else ::= ELSE expr",
 /* 193 */ "case_else ::=",
 /* 194 */ "case_operand ::= expr",
 /* 195 */ "case_operand ::=",
 /* 196 */ "exprlist ::=",
 /* 197 */ "nexprlist ::= nexprlist COMMA expr",
 /* 198 */ "nexprlist ::= expr",
 /* 199 */ "paren_exprlist ::=",
 /* 200 */ "paren_exprlist ::= LP exprlist RP",
 /* 201 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt",
 /* 202 */ "uniqueflag ::= UNIQUE",
 /* 203 */ "uniqueflag ::=",
 /* 204 */ "eidlist_opt ::=",
 /* 205 */ "eidlist_opt ::= LP eidlist RP",
 /* 206 */ "eidlist ::= eidlist COMMA nm collate sortorder",
 /* 207 */ "eidlist ::= nm collate sortorder",
 /* 208 */ "collate ::=",
 /* 209 */ "collate ::= COLLATE ID|INDEXED",
 /* 210 */ "cmd ::= DROP INDEX ifexists fullname ON nm",
 /* 211 */ "cmd ::= PRAGMA nm",
 /* 212 */ "cmd ::= PRAGMA nm EQ nmnum",
 /* 213 */ "cmd ::= PRAGMA nm LP nmnum RP",
 /* 214 */ "cmd ::= PRAGMA nm EQ minus_num",
 /* 215 */ "cmd ::= PRAGMA nm LP minus_num RP",
 /* 216 */ "cmd ::= PRAGMA nm EQ nm DOT nm",
 /* 217 */ "plus_num ::= PLUS INTEGER|FLOAT",
 /* 218 */ "minus_num ::= MINUS INTEGER|FLOAT",
 /* 219 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
 /* 220 */ "trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause",
 /* 221 */ "trigger_time ::= BEFORE",
 /* 222 */ "trigger_time ::= AFTER",
 /* 223 */ "trigger_time ::= INSTEAD OF",
 /* 224 */ "trigger_time ::=",
 /* 225 */ "trigger_event ::= DELETE|INSERT",
 /* 226 */ "trigger_event ::= UPDATE",
 /* 227 */ "trigger_event ::= UPDATE OF idlist",
 /* 228 */ "when_clause ::=",
 /* 229 */ "when_clause ::= WHEN expr",
 /* 230 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
 /* 231 */ "trigger_cmd_list ::= trigger_cmd SEMI",
 /* 232 */ "trnm ::= nm DOT nm",
 /* 233 */ "tridxby ::= INDEXED BY nm",
 /* 234 */ "tridxby ::= NOT INDEXED",
 /* 235 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 236 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
 /* 237 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 238 */ "trigger_cmd ::= select",
 /* 239 */ "expr ::= RAISE LP IGNORE RP",
 /* 240 */ "expr ::= RAISE LP raisetype COMMA STRING RP",
 /* 241 */ "raisetype ::= ROLLBACK",
 /* 242 */ "raisetype ::= ABORT",
 /* 243 */ "raisetype ::= FAIL",
 /* 244 */ "cmd ::= DROP TRIGGER ifexists fullname",
 /* 245 */ "cmd ::= REINDEX",
 /* 246 */ "cmd ::= REINDEX nm",
 /* 247 */ "cmd ::= REINDEX nm ON nm",
 /* 248 */ "cmd ::= ANALYZE",
 /* 249 */ "cmd ::= ANALYZE nm",
 /* 250 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
 /* 251 */ "cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist",
 /* 252 */ "add_column_fullname ::= fullname",
 /* 253 */ "with ::=",
 /* 254 */ "with ::= WITH wqlist",
 /* 255 */ "with ::= WITH RECURSIVE wqlist",
 /* 256 */ "wqlist ::= nm eidlist_opt AS LP select RP",
 /* 257 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
 /* 258 */ "input ::= ecmd",
 /* 259 */ "explain ::=",
 /* 260 */ "cmdx ::= cmd",
 /* 261 */ "trans_opt ::=",
 /* 262 */ "trans_opt ::= TRANSACTION",
 /* 263 */ "trans_opt ::= TRANSACTION nm",
 /* 264 */ "savepoint_opt ::= SAVEPOINT",
 /* 265 */ "savepoint_opt ::=",
 /* 266 */ "cmd ::= create_table create_table_args",
 /* 267 */ "columnlist ::= columnlist COMMA columnname carglist",
 /* 268 */ "columnlist ::= columnname carglist",
 /* 269 */ "typetoken ::= typename",
 /* 270 */ "typename ::= ID|STRING",
 /* 271 */ "signed ::= plus_num",
 /* 272 */ "signed ::= minus_num",
 /* 273 */ "carglist ::= carglist ccons",
 /* 274 */ "carglist ::=",
 /* 275 */ "ccons ::= NULL onconf",
 /* 276 */ "conslist_opt ::= COMMA conslist",
 /* 277 */ "conslist ::= conslist tconscomma tcons",
 /* 278 */ "conslist ::= tcons",
 /* 279 */ "tconscomma ::=",
 /* 280 */ "defer_subclause_opt ::= defer_subclause",
 /* 281 */ "resolvetype ::= raisetype",
 /* 282 */ "selectnowith ::= oneselect",
 /* 283 */ "oneselect ::= values",
 /* 284 */ "sclp ::= selcollist COMMA",
 /* 285 */ "as ::= ID|STRING",
 /* 286 */ "join_nm ::= ID|INDEXED",
 /* 287 */ "join_nm ::= JOIN_KW",
 /* 288 */ "expr ::= term",
 /* 289 */ "exprlist ::= nexprlist",
 /* 290 */ "nmnum ::= plus_num",
 /* 291 */ "nmnum ::= STRING",
 /* 292 */ "nmnum ::= nm",
 /* 293 */ "nmnum ::= ON",
 /* 294 */ "nmnum ::= DELETE",
 /* 295 */ "nmnum ::= DEFAULT",
 /* 296 */ "plus_num ::= INTEGER|FLOAT",
 /* 297 */ "foreach_clause ::=",
 /* 298 */ "foreach_clause ::= FOR EACH ROW",
 /* 299 */ "trnm ::= nm",
 /* 300 */ "tridxby ::=",
 /* 301 */ "kwcolumn_opt ::=",
 /* 302 */ "kwcolumn_opt ::= COLUMNKW",
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
    case 152: /* select */
    case 183: /* selectnowith */
    case 184: /* oneselect */
    case 195: /* values */
{
#line 386 "parse.y"
sqlite3SelectDelete(pParse->db, (yypminor->yy309));
#line 1481 "parse.c"
}
      break;
    case 161: /* term */
    case 162: /* expr */
{
#line 829 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy200).pExpr);
#line 1489 "parse.c"
}
      break;
    case 166: /* eidlist_opt */
    case 175: /* sortlist */
    case 176: /* eidlist */
    case 188: /* selcollist */
    case 191: /* groupby_opt */
    case 193: /* orderby_opt */
    case 196: /* nexprlist */
    case 197: /* exprlist */
    case 198: /* sclp */
    case 208: /* setlist */
    case 214: /* paren_exprlist */
    case 216: /* case_exprlist */
{
#line 1261 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy272));
#line 1507 "parse.c"
}
      break;
    case 182: /* fullname */
    case 189: /* from */
    case 200: /* seltablist */
    case 201: /* stl_prefix */
{
#line 613 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy73));
#line 1517 "parse.c"
}
      break;
    case 185: /* with */
    case 232: /* wqlist */
{
#line 1506 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy445));
#line 1525 "parse.c"
}
      break;
    case 190: /* where_opt */
    case 192: /* having_opt */
    case 204: /* on_opt */
    case 215: /* case_operand */
    case 217: /* case_else */
    case 226: /* when_clause */
{
#line 738 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy294));
#line 1537 "parse.c"
}
      break;
    case 205: /* using_opt */
    case 207: /* idlist */
    case 210: /* idlist_opt */
{
#line 650 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy436));
#line 1546 "parse.c"
}
      break;
    case 222: /* trigger_cmd_list */
    case 227: /* trigger_cmd */
{
#line 1381 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy251));
#line 1554 "parse.c"
}
      break;
    case 224: /* trigger_event */
{
#line 1367 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy60).b);
#line 1561 "parse.c"
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
#line 1736 "parse.c"
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
  { 138, 3 },
  { 138, 1 },
  { 139, 1 },
  { 139, 3 },
  { 141, 3 },
  { 142, 0 },
  { 142, 1 },
  { 141, 2 },
  { 141, 2 },
  { 141, 2 },
  { 141, 2 },
  { 141, 3 },
  { 141, 5 },
  { 146, 4 },
  { 148, 1 },
  { 149, 0 },
  { 149, 3 },
  { 147, 4 },
  { 147, 2 },
  { 153, 2 },
  { 144, 1 },
  { 155, 0 },
  { 155, 4 },
  { 155, 6 },
  { 156, 2 },
  { 160, 2 },
  { 160, 2 },
  { 160, 4 },
  { 160, 3 },
  { 160, 3 },
  { 160, 2 },
  { 160, 3 },
  { 160, 5 },
  { 160, 2 },
  { 160, 4 },
  { 160, 4 },
  { 160, 1 },
  { 160, 2 },
  { 165, 0 },
  { 165, 1 },
  { 167, 0 },
  { 167, 2 },
  { 169, 2 },
  { 169, 3 },
  { 169, 3 },
  { 169, 3 },
  { 170, 2 },
  { 170, 2 },
  { 170, 1 },
  { 170, 1 },
  { 170, 2 },
  { 168, 3 },
  { 168, 2 },
  { 171, 0 },
  { 171, 2 },
  { 171, 2 },
  { 151, 0 },
  { 173, 1 },
  { 174, 2 },
  { 174, 7 },
  { 174, 5 },
  { 174, 5 },
  { 174, 10 },
  { 177, 0 },
  { 163, 0 },
  { 163, 3 },
  { 178, 0 },
  { 178, 2 },
  { 179, 1 },
  { 179, 1 },
  { 141, 4 },
  { 181, 2 },
  { 181, 0 },
  { 141, 7 },
  { 141, 4 },
  { 141, 1 },
  { 152, 2 },
  { 183, 3 },
  { 186, 1 },
  { 186, 2 },
  { 186, 1 },
  { 184, 9 },
  { 195, 4 },
  { 195, 5 },
  { 187, 1 },
  { 187, 1 },
  { 187, 0 },
  { 198, 0 },
  { 188, 3 },
  { 188, 2 },
  { 188, 4 },
  { 199, 2 },
  { 199, 0 },
  { 189, 0 },
  { 189, 2 },
  { 201, 2 },
  { 201, 0 },
  { 200, 6 },
  { 200, 8 },
  { 200, 7 },
  { 200, 7 },
  { 182, 1 },
  { 202, 1 },
  { 202, 2 },
  { 202, 3 },
  { 202, 4 },
  { 204, 2 },
  { 204, 0 },
  { 203, 0 },
  { 203, 3 },
  { 203, 2 },
  { 205, 4 },
  { 205, 0 },
  { 193, 0 },
  { 193, 3 },
  { 175, 4 },
  { 175, 2 },
  { 164, 1 },
  { 164, 1 },
  { 164, 0 },
  { 191, 0 },
  { 191, 3 },
  { 192, 0 },
  { 192, 2 },
  { 194, 0 },
  { 194, 2 },
  { 194, 4 },
  { 194, 4 },
  { 141, 6 },
  { 190, 0 },
  { 190, 2 },
  { 141, 8 },
  { 208, 5 },
  { 208, 7 },
  { 208, 3 },
  { 208, 5 },
  { 141, 6 },
  { 141, 7 },
  { 209, 2 },
  { 209, 1 },
  { 210, 0 },
  { 210, 3 },
  { 207, 3 },
  { 207, 1 },
  { 162, 3 },
  { 161, 1 },
  { 162, 1 },
  { 162, 1 },
  { 162, 3 },
  { 161, 1 },
  { 161, 1 },
  { 161, 1 },
  { 162, 1 },
  { 162, 3 },
  { 162, 6 },
  { 162, 5 },
  { 162, 4 },
  { 161, 1 },
  { 162, 5 },
  { 162, 3 },
  { 162, 3 },
  { 162, 3 },
  { 162, 3 },
  { 162, 3 },
  { 162, 3 },
  { 162, 3 },
  { 162, 3 },
  { 211, 1 },
  { 211, 2 },
  { 162, 3 },
  { 162, 5 },
  { 162, 2 },
  { 162, 3 },
  { 162, 3 },
  { 162, 4 },
  { 162, 2 },
  { 162, 2 },
  { 162, 2 },
  { 162, 2 },
  { 212, 1 },
  { 212, 2 },
  { 162, 5 },
  { 213, 1 },
  { 213, 2 },
  { 162, 5 },
  { 162, 3 },
  { 162, 5 },
  { 162, 4 },
  { 162, 4 },
  { 162, 5 },
  { 216, 5 },
  { 216, 4 },
  { 217, 2 },
  { 217, 0 },
  { 215, 1 },
  { 215, 0 },
  { 197, 0 },
  { 196, 3 },
  { 196, 1 },
  { 214, 0 },
  { 214, 3 },
  { 141, 11 },
  { 218, 1 },
  { 218, 0 },
  { 166, 0 },
  { 166, 3 },
  { 176, 5 },
  { 176, 3 },
  { 219, 0 },
  { 219, 2 },
  { 141, 6 },
  { 141, 2 },
  { 141, 4 },
  { 141, 5 },
  { 141, 4 },
  { 141, 5 },
  { 141, 6 },
  { 158, 2 },
  { 159, 2 },
  { 141, 5 },
  { 221, 9 },
  { 223, 1 },
  { 223, 1 },
  { 223, 2 },
  { 223, 0 },
  { 224, 1 },
  { 224, 1 },
  { 224, 3 },
  { 226, 0 },
  { 226, 2 },
  { 222, 3 },
  { 222, 2 },
  { 228, 3 },
  { 229, 3 },
  { 229, 2 },
  { 227, 7 },
  { 227, 5 },
  { 227, 5 },
  { 227, 1 },
  { 162, 4 },
  { 162, 6 },
  { 180, 1 },
  { 180, 1 },
  { 180, 1 },
  { 141, 4 },
  { 141, 1 },
  { 141, 2 },
  { 141, 4 },
  { 141, 1 },
  { 141, 2 },
  { 141, 6 },
  { 141, 7 },
  { 230, 1 },
  { 185, 0 },
  { 185, 2 },
  { 185, 3 },
  { 232, 6 },
  { 232, 8 },
  { 137, 1 },
  { 139, 0 },
  { 140, 1 },
  { 143, 0 },
  { 143, 1 },
  { 143, 2 },
  { 145, 1 },
  { 145, 0 },
  { 141, 2 },
  { 150, 4 },
  { 150, 2 },
  { 155, 1 },
  { 156, 1 },
  { 157, 1 },
  { 157, 1 },
  { 154, 2 },
  { 154, 0 },
  { 160, 2 },
  { 151, 2 },
  { 172, 3 },
  { 172, 1 },
  { 173, 0 },
  { 177, 1 },
  { 179, 1 },
  { 183, 1 },
  { 184, 1 },
  { 198, 2 },
  { 199, 1 },
  { 206, 1 },
  { 206, 1 },
  { 162, 1 },
  { 197, 1 },
  { 220, 1 },
  { 220, 1 },
  { 220, 1 },
  { 220, 1 },
  { 220, 1 },
  { 220, 1 },
  { 158, 1 },
  { 225, 0 },
  { 225, 3 },
  { 228, 1 },
  { 229, 0 },
  { 231, 0 },
  { 231, 1 },
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
#line 2179 "parse.c"
        break;
      case 1: /* ecmd ::= SEMI */
#line 112 "parse.y"
{
  sqlite3ErrorMsg(pParse, "syntax error: empty request");
}
#line 2186 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN */
#line 117 "parse.y"
{ pParse->explain = 1; }
#line 2191 "parse.c"
        break;
      case 3: /* explain ::= EXPLAIN QUERY PLAN */
#line 118 "parse.y"
{ pParse->explain = 2; }
#line 2196 "parse.c"
        break;
      case 4: /* cmd ::= BEGIN transtype trans_opt */
#line 150 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy322);}
#line 2201 "parse.c"
        break;
      case 5: /* transtype ::= */
#line 155 "parse.y"
{yymsp[1].minor.yy322 = TK_DEFERRED;}
#line 2206 "parse.c"
        break;
      case 6: /* transtype ::= DEFERRED */
#line 156 "parse.y"
{yymsp[0].minor.yy322 = yymsp[0].major; /*A-overwrites-X*/}
#line 2211 "parse.c"
        break;
      case 7: /* cmd ::= COMMIT trans_opt */
      case 8: /* cmd ::= END trans_opt */ yytestcase(yyruleno==8);
#line 157 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2217 "parse.c"
        break;
      case 9: /* cmd ::= ROLLBACK trans_opt */
#line 159 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2222 "parse.c"
        break;
      case 10: /* cmd ::= SAVEPOINT nm */
#line 163 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2229 "parse.c"
        break;
      case 11: /* cmd ::= RELEASE savepoint_opt nm */
#line 166 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2236 "parse.c"
        break;
      case 12: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 169 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2243 "parse.c"
        break;
      case 13: /* create_table ::= createkw TABLE ifnotexists nm */
#line 176 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[0].minor.yy0,yymsp[-1].minor.yy322);
}
#line 2250 "parse.c"
        break;
      case 14: /* createkw ::= CREATE */
#line 179 "parse.y"
{disableLookaside(pParse);}
#line 2255 "parse.c"
        break;
      case 15: /* ifnotexists ::= */
      case 38: /* autoinc ::= */ yytestcase(yyruleno==38);
      case 53: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==53);
      case 63: /* defer_subclause_opt ::= */ yytestcase(yyruleno==63);
      case 72: /* ifexists ::= */ yytestcase(yyruleno==72);
      case 86: /* distinct ::= */ yytestcase(yyruleno==86);
      case 208: /* collate ::= */ yytestcase(yyruleno==208);
#line 182 "parse.y"
{yymsp[1].minor.yy322 = 0;}
#line 2266 "parse.c"
        break;
      case 16: /* ifnotexists ::= IF NOT EXISTS */
#line 183 "parse.y"
{yymsp[-2].minor.yy322 = 1;}
#line 2271 "parse.c"
        break;
      case 17: /* create_table_args ::= LP columnlist conslist_opt RP */
#line 185 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,0,0);
}
#line 2278 "parse.c"
        break;
      case 18: /* create_table_args ::= AS select */
#line 188 "parse.y"
{
  sqlite3EndTable(pParse,0,0,0,yymsp[0].minor.yy309);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy309);
}
#line 2286 "parse.c"
        break;
      case 19: /* columnname ::= nm typetoken */
#line 194 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2291 "parse.c"
        break;
      case 20: /* nm ::= ID|INDEXED */
#line 225 "parse.y"
{
  if(yymsp[0].minor.yy0.isReserved) {
    sqlite3ErrorMsg(pParse, "keyword \"%T\" is reserved", &yymsp[0].minor.yy0);
  }
}
#line 2300 "parse.c"
        break;
      case 21: /* typetoken ::= */
      case 56: /* conslist_opt ::= */ yytestcase(yyruleno==56);
      case 92: /* as ::= */ yytestcase(yyruleno==92);
#line 236 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2307 "parse.c"
        break;
      case 22: /* typetoken ::= typename LP signed RP */
#line 238 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2314 "parse.c"
        break;
      case 23: /* typetoken ::= typename LP signed COMMA signed RP */
#line 241 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2321 "parse.c"
        break;
      case 24: /* typename ::= typename ID|STRING */
#line 246 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2326 "parse.c"
        break;
      case 25: /* ccons ::= CONSTRAINT nm */
      case 58: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==58);
#line 255 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2332 "parse.c"
        break;
      case 26: /* ccons ::= DEFAULT term */
      case 28: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==28);
#line 256 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy200);}
#line 2338 "parse.c"
        break;
      case 27: /* ccons ::= DEFAULT LP expr RP */
#line 257 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy200);}
#line 2343 "parse.c"
        break;
      case 29: /* ccons ::= DEFAULT MINUS term */
#line 259 "parse.y"
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy200.pExpr, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy200.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2354 "parse.c"
        break;
      case 30: /* ccons ::= DEFAULT ID|INDEXED */
#line 266 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2363 "parse.c"
        break;
      case 31: /* ccons ::= NOT NULL onconf */
#line 276 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy322);}
#line 2368 "parse.c"
        break;
      case 32: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 278 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy322,yymsp[0].minor.yy322,yymsp[-2].minor.yy322);}
#line 2373 "parse.c"
        break;
      case 33: /* ccons ::= UNIQUE onconf */
#line 279 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[0].minor.yy322,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2379 "parse.c"
        break;
      case 34: /* ccons ::= CHECK LP expr RP */
#line 281 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy200.pExpr);}
#line 2384 "parse.c"
        break;
      case 35: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 283 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy272,yymsp[0].minor.yy322);}
#line 2389 "parse.c"
        break;
      case 36: /* ccons ::= defer_subclause */
#line 284 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy322);}
#line 2394 "parse.c"
        break;
      case 37: /* ccons ::= COLLATE ID|INDEXED */
#line 285 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2399 "parse.c"
        break;
      case 39: /* autoinc ::= AUTOINCR */
#line 290 "parse.y"
{yymsp[0].minor.yy322 = 1;}
#line 2404 "parse.c"
        break;
      case 40: /* refargs ::= */
#line 298 "parse.y"
{ yymsp[1].minor.yy322 = OE_None*0x0101; /* EV: R-19803-45884 */}
#line 2409 "parse.c"
        break;
      case 41: /* refargs ::= refargs refarg */
#line 299 "parse.y"
{ yymsp[-1].minor.yy322 = (yymsp[-1].minor.yy322 & ~yymsp[0].minor.yy369.mask) | yymsp[0].minor.yy369.value; }
#line 2414 "parse.c"
        break;
      case 42: /* refarg ::= MATCH nm */
#line 301 "parse.y"
{ yymsp[-1].minor.yy369.value = 0;     yymsp[-1].minor.yy369.mask = 0x000000; }
#line 2419 "parse.c"
        break;
      case 43: /* refarg ::= ON INSERT refact */
#line 302 "parse.y"
{ yymsp[-2].minor.yy369.value = 0;     yymsp[-2].minor.yy369.mask = 0x000000; }
#line 2424 "parse.c"
        break;
      case 44: /* refarg ::= ON DELETE refact */
#line 303 "parse.y"
{ yymsp[-2].minor.yy369.value = yymsp[0].minor.yy322;     yymsp[-2].minor.yy369.mask = 0x0000ff; }
#line 2429 "parse.c"
        break;
      case 45: /* refarg ::= ON UPDATE refact */
#line 304 "parse.y"
{ yymsp[-2].minor.yy369.value = yymsp[0].minor.yy322<<8;  yymsp[-2].minor.yy369.mask = 0x00ff00; }
#line 2434 "parse.c"
        break;
      case 46: /* refact ::= SET NULL */
#line 306 "parse.y"
{ yymsp[-1].minor.yy322 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2439 "parse.c"
        break;
      case 47: /* refact ::= SET DEFAULT */
#line 307 "parse.y"
{ yymsp[-1].minor.yy322 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2444 "parse.c"
        break;
      case 48: /* refact ::= CASCADE */
#line 308 "parse.y"
{ yymsp[0].minor.yy322 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2449 "parse.c"
        break;
      case 49: /* refact ::= RESTRICT */
#line 309 "parse.y"
{ yymsp[0].minor.yy322 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2454 "parse.c"
        break;
      case 50: /* refact ::= NO ACTION */
#line 310 "parse.y"
{ yymsp[-1].minor.yy322 = OE_None;     /* EV: R-33326-45252 */}
#line 2459 "parse.c"
        break;
      case 51: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 312 "parse.y"
{yymsp[-2].minor.yy322 = 0;}
#line 2464 "parse.c"
        break;
      case 52: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 67: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==67);
      case 138: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==138);
#line 313 "parse.y"
{yymsp[-1].minor.yy322 = yymsp[0].minor.yy322;}
#line 2471 "parse.c"
        break;
      case 54: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 71: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==71);
      case 180: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==180);
      case 183: /* in_op ::= NOT IN */ yytestcase(yyruleno==183);
      case 209: /* collate ::= COLLATE ID|INDEXED */ yytestcase(yyruleno==209);
#line 316 "parse.y"
{yymsp[-1].minor.yy322 = 1;}
#line 2480 "parse.c"
        break;
      case 55: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 317 "parse.y"
{yymsp[-1].minor.yy322 = 0;}
#line 2485 "parse.c"
        break;
      case 57: /* tconscomma ::= COMMA */
#line 323 "parse.y"
{pParse->constraintName.n = 0;}
#line 2490 "parse.c"
        break;
      case 59: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 327 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy272,yymsp[0].minor.yy322,yymsp[-2].minor.yy322,0);}
#line 2495 "parse.c"
        break;
      case 60: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 329 "parse.y"
{sqlite3CreateIndex(pParse,0,0,yymsp[-2].minor.yy272,yymsp[0].minor.yy322,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2501 "parse.c"
        break;
      case 61: /* tcons ::= CHECK LP expr RP onconf */
#line 332 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy200.pExpr);}
#line 2506 "parse.c"
        break;
      case 62: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 334 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy272, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy272, yymsp[-1].minor.yy322);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy322);
}
#line 2514 "parse.c"
        break;
      case 64: /* onconf ::= */
      case 66: /* orconf ::= */ yytestcase(yyruleno==66);
#line 348 "parse.y"
{yymsp[1].minor.yy322 = OE_Default;}
#line 2520 "parse.c"
        break;
      case 65: /* onconf ::= ON CONFLICT resolvetype */
#line 349 "parse.y"
{yymsp[-2].minor.yy322 = yymsp[0].minor.yy322;}
#line 2525 "parse.c"
        break;
      case 68: /* resolvetype ::= IGNORE */
#line 353 "parse.y"
{yymsp[0].minor.yy322 = OE_Ignore;}
#line 2530 "parse.c"
        break;
      case 69: /* resolvetype ::= REPLACE */
      case 139: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==139);
#line 354 "parse.y"
{yymsp[0].minor.yy322 = OE_Replace;}
#line 2536 "parse.c"
        break;
      case 70: /* cmd ::= DROP TABLE ifexists fullname */
#line 358 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy73, 0, yymsp[-1].minor.yy322);
}
#line 2543 "parse.c"
        break;
      case 73: /* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 369 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-6].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy272, yymsp[0].minor.yy309, yymsp[-4].minor.yy322);
}
#line 2550 "parse.c"
        break;
      case 74: /* cmd ::= DROP VIEW ifexists fullname */
#line 372 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy73, 1, yymsp[-1].minor.yy322);
}
#line 2557 "parse.c"
        break;
      case 75: /* cmd ::= select */
#line 379 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy309, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy309);
}
#line 2566 "parse.c"
        break;
      case 76: /* select ::= with selectnowith */
#line 416 "parse.y"
{
  Select *p = yymsp[0].minor.yy309;
  if( p ){
    p->pWith = yymsp[-1].minor.yy445;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, yymsp[-1].minor.yy445);
  }
  yymsp[-1].minor.yy309 = p; /*A-overwrites-W*/
}
#line 2580 "parse.c"
        break;
      case 77: /* selectnowith ::= selectnowith multiselect_op oneselect */
#line 429 "parse.y"
{
  Select *pRhs = yymsp[0].minor.yy309;
  Select *pLhs = yymsp[-2].minor.yy309;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,&x,pRhs,0,0);
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)yymsp[-1].minor.yy322;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( yymsp[-1].minor.yy322!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  yymsp[-2].minor.yy309 = pRhs;
}
#line 2606 "parse.c"
        break;
      case 78: /* multiselect_op ::= UNION */
      case 80: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==80);
#line 452 "parse.y"
{yymsp[0].minor.yy322 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2612 "parse.c"
        break;
      case 79: /* multiselect_op ::= UNION ALL */
#line 453 "parse.y"
{yymsp[-1].minor.yy322 = TK_ALL;}
#line 2617 "parse.c"
        break;
      case 81: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 457 "parse.y"
{
#if SELECTTRACE_ENABLED
  Token s = yymsp[-8].minor.yy0; /*A-overwrites-S*/
#endif
  yymsp[-8].minor.yy309 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy272,yymsp[-5].minor.yy73,yymsp[-4].minor.yy294,yymsp[-3].minor.yy272,yymsp[-2].minor.yy294,yymsp[-1].minor.yy272,yymsp[-7].minor.yy322,yymsp[0].minor.yy400.pLimit,yymsp[0].minor.yy400.pOffset);
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
  if( yymsp[-8].minor.yy309!=0 ){
    const char *z = s.z+6;
    int i;
    sqlite3_snprintf(sizeof(yymsp[-8].minor.yy309->zSelName), yymsp[-8].minor.yy309->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(yymsp[-8].minor.yy309->zSelName), yymsp[-8].minor.yy309->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
#line 2651 "parse.c"
        break;
      case 82: /* values ::= VALUES LP nexprlist RP */
#line 491 "parse.y"
{
  yymsp[-3].minor.yy309 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy272,0,0,0,0,0,SF_Values,0,0);
}
#line 2658 "parse.c"
        break;
      case 83: /* values ::= values COMMA LP exprlist RP */
#line 494 "parse.y"
{
  Select *pRight, *pLeft = yymsp[-4].minor.yy309;
  pRight = sqlite3SelectNew(pParse,yymsp[-1].minor.yy272,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    yymsp[-4].minor.yy309 = pRight;
  }else{
    yymsp[-4].minor.yy309 = pLeft;
  }
}
#line 2674 "parse.c"
        break;
      case 84: /* distinct ::= DISTINCT */
#line 511 "parse.y"
{yymsp[0].minor.yy322 = SF_Distinct;}
#line 2679 "parse.c"
        break;
      case 85: /* distinct ::= ALL */
#line 512 "parse.y"
{yymsp[0].minor.yy322 = SF_All;}
#line 2684 "parse.c"
        break;
      case 87: /* sclp ::= */
      case 113: /* orderby_opt ::= */ yytestcase(yyruleno==113);
      case 120: /* groupby_opt ::= */ yytestcase(yyruleno==120);
      case 196: /* exprlist ::= */ yytestcase(yyruleno==196);
      case 199: /* paren_exprlist ::= */ yytestcase(yyruleno==199);
      case 204: /* eidlist_opt ::= */ yytestcase(yyruleno==204);
#line 525 "parse.y"
{yymsp[1].minor.yy272 = 0;}
#line 2694 "parse.c"
        break;
      case 88: /* selcollist ::= sclp expr as */
#line 526 "parse.y"
{
   yymsp[-2].minor.yy272 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy272, yymsp[-1].minor.yy200.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy272, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy272,&yymsp[-1].minor.yy200);
}
#line 2703 "parse.c"
        break;
      case 89: /* selcollist ::= sclp STAR */
#line 531 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy272 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy272, p);
}
#line 2711 "parse.c"
        break;
      case 90: /* selcollist ::= sclp nm DOT STAR */
#line 535 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy272 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy272, pDot);
}
#line 2721 "parse.c"
        break;
      case 91: /* as ::= AS nm */
      case 217: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==217);
      case 218: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==218);
#line 546 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2728 "parse.c"
        break;
      case 93: /* from ::= */
#line 560 "parse.y"
{yymsp[1].minor.yy73 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy73));}
#line 2733 "parse.c"
        break;
      case 94: /* from ::= FROM seltablist */
#line 561 "parse.y"
{
  yymsp[-1].minor.yy73 = yymsp[0].minor.yy73;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy73);
}
#line 2741 "parse.c"
        break;
      case 95: /* stl_prefix ::= seltablist joinop */
#line 569 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy73 && yymsp[-1].minor.yy73->nSrc>0) ) yymsp[-1].minor.yy73->a[yymsp[-1].minor.yy73->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy322;
}
#line 2748 "parse.c"
        break;
      case 96: /* stl_prefix ::= */
#line 572 "parse.y"
{yymsp[1].minor.yy73 = 0;}
#line 2753 "parse.c"
        break;
      case 97: /* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 574 "parse.y"
{
  yymsp[-5].minor.yy73 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-5].minor.yy73,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy294,yymsp[0].minor.yy436);
  sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy73, &yymsp[-2].minor.yy0);
}
#line 2761 "parse.c"
        break;
      case 98: /* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 579 "parse.y"
{
  yymsp[-7].minor.yy73 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-7].minor.yy73,&yymsp[-6].minor.yy0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy294,yymsp[0].minor.yy436);
  sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy73, yymsp[-4].minor.yy272);
}
#line 2769 "parse.c"
        break;
      case 99: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 585 "parse.y"
{
    yymsp[-6].minor.yy73 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy73,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy309,yymsp[-1].minor.yy294,yymsp[0].minor.yy436);
  }
#line 2776 "parse.c"
        break;
      case 100: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 589 "parse.y"
{
    if( yymsp[-6].minor.yy73==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy294==0 && yymsp[0].minor.yy436==0 ){
      yymsp[-6].minor.yy73 = yymsp[-4].minor.yy73;
    }else if( yymsp[-4].minor.yy73->nSrc==1 ){
      yymsp[-6].minor.yy73 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy73,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy294,yymsp[0].minor.yy436);
      if( yymsp[-6].minor.yy73 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy73->a[yymsp[-6].minor.yy73->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy73->a;
        pNew->zName = pOld->zName;
        pNew->pSelect = pOld->pSelect;
        pOld->zName =  0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy73);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy73);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy73,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy73 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy73,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy294,yymsp[0].minor.yy436);
    }
  }
#line 2801 "parse.c"
        break;
      case 101: /* fullname ::= nm */
#line 615 "parse.y"
{yymsp[0].minor.yy73 = sqlite3SrcListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 2806 "parse.c"
        break;
      case 102: /* joinop ::= COMMA|JOIN */
#line 621 "parse.y"
{ yymsp[0].minor.yy322 = JT_INNER; }
#line 2811 "parse.c"
        break;
      case 103: /* joinop ::= JOIN_KW JOIN */
#line 623 "parse.y"
{yymsp[-1].minor.yy322 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2816 "parse.c"
        break;
      case 104: /* joinop ::= JOIN_KW join_nm JOIN */
#line 625 "parse.y"
{yymsp[-2].minor.yy322 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2821 "parse.c"
        break;
      case 105: /* joinop ::= JOIN_KW join_nm join_nm JOIN */
#line 627 "parse.y"
{yymsp[-3].minor.yy322 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2826 "parse.c"
        break;
      case 106: /* on_opt ::= ON expr */
      case 123: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==123);
      case 130: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==130);
      case 192: /* case_else ::= ELSE expr */ yytestcase(yyruleno==192);
#line 631 "parse.y"
{yymsp[-1].minor.yy294 = yymsp[0].minor.yy200.pExpr;}
#line 2834 "parse.c"
        break;
      case 107: /* on_opt ::= */
      case 122: /* having_opt ::= */ yytestcase(yyruleno==122);
      case 129: /* where_opt ::= */ yytestcase(yyruleno==129);
      case 193: /* case_else ::= */ yytestcase(yyruleno==193);
      case 195: /* case_operand ::= */ yytestcase(yyruleno==195);
#line 632 "parse.y"
{yymsp[1].minor.yy294 = 0;}
#line 2843 "parse.c"
        break;
      case 108: /* indexed_opt ::= */
#line 645 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2848 "parse.c"
        break;
      case 109: /* indexed_opt ::= INDEXED BY nm */
#line 646 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2853 "parse.c"
        break;
      case 110: /* indexed_opt ::= NOT INDEXED */
#line 647 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2858 "parse.c"
        break;
      case 111: /* using_opt ::= USING LP idlist RP */
#line 651 "parse.y"
{yymsp[-3].minor.yy436 = yymsp[-1].minor.yy436;}
#line 2863 "parse.c"
        break;
      case 112: /* using_opt ::= */
      case 140: /* idlist_opt ::= */ yytestcase(yyruleno==140);
#line 652 "parse.y"
{yymsp[1].minor.yy436 = 0;}
#line 2869 "parse.c"
        break;
      case 114: /* orderby_opt ::= ORDER BY sortlist */
      case 121: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==121);
#line 666 "parse.y"
{yymsp[-2].minor.yy272 = yymsp[0].minor.yy272;}
#line 2875 "parse.c"
        break;
      case 115: /* sortlist ::= sortlist COMMA expr sortorder */
#line 667 "parse.y"
{
  yymsp[-3].minor.yy272 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy272,yymsp[-1].minor.yy200.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy272,yymsp[0].minor.yy322);
}
#line 2883 "parse.c"
        break;
      case 116: /* sortlist ::= expr sortorder */
#line 671 "parse.y"
{
  yymsp[-1].minor.yy272 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy200.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy272,yymsp[0].minor.yy322);
}
#line 2891 "parse.c"
        break;
      case 117: /* sortorder ::= ASC */
#line 678 "parse.y"
{yymsp[0].minor.yy322 = SQLITE_SO_ASC;}
#line 2896 "parse.c"
        break;
      case 118: /* sortorder ::= DESC */
#line 679 "parse.y"
{yymsp[0].minor.yy322 = SQLITE_SO_DESC;}
#line 2901 "parse.c"
        break;
      case 119: /* sortorder ::= */
#line 680 "parse.y"
{yymsp[1].minor.yy322 = SQLITE_SO_UNDEFINED;}
#line 2906 "parse.c"
        break;
      case 124: /* limit_opt ::= */
#line 705 "parse.y"
{yymsp[1].minor.yy400.pLimit = 0; yymsp[1].minor.yy400.pOffset = 0;}
#line 2911 "parse.c"
        break;
      case 125: /* limit_opt ::= LIMIT expr */
#line 706 "parse.y"
{yymsp[-1].minor.yy400.pLimit = yymsp[0].minor.yy200.pExpr; yymsp[-1].minor.yy400.pOffset = 0;}
#line 2916 "parse.c"
        break;
      case 126: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 708 "parse.y"
{yymsp[-3].minor.yy400.pLimit = yymsp[-2].minor.yy200.pExpr; yymsp[-3].minor.yy400.pOffset = yymsp[0].minor.yy200.pExpr;}
#line 2921 "parse.c"
        break;
      case 127: /* limit_opt ::= LIMIT expr COMMA expr */
#line 710 "parse.y"
{yymsp[-3].minor.yy400.pOffset = yymsp[-2].minor.yy200.pExpr; yymsp[-3].minor.yy400.pLimit = yymsp[0].minor.yy200.pExpr;}
#line 2926 "parse.c"
        break;
      case 128: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 727 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy445, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy73, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy73,yymsp[0].minor.yy294);
}
#line 2938 "parse.c"
        break;
      case 131: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 760 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy445, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy73, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy272,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Update(pParse,yymsp[-4].minor.yy73,yymsp[-1].minor.yy272,yymsp[0].minor.yy294,yymsp[-5].minor.yy322);
}
#line 2951 "parse.c"
        break;
      case 132: /* setlist ::= setlist COMMA nm EQ expr */
#line 774 "parse.y"
{
  yymsp[-4].minor.yy272 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy272, yymsp[0].minor.yy200.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy272, &yymsp[-2].minor.yy0, 1);
}
#line 2959 "parse.c"
        break;
      case 133: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 778 "parse.y"
{
  yymsp[-6].minor.yy272 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy272, yymsp[-3].minor.yy436, yymsp[0].minor.yy200.pExpr);
}
#line 2966 "parse.c"
        break;
      case 134: /* setlist ::= nm EQ expr */
#line 781 "parse.y"
{
  yylhsminor.yy272 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy200.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy272, &yymsp[-2].minor.yy0, 1);
}
#line 2974 "parse.c"
  yymsp[-2].minor.yy272 = yylhsminor.yy272;
        break;
      case 135: /* setlist ::= LP idlist RP EQ expr */
#line 785 "parse.y"
{
  yymsp[-4].minor.yy272 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy436, yymsp[0].minor.yy200.pExpr);
}
#line 2982 "parse.c"
        break;
      case 136: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 791 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy445, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-2].minor.yy73, yymsp[0].minor.yy309, yymsp[-1].minor.yy436, yymsp[-4].minor.yy322);
}
#line 2993 "parse.c"
        break;
      case 137: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 799 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy445, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-3].minor.yy73, 0, yymsp[-2].minor.yy436, yymsp[-5].minor.yy322);
}
#line 3004 "parse.c"
        break;
      case 141: /* idlist_opt ::= LP idlist RP */
#line 817 "parse.y"
{yymsp[-2].minor.yy436 = yymsp[-1].minor.yy436;}
#line 3009 "parse.c"
        break;
      case 142: /* idlist ::= idlist COMMA nm */
#line 819 "parse.y"
{yymsp[-2].minor.yy436 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy436,&yymsp[0].minor.yy0);}
#line 3014 "parse.c"
        break;
      case 143: /* idlist ::= nm */
#line 821 "parse.y"
{yymsp[0].minor.yy436 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3019 "parse.c"
        break;
      case 144: /* expr ::= LP expr RP */
#line 870 "parse.y"
{spanSet(&yymsp[-2].minor.yy200,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy200.pExpr = yymsp[-1].minor.yy200.pExpr;}
#line 3024 "parse.c"
        break;
      case 145: /* term ::= NULL */
      case 149: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==149);
      case 150: /* term ::= STRING */ yytestcase(yyruleno==150);
#line 871 "parse.y"
{spanExpr(&yymsp[0].minor.yy200,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3031 "parse.c"
        break;
      case 146: /* expr ::= ID|INDEXED */
      case 147: /* expr ::= JOIN_KW */ yytestcase(yyruleno==147);
#line 872 "parse.y"
{spanExpr(&yymsp[0].minor.yy200,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3037 "parse.c"
        break;
      case 148: /* expr ::= nm DOT nm */
#line 874 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy200,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3047 "parse.c"
        break;
      case 151: /* term ::= INTEGER */
#line 882 "parse.y"
{
  yylhsminor.yy200.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy200.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy200.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy200.pExpr ) yylhsminor.yy200.pExpr->flags |= EP_Leaf;
}
#line 3057 "parse.c"
  yymsp[0].minor.yy200 = yylhsminor.yy200;
        break;
      case 152: /* expr ::= VARIABLE */
#line 888 "parse.y"
{
  if( !(yymsp[0].minor.yy0.z[0]=='#' && sqlite3Isdigit(yymsp[0].minor.yy0.z[1])) ){
    u32 n = yymsp[0].minor.yy0.n;
    spanExpr(&yymsp[0].minor.yy200, pParse, TK_VARIABLE, yymsp[0].minor.yy0);
    sqlite3ExprAssignVarNumber(pParse, yymsp[0].minor.yy200.pExpr, n);
  }else{
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    Token t = yymsp[0].minor.yy0; /*A-overwrites-X*/
    assert( t.n>=2 );
    spanSet(&yymsp[0].minor.yy200, &t, &t);
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &t);
      yymsp[0].minor.yy200.pExpr = 0;
    }else{
      yymsp[0].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0);
      if( yymsp[0].minor.yy200.pExpr ) sqlite3GetInt32(&t.z[1], &yymsp[0].minor.yy200.pExpr->iTable);
    }
  }
}
#line 3083 "parse.c"
        break;
      case 153: /* expr ::= expr COLLATE ID|INDEXED */
#line 909 "parse.y"
{
  yymsp[-2].minor.yy200.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy200.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy200.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3091 "parse.c"
        break;
      case 154: /* expr ::= CAST LP expr AS typetoken RP */
#line 914 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy200,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy200.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy200.pExpr, yymsp[-3].minor.yy200.pExpr, 0);
}
#line 3100 "parse.c"
        break;
      case 155: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 920 "parse.y"
{
  if( yymsp[-1].minor.yy272 && yymsp[-1].minor.yy272->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yylhsminor.yy200.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy272, &yymsp[-4].minor.yy0);
  spanSet(&yylhsminor.yy200,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy322==SF_Distinct && yylhsminor.yy200.pExpr ){
    yylhsminor.yy200.pExpr->flags |= EP_Distinct;
  }
}
#line 3114 "parse.c"
  yymsp[-4].minor.yy200 = yylhsminor.yy200;
        break;
      case 156: /* expr ::= ID|INDEXED LP STAR RP */
#line 930 "parse.y"
{
  yylhsminor.yy200.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy200,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3123 "parse.c"
  yymsp[-3].minor.yy200 = yylhsminor.yy200;
        break;
      case 157: /* term ::= CTIME_KW */
#line 934 "parse.y"
{
  yylhsminor.yy200.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy200, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3132 "parse.c"
  yymsp[0].minor.yy200 = yylhsminor.yy200;
        break;
      case 158: /* expr ::= LP nexprlist COMMA expr RP */
#line 963 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy272, yymsp[-1].minor.yy200.pExpr);
  yylhsminor.yy200.pExpr = sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
  if( yylhsminor.yy200.pExpr ){
    yylhsminor.yy200.pExpr->x.pList = pList;
    spanSet(&yylhsminor.yy200, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  }
}
#line 3147 "parse.c"
  yymsp[-4].minor.yy200 = yylhsminor.yy200;
        break;
      case 159: /* expr ::= expr AND expr */
      case 160: /* expr ::= expr OR expr */ yytestcase(yyruleno==160);
      case 161: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==161);
      case 162: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==162);
      case 163: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==163);
      case 164: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==164);
      case 165: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==165);
      case 166: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==166);
#line 974 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy200,&yymsp[0].minor.yy200);}
#line 3160 "parse.c"
        break;
      case 167: /* likeop ::= LIKE_KW|MATCH */
#line 987 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3165 "parse.c"
        break;
      case 168: /* likeop ::= NOT LIKE_KW|MATCH */
#line 988 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3170 "parse.c"
        break;
      case 169: /* expr ::= expr likeop expr */
#line 989 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
  yymsp[-1].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy200.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy200.pExpr);
  yymsp[-2].minor.yy200.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-2].minor.yy200);
  yymsp[-2].minor.yy200.zEnd = yymsp[0].minor.yy200.zEnd;
  if( yymsp[-2].minor.yy200.pExpr ) yymsp[-2].minor.yy200.pExpr->flags |= EP_InfixFunc;
}
#line 3185 "parse.c"
        break;
      case 170: /* expr ::= expr likeop expr ESCAPE expr */
#line 1000 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
  yymsp[-3].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy200.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy200.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy200.pExpr);
  yymsp[-4].minor.yy200.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-4].minor.yy200);
  yymsp[-4].minor.yy200.zEnd = yymsp[0].minor.yy200.zEnd;
  if( yymsp[-4].minor.yy200.pExpr ) yymsp[-4].minor.yy200.pExpr->flags |= EP_InfixFunc;
}
#line 3201 "parse.c"
        break;
      case 171: /* expr ::= expr ISNULL|NOTNULL */
#line 1027 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy200,&yymsp[0].minor.yy0);}
#line 3206 "parse.c"
        break;
      case 172: /* expr ::= expr NOT NULL */
#line 1028 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy200,&yymsp[0].minor.yy0);}
#line 3211 "parse.c"
        break;
      case 173: /* expr ::= expr IS expr */
#line 1049 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy200,&yymsp[0].minor.yy200);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy200.pExpr, yymsp[-2].minor.yy200.pExpr, TK_ISNULL);
}
#line 3219 "parse.c"
        break;
      case 174: /* expr ::= expr IS NOT expr */
#line 1053 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy200,&yymsp[0].minor.yy200);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy200.pExpr, yymsp[-3].minor.yy200.pExpr, TK_NOTNULL);
}
#line 3227 "parse.c"
        break;
      case 175: /* expr ::= NOT expr */
      case 176: /* expr ::= BITNOT expr */ yytestcase(yyruleno==176);
#line 1077 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy200,pParse,yymsp[-1].major,&yymsp[0].minor.yy200,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3233 "parse.c"
        break;
      case 177: /* expr ::= MINUS expr */
#line 1081 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy200,pParse,TK_UMINUS,&yymsp[0].minor.yy200,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3238 "parse.c"
        break;
      case 178: /* expr ::= PLUS expr */
#line 1083 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy200,pParse,TK_UPLUS,&yymsp[0].minor.yy200,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3243 "parse.c"
        break;
      case 179: /* between_op ::= BETWEEN */
      case 182: /* in_op ::= IN */ yytestcase(yyruleno==182);
#line 1086 "parse.y"
{yymsp[0].minor.yy322 = 0;}
#line 3249 "parse.c"
        break;
      case 181: /* expr ::= expr between_op expr AND expr */
#line 1088 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy200.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy200.pExpr);
  yymsp[-4].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy200.pExpr, 0);
  if( yymsp[-4].minor.yy200.pExpr ){
    yymsp[-4].minor.yy200.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, yymsp[-3].minor.yy322, &yymsp[-4].minor.yy200);
  yymsp[-4].minor.yy200.zEnd = yymsp[0].minor.yy200.zEnd;
}
#line 3265 "parse.c"
        break;
      case 184: /* expr ::= expr in_op LP exprlist RP */
#line 1104 "parse.y"
{
    if( yymsp[-1].minor.yy272==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      sqlite3ExprDelete(pParse->db, yymsp[-4].minor.yy200.pExpr);
      yymsp[-4].minor.yy200.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER,&sqlite3IntTokens[yymsp[-3].minor.yy322],1);
    }else if( yymsp[-1].minor.yy272->nExpr==1 ){
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
      Expr *pRHS = yymsp[-1].minor.yy272->a[0].pExpr;
      yymsp[-1].minor.yy272->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy272);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      yymsp[-4].minor.yy200.pExpr = sqlite3PExpr(pParse, yymsp[-3].minor.yy322 ? TK_NE : TK_EQ, yymsp[-4].minor.yy200.pExpr, pRHS);
    }else{
      yymsp[-4].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy200.pExpr, 0);
      if( yymsp[-4].minor.yy200.pExpr ){
        yymsp[-4].minor.yy200.pExpr->x.pList = yymsp[-1].minor.yy272;
        sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy200.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy272);
      }
      exprNot(pParse, yymsp[-3].minor.yy322, &yymsp[-4].minor.yy200);
    }
    yymsp[-4].minor.yy200.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3320 "parse.c"
        break;
      case 185: /* expr ::= LP select RP */
#line 1155 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy200,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy200.pExpr, yymsp[-1].minor.yy309);
  }
#line 3329 "parse.c"
        break;
      case 186: /* expr ::= expr in_op LP select RP */
#line 1160 "parse.y"
{
    yymsp[-4].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy200.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy200.pExpr, yymsp[-1].minor.yy309);
    exprNot(pParse, yymsp[-3].minor.yy322, &yymsp[-4].minor.yy200);
    yymsp[-4].minor.yy200.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3339 "parse.c"
        break;
      case 187: /* expr ::= expr in_op nm paren_exprlist */
#line 1166 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy272 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy272);
    yymsp[-3].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy200.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-3].minor.yy200.pExpr, pSelect);
    exprNot(pParse, yymsp[-2].minor.yy322, &yymsp[-3].minor.yy200);
    yymsp[-3].minor.yy200.zEnd = &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
#line 3352 "parse.c"
        break;
      case 188: /* expr ::= EXISTS LP select RP */
#line 1175 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy200,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy309);
  }
#line 3362 "parse.c"
        break;
      case 189: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1184 "parse.y"
{
  spanSet(&yymsp[-4].minor.yy200,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-C*/
  yymsp[-4].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy294, 0);
  if( yymsp[-4].minor.yy200.pExpr ){
    yymsp[-4].minor.yy200.pExpr->x.pList = yymsp[-1].minor.yy294 ? sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy272,yymsp[-1].minor.yy294) : yymsp[-2].minor.yy272;
    sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy200.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy272);
    sqlite3ExprDelete(pParse->db, yymsp[-1].minor.yy294);
  }
}
#line 3377 "parse.c"
        break;
      case 190: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1197 "parse.y"
{
  yymsp[-4].minor.yy272 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy272, yymsp[-2].minor.yy200.pExpr);
  yymsp[-4].minor.yy272 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy272, yymsp[0].minor.yy200.pExpr);
}
#line 3385 "parse.c"
        break;
      case 191: /* case_exprlist ::= WHEN expr THEN expr */
#line 1201 "parse.y"
{
  yymsp[-3].minor.yy272 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy200.pExpr);
  yymsp[-3].minor.yy272 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy272, yymsp[0].minor.yy200.pExpr);
}
#line 3393 "parse.c"
        break;
      case 194: /* case_operand ::= expr */
#line 1211 "parse.y"
{yymsp[0].minor.yy294 = yymsp[0].minor.yy200.pExpr; /*A-overwrites-X*/}
#line 3398 "parse.c"
        break;
      case 197: /* nexprlist ::= nexprlist COMMA expr */
#line 1222 "parse.y"
{yymsp[-2].minor.yy272 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy272,yymsp[0].minor.yy200.pExpr);}
#line 3403 "parse.c"
        break;
      case 198: /* nexprlist ::= expr */
#line 1224 "parse.y"
{yymsp[0].minor.yy272 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy200.pExpr); /*A-overwrites-Y*/}
#line 3408 "parse.c"
        break;
      case 200: /* paren_exprlist ::= LP exprlist RP */
      case 205: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==205);
#line 1232 "parse.y"
{yymsp[-2].minor.yy272 = yymsp[-1].minor.yy272;}
#line 3414 "parse.c"
        break;
      case 201: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1239 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0), yymsp[-2].minor.yy272, yymsp[-9].minor.yy322,
                      &yymsp[-10].minor.yy0, yymsp[0].minor.yy294, SQLITE_SO_ASC, yymsp[-7].minor.yy322, SQLITE_IDXTYPE_APPDEF);
}
#line 3423 "parse.c"
        break;
      case 202: /* uniqueflag ::= UNIQUE */
      case 242: /* raisetype ::= ABORT */ yytestcase(yyruleno==242);
#line 1246 "parse.y"
{yymsp[0].minor.yy322 = OE_Abort;}
#line 3429 "parse.c"
        break;
      case 203: /* uniqueflag ::= */
#line 1247 "parse.y"
{yymsp[1].minor.yy322 = OE_None;}
#line 3434 "parse.c"
        break;
      case 206: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1290 "parse.y"
{
  yymsp[-4].minor.yy272 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy272, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy322, yymsp[0].minor.yy322);
}
#line 3441 "parse.c"
        break;
      case 207: /* eidlist ::= nm collate sortorder */
#line 1293 "parse.y"
{
  yymsp[-2].minor.yy272 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy322, yymsp[0].minor.yy322); /*A-overwrites-Y*/
}
#line 3448 "parse.c"
        break;
      case 210: /* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1304 "parse.y"
{
    sqlite3DropIndex(pParse, yymsp[-2].minor.yy73, &yymsp[0].minor.yy0, yymsp[-3].minor.yy322);
}
#line 3455 "parse.c"
        break;
      case 211: /* cmd ::= PRAGMA nm */
#line 1311 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[0].minor.yy0,0,0,0,0);
}
#line 3462 "parse.c"
        break;
      case 212: /* cmd ::= PRAGMA nm EQ nmnum */
#line 1314 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,0);
}
#line 3469 "parse.c"
        break;
      case 213: /* cmd ::= PRAGMA nm LP nmnum RP */
#line 1317 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,0);
}
#line 3476 "parse.c"
        break;
      case 214: /* cmd ::= PRAGMA nm EQ minus_num */
#line 1320 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,1);
}
#line 3483 "parse.c"
        break;
      case 215: /* cmd ::= PRAGMA nm LP minus_num RP */
#line 1323 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,1);
}
#line 3490 "parse.c"
        break;
      case 216: /* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1326 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,0,&yymsp[0].minor.yy0,&yymsp[-2].minor.yy0,0);
}
#line 3497 "parse.c"
        break;
      case 219: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1346 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy251, &all);
}
#line 3507 "parse.c"
        break;
      case 220: /* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1355 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy322, yymsp[-4].minor.yy60.a, yymsp[-4].minor.yy60.b, yymsp[-2].minor.yy73, yymsp[0].minor.yy294, yymsp[-7].minor.yy322);
  yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0; /*yymsp[-8].minor.yy0-overwrites-T*/
}
#line 3515 "parse.c"
        break;
      case 221: /* trigger_time ::= BEFORE */
#line 1361 "parse.y"
{ yymsp[0].minor.yy322 = TK_BEFORE; }
#line 3520 "parse.c"
        break;
      case 222: /* trigger_time ::= AFTER */
#line 1362 "parse.y"
{ yymsp[0].minor.yy322 = TK_AFTER;  }
#line 3525 "parse.c"
        break;
      case 223: /* trigger_time ::= INSTEAD OF */
#line 1363 "parse.y"
{ yymsp[-1].minor.yy322 = TK_INSTEAD;}
#line 3530 "parse.c"
        break;
      case 224: /* trigger_time ::= */
#line 1364 "parse.y"
{ yymsp[1].minor.yy322 = TK_BEFORE; }
#line 3535 "parse.c"
        break;
      case 225: /* trigger_event ::= DELETE|INSERT */
      case 226: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==226);
#line 1368 "parse.y"
{yymsp[0].minor.yy60.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy60.b = 0;}
#line 3541 "parse.c"
        break;
      case 227: /* trigger_event ::= UPDATE OF idlist */
#line 1370 "parse.y"
{yymsp[-2].minor.yy60.a = TK_UPDATE; yymsp[-2].minor.yy60.b = yymsp[0].minor.yy436;}
#line 3546 "parse.c"
        break;
      case 228: /* when_clause ::= */
#line 1377 "parse.y"
{ yymsp[1].minor.yy294 = 0; }
#line 3551 "parse.c"
        break;
      case 229: /* when_clause ::= WHEN expr */
#line 1378 "parse.y"
{ yymsp[-1].minor.yy294 = yymsp[0].minor.yy200.pExpr; }
#line 3556 "parse.c"
        break;
      case 230: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1382 "parse.y"
{
  assert( yymsp[-2].minor.yy251!=0 );
  yymsp[-2].minor.yy251->pLast->pNext = yymsp[-1].minor.yy251;
  yymsp[-2].minor.yy251->pLast = yymsp[-1].minor.yy251;
}
#line 3565 "parse.c"
        break;
      case 231: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1387 "parse.y"
{ 
  assert( yymsp[-1].minor.yy251!=0 );
  yymsp[-1].minor.yy251->pLast = yymsp[-1].minor.yy251;
}
#line 3573 "parse.c"
        break;
      case 232: /* trnm ::= nm DOT nm */
#line 1398 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3583 "parse.c"
        break;
      case 233: /* tridxby ::= INDEXED BY nm */
#line 1410 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3592 "parse.c"
        break;
      case 234: /* tridxby ::= NOT INDEXED */
#line 1415 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3601 "parse.c"
        break;
      case 235: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1428 "parse.y"
{yymsp[-6].minor.yy251 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy272, yymsp[0].minor.yy294, yymsp[-5].minor.yy322);}
#line 3606 "parse.c"
        break;
      case 236: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1432 "parse.y"
{yymsp[-4].minor.yy251 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy436, yymsp[0].minor.yy309, yymsp[-4].minor.yy322);/*A-overwrites-R*/}
#line 3611 "parse.c"
        break;
      case 237: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1436 "parse.y"
{yymsp[-4].minor.yy251 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy294);}
#line 3616 "parse.c"
        break;
      case 238: /* trigger_cmd ::= select */
#line 1440 "parse.y"
{yymsp[0].minor.yy251 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy309); /*A-overwrites-X*/}
#line 3621 "parse.c"
        break;
      case 239: /* expr ::= RAISE LP IGNORE RP */
#line 1443 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy200,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy200.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy200.pExpr ){
    yymsp[-3].minor.yy200.pExpr->affinity = OE_Ignore;
  }
}
#line 3632 "parse.c"
        break;
      case 240: /* expr ::= RAISE LP raisetype COMMA STRING RP */
#line 1450 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy200,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy200.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy200.pExpr ) {
    yymsp[-5].minor.yy200.pExpr->affinity = (char)yymsp[-3].minor.yy322;
  }
}
#line 3643 "parse.c"
        break;
      case 241: /* raisetype ::= ROLLBACK */
#line 1460 "parse.y"
{yymsp[0].minor.yy322 = OE_Rollback;}
#line 3648 "parse.c"
        break;
      case 243: /* raisetype ::= FAIL */
#line 1462 "parse.y"
{yymsp[0].minor.yy322 = OE_Fail;}
#line 3653 "parse.c"
        break;
      case 244: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1467 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy73,yymsp[-1].minor.yy322);
}
#line 3660 "parse.c"
        break;
      case 245: /* cmd ::= REINDEX */
#line 1474 "parse.y"
{sqlite3Reindex(pParse, 0, 0);}
#line 3665 "parse.c"
        break;
      case 246: /* cmd ::= REINDEX nm */
#line 1475 "parse.y"
{sqlite3Reindex(pParse, &yymsp[0].minor.yy0, 0);}
#line 3670 "parse.c"
        break;
      case 247: /* cmd ::= REINDEX nm ON nm */
#line 1476 "parse.y"
{sqlite3Reindex(pParse, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);}
#line 3675 "parse.c"
        break;
      case 248: /* cmd ::= ANALYZE */
#line 1481 "parse.y"
{sqlite3Analyze(pParse, 0);}
#line 3680 "parse.c"
        break;
      case 249: /* cmd ::= ANALYZE nm */
#line 1482 "parse.y"
{sqlite3Analyze(pParse, &yymsp[0].minor.yy0);}
#line 3685 "parse.c"
        break;
      case 250: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1487 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy73,&yymsp[0].minor.yy0);
}
#line 3692 "parse.c"
        break;
      case 251: /* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist */
#line 1491 "parse.y"
{
  yymsp[-1].minor.yy0.n = (int)(pParse->sLastToken.z-yymsp[-1].minor.yy0.z) + pParse->sLastToken.n;
  sqlite3AlterFinishAddColumn(pParse, &yymsp[-1].minor.yy0);
}
#line 3700 "parse.c"
        break;
      case 252: /* add_column_fullname ::= fullname */
#line 1495 "parse.y"
{
  disableLookaside(pParse);
  sqlite3AlterBeginAddColumn(pParse, yymsp[0].minor.yy73);
}
#line 3708 "parse.c"
        break;
      case 253: /* with ::= */
#line 1509 "parse.y"
{yymsp[1].minor.yy445 = 0;}
#line 3713 "parse.c"
        break;
      case 254: /* with ::= WITH wqlist */
#line 1511 "parse.y"
{ yymsp[-1].minor.yy445 = yymsp[0].minor.yy445; }
#line 3718 "parse.c"
        break;
      case 255: /* with ::= WITH RECURSIVE wqlist */
#line 1512 "parse.y"
{ yymsp[-2].minor.yy445 = yymsp[0].minor.yy445; }
#line 3723 "parse.c"
        break;
      case 256: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1514 "parse.y"
{
  yymsp[-5].minor.yy445 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy272, yymsp[-1].minor.yy309); /*A-overwrites-X*/
}
#line 3730 "parse.c"
        break;
      case 257: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1517 "parse.y"
{
  yymsp[-7].minor.yy445 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy445, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy272, yymsp[-1].minor.yy309);
}
#line 3737 "parse.c"
        break;
      default:
      /* (258) input ::= ecmd */ yytestcase(yyruleno==258);
      /* (259) explain ::= */ yytestcase(yyruleno==259);
      /* (260) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno!=260);
      /* (261) trans_opt ::= */ yytestcase(yyruleno==261);
      /* (262) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==262);
      /* (263) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==263);
      /* (264) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==264);
      /* (265) savepoint_opt ::= */ yytestcase(yyruleno==265);
      /* (266) cmd ::= create_table create_table_args */ yytestcase(yyruleno==266);
      /* (267) columnlist ::= columnlist COMMA columnname carglist */ yytestcase(yyruleno==267);
      /* (268) columnlist ::= columnname carglist */ yytestcase(yyruleno==268);
      /* (269) typetoken ::= typename */ yytestcase(yyruleno==269);
      /* (270) typename ::= ID|STRING */ yytestcase(yyruleno==270);
      /* (271) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=271);
      /* (272) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=272);
      /* (273) carglist ::= carglist ccons */ yytestcase(yyruleno==273);
      /* (274) carglist ::= */ yytestcase(yyruleno==274);
      /* (275) ccons ::= NULL onconf */ yytestcase(yyruleno==275);
      /* (276) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==276);
      /* (277) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==277);
      /* (278) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=278);
      /* (279) tconscomma ::= */ yytestcase(yyruleno==279);
      /* (280) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=280);
      /* (281) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=281);
      /* (282) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=282);
      /* (283) oneselect ::= values */ yytestcase(yyruleno==283);
      /* (284) sclp ::= selcollist COMMA */ yytestcase(yyruleno==284);
      /* (285) as ::= ID|STRING */ yytestcase(yyruleno==285);
      /* (286) join_nm ::= ID|INDEXED */ yytestcase(yyruleno==286);
      /* (287) join_nm ::= JOIN_KW */ yytestcase(yyruleno==287);
      /* (288) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=288);
      /* (289) exprlist ::= nexprlist */ yytestcase(yyruleno==289);
      /* (290) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=290);
      /* (291) nmnum ::= STRING */ yytestcase(yyruleno==291);
      /* (292) nmnum ::= nm */ yytestcase(yyruleno==292);
      /* (293) nmnum ::= ON */ yytestcase(yyruleno==293);
      /* (294) nmnum ::= DELETE */ yytestcase(yyruleno==294);
      /* (295) nmnum ::= DEFAULT */ yytestcase(yyruleno==295);
      /* (296) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno==296);
      /* (297) foreach_clause ::= */ yytestcase(yyruleno==297);
      /* (298) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==298);
      /* (299) trnm ::= nm */ yytestcase(yyruleno==299);
      /* (300) tridxby ::= */ yytestcase(yyruleno==300);
      /* (301) kwcolumn_opt ::= */ yytestcase(yyruleno==301);
      /* (302) kwcolumn_opt ::= COLUMNKW */ yytestcase(yyruleno==302);
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
#line 3850 "parse.c"
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
