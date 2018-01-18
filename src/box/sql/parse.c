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

#line 405 "parse.y"

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
#line 844 "parse.y"

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
#line 952 "parse.y"

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
#line 1026 "parse.y"

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
#line 1043 "parse.y"

  /* A routine to convert a binary TK_IS or TK_ISNOT expression into a
  ** unary TK_ISNULL or TK_NOTNULL expression. */
  static void binaryToUnaryIfNull(Parse *pParse, Expr *pY, Expr *pA, int op){
    sqlite3 *db = pParse->db;
    if( pA && pY && pY->op==TK_NULL ){
      pA->op = (u8)op;
      sql_expr_free(db, pA->pRight, false);
      pA->pRight = 0;
    }
  }
#line 1071 "parse.y"

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
#line 1276 "parse.y"

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
#define YYNOCODE 231
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 74
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  struct TrigEvent yy10;
  IdList* yy40;
  int yy52;
  struct {int value; int mask;} yy107;
  With* yy151;
  ExprSpan yy162;
  Select* yy279;
  Expr* yy362;
  ExprList* yy382;
  struct LimitVal yy384;
  SrcList* yy387;
  TriggerStep* yy427;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYFALLBACK 1
#define YYNSTATE             409
#define YYNRULE              297
#define YY_MAX_SHIFT         408
#define YY_MIN_SHIFTREDUCE   604
#define YY_MAX_SHIFTREDUCE   900
#define YY_MIN_REDUCE        901
#define YY_MAX_REDUCE        1197
#define YY_ERROR_ACTION      1198
#define YY_ACCEPT_ACTION     1199
#define YY_NO_ACTION         1200
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
#define YY_ACTTAB_COUNT (1402)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    91,   92,  285,   82,  771,  771,  783,  786,  775,  775,
 /*    10 */    89,   89,   90,   90,   90,   90,  307,   88,   88,   88,
 /*    20 */    88,   87,   87,   86,   86,   86,   85,  307,   90,   90,
 /*    30 */    90,   90,   83,   88,   88,   88,   88,   87,   87,   86,
 /*    40 */    86,   86,   85,  307,  208,  201,  885,   90,   90,   90,
 /*    50 */    90,  122,   88,   88,   88,   88,   87,   87,   86,   86,
 /*    60 */    86,   85,  307,   87,   87,   86,   86,   86,   85,  307,
 /*    70 */   885,   86,   86,   86,   85,  307,   91,   92,  285,   82,
 /*    80 */   771,  771,  783,  786,  775,  775,   89,   89,   90,   90,
 /*    90 */    90,   90,  630,   88,   88,   88,   88,   87,   87,   86,
 /*   100 */    86,   86,   85,  307,   91,   92,  285,   82,  771,  771,
 /*   110 */   783,  786,  775,  775,   89,   89,   90,   90,   90,   90,
 /*   120 */   287,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   130 */    85,  307,  705,   91,   92,  285,   82,  771,  771,  783,
 /*   140 */   786,  775,  775,   89,   89,   90,   90,   90,   90,   67,
 /*   150 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   160 */   307,  772,  772,  784,  787,  740,   93,  172,  280,  706,
 /*   170 */   312,  278,  277,  276,  220,  274,  243,  633,  618,  721,
 /*   180 */   722,  632,   91,   92,  285,   82,  771,  771,  783,  786,
 /*   190 */   775,  775,   89,   89,   90,   90,   90,   90,  109,   88,
 /*   200 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  307,
 /*   210 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   220 */   307,  344,  165,  706,  341,  638,  286,  305,  304,  824,
 /*   230 */   776,   91,   92,  285,   82,  771,  771,  783,  786,  775,
 /*   240 */   775,   89,   89,   90,   90,   90,   90,  333,   88,   88,
 /*   250 */    88,   88,   87,   87,   86,   86,   86,   85,  307,  182,
 /*   260 */   743,  666,  645,  407,  407,  180,  313,  122,  137,  218,
 /*   270 */   666,  119,  624,  624,  631,  679,  236,  330,  235,  877,
 /*   280 */    91,   92,  285,   82,  771,  771,  783,  786,  775,  775,
 /*   290 */    89,   89,   90,   90,   90,   90,  743,   88,   88,   88,
 /*   300 */    88,   87,   87,   86,   86,   86,   85,  307,   22,  215,
 /*   310 */   404,  183,  236,  320,  224,  761,  896,  754,  896,   54,
 /*   320 */   749,  720,  705,  762,  122,  216,  366,   48,   48,   91,
 /*   330 */    92,  285,   82,  771,  771,  783,  786,  775,  775,   89,
 /*   340 */    89,   90,   90,   90,   90,  265,   88,   88,   88,   88,
 /*   350 */    87,   87,   86,   86,   86,   85,  307,  753,  753,  755,
 /*   360 */   223,  199,  382,  367,  356,  353,  352,  649,   84,   81,
 /*   370 */   176,  699,  748, 1199,  408,    3,  351,  294,   91,   92,
 /*   380 */   285,   82,  771,  771,  783,  786,  775,  775,   89,   89,
 /*   390 */    90,   90,   90,   90,  317,   88,   88,   88,   88,   87,
 /*   400 */    87,   86,   86,   86,   85,  307,   91,   92,  285,   82,
 /*   410 */   771,  771,  783,  786,  775,  775,   89,   89,   90,   90,
 /*   420 */    90,   90,  648,   88,   88,   88,   88,   87,   87,   86,
 /*   430 */    86,   86,   85,  307,   91,   92,  285,   82,  771,  771,
 /*   440 */   783,  786,  775,  775,   89,   89,   90,   90,   90,   90,
 /*   450 */   122,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   460 */    85,  307,   91,   92,  285,   82,  771,  771,  783,  786,
 /*   470 */   775,  775,   89,   89,   90,   90,   90,   90,  145,   88,
 /*   480 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  307,
 /*   490 */  1150, 1150,   84,   81,  176,   70,   92,  285,   82,  771,
 /*   500 */   771,  783,  786,  775,  775,   89,   89,   90,   90,   90,
 /*   510 */    90,  647,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   520 */    86,   85,  307,   73,  207,  122,  402,  402,  402,   91,
 /*   530 */    80,  285,   82,  771,  771,  783,  786,  775,  775,   89,
 /*   540 */    89,   90,   90,   90,   90,  376,   88,   88,   88,   88,
 /*   550 */    87,   87,   86,   86,   86,   85,  307,  285,   82,  771,
 /*   560 */   771,  783,  786,  775,  775,   89,   89,   90,   90,   90,
 /*   570 */    90,   78,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   580 */    86,   85,  307,  404,  306,  306,  306,  605,  310,  141,
 /*   590 */    75,   76,  264,  624,  624,  692,  873,   77,  284,  315,
 /*   600 */    48,   48,  305,  304,  640,   78,  265,   84,   81,  176,
 /*   610 */   400,    2, 1097,  315,  314,  308,  308,  200,  199,  200,
 /*   620 */   141,  356,  353,  352,   75,   76,  273,  624,  624,  281,
 /*   630 */   404,   77,  389,  351,  387,  382,  372,  761,  303,  754,
 /*   640 */   743, 1173,  749,  661,  400,    2,  404,   10,   10,  308,
 /*   650 */   308,  891,  299,  895,  383,  373,  236,  330,  235,  371,
 /*   660 */   893,  293,  894,   48,   48,  361,  389,  751,  348,  333,
 /*   670 */   201,  761,  846,  754,  705,  315,  749,  296,  398,  753,
 /*   680 */   753,  755,  756,  403,   18,  138,  847,  109,  404,  624,
 /*   690 */   624,  896,  109,  896,  848,  672,  164,  122,  382,  384,
 /*   700 */   374,  751,  673,  843,  122,   48,   48,  162,  174,   78,
 /*   710 */   719,  843,  184,  753,  753,  755,  756,  403,   18,  886,
 /*   720 */   886,  217,  155,  253,  359,  248,  358,  203,   75,   76,
 /*   730 */   693,  122,  689,  189,  246,   77,  109,  688,  404,  328,
 /*   740 */   382,  381,  870,   78,  109,  287,  363,  404,  400,    2,
 /*   750 */   246,   85,  307,  308,  308,   30,   30,   84,   81,  176,
 /*   760 */   331,  658,   75,   76,   48,   48,  870,  404,  331,   77,
 /*   770 */   389,  887,  708,   23,  370,  761,  167,  754,  329,  109,
 /*   780 */   749,  255,  400,    2,   10,   10,  340,  308,  308,  404,
 /*   790 */   364,    9,    9,  216,  366,  404,  177,  177,  295,  382,
 /*   800 */   362,  830,  830,  325,  389,  751,   48,   48,  369,  761,
 /*   810 */   705,  754,   10,   10,  749,  659,  404,  753,  753,  755,
 /*   820 */   756,  403,   18,  624,  624,  404,  198,  404,  339,  886,
 /*   830 */   886,  671,  671,   48,   48,  316,  157,  156,  823,  751,
 /*   840 */   823,  300,   47,   47,   10,   10,   66,   68,  185,  816,
 /*   850 */   818,  753,  753,  755,  756,  403,   18,   95,  379,  146,
 /*   860 */   404,  634,  634,  177,  177,  652,   75,   76,  377,  241,
 /*   870 */     5,  357,  208,   77,  885,  369,  653,   34,   34,  643,
 /*   880 */   339,  887,  707,  761,  404,  754,  400,    2,  749,  318,
 /*   890 */   705,  308,  308,  706,  297,  229,  263,  705,  885,  404,
 /*   900 */    24,   35,   35,  335,  624,  624,  806,  846,  389,  624,
 /*   910 */   624,  816,  188,  761,  404,  754,   36,   36,  749,  240,
 /*   920 */   178,  847,  404,  747,  404,  753,  753,  755,  849,  848,
 /*   930 */   388,   37,   37,  404,  705,  321,  404,  669,  669,   38,
 /*   940 */    38,   26,   26,  751,  404,  228,  165,  706,  404,  841,
 /*   950 */    27,   27,  232,   29,   29,  753,  753,  755,  756,  403,
 /*   960 */    18,   39,   39,  404,  234,   40,   40,  827,  264,  826,
 /*   970 */   404,  705,  678,  404,  921,  323,  227,  404,  226,  404,
 /*   980 */    41,   41,  109,  623,  288,  404,  324,   11,   11,  404,
 /*   990 */    42,   42,  143,  404,   97,   97,   43,   43,  404,  747,
 /*  1000 */   404,  257,   44,   44,  404,  339,   31,   31,  404,  674,
 /*  1010 */    45,   45,  404,  747,  404,   46,   46,   32,   32,  404,
 /*  1020 */   752,  112,  112,  252,  694,  113,  113,  404,  705,  114,
 /*  1030 */   114,   52,   52,  404,  251,  404,   33,   33,  404,  677,
 /*  1040 */   404,  208,  404,  885,   98,   98,  404,  624,  624,  259,
 /*  1050 */    49,   49,   99,   99,  394,  100,  100,   96,   96,  111,
 /*  1060 */   111,  404,  290,  108,  108,  404,  231,  885,  404,  747,
 /*  1070 */   404,  747,  109,  404,  163,  404,  291,  404,  104,  104,
 /*  1080 */   404,  169,  103,  103,  404,  101,  101,  102,  102,  191,
 /*  1090 */    51,   51,   53,   53,   50,   50,  107,   25,   25,    1,
 /*  1100 */   628,   28,   28,  624,  624,  365,  681,  684,  684,  622,
 /*  1110 */   161,  160,  159,  401,  288,  391,  395,  369,  399,  175,
 /*  1120 */   174,  337,  721,  722,  628,   74,  884,   72,  689,  110,
 /*  1130 */   262,  149,  292,  688,  301,  745,   20,  206,  332,  334,
 /*  1140 */   206,  206,  109,  237,   64,   66,  651,  650,  109,  109,
 /*  1150 */   349,  109,  212,  244,  822,   66,  822,  641,  641,  686,
 /*  1160 */   715,   69,  206,    7,  813,  813,  338,   19,  735,  809,
 /*  1170 */   626,  212,  106,  852,  757,  757,  319,  851,  153,  820,
 /*  1180 */   289,  814,  225,  168,  233,  838,  840,  837,  336,  342,
 /*  1190 */   343,  239,  354,  617,  242,  158,  662,  646,  247,  713,
 /*  1200 */   645,  746,  261,  695,  250,  390,  811,  266,  810,  267,
 /*  1210 */   272,  629,  154,  866,  615,  614,  616,  863,  135,  124,
 /*  1220 */   117,   64,  322,  825,  732,   55,  327,  230,  347,  187,
 /*  1230 */   147,  194,  144,  195,  126,  360,  196,  298,  643,  128,
 /*  1240 */   665,  129,  130,  131,  345,  282,  664,  139,  663,  375,
 /*  1250 */    63,    6,  742,   71,  842,  637,  302,  283,  636,  656,
 /*  1260 */    94,  249,  635,  875,  380,   65,  805,  655,  378,   21,
 /*  1270 */   864,  222,  607,  610,  309,  179,  819,  219,  279,  221,
 /*  1280 */   405,  406,  612,  611,  608,  311,  123,  817,  181,  393,
 /*  1290 */   741,  115,  397,  125,  120,  127,  186,  675,  116,  828,
 /*  1300 */   703,  133,  206,  132,  836,  898,  254,  326,  136,  134,
 /*  1310 */   105,  704,  256,  202,  702,   56,   57,   58,  258,  269,
 /*  1320 */   701,  260,  685,  268,  270,  271,   59,  839,  121,  835,
 /*  1330 */   791,  190,  192,   12,  209,  238,    8,  193,  148,  620,
 /*  1340 */   346,  197,  140,  210,  211,  251,  350,  355,  654,  204,
 /*  1350 */   118,   60,   13,  683,  245,   14,   61,   62,  170,  760,
 /*  1360 */   759,  789,   15,  385,  687,    4,  714,  368,  171,  173,
 /*  1370 */   205,  142,   69,  709,  166,   16,   66,   17,  804,  790,
 /*  1380 */   788,  793,  845,  214,  844,  386,  856,  150,  392,  275,
 /*  1390 */   857,  213,  151,  396,  792,  152,  604,  758,  627,   79,
 /*  1400 */  1155,  621,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*    10 */    15,   16,   17,   18,   19,   20,   32,   22,   23,   24,
 /*    20 */    25,   26,   27,   28,   29,   30,   31,   32,   17,   18,
 /*    30 */    19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
 /*    40 */    29,   30,   31,   32,   49,    9,   51,   17,   18,   19,
 /*    50 */    20,  133,   22,   23,   24,   25,   26,   27,   28,   29,
 /*    60 */    30,   31,   32,   26,   27,   28,   29,   30,   31,   32,
 /*    70 */    75,   28,   29,   30,   31,   32,    5,    6,    7,    8,
 /*    80 */     9,   10,   11,   12,   13,   14,   15,   16,   17,   18,
 /*    90 */    19,   20,  160,   22,   23,   24,   25,   26,   27,   28,
 /*   100 */    29,   30,   31,   32,    5,    6,    7,    8,    9,   10,
 /*   110 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   120 */    84,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   130 */    31,   32,  143,    5,    6,    7,    8,    9,   10,   11,
 /*   140 */    12,   13,   14,   15,   16,   17,   18,   19,   20,   50,
 /*   150 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   160 */    32,    9,   10,   11,   12,  151,   67,   48,   34,   50,
 /*   170 */   181,   37,   38,   39,   40,   41,   48,  160,   44,  108,
 /*   180 */   109,  160,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   190 */    13,   14,   15,   16,   17,   18,   19,   20,  184,   22,
 /*   200 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   210 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   220 */    32,  217,  103,  104,  220,   48,   92,   26,   27,   38,
 /*   230 */    78,    5,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   240 */    14,   15,   16,   17,   18,   19,   20,  143,   22,   23,
 /*   250 */    24,   25,   26,   27,   28,   29,   30,   31,   32,  125,
 /*   260 */    69,  167,  168,  139,  140,  131,  132,  133,   47,  145,
 /*   270 */   176,  147,   51,   52,   48,  151,   85,   86,   87,  173,
 /*   280 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*   290 */    15,   16,   17,   18,   19,   20,   69,   22,   23,   24,
 /*   300 */    25,   26,   27,   28,   29,   30,   31,   32,  184,  187,
 /*   310 */   143,  207,   85,   86,   87,   73,  115,   75,  117,  197,
 /*   320 */    78,  163,  143,   48,  133,   98,   99,  160,  161,    5,
 /*   330 */     6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
 /*   340 */    16,   17,   18,   19,   20,  143,   22,   23,   24,   25,
 /*   350 */    26,   27,   28,   29,   30,   31,   32,  115,  116,  117,
 /*   360 */   181,   76,  195,  196,   79,   80,   81,  169,  210,  211,
 /*   370 */   212,  201,   48,  136,  137,  138,   91,  175,    5,    6,
 /*   380 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   390 */    17,   18,   19,   20,   77,   22,   23,   24,   25,   26,
 /*   400 */    27,   28,   29,   30,   31,   32,    5,    6,    7,    8,
 /*   410 */     9,   10,   11,   12,   13,   14,   15,   16,   17,   18,
 /*   420 */    19,   20,  169,   22,   23,   24,   25,   26,   27,   28,
 /*   430 */    29,   30,   31,   32,    5,    6,    7,    8,    9,   10,
 /*   440 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   450 */   133,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   460 */    31,   32,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   470 */    13,   14,   15,   16,   17,   18,   19,   20,   49,   22,
 /*   480 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   490 */    98,   99,  210,  211,  212,  122,    6,    7,    8,    9,
 /*   500 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   510 */    20,  169,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   520 */    30,   31,   32,  122,  198,  133,  156,  157,  158,    5,
 /*   530 */     6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
 /*   540 */    16,   17,   18,   19,   20,  205,   22,   23,   24,   25,
 /*   550 */    26,   27,   28,   29,   30,   31,   32,    7,    8,    9,
 /*   560 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   570 */    20,    7,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   580 */    30,   31,   32,  143,  156,  157,  158,    1,    2,  143,
 /*   590 */    26,   27,  143,   51,   52,  198,  159,   33,  152,  143,
 /*   600 */   160,  161,   26,   27,  167,    7,  143,  210,  211,  212,
 /*   610 */    46,   47,   48,  157,  158,   51,   52,  171,   76,  173,
 /*   620 */   143,   79,   80,   81,   26,   27,  149,   51,   52,  152,
 /*   630 */   143,   33,   68,   91,  179,  195,  196,   73,  175,   75,
 /*   640 */    69,   48,   78,   50,   46,   47,  143,  160,  161,   51,
 /*   650 */    52,   75,    7,   77,  151,    7,   85,   86,   87,  151,
 /*   660 */    84,  174,   86,  160,  161,   28,   68,  103,    7,  143,
 /*   670 */     9,   73,   39,   75,  143,  219,   78,   32,  229,  115,
 /*   680 */   116,  117,  118,  119,  120,   47,   53,  184,  143,   51,
 /*   690 */    52,  115,  184,  117,   61,   62,  205,  133,  195,  196,
 /*   700 */    52,  103,   69,  151,  133,  160,  161,  199,  200,    7,
 /*   710 */   163,  151,  181,  115,  116,  117,  118,  119,  120,   51,
 /*   720 */    52,   76,   77,   78,   79,   80,   81,   82,   26,   27,
 /*   730 */    28,  133,   95,  207,   89,   33,  184,  100,  143,  223,
 /*   740 */   195,  196,   51,    7,  184,   84,  151,  143,   46,   47,
 /*   750 */    89,   31,   32,   51,   52,  160,  161,  210,  211,  212,
 /*   760 */   208,   36,   26,   27,  160,  161,   75,  143,  208,   33,
 /*   770 */    68,  103,  104,  221,  143,   73,  222,   75,  226,  184,
 /*   780 */    78,  198,   46,   47,  160,  161,  226,   51,   52,  143,
 /*   790 */   195,  160,  161,   98,   99,  143,  182,  183,  174,  195,
 /*   800 */   196,   85,   86,   87,   68,  103,  160,  161,  194,   73,
 /*   810 */   143,   75,  160,  161,   78,   90,  143,  115,  116,  117,
 /*   820 */   118,  119,  120,   51,   52,  143,  174,  143,  143,   51,
 /*   830 */    52,  178,  179,  160,  161,  143,   26,   27,  115,  103,
 /*   840 */   117,  195,  160,  161,  160,  161,   50,    7,  181,  157,
 /*   850 */   158,  115,  116,  117,  118,  119,  120,   47,  174,  185,
 /*   860 */   143,   51,   52,  182,  183,   59,   26,   27,  195,   43,
 /*   870 */    47,   65,   49,   33,   51,  194,   70,  160,  161,   83,
 /*   880 */   143,  103,  104,   73,  143,   75,   46,   47,   78,  208,
 /*   890 */   143,   51,   52,   50,   88,   43,  214,  143,   75,  143,
 /*   900 */    47,  160,  161,  143,   51,   52,   80,   39,   68,   51,
 /*   910 */    52,  219,  227,   73,  143,   75,  160,  161,   78,   93,
 /*   920 */    47,   53,  143,  143,  143,  115,  116,  117,  181,   61,
 /*   930 */    62,  160,  161,  143,  143,  181,  143,  178,  179,  160,
 /*   940 */   161,  160,  161,  103,  143,   93,  103,  104,  143,  151,
 /*   950 */   160,  161,  126,  160,  161,  115,  116,  117,  118,  119,
 /*   960 */   120,  160,  161,  143,  227,  160,  161,   56,  143,   58,
 /*   970 */   143,  143,  181,  143,  101,   64,  124,  143,  126,  143,
 /*   980 */   160,  161,  184,  154,  155,  143,  206,  160,  161,  143,
 /*   990 */   160,  161,  134,  143,  160,  161,  160,  161,  143,  143,
 /*  1000 */   143,  198,  160,  161,  143,  143,  160,  161,  143,  181,
 /*  1010 */   160,  161,  143,  143,  143,  160,  161,  160,  161,  143,
 /*  1020 */   143,  160,  161,   78,   28,  160,  161,  143,  143,  160,
 /*  1030 */   161,  160,  161,  143,   89,  143,  160,  161,  143,  151,
 /*  1040 */   143,   49,  143,   51,  160,  161,  143,   51,   52,  198,
 /*  1050 */   160,  161,  160,  161,  229,  160,  161,  160,  161,  160,
 /*  1060 */   161,  143,  206,  160,  161,  143,  181,   75,  143,  143,
 /*  1070 */   143,  143,  184,  143,  143,  143,  206,  143,  160,  161,
 /*  1080 */   143,   50,  160,  161,  143,  160,  161,  160,  161,  227,
 /*  1090 */   160,  161,  160,  161,  160,  161,   47,  160,  161,   47,
 /*  1100 */    51,  160,  161,   51,   52,   94,  183,   96,   97,  151,
 /*  1110 */    85,   86,   87,  154,  155,  151,  151,  194,  151,  199,
 /*  1120 */   200,    7,  108,  109,   75,  121,   50,  123,   95,   47,
 /*  1130 */   143,   49,  206,  100,  206,   48,   16,   50,   48,   48,
 /*  1140 */    50,   50,  184,   48,  113,   50,   77,   78,  184,  184,
 /*  1150 */    48,  184,   50,   48,  115,   50,  117,   51,   52,   48,
 /*  1160 */    48,   50,   50,  186,   51,   52,   52,   47,  189,   48,
 /*  1170 */    48,   50,   50,  143,   51,   52,  202,  143,  102,  143,
 /*  1180 */   143,  143,  202,  143,  228,  143,  189,  143,  228,  143,
 /*  1190 */   143,  143,  164,  143,  143,  172,  143,  143,  143,  143,
 /*  1200 */   168,  143,  202,  143,  163,  216,  163,  143,  163,  143,
 /*  1210 */   188,  143,  186,  146,  143,  143,  143,  143,   47,  209,
 /*  1220 */     5,  113,   45,  225,  189,  121,  128,  224,   45,  148,
 /*  1230 */   209,  148,   47,  148,  177,   84,  148,   63,   83,  180,
 /*  1240 */   162,  180,  180,  180,  165,  165,  162,  177,  162,  106,
 /*  1250 */    84,   47,  177,  121,  189,  162,   32,  165,  164,  170,
 /*  1260 */   112,  162,  162,  162,  107,  111,  189,  170,  110,   50,
 /*  1270 */    40,   35,    4,   36,    3,   42,   48,  144,  141,  144,
 /*  1280 */   150,  142,  142,  142,  142,   72,   43,   48,  101,  165,
 /*  1290 */    99,  153,  165,  114,   88,  102,   84,   46,  153,  127,
 /*  1300 */   204,   84,   50,  127,    1,  130,  203,  129,  114,  102,
 /*  1310 */   166,  204,  203,  166,  204,   16,   16,   16,  203,  191,
 /*  1320 */   204,  203,  193,  192,  190,  189,   16,   52,   88,    1,
 /*  1330 */   213,  105,  101,   47,  215,  124,   34,   84,   49,   46,
 /*  1340 */     7,   82,   47,  218,  218,   89,   66,   66,   54,   66,
 /*  1350 */    60,   47,   47,   95,   48,   47,   47,   50,  101,   48,
 /*  1360 */    48,   48,   47,   75,   48,   47,   52,   50,   48,   48,
 /*  1370 */   105,   47,   50,  104,   47,  105,   50,  105,   48,   48,
 /*  1380 */    48,   38,   48,  101,   48,   50,   48,   47,   49,   42,
 /*  1390 */    48,   50,   47,   49,   48,   47,    1,   48,   48,   47,
 /*  1400 */     0,   48,
};
#define YY_SHIFT_USE_DFLT (1402)
#define YY_SHIFT_COUNT    (408)
#define YY_SHIFT_MIN      (-82)
#define YY_SHIFT_MAX      (1400)
static const short yy_shift_ofst[] = {
 /*     0 */   586,  564,  598,  134,  736,  736,  736,  736,  571,   -5,
 /*    10 */    71,   71,  736,  736,  736,  736,  736,  736,  736,  576,
 /*    20 */   576,  542,  227,  191,  392,   99,  128,  177,  226,  275,
 /*    30 */   324,  373,  401,  429,  457,  457,  457,  457,  457,  457,
 /*    40 */   457,  457,  457,  457,  457,  457,  457,  457,  457,  524,
 /*    50 */   457,  490,  550,  550,  702,  736,  736,  736,  736,  736,
 /*    60 */   736,  736,  736,  736,  736,  736,  736,  736,  736,  736,
 /*    70 */   736,  736,  736,  736,  736,  736,  736,  736,  736,  736,
 /*    80 */   736,  736,  840,  736,  736,  736,  736,  736,  736,  736,
 /*    90 */   736,  736,  736,  736,  736,  736,   11,   30,   30,   30,
 /*   100 */    30,   30,  188,   37,   43,  661,  201,  201,  720,  695,
 /*   110 */   772,  -16, 1402, 1402, 1402,  645,  645,  633,  633,  826,
 /*   120 */   221,  221,  858,  772,  317,  772,  772,  772,  772,  772,
 /*   130 */   772,  772,  772,  772,  772,  772,  772,  772,  772,  772,
 /*   140 */   772,  691,  772,  772,  772,  691,  695,  -82,  -82,  -82,
 /*   150 */   -82,  -82,  -82, 1402, 1402,  810,  242,  242,  285,  806,
 /*   160 */   806,  806,  119,  823,  668,  778,  868,  716,  911,  638,
 /*   170 */   996,  992,  992,  992,  853,  843, 1052, 1011,  637,  772,
 /*   180 */   772,  772,  772, 1031,  648,  648,  772,  772, 1114, 1031,
 /*   190 */   772, 1114,  772,  772,  772,  772,  772,  772,  796,  772,
 /*   200 */   593,  772,   36,  772, 1014,  772,  772,  648,  772, 1004,
 /*   210 */  1014, 1014,  772,  772,  772, 1076, 1033,  772, 1082,  772,
 /*   220 */   772,  772,  772, 1171, 1215, 1108, 1177, 1177, 1177, 1177,
 /*   230 */  1104, 1098, 1183, 1108, 1171, 1215, 1215, 1108, 1183, 1185,
 /*   240 */  1183, 1183, 1185, 1151, 1151, 1151, 1174, 1185, 1151, 1155,
 /*   250 */  1151, 1174, 1151, 1151, 1143, 1166, 1143, 1166, 1143, 1166,
 /*   260 */  1143, 1166, 1204, 1132, 1185, 1224, 1224, 1185, 1148, 1157,
 /*   270 */  1154, 1158, 1108, 1219, 1230, 1230, 1236, 1236, 1236, 1236,
 /*   280 */  1237, 1402, 1402, 1402, 1402,  152,  852, 1025, 1049, 1120,
 /*   290 */  1087, 1090, 1091, 1095, 1102, 1105, 1106, 1069,  725,  945,
 /*   300 */  1111, 1112, 1113, 1121,  723, 1039, 1122, 1123,  873, 1268,
 /*   310 */  1271, 1233, 1213, 1243, 1228, 1239, 1187, 1191, 1179, 1206,
 /*   320 */  1193, 1212, 1251, 1172, 1252, 1176, 1175, 1178, 1217, 1303,
 /*   330 */  1207, 1194, 1299, 1300, 1301, 1310, 1240, 1275, 1226, 1231,
 /*   340 */  1328, 1302, 1286, 1253, 1211, 1289, 1293, 1333, 1256, 1259,
 /*   350 */  1295, 1280, 1304, 1305, 1306, 1308, 1281, 1294, 1309, 1283,
 /*   360 */  1290, 1311, 1312, 1313, 1307, 1258, 1315, 1316, 1318, 1317,
 /*   370 */  1257, 1320, 1321, 1314, 1265, 1324, 1269, 1322, 1270, 1326,
 /*   380 */  1272, 1330, 1322, 1331, 1332, 1334, 1288, 1335, 1336, 1327,
 /*   390 */  1343, 1338, 1340, 1339, 1341, 1342, 1345, 1344, 1341, 1346,
 /*   400 */  1348, 1349, 1350, 1352, 1282, 1353, 1347, 1395, 1400,
};
#define YY_REDUCE_USE_DFLT (-69)
#define YY_REDUCE_COUNT (284)
#define YY_REDUCE_MIN   (-68)
#define YY_REDUCE_MAX   (1147)
static const short yy_reduce_ofst[] = {
 /*     0 */   237,  503,  595,  124,  167,  440,  545,  604,  552,  397,
 /*    10 */   158,  547,  487,  624,  652,  646,  673,  684,  682,  456,
 /*    20 */   692,  446,  681,  560,  508,  282,  282,  282,  282,  282,
 /*    30 */   282,  282,  282,  282,  282,  282,  282,  282,  282,  282,
 /*    40 */   282,  282,  282,  282,  282,  282,  282,  282,  282,  282,
 /*    50 */   282,  282,  282,  282,  631,  717,  741,  756,  771,  779,
 /*    60 */   781,  790,  793,  801,  805,  820,  827,  830,  834,  836,
 /*    70 */   842,  846,  850,  855,  857,  861,  865,  869,  871,  876,
 /*    80 */   884,  890,  892,  895,  897,  899,  903,  918,  922,  925,
 /*    90 */   927,  930,  932,  934,  937,  941,  282,  282,  282,  282,
 /*   100 */   282,  282,  282,  282,  282,   94,  370,  428,  282,  614,
 /*   110 */   477,  282,  282,  282,  282,  437,  437,  653,  759,    4,
 /*   120 */   104,  526,  449,  -11,   14,  179,  531,  667,  747,  754,
 /*   130 */   791,  828,  780,  885,  685,  856,  737,  870,  926,  862,
 /*   140 */   202,  829,  928,  825,  463,  959,  923,  798,  888,  958,
 /*   150 */   964,  965,  967,  920,  122,  -68,   17,   21,  106,  198,
 /*   160 */   253,  342,  170,  326,  340,  491,  455,  516,  554,  760,
 /*   170 */   877,  583,  803,  851,  931,  170,  987,  674,  977, 1030,
 /*   180 */  1034, 1036, 1037,  979,  974,  980, 1038, 1040,  956,  997,
 /*   190 */  1042,  960, 1044, 1046, 1047, 1048, 1050, 1051, 1028, 1053,
 /*   200 */  1023, 1054, 1032, 1055, 1041, 1056, 1058, 1000, 1060,  989,
 /*   210 */  1043, 1045, 1064, 1066,  877, 1022, 1026, 1068, 1067, 1071,
 /*   220 */  1072, 1073, 1074, 1010, 1057, 1035, 1059, 1061, 1062, 1063,
 /*   230 */   998, 1003, 1081, 1065, 1021, 1070, 1075, 1077, 1083, 1079,
 /*   240 */  1085, 1088, 1080, 1078, 1084, 1086, 1089, 1092, 1093, 1094,
 /*   250 */  1099, 1097, 1100, 1101, 1096, 1103, 1107, 1109, 1110, 1115,
 /*   260 */  1116, 1118, 1117, 1119, 1124, 1125, 1126, 1127, 1129, 1131,
 /*   270 */  1128, 1134, 1136, 1130, 1133, 1135, 1139, 1140, 1141, 1142,
 /*   280 */  1137, 1138, 1144, 1147, 1145,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1156, 1150, 1150, 1150, 1097, 1097, 1097, 1097, 1150,  993,
 /*    10 */  1020, 1020, 1198, 1198, 1198, 1198, 1198, 1198, 1096, 1198,
 /*    20 */  1198, 1198, 1198, 1150,  997, 1026, 1198, 1198, 1198, 1098,
 /*    30 */  1099, 1198, 1198, 1198, 1131, 1036, 1035, 1034, 1033, 1007,
 /*    40 */  1031, 1024, 1028, 1098, 1092, 1093, 1091, 1095, 1099, 1198,
 /*    50 */  1027, 1061, 1076, 1060, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    60 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    70 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    80 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*    90 */  1198, 1198, 1198, 1198, 1198, 1198, 1070, 1075, 1082, 1074,
 /*   100 */  1071, 1063, 1062, 1064, 1065,  964, 1198, 1198, 1066, 1198,
 /*   110 */  1198, 1067, 1079, 1078, 1077, 1165, 1164, 1198, 1198, 1104,
 /*   120 */  1198, 1198, 1198, 1198, 1150, 1198, 1198, 1198, 1198, 1198,
 /*   130 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   140 */  1198,  922, 1198, 1198, 1198,  922, 1198, 1150, 1150, 1150,
 /*   150 */  1150, 1150, 1150,  997,  988, 1198, 1198, 1198, 1198, 1198,
 /*   160 */  1198, 1198, 1198,  993, 1198, 1198, 1198, 1198, 1126, 1198,
 /*   170 */  1198,  993,  993,  993, 1198,  995, 1198,  977,  987, 1198,
 /*   180 */  1147, 1198, 1118, 1030, 1009, 1009, 1198, 1198, 1197, 1030,
 /*   190 */  1198, 1197, 1198, 1198, 1198, 1198, 1198, 1198,  939, 1198,
 /*   200 */  1176, 1198,  936, 1198, 1020, 1198, 1198, 1009, 1198, 1094,
 /*   210 */  1020, 1020, 1198, 1198, 1198,  994,  987, 1198, 1198, 1198,
 /*   220 */  1198, 1198, 1159, 1041,  967, 1030,  973,  973,  973,  973,
 /*   230 */  1130, 1194,  916, 1030, 1041,  967,  967, 1030,  916, 1105,
 /*   240 */   916,  916, 1105,  965,  965,  965,  954, 1105,  965,  939,
 /*   250 */   965,  954,  965,  965, 1013, 1008, 1013, 1008, 1013, 1008,
 /*   260 */  1013, 1008, 1100, 1198, 1105, 1109, 1109, 1105, 1025, 1014,
 /*   270 */  1023, 1021, 1030,  957, 1162, 1162, 1158, 1158, 1158, 1158,
 /*   280 */   906, 1171,  941,  941, 1171, 1198, 1198, 1198, 1166, 1112,
 /*   290 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   300 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1047, 1198,
 /*   310 */   903, 1198, 1198, 1198, 1198, 1198, 1189, 1198, 1198, 1198,
 /*   320 */  1198, 1198, 1198, 1198, 1129, 1128, 1198, 1198, 1198, 1198,
 /*   330 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1196,
 /*   340 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   350 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   360 */  1198, 1198, 1198, 1198, 1198,  979, 1198, 1198, 1198, 1180,
 /*   370 */  1198, 1198, 1198, 1198, 1198, 1198, 1198, 1022, 1198, 1015,
 /*   380 */  1198, 1198, 1186, 1198, 1198, 1198, 1198, 1198, 1198, 1198,
 /*   390 */  1198, 1198, 1198, 1198, 1152, 1198, 1198, 1198, 1151, 1198,
 /*   400 */  1198, 1198, 1198, 1198, 1198, 1198,  910, 1198, 1198,
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
    0,  /*    INDEXED => nothing */
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
  "ALTER",         "WITH",          "RECURSIVE",     "error",       
  "input",         "ecmd",          "explain",       "cmdx",        
  "cmd",           "transtype",     "trans_opt",     "nm",          
  "savepoint_opt",  "create_table",  "create_table_args",  "createkw",    
  "ifnotexists",   "columnlist",    "conslist_opt",  "select",      
  "columnname",    "carglist",      "typetoken",     "typename",    
  "signed",        "plus_num",      "minus_num",     "ccons",       
  "term",          "expr",          "onconf",        "sortorder",   
  "autoinc",       "eidlist_opt",   "refargs",       "defer_subclause",
  "refarg",        "refact",        "init_deferred_pred_opt",  "conslist",    
  "tconscomma",    "tcons",         "sortlist",      "eidlist",     
  "defer_subclause_opt",  "orconf",        "resolvetype",   "raisetype",   
  "ifexists",      "fullname",      "selectnowith",  "oneselect",   
  "with",          "multiselect_op",  "distinct",      "selcollist",  
  "from",          "where_opt",     "groupby_opt",   "having_opt",  
  "orderby_opt",   "limit_opt",     "values",        "nexprlist",   
  "exprlist",      "sclp",          "as",            "seltablist",  
  "stl_prefix",    "joinop",        "indexed_opt",   "on_opt",      
  "using_opt",     "join_nm",       "idlist",        "setlist",     
  "insert_cmd",    "idlist_opt",    "likeop",        "between_op",  
  "in_op",         "paren_exprlist",  "case_operand",  "case_exprlist",
  "case_else",     "uniqueflag",    "collate",       "nmnum",       
  "trigger_decl",  "trigger_cmd_list",  "trigger_time",  "trigger_event",
  "foreach_clause",  "when_clause",   "trigger_cmd",   "trnm",        
  "tridxby",       "wqlist",      
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
 /* 217 */ "cmd ::= PRAGMA",
 /* 218 */ "plus_num ::= PLUS INTEGER|FLOAT",
 /* 219 */ "minus_num ::= MINUS INTEGER|FLOAT",
 /* 220 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
 /* 221 */ "trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause",
 /* 222 */ "trigger_time ::= BEFORE",
 /* 223 */ "trigger_time ::= AFTER",
 /* 224 */ "trigger_time ::= INSTEAD OF",
 /* 225 */ "trigger_time ::=",
 /* 226 */ "trigger_event ::= DELETE|INSERT",
 /* 227 */ "trigger_event ::= UPDATE",
 /* 228 */ "trigger_event ::= UPDATE OF idlist",
 /* 229 */ "when_clause ::=",
 /* 230 */ "when_clause ::= WHEN expr",
 /* 231 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
 /* 232 */ "trigger_cmd_list ::= trigger_cmd SEMI",
 /* 233 */ "trnm ::= nm DOT nm",
 /* 234 */ "tridxby ::= INDEXED BY nm",
 /* 235 */ "tridxby ::= NOT INDEXED",
 /* 236 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 237 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
 /* 238 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 239 */ "trigger_cmd ::= select",
 /* 240 */ "expr ::= RAISE LP IGNORE RP",
 /* 241 */ "expr ::= RAISE LP raisetype COMMA STRING RP",
 /* 242 */ "raisetype ::= ROLLBACK",
 /* 243 */ "raisetype ::= ABORT",
 /* 244 */ "raisetype ::= FAIL",
 /* 245 */ "cmd ::= DROP TRIGGER ifexists fullname",
 /* 246 */ "cmd ::= ANALYZE",
 /* 247 */ "cmd ::= ANALYZE nm",
 /* 248 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
 /* 249 */ "with ::=",
 /* 250 */ "with ::= WITH wqlist",
 /* 251 */ "with ::= WITH RECURSIVE wqlist",
 /* 252 */ "wqlist ::= nm eidlist_opt AS LP select RP",
 /* 253 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
 /* 254 */ "input ::= ecmd",
 /* 255 */ "explain ::=",
 /* 256 */ "cmdx ::= cmd",
 /* 257 */ "trans_opt ::=",
 /* 258 */ "trans_opt ::= TRANSACTION",
 /* 259 */ "trans_opt ::= TRANSACTION nm",
 /* 260 */ "savepoint_opt ::= SAVEPOINT",
 /* 261 */ "savepoint_opt ::=",
 /* 262 */ "cmd ::= create_table create_table_args",
 /* 263 */ "columnlist ::= columnlist COMMA columnname carglist",
 /* 264 */ "columnlist ::= columnname carglist",
 /* 265 */ "typetoken ::= typename",
 /* 266 */ "typename ::= ID|STRING",
 /* 267 */ "signed ::= plus_num",
 /* 268 */ "signed ::= minus_num",
 /* 269 */ "carglist ::= carglist ccons",
 /* 270 */ "carglist ::=",
 /* 271 */ "ccons ::= NULL onconf",
 /* 272 */ "conslist_opt ::= COMMA conslist",
 /* 273 */ "conslist ::= conslist tconscomma tcons",
 /* 274 */ "conslist ::= tcons",
 /* 275 */ "tconscomma ::=",
 /* 276 */ "defer_subclause_opt ::= defer_subclause",
 /* 277 */ "resolvetype ::= raisetype",
 /* 278 */ "selectnowith ::= oneselect",
 /* 279 */ "oneselect ::= values",
 /* 280 */ "sclp ::= selcollist COMMA",
 /* 281 */ "as ::= ID|STRING",
 /* 282 */ "join_nm ::= ID|INDEXED",
 /* 283 */ "join_nm ::= JOIN_KW",
 /* 284 */ "expr ::= term",
 /* 285 */ "exprlist ::= nexprlist",
 /* 286 */ "nmnum ::= plus_num",
 /* 287 */ "nmnum ::= STRING",
 /* 288 */ "nmnum ::= nm",
 /* 289 */ "nmnum ::= ON",
 /* 290 */ "nmnum ::= DELETE",
 /* 291 */ "nmnum ::= DEFAULT",
 /* 292 */ "plus_num ::= INTEGER|FLOAT",
 /* 293 */ "foreach_clause ::=",
 /* 294 */ "foreach_clause ::= FOR EACH ROW",
 /* 295 */ "trnm ::= nm",
 /* 296 */ "tridxby ::=",
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
    case 151: /* select */
    case 182: /* selectnowith */
    case 183: /* oneselect */
    case 194: /* values */
{
#line 399 "parse.y"
sqlite3SelectDelete(pParse->db, (yypminor->yy279));
#line 1469 "parse.c"
}
      break;
    case 160: /* term */
    case 161: /* expr */
{
#line 842 "parse.y"
sql_expr_free(pParse->db, (yypminor->yy162).pExpr, false);
#line 1477 "parse.c"
}
      break;
    case 165: /* eidlist_opt */
    case 174: /* sortlist */
    case 175: /* eidlist */
    case 187: /* selcollist */
    case 190: /* groupby_opt */
    case 192: /* orderby_opt */
    case 195: /* nexprlist */
    case 196: /* exprlist */
    case 197: /* sclp */
    case 207: /* setlist */
    case 213: /* paren_exprlist */
    case 215: /* case_exprlist */
{
#line 1274 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy382));
#line 1495 "parse.c"
}
      break;
    case 181: /* fullname */
    case 188: /* from */
    case 199: /* seltablist */
    case 200: /* stl_prefix */
{
#line 626 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy387));
#line 1505 "parse.c"
}
      break;
    case 184: /* with */
    case 229: /* wqlist */
{
#line 1525 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy151));
#line 1513 "parse.c"
}
      break;
    case 189: /* where_opt */
    case 191: /* having_opt */
    case 203: /* on_opt */
    case 214: /* case_operand */
    case 216: /* case_else */
    case 225: /* when_clause */
{
#line 751 "parse.y"
sql_expr_free(pParse->db, (yypminor->yy362), false);
#line 1525 "parse.c"
}
      break;
    case 204: /* using_opt */
    case 206: /* idlist */
    case 209: /* idlist_opt */
{
#line 663 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy40));
#line 1534 "parse.c"
}
      break;
    case 221: /* trigger_cmd_list */
    case 226: /* trigger_cmd */
{
#line 1398 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy427));
#line 1542 "parse.c"
}
      break;
    case 223: /* trigger_event */
{
#line 1384 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy10).b);
#line 1549 "parse.c"
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
#line 1724 "parse.c"
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
  { 137, 3 },
  { 137, 1 },
  { 138, 1 },
  { 138, 3 },
  { 140, 3 },
  { 141, 0 },
  { 141, 1 },
  { 140, 2 },
  { 140, 2 },
  { 140, 2 },
  { 140, 2 },
  { 140, 3 },
  { 140, 5 },
  { 145, 4 },
  { 147, 1 },
  { 148, 0 },
  { 148, 3 },
  { 146, 4 },
  { 146, 2 },
  { 152, 2 },
  { 143, 1 },
  { 154, 0 },
  { 154, 4 },
  { 154, 6 },
  { 155, 2 },
  { 159, 2 },
  { 159, 2 },
  { 159, 4 },
  { 159, 3 },
  { 159, 3 },
  { 159, 2 },
  { 159, 3 },
  { 159, 5 },
  { 159, 2 },
  { 159, 4 },
  { 159, 4 },
  { 159, 1 },
  { 159, 2 },
  { 164, 0 },
  { 164, 1 },
  { 166, 0 },
  { 166, 2 },
  { 168, 2 },
  { 168, 3 },
  { 168, 3 },
  { 168, 3 },
  { 169, 2 },
  { 169, 2 },
  { 169, 1 },
  { 169, 1 },
  { 169, 2 },
  { 167, 3 },
  { 167, 2 },
  { 170, 0 },
  { 170, 2 },
  { 170, 2 },
  { 150, 0 },
  { 172, 1 },
  { 173, 2 },
  { 173, 7 },
  { 173, 5 },
  { 173, 5 },
  { 173, 10 },
  { 176, 0 },
  { 162, 0 },
  { 162, 3 },
  { 177, 0 },
  { 177, 2 },
  { 178, 1 },
  { 178, 1 },
  { 140, 4 },
  { 180, 2 },
  { 180, 0 },
  { 140, 7 },
  { 140, 4 },
  { 140, 1 },
  { 151, 2 },
  { 182, 3 },
  { 185, 1 },
  { 185, 2 },
  { 185, 1 },
  { 183, 9 },
  { 194, 4 },
  { 194, 5 },
  { 186, 1 },
  { 186, 1 },
  { 186, 0 },
  { 197, 0 },
  { 187, 3 },
  { 187, 2 },
  { 187, 4 },
  { 198, 2 },
  { 198, 0 },
  { 188, 0 },
  { 188, 2 },
  { 200, 2 },
  { 200, 0 },
  { 199, 6 },
  { 199, 8 },
  { 199, 7 },
  { 199, 7 },
  { 181, 1 },
  { 201, 1 },
  { 201, 2 },
  { 201, 3 },
  { 201, 4 },
  { 203, 2 },
  { 203, 0 },
  { 202, 0 },
  { 202, 3 },
  { 202, 2 },
  { 204, 4 },
  { 204, 0 },
  { 192, 0 },
  { 192, 3 },
  { 174, 4 },
  { 174, 2 },
  { 163, 1 },
  { 163, 1 },
  { 163, 0 },
  { 190, 0 },
  { 190, 3 },
  { 191, 0 },
  { 191, 2 },
  { 193, 0 },
  { 193, 2 },
  { 193, 4 },
  { 193, 4 },
  { 140, 6 },
  { 189, 0 },
  { 189, 2 },
  { 140, 8 },
  { 207, 5 },
  { 207, 7 },
  { 207, 3 },
  { 207, 5 },
  { 140, 6 },
  { 140, 7 },
  { 208, 2 },
  { 208, 1 },
  { 209, 0 },
  { 209, 3 },
  { 206, 3 },
  { 206, 1 },
  { 161, 3 },
  { 160, 1 },
  { 161, 1 },
  { 161, 1 },
  { 161, 3 },
  { 160, 1 },
  { 160, 1 },
  { 160, 1 },
  { 161, 1 },
  { 161, 3 },
  { 161, 6 },
  { 161, 5 },
  { 161, 4 },
  { 160, 1 },
  { 161, 5 },
  { 161, 3 },
  { 161, 3 },
  { 161, 3 },
  { 161, 3 },
  { 161, 3 },
  { 161, 3 },
  { 161, 3 },
  { 161, 3 },
  { 210, 1 },
  { 210, 2 },
  { 161, 3 },
  { 161, 5 },
  { 161, 2 },
  { 161, 3 },
  { 161, 3 },
  { 161, 4 },
  { 161, 2 },
  { 161, 2 },
  { 161, 2 },
  { 161, 2 },
  { 211, 1 },
  { 211, 2 },
  { 161, 5 },
  { 212, 1 },
  { 212, 2 },
  { 161, 5 },
  { 161, 3 },
  { 161, 5 },
  { 161, 4 },
  { 161, 4 },
  { 161, 5 },
  { 215, 5 },
  { 215, 4 },
  { 216, 2 },
  { 216, 0 },
  { 214, 1 },
  { 214, 0 },
  { 196, 0 },
  { 195, 3 },
  { 195, 1 },
  { 213, 0 },
  { 213, 3 },
  { 140, 11 },
  { 217, 1 },
  { 217, 0 },
  { 165, 0 },
  { 165, 3 },
  { 175, 5 },
  { 175, 3 },
  { 218, 0 },
  { 218, 2 },
  { 140, 6 },
  { 140, 2 },
  { 140, 4 },
  { 140, 5 },
  { 140, 4 },
  { 140, 5 },
  { 140, 6 },
  { 140, 1 },
  { 157, 2 },
  { 158, 2 },
  { 140, 5 },
  { 220, 9 },
  { 222, 1 },
  { 222, 1 },
  { 222, 2 },
  { 222, 0 },
  { 223, 1 },
  { 223, 1 },
  { 223, 3 },
  { 225, 0 },
  { 225, 2 },
  { 221, 3 },
  { 221, 2 },
  { 227, 3 },
  { 228, 3 },
  { 228, 2 },
  { 226, 7 },
  { 226, 5 },
  { 226, 5 },
  { 226, 1 },
  { 161, 4 },
  { 161, 6 },
  { 179, 1 },
  { 179, 1 },
  { 179, 1 },
  { 140, 4 },
  { 140, 1 },
  { 140, 2 },
  { 140, 6 },
  { 184, 0 },
  { 184, 2 },
  { 184, 3 },
  { 229, 6 },
  { 229, 8 },
  { 136, 1 },
  { 138, 0 },
  { 139, 1 },
  { 142, 0 },
  { 142, 1 },
  { 142, 2 },
  { 144, 1 },
  { 144, 0 },
  { 140, 2 },
  { 149, 4 },
  { 149, 2 },
  { 154, 1 },
  { 155, 1 },
  { 156, 1 },
  { 156, 1 },
  { 153, 2 },
  { 153, 0 },
  { 159, 2 },
  { 150, 2 },
  { 171, 3 },
  { 171, 1 },
  { 172, 0 },
  { 176, 1 },
  { 178, 1 },
  { 182, 1 },
  { 183, 1 },
  { 197, 2 },
  { 198, 1 },
  { 205, 1 },
  { 205, 1 },
  { 161, 1 },
  { 196, 1 },
  { 219, 1 },
  { 219, 1 },
  { 219, 1 },
  { 219, 1 },
  { 219, 1 },
  { 219, 1 },
  { 157, 1 },
  { 224, 0 },
  { 224, 3 },
  { 227, 1 },
  { 228, 0 },
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
{
	if (!pParse->parse_only)
		sqlite3FinishCoding(pParse);
}
#line 2164 "parse.c"
        break;
      case 1: /* ecmd ::= SEMI */
#line 115 "parse.y"
{
  sqlite3ErrorMsg(pParse, "syntax error: empty request");
}
#line 2171 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN */
#line 120 "parse.y"
{ pParse->explain = 1; }
#line 2176 "parse.c"
        break;
      case 3: /* explain ::= EXPLAIN QUERY PLAN */
#line 121 "parse.y"
{ pParse->explain = 2; }
#line 2181 "parse.c"
        break;
      case 4: /* cmd ::= BEGIN transtype trans_opt */
#line 153 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy52);}
#line 2186 "parse.c"
        break;
      case 5: /* transtype ::= */
#line 158 "parse.y"
{yymsp[1].minor.yy52 = TK_DEFERRED;}
#line 2191 "parse.c"
        break;
      case 6: /* transtype ::= DEFERRED */
#line 159 "parse.y"
{yymsp[0].minor.yy52 = yymsp[0].major; /*A-overwrites-X*/}
#line 2196 "parse.c"
        break;
      case 7: /* cmd ::= COMMIT trans_opt */
      case 8: /* cmd ::= END trans_opt */ yytestcase(yyruleno==8);
#line 160 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2202 "parse.c"
        break;
      case 9: /* cmd ::= ROLLBACK trans_opt */
#line 162 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2207 "parse.c"
        break;
      case 10: /* cmd ::= SAVEPOINT nm */
#line 166 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2214 "parse.c"
        break;
      case 11: /* cmd ::= RELEASE savepoint_opt nm */
#line 169 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2221 "parse.c"
        break;
      case 12: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 172 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2228 "parse.c"
        break;
      case 13: /* create_table ::= createkw TABLE ifnotexists nm */
#line 179 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[0].minor.yy0,yymsp[-1].minor.yy52);
}
#line 2235 "parse.c"
        break;
      case 14: /* createkw ::= CREATE */
#line 182 "parse.y"
{disableLookaside(pParse);}
#line 2240 "parse.c"
        break;
      case 15: /* ifnotexists ::= */
      case 38: /* autoinc ::= */ yytestcase(yyruleno==38);
      case 53: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==53);
      case 63: /* defer_subclause_opt ::= */ yytestcase(yyruleno==63);
      case 72: /* ifexists ::= */ yytestcase(yyruleno==72);
      case 86: /* distinct ::= */ yytestcase(yyruleno==86);
      case 208: /* collate ::= */ yytestcase(yyruleno==208);
#line 185 "parse.y"
{yymsp[1].minor.yy52 = 0;}
#line 2251 "parse.c"
        break;
      case 16: /* ifnotexists ::= IF NOT EXISTS */
#line 186 "parse.y"
{yymsp[-2].minor.yy52 = 1;}
#line 2256 "parse.c"
        break;
      case 17: /* create_table_args ::= LP columnlist conslist_opt RP */
#line 188 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0,0);
}
#line 2263 "parse.c"
        break;
      case 18: /* create_table_args ::= AS select */
#line 191 "parse.y"
{
  sqlite3EndTable(pParse,0,0,yymsp[0].minor.yy279);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy279);
}
#line 2271 "parse.c"
        break;
      case 19: /* columnname ::= nm typetoken */
#line 197 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2276 "parse.c"
        break;
      case 20: /* nm ::= ID|INDEXED */
#line 235 "parse.y"
{
  if(yymsp[0].minor.yy0.isReserved) {
    sqlite3ErrorMsg(pParse, "keyword \"%T\" is reserved", &yymsp[0].minor.yy0);
  }
}
#line 2285 "parse.c"
        break;
      case 21: /* typetoken ::= */
      case 56: /* conslist_opt ::= */ yytestcase(yyruleno==56);
      case 92: /* as ::= */ yytestcase(yyruleno==92);
#line 246 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2292 "parse.c"
        break;
      case 22: /* typetoken ::= typename LP signed RP */
#line 248 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2299 "parse.c"
        break;
      case 23: /* typetoken ::= typename LP signed COMMA signed RP */
#line 251 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2306 "parse.c"
        break;
      case 24: /* typename ::= typename ID|STRING */
#line 256 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2311 "parse.c"
        break;
      case 25: /* ccons ::= CONSTRAINT nm */
      case 58: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==58);
#line 265 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2317 "parse.c"
        break;
      case 26: /* ccons ::= DEFAULT term */
      case 28: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==28);
#line 266 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy162);}
#line 2323 "parse.c"
        break;
      case 27: /* ccons ::= DEFAULT LP expr RP */
#line 267 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy162);}
#line 2328 "parse.c"
        break;
      case 29: /* ccons ::= DEFAULT MINUS term */
#line 269 "parse.y"
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy162.pExpr, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy162.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2339 "parse.c"
        break;
      case 30: /* ccons ::= DEFAULT ID|INDEXED */
#line 276 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2348 "parse.c"
        break;
      case 31: /* ccons ::= NOT NULL onconf */
#line 286 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy52);}
#line 2353 "parse.c"
        break;
      case 32: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 288 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy52,yymsp[0].minor.yy52,yymsp[-2].minor.yy52);}
#line 2358 "parse.c"
        break;
      case 33: /* ccons ::= UNIQUE onconf */
#line 289 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[0].minor.yy52,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2364 "parse.c"
        break;
      case 34: /* ccons ::= CHECK LP expr RP */
#line 291 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy162.pExpr);}
#line 2369 "parse.c"
        break;
      case 35: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 293 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy382,yymsp[0].minor.yy52);}
#line 2374 "parse.c"
        break;
      case 36: /* ccons ::= defer_subclause */
#line 294 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy52);}
#line 2379 "parse.c"
        break;
      case 37: /* ccons ::= COLLATE ID|INDEXED */
#line 295 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2384 "parse.c"
        break;
      case 39: /* autoinc ::= AUTOINCR */
#line 300 "parse.y"
{yymsp[0].minor.yy52 = 1;}
#line 2389 "parse.c"
        break;
      case 40: /* refargs ::= */
#line 308 "parse.y"
{ yymsp[1].minor.yy52 = ON_CONFLICT_ACTION_NONE*0x0101; /* EV: R-19803-45884 */}
#line 2394 "parse.c"
        break;
      case 41: /* refargs ::= refargs refarg */
#line 309 "parse.y"
{ yymsp[-1].minor.yy52 = (yymsp[-1].minor.yy52 & ~yymsp[0].minor.yy107.mask) | yymsp[0].minor.yy107.value; }
#line 2399 "parse.c"
        break;
      case 42: /* refarg ::= MATCH nm */
#line 311 "parse.y"
{ yymsp[-1].minor.yy107.value = 0;     yymsp[-1].minor.yy107.mask = 0x000000; }
#line 2404 "parse.c"
        break;
      case 43: /* refarg ::= ON INSERT refact */
#line 312 "parse.y"
{ yymsp[-2].minor.yy107.value = 0;     yymsp[-2].minor.yy107.mask = 0x000000; }
#line 2409 "parse.c"
        break;
      case 44: /* refarg ::= ON DELETE refact */
#line 313 "parse.y"
{ yymsp[-2].minor.yy107.value = yymsp[0].minor.yy52;     yymsp[-2].minor.yy107.mask = 0x0000ff; }
#line 2414 "parse.c"
        break;
      case 45: /* refarg ::= ON UPDATE refact */
#line 314 "parse.y"
{ yymsp[-2].minor.yy107.value = yymsp[0].minor.yy52<<8;  yymsp[-2].minor.yy107.mask = 0x00ff00; }
#line 2419 "parse.c"
        break;
      case 46: /* refact ::= SET NULL */
#line 316 "parse.y"
{ yymsp[-1].minor.yy52 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2424 "parse.c"
        break;
      case 47: /* refact ::= SET DEFAULT */
#line 317 "parse.y"
{ yymsp[-1].minor.yy52 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2429 "parse.c"
        break;
      case 48: /* refact ::= CASCADE */
#line 318 "parse.y"
{ yymsp[0].minor.yy52 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2434 "parse.c"
        break;
      case 49: /* refact ::= RESTRICT */
#line 319 "parse.y"
{ yymsp[0].minor.yy52 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2439 "parse.c"
        break;
      case 50: /* refact ::= NO ACTION */
#line 320 "parse.y"
{ yymsp[-1].minor.yy52 = ON_CONFLICT_ACTION_NONE;     /* EV: R-33326-45252 */}
#line 2444 "parse.c"
        break;
      case 51: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 322 "parse.y"
{yymsp[-2].minor.yy52 = 0;}
#line 2449 "parse.c"
        break;
      case 52: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 67: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==67);
      case 138: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==138);
#line 323 "parse.y"
{yymsp[-1].minor.yy52 = yymsp[0].minor.yy52;}
#line 2456 "parse.c"
        break;
      case 54: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 71: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==71);
      case 180: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==180);
      case 183: /* in_op ::= NOT IN */ yytestcase(yyruleno==183);
      case 209: /* collate ::= COLLATE ID|INDEXED */ yytestcase(yyruleno==209);
#line 326 "parse.y"
{yymsp[-1].minor.yy52 = 1;}
#line 2465 "parse.c"
        break;
      case 55: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 327 "parse.y"
{yymsp[-1].minor.yy52 = 0;}
#line 2470 "parse.c"
        break;
      case 57: /* tconscomma ::= COMMA */
#line 333 "parse.y"
{pParse->constraintName.n = 0;}
#line 2475 "parse.c"
        break;
      case 59: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 337 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy382,yymsp[0].minor.yy52,yymsp[-2].minor.yy52,0);}
#line 2480 "parse.c"
        break;
      case 60: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 339 "parse.y"
{sqlite3CreateIndex(pParse,0,0,yymsp[-2].minor.yy382,yymsp[0].minor.yy52,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2486 "parse.c"
        break;
      case 61: /* tcons ::= CHECK LP expr RP onconf */
#line 342 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy162.pExpr);}
#line 2491 "parse.c"
        break;
      case 62: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 344 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy382, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy382, yymsp[-1].minor.yy52);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy52);
}
#line 2499 "parse.c"
        break;
      case 64: /* onconf ::= */
      case 66: /* orconf ::= */ yytestcase(yyruleno==66);
#line 358 "parse.y"
{yymsp[1].minor.yy52 = ON_CONFLICT_ACTION_DEFAULT;}
#line 2505 "parse.c"
        break;
      case 65: /* onconf ::= ON CONFLICT resolvetype */
#line 359 "parse.y"
{yymsp[-2].minor.yy52 = yymsp[0].minor.yy52;}
#line 2510 "parse.c"
        break;
      case 68: /* resolvetype ::= IGNORE */
#line 363 "parse.y"
{yymsp[0].minor.yy52 = ON_CONFLICT_ACTION_IGNORE;}
#line 2515 "parse.c"
        break;
      case 69: /* resolvetype ::= REPLACE */
      case 139: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==139);
#line 364 "parse.y"
{yymsp[0].minor.yy52 = ON_CONFLICT_ACTION_REPLACE;}
#line 2521 "parse.c"
        break;
      case 70: /* cmd ::= DROP TABLE ifexists fullname */
#line 368 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy387, 0, yymsp[-1].minor.yy52);
}
#line 2528 "parse.c"
        break;
      case 73: /* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 379 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-6].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy382, yymsp[0].minor.yy279, yymsp[-4].minor.yy52);
}
#line 2535 "parse.c"
        break;
      case 74: /* cmd ::= DROP VIEW ifexists fullname */
#line 382 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy387, 1, yymsp[-1].minor.yy52);
}
#line 2542 "parse.c"
        break;
      case 75: /* cmd ::= select */
#line 389 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  if(!pParse->parse_only)
	  sqlite3Select(pParse, yymsp[0].minor.yy279, &dest);
  else
	  sql_expr_extract_select(pParse, yymsp[0].minor.yy279);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy279);
}
#line 2554 "parse.c"
        break;
      case 76: /* select ::= with selectnowith */
#line 429 "parse.y"
{
  Select *p = yymsp[0].minor.yy279;
  if( p ){
    p->pWith = yymsp[-1].minor.yy151;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, yymsp[-1].minor.yy151);
  }
  yymsp[-1].minor.yy279 = p; /*A-overwrites-W*/
}
#line 2568 "parse.c"
        break;
      case 77: /* selectnowith ::= selectnowith multiselect_op oneselect */
#line 442 "parse.y"
{
  Select *pRhs = yymsp[0].minor.yy279;
  Select *pLhs = yymsp[-2].minor.yy279;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,&x,pRhs,0,0);
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)yymsp[-1].minor.yy52;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( yymsp[-1].minor.yy52!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  yymsp[-2].minor.yy279 = pRhs;
}
#line 2594 "parse.c"
        break;
      case 78: /* multiselect_op ::= UNION */
      case 80: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==80);
#line 465 "parse.y"
{yymsp[0].minor.yy52 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2600 "parse.c"
        break;
      case 79: /* multiselect_op ::= UNION ALL */
#line 466 "parse.y"
{yymsp[-1].minor.yy52 = TK_ALL;}
#line 2605 "parse.c"
        break;
      case 81: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 470 "parse.y"
{
#ifdef SELECTTRACE_ENABLED
  Token s = yymsp[-8].minor.yy0; /*A-overwrites-S*/
#endif
  yymsp[-8].minor.yy279 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy382,yymsp[-5].minor.yy387,yymsp[-4].minor.yy362,yymsp[-3].minor.yy382,yymsp[-2].minor.yy362,yymsp[-1].minor.yy382,yymsp[-7].minor.yy52,yymsp[0].minor.yy384.pLimit,yymsp[0].minor.yy384.pOffset);
#ifdef SELECTTRACE_ENABLED
  /* Populate the Select.zSelName[] string that is used to help with
  ** query planner debugging, to differentiate between multiple Select
  ** objects in a complex query.
  **
  ** If the SELECT keyword is immediately followed by a C-style comment
  ** then extract the first few alphanumeric characters from within that
  ** comment to be the zSelName value.  Otherwise, the label is #N where
  ** is an integer that is incremented with each SELECT statement seen.
  */
  if( yymsp[-8].minor.yy279!=0 ){
    const char *z = s.z+6;
    int i;
    sqlite3_snprintf(sizeof(yymsp[-8].minor.yy279->zSelName), yymsp[-8].minor.yy279->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(yymsp[-8].minor.yy279->zSelName), yymsp[-8].minor.yy279->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
#line 2639 "parse.c"
        break;
      case 82: /* values ::= VALUES LP nexprlist RP */
#line 504 "parse.y"
{
  yymsp[-3].minor.yy279 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy382,0,0,0,0,0,SF_Values,0,0);
}
#line 2646 "parse.c"
        break;
      case 83: /* values ::= values COMMA LP exprlist RP */
#line 507 "parse.y"
{
  Select *pRight, *pLeft = yymsp[-4].minor.yy279;
  pRight = sqlite3SelectNew(pParse,yymsp[-1].minor.yy382,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    yymsp[-4].minor.yy279 = pRight;
  }else{
    yymsp[-4].minor.yy279 = pLeft;
  }
}
#line 2662 "parse.c"
        break;
      case 84: /* distinct ::= DISTINCT */
#line 524 "parse.y"
{yymsp[0].minor.yy52 = SF_Distinct;}
#line 2667 "parse.c"
        break;
      case 85: /* distinct ::= ALL */
#line 525 "parse.y"
{yymsp[0].minor.yy52 = SF_All;}
#line 2672 "parse.c"
        break;
      case 87: /* sclp ::= */
      case 113: /* orderby_opt ::= */ yytestcase(yyruleno==113);
      case 120: /* groupby_opt ::= */ yytestcase(yyruleno==120);
      case 196: /* exprlist ::= */ yytestcase(yyruleno==196);
      case 199: /* paren_exprlist ::= */ yytestcase(yyruleno==199);
      case 204: /* eidlist_opt ::= */ yytestcase(yyruleno==204);
#line 538 "parse.y"
{yymsp[1].minor.yy382 = 0;}
#line 2682 "parse.c"
        break;
      case 88: /* selcollist ::= sclp expr as */
#line 539 "parse.y"
{
   yymsp[-2].minor.yy382 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy382, yymsp[-1].minor.yy162.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy382, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy382,&yymsp[-1].minor.yy162);
}
#line 2691 "parse.c"
        break;
      case 89: /* selcollist ::= sclp STAR */
#line 544 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy382 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy382, p);
}
#line 2699 "parse.c"
        break;
      case 90: /* selcollist ::= sclp nm DOT STAR */
#line 548 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy382 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy382, pDot);
}
#line 2709 "parse.c"
        break;
      case 91: /* as ::= AS nm */
      case 218: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==218);
      case 219: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==219);
#line 559 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2716 "parse.c"
        break;
      case 93: /* from ::= */
#line 573 "parse.y"
{yymsp[1].minor.yy387 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy387));}
#line 2721 "parse.c"
        break;
      case 94: /* from ::= FROM seltablist */
#line 574 "parse.y"
{
  yymsp[-1].minor.yy387 = yymsp[0].minor.yy387;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy387);
}
#line 2729 "parse.c"
        break;
      case 95: /* stl_prefix ::= seltablist joinop */
#line 582 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy387 && yymsp[-1].minor.yy387->nSrc>0) ) yymsp[-1].minor.yy387->a[yymsp[-1].minor.yy387->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy52;
}
#line 2736 "parse.c"
        break;
      case 96: /* stl_prefix ::= */
#line 585 "parse.y"
{yymsp[1].minor.yy387 = 0;}
#line 2741 "parse.c"
        break;
      case 97: /* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 587 "parse.y"
{
  yymsp[-5].minor.yy387 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-5].minor.yy387,&yymsp[-4].minor.yy0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy362,yymsp[0].minor.yy40);
  sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy387, &yymsp[-2].minor.yy0);
}
#line 2749 "parse.c"
        break;
      case 98: /* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 592 "parse.y"
{
  yymsp[-7].minor.yy387 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-7].minor.yy387,&yymsp[-6].minor.yy0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy362,yymsp[0].minor.yy40);
  sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy387, yymsp[-4].minor.yy382);
}
#line 2757 "parse.c"
        break;
      case 99: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 598 "parse.y"
{
    yymsp[-6].minor.yy387 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy387,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy279,yymsp[-1].minor.yy362,yymsp[0].minor.yy40);
  }
#line 2764 "parse.c"
        break;
      case 100: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 602 "parse.y"
{
    if( yymsp[-6].minor.yy387==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy362==0 && yymsp[0].minor.yy40==0 ){
      yymsp[-6].minor.yy387 = yymsp[-4].minor.yy387;
    }else if( yymsp[-4].minor.yy387->nSrc==1 ){
      yymsp[-6].minor.yy387 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy387,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy362,yymsp[0].minor.yy40);
      if( yymsp[-6].minor.yy387 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy387->a[yymsp[-6].minor.yy387->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy387->a;
        pNew->zName = pOld->zName;
        pNew->pSelect = pOld->pSelect;
        pOld->zName =  0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy387);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy387);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy387,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy387 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy387,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy362,yymsp[0].minor.yy40);
    }
  }
#line 2789 "parse.c"
        break;
      case 101: /* fullname ::= nm */
#line 628 "parse.y"
{yymsp[0].minor.yy387 = sqlite3SrcListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 2794 "parse.c"
        break;
      case 102: /* joinop ::= COMMA|JOIN */
#line 634 "parse.y"
{ yymsp[0].minor.yy52 = JT_INNER; }
#line 2799 "parse.c"
        break;
      case 103: /* joinop ::= JOIN_KW JOIN */
#line 636 "parse.y"
{yymsp[-1].minor.yy52 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2804 "parse.c"
        break;
      case 104: /* joinop ::= JOIN_KW join_nm JOIN */
#line 638 "parse.y"
{yymsp[-2].minor.yy52 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2809 "parse.c"
        break;
      case 105: /* joinop ::= JOIN_KW join_nm join_nm JOIN */
#line 640 "parse.y"
{yymsp[-3].minor.yy52 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2814 "parse.c"
        break;
      case 106: /* on_opt ::= ON expr */
      case 123: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==123);
      case 130: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==130);
      case 192: /* case_else ::= ELSE expr */ yytestcase(yyruleno==192);
#line 644 "parse.y"
{yymsp[-1].minor.yy362 = yymsp[0].minor.yy162.pExpr;}
#line 2822 "parse.c"
        break;
      case 107: /* on_opt ::= */
      case 122: /* having_opt ::= */ yytestcase(yyruleno==122);
      case 129: /* where_opt ::= */ yytestcase(yyruleno==129);
      case 193: /* case_else ::= */ yytestcase(yyruleno==193);
      case 195: /* case_operand ::= */ yytestcase(yyruleno==195);
#line 645 "parse.y"
{yymsp[1].minor.yy362 = 0;}
#line 2831 "parse.c"
        break;
      case 108: /* indexed_opt ::= */
#line 658 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2836 "parse.c"
        break;
      case 109: /* indexed_opt ::= INDEXED BY nm */
#line 659 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2841 "parse.c"
        break;
      case 110: /* indexed_opt ::= NOT INDEXED */
#line 660 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2846 "parse.c"
        break;
      case 111: /* using_opt ::= USING LP idlist RP */
#line 664 "parse.y"
{yymsp[-3].minor.yy40 = yymsp[-1].minor.yy40;}
#line 2851 "parse.c"
        break;
      case 112: /* using_opt ::= */
      case 140: /* idlist_opt ::= */ yytestcase(yyruleno==140);
#line 665 "parse.y"
{yymsp[1].minor.yy40 = 0;}
#line 2857 "parse.c"
        break;
      case 114: /* orderby_opt ::= ORDER BY sortlist */
      case 121: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==121);
#line 679 "parse.y"
{yymsp[-2].minor.yy382 = yymsp[0].minor.yy382;}
#line 2863 "parse.c"
        break;
      case 115: /* sortlist ::= sortlist COMMA expr sortorder */
#line 680 "parse.y"
{
  yymsp[-3].minor.yy382 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy382,yymsp[-1].minor.yy162.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy382,yymsp[0].minor.yy52);
}
#line 2871 "parse.c"
        break;
      case 116: /* sortlist ::= expr sortorder */
#line 684 "parse.y"
{
  yymsp[-1].minor.yy382 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy162.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy382,yymsp[0].minor.yy52);
}
#line 2879 "parse.c"
        break;
      case 117: /* sortorder ::= ASC */
#line 691 "parse.y"
{yymsp[0].minor.yy52 = SQLITE_SO_ASC;}
#line 2884 "parse.c"
        break;
      case 118: /* sortorder ::= DESC */
#line 692 "parse.y"
{yymsp[0].minor.yy52 = SQLITE_SO_DESC;}
#line 2889 "parse.c"
        break;
      case 119: /* sortorder ::= */
#line 693 "parse.y"
{yymsp[1].minor.yy52 = SQLITE_SO_UNDEFINED;}
#line 2894 "parse.c"
        break;
      case 124: /* limit_opt ::= */
#line 718 "parse.y"
{yymsp[1].minor.yy384.pLimit = 0; yymsp[1].minor.yy384.pOffset = 0;}
#line 2899 "parse.c"
        break;
      case 125: /* limit_opt ::= LIMIT expr */
#line 719 "parse.y"
{yymsp[-1].minor.yy384.pLimit = yymsp[0].minor.yy162.pExpr; yymsp[-1].minor.yy384.pOffset = 0;}
#line 2904 "parse.c"
        break;
      case 126: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 721 "parse.y"
{yymsp[-3].minor.yy384.pLimit = yymsp[-2].minor.yy162.pExpr; yymsp[-3].minor.yy384.pOffset = yymsp[0].minor.yy162.pExpr;}
#line 2909 "parse.c"
        break;
      case 127: /* limit_opt ::= LIMIT expr COMMA expr */
#line 723 "parse.y"
{yymsp[-3].minor.yy384.pOffset = yymsp[-2].minor.yy162.pExpr; yymsp[-3].minor.yy384.pLimit = yymsp[0].minor.yy162.pExpr;}
#line 2914 "parse.c"
        break;
      case 128: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 740 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy151, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy387, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy387,yymsp[0].minor.yy362);
}
#line 2926 "parse.c"
        break;
      case 131: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 773 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy151, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy387, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy382,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Update(pParse,yymsp[-4].minor.yy387,yymsp[-1].minor.yy382,yymsp[0].minor.yy362,yymsp[-5].minor.yy52);
}
#line 2939 "parse.c"
        break;
      case 132: /* setlist ::= setlist COMMA nm EQ expr */
#line 787 "parse.y"
{
  yymsp[-4].minor.yy382 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy382, yymsp[0].minor.yy162.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy382, &yymsp[-2].minor.yy0, 1);
}
#line 2947 "parse.c"
        break;
      case 133: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 791 "parse.y"
{
  yymsp[-6].minor.yy382 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy382, yymsp[-3].minor.yy40, yymsp[0].minor.yy162.pExpr);
}
#line 2954 "parse.c"
        break;
      case 134: /* setlist ::= nm EQ expr */
#line 794 "parse.y"
{
  yylhsminor.yy382 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy162.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy382, &yymsp[-2].minor.yy0, 1);
}
#line 2962 "parse.c"
  yymsp[-2].minor.yy382 = yylhsminor.yy382;
        break;
      case 135: /* setlist ::= LP idlist RP EQ expr */
#line 798 "parse.y"
{
  yymsp[-4].minor.yy382 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy40, yymsp[0].minor.yy162.pExpr);
}
#line 2970 "parse.c"
        break;
      case 136: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 804 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy151, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-2].minor.yy387, yymsp[0].minor.yy279, yymsp[-1].minor.yy40, yymsp[-4].minor.yy52);
}
#line 2981 "parse.c"
        break;
      case 137: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 812 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy151, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-3].minor.yy387, 0, yymsp[-2].minor.yy40, yymsp[-5].minor.yy52);
}
#line 2992 "parse.c"
        break;
      case 141: /* idlist_opt ::= LP idlist RP */
#line 830 "parse.y"
{yymsp[-2].minor.yy40 = yymsp[-1].minor.yy40;}
#line 2997 "parse.c"
        break;
      case 142: /* idlist ::= idlist COMMA nm */
#line 832 "parse.y"
{yymsp[-2].minor.yy40 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy40,&yymsp[0].minor.yy0);}
#line 3002 "parse.c"
        break;
      case 143: /* idlist ::= nm */
#line 834 "parse.y"
{yymsp[0].minor.yy40 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3007 "parse.c"
        break;
      case 144: /* expr ::= LP expr RP */
#line 883 "parse.y"
{spanSet(&yymsp[-2].minor.yy162,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy162.pExpr = yymsp[-1].minor.yy162.pExpr;}
#line 3012 "parse.c"
        break;
      case 145: /* term ::= NULL */
      case 149: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==149);
      case 150: /* term ::= STRING */ yytestcase(yyruleno==150);
#line 884 "parse.y"
{spanExpr(&yymsp[0].minor.yy162,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3019 "parse.c"
        break;
      case 146: /* expr ::= ID|INDEXED */
      case 147: /* expr ::= JOIN_KW */ yytestcase(yyruleno==147);
#line 885 "parse.y"
{spanExpr(&yymsp[0].minor.yy162,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3025 "parse.c"
        break;
      case 148: /* expr ::= nm DOT nm */
#line 887 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy162,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3035 "parse.c"
        break;
      case 151: /* term ::= INTEGER */
#line 895 "parse.y"
{
  yylhsminor.yy162.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy162.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy162.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy162.pExpr ) yylhsminor.yy162.pExpr->flags |= EP_Leaf;
}
#line 3045 "parse.c"
  yymsp[0].minor.yy162 = yylhsminor.yy162;
        break;
      case 152: /* expr ::= VARIABLE */
#line 901 "parse.y"
{
  if( !(yymsp[0].minor.yy0.z[0]=='#' && sqlite3Isdigit(yymsp[0].minor.yy0.z[1])) ){
    u32 n = yymsp[0].minor.yy0.n;
    spanExpr(&yymsp[0].minor.yy162, pParse, TK_VARIABLE, yymsp[0].minor.yy0);
    sqlite3ExprAssignVarNumber(pParse, yymsp[0].minor.yy162.pExpr, n);
  }else{
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    Token t = yymsp[0].minor.yy0; /*A-overwrites-X*/
    assert( t.n>=2 );
    spanSet(&yymsp[0].minor.yy162, &t, &t);
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &t);
      yymsp[0].minor.yy162.pExpr = 0;
    }else{
      yymsp[0].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0);
      if( yymsp[0].minor.yy162.pExpr ) sqlite3GetInt32(&t.z[1], &yymsp[0].minor.yy162.pExpr->iTable);
    }
  }
}
#line 3071 "parse.c"
        break;
      case 153: /* expr ::= expr COLLATE ID|INDEXED */
#line 922 "parse.y"
{
  yymsp[-2].minor.yy162.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy162.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy162.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3079 "parse.c"
        break;
      case 154: /* expr ::= CAST LP expr AS typetoken RP */
#line 927 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy162,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy162.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy162.pExpr, yymsp[-3].minor.yy162.pExpr, 0);
}
#line 3088 "parse.c"
        break;
      case 155: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 933 "parse.y"
{
  if( yymsp[-1].minor.yy382 && yymsp[-1].minor.yy382->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yylhsminor.yy162.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy382, &yymsp[-4].minor.yy0);
  spanSet(&yylhsminor.yy162,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy52==SF_Distinct && yylhsminor.yy162.pExpr ){
    yylhsminor.yy162.pExpr->flags |= EP_Distinct;
  }
}
#line 3102 "parse.c"
  yymsp[-4].minor.yy162 = yylhsminor.yy162;
        break;
      case 156: /* expr ::= ID|INDEXED LP STAR RP */
#line 943 "parse.y"
{
  yylhsminor.yy162.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy162,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3111 "parse.c"
  yymsp[-3].minor.yy162 = yylhsminor.yy162;
        break;
      case 157: /* term ::= CTIME_KW */
#line 947 "parse.y"
{
  yylhsminor.yy162.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy162, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3120 "parse.c"
  yymsp[0].minor.yy162 = yylhsminor.yy162;
        break;
      case 158: /* expr ::= LP nexprlist COMMA expr RP */
#line 976 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy382, yymsp[-1].minor.yy162.pExpr);
  yylhsminor.yy162.pExpr = sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
  if( yylhsminor.yy162.pExpr ){
    yylhsminor.yy162.pExpr->x.pList = pList;
    spanSet(&yylhsminor.yy162, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  }
}
#line 3135 "parse.c"
  yymsp[-4].minor.yy162 = yylhsminor.yy162;
        break;
      case 159: /* expr ::= expr AND expr */
      case 160: /* expr ::= expr OR expr */ yytestcase(yyruleno==160);
      case 161: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==161);
      case 162: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==162);
      case 163: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==163);
      case 164: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==164);
      case 165: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==165);
      case 166: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==166);
#line 987 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy162,&yymsp[0].minor.yy162);}
#line 3148 "parse.c"
        break;
      case 167: /* likeop ::= LIKE_KW|MATCH */
#line 1000 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3153 "parse.c"
        break;
      case 168: /* likeop ::= NOT LIKE_KW|MATCH */
#line 1001 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3158 "parse.c"
        break;
      case 169: /* expr ::= expr likeop expr */
#line 1002 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
  yymsp[-1].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy162.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy162.pExpr);
  yymsp[-2].minor.yy162.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-2].minor.yy162);
  yymsp[-2].minor.yy162.zEnd = yymsp[0].minor.yy162.zEnd;
  if( yymsp[-2].minor.yy162.pExpr ) yymsp[-2].minor.yy162.pExpr->flags |= EP_InfixFunc;
}
#line 3173 "parse.c"
        break;
      case 170: /* expr ::= expr likeop expr ESCAPE expr */
#line 1013 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
  yymsp[-3].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy162.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy162.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy162.pExpr);
  yymsp[-4].minor.yy162.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-4].minor.yy162);
  yymsp[-4].minor.yy162.zEnd = yymsp[0].minor.yy162.zEnd;
  if( yymsp[-4].minor.yy162.pExpr ) yymsp[-4].minor.yy162.pExpr->flags |= EP_InfixFunc;
}
#line 3189 "parse.c"
        break;
      case 171: /* expr ::= expr ISNULL|NOTNULL */
#line 1040 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy162,&yymsp[0].minor.yy0);}
#line 3194 "parse.c"
        break;
      case 172: /* expr ::= expr NOT NULL */
#line 1041 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy162,&yymsp[0].minor.yy0);}
#line 3199 "parse.c"
        break;
      case 173: /* expr ::= expr IS expr */
#line 1062 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy162,&yymsp[0].minor.yy162);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy162.pExpr, yymsp[-2].minor.yy162.pExpr, TK_ISNULL);
}
#line 3207 "parse.c"
        break;
      case 174: /* expr ::= expr IS NOT expr */
#line 1066 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy162,&yymsp[0].minor.yy162);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy162.pExpr, yymsp[-3].minor.yy162.pExpr, TK_NOTNULL);
}
#line 3215 "parse.c"
        break;
      case 175: /* expr ::= NOT expr */
      case 176: /* expr ::= BITNOT expr */ yytestcase(yyruleno==176);
#line 1090 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy162,pParse,yymsp[-1].major,&yymsp[0].minor.yy162,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3221 "parse.c"
        break;
      case 177: /* expr ::= MINUS expr */
#line 1094 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy162,pParse,TK_UMINUS,&yymsp[0].minor.yy162,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3226 "parse.c"
        break;
      case 178: /* expr ::= PLUS expr */
#line 1096 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy162,pParse,TK_UPLUS,&yymsp[0].minor.yy162,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3231 "parse.c"
        break;
      case 179: /* between_op ::= BETWEEN */
      case 182: /* in_op ::= IN */ yytestcase(yyruleno==182);
#line 1099 "parse.y"
{yymsp[0].minor.yy52 = 0;}
#line 3237 "parse.c"
        break;
      case 181: /* expr ::= expr between_op expr AND expr */
#line 1101 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy162.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy162.pExpr);
  yymsp[-4].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy162.pExpr, 0);
  if( yymsp[-4].minor.yy162.pExpr ){
    yymsp[-4].minor.yy162.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, yymsp[-3].minor.yy52, &yymsp[-4].minor.yy162);
  yymsp[-4].minor.yy162.zEnd = yymsp[0].minor.yy162.zEnd;
}
#line 3253 "parse.c"
        break;
      case 184: /* expr ::= expr in_op LP exprlist RP */
#line 1117 "parse.y"
{
    if( yymsp[-1].minor.yy382==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
	    sql_expr_free(pParse->db, yymsp[-4].minor.yy162.pExpr, false);
      yymsp[-4].minor.yy162.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER,&sqlite3IntTokens[yymsp[-3].minor.yy52],1);
    }else if( yymsp[-1].minor.yy382->nExpr==1 ){
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
      Expr *pRHS = yymsp[-1].minor.yy382->a[0].pExpr;
      yymsp[-1].minor.yy382->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy382);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      yymsp[-4].minor.yy162.pExpr = sqlite3PExpr(pParse, yymsp[-3].minor.yy52 ? TK_NE : TK_EQ, yymsp[-4].minor.yy162.pExpr, pRHS);
    }else{
      yymsp[-4].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy162.pExpr, 0);
      if( yymsp[-4].minor.yy162.pExpr ){
        yymsp[-4].minor.yy162.pExpr->x.pList = yymsp[-1].minor.yy382;
        sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy162.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy382);
      }
      exprNot(pParse, yymsp[-3].minor.yy52, &yymsp[-4].minor.yy162);
    }
    yymsp[-4].minor.yy162.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3308 "parse.c"
        break;
      case 185: /* expr ::= LP select RP */
#line 1168 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy162,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy162.pExpr, yymsp[-1].minor.yy279);
  }
#line 3317 "parse.c"
        break;
      case 186: /* expr ::= expr in_op LP select RP */
#line 1173 "parse.y"
{
    yymsp[-4].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy162.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy162.pExpr, yymsp[-1].minor.yy279);
    exprNot(pParse, yymsp[-3].minor.yy52, &yymsp[-4].minor.yy162);
    yymsp[-4].minor.yy162.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3327 "parse.c"
        break;
      case 187: /* expr ::= expr in_op nm paren_exprlist */
#line 1179 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy382 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy382);
    yymsp[-3].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy162.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-3].minor.yy162.pExpr, pSelect);
    exprNot(pParse, yymsp[-2].minor.yy52, &yymsp[-3].minor.yy162);
    yymsp[-3].minor.yy162.zEnd = &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
#line 3340 "parse.c"
        break;
      case 188: /* expr ::= EXISTS LP select RP */
#line 1188 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy162,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy279);
  }
#line 3350 "parse.c"
        break;
      case 189: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1197 "parse.y"
{
  spanSet(&yymsp[-4].minor.yy162,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-C*/
  yymsp[-4].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy362, 0);
  if( yymsp[-4].minor.yy162.pExpr ){
    yymsp[-4].minor.yy162.pExpr->x.pList = yymsp[-1].minor.yy362 ? sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy382,yymsp[-1].minor.yy362) : yymsp[-2].minor.yy382;
    sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy162.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy382);
    sql_expr_free(pParse->db, yymsp[-1].minor.yy362, false);
  }
}
#line 3365 "parse.c"
        break;
      case 190: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1210 "parse.y"
{
  yymsp[-4].minor.yy382 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy382, yymsp[-2].minor.yy162.pExpr);
  yymsp[-4].minor.yy382 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy382, yymsp[0].minor.yy162.pExpr);
}
#line 3373 "parse.c"
        break;
      case 191: /* case_exprlist ::= WHEN expr THEN expr */
#line 1214 "parse.y"
{
  yymsp[-3].minor.yy382 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy162.pExpr);
  yymsp[-3].minor.yy382 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy382, yymsp[0].minor.yy162.pExpr);
}
#line 3381 "parse.c"
        break;
      case 194: /* case_operand ::= expr */
#line 1224 "parse.y"
{yymsp[0].minor.yy362 = yymsp[0].minor.yy162.pExpr; /*A-overwrites-X*/}
#line 3386 "parse.c"
        break;
      case 197: /* nexprlist ::= nexprlist COMMA expr */
#line 1235 "parse.y"
{yymsp[-2].minor.yy382 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy382,yymsp[0].minor.yy162.pExpr);}
#line 3391 "parse.c"
        break;
      case 198: /* nexprlist ::= expr */
#line 1237 "parse.y"
{yymsp[0].minor.yy382 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy162.pExpr); /*A-overwrites-Y*/}
#line 3396 "parse.c"
        break;
      case 200: /* paren_exprlist ::= LP exprlist RP */
      case 205: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==205);
#line 1245 "parse.y"
{yymsp[-2].minor.yy382 = yymsp[-1].minor.yy382;}
#line 3402 "parse.c"
        break;
      case 201: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1252 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0), yymsp[-2].minor.yy382, yymsp[-9].minor.yy52,
                      &yymsp[-10].minor.yy0, yymsp[0].minor.yy362, SQLITE_SO_ASC, yymsp[-7].minor.yy52, SQLITE_IDXTYPE_APPDEF);
}
#line 3411 "parse.c"
        break;
      case 202: /* uniqueflag ::= UNIQUE */
      case 243: /* raisetype ::= ABORT */ yytestcase(yyruleno==243);
#line 1259 "parse.y"
{yymsp[0].minor.yy52 = ON_CONFLICT_ACTION_ABORT;}
#line 3417 "parse.c"
        break;
      case 203: /* uniqueflag ::= */
#line 1260 "parse.y"
{yymsp[1].minor.yy52 = ON_CONFLICT_ACTION_NONE;}
#line 3422 "parse.c"
        break;
      case 206: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1303 "parse.y"
{
  yymsp[-4].minor.yy382 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy382, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy52, yymsp[0].minor.yy52);
}
#line 3429 "parse.c"
        break;
      case 207: /* eidlist ::= nm collate sortorder */
#line 1306 "parse.y"
{
  yymsp[-2].minor.yy382 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy52, yymsp[0].minor.yy52); /*A-overwrites-Y*/
}
#line 3436 "parse.c"
        break;
      case 210: /* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1317 "parse.y"
{
    sqlite3DropIndex(pParse, yymsp[-2].minor.yy387, &yymsp[0].minor.yy0, yymsp[-3].minor.yy52);
}
#line 3443 "parse.c"
        break;
      case 211: /* cmd ::= PRAGMA nm */
#line 1324 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[0].minor.yy0,0,0,0);
}
#line 3450 "parse.c"
        break;
      case 212: /* cmd ::= PRAGMA nm EQ nmnum */
#line 1327 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0,0,0);
}
#line 3457 "parse.c"
        break;
      case 213: /* cmd ::= PRAGMA nm LP nmnum RP */
#line 1330 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,&yymsp[-1].minor.yy0,0,0);
}
#line 3464 "parse.c"
        break;
      case 214: /* cmd ::= PRAGMA nm EQ minus_num */
#line 1333 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0,0,1);
}
#line 3471 "parse.c"
        break;
      case 215: /* cmd ::= PRAGMA nm LP minus_num RP */
#line 1336 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,&yymsp[-1].minor.yy0,0,1);
}
#line 3478 "parse.c"
        break;
      case 216: /* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1339 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0,&yymsp[-2].minor.yy0,0);
}
#line 3485 "parse.c"
        break;
      case 217: /* cmd ::= PRAGMA */
#line 1342 "parse.y"
{
    sqlite3Pragma(pParse, 0,0,0,0);
}
#line 3492 "parse.c"
        break;
      case 220: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1362 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  pParse->initiateTTrans = false;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy427, &all);
}
#line 3503 "parse.c"
        break;
      case 221: /* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1372 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy52, yymsp[-4].minor.yy10.a, yymsp[-4].minor.yy10.b, yymsp[-2].minor.yy387, yymsp[0].minor.yy362, yymsp[-7].minor.yy52);
  yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0; /*yymsp[-8].minor.yy0-overwrites-T*/
}
#line 3511 "parse.c"
        break;
      case 222: /* trigger_time ::= BEFORE */
#line 1378 "parse.y"
{ yymsp[0].minor.yy52 = TK_BEFORE; }
#line 3516 "parse.c"
        break;
      case 223: /* trigger_time ::= AFTER */
#line 1379 "parse.y"
{ yymsp[0].minor.yy52 = TK_AFTER;  }
#line 3521 "parse.c"
        break;
      case 224: /* trigger_time ::= INSTEAD OF */
#line 1380 "parse.y"
{ yymsp[-1].minor.yy52 = TK_INSTEAD;}
#line 3526 "parse.c"
        break;
      case 225: /* trigger_time ::= */
#line 1381 "parse.y"
{ yymsp[1].minor.yy52 = TK_BEFORE; }
#line 3531 "parse.c"
        break;
      case 226: /* trigger_event ::= DELETE|INSERT */
      case 227: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==227);
#line 1385 "parse.y"
{yymsp[0].minor.yy10.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy10.b = 0;}
#line 3537 "parse.c"
        break;
      case 228: /* trigger_event ::= UPDATE OF idlist */
#line 1387 "parse.y"
{yymsp[-2].minor.yy10.a = TK_UPDATE; yymsp[-2].minor.yy10.b = yymsp[0].minor.yy40;}
#line 3542 "parse.c"
        break;
      case 229: /* when_clause ::= */
#line 1394 "parse.y"
{ yymsp[1].minor.yy362 = 0; }
#line 3547 "parse.c"
        break;
      case 230: /* when_clause ::= WHEN expr */
#line 1395 "parse.y"
{ yymsp[-1].minor.yy362 = yymsp[0].minor.yy162.pExpr; }
#line 3552 "parse.c"
        break;
      case 231: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1399 "parse.y"
{
  assert( yymsp[-2].minor.yy427!=0 );
  yymsp[-2].minor.yy427->pLast->pNext = yymsp[-1].minor.yy427;
  yymsp[-2].minor.yy427->pLast = yymsp[-1].minor.yy427;
}
#line 3561 "parse.c"
        break;
      case 232: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1404 "parse.y"
{ 
  assert( yymsp[-1].minor.yy427!=0 );
  yymsp[-1].minor.yy427->pLast = yymsp[-1].minor.yy427;
}
#line 3569 "parse.c"
        break;
      case 233: /* trnm ::= nm DOT nm */
#line 1415 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3579 "parse.c"
        break;
      case 234: /* tridxby ::= INDEXED BY nm */
#line 1427 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3588 "parse.c"
        break;
      case 235: /* tridxby ::= NOT INDEXED */
#line 1432 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3597 "parse.c"
        break;
      case 236: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1445 "parse.y"
{yymsp[-6].minor.yy427 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy382, yymsp[0].minor.yy362, yymsp[-5].minor.yy52);}
#line 3602 "parse.c"
        break;
      case 237: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1449 "parse.y"
{yymsp[-4].minor.yy427 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy40, yymsp[0].minor.yy279, yymsp[-4].minor.yy52);/*A-overwrites-R*/}
#line 3607 "parse.c"
        break;
      case 238: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1453 "parse.y"
{yymsp[-4].minor.yy427 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy362);}
#line 3612 "parse.c"
        break;
      case 239: /* trigger_cmd ::= select */
#line 1457 "parse.y"
{yymsp[0].minor.yy427 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy279); /*A-overwrites-X*/}
#line 3617 "parse.c"
        break;
      case 240: /* expr ::= RAISE LP IGNORE RP */
#line 1460 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy162,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy162.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy162.pExpr ){
    yymsp[-3].minor.yy162.pExpr->affinity = ON_CONFLICT_ACTION_IGNORE;
  }
}
#line 3628 "parse.c"
        break;
      case 241: /* expr ::= RAISE LP raisetype COMMA STRING RP */
#line 1467 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy162,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy162.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy162.pExpr ) {
    yymsp[-5].minor.yy162.pExpr->affinity = (char)yymsp[-3].minor.yy52;
  }
}
#line 3639 "parse.c"
        break;
      case 242: /* raisetype ::= ROLLBACK */
#line 1477 "parse.y"
{yymsp[0].minor.yy52 = ON_CONFLICT_ACTION_ROLLBACK;}
#line 3644 "parse.c"
        break;
      case 244: /* raisetype ::= FAIL */
#line 1479 "parse.y"
{yymsp[0].minor.yy52 = ON_CONFLICT_ACTION_FAIL;}
#line 3649 "parse.c"
        break;
      case 245: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1484 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy387,yymsp[-1].minor.yy52);
}
#line 3656 "parse.c"
        break;
      case 246: /* cmd ::= ANALYZE */
#line 1499 "parse.y"
{sqlite3Analyze(pParse, 0);}
#line 3661 "parse.c"
        break;
      case 247: /* cmd ::= ANALYZE nm */
#line 1500 "parse.y"
{sqlite3Analyze(pParse, &yymsp[0].minor.yy0);}
#line 3666 "parse.c"
        break;
      case 248: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1505 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy387,&yymsp[0].minor.yy0);
}
#line 3673 "parse.c"
        break;
      case 249: /* with ::= */
#line 1528 "parse.y"
{yymsp[1].minor.yy151 = 0;}
#line 3678 "parse.c"
        break;
      case 250: /* with ::= WITH wqlist */
#line 1530 "parse.y"
{ yymsp[-1].minor.yy151 = yymsp[0].minor.yy151; }
#line 3683 "parse.c"
        break;
      case 251: /* with ::= WITH RECURSIVE wqlist */
#line 1531 "parse.y"
{ yymsp[-2].minor.yy151 = yymsp[0].minor.yy151; }
#line 3688 "parse.c"
        break;
      case 252: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1533 "parse.y"
{
  yymsp[-5].minor.yy151 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy382, yymsp[-1].minor.yy279); /*A-overwrites-X*/
}
#line 3695 "parse.c"
        break;
      case 253: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1536 "parse.y"
{
  yymsp[-7].minor.yy151 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy151, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy382, yymsp[-1].minor.yy279);
}
#line 3702 "parse.c"
        break;
      default:
      /* (254) input ::= ecmd */ yytestcase(yyruleno==254);
      /* (255) explain ::= */ yytestcase(yyruleno==255);
      /* (256) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno!=256);
      /* (257) trans_opt ::= */ yytestcase(yyruleno==257);
      /* (258) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==258);
      /* (259) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==259);
      /* (260) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==260);
      /* (261) savepoint_opt ::= */ yytestcase(yyruleno==261);
      /* (262) cmd ::= create_table create_table_args */ yytestcase(yyruleno==262);
      /* (263) columnlist ::= columnlist COMMA columnname carglist */ yytestcase(yyruleno==263);
      /* (264) columnlist ::= columnname carglist */ yytestcase(yyruleno==264);
      /* (265) typetoken ::= typename */ yytestcase(yyruleno==265);
      /* (266) typename ::= ID|STRING */ yytestcase(yyruleno==266);
      /* (267) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=267);
      /* (268) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=268);
      /* (269) carglist ::= carglist ccons */ yytestcase(yyruleno==269);
      /* (270) carglist ::= */ yytestcase(yyruleno==270);
      /* (271) ccons ::= NULL onconf */ yytestcase(yyruleno==271);
      /* (272) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==272);
      /* (273) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==273);
      /* (274) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=274);
      /* (275) tconscomma ::= */ yytestcase(yyruleno==275);
      /* (276) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=276);
      /* (277) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=277);
      /* (278) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=278);
      /* (279) oneselect ::= values */ yytestcase(yyruleno==279);
      /* (280) sclp ::= selcollist COMMA */ yytestcase(yyruleno==280);
      /* (281) as ::= ID|STRING */ yytestcase(yyruleno==281);
      /* (282) join_nm ::= ID|INDEXED */ yytestcase(yyruleno==282);
      /* (283) join_nm ::= JOIN_KW */ yytestcase(yyruleno==283);
      /* (284) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=284);
      /* (285) exprlist ::= nexprlist */ yytestcase(yyruleno==285);
      /* (286) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=286);
      /* (287) nmnum ::= STRING */ yytestcase(yyruleno==287);
      /* (288) nmnum ::= nm */ yytestcase(yyruleno==288);
      /* (289) nmnum ::= ON */ yytestcase(yyruleno==289);
      /* (290) nmnum ::= DELETE */ yytestcase(yyruleno==290);
      /* (291) nmnum ::= DEFAULT */ yytestcase(yyruleno==291);
      /* (292) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno==292);
      /* (293) foreach_clause ::= */ yytestcase(yyruleno==293);
      /* (294) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==294);
      /* (295) trnm ::= nm */ yytestcase(yyruleno==295);
      /* (296) tridxby ::= */ yytestcase(yyruleno==296);
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
#line 3813 "parse.c"
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
