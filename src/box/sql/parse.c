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
/************ Begin %include sections from the grammar ************************/
#line 48 "parse.y"

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

#line 400 "parse.y"

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
        sqlite3ErrorMsg(pParse, "too many terms in compound SELECT");
      }
    }
  }
#line 829 "parse.y"

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
#line 946 "parse.y"

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
#line 1020 "parse.y"

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
#line 1037 "parse.y"

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
#line 1065 "parse.y"

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
#line 1277 "parse.y"

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
#define YYNOCODE 251
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 96
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  struct LimitVal yy64;
  Expr* yy122;
  Select* yy159;
  IdList* yy180;
  struct {int value; int mask;} yy207;
  TriggerStep* yy327;
  With* yy331;
  ExprSpan yy342;
  SrcList* yy347;
  int yy392;
  struct TrigEvent yy410;
  ExprList* yy442;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYFALLBACK 1
#define YYNSTATE             455
#define YYNRULE              328
#define YY_MAX_SHIFT         454
#define YY_MIN_SHIFTREDUCE   664
#define YY_MAX_SHIFTREDUCE   991
#define YY_MIN_REDUCE        992
#define YY_MAX_REDUCE        1319
#define YY_ERROR_ACTION      1320
#define YY_ACCEPT_ACTION     1321
#define YY_NO_ACTION         1322
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
#define YY_ACTTAB_COUNT (1559)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */   323,  828,  349,  821,    5,  201,  201,  815,   99,  100,
 /*    10 */    90,  838,  838,  850,  853,  842,  842,   97,   97,   98,
 /*    20 */    98,   98,   98,  299,   96,   96,   96,   96,   95,   95,
 /*    30 */    94,   94,   94,   93,  349,  323,  969,  969,  820,  820,
 /*    40 */   822,  938,  352,   99,  100,   90,  838,  838,  850,  853,
 /*    50 */   842,  842,   97,   97,   98,   98,   98,   98,  336,   96,
 /*    60 */    96,   96,   96,   95,   95,   94,   94,   94,   93,  349,
 /*    70 */    95,   95,   94,   94,   94,   93,  349,  787,  969,  969,
 /*    80 */   323,   94,   94,   94,   93,  349,  788,   75,   99,  100,
 /*    90 */    90,  838,  838,  850,  853,  842,  842,   97,   97,   98,
 /*   100 */    98,   98,   98,  448,   96,   96,   96,   96,   95,   95,
 /*   110 */    94,   94,   94,   93,  349, 1321,  454,    2,  132,  323,
 /*   120 */   417,  146,  694,   52,   52,   93,  349,   99,  100,   90,
 /*   130 */   838,  838,  850,  853,  842,  842,   97,   97,   98,   98,
 /*   140 */    98,   98,  101,   96,   96,   96,   96,   95,   95,   94,
 /*   150 */    94,   94,   93,  349,  950,  950,  323,  418,  426,  411,
 /*   160 */   409,   61,  748,  748,   99,  100,   90,  838,  838,  850,
 /*   170 */   853,  842,  842,   97,   97,   98,   98,   98,   98,   60,
 /*   180 */    96,   96,   96,   96,   95,   95,   94,   94,   94,   93,
 /*   190 */   349,  323,   92,   89,  177,  275,  951,  952,  371,   99,
 /*   200 */   100,   90,  838,  838,  850,  853,  842,  842,   97,   97,
 /*   210 */    98,   98,   98,   98,  299,   96,   96,   96,   96,   95,
 /*   220 */    95,   94,   94,   94,   93,  349,  323,  932, 1314,  697,
 /*   230 */   702, 1314,  240,  410,   99,  100,   90,  838,  838,  850,
 /*   240 */   853,  842,  842,   97,   97,   98,   98,   98,   98,  345,
 /*   250 */    96,   96,   96,   96,   95,   95,   94,   94,   94,   93,
 /*   260 */   349,  323,  932, 1315,  225,  695, 1315,  813,  371,   99,
 /*   270 */   100,   90,  838,  838,  850,  853,  842,  842,   97,   97,
 /*   280 */    98,   98,   98,   98,  373,   96,   96,   96,   96,   95,
 /*   290 */    95,   94,   94,   94,   93,  349,  323,  445,  445,  445,
 /*   300 */   829,  930,  988,  316,   99,  100,   90,  838,  838,  850,
 /*   310 */   853,  842,  842,   97,   97,   98,   98,   98,   98,  377,
 /*   320 */    96,   96,   96,   96,   95,   95,   94,   94,   94,   93,
 /*   330 */   349,  323,  332,  327,  229,  814,  930,  735,  735,   99,
 /*   340 */   100,   90,  838,  838,  850,  853,  842,  842,   97,   97,
 /*   350 */    98,   98,   98,   98,  696,   96,   96,   96,   96,   95,
 /*   360 */    95,   94,   94,   94,   93,  349,  323,  733,  733,   92,
 /*   370 */    89,  177,  813,  298,   99,  100,   90,  838,  838,  850,
 /*   380 */   853,  842,  842,   97,   97,   98,   98,   98,   98,  915,
 /*   390 */    96,   96,   96,   96,   95,   95,   94,   94,   94,   93,
 /*   400 */   349,  323,  266,  961,  382,  228,  147,  379,  447,   99,
 /*   410 */   100,   90,  838,  838,  850,  853,  842,  842,   97,   97,
 /*   420 */    98,   98,   98,   98,  294,   96,   96,   96,   96,   95,
 /*   430 */    95,   94,   94,   94,   93,  349,  323,  333,  298,  950,
 /*   440 */   950,  157,   25,  248,   99,  100,   90,  838,  838,  850,
 /*   450 */   853,  842,  842,   97,   97,   98,   98,   98,   98,  448,
 /*   460 */    96,   96,   96,   96,   95,   95,   94,   94,   94,   93,
 /*   470 */   349,  441,  348,  348,  348,  950,  950,  954,  323,   52,
 /*   480 */    52,  951,  952,  806,  171,   78,   99,  100,   90,  838,
 /*   490 */   838,  850,  853,  842,  842,   97,   97,   98,   98,   98,
 /*   500 */    98,  377,   96,   96,   96,   96,   95,   95,   94,   94,
 /*   510 */    94,   93,  349,  323,  426,  416,  120,  951,  952,  954,
 /*   520 */    81,   99,   88,   90,  838,  838,  850,  853,  842,  842,
 /*   530 */    97,   97,   98,   98,   98,   98,  437,   96,   96,   96,
 /*   540 */    96,   95,   95,   94,   94,   94,   93,  349,  323,  839,
 /*   550 */   839,  851,  854,  688,  329,  341,  377,  100,   90,  838,
 /*   560 */   838,  850,  853,  842,  842,   97,   97,   98,   98,   98,
 /*   570 */    98,  713,   96,   96,   96,   96,   95,   95,   94,   94,
 /*   580 */    94,   93,  349,  323,  895,  895,  385,  258,   72,  338,
 /*   590 */   669,  670,  671,   90,  838,  838,  850,  853,  842,  842,
 /*   600 */    97,   97,   98,   98,   98,   98,  359,   96,   96,   96,
 /*   610 */    96,   95,   95,   94,   94,   94,   93,  349,   86,  443,
 /*   620 */   843,    3, 1195,  359,  358,  132,  420,  809,  950,  950,
 /*   630 */   772,   86,  443,  360,    3,  210,  168,  285,  403,  280,
 /*   640 */   402,  197,  230,  448,  175,  756,   83,   84,  278,  813,
 /*   650 */   260,  363,  249,   85,  350,  350,   92,   89,  177,   83,
 /*   660 */    84,  240,  410,   52,   52,  446,   85,  350,  350,  448,
 /*   670 */   951,  952,  192,  453,  453,  400,  397,  396,  446,  241,
 /*   680 */   219,  114,  432,  772,  359,  448,  395,  712,  743,   10,
 /*   690 */    10,  132,  132,  957,  828,  432,  450,  449,  426,  425,
 /*   700 */   815,  704,  176,  335,  132,   52,   52,  828,  266,  450,
 /*   710 */   449,  813,  192,  815,  334,  400,  397,  396,  448, 1261,
 /*   720 */  1261,   23,  950,  950,   86,  443,  395,    3,  772,  427,
 /*   730 */   889,  820,  820,  822,  823,   19,  296,  716,   52,   52,
 /*   740 */   426,  406,  444,  329,  820,  820,  822,  823,   19,  265,
 /*   750 */   401,  153,   83,   84,  757,  176,  745,  448,  717,   85,
 /*   760 */   350,  350,  120, 1288,  951,  952,  725,  413,  407,  353,
 /*   770 */   923,  446,  786,  426,  428,  785,  384,   32,   32,   86,
 /*   780 */   443,  772,    3,  339,   98,   98,   98,   98,  432,   96,
 /*   790 */    96,   96,   96,   95,   95,   94,   94,   94,   93,  349,
 /*   800 */   828,  120,  450,  449,  809,  813,  815,   83,   84,  243,
 /*   810 */   809,  132,  408,  883,   85,  350,  350,  132,   92,   89,
 /*   820 */   177,   92,   89,  177,  911,  244,  446,  260,  368,  259,
 /*   830 */   883,  885,  415,  260,  368,  259,  266,  820,  820,  822,
 /*   840 */   823,   19,  201,  432,   96,   96,   96,   96,   95,   95,
 /*   850 */    94,   94,   94,   93,  349,  828,  711,  450,  449,  682,
 /*   860 */   758,  815,  448,  347,  346,  120,  950,  950,  908,  912,
 /*   870 */   343,  950,  950,  969,  247,   76,  443,  356,    3,  448,
 /*   880 */   198,  174,   51,   51,  892,  913,  448,  736,  891,  950,
 /*   890 */   950,  883,  820,  820,  822,  823,   19,  737,  928,   52,
 /*   900 */    52,  120,  383,   83,   84,  906,   10,   10,  951,  952,
 /*   910 */    85,  350,  350,  951,  952,  969,  976,  950,  950,  741,
 /*   920 */   337,  156,  446,  974,  369,  975,  354,  730,  709,  366,
 /*   930 */   239,  951,  952,  318,  342,  297,  730,   24,  120,  432,
 /*   940 */    58,  873,  367,  347,  346,   82,    1,   80,  977,  908,
 /*   950 */   977,  828,  120,  450,  449,  950,  950,  815,  774,  951,
 /*   960 */   952,   98,   98,   98,   98,   91,   96,   96,   96,   96,
 /*   970 */    95,   95,   94,   94,   94,   93,  349,  255, 1202,  950,
 /*   980 */   950,  254,  120,  763,  103,  392,  773,  448,  820,  820,
 /*   990 */   822,  823,   19,  448,  224,  448,  196,  951,  952,  313,
 /*  1000 */   312,  311,  213,  309,  903,  369,  679,   52,   52,  170,
 /*  1010 */   169,  911,  156,   10,   10,   10,   10,  698,  698,  414,
 /*  1020 */   405,  951,  952,  378,  322,  222,  222,  231,  977,  423,
 /*  1030 */   977,  753,  330,  133,  266,  902,  752,  413,  266,    9,
 /*  1040 */     9,  448,  421,  232,  448,  232,  448,  685,  448,  179,
 /*  1050 */   361,  246,  380,  251,   74,  253,  912,  828,  266,  821,
 /*  1060 */   245,   36,   36,  815,   37,   37,   12,   12,   27,   27,
 /*  1070 */   448,  181,  913,  328,  431,  226,  156,  448,  278,  227,
 /*  1080 */   120,  132,  180,  434,  308,  262,  448,  753,  319,  448,
 /*  1090 */    38,   38,  752,  448,  820,  820,  822,   39,   39,  914,
 /*  1100 */   448,  266,  448,  324,  267,  252,   40,   40,  448,   41,
 /*  1110 */    41,  448,  270,   42,   42,  448,  120,  438,  448,  266,
 /*  1120 */    28,   28,   29,   29,  448,  272,  448,  196,   31,   31,
 /*  1130 */   357,   43,   43,  448,  707,   44,   44,  448,   45,   45,
 /*  1140 */   787,  448,  881,  448,   11,   11,   46,   46,  448,  788,
 /*  1150 */   120,  448,  274,  105,  105,  448,  726,   47,   47,  448,
 /*  1160 */   742,   48,   48,   33,   33,  448,  117,  448,   49,   49,
 /*  1170 */   448,   50,   50,  448,  968,   34,   34,  448,  710,  122,
 /*  1180 */   122,  448,  195,  194,  193,  123,  123,  124,  124,  448,
 /*  1190 */    56,   56,  448,   35,   35,  448,  273,  106,  106,  692,
 /*  1200 */   448,   53,   53,  448,  328,  448,  118,  448,  161,  107,
 /*  1210 */   107,  448,  108,  108,  448,  104,  104,  448,  266,  448,
 /*  1220 */   121,  121,  448,  119,  119,  112,  112,  111,  111,  448,
 /*  1230 */   375,  109,  109,  448,  110,  110,  223,   55,   55,   57,
 /*  1240 */    57,  692,   54,   54,  222,  222,  315,  990,   20,   26,
 /*  1250 */    26,  315,  931,   30,   30,  442,  413,  221,  174,  738,
 /*  1260 */   705,  927,  811,   21,  317,  200,  370,  376,  268,  200,
 /*  1270 */   271,  166,  372,  261,  393,  200,   74,  206,  276,  722,
 /*  1280 */   723,   74,  715,  714,  284,  880,  750,  781,  120,   77,
 /*  1290 */   200,  876,  279,  779,  206,  283,  888,  887,  888,  887,
 /*  1300 */   690,  824,  705,  116,  812,  759,  770,  235,  429,  430,
 /*  1310 */   300,  301,  819,  693,  687,  287,  388,  676,  675,  677,
 /*  1320 */   991,  944,  216,  289,  291,  991,  158,  880,    7,  801,
 /*  1330 */   314,  362,  257,  250,  905,  172, 1278,  374,  293,  433,
 /*  1340 */   947,  985,  306,  824,  398,  135,  282,  878,  877,  203,
 /*  1350 */   709,  922,  920,  167,  982,   59,   62,  331,  144,  155,
 /*  1360 */   130,   72,  364,  137,  365,  391,  183,  187,  139,  140,
 /*  1370 */   141,  381,   67,  890,  387,  771,  798,  159,  142,  148,
 /*  1380 */   808,  264,  154,  217,  389,  907,  269,  404,  872,  188,
 /*  1390 */   189,  320,  678,  190,  729,  728,  727,  340,  707,  720,
 /*  1400 */   719,  419,   71,    6,  767,  768,  202,   79,  344,  286,
 /*  1410 */   288,  295,  766,  321,  701,  700,  290,  281,  699,  765,
 /*  1420 */   959,  292,  102,  858,  749,  424,  236,  211,  436,   73,
 /*  1430 */   440,  237,  422,   22,  684,  451,  945,  215,  212,  214,
 /*  1440 */   238,  452,  125,  134,  673,  302,  672,  667,  126,  666,
 /*  1450 */   351,  165,  127,  242,  178,  355,  115,  305,  303,  304,
 /*  1460 */   233,  886,  113,  884,  325,  807,  136,  128,  739,  138,
 /*  1470 */   256,  901,  143,  182,  145,  129,  904,  184,   63,   64,
 /*  1480 */    65,   66,  900,    8,  185,   13,  186,  200,  893,  149,
 /*  1490 */   263,  979,  150,  386,  681,  160,  390,  191,  283,  277,
 /*  1500 */   747,  151,   68,  394,   14,   15,  399,  326,  718,   69,
 /*  1510 */    70,  234,  827,  826,  856,   16,    4,  131,  751,  173,
 /*  1520 */   218,  220,  412,  780,  199,  152,   77,  775,   17,   18,
 /*  1530 */    74,  871,  857,  855,  910,  860,  909,  205,  204,  936,
 /*  1540 */   162,  435,  939,  937,  163,  207,  992,  439,  859,  164,
 /*  1550 */   208,  825,  691,   87,  310,  209, 1280, 1279,  307,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */    19,   95,   53,   97,   22,   24,   24,  101,   27,   28,
 /*    10 */    29,   30,   31,   32,   33,   34,   35,   36,   37,   38,
 /*    20 */    39,   40,   41,  151,   43,   44,   45,   46,   47,   48,
 /*    30 */    49,   50,   51,   52,   53,   19,   55,   55,  132,  133,
 /*    40 */   134,    1,    2,   27,   28,   29,   30,   31,   32,   33,
 /*    50 */    34,   35,   36,   37,   38,   39,   40,   41,  186,   43,
 /*    60 */    44,   45,   46,   47,   48,   49,   50,   51,   52,   53,
 /*    70 */    47,   48,   49,   50,   51,   52,   53,   61,   97,   97,
 /*    80 */    19,   49,   50,   51,   52,   53,   70,   26,   27,   28,
 /*    90 */    29,   30,   31,   32,   33,   34,   35,   36,   37,   38,
 /*   100 */    39,   40,   41,  151,   43,   44,   45,   46,   47,   48,
 /*   110 */    49,   50,   51,   52,   53,  144,  145,  146,   92,   19,
 /*   120 */    19,   22,  171,  171,  172,   52,   53,   27,   28,   29,
 /*   130 */    30,   31,   32,   33,   34,   35,   36,   37,   38,   39,
 /*   140 */    40,   41,   81,   43,   44,   45,   46,   47,   48,   49,
 /*   150 */    50,   51,   52,   53,   55,   56,   19,   56,  206,  207,
 /*   160 */   115,   24,  117,  118,   27,   28,   29,   30,   31,   32,
 /*   170 */    33,   34,   35,   36,   37,   38,   39,   40,   41,   79,
 /*   180 */    43,   44,   45,   46,   47,   48,   49,   50,   51,   52,
 /*   190 */    53,   19,  220,  221,  222,   23,   97,   98,  151,   27,
 /*   200 */    28,   29,   30,   31,   32,   33,   34,   35,   36,   37,
 /*   210 */    38,   39,   40,   41,  151,   43,   44,   45,   46,   47,
 /*   220 */    48,   49,   50,   51,   52,   53,   19,   22,   23,  171,
 /*   230 */    23,   26,  119,  120,   27,   28,   29,   30,   31,   32,
 /*   240 */    33,   34,   35,   36,   37,   38,   39,   40,   41,  186,
 /*   250 */    43,   44,   45,   46,   47,   48,   49,   50,   51,   52,
 /*   260 */    53,   19,   22,   23,  217,   23,   26,  151,  151,   27,
 /*   270 */    28,   29,   30,   31,   32,   33,   34,   35,   36,   37,
 /*   280 */    38,   39,   40,   41,  151,   43,   44,   45,   46,   47,
 /*   290 */    48,   49,   50,   51,   52,   53,   19,  167,  168,  169,
 /*   300 */    23,   96,  246,  247,   27,   28,   29,   30,   31,   32,
 /*   310 */    33,   34,   35,   36,   37,   38,   39,   40,   41,  151,
 /*   320 */    43,   44,   45,   46,   47,   48,   49,   50,   51,   52,
 /*   330 */    53,   19,  216,  156,  217,   23,   96,  189,  190,   27,
 /*   340 */    28,   29,   30,   31,   32,   33,   34,   35,   36,   37,
 /*   350 */    38,   39,   40,   41,  171,   43,   44,   45,   46,   47,
 /*   360 */    48,   49,   50,   51,   52,   53,   19,  189,  190,  220,
 /*   370 */   221,  222,  151,  151,   27,   28,   29,   30,   31,   32,
 /*   380 */    33,   34,   35,   36,   37,   38,   39,   40,   41,  240,
 /*   390 */    43,   44,   45,   46,   47,   48,   49,   50,   51,   52,
 /*   400 */    53,   19,  151,  184,  227,  237,   22,  230,  151,   27,
 /*   410 */    28,   29,   30,   31,   32,   33,   34,   35,   36,   37,
 /*   420 */    38,   39,   40,   41,  151,   43,   44,   45,   46,   47,
 /*   430 */    48,   49,   50,   51,   52,   53,   19,  216,  151,   55,
 /*   440 */    56,   24,   22,  192,   27,   28,   29,   30,   31,   32,
 /*   450 */    33,   34,   35,   36,   37,   38,   39,   40,   41,  151,
 /*   460 */    43,   44,   45,   46,   47,   48,   49,   50,   51,   52,
 /*   470 */    53,  249,  167,  168,  169,   55,   56,   55,   19,  171,
 /*   480 */   172,   97,   98,  162,   26,  138,   27,   28,   29,   30,
 /*   490 */    31,   32,   33,   34,   35,   36,   37,   38,   39,   40,
 /*   500 */    41,  151,   43,   44,   45,   46,   47,   48,   49,   50,
 /*   510 */    51,   52,   53,   19,  206,  207,  195,   97,   98,   97,
 /*   520 */   138,   27,   28,   29,   30,   31,   32,   33,   34,   35,
 /*   530 */    36,   37,   38,   39,   40,   41,  249,   43,   44,   45,
 /*   540 */    46,   47,   48,   49,   50,   51,   52,   53,   19,   30,
 /*   550 */    31,   32,   33,  165,  166,   19,  151,   28,   29,   30,
 /*   560 */    31,   32,   33,   34,   35,   36,   37,   38,   39,   40,
 /*   570 */    41,  180,   43,   44,   45,   46,   47,   48,   49,   50,
 /*   580 */    51,   52,   53,   19,  108,  109,  110,  237,  130,   53,
 /*   590 */     7,    8,    9,   29,   30,   31,   32,   33,   34,   35,
 /*   600 */    36,   37,   38,   39,   40,   41,  151,   43,   44,   45,
 /*   610 */    46,   47,   48,   49,   50,   51,   52,   53,   19,   20,
 /*   620 */   101,   22,   23,  168,  169,   92,  151,   85,   55,   56,
 /*   630 */    26,   19,   20,  100,   22,   99,  100,  101,  102,  103,
 /*   640 */   104,  105,  237,  151,  151,  209,   47,   48,  112,  151,
 /*   650 */   108,  109,  110,   54,   55,   56,  220,  221,  222,   47,
 /*   660 */    48,  119,  120,  171,  172,   66,   54,   55,   56,  151,
 /*   670 */    97,   98,   99,  147,  148,  102,  103,  104,   66,  153,
 /*   680 */    23,  155,   83,   26,  229,  151,  113,  180,  162,  171,
 /*   690 */   172,   92,   92,  170,   95,   83,   97,   98,  206,  207,
 /*   700 */   101,  178,   98,  185,   92,  171,  172,   95,  151,   97,
 /*   710 */    98,  151,   99,  101,  216,  102,  103,  104,  151,  119,
 /*   720 */   120,  195,   55,   56,   19,   20,  113,   22,  124,  162,
 /*   730 */    11,  132,  133,  134,  135,  136,  151,   65,  171,  172,
 /*   740 */   206,  207,  165,  166,  132,  133,  134,  135,  136,  192,
 /*   750 */    78,   84,   47,   48,   49,   98,  194,  151,   86,   54,
 /*   760 */    55,   56,  195,   23,   97,   98,   26,  205,  162,  243,
 /*   770 */   151,   66,  174,  206,  207,  174,  216,  171,  172,   19,
 /*   780 */    20,  124,   22,  111,   38,   39,   40,   41,   83,   43,
 /*   790 */    44,   45,   46,   47,   48,   49,   50,   51,   52,   53,
 /*   800 */    95,  195,   97,   98,   85,  151,  101,   47,   48,  151,
 /*   810 */    85,   92,  206,  151,   54,   55,   56,   92,  220,  221,
 /*   820 */   222,  220,  221,  222,   12,  151,   66,  108,  109,  110,
 /*   830 */   168,  169,  162,  108,  109,  110,  151,  132,  133,  134,
 /*   840 */   135,  136,   24,   83,   43,   44,   45,   46,   47,   48,
 /*   850 */    49,   50,   51,   52,   53,   95,  180,   97,   98,   21,
 /*   860 */    49,  101,  151,   47,   48,  195,   55,   56,  162,   57,
 /*   870 */   216,   55,   56,   55,  151,   19,   20,  192,   22,  151,
 /*   880 */   210,  211,  171,  172,   59,   73,  151,   75,   63,   55,
 /*   890 */    56,  229,  132,  133,  134,  135,  136,   85,  151,  171,
 /*   900 */   172,  195,   77,   47,   48,  162,  171,  172,   97,   98,
 /*   910 */    54,   55,   56,   97,   98,   97,  100,   55,   56,  162,
 /*   920 */   185,  151,   66,  107,  218,  109,  241,  178,  179,   91,
 /*   930 */   198,   97,   98,  163,  206,  224,  187,  231,  195,   83,
 /*   940 */   208,  103,  236,   47,   48,  137,   22,  139,  132,  162,
 /*   950 */   134,   95,  195,   97,   98,   55,   56,  101,  124,   97,
 /*   960 */    98,   38,   39,   40,   41,   42,   43,   44,   45,   46,
 /*   970 */    47,   48,   49,   50,   51,   52,   53,  151,  140,   55,
 /*   980 */    56,   16,  195,  212,   22,   19,  124,  151,  132,  133,
 /*   990 */   134,  135,  136,  151,    5,  151,   30,   97,   98,   10,
 /*  1000 */    11,   12,   13,   14,  151,  218,   17,  171,  172,   47,
 /*  1010 */    48,   12,  151,  171,  172,  171,  172,   55,   56,  151,
 /*  1020 */    49,   97,   98,  236,  163,  193,  194,  185,  132,  185,
 /*  1030 */   134,  116,  244,  245,  151,  151,  121,  205,  151,  171,
 /*  1040 */   172,  151,  206,  182,  151,  184,  151,  162,  151,   60,
 /*  1050 */   218,   62,  151,   88,   26,   90,   57,   95,  151,   97,
 /*  1060 */    71,  171,  172,  101,  171,  172,  171,  172,  171,  172,
 /*  1070 */   151,   82,   73,  107,   75,  192,  151,  151,  112,  192,
 /*  1080 */   195,   92,   93,  162,  159,  151,  151,  116,  163,  151,
 /*  1090 */   171,  172,  121,  151,  132,  133,  134,  171,  172,  192,
 /*  1100 */   151,  151,  151,  114,  151,  140,  171,  172,  151,  171,
 /*  1110 */   172,  151,  151,  171,  172,  151,  195,  162,  151,  151,
 /*  1120 */   171,  172,  171,  172,  151,  151,  151,   30,  171,  172,
 /*  1130 */   141,  171,  172,  151,  106,  171,  172,  151,  171,  172,
 /*  1140 */    61,  151,  192,  151,  171,  172,  171,  172,  151,   70,
 /*  1150 */   195,  151,  151,  171,  172,  151,  151,  171,  172,  151,
 /*  1160 */   192,  171,  172,  171,  172,  151,   22,  151,  171,  172,
 /*  1170 */   151,  171,  172,  151,   26,  171,  172,  151,  151,  171,
 /*  1180 */   172,  151,  108,  109,  110,  171,  172,  171,  172,  151,
 /*  1190 */   171,  172,  151,  171,  172,  151,   16,  171,  172,   55,
 /*  1200 */   151,  171,  172,  151,  107,  151,   22,  151,   24,  171,
 /*  1210 */   172,  151,  171,  172,  151,  171,  172,  151,  151,  151,
 /*  1220 */   171,  172,  151,  171,  172,  171,  172,  171,  172,  151,
 /*  1230 */    19,  171,  172,  151,  171,  172,   22,  171,  172,  171,
 /*  1240 */   172,   97,  171,  172,  193,  194,   22,   23,   22,  171,
 /*  1250 */   172,   22,   23,  171,  172,  162,  205,  210,  211,  192,
 /*  1260 */    55,   23,   23,   37,   26,   26,   23,   56,   88,   26,
 /*  1270 */    90,  123,   23,   23,   23,   26,   26,   26,   23,    7,
 /*  1280 */     8,   26,  100,  101,  101,   55,   23,   23,  195,   26,
 /*  1290 */    26,   23,  151,  151,   26,  112,  132,  132,  134,  134,
 /*  1300 */    23,   55,   97,   26,  151,  151,  151,  209,  151,  190,
 /*  1310 */   151,  151,  151,  151,  151,  209,  233,  151,  151,  151,
 /*  1320 */    96,  151,  232,  209,  209,   96,  196,   97,  197,  200,
 /*  1330 */   149,  213,  238,  213,  200,  183,  122,  238,  213,  226,
 /*  1340 */   154,   67,  199,   97,  175,  242,  174,  174,  174,  122,
 /*  1350 */   179,  158,  158,  197,   69,  239,  239,  158,   22,  219,
 /*  1360 */    27,  130,   18,  188,  158,   18,  157,  157,  191,  191,
 /*  1370 */   191,  158,  137,  235,   74,  158,  200,  219,  191,  188,
 /*  1380 */   188,  234,   22,  158,  176,  200,  158,  107,  200,  157,
 /*  1390 */   157,  176,  158,  157,  173,  173,  173,   76,  106,  181,
 /*  1400 */   181,  125,  107,   22,  215,  215,  158,  137,   53,  214,
 /*  1410 */   214,  158,  215,  176,  173,  175,  214,  173,  173,  215,
 /*  1420 */   173,  214,  129,  223,  204,  126,  225,   25,  176,  128,
 /*  1430 */   176,  228,  127,   26,  161,  160,   13,    6,  152,  152,
 /*  1440 */   228,  150,  164,  245,  150,  203,  150,  150,  164,    4,
 /*  1450 */     3,   22,  164,  142,   15,   94,  177,  200,  202,  201,
 /*  1460 */   177,   23,   16,   23,  248,  120,  131,  111,   20,  123,
 /*  1470 */    16,    1,  123,  125,  131,  111,   56,   64,   37,   37,
 /*  1480 */    37,   37,    1,    5,  122,   22,  107,   26,   80,   80,
 /*  1490 */   140,   87,  107,   72,   20,   24,   19,  105,  112,   23,
 /*  1500 */   116,   22,   22,   79,   22,   22,   79,  248,   58,   22,
 /*  1510 */    26,   79,   23,   23,   23,   22,   22,   68,   23,  122,
 /*  1520 */    23,   23,   26,   56,   64,   22,   26,  124,   64,   64,
 /*  1530 */    26,   23,   23,   23,   23,   11,   23,   22,   26,   23,
 /*  1540 */    22,   24,    1,   23,   22,   26,    0,   24,   23,   22,
 /*  1550 */   122,   23,   23,   22,   15,  122,  122,  122,   23,
};
#define YY_SHIFT_USE_DFLT (1559)
#define YY_SHIFT_COUNT    (454)
#define YY_SHIFT_MIN      (-94)
#define YY_SHIFT_MAX      (1546)
static const short yy_shift_ofst[] = {
 /*     0 */    40,  599,  989,  612,  760,  760,  760,  760,  725,  -19,
 /*    10 */    16,   16,  100,  760,  760,  760,  760,  760,  760,  760,
 /*    20 */   816,  816,  573,  542,  719,  600,   61,  137,  172,  207,
 /*    30 */   242,  277,  312,  347,  382,  417,  459,  459,  459,  459,
 /*    40 */   459,  459,  459,  459,  459,  459,  459,  459,  459,  459,
 /*    50 */   459,  459,  459,  494,  459,  529,  564,  564,  705,  760,
 /*    60 */   760,  760,  760,  760,  760,  760,  760,  760,  760,  760,
 /*    70 */   760,  760,  760,  760,  760,  760,  760,  760,  760,  760,
 /*    80 */   760,  760,  760,  760,  760,  760,  760,  760,  760,  760,
 /*    90 */   856,  760,  760,  760,  760,  760,  760,  760,  760,  760,
 /*   100 */   760,  760,  760,  760,  923,  746,  746,  746,  746,  746,
 /*   110 */   801,   23,   32,  900,  838,  966,  896,  896,  900,   73,
 /*   120 */   113,  -51, 1559, 1559, 1559,  536,  536,  536,   99,   99,
 /*   130 */   812,  812,  667,  205,  240,  900,  900,  900,  900,  900,
 /*   140 */   900,  900,  900,  900,  900,  900,  900,  900,  900,  900,
 /*   150 */   900,  900,  900,  900,  900,  533,  422,  422,  113,   26,
 /*   160 */    26,   26,   26,   26,   26, 1559, 1559, 1559,  962,  -94,
 /*   170 */   -94,  384,  613,  811,  420,  834,  862,  924,  900,  900,
 /*   180 */   900,  900,  900,  900,  900,  900,  900,  900,  900,  900,
 /*   190 */   900,  900,  900,  672,  672,  672,  900,  900,  657,  900,
 /*   200 */   900,  900,  -18,  900,  900,  999,  900,  900,  900,  900,
 /*   210 */   900,  900,  900,  900,  900,  900,  476,  825,  818,  818,
 /*   220 */   818,  604,   45,  971,  583,  458,  101,  101, 1211,  458,
 /*   230 */  1211, 1028,  740, 1097, 1079,  101,  808, 1079, 1079, 1148,
 /*   240 */   915, 1184, 1274, 1227, 1227, 1285, 1285, 1227, 1336, 1333,
 /*   250 */  1231, 1344, 1344, 1344, 1344, 1227, 1347, 1231, 1336, 1333,
 /*   260 */  1333, 1231, 1227, 1347, 1235, 1300, 1227, 1227, 1347, 1360,
 /*   270 */  1227, 1347, 1227, 1347, 1360, 1280, 1280, 1280, 1321, 1360,
 /*   280 */  1280, 1292, 1280, 1321, 1280, 1280, 1276, 1295, 1276, 1295,
 /*   290 */  1276, 1295, 1276, 1295, 1227, 1381, 1227, 1270, 1360, 1355,
 /*   300 */  1355, 1360, 1293, 1299, 1301, 1305, 1231, 1402, 1407, 1423,
 /*   310 */  1423, 1431, 1431, 1431, 1431, 1559, 1559, 1559, 1559, 1559,
 /*   320 */  1559, 1559, 1559,  519,  965, 1224, 1229, 1180, 1074, 1144,
 /*   330 */  1238, 1226, 1239, 1243, 1249, 1250, 1251, 1255, 1205, 1182,
 /*   340 */  1272, 1183, 1263, 1264, 1230, 1268, 1164, 1165, 1277, 1246,
 /*   350 */  1214, 1445, 1447, 1429, 1311, 1439, 1361, 1446, 1438, 1440,
 /*   360 */  1345, 1335, 1356, 1346, 1448, 1348, 1454, 1470, 1349, 1343,
 /*   370 */  1441, 1442, 1443, 1444, 1364, 1420, 1413, 1362, 1481, 1478,
 /*   380 */  1463, 1379, 1350, 1408, 1461, 1409, 1404, 1421, 1385, 1471,
 /*   390 */  1474, 1477, 1386, 1392, 1479, 1424, 1480, 1482, 1476, 1483,
 /*   400 */  1427, 1450, 1487, 1432, 1449, 1489, 1490, 1491, 1484, 1384,
 /*   410 */  1493, 1495, 1494, 1496, 1397, 1497, 1498, 1467, 1460, 1503,
 /*   420 */  1403, 1500, 1464, 1504, 1465, 1508, 1500, 1509, 1510, 1511,
 /*   430 */  1512, 1513, 1515, 1524, 1516, 1518, 1517, 1519, 1520, 1522,
 /*   440 */  1523, 1519, 1525, 1527, 1528, 1529, 1531, 1428, 1433, 1434,
 /*   450 */  1435, 1535, 1539, 1541, 1546,
};
#define YY_REDUCE_USE_DFLT (-129)
#define YY_REDUCE_COUNT (322)
#define YY_REDUCE_MIN   (-128)
#define YY_REDUCE_MAX   (1297)
static const short yy_reduce_ofst[] = {
 /*     0 */   -29,  567,  526,  606,  -48,  308,  492,  534,  706,  436,
 /*    10 */   598,  601,  149,  518,  735,  842,  728,  836,  844,  711,
 /*    20 */   455,  662,  861,  832,  787,  670,  -28,  -28,  -28,  -28,
 /*    30 */   -28,  -28,  -28,  -28,  -28,  -28,  -28,  -28,  -28,  -28,
 /*    40 */   -28,  -28,  -28,  -28,  -28,  -28,  -28,  -28,  -28,  -28,
 /*    50 */   -28,  -28,  -28,  -28,  -28,  -28,  -28,  -28,  868,  890,
 /*    60 */   893,  895,  897,  919,  926,  935,  938,  942,  949,  951,
 /*    70 */   957,  960,  964,  967,  973,  975,  982,  986,  990,  992,
 /*    80 */   997, 1000, 1004, 1008, 1014, 1016, 1019, 1022, 1026, 1030,
 /*    90 */  1038, 1041, 1044, 1049, 1052, 1054, 1056, 1060, 1063, 1066,
 /*   100 */  1068, 1071, 1078, 1082,  -28,  -28,  -28,  -28,  -28,  -28,
 /*   110 */   -28,  -28,  -28,  685,  177,  749,  130,  305,  925,  -28,
 /*   120 */  1051,  -28,  -28,  -28,  -28,  523,  523,  523,   47,  117,
 /*   130 */   148,  178,  222,   56,   56,  770,  251,  883,  887,  907,
 /*   140 */   950,  968, 1067,  168,  116,  350,  221,  498,  405,  560,
 /*   150 */   557, -128,  654,  287,   63,  321,  388,  577,  562,  743,
 /*   160 */   757,  885,  921,  955, 1093,  788, 1047,  732,  -49,   58,
 /*   170 */   183,  133,  219,  257,  273,  475,  493,  585,  619,  658,
 /*   180 */   674,  723,  747,  826,  853,  884,  901,  934,  953,  961,
 /*   190 */   974, 1001, 1005,  391,  507,  676, 1027, 1141,  771, 1142,
 /*   200 */  1153, 1154, 1098, 1155, 1157, 1119, 1159, 1160, 1161,  257,
 /*   210 */  1162, 1163, 1166, 1167, 1168, 1170, 1083, 1090, 1106, 1114,
 /*   220 */  1115,  771, 1130, 1131, 1181, 1129, 1118, 1120, 1094, 1134,
 /*   230 */  1099, 1169, 1152, 1171, 1172, 1125, 1113, 1173, 1174, 1143,
 /*   240 */  1156, 1186, 1103, 1193, 1194, 1116, 1117, 1199, 1140, 1175,
 /*   250 */  1176, 1177, 1178, 1179, 1187, 1206, 1209, 1185, 1158, 1191,
 /*   260 */  1192, 1188, 1213, 1210, 1138, 1147, 1217, 1225, 1232, 1208,
 /*   270 */  1228, 1233, 1234, 1236, 1215, 1221, 1222, 1223, 1218, 1237,
 /*   280 */  1241, 1240, 1244, 1219, 1245, 1247, 1189, 1195, 1190, 1196,
 /*   290 */  1197, 1202, 1204, 1207, 1248, 1200, 1253, 1201, 1252, 1203,
 /*   300 */  1212, 1254, 1220, 1242, 1256, 1258, 1257, 1273, 1275, 1286,
 /*   310 */  1287, 1291, 1294, 1296, 1297, 1216, 1259, 1198, 1278, 1284,
 /*   320 */  1279, 1283, 1288,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1268, 1261, 1261, 1261, 1195, 1195, 1195, 1195, 1261, 1088,
 /*    10 */  1117, 1117, 1245, 1320, 1320, 1320, 1320, 1320, 1320, 1194,
 /*    20 */  1320, 1320, 1320, 1320, 1261, 1092, 1123, 1320, 1320, 1320,
 /*    30 */  1320, 1196, 1197, 1320, 1320, 1320, 1244, 1246, 1133, 1132,
 /*    40 */  1131, 1130, 1227, 1104, 1128, 1121, 1125, 1196, 1190, 1191,
 /*    50 */  1189, 1193, 1197, 1320, 1124, 1159, 1174, 1158, 1320, 1320,
 /*    60 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*    70 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*    80 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*    90 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   100 */  1320, 1320, 1320, 1320, 1168, 1173, 1180, 1172, 1169, 1161,
 /*   110 */  1160, 1162, 1163, 1320, 1011, 1059, 1320, 1320, 1320, 1164,
 /*   120 */  1320, 1165, 1177, 1176, 1175, 1252, 1277, 1276, 1320, 1320,
 /*   130 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   140 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   150 */  1320, 1320, 1320, 1320, 1320, 1261, 1017, 1017, 1320, 1261,
 /*   160 */  1261, 1261, 1261, 1261, 1261, 1257, 1092, 1083, 1320, 1320,
 /*   170 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1249,
 /*   180 */  1247, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   190 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   200 */  1320, 1320, 1088, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   210 */  1320, 1320, 1320, 1320, 1320, 1271, 1320, 1222, 1088, 1088,
 /*   220 */  1088, 1090, 1072, 1082,  996, 1127, 1106, 1106, 1309, 1127,
 /*   230 */  1309, 1034, 1291, 1031, 1117, 1106, 1192, 1117, 1117, 1089,
 /*   240 */  1082, 1320, 1312, 1097, 1097, 1311, 1311, 1097, 1138, 1062,
 /*   250 */  1127, 1068, 1068, 1068, 1068, 1097, 1008, 1127, 1138, 1062,
 /*   260 */  1062, 1127, 1097, 1008, 1226, 1306, 1097, 1097, 1008, 1203,
 /*   270 */  1097, 1008, 1097, 1008, 1203, 1060, 1060, 1060, 1049, 1203,
 /*   280 */  1060, 1034, 1060, 1049, 1060, 1060, 1110, 1105, 1110, 1105,
 /*   290 */  1110, 1105, 1110, 1105, 1097, 1198, 1097, 1320, 1203, 1207,
 /*   300 */  1207, 1203, 1122, 1111, 1120, 1118, 1127, 1014, 1052, 1274,
 /*   310 */  1274, 1270, 1270, 1270, 1270, 1317, 1317, 1257, 1286, 1286,
 /*   320 */  1036, 1036, 1286, 1320, 1320, 1320, 1320, 1320, 1320, 1281,
 /*   330 */  1320, 1210, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   340 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   350 */  1144, 1320,  993, 1254, 1320, 1320, 1253, 1320, 1320, 1320,
 /*   360 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   370 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1308, 1320, 1320,
 /*   380 */  1320, 1320, 1320, 1320, 1225, 1224, 1320, 1320, 1320, 1320,
 /*   390 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   400 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1320, 1074,
 /*   410 */  1320, 1320, 1320, 1295, 1320, 1320, 1320, 1320, 1320, 1320,
 /*   420 */  1320, 1119, 1320, 1112, 1320, 1320, 1299, 1320, 1320, 1320,
 /*   430 */  1320, 1320, 1320, 1320, 1320, 1320, 1320, 1263, 1320, 1320,
 /*   440 */  1320, 1262, 1320, 1320, 1320, 1320, 1320, 1146, 1320, 1145,
 /*   450 */  1149, 1320, 1002, 1320, 1320,
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
   55,  /*    EXPLAIN => ID */
   55,  /*      QUERY => ID */
   55,  /*       PLAN => ID */
   55,  /*      BEGIN => ID */
    0,  /* TRANSACTION => nothing */
   55,  /*   DEFERRED => ID */
   55,  /*  IMMEDIATE => ID */
   55,  /*  EXCLUSIVE => ID */
    0,  /*     COMMIT => nothing */
   55,  /*        END => ID */
   55,  /*   ROLLBACK => ID */
   55,  /*  SAVEPOINT => ID */
   55,  /*    RELEASE => ID */
    0,  /*         TO => nothing */
    0,  /*      TABLE => nothing */
    0,  /*     CREATE => nothing */
   55,  /*         IF => ID */
    0,  /*        NOT => nothing */
    0,  /*     EXISTS => nothing */
   55,  /*       TEMP => ID */
    0,  /*         LP => nothing */
    0,  /*         RP => nothing */
    0,  /*         AS => nothing */
   55,  /*    WITHOUT => ID */
    0,  /*      COMMA => nothing */
    0,  /*         OR => nothing */
    0,  /*        AND => nothing */
    0,  /*         IS => nothing */
   55,  /*      MATCH => ID */
   55,  /*    LIKE_KW => ID */
    0,  /*    BETWEEN => nothing */
    0,  /*         IN => nothing */
    0,  /*     ISNULL => nothing */
    0,  /*    NOTNULL => nothing */
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
    0,  /*         ID => nothing */
    0,  /*    INDEXED => nothing */
   55,  /*      ABORT => ID */
   55,  /*     ACTION => ID */
   55,  /*      AFTER => ID */
   55,  /*    ANALYZE => ID */
   55,  /*        ASC => ID */
   55,  /*     ATTACH => ID */
   55,  /*     BEFORE => ID */
   55,  /*         BY => ID */
   55,  /*    CASCADE => ID */
   55,  /*       CAST => ID */
   55,  /*   COLUMNKW => ID */
   55,  /*   CONFLICT => ID */
   55,  /*   DATABASE => ID */
   55,  /*       DESC => ID */
   55,  /*     DETACH => ID */
   55,  /*       EACH => ID */
   55,  /*       FAIL => ID */
   55,  /*        FOR => ID */
   55,  /*     IGNORE => ID */
   55,  /*  INITIALLY => ID */
   55,  /*    INSTEAD => ID */
   55,  /*         NO => ID */
   55,  /*        KEY => ID */
   55,  /*         OF => ID */
   55,  /*     OFFSET => ID */
   55,  /*     PRAGMA => ID */
   55,  /*      RAISE => ID */
   55,  /*  RECURSIVE => ID */
   55,  /*    REPLACE => ID */
   55,  /*   RESTRICT => ID */
   55,  /*        ROW => ID */
   55,  /*    TRIGGER => ID */
   55,  /*     VACUUM => ID */
   55,  /*       VIEW => ID */
   55,  /*    VIRTUAL => ID */
   55,  /*       WITH => ID */
   55,  /*    REINDEX => ID */
   55,  /*     RENAME => ID */
   55,  /*   CTIME_KW => ID */
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
  "PLAN",          "BEGIN",         "TRANSACTION",   "DEFERRED",    
  "IMMEDIATE",     "EXCLUSIVE",     "COMMIT",        "END",         
  "ROLLBACK",      "SAVEPOINT",     "RELEASE",       "TO",          
  "TABLE",         "CREATE",        "IF",            "NOT",         
  "EXISTS",        "TEMP",          "LP",            "RP",          
  "AS",            "WITHOUT",       "COMMA",         "OR",          
  "AND",           "IS",            "MATCH",         "LIKE_KW",     
  "BETWEEN",       "IN",            "ISNULL",        "NOTNULL",     
  "NE",            "EQ",            "GT",            "LE",          
  "LT",            "GE",            "ESCAPE",        "BITAND",      
  "BITOR",         "LSHIFT",        "RSHIFT",        "PLUS",        
  "MINUS",         "STAR",          "SLASH",         "REM",         
  "CONCAT",        "COLLATE",       "BITNOT",        "ID",          
  "INDEXED",       "ABORT",         "ACTION",        "AFTER",       
  "ANALYZE",       "ASC",           "ATTACH",        "BEFORE",      
  "BY",            "CASCADE",       "CAST",          "COLUMNKW",    
  "CONFLICT",      "DATABASE",      "DESC",          "DETACH",      
  "EACH",          "FAIL",          "FOR",           "IGNORE",      
  "INITIALLY",     "INSTEAD",       "NO",            "KEY",         
  "OF",            "OFFSET",        "PRAGMA",        "RAISE",       
  "RECURSIVE",     "REPLACE",       "RESTRICT",      "ROW",         
  "TRIGGER",       "VACUUM",        "VIEW",          "VIRTUAL",     
  "WITH",          "REINDEX",       "RENAME",        "CTIME_KW",    
  "ANY",           "STRING",        "JOIN_KW",       "CONSTRAINT",  
  "DEFAULT",       "NULL",          "PRIMARY",       "UNIQUE",      
  "CHECK",         "REFERENCES",    "AUTOINCR",      "ON",          
  "INSERT",        "DELETE",        "UPDATE",        "SET",         
  "DEFERRABLE",    "FOREIGN",       "DROP",          "UNION",       
  "ALL",           "EXCEPT",        "INTERSECT",     "SELECT",      
  "VALUES",        "DISTINCT",      "DOT",           "FROM",        
  "JOIN",          "USING",         "ORDER",         "GROUP",       
  "HAVING",        "LIMIT",         "WHERE",         "INTO",        
  "FLOAT",         "BLOB",          "INTEGER",       "VARIABLE",    
  "CASE",          "WHEN",          "THEN",          "ELSE",        
  "INDEX",         "ALTER",         "ADD",           "error",       
  "input",         "ecmd",          "explain",       "cmdx",        
  "cmd",           "transtype",     "trans_opt",     "nm",          
  "savepoint_opt",  "create_table",  "create_table_args",  "createkw",    
  "temp",          "ifnotexists",   "dbnm",          "columnlist",  
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
  "idlist",        "setlist",       "insert_cmd",    "idlist_opt",  
  "likeop",        "between_op",    "in_op",         "paren_exprlist",
  "case_operand",  "case_exprlist",  "case_else",     "uniqueflag",  
  "collate",       "nmnum",         "trigger_decl",  "trigger_cmd_list",
  "trigger_time",  "trigger_event",  "foreach_clause",  "when_clause", 
  "trigger_cmd",   "trnm",          "tridxby",       "database_kw_opt",
  "key_opt",       "add_column_fullname",  "kwcolumn_opt",  "create_vtab", 
  "vtabarglist",   "vtabarg",       "vtabargtoken",  "lp",          
  "anylist",       "wqlist",      
};
#endif /* NDEBUG */

#ifndef NDEBUG
/* For tracing reduce actions, the names of all rules are required.
*/
static const char *const yyRuleName[] = {
 /*   0 */ "input ::= ecmd",
 /*   1 */ "explain ::= EXPLAIN",
 /*   2 */ "explain ::= EXPLAIN QUERY PLAN",
 /*   3 */ "cmd ::= BEGIN transtype trans_opt",
 /*   4 */ "transtype ::=",
 /*   5 */ "transtype ::= DEFERRED",
 /*   6 */ "transtype ::= IMMEDIATE",
 /*   7 */ "transtype ::= EXCLUSIVE",
 /*   8 */ "cmd ::= COMMIT trans_opt",
 /*   9 */ "cmd ::= END trans_opt",
 /*  10 */ "cmd ::= ROLLBACK trans_opt",
 /*  11 */ "cmd ::= SAVEPOINT nm",
 /*  12 */ "cmd ::= RELEASE savepoint_opt nm",
 /*  13 */ "cmd ::= ROLLBACK trans_opt TO savepoint_opt nm",
 /*  14 */ "create_table ::= createkw temp TABLE ifnotexists nm dbnm",
 /*  15 */ "createkw ::= CREATE",
 /*  16 */ "ifnotexists ::=",
 /*  17 */ "ifnotexists ::= IF NOT EXISTS",
 /*  18 */ "temp ::= TEMP",
 /*  19 */ "temp ::=",
 /*  20 */ "create_table_args ::= LP columnlist conslist_opt RP table_options",
 /*  21 */ "create_table_args ::= AS select",
 /*  22 */ "table_options ::=",
 /*  23 */ "table_options ::= WITHOUT nm",
 /*  24 */ "columnname ::= nm typetoken",
 /*  25 */ "typetoken ::=",
 /*  26 */ "typetoken ::= typename LP signed RP",
 /*  27 */ "typetoken ::= typename LP signed COMMA signed RP",
 /*  28 */ "typename ::= typename ID|STRING",
 /*  29 */ "ccons ::= CONSTRAINT nm",
 /*  30 */ "ccons ::= DEFAULT term",
 /*  31 */ "ccons ::= DEFAULT LP expr RP",
 /*  32 */ "ccons ::= DEFAULT PLUS term",
 /*  33 */ "ccons ::= DEFAULT MINUS term",
 /*  34 */ "ccons ::= DEFAULT ID|INDEXED",
 /*  35 */ "ccons ::= NOT NULL onconf",
 /*  36 */ "ccons ::= PRIMARY KEY sortorder onconf autoinc",
 /*  37 */ "ccons ::= UNIQUE onconf",
 /*  38 */ "ccons ::= CHECK LP expr RP",
 /*  39 */ "ccons ::= REFERENCES nm eidlist_opt refargs",
 /*  40 */ "ccons ::= defer_subclause",
 /*  41 */ "ccons ::= COLLATE ID|STRING",
 /*  42 */ "autoinc ::=",
 /*  43 */ "autoinc ::= AUTOINCR",
 /*  44 */ "refargs ::=",
 /*  45 */ "refargs ::= refargs refarg",
 /*  46 */ "refarg ::= MATCH nm",
 /*  47 */ "refarg ::= ON INSERT refact",
 /*  48 */ "refarg ::= ON DELETE refact",
 /*  49 */ "refarg ::= ON UPDATE refact",
 /*  50 */ "refact ::= SET NULL",
 /*  51 */ "refact ::= SET DEFAULT",
 /*  52 */ "refact ::= CASCADE",
 /*  53 */ "refact ::= RESTRICT",
 /*  54 */ "refact ::= NO ACTION",
 /*  55 */ "defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt",
 /*  56 */ "defer_subclause ::= DEFERRABLE init_deferred_pred_opt",
 /*  57 */ "init_deferred_pred_opt ::=",
 /*  58 */ "init_deferred_pred_opt ::= INITIALLY DEFERRED",
 /*  59 */ "init_deferred_pred_opt ::= INITIALLY IMMEDIATE",
 /*  60 */ "conslist_opt ::=",
 /*  61 */ "tconscomma ::= COMMA",
 /*  62 */ "tcons ::= CONSTRAINT nm",
 /*  63 */ "tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf",
 /*  64 */ "tcons ::= UNIQUE LP sortlist RP onconf",
 /*  65 */ "tcons ::= CHECK LP expr RP onconf",
 /*  66 */ "tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt",
 /*  67 */ "defer_subclause_opt ::=",
 /*  68 */ "onconf ::=",
 /*  69 */ "onconf ::= ON CONFLICT resolvetype",
 /*  70 */ "orconf ::=",
 /*  71 */ "orconf ::= OR resolvetype",
 /*  72 */ "resolvetype ::= IGNORE",
 /*  73 */ "resolvetype ::= REPLACE",
 /*  74 */ "cmd ::= DROP TABLE ifexists fullname",
 /*  75 */ "ifexists ::= IF EXISTS",
 /*  76 */ "ifexists ::=",
 /*  77 */ "cmd ::= createkw temp VIEW ifnotexists nm dbnm eidlist_opt AS select",
 /*  78 */ "cmd ::= DROP VIEW ifexists fullname",
 /*  79 */ "cmd ::= select",
 /*  80 */ "select ::= with selectnowith",
 /*  81 */ "selectnowith ::= selectnowith multiselect_op oneselect",
 /*  82 */ "multiselect_op ::= UNION",
 /*  83 */ "multiselect_op ::= UNION ALL",
 /*  84 */ "multiselect_op ::= EXCEPT|INTERSECT",
 /*  85 */ "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
 /*  86 */ "values ::= VALUES LP nexprlist RP",
 /*  87 */ "values ::= values COMMA LP exprlist RP",
 /*  88 */ "distinct ::= DISTINCT",
 /*  89 */ "distinct ::= ALL",
 /*  90 */ "distinct ::=",
 /*  91 */ "sclp ::=",
 /*  92 */ "selcollist ::= sclp expr as",
 /*  93 */ "selcollist ::= sclp STAR",
 /*  94 */ "selcollist ::= sclp nm DOT STAR",
 /*  95 */ "as ::= AS nm",
 /*  96 */ "as ::=",
 /*  97 */ "from ::=",
 /*  98 */ "from ::= FROM seltablist",
 /*  99 */ "stl_prefix ::= seltablist joinop",
 /* 100 */ "stl_prefix ::=",
 /* 101 */ "seltablist ::= stl_prefix nm dbnm as indexed_opt on_opt using_opt",
 /* 102 */ "seltablist ::= stl_prefix nm dbnm LP exprlist RP as on_opt using_opt",
 /* 103 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
 /* 104 */ "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
 /* 105 */ "dbnm ::=",
 /* 106 */ "dbnm ::= DOT nm",
 /* 107 */ "fullname ::= nm dbnm",
 /* 108 */ "joinop ::= COMMA|JOIN",
 /* 109 */ "joinop ::= JOIN_KW JOIN",
 /* 110 */ "joinop ::= JOIN_KW nm JOIN",
 /* 111 */ "joinop ::= JOIN_KW nm nm JOIN",
 /* 112 */ "on_opt ::= ON expr",
 /* 113 */ "on_opt ::=",
 /* 114 */ "indexed_opt ::=",
 /* 115 */ "indexed_opt ::= INDEXED BY nm",
 /* 116 */ "indexed_opt ::= NOT INDEXED",
 /* 117 */ "using_opt ::= USING LP idlist RP",
 /* 118 */ "using_opt ::=",
 /* 119 */ "orderby_opt ::=",
 /* 120 */ "orderby_opt ::= ORDER BY sortlist",
 /* 121 */ "sortlist ::= sortlist COMMA expr sortorder",
 /* 122 */ "sortlist ::= expr sortorder",
 /* 123 */ "sortorder ::= ASC",
 /* 124 */ "sortorder ::= DESC",
 /* 125 */ "sortorder ::=",
 /* 126 */ "groupby_opt ::=",
 /* 127 */ "groupby_opt ::= GROUP BY nexprlist",
 /* 128 */ "having_opt ::=",
 /* 129 */ "having_opt ::= HAVING expr",
 /* 130 */ "limit_opt ::=",
 /* 131 */ "limit_opt ::= LIMIT expr",
 /* 132 */ "limit_opt ::= LIMIT expr OFFSET expr",
 /* 133 */ "limit_opt ::= LIMIT expr COMMA expr",
 /* 134 */ "cmd ::= with DELETE FROM fullname indexed_opt where_opt",
 /* 135 */ "where_opt ::=",
 /* 136 */ "where_opt ::= WHERE expr",
 /* 137 */ "cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt",
 /* 138 */ "setlist ::= setlist COMMA nm EQ expr",
 /* 139 */ "setlist ::= setlist COMMA LP idlist RP EQ expr",
 /* 140 */ "setlist ::= nm EQ expr",
 /* 141 */ "setlist ::= LP idlist RP EQ expr",
 /* 142 */ "cmd ::= with insert_cmd INTO fullname idlist_opt select",
 /* 143 */ "cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES",
 /* 144 */ "insert_cmd ::= INSERT orconf",
 /* 145 */ "insert_cmd ::= REPLACE",
 /* 146 */ "idlist_opt ::=",
 /* 147 */ "idlist_opt ::= LP idlist RP",
 /* 148 */ "idlist ::= idlist COMMA nm",
 /* 149 */ "idlist ::= nm",
 /* 150 */ "expr ::= LP expr RP",
 /* 151 */ "term ::= NULL",
 /* 152 */ "expr ::= ID|INDEXED",
 /* 153 */ "expr ::= JOIN_KW",
 /* 154 */ "expr ::= nm DOT nm",
 /* 155 */ "expr ::= nm DOT nm DOT nm",
 /* 156 */ "term ::= FLOAT|BLOB",
 /* 157 */ "term ::= STRING",
 /* 158 */ "term ::= INTEGER",
 /* 159 */ "expr ::= VARIABLE",
 /* 160 */ "expr ::= expr COLLATE ID|STRING",
 /* 161 */ "expr ::= CAST LP expr AS typetoken RP",
 /* 162 */ "expr ::= ID|INDEXED LP distinct exprlist RP",
 /* 163 */ "expr ::= ID|INDEXED LP STAR RP",
 /* 164 */ "term ::= CTIME_KW",
 /* 165 */ "expr ::= LP nexprlist COMMA expr RP",
 /* 166 */ "expr ::= expr AND expr",
 /* 167 */ "expr ::= expr OR expr",
 /* 168 */ "expr ::= expr LT|GT|GE|LE expr",
 /* 169 */ "expr ::= expr EQ|NE expr",
 /* 170 */ "expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr",
 /* 171 */ "expr ::= expr PLUS|MINUS expr",
 /* 172 */ "expr ::= expr STAR|SLASH|REM expr",
 /* 173 */ "expr ::= expr CONCAT expr",
 /* 174 */ "likeop ::= LIKE_KW|MATCH",
 /* 175 */ "likeop ::= NOT LIKE_KW|MATCH",
 /* 176 */ "expr ::= expr likeop expr",
 /* 177 */ "expr ::= expr likeop expr ESCAPE expr",
 /* 178 */ "expr ::= expr ISNULL|NOTNULL",
 /* 179 */ "expr ::= expr NOT NULL",
 /* 180 */ "expr ::= expr IS expr",
 /* 181 */ "expr ::= expr IS NOT expr",
 /* 182 */ "expr ::= NOT expr",
 /* 183 */ "expr ::= BITNOT expr",
 /* 184 */ "expr ::= MINUS expr",
 /* 185 */ "expr ::= PLUS expr",
 /* 186 */ "between_op ::= BETWEEN",
 /* 187 */ "between_op ::= NOT BETWEEN",
 /* 188 */ "expr ::= expr between_op expr AND expr",
 /* 189 */ "in_op ::= IN",
 /* 190 */ "in_op ::= NOT IN",
 /* 191 */ "expr ::= expr in_op LP exprlist RP",
 /* 192 */ "expr ::= LP select RP",
 /* 193 */ "expr ::= expr in_op LP select RP",
 /* 194 */ "expr ::= expr in_op nm dbnm paren_exprlist",
 /* 195 */ "expr ::= EXISTS LP select RP",
 /* 196 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 197 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 198 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 199 */ "case_else ::= ELSE expr",
 /* 200 */ "case_else ::=",
 /* 201 */ "case_operand ::= expr",
 /* 202 */ "case_operand ::=",
 /* 203 */ "exprlist ::=",
 /* 204 */ "nexprlist ::= nexprlist COMMA expr",
 /* 205 */ "nexprlist ::= expr",
 /* 206 */ "paren_exprlist ::=",
 /* 207 */ "paren_exprlist ::= LP exprlist RP",
 /* 208 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm dbnm ON nm LP sortlist RP where_opt",
 /* 209 */ "uniqueflag ::= UNIQUE",
 /* 210 */ "uniqueflag ::=",
 /* 211 */ "eidlist_opt ::=",
 /* 212 */ "eidlist_opt ::= LP eidlist RP",
 /* 213 */ "eidlist ::= eidlist COMMA nm collate sortorder",
 /* 214 */ "eidlist ::= nm collate sortorder",
 /* 215 */ "collate ::=",
 /* 216 */ "collate ::= COLLATE ID|STRING",
 /* 217 */ "cmd ::= DROP INDEX ifexists fullname",
 /* 218 */ "cmd ::= PRAGMA nm dbnm",
 /* 219 */ "cmd ::= PRAGMA nm dbnm EQ nmnum",
 /* 220 */ "cmd ::= PRAGMA nm dbnm LP nmnum RP",
 /* 221 */ "cmd ::= PRAGMA nm dbnm EQ minus_num",
 /* 222 */ "cmd ::= PRAGMA nm dbnm LP minus_num RP",
 /* 223 */ "plus_num ::= PLUS INTEGER|FLOAT",
 /* 224 */ "minus_num ::= MINUS INTEGER|FLOAT",
 /* 225 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
 /* 226 */ "trigger_decl ::= temp TRIGGER ifnotexists nm dbnm trigger_time trigger_event ON fullname foreach_clause when_clause",
 /* 227 */ "trigger_time ::= BEFORE",
 /* 228 */ "trigger_time ::= AFTER",
 /* 229 */ "trigger_time ::= INSTEAD OF",
 /* 230 */ "trigger_time ::=",
 /* 231 */ "trigger_event ::= DELETE|INSERT",
 /* 232 */ "trigger_event ::= UPDATE",
 /* 233 */ "trigger_event ::= UPDATE OF idlist",
 /* 234 */ "when_clause ::=",
 /* 235 */ "when_clause ::= WHEN expr",
 /* 236 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
 /* 237 */ "trigger_cmd_list ::= trigger_cmd SEMI",
 /* 238 */ "trnm ::= nm DOT nm",
 /* 239 */ "tridxby ::= INDEXED BY nm",
 /* 240 */ "tridxby ::= NOT INDEXED",
 /* 241 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 242 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
 /* 243 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 244 */ "trigger_cmd ::= select",
 /* 245 */ "expr ::= RAISE LP IGNORE RP",
 /* 246 */ "expr ::= RAISE LP raisetype COMMA nm RP",
 /* 247 */ "raisetype ::= ROLLBACK",
 /* 248 */ "raisetype ::= ABORT",
 /* 249 */ "raisetype ::= FAIL",
 /* 250 */ "cmd ::= DROP TRIGGER ifexists fullname",
 /* 251 */ "cmd ::= ATTACH database_kw_opt expr AS expr key_opt",
 /* 252 */ "cmd ::= DETACH database_kw_opt expr",
 /* 253 */ "key_opt ::=",
 /* 254 */ "key_opt ::= KEY expr",
 /* 255 */ "cmd ::= REINDEX",
 /* 256 */ "cmd ::= REINDEX nm dbnm",
 /* 257 */ "cmd ::= ANALYZE",
 /* 258 */ "cmd ::= ANALYZE nm dbnm",
 /* 259 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
 /* 260 */ "cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist",
 /* 261 */ "add_column_fullname ::= fullname",
 /* 262 */ "cmd ::= create_vtab",
 /* 263 */ "cmd ::= create_vtab LP vtabarglist RP",
 /* 264 */ "create_vtab ::= createkw VIRTUAL TABLE ifnotexists nm dbnm USING nm",
 /* 265 */ "vtabarg ::=",
 /* 266 */ "vtabargtoken ::= ANY",
 /* 267 */ "vtabargtoken ::= lp anylist RP",
 /* 268 */ "lp ::= LP",
 /* 269 */ "with ::=",
 /* 270 */ "with ::= WITH wqlist",
 /* 271 */ "with ::= WITH RECURSIVE wqlist",
 /* 272 */ "wqlist ::= nm eidlist_opt AS LP select RP",
 /* 273 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
 /* 274 */ "ecmd ::= SEMI",
 /* 275 */ "ecmd ::= explain cmdx SEMI",
 /* 276 */ "explain ::=",
 /* 277 */ "cmdx ::= cmd",
 /* 278 */ "trans_opt ::=",
 /* 279 */ "trans_opt ::= TRANSACTION",
 /* 280 */ "trans_opt ::= TRANSACTION nm",
 /* 281 */ "savepoint_opt ::= SAVEPOINT",
 /* 282 */ "savepoint_opt ::=",
 /* 283 */ "cmd ::= create_table create_table_args",
 /* 284 */ "columnlist ::= columnlist COMMA columnname carglist",
 /* 285 */ "columnlist ::= columnname carglist",
 /* 286 */ "nm ::= ID|INDEXED",
 /* 287 */ "nm ::= STRING",
 /* 288 */ "nm ::= JOIN_KW",
 /* 289 */ "typetoken ::= typename",
 /* 290 */ "typename ::= ID|STRING",
 /* 291 */ "signed ::= plus_num",
 /* 292 */ "signed ::= minus_num",
 /* 293 */ "carglist ::= carglist ccons",
 /* 294 */ "carglist ::=",
 /* 295 */ "ccons ::= NULL onconf",
 /* 296 */ "conslist_opt ::= COMMA conslist",
 /* 297 */ "conslist ::= conslist tconscomma tcons",
 /* 298 */ "conslist ::= tcons",
 /* 299 */ "tconscomma ::=",
 /* 300 */ "defer_subclause_opt ::= defer_subclause",
 /* 301 */ "resolvetype ::= raisetype",
 /* 302 */ "selectnowith ::= oneselect",
 /* 303 */ "oneselect ::= values",
 /* 304 */ "sclp ::= selcollist COMMA",
 /* 305 */ "as ::= ID|STRING",
 /* 306 */ "expr ::= term",
 /* 307 */ "exprlist ::= nexprlist",
 /* 308 */ "nmnum ::= plus_num",
 /* 309 */ "nmnum ::= nm",
 /* 310 */ "nmnum ::= ON",
 /* 311 */ "nmnum ::= DELETE",
 /* 312 */ "nmnum ::= DEFAULT",
 /* 313 */ "plus_num ::= INTEGER|FLOAT",
 /* 314 */ "foreach_clause ::=",
 /* 315 */ "foreach_clause ::= FOR EACH ROW",
 /* 316 */ "trnm ::= nm",
 /* 317 */ "tridxby ::=",
 /* 318 */ "database_kw_opt ::= DATABASE",
 /* 319 */ "database_kw_opt ::=",
 /* 320 */ "kwcolumn_opt ::=",
 /* 321 */ "kwcolumn_opt ::= COLUMNKW",
 /* 322 */ "vtabarglist ::= vtabarg",
 /* 323 */ "vtabarglist ::= vtabarglist COMMA vtabarg",
 /* 324 */ "vtabarg ::= vtabarg vtabargtoken",
 /* 325 */ "anylist ::=",
 /* 326 */ "anylist ::= anylist LP anylist RP",
 /* 327 */ "anylist ::= anylist ANY",
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
    case 162: /* select */
    case 193: /* selectnowith */
    case 194: /* oneselect */
    case 205: /* values */
{
#line 394 "parse.y"
sqlite3SelectDelete(pParse->db, (yypminor->yy159));
#line 1569 "parse.c"
}
      break;
    case 171: /* term */
    case 172: /* expr */
{
#line 827 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy342).pExpr);
#line 1577 "parse.c"
}
      break;
    case 176: /* eidlist_opt */
    case 185: /* sortlist */
    case 186: /* eidlist */
    case 198: /* selcollist */
    case 201: /* groupby_opt */
    case 203: /* orderby_opt */
    case 206: /* nexprlist */
    case 207: /* exprlist */
    case 208: /* sclp */
    case 217: /* setlist */
    case 223: /* paren_exprlist */
    case 225: /* case_exprlist */
{
#line 1275 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy442));
#line 1595 "parse.c"
}
      break;
    case 192: /* fullname */
    case 199: /* from */
    case 210: /* seltablist */
    case 211: /* stl_prefix */
{
#line 626 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy347));
#line 1605 "parse.c"
}
      break;
    case 195: /* with */
    case 249: /* wqlist */
{
#line 1552 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy331));
#line 1613 "parse.c"
}
      break;
    case 200: /* where_opt */
    case 202: /* having_opt */
    case 214: /* on_opt */
    case 224: /* case_operand */
    case 226: /* case_else */
    case 235: /* when_clause */
    case 240: /* key_opt */
{
#line 744 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy122));
#line 1626 "parse.c"
}
      break;
    case 215: /* using_opt */
    case 216: /* idlist */
    case 219: /* idlist_opt */
{
#line 660 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy180));
#line 1635 "parse.c"
}
      break;
    case 231: /* trigger_cmd_list */
    case 236: /* trigger_cmd */
{
#line 1389 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy327));
#line 1643 "parse.c"
}
      break;
    case 233: /* trigger_event */
{
#line 1375 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy410).b);
#line 1650 "parse.c"
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
      YYCODETYPE iFallback;            /* Fallback token */
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
#line 37 "parse.y"

  sqlite3ErrorMsg(pParse, "parser stack overflow");
#line 1823 "parse.c"
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
  { 144, 1 },
  { 146, 1 },
  { 146, 3 },
  { 148, 3 },
  { 149, 0 },
  { 149, 1 },
  { 149, 1 },
  { 149, 1 },
  { 148, 2 },
  { 148, 2 },
  { 148, 2 },
  { 148, 2 },
  { 148, 3 },
  { 148, 5 },
  { 153, 6 },
  { 155, 1 },
  { 157, 0 },
  { 157, 3 },
  { 156, 1 },
  { 156, 0 },
  { 154, 5 },
  { 154, 2 },
  { 161, 0 },
  { 161, 2 },
  { 163, 2 },
  { 165, 0 },
  { 165, 4 },
  { 165, 6 },
  { 166, 2 },
  { 170, 2 },
  { 170, 2 },
  { 170, 4 },
  { 170, 3 },
  { 170, 3 },
  { 170, 2 },
  { 170, 3 },
  { 170, 5 },
  { 170, 2 },
  { 170, 4 },
  { 170, 4 },
  { 170, 1 },
  { 170, 2 },
  { 175, 0 },
  { 175, 1 },
  { 177, 0 },
  { 177, 2 },
  { 179, 2 },
  { 179, 3 },
  { 179, 3 },
  { 179, 3 },
  { 180, 2 },
  { 180, 2 },
  { 180, 1 },
  { 180, 1 },
  { 180, 2 },
  { 178, 3 },
  { 178, 2 },
  { 181, 0 },
  { 181, 2 },
  { 181, 2 },
  { 160, 0 },
  { 183, 1 },
  { 184, 2 },
  { 184, 7 },
  { 184, 5 },
  { 184, 5 },
  { 184, 10 },
  { 187, 0 },
  { 173, 0 },
  { 173, 3 },
  { 188, 0 },
  { 188, 2 },
  { 189, 1 },
  { 189, 1 },
  { 148, 4 },
  { 191, 2 },
  { 191, 0 },
  { 148, 9 },
  { 148, 4 },
  { 148, 1 },
  { 162, 2 },
  { 193, 3 },
  { 196, 1 },
  { 196, 2 },
  { 196, 1 },
  { 194, 9 },
  { 205, 4 },
  { 205, 5 },
  { 197, 1 },
  { 197, 1 },
  { 197, 0 },
  { 208, 0 },
  { 198, 3 },
  { 198, 2 },
  { 198, 4 },
  { 209, 2 },
  { 209, 0 },
  { 199, 0 },
  { 199, 2 },
  { 211, 2 },
  { 211, 0 },
  { 210, 7 },
  { 210, 9 },
  { 210, 7 },
  { 210, 7 },
  { 158, 0 },
  { 158, 2 },
  { 192, 2 },
  { 212, 1 },
  { 212, 2 },
  { 212, 3 },
  { 212, 4 },
  { 214, 2 },
  { 214, 0 },
  { 213, 0 },
  { 213, 3 },
  { 213, 2 },
  { 215, 4 },
  { 215, 0 },
  { 203, 0 },
  { 203, 3 },
  { 185, 4 },
  { 185, 2 },
  { 174, 1 },
  { 174, 1 },
  { 174, 0 },
  { 201, 0 },
  { 201, 3 },
  { 202, 0 },
  { 202, 2 },
  { 204, 0 },
  { 204, 2 },
  { 204, 4 },
  { 204, 4 },
  { 148, 6 },
  { 200, 0 },
  { 200, 2 },
  { 148, 8 },
  { 217, 5 },
  { 217, 7 },
  { 217, 3 },
  { 217, 5 },
  { 148, 6 },
  { 148, 7 },
  { 218, 2 },
  { 218, 1 },
  { 219, 0 },
  { 219, 3 },
  { 216, 3 },
  { 216, 1 },
  { 172, 3 },
  { 171, 1 },
  { 172, 1 },
  { 172, 1 },
  { 172, 3 },
  { 172, 5 },
  { 171, 1 },
  { 171, 1 },
  { 171, 1 },
  { 172, 1 },
  { 172, 3 },
  { 172, 6 },
  { 172, 5 },
  { 172, 4 },
  { 171, 1 },
  { 172, 5 },
  { 172, 3 },
  { 172, 3 },
  { 172, 3 },
  { 172, 3 },
  { 172, 3 },
  { 172, 3 },
  { 172, 3 },
  { 172, 3 },
  { 220, 1 },
  { 220, 2 },
  { 172, 3 },
  { 172, 5 },
  { 172, 2 },
  { 172, 3 },
  { 172, 3 },
  { 172, 4 },
  { 172, 2 },
  { 172, 2 },
  { 172, 2 },
  { 172, 2 },
  { 221, 1 },
  { 221, 2 },
  { 172, 5 },
  { 222, 1 },
  { 222, 2 },
  { 172, 5 },
  { 172, 3 },
  { 172, 5 },
  { 172, 5 },
  { 172, 4 },
  { 172, 5 },
  { 225, 5 },
  { 225, 4 },
  { 226, 2 },
  { 226, 0 },
  { 224, 1 },
  { 224, 0 },
  { 207, 0 },
  { 206, 3 },
  { 206, 1 },
  { 223, 0 },
  { 223, 3 },
  { 148, 12 },
  { 227, 1 },
  { 227, 0 },
  { 176, 0 },
  { 176, 3 },
  { 186, 5 },
  { 186, 3 },
  { 228, 0 },
  { 228, 2 },
  { 148, 4 },
  { 148, 3 },
  { 148, 5 },
  { 148, 6 },
  { 148, 5 },
  { 148, 6 },
  { 168, 2 },
  { 169, 2 },
  { 148, 5 },
  { 230, 11 },
  { 232, 1 },
  { 232, 1 },
  { 232, 2 },
  { 232, 0 },
  { 233, 1 },
  { 233, 1 },
  { 233, 3 },
  { 235, 0 },
  { 235, 2 },
  { 231, 3 },
  { 231, 2 },
  { 237, 3 },
  { 238, 3 },
  { 238, 2 },
  { 236, 7 },
  { 236, 5 },
  { 236, 5 },
  { 236, 1 },
  { 172, 4 },
  { 172, 6 },
  { 190, 1 },
  { 190, 1 },
  { 190, 1 },
  { 148, 4 },
  { 148, 6 },
  { 148, 3 },
  { 240, 0 },
  { 240, 2 },
  { 148, 1 },
  { 148, 3 },
  { 148, 1 },
  { 148, 3 },
  { 148, 6 },
  { 148, 7 },
  { 241, 1 },
  { 148, 1 },
  { 148, 4 },
  { 243, 8 },
  { 245, 0 },
  { 246, 1 },
  { 246, 3 },
  { 247, 1 },
  { 195, 0 },
  { 195, 2 },
  { 195, 3 },
  { 249, 6 },
  { 249, 8 },
  { 145, 1 },
  { 145, 3 },
  { 146, 0 },
  { 147, 1 },
  { 150, 0 },
  { 150, 1 },
  { 150, 2 },
  { 152, 1 },
  { 152, 0 },
  { 148, 2 },
  { 159, 4 },
  { 159, 2 },
  { 151, 1 },
  { 151, 1 },
  { 151, 1 },
  { 165, 1 },
  { 166, 1 },
  { 167, 1 },
  { 167, 1 },
  { 164, 2 },
  { 164, 0 },
  { 170, 2 },
  { 160, 2 },
  { 182, 3 },
  { 182, 1 },
  { 183, 0 },
  { 187, 1 },
  { 189, 1 },
  { 193, 1 },
  { 194, 1 },
  { 208, 2 },
  { 209, 1 },
  { 172, 1 },
  { 207, 1 },
  { 229, 1 },
  { 229, 1 },
  { 229, 1 },
  { 229, 1 },
  { 229, 1 },
  { 168, 1 },
  { 234, 0 },
  { 234, 3 },
  { 237, 1 },
  { 238, 0 },
  { 239, 1 },
  { 239, 0 },
  { 242, 0 },
  { 242, 1 },
  { 244, 1 },
  { 244, 3 },
  { 245, 2 },
  { 248, 0 },
  { 248, 4 },
  { 248, 2 },
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
      case 0: /* input ::= ecmd */
#line 106 "parse.y"
{ sqlite3FinishCoding(pParse); }
#line 2291 "parse.c"
        break;
      case 1: /* explain ::= EXPLAIN */
#line 111 "parse.y"
{ pParse->explain = 1; }
#line 2296 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN QUERY PLAN */
#line 112 "parse.y"
{ pParse->explain = 2; }
#line 2301 "parse.c"
        break;
      case 3: /* cmd ::= BEGIN transtype trans_opt */
#line 119 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy392);}
#line 2306 "parse.c"
        break;
      case 4: /* transtype ::= */
#line 124 "parse.y"
{yymsp[1].minor.yy392 = TK_DEFERRED;}
#line 2311 "parse.c"
        break;
      case 5: /* transtype ::= DEFERRED */
      case 6: /* transtype ::= IMMEDIATE */ yytestcase(yyruleno==6);
      case 7: /* transtype ::= EXCLUSIVE */ yytestcase(yyruleno==7);
#line 125 "parse.y"
{yymsp[0].minor.yy392 = yymsp[0].major; /*A-overwrites-X*/}
#line 2318 "parse.c"
        break;
      case 8: /* cmd ::= COMMIT trans_opt */
      case 9: /* cmd ::= END trans_opt */ yytestcase(yyruleno==9);
#line 128 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2324 "parse.c"
        break;
      case 10: /* cmd ::= ROLLBACK trans_opt */
#line 130 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2329 "parse.c"
        break;
      case 11: /* cmd ::= SAVEPOINT nm */
#line 134 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2336 "parse.c"
        break;
      case 12: /* cmd ::= RELEASE savepoint_opt nm */
#line 137 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2343 "parse.c"
        break;
      case 13: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 140 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2350 "parse.c"
        break;
      case 14: /* create_table ::= createkw temp TABLE ifnotexists nm dbnm */
#line 147 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,yymsp[-4].minor.yy392,0,0,yymsp[-2].minor.yy392);
}
#line 2357 "parse.c"
        break;
      case 15: /* createkw ::= CREATE */
#line 150 "parse.y"
{disableLookaside(pParse);}
#line 2362 "parse.c"
        break;
      case 16: /* ifnotexists ::= */
      case 19: /* temp ::= */ yytestcase(yyruleno==19);
      case 22: /* table_options ::= */ yytestcase(yyruleno==22);
      case 42: /* autoinc ::= */ yytestcase(yyruleno==42);
      case 57: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==57);
      case 67: /* defer_subclause_opt ::= */ yytestcase(yyruleno==67);
      case 76: /* ifexists ::= */ yytestcase(yyruleno==76);
      case 90: /* distinct ::= */ yytestcase(yyruleno==90);
      case 215: /* collate ::= */ yytestcase(yyruleno==215);
#line 153 "parse.y"
{yymsp[1].minor.yy392 = 0;}
#line 2375 "parse.c"
        break;
      case 17: /* ifnotexists ::= IF NOT EXISTS */
#line 154 "parse.y"
{yymsp[-2].minor.yy392 = 1;}
#line 2380 "parse.c"
        break;
      case 18: /* temp ::= TEMP */
      case 43: /* autoinc ::= AUTOINCR */ yytestcase(yyruleno==43);
#line 157 "parse.y"
{yymsp[0].minor.yy392 = 1;}
#line 2386 "parse.c"
        break;
      case 20: /* create_table_args ::= LP columnlist conslist_opt RP table_options */
#line 160 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,yymsp[0].minor.yy392,0);
}
#line 2393 "parse.c"
        break;
      case 21: /* create_table_args ::= AS select */
#line 163 "parse.y"
{
  sqlite3EndTable(pParse,0,0,0,yymsp[0].minor.yy159);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy159);
}
#line 2401 "parse.c"
        break;
      case 23: /* table_options ::= WITHOUT nm */
#line 169 "parse.y"
{
  if( yymsp[0].minor.yy0.n==5 && sqlite3_strnicmp(yymsp[0].minor.yy0.z,"rowid",5)==0 ){
    yymsp[-1].minor.yy392 = TF_WithoutRowid | TF_NoVisibleRowid;
  }else{
    yymsp[-1].minor.yy392 = 0;
    sqlite3ErrorMsg(pParse, "unknown table option: %.*s", yymsp[0].minor.yy0.n, yymsp[0].minor.yy0.z);
  }
}
#line 2413 "parse.c"
        break;
      case 24: /* columnname ::= nm typetoken */
#line 179 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2418 "parse.c"
        break;
      case 25: /* typetoken ::= */
      case 60: /* conslist_opt ::= */ yytestcase(yyruleno==60);
      case 96: /* as ::= */ yytestcase(yyruleno==96);
#line 244 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2425 "parse.c"
        break;
      case 26: /* typetoken ::= typename LP signed RP */
#line 246 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2432 "parse.c"
        break;
      case 27: /* typetoken ::= typename LP signed COMMA signed RP */
#line 249 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2439 "parse.c"
        break;
      case 28: /* typename ::= typename ID|STRING */
#line 254 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2444 "parse.c"
        break;
      case 29: /* ccons ::= CONSTRAINT nm */
      case 62: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==62);
#line 263 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2450 "parse.c"
        break;
      case 30: /* ccons ::= DEFAULT term */
      case 32: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==32);
#line 264 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy342);}
#line 2456 "parse.c"
        break;
      case 31: /* ccons ::= DEFAULT LP expr RP */
#line 265 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy342);}
#line 2461 "parse.c"
        break;
      case 33: /* ccons ::= DEFAULT MINUS term */
#line 267 "parse.y"
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy342.pExpr, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy342.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2472 "parse.c"
        break;
      case 34: /* ccons ::= DEFAULT ID|INDEXED */
#line 274 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2481 "parse.c"
        break;
      case 35: /* ccons ::= NOT NULL onconf */
#line 284 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy392);}
#line 2486 "parse.c"
        break;
      case 36: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 286 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy392,yymsp[0].minor.yy392,yymsp[-2].minor.yy392);}
#line 2491 "parse.c"
        break;
      case 37: /* ccons ::= UNIQUE onconf */
#line 287 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,0,yymsp[0].minor.yy392,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2497 "parse.c"
        break;
      case 38: /* ccons ::= CHECK LP expr RP */
#line 289 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy342.pExpr);}
#line 2502 "parse.c"
        break;
      case 39: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 291 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy442,yymsp[0].minor.yy392);}
#line 2507 "parse.c"
        break;
      case 40: /* ccons ::= defer_subclause */
#line 292 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy392);}
#line 2512 "parse.c"
        break;
      case 41: /* ccons ::= COLLATE ID|STRING */
#line 293 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2517 "parse.c"
        break;
      case 44: /* refargs ::= */
#line 306 "parse.y"
{ yymsp[1].minor.yy392 = OE_None*0x0101; /* EV: R-19803-45884 */}
#line 2522 "parse.c"
        break;
      case 45: /* refargs ::= refargs refarg */
#line 307 "parse.y"
{ yymsp[-1].minor.yy392 = (yymsp[-1].minor.yy392 & ~yymsp[0].minor.yy207.mask) | yymsp[0].minor.yy207.value; }
#line 2527 "parse.c"
        break;
      case 46: /* refarg ::= MATCH nm */
#line 309 "parse.y"
{ yymsp[-1].minor.yy207.value = 0;     yymsp[-1].minor.yy207.mask = 0x000000; }
#line 2532 "parse.c"
        break;
      case 47: /* refarg ::= ON INSERT refact */
#line 310 "parse.y"
{ yymsp[-2].minor.yy207.value = 0;     yymsp[-2].minor.yy207.mask = 0x000000; }
#line 2537 "parse.c"
        break;
      case 48: /* refarg ::= ON DELETE refact */
#line 311 "parse.y"
{ yymsp[-2].minor.yy207.value = yymsp[0].minor.yy392;     yymsp[-2].minor.yy207.mask = 0x0000ff; }
#line 2542 "parse.c"
        break;
      case 49: /* refarg ::= ON UPDATE refact */
#line 312 "parse.y"
{ yymsp[-2].minor.yy207.value = yymsp[0].minor.yy392<<8;  yymsp[-2].minor.yy207.mask = 0x00ff00; }
#line 2547 "parse.c"
        break;
      case 50: /* refact ::= SET NULL */
#line 314 "parse.y"
{ yymsp[-1].minor.yy392 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2552 "parse.c"
        break;
      case 51: /* refact ::= SET DEFAULT */
#line 315 "parse.y"
{ yymsp[-1].minor.yy392 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2557 "parse.c"
        break;
      case 52: /* refact ::= CASCADE */
#line 316 "parse.y"
{ yymsp[0].minor.yy392 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2562 "parse.c"
        break;
      case 53: /* refact ::= RESTRICT */
#line 317 "parse.y"
{ yymsp[0].minor.yy392 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2567 "parse.c"
        break;
      case 54: /* refact ::= NO ACTION */
#line 318 "parse.y"
{ yymsp[-1].minor.yy392 = OE_None;     /* EV: R-33326-45252 */}
#line 2572 "parse.c"
        break;
      case 55: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 320 "parse.y"
{yymsp[-2].minor.yy392 = 0;}
#line 2577 "parse.c"
        break;
      case 56: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 71: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==71);
      case 144: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==144);
#line 321 "parse.y"
{yymsp[-1].minor.yy392 = yymsp[0].minor.yy392;}
#line 2584 "parse.c"
        break;
      case 58: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 75: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==75);
      case 187: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==187);
      case 190: /* in_op ::= NOT IN */ yytestcase(yyruleno==190);
      case 216: /* collate ::= COLLATE ID|STRING */ yytestcase(yyruleno==216);
#line 324 "parse.y"
{yymsp[-1].minor.yy392 = 1;}
#line 2593 "parse.c"
        break;
      case 59: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 325 "parse.y"
{yymsp[-1].minor.yy392 = 0;}
#line 2598 "parse.c"
        break;
      case 61: /* tconscomma ::= COMMA */
#line 331 "parse.y"
{pParse->constraintName.n = 0;}
#line 2603 "parse.c"
        break;
      case 63: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 335 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy442,yymsp[0].minor.yy392,yymsp[-2].minor.yy392,0);}
#line 2608 "parse.c"
        break;
      case 64: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 337 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[-2].minor.yy442,yymsp[0].minor.yy392,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2614 "parse.c"
        break;
      case 65: /* tcons ::= CHECK LP expr RP onconf */
#line 340 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy342.pExpr);}
#line 2619 "parse.c"
        break;
      case 66: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 342 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy442, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy442, yymsp[-1].minor.yy392);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy392);
}
#line 2627 "parse.c"
        break;
      case 68: /* onconf ::= */
      case 70: /* orconf ::= */ yytestcase(yyruleno==70);
#line 356 "parse.y"
{yymsp[1].minor.yy392 = OE_Default;}
#line 2633 "parse.c"
        break;
      case 69: /* onconf ::= ON CONFLICT resolvetype */
#line 357 "parse.y"
{yymsp[-2].minor.yy392 = yymsp[0].minor.yy392;}
#line 2638 "parse.c"
        break;
      case 72: /* resolvetype ::= IGNORE */
#line 361 "parse.y"
{yymsp[0].minor.yy392 = OE_Ignore;}
#line 2643 "parse.c"
        break;
      case 73: /* resolvetype ::= REPLACE */
      case 145: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==145);
#line 362 "parse.y"
{yymsp[0].minor.yy392 = OE_Replace;}
#line 2649 "parse.c"
        break;
      case 74: /* cmd ::= DROP TABLE ifexists fullname */
#line 366 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy347, 0, yymsp[-1].minor.yy392);
}
#line 2656 "parse.c"
        break;
      case 77: /* cmd ::= createkw temp VIEW ifnotexists nm dbnm eidlist_opt AS select */
#line 377 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-8].minor.yy0, &yymsp[-4].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy442, yymsp[0].minor.yy159, yymsp[-7].minor.yy392, yymsp[-5].minor.yy392);
}
#line 2663 "parse.c"
        break;
      case 78: /* cmd ::= DROP VIEW ifexists fullname */
#line 380 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy347, 1, yymsp[-1].minor.yy392);
}
#line 2670 "parse.c"
        break;
      case 79: /* cmd ::= select */
#line 387 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy159, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy159);
}
#line 2679 "parse.c"
        break;
      case 80: /* select ::= with selectnowith */
#line 424 "parse.y"
{
  Select *p = yymsp[0].minor.yy159;
  if( p ){
    p->pWith = yymsp[-1].minor.yy331;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, yymsp[-1].minor.yy331);
  }
  yymsp[-1].minor.yy159 = p; /*A-overwrites-W*/
}
#line 2693 "parse.c"
        break;
      case 81: /* selectnowith ::= selectnowith multiselect_op oneselect */
#line 437 "parse.y"
{
  Select *pRhs = yymsp[0].minor.yy159;
  Select *pLhs = yymsp[-2].minor.yy159;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,0,&x,pRhs,0,0);
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)yymsp[-1].minor.yy392;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( yymsp[-1].minor.yy392!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  yymsp[-2].minor.yy159 = pRhs;
}
#line 2719 "parse.c"
        break;
      case 82: /* multiselect_op ::= UNION */
      case 84: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==84);
#line 460 "parse.y"
{yymsp[0].minor.yy392 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2725 "parse.c"
        break;
      case 83: /* multiselect_op ::= UNION ALL */
#line 461 "parse.y"
{yymsp[-1].minor.yy392 = TK_ALL;}
#line 2730 "parse.c"
        break;
      case 85: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 465 "parse.y"
{
#if SELECTTRACE_ENABLED
  Token s = yymsp[-8].minor.yy0; /*A-overwrites-S*/
#endif
  yymsp[-8].minor.yy159 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy442,yymsp[-5].minor.yy347,yymsp[-4].minor.yy122,yymsp[-3].minor.yy442,yymsp[-2].minor.yy122,yymsp[-1].minor.yy442,yymsp[-7].minor.yy392,yymsp[0].minor.yy64.pLimit,yymsp[0].minor.yy64.pOffset);
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
  if( yymsp[-8].minor.yy159!=0 ){
    const char *z = s.z+6;
    int i;
    sqlite3_snprintf(sizeof(yymsp[-8].minor.yy159->zSelName), yymsp[-8].minor.yy159->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(yymsp[-8].minor.yy159->zSelName), yymsp[-8].minor.yy159->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
#line 2764 "parse.c"
        break;
      case 86: /* values ::= VALUES LP nexprlist RP */
#line 499 "parse.y"
{
  yymsp[-3].minor.yy159 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy442,0,0,0,0,0,SF_Values,0,0);
}
#line 2771 "parse.c"
        break;
      case 87: /* values ::= values COMMA LP exprlist RP */
#line 502 "parse.y"
{
  Select *pRight, *pLeft = yymsp[-4].minor.yy159;
  pRight = sqlite3SelectNew(pParse,yymsp[-1].minor.yy442,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    yymsp[-4].minor.yy159 = pRight;
  }else{
    yymsp[-4].minor.yy159 = pLeft;
  }
}
#line 2787 "parse.c"
        break;
      case 88: /* distinct ::= DISTINCT */
#line 519 "parse.y"
{yymsp[0].minor.yy392 = SF_Distinct;}
#line 2792 "parse.c"
        break;
      case 89: /* distinct ::= ALL */
#line 520 "parse.y"
{yymsp[0].minor.yy392 = SF_All;}
#line 2797 "parse.c"
        break;
      case 91: /* sclp ::= */
      case 119: /* orderby_opt ::= */ yytestcase(yyruleno==119);
      case 126: /* groupby_opt ::= */ yytestcase(yyruleno==126);
      case 203: /* exprlist ::= */ yytestcase(yyruleno==203);
      case 206: /* paren_exprlist ::= */ yytestcase(yyruleno==206);
      case 211: /* eidlist_opt ::= */ yytestcase(yyruleno==211);
#line 533 "parse.y"
{yymsp[1].minor.yy442 = 0;}
#line 2807 "parse.c"
        break;
      case 92: /* selcollist ::= sclp expr as */
#line 534 "parse.y"
{
   yymsp[-2].minor.yy442 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy442, yymsp[-1].minor.yy342.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy442, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy442,&yymsp[-1].minor.yy342);
}
#line 2816 "parse.c"
        break;
      case 93: /* selcollist ::= sclp STAR */
#line 539 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy442 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy442, p);
}
#line 2824 "parse.c"
        break;
      case 94: /* selcollist ::= sclp nm DOT STAR */
#line 543 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy442 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy442, pDot);
}
#line 2834 "parse.c"
        break;
      case 95: /* as ::= AS nm */
      case 106: /* dbnm ::= DOT nm */ yytestcase(yyruleno==106);
      case 223: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==223);
      case 224: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==224);
#line 554 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2842 "parse.c"
        break;
      case 97: /* from ::= */
#line 568 "parse.y"
{yymsp[1].minor.yy347 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy347));}
#line 2847 "parse.c"
        break;
      case 98: /* from ::= FROM seltablist */
#line 569 "parse.y"
{
  yymsp[-1].minor.yy347 = yymsp[0].minor.yy347;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy347);
}
#line 2855 "parse.c"
        break;
      case 99: /* stl_prefix ::= seltablist joinop */
#line 577 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy347 && yymsp[-1].minor.yy347->nSrc>0) ) yymsp[-1].minor.yy347->a[yymsp[-1].minor.yy347->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy392;
}
#line 2862 "parse.c"
        break;
      case 100: /* stl_prefix ::= */
#line 580 "parse.y"
{yymsp[1].minor.yy347 = 0;}
#line 2867 "parse.c"
        break;
      case 101: /* seltablist ::= stl_prefix nm dbnm as indexed_opt on_opt using_opt */
#line 582 "parse.y"
{
  yymsp[-6].minor.yy347 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy347,&yymsp[-5].minor.yy0,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy122,yymsp[0].minor.yy180);
  sqlite3SrcListIndexedBy(pParse, yymsp[-6].minor.yy347, &yymsp[-2].minor.yy0);
}
#line 2875 "parse.c"
        break;
      case 102: /* seltablist ::= stl_prefix nm dbnm LP exprlist RP as on_opt using_opt */
#line 587 "parse.y"
{
  yymsp[-8].minor.yy347 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-8].minor.yy347,&yymsp[-7].minor.yy0,&yymsp[-6].minor.yy0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy122,yymsp[0].minor.yy180);
  sqlite3SrcListFuncArgs(pParse, yymsp[-8].minor.yy347, yymsp[-4].minor.yy442);
}
#line 2883 "parse.c"
        break;
      case 103: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 593 "parse.y"
{
    yymsp[-6].minor.yy347 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy347,0,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy159,yymsp[-1].minor.yy122,yymsp[0].minor.yy180);
  }
#line 2890 "parse.c"
        break;
      case 104: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 597 "parse.y"
{
    if( yymsp[-6].minor.yy347==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy122==0 && yymsp[0].minor.yy180==0 ){
      yymsp[-6].minor.yy347 = yymsp[-4].minor.yy347;
    }else if( yymsp[-4].minor.yy347->nSrc==1 ){
      yymsp[-6].minor.yy347 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy347,0,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy122,yymsp[0].minor.yy180);
      if( yymsp[-6].minor.yy347 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy347->a[yymsp[-6].minor.yy347->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy347->a;
        pNew->zName = pOld->zName;
        pNew->zDatabase = pOld->zDatabase;
        pNew->pSelect = pOld->pSelect;
        pOld->zName = pOld->zDatabase = 0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy347);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy347);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy347,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy347 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy347,0,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy122,yymsp[0].minor.yy180);
    }
  }
#line 2916 "parse.c"
        break;
      case 105: /* dbnm ::= */
      case 114: /* indexed_opt ::= */ yytestcase(yyruleno==114);
#line 622 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2922 "parse.c"
        break;
      case 107: /* fullname ::= nm dbnm */
#line 628 "parse.y"
{yymsp[-1].minor.yy347 = sqlite3SrcListAppend(pParse->db,0,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 2927 "parse.c"
        break;
      case 108: /* joinop ::= COMMA|JOIN */
#line 631 "parse.y"
{ yymsp[0].minor.yy392 = JT_INNER; }
#line 2932 "parse.c"
        break;
      case 109: /* joinop ::= JOIN_KW JOIN */
#line 633 "parse.y"
{yymsp[-1].minor.yy392 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2937 "parse.c"
        break;
      case 110: /* joinop ::= JOIN_KW nm JOIN */
#line 635 "parse.y"
{yymsp[-2].minor.yy392 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2942 "parse.c"
        break;
      case 111: /* joinop ::= JOIN_KW nm nm JOIN */
#line 637 "parse.y"
{yymsp[-3].minor.yy392 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2947 "parse.c"
        break;
      case 112: /* on_opt ::= ON expr */
      case 129: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==129);
      case 136: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==136);
      case 199: /* case_else ::= ELSE expr */ yytestcase(yyruleno==199);
#line 641 "parse.y"
{yymsp[-1].minor.yy122 = yymsp[0].minor.yy342.pExpr;}
#line 2955 "parse.c"
        break;
      case 113: /* on_opt ::= */
      case 128: /* having_opt ::= */ yytestcase(yyruleno==128);
      case 135: /* where_opt ::= */ yytestcase(yyruleno==135);
      case 200: /* case_else ::= */ yytestcase(yyruleno==200);
      case 202: /* case_operand ::= */ yytestcase(yyruleno==202);
#line 642 "parse.y"
{yymsp[1].minor.yy122 = 0;}
#line 2964 "parse.c"
        break;
      case 115: /* indexed_opt ::= INDEXED BY nm */
#line 656 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2969 "parse.c"
        break;
      case 116: /* indexed_opt ::= NOT INDEXED */
#line 657 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2974 "parse.c"
        break;
      case 117: /* using_opt ::= USING LP idlist RP */
#line 661 "parse.y"
{yymsp[-3].minor.yy180 = yymsp[-1].minor.yy180;}
#line 2979 "parse.c"
        break;
      case 118: /* using_opt ::= */
      case 146: /* idlist_opt ::= */ yytestcase(yyruleno==146);
#line 662 "parse.y"
{yymsp[1].minor.yy180 = 0;}
#line 2985 "parse.c"
        break;
      case 120: /* orderby_opt ::= ORDER BY sortlist */
      case 127: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==127);
#line 676 "parse.y"
{yymsp[-2].minor.yy442 = yymsp[0].minor.yy442;}
#line 2991 "parse.c"
        break;
      case 121: /* sortlist ::= sortlist COMMA expr sortorder */
#line 677 "parse.y"
{
  yymsp[-3].minor.yy442 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy442,yymsp[-1].minor.yy342.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy442,yymsp[0].minor.yy392);
}
#line 2999 "parse.c"
        break;
      case 122: /* sortlist ::= expr sortorder */
#line 681 "parse.y"
{
  yymsp[-1].minor.yy442 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy342.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy442,yymsp[0].minor.yy392);
}
#line 3007 "parse.c"
        break;
      case 123: /* sortorder ::= ASC */
#line 688 "parse.y"
{yymsp[0].minor.yy392 = SQLITE_SO_ASC;}
#line 3012 "parse.c"
        break;
      case 124: /* sortorder ::= DESC */
#line 689 "parse.y"
{yymsp[0].minor.yy392 = SQLITE_SO_DESC;}
#line 3017 "parse.c"
        break;
      case 125: /* sortorder ::= */
#line 690 "parse.y"
{yymsp[1].minor.yy392 = SQLITE_SO_UNDEFINED;}
#line 3022 "parse.c"
        break;
      case 130: /* limit_opt ::= */
#line 715 "parse.y"
{yymsp[1].minor.yy64.pLimit = 0; yymsp[1].minor.yy64.pOffset = 0;}
#line 3027 "parse.c"
        break;
      case 131: /* limit_opt ::= LIMIT expr */
#line 716 "parse.y"
{yymsp[-1].minor.yy64.pLimit = yymsp[0].minor.yy342.pExpr; yymsp[-1].minor.yy64.pOffset = 0;}
#line 3032 "parse.c"
        break;
      case 132: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 718 "parse.y"
{yymsp[-3].minor.yy64.pLimit = yymsp[-2].minor.yy342.pExpr; yymsp[-3].minor.yy64.pOffset = yymsp[0].minor.yy342.pExpr;}
#line 3037 "parse.c"
        break;
      case 133: /* limit_opt ::= LIMIT expr COMMA expr */
#line 720 "parse.y"
{yymsp[-3].minor.yy64.pOffset = yymsp[-2].minor.yy342.pExpr; yymsp[-3].minor.yy64.pLimit = yymsp[0].minor.yy342.pExpr;}
#line 3042 "parse.c"
        break;
      case 134: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 735 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy331, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy347, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy347,yymsp[0].minor.yy122);
}
#line 3052 "parse.c"
        break;
      case 137: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 764 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy331, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy347, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy442,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  sqlite3Update(pParse,yymsp[-4].minor.yy347,yymsp[-1].minor.yy442,yymsp[0].minor.yy122,yymsp[-5].minor.yy392);
}
#line 3063 "parse.c"
        break;
      case 138: /* setlist ::= setlist COMMA nm EQ expr */
#line 776 "parse.y"
{
  yymsp[-4].minor.yy442 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy442, yymsp[0].minor.yy342.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy442, &yymsp[-2].minor.yy0, 1);
}
#line 3071 "parse.c"
        break;
      case 139: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 780 "parse.y"
{
  yymsp[-6].minor.yy442 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy442, yymsp[-3].minor.yy180, yymsp[0].minor.yy342.pExpr);
}
#line 3078 "parse.c"
        break;
      case 140: /* setlist ::= nm EQ expr */
#line 783 "parse.y"
{
  yylhsminor.yy442 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy342.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy442, &yymsp[-2].minor.yy0, 1);
}
#line 3086 "parse.c"
  yymsp[-2].minor.yy442 = yylhsminor.yy442;
        break;
      case 141: /* setlist ::= LP idlist RP EQ expr */
#line 787 "parse.y"
{
  yymsp[-4].minor.yy442 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy180, yymsp[0].minor.yy342.pExpr);
}
#line 3094 "parse.c"
        break;
      case 142: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 793 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy331, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  sqlite3Insert(pParse, yymsp[-2].minor.yy347, yymsp[0].minor.yy159, yymsp[-1].minor.yy180, yymsp[-4].minor.yy392);
}
#line 3103 "parse.c"
        break;
      case 143: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 799 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy331, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  sqlite3Insert(pParse, yymsp[-3].minor.yy347, 0, yymsp[-2].minor.yy180, yymsp[-5].minor.yy392);
}
#line 3112 "parse.c"
        break;
      case 147: /* idlist_opt ::= LP idlist RP */
#line 815 "parse.y"
{yymsp[-2].minor.yy180 = yymsp[-1].minor.yy180;}
#line 3117 "parse.c"
        break;
      case 148: /* idlist ::= idlist COMMA nm */
#line 817 "parse.y"
{yymsp[-2].minor.yy180 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy180,&yymsp[0].minor.yy0);}
#line 3122 "parse.c"
        break;
      case 149: /* idlist ::= nm */
#line 819 "parse.y"
{yymsp[0].minor.yy180 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3127 "parse.c"
        break;
      case 150: /* expr ::= LP expr RP */
#line 869 "parse.y"
{spanSet(&yymsp[-2].minor.yy342,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy342.pExpr = yymsp[-1].minor.yy342.pExpr;}
#line 3132 "parse.c"
        break;
      case 151: /* term ::= NULL */
      case 156: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==156);
      case 157: /* term ::= STRING */ yytestcase(yyruleno==157);
#line 870 "parse.y"
{spanExpr(&yymsp[0].minor.yy342,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3139 "parse.c"
        break;
      case 152: /* expr ::= ID|INDEXED */
      case 153: /* expr ::= JOIN_KW */ yytestcase(yyruleno==153);
#line 871 "parse.y"
{spanExpr(&yymsp[0].minor.yy342,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3145 "parse.c"
        break;
      case 154: /* expr ::= nm DOT nm */
#line 873 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy342,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3155 "parse.c"
        break;
      case 155: /* expr ::= nm DOT nm DOT nm */
#line 879 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-4].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp3 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  Expr *temp4 = sqlite3PExpr(pParse, TK_DOT, temp2, temp3);
  spanSet(&yymsp[-4].minor.yy342,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-4].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp4);
}
#line 3167 "parse.c"
        break;
      case 158: /* term ::= INTEGER */
#line 889 "parse.y"
{
  yylhsminor.yy342.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy342.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy342.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy342.pExpr ) yylhsminor.yy342.pExpr->flags |= EP_Leaf;
}
#line 3177 "parse.c"
  yymsp[0].minor.yy342 = yylhsminor.yy342;
        break;
      case 159: /* expr ::= VARIABLE */
#line 895 "parse.y"
{
  if( !(yymsp[0].minor.yy0.z[0]=='#' && sqlite3Isdigit(yymsp[0].minor.yy0.z[1])) ){
    u32 n = yymsp[0].minor.yy0.n;
    spanExpr(&yymsp[0].minor.yy342, pParse, TK_VARIABLE, yymsp[0].minor.yy0);
    sqlite3ExprAssignVarNumber(pParse, yymsp[0].minor.yy342.pExpr, n);
  }else{
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    Token t = yymsp[0].minor.yy0; /*A-overwrites-X*/
    assert( t.n>=2 );
    spanSet(&yymsp[0].minor.yy342, &t, &t);
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &t);
      yymsp[0].minor.yy342.pExpr = 0;
    }else{
      yymsp[0].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0);
      if( yymsp[0].minor.yy342.pExpr ) sqlite3GetInt32(&t.z[1], &yymsp[0].minor.yy342.pExpr->iTable);
    }
  }
}
#line 3203 "parse.c"
        break;
      case 160: /* expr ::= expr COLLATE ID|STRING */
#line 916 "parse.y"
{
  yymsp[-2].minor.yy342.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy342.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy342.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3211 "parse.c"
        break;
      case 161: /* expr ::= CAST LP expr AS typetoken RP */
#line 921 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy342,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy342.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy342.pExpr, yymsp[-3].minor.yy342.pExpr, 0);
}
#line 3220 "parse.c"
        break;
      case 162: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 927 "parse.y"
{
  if( yymsp[-1].minor.yy442 && yymsp[-1].minor.yy442->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yylhsminor.yy342.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy442, &yymsp[-4].minor.yy0);
  spanSet(&yylhsminor.yy342,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy392==SF_Distinct && yylhsminor.yy342.pExpr ){
    yylhsminor.yy342.pExpr->flags |= EP_Distinct;
  }
}
#line 3234 "parse.c"
  yymsp[-4].minor.yy342 = yylhsminor.yy342;
        break;
      case 163: /* expr ::= ID|INDEXED LP STAR RP */
#line 937 "parse.y"
{
  yylhsminor.yy342.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy342,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3243 "parse.c"
  yymsp[-3].minor.yy342 = yylhsminor.yy342;
        break;
      case 164: /* term ::= CTIME_KW */
#line 941 "parse.y"
{
  yylhsminor.yy342.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy342, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3252 "parse.c"
  yymsp[0].minor.yy342 = yylhsminor.yy342;
        break;
      case 165: /* expr ::= LP nexprlist COMMA expr RP */
#line 970 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy442, yymsp[-1].minor.yy342.pExpr);
  yylhsminor.yy342.pExpr = sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
  if( yylhsminor.yy342.pExpr ){
    yylhsminor.yy342.pExpr->x.pList = pList;
    spanSet(&yylhsminor.yy342, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  }
}
#line 3267 "parse.c"
  yymsp[-4].minor.yy342 = yylhsminor.yy342;
        break;
      case 166: /* expr ::= expr AND expr */
      case 167: /* expr ::= expr OR expr */ yytestcase(yyruleno==167);
      case 168: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==168);
      case 169: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==169);
      case 170: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==170);
      case 171: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==171);
      case 172: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==172);
      case 173: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==173);
#line 981 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy342,&yymsp[0].minor.yy342);}
#line 3280 "parse.c"
        break;
      case 174: /* likeop ::= LIKE_KW|MATCH */
#line 994 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3285 "parse.c"
        break;
      case 175: /* likeop ::= NOT LIKE_KW|MATCH */
#line 995 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3290 "parse.c"
        break;
      case 176: /* expr ::= expr likeop expr */
#line 996 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
  yymsp[-1].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy342.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy342.pExpr);
  yymsp[-2].minor.yy342.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-2].minor.yy342);
  yymsp[-2].minor.yy342.zEnd = yymsp[0].minor.yy342.zEnd;
  if( yymsp[-2].minor.yy342.pExpr ) yymsp[-2].minor.yy342.pExpr->flags |= EP_InfixFunc;
}
#line 3305 "parse.c"
        break;
      case 177: /* expr ::= expr likeop expr ESCAPE expr */
#line 1007 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
  yymsp[-3].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy342.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy342.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy342.pExpr);
  yymsp[-4].minor.yy342.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-4].minor.yy342);
  yymsp[-4].minor.yy342.zEnd = yymsp[0].minor.yy342.zEnd;
  if( yymsp[-4].minor.yy342.pExpr ) yymsp[-4].minor.yy342.pExpr->flags |= EP_InfixFunc;
}
#line 3321 "parse.c"
        break;
      case 178: /* expr ::= expr ISNULL|NOTNULL */
#line 1034 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy342,&yymsp[0].minor.yy0);}
#line 3326 "parse.c"
        break;
      case 179: /* expr ::= expr NOT NULL */
#line 1035 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy342,&yymsp[0].minor.yy0);}
#line 3331 "parse.c"
        break;
      case 180: /* expr ::= expr IS expr */
#line 1056 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy342,&yymsp[0].minor.yy342);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy342.pExpr, yymsp[-2].minor.yy342.pExpr, TK_ISNULL);
}
#line 3339 "parse.c"
        break;
      case 181: /* expr ::= expr IS NOT expr */
#line 1060 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy342,&yymsp[0].minor.yy342);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy342.pExpr, yymsp[-3].minor.yy342.pExpr, TK_NOTNULL);
}
#line 3347 "parse.c"
        break;
      case 182: /* expr ::= NOT expr */
      case 183: /* expr ::= BITNOT expr */ yytestcase(yyruleno==183);
#line 1084 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy342,pParse,yymsp[-1].major,&yymsp[0].minor.yy342,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3353 "parse.c"
        break;
      case 184: /* expr ::= MINUS expr */
#line 1088 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy342,pParse,TK_UMINUS,&yymsp[0].minor.yy342,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3358 "parse.c"
        break;
      case 185: /* expr ::= PLUS expr */
#line 1090 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy342,pParse,TK_UPLUS,&yymsp[0].minor.yy342,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3363 "parse.c"
        break;
      case 186: /* between_op ::= BETWEEN */
      case 189: /* in_op ::= IN */ yytestcase(yyruleno==189);
#line 1093 "parse.y"
{yymsp[0].minor.yy392 = 0;}
#line 3369 "parse.c"
        break;
      case 188: /* expr ::= expr between_op expr AND expr */
#line 1095 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy342.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy342.pExpr);
  yymsp[-4].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy342.pExpr, 0);
  if( yymsp[-4].minor.yy342.pExpr ){
    yymsp[-4].minor.yy342.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, yymsp[-3].minor.yy392, &yymsp[-4].minor.yy342);
  yymsp[-4].minor.yy342.zEnd = yymsp[0].minor.yy342.zEnd;
}
#line 3385 "parse.c"
        break;
      case 191: /* expr ::= expr in_op LP exprlist RP */
#line 1111 "parse.y"
{
    if( yymsp[-1].minor.yy442==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      sqlite3ExprDelete(pParse->db, yymsp[-4].minor.yy342.pExpr);
      yymsp[-4].minor.yy342.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER,&sqlite3IntTokens[yymsp[-3].minor.yy392],1);
    }else if( yymsp[-1].minor.yy442->nExpr==1 ){
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
      Expr *pRHS = yymsp[-1].minor.yy442->a[0].pExpr;
      yymsp[-1].minor.yy442->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy442);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      yymsp[-4].minor.yy342.pExpr = sqlite3PExpr(pParse, yymsp[-3].minor.yy392 ? TK_NE : TK_EQ, yymsp[-4].minor.yy342.pExpr, pRHS);
    }else{
      yymsp[-4].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy342.pExpr, 0);
      if( yymsp[-4].minor.yy342.pExpr ){
        yymsp[-4].minor.yy342.pExpr->x.pList = yymsp[-1].minor.yy442;
        sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy342.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy442);
      }
      exprNot(pParse, yymsp[-3].minor.yy392, &yymsp[-4].minor.yy342);
    }
    yymsp[-4].minor.yy342.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3440 "parse.c"
        break;
      case 192: /* expr ::= LP select RP */
#line 1162 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy342,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy342.pExpr, yymsp[-1].minor.yy159);
  }
#line 3449 "parse.c"
        break;
      case 193: /* expr ::= expr in_op LP select RP */
#line 1167 "parse.y"
{
    yymsp[-4].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy342.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy342.pExpr, yymsp[-1].minor.yy159);
    exprNot(pParse, yymsp[-3].minor.yy392, &yymsp[-4].minor.yy342);
    yymsp[-4].minor.yy342.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3459 "parse.c"
        break;
      case 194: /* expr ::= expr in_op nm dbnm paren_exprlist */
#line 1173 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy442 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy442);
    yymsp[-4].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy342.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy342.pExpr, pSelect);
    exprNot(pParse, yymsp[-3].minor.yy392, &yymsp[-4].minor.yy342);
    yymsp[-4].minor.yy342.zEnd = yymsp[-1].minor.yy0.z ? &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n] : &yymsp[-2].minor.yy0.z[yymsp[-2].minor.yy0.n];
  }
#line 3472 "parse.c"
        break;
      case 195: /* expr ::= EXISTS LP select RP */
#line 1182 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy342,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy159);
  }
#line 3482 "parse.c"
        break;
      case 196: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1191 "parse.y"
{
  spanSet(&yymsp[-4].minor.yy342,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-C*/
  yymsp[-4].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy122, 0);
  if( yymsp[-4].minor.yy342.pExpr ){
    yymsp[-4].minor.yy342.pExpr->x.pList = yymsp[-1].minor.yy122 ? sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy442,yymsp[-1].minor.yy122) : yymsp[-2].minor.yy442;
    sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy342.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy442);
    sqlite3ExprDelete(pParse->db, yymsp[-1].minor.yy122);
  }
}
#line 3497 "parse.c"
        break;
      case 197: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1204 "parse.y"
{
  yymsp[-4].minor.yy442 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy442, yymsp[-2].minor.yy342.pExpr);
  yymsp[-4].minor.yy442 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy442, yymsp[0].minor.yy342.pExpr);
}
#line 3505 "parse.c"
        break;
      case 198: /* case_exprlist ::= WHEN expr THEN expr */
#line 1208 "parse.y"
{
  yymsp[-3].minor.yy442 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy342.pExpr);
  yymsp[-3].minor.yy442 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy442, yymsp[0].minor.yy342.pExpr);
}
#line 3513 "parse.c"
        break;
      case 201: /* case_operand ::= expr */
#line 1218 "parse.y"
{yymsp[0].minor.yy122 = yymsp[0].minor.yy342.pExpr; /*A-overwrites-X*/}
#line 3518 "parse.c"
        break;
      case 204: /* nexprlist ::= nexprlist COMMA expr */
#line 1229 "parse.y"
{yymsp[-2].minor.yy442 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy442,yymsp[0].minor.yy342.pExpr);}
#line 3523 "parse.c"
        break;
      case 205: /* nexprlist ::= expr */
#line 1231 "parse.y"
{yymsp[0].minor.yy442 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy342.pExpr); /*A-overwrites-Y*/}
#line 3528 "parse.c"
        break;
      case 207: /* paren_exprlist ::= LP exprlist RP */
      case 212: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==212);
#line 1239 "parse.y"
{yymsp[-2].minor.yy442 = yymsp[-1].minor.yy442;}
#line 3534 "parse.c"
        break;
      case 208: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm dbnm ON nm LP sortlist RP where_opt */
#line 1246 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-7].minor.yy0, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0,0), yymsp[-2].minor.yy442, yymsp[-10].minor.yy392,
                      &yymsp[-11].minor.yy0, yymsp[0].minor.yy122, SQLITE_SO_ASC, yymsp[-8].minor.yy392, SQLITE_IDXTYPE_APPDEF);
}
#line 3543 "parse.c"
        break;
      case 209: /* uniqueflag ::= UNIQUE */
      case 248: /* raisetype ::= ABORT */ yytestcase(yyruleno==248);
#line 1253 "parse.y"
{yymsp[0].minor.yy392 = OE_Abort;}
#line 3549 "parse.c"
        break;
      case 210: /* uniqueflag ::= */
#line 1254 "parse.y"
{yymsp[1].minor.yy392 = OE_None;}
#line 3554 "parse.c"
        break;
      case 213: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1304 "parse.y"
{
  yymsp[-4].minor.yy442 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy442, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy392, yymsp[0].minor.yy392);
}
#line 3561 "parse.c"
        break;
      case 214: /* eidlist ::= nm collate sortorder */
#line 1307 "parse.y"
{
  yymsp[-2].minor.yy442 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy392, yymsp[0].minor.yy392); /*A-overwrites-Y*/
}
#line 3568 "parse.c"
        break;
      case 217: /* cmd ::= DROP INDEX ifexists fullname */
#line 1318 "parse.y"
{sqlite3DropIndex(pParse, yymsp[0].minor.yy347, yymsp[-1].minor.yy392);}
#line 3573 "parse.c"
        break;
      case 218: /* cmd ::= PRAGMA nm dbnm */
#line 1332 "parse.y"
{sqlite3Pragma(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,0,0);}
#line 3578 "parse.c"
        break;
      case 219: /* cmd ::= PRAGMA nm dbnm EQ nmnum */
#line 1333 "parse.y"
{sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0,0);}
#line 3583 "parse.c"
        break;
      case 220: /* cmd ::= PRAGMA nm dbnm LP nmnum RP */
#line 1334 "parse.y"
{sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,&yymsp[-1].minor.yy0,0);}
#line 3588 "parse.c"
        break;
      case 221: /* cmd ::= PRAGMA nm dbnm EQ minus_num */
#line 1336 "parse.y"
{sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0,1);}
#line 3593 "parse.c"
        break;
      case 222: /* cmd ::= PRAGMA nm dbnm LP minus_num RP */
#line 1338 "parse.y"
{sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,&yymsp[-1].minor.yy0,1);}
#line 3598 "parse.c"
        break;
      case 225: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1354 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy327, &all);
}
#line 3608 "parse.c"
        break;
      case 226: /* trigger_decl ::= temp TRIGGER ifnotexists nm dbnm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1363 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-7].minor.yy0, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy392, yymsp[-4].minor.yy410.a, yymsp[-4].minor.yy410.b, yymsp[-2].minor.yy347, yymsp[0].minor.yy122, yymsp[-10].minor.yy392, yymsp[-8].minor.yy392);
  yymsp[-10].minor.yy0 = (yymsp[-6].minor.yy0.n==0?yymsp[-7].minor.yy0:yymsp[-6].minor.yy0); /*A-overwrites-T*/
}
#line 3616 "parse.c"
        break;
      case 227: /* trigger_time ::= BEFORE */
#line 1369 "parse.y"
{ yymsp[0].minor.yy392 = TK_BEFORE; }
#line 3621 "parse.c"
        break;
      case 228: /* trigger_time ::= AFTER */
#line 1370 "parse.y"
{ yymsp[0].minor.yy392 = TK_AFTER;  }
#line 3626 "parse.c"
        break;
      case 229: /* trigger_time ::= INSTEAD OF */
#line 1371 "parse.y"
{ yymsp[-1].minor.yy392 = TK_INSTEAD;}
#line 3631 "parse.c"
        break;
      case 230: /* trigger_time ::= */
#line 1372 "parse.y"
{ yymsp[1].minor.yy392 = TK_BEFORE; }
#line 3636 "parse.c"
        break;
      case 231: /* trigger_event ::= DELETE|INSERT */
      case 232: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==232);
#line 1376 "parse.y"
{yymsp[0].minor.yy410.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy410.b = 0;}
#line 3642 "parse.c"
        break;
      case 233: /* trigger_event ::= UPDATE OF idlist */
#line 1378 "parse.y"
{yymsp[-2].minor.yy410.a = TK_UPDATE; yymsp[-2].minor.yy410.b = yymsp[0].minor.yy180;}
#line 3647 "parse.c"
        break;
      case 234: /* when_clause ::= */
      case 253: /* key_opt ::= */ yytestcase(yyruleno==253);
#line 1385 "parse.y"
{ yymsp[1].minor.yy122 = 0; }
#line 3653 "parse.c"
        break;
      case 235: /* when_clause ::= WHEN expr */
      case 254: /* key_opt ::= KEY expr */ yytestcase(yyruleno==254);
#line 1386 "parse.y"
{ yymsp[-1].minor.yy122 = yymsp[0].minor.yy342.pExpr; }
#line 3659 "parse.c"
        break;
      case 236: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1390 "parse.y"
{
  assert( yymsp[-2].minor.yy327!=0 );
  yymsp[-2].minor.yy327->pLast->pNext = yymsp[-1].minor.yy327;
  yymsp[-2].minor.yy327->pLast = yymsp[-1].minor.yy327;
}
#line 3668 "parse.c"
        break;
      case 237: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1395 "parse.y"
{ 
  assert( yymsp[-1].minor.yy327!=0 );
  yymsp[-1].minor.yy327->pLast = yymsp[-1].minor.yy327;
}
#line 3676 "parse.c"
        break;
      case 238: /* trnm ::= nm DOT nm */
#line 1406 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3686 "parse.c"
        break;
      case 239: /* tridxby ::= INDEXED BY nm */
#line 1418 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3695 "parse.c"
        break;
      case 240: /* tridxby ::= NOT INDEXED */
#line 1423 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3704 "parse.c"
        break;
      case 241: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1436 "parse.y"
{yymsp[-6].minor.yy327 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy442, yymsp[0].minor.yy122, yymsp[-5].minor.yy392);}
#line 3709 "parse.c"
        break;
      case 242: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1440 "parse.y"
{yymsp[-4].minor.yy327 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy180, yymsp[0].minor.yy159, yymsp[-4].minor.yy392);/*A-overwrites-R*/}
#line 3714 "parse.c"
        break;
      case 243: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1444 "parse.y"
{yymsp[-4].minor.yy327 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy122);}
#line 3719 "parse.c"
        break;
      case 244: /* trigger_cmd ::= select */
#line 1448 "parse.y"
{yymsp[0].minor.yy327 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy159); /*A-overwrites-X*/}
#line 3724 "parse.c"
        break;
      case 245: /* expr ::= RAISE LP IGNORE RP */
#line 1451 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy342,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy342.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy342.pExpr ){
    yymsp[-3].minor.yy342.pExpr->affinity = OE_Ignore;
  }
}
#line 3735 "parse.c"
        break;
      case 246: /* expr ::= RAISE LP raisetype COMMA nm RP */
#line 1458 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy342,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy342.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy342.pExpr ) {
    yymsp[-5].minor.yy342.pExpr->affinity = (char)yymsp[-3].minor.yy392;
  }
}
#line 3746 "parse.c"
        break;
      case 247: /* raisetype ::= ROLLBACK */
#line 1468 "parse.y"
{yymsp[0].minor.yy392 = OE_Rollback;}
#line 3751 "parse.c"
        break;
      case 249: /* raisetype ::= FAIL */
#line 1470 "parse.y"
{yymsp[0].minor.yy392 = OE_Fail;}
#line 3756 "parse.c"
        break;
      case 250: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1475 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy347,yymsp[-1].minor.yy392);
}
#line 3763 "parse.c"
        break;
      case 251: /* cmd ::= ATTACH database_kw_opt expr AS expr key_opt */
#line 1482 "parse.y"
{
  sqlite3Attach(pParse, yymsp[-3].minor.yy342.pExpr, yymsp[-1].minor.yy342.pExpr, yymsp[0].minor.yy122);
}
#line 3770 "parse.c"
        break;
      case 252: /* cmd ::= DETACH database_kw_opt expr */
#line 1485 "parse.y"
{
  sqlite3Detach(pParse, yymsp[0].minor.yy342.pExpr);
}
#line 3777 "parse.c"
        break;
      case 255: /* cmd ::= REINDEX */
#line 1500 "parse.y"
{sqlite3Reindex(pParse, 0, 0);}
#line 3782 "parse.c"
        break;
      case 256: /* cmd ::= REINDEX nm dbnm */
#line 1501 "parse.y"
{sqlite3Reindex(pParse, &yymsp[-1].minor.yy0, &yymsp[0].minor.yy0);}
#line 3787 "parse.c"
        break;
      case 257: /* cmd ::= ANALYZE */
#line 1506 "parse.y"
{sqlite3Analyze(pParse, 0, 0);}
#line 3792 "parse.c"
        break;
      case 258: /* cmd ::= ANALYZE nm dbnm */
#line 1507 "parse.y"
{sqlite3Analyze(pParse, &yymsp[-1].minor.yy0, &yymsp[0].minor.yy0);}
#line 3797 "parse.c"
        break;
      case 259: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1512 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy347,&yymsp[0].minor.yy0);
}
#line 3804 "parse.c"
        break;
      case 260: /* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist */
#line 1516 "parse.y"
{
  yymsp[-1].minor.yy0.n = (int)(pParse->sLastToken.z-yymsp[-1].minor.yy0.z) + pParse->sLastToken.n;
  sqlite3AlterFinishAddColumn(pParse, &yymsp[-1].minor.yy0);
}
#line 3812 "parse.c"
        break;
      case 261: /* add_column_fullname ::= fullname */
#line 1520 "parse.y"
{
  disableLookaside(pParse);
  sqlite3AlterBeginAddColumn(pParse, yymsp[0].minor.yy347);
}
#line 3820 "parse.c"
        break;
      case 262: /* cmd ::= create_vtab */
#line 1530 "parse.y"
{sqlite3VtabFinishParse(pParse,0);}
#line 3825 "parse.c"
        break;
      case 263: /* cmd ::= create_vtab LP vtabarglist RP */
#line 1531 "parse.y"
{sqlite3VtabFinishParse(pParse,&yymsp[0].minor.yy0);}
#line 3830 "parse.c"
        break;
      case 264: /* create_vtab ::= createkw VIRTUAL TABLE ifnotexists nm dbnm USING nm */
#line 1533 "parse.y"
{
    sqlite3VtabBeginParse(pParse, &yymsp[-3].minor.yy0, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0, yymsp[-4].minor.yy392);
}
#line 3837 "parse.c"
        break;
      case 265: /* vtabarg ::= */
#line 1538 "parse.y"
{sqlite3VtabArgInit(pParse);}
#line 3842 "parse.c"
        break;
      case 266: /* vtabargtoken ::= ANY */
      case 267: /* vtabargtoken ::= lp anylist RP */ yytestcase(yyruleno==267);
      case 268: /* lp ::= LP */ yytestcase(yyruleno==268);
#line 1540 "parse.y"
{sqlite3VtabArgExtend(pParse,&yymsp[0].minor.yy0);}
#line 3849 "parse.c"
        break;
      case 269: /* with ::= */
#line 1555 "parse.y"
{yymsp[1].minor.yy331 = 0;}
#line 3854 "parse.c"
        break;
      case 270: /* with ::= WITH wqlist */
#line 1557 "parse.y"
{ yymsp[-1].minor.yy331 = yymsp[0].minor.yy331; }
#line 3859 "parse.c"
        break;
      case 271: /* with ::= WITH RECURSIVE wqlist */
#line 1558 "parse.y"
{ yymsp[-2].minor.yy331 = yymsp[0].minor.yy331; }
#line 3864 "parse.c"
        break;
      case 272: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1560 "parse.y"
{
  yymsp[-5].minor.yy331 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy442, yymsp[-1].minor.yy159); /*A-overwrites-X*/
}
#line 3871 "parse.c"
        break;
      case 273: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1563 "parse.y"
{
  yymsp[-7].minor.yy331 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy331, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy442, yymsp[-1].minor.yy159);
}
#line 3878 "parse.c"
        break;
      default:
      /* (274) ecmd ::= SEMI */ yytestcase(yyruleno==274);
      /* (275) ecmd ::= explain cmdx SEMI */ yytestcase(yyruleno==275);
      /* (276) explain ::= */ yytestcase(yyruleno==276);
      /* (277) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno!=277);
      /* (278) trans_opt ::= */ yytestcase(yyruleno==278);
      /* (279) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==279);
      /* (280) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==280);
      /* (281) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==281);
      /* (282) savepoint_opt ::= */ yytestcase(yyruleno==282);
      /* (283) cmd ::= create_table create_table_args */ yytestcase(yyruleno==283);
      /* (284) columnlist ::= columnlist COMMA columnname carglist */ yytestcase(yyruleno==284);
      /* (285) columnlist ::= columnname carglist */ yytestcase(yyruleno==285);
      /* (286) nm ::= ID|INDEXED */ yytestcase(yyruleno==286);
      /* (287) nm ::= STRING */ yytestcase(yyruleno==287);
      /* (288) nm ::= JOIN_KW */ yytestcase(yyruleno==288);
      /* (289) typetoken ::= typename */ yytestcase(yyruleno==289);
      /* (290) typename ::= ID|STRING */ yytestcase(yyruleno==290);
      /* (291) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=291);
      /* (292) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=292);
      /* (293) carglist ::= carglist ccons */ yytestcase(yyruleno==293);
      /* (294) carglist ::= */ yytestcase(yyruleno==294);
      /* (295) ccons ::= NULL onconf */ yytestcase(yyruleno==295);
      /* (296) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==296);
      /* (297) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==297);
      /* (298) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=298);
      /* (299) tconscomma ::= */ yytestcase(yyruleno==299);
      /* (300) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=300);
      /* (301) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=301);
      /* (302) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=302);
      /* (303) oneselect ::= values */ yytestcase(yyruleno==303);
      /* (304) sclp ::= selcollist COMMA */ yytestcase(yyruleno==304);
      /* (305) as ::= ID|STRING */ yytestcase(yyruleno==305);
      /* (306) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=306);
      /* (307) exprlist ::= nexprlist */ yytestcase(yyruleno==307);
      /* (308) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=308);
      /* (309) nmnum ::= nm (OPTIMIZED OUT) */ assert(yyruleno!=309);
      /* (310) nmnum ::= ON */ yytestcase(yyruleno==310);
      /* (311) nmnum ::= DELETE */ yytestcase(yyruleno==311);
      /* (312) nmnum ::= DEFAULT */ yytestcase(yyruleno==312);
      /* (313) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno==313);
      /* (314) foreach_clause ::= */ yytestcase(yyruleno==314);
      /* (315) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==315);
      /* (316) trnm ::= nm */ yytestcase(yyruleno==316);
      /* (317) tridxby ::= */ yytestcase(yyruleno==317);
      /* (318) database_kw_opt ::= DATABASE */ yytestcase(yyruleno==318);
      /* (319) database_kw_opt ::= */ yytestcase(yyruleno==319);
      /* (320) kwcolumn_opt ::= */ yytestcase(yyruleno==320);
      /* (321) kwcolumn_opt ::= COLUMNKW */ yytestcase(yyruleno==321);
      /* (322) vtabarglist ::= vtabarg */ yytestcase(yyruleno==322);
      /* (323) vtabarglist ::= vtabarglist COMMA vtabarg */ yytestcase(yyruleno==323);
      /* (324) vtabarg ::= vtabarg vtabargtoken */ yytestcase(yyruleno==324);
      /* (325) anylist ::= */ yytestcase(yyruleno==325);
      /* (326) anylist ::= anylist LP anylist RP */ yytestcase(yyruleno==326);
      /* (327) anylist ::= anylist ANY */ yytestcase(yyruleno==327);
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
  sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &TOKEN);
#line 3996 "parse.c"
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
