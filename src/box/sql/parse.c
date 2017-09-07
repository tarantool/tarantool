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

#line 399 "parse.y"

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
#line 836 "parse.y"

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
#line 953 "parse.y"

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
#line 1027 "parse.y"

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
#line 1044 "parse.y"

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
#line 1072 "parse.y"

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
#line 1284 "parse.y"

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
#define YYNOCODE 245
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 94
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  struct TrigEvent yy114;
  Select* yy171;
  IdList* yy224;
  TriggerStep* yy251;
  Expr* yy258;
  int yy284;
  ExprList* yy298;
  With* yy323;
  SrcList* yy339;
  struct {int value; int mask;} yy431;
  ExprSpan yy438;
  struct LimitVal yy452;
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
#define YYNRULE              320
#define YY_MAX_SHIFT         437
#define YY_MIN_SHIFTREDUCE   644
#define YY_MAX_SHIFTREDUCE   963
#define YY_MIN_REDUCE        964
#define YY_MAX_REDUCE        1283
#define YY_ERROR_ACTION      1284
#define YY_ACCEPT_ACTION     1285
#define YY_NO_ACTION         1286
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
#define YY_ACTTAB_COUNT (1472)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    91,   92,  304,   82,  815,  815,  827,  830,  819,  819,
 /*    10 */    89,   89,   90,   90,   90,   90,  329,   88,   88,   88,
 /*    20 */    88,   87,   87,   86,   86,   86,   85,  329,   90,   90,
 /*    30 */    90,   90,   83,   88,   88,   88,   88,   87,   87,   86,
 /*    40 */    86,   86,   85,  329,  645,  332,  196,  924,  924,  943,
 /*    50 */   296,  962,   91,   92,  304,   82,  815,  815,  827,  830,
 /*    60 */   819,  819,   89,   89,   90,   90,   90,   90,  124,   88,
 /*    70 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  329,
 /*    80 */    87,   87,   86,   86,   86,   85,  329,  786,  925,  926,
 /*    90 */   943,   90,   90,   90,   90,  963,   88,   88,   88,   88,
 /*   100 */    87,   87,   86,   86,   86,   85,  329,  764,  148,  251,
 /*   110 */   345,  238,  366,  805,  289,  798,  765,  673,  300,  792,
 /*   120 */   234,  393,   91,   92,  304,   82,  815,  815,  827,  830,
 /*   130 */   819,  819,   89,   89,   90,   90,   90,   90,  676,   88,
 /*   140 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  329,
 /*   150 */   797,  797,  799,  675,   91,   92,  304,   82,  815,  815,
 /*   160 */   827,  830,  819,  819,   89,   89,   90,   90,   90,   90,
 /*   170 */    67,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   180 */    85,  329,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   190 */    86,   85,  329,   84,   81,  169,  222,   93,  280,  258,
 /*   200 */  1285,  437,    2,   91,   92,  304,   82,  815,  815,  827,
 /*   210 */   830,  819,  819,   89,   89,   90,   90,   90,   90,  928,
 /*   220 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   230 */   329,  316,   91,   92,  304,   82,  815,  815,  827,  830,
 /*   240 */   819,  819,   89,   89,   90,   90,   90,   90,  681,   88,
 /*   250 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  329,
 /*   260 */   928,   91,   92,  304,   82,  815,  815,  827,  830,  819,
 /*   270 */   819,   89,   89,   90,   90,   90,   90,  674,   88,   88,
 /*   280 */    88,   88,   87,   87,   86,   86,   86,   85,  329,  362,
 /*   290 */    91,   92,  304,   82,  815,  815,  827,  830,  819,  819,
 /*   300 */    89,   89,   90,   90,   90,   90,  806,   88,   88,   88,
 /*   310 */    88,   87,   87,   86,   86,   86,   85,  329,  935,   91,
 /*   320 */    92,  304,   82,  815,  815,  827,  830,  819,  819,   89,
 /*   330 */    89,   90,   90,   90,   90,  791,   88,   88,   88,   88,
 /*   340 */    87,   87,   86,   86,   86,   85,  329,   91,   92,  304,
 /*   350 */    82,  815,  815,  827,  830,  819,  819,   89,   89,   90,
 /*   360 */    90,   90,   90,  430,   88,   88,   88,   88,   87,   87,
 /*   370 */    86,   86,   86,   85,  329,   91,   92,  304,   82,  815,
 /*   380 */   815,  827,  830,  819,  819,   89,   89,   90,   90,   90,
 /*   390 */    90,  197,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   400 */    86,   85,  329,   85,  329,   91,   92,  304,   82,  815,
 /*   410 */   815,  827,  830,  819,  819,   89,   89,   90,   90,   90,
 /*   420 */    90,  149,   88,   88,   88,   88,   87,   87,   86,   86,
 /*   430 */    86,   85,  329,  403,   91,   80,  304,   82,  815,  815,
 /*   440 */   827,  830,  819,  819,   89,   89,   90,   90,   90,   90,
 /*   450 */    70,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   460 */    85,  329,  167,   92,  304,   82,  815,  815,  827,  830,
 /*   470 */   819,  819,   89,   89,   90,   90,   90,   90,   73,   88,
 /*   480 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  329,
 /*   490 */   304,   82,  815,  815,  827,  830,  819,  819,   89,   89,
 /*   500 */    90,   90,   90,   90,   78,   88,   88,   88,   88,   87,
 /*   510 */    87,   86,   86,   86,   85,  329,   78,  867,  148,  436,
 /*   520 */   436,  296,  906,   75,   76,  235,  196,  122,  299,  943,
 /*   530 */    77,  786,  722,  256,  748,   75,   76,  124,  216,  216,
 /*   540 */   327,  326,   77,  234,  393,  426,    3, 1164,  277,  398,
 /*   550 */   396,  330,  330,  251,  357,  250,  340,  426,    3,  924,
 /*   560 */   924,  786,  429,  330,  330,   22,  963,  124,  924,  924,
 /*   570 */   943,  340,  339,  336,  429,  247,  255,  349,  392,  415,
 /*   580 */   727,  727,  111,  251,  357,  250,  898,  124,  145,  850,
 /*   590 */   805,  415,  433,  432,  764,  431,  792,  193,  166,  124,
 /*   600 */   925,  926,  805,  765,  433,  432,  714,  714,  792,  925,
 /*   610 */   926,  333,  950,   48,   48,  431,  233,  924,  924,  948,
 /*   620 */   334,  949,  360,  360,  410,  735,   54,  797,  797,  799,
 /*   630 */   800,   18,  340,   48,   48,   78,   84,   81,  169,  797,
 /*   640 */   797,  799,  800,   18,  951,  790,  951,   78,  409,  394,
 /*   650 */   321,  650,  651,  652,   75,   76,  736,  111,  925,  926,
 /*   660 */   187,   77,  431,  383,  380,  379,   75,   76,  409,  411,
 /*   670 */   931,  390,  897,   77,  378,  318,  426,    3,  683,  140,
 /*   680 */    30,   30,  330,  330,  924,  924,  219,  223,  426,    3,
 /*   690 */   124,  218,  895,  429,  330,  330,  294,  293,  292,  207,
 /*   700 */   290,  431,  338,  660,  111,  429,  124,  243,  351,  371,
 /*   710 */   415,  431,  368,  431,  342,  391,  171, 1228, 1228,   48,
 /*   720 */    48,  805,  415,  433,  432,  925,  926,  792,  748,   48,
 /*   730 */    48,   47,   47,  805,  163,  433,  432,  864,  175,  792,
 /*   740 */   204,  160,  268,  386,  263,  385,  192,  124,  173,  240,
 /*   750 */   242,  108,  737,  261,  409,  399,  671,  311,  797,  797,
 /*   760 */   799,  800,   18,  431,  409,  408,   68,  237,  889,  305,
 /*   770 */   797,  797,  799,  800,   18,  162,  161,  886,  924,  924,
 /*   780 */   858,   48,   48,  890,  278,   75,   76,  431,  141,  684,
 /*   790 */   216,  216,   77,  924,  924,  903,  337,  671,   95,  891,
 /*   800 */   241,  715,  396,  677,  677,   48,   48,  426,    3,   64,
 /*   810 */   111,  716,  790,  330,  330,  343,  409,  389,  187,  925,
 /*   820 */   926,  383,  380,  379,  429,   86,   86,   86,   85,  329,
 /*   830 */   684,   24,  378,  358,  925,  926,  924,  924,  763,  279,
 /*   840 */   322,  415,  805,  886,  798,  375,   23,  191,  792,  327,
 /*   850 */   326,  356,  805,  431,  433,  432,  924,  924,  792,  924,
 /*   860 */   924,    1,  816,  816,  828,  831,  924,  924,  762,  397,
 /*   870 */   692,   48,   48,  709,  688,  312,  111,  925,  926,  797,
 /*   880 */   797,  799,  709,  348,   84,   81,  169,    9,    9,  797,
 /*   890 */   797,  799,  800,   18,  431,  712,  712,  925,  926,  358,
 /*   900 */   925,  926,  431,  341,  431,  211,  404,  925,  926,  109,
 /*   910 */   431,  153,   10,   10,   84,   81,  169,  367,  860,  862,
 /*   920 */    10,   10,   10,   10,  751,  148,  315,  750,   10,   10,
 /*   930 */   881,  783,  431,  424,  317,  303,  225,  428,  428,  428,
 /*   940 */   907, 1278,  406,  308, 1278,   74,  695,   72,  261,  880,
 /*   950 */    34,   34,  820,  951,  226,  951,  226,  907, 1279,  384,
 /*   960 */     5, 1279,  196,  213,  111,  943,  749,  696,  889,  431,
 /*   970 */   749,  431,  960,  297,  431,  280,  431,  790,  431,  860,
 /*   980 */   328,  328,  328,  890,  369,  905,  431,   35,   35,   36,
 /*   990 */    36,  319,   37,   37,   38,   38,   26,   26,  431,  891,
 /*  1000 */   431,  414,  905,  431,   27,   27,  943,  431,  325,  168,
 /*  1010 */   431,  748,  431,  168,  431,  732,   29,   29,   39,   39,
 /*  1020 */   731,   40,   40,  388,  370,   41,   41,  431,   11,   11,
 /*  1030 */    42,   42,   97,   97,  431,  749,  431,  400,  431,  749,
 /*  1040 */   313,  431,  279,  431,  748,   43,   43,  431,  748,  431,
 /*  1050 */   220,  431,   44,   44,   31,   31,   45,   45,  431,   46,
 /*  1060 */    46,   32,   32,  431,  366,  113,  113,  114,  114,  115,
 /*  1070 */   115,  431,  748,  431,  366,  431,   52,   52,  431,  667,
 /*  1080 */   309,   33,   33,  221,  431,  401,  431,  892,  790,   98,
 /*  1090 */    98,   49,   49,   99,   99,  870,  100,  100,  431,  869,
 /*  1100 */   431,  790,   96,   96,  112,  112,  431,  191,  431,  732,
 /*  1110 */   431,  346,  431,  350,  731,  431,  110,  110,  104,  104,
 /*  1120 */   748,  431,  748,  748,  103,  103,  101,  101,  102,  102,
 /*  1130 */    51,   51,  364,   53,   53,  431,  420,  431,  884,   50,
 /*  1140 */    50,  873,  873,  352,   66,  190,  189,  188,  249,  720,
 /*  1150 */   664,  314,  724,   25,   25,   28,   28,  417,  224,  721,
 /*  1160 */   421,  717,  246,  396,  323,  427,  309,  425,  310,  125,
 /*  1170 */   942,  111, 1254,  215,  166,  704,  902,   20,  788,  298,
 /*  1180 */   365,  195,  111,  111,  359,  361,  252,  195,  195,   66,
 /*  1190 */   111,  376,  857,  111,  200,  686,  259,  694,  693,   66,
 /*  1200 */   111,  701,  702,  308,  267,  729,  758,  801,   69,  195,
 /*  1210 */    19,  217,  866,  853,  866,  266,  200,  865,  669,  865,
 /*  1220 */   254,  107,  659,  257,  705,  691,  690,  689,  262,  742,
 /*  1230 */   756,  229,  789,  857,  738,  412,  355,  413,  158,  281,
 /*  1240 */   210,  282,  796,  672,  666,  657,  656,  270,  801,  658,
 /*  1250 */   918,  272,  274,  150,  295,    7,  778,  344,  248,  883,
 /*  1260 */   381,  239,  363,  164,  276,  416,  921,  287,  957,  159,
 /*  1270 */   127,  138,  147,  265,  855,  121,  854,  688,   64,  347,
 /*  1280 */   374,  178, 1244,   55,  775,  868,  354,  179,  245,  146,
 /*  1290 */   387,  183,  129,  372,  184,  151,  131,  132,  133,  134,
 /*  1300 */   142,  185,  785,  301,  320,  686,  699,  698,  708,  885,
 /*  1310 */   402,  849,  707,  706,  302,   63,  679,    6,  680,   71,
 /*  1320 */   264,  678,  324,  933,   94,   65,  205,  407,   21,  405,
 /*  1330 */   746,  747,  434,  269,  271,  663,  919,  209,  419,  206,
 /*  1340 */   208,  435,  423,  745,  654,  647,  653,  331,  648,  157,
 /*  1350 */   170,  116,  117,  335,  106,  227,  236,  105,  172,  863,
 /*  1360 */   861,  174,  784,  128,  119,  118,  130,  273,  286,  744,
 /*  1370 */   275,  728,  283,  284,  285,  718,  176,  177,  835,  244,
 /*  1380 */   871,  195,  135,  953,  353,  230,  136,  879,  137,  139,
 /*  1390 */    56,  231,   57,  232,   58,   59,  120,  882,  180,  181,
 /*  1400 */   878,    8,   12,  182,  253,  152,  662,  373,  186,  266,
 /*  1410 */   143,  377,   60,   13,  382,  260,  306,   14,   61,  126,
 /*  1420 */   804,  307,  123,  697,  228,  803,   62,  833,  726,   15,
 /*  1430 */   752,  730,    4,  644,  212,  395,  165,  214,  144,  202,
 /*  1440 */   757,  203, 1246,   69,  848,   66,  834,  832,  888,  837,
 /*  1450 */   199,  194, 1245,  887,   16,  198,   17,  911,  201,  154,
 /*  1460 */  1233,  155,  418,  912,  156,  422,  836,  802,  670,   79,
 /*  1470 */   291,  288,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*    10 */    15,   16,   17,   18,   19,   20,   32,   22,   23,   24,
 /*    20 */    25,   26,   27,   28,   29,   30,   31,   32,   17,   18,
 /*    30 */    19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
 /*    40 */    29,   30,   31,   32,    1,    2,   51,   54,   55,   54,
 /*    50 */    49,   50,    5,    6,    7,    8,    9,   10,   11,   12,
 /*    60 */    13,   14,   15,   16,   17,   18,   19,   20,   90,   22,
 /*    70 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*    80 */    26,   27,   28,   29,   30,   31,   32,   84,   95,   96,
 /*    90 */    95,   17,   18,   19,   20,   94,   22,   23,   24,   25,
 /*   100 */    26,   27,   28,   29,   30,   31,   32,   60,  149,  106,
 /*   110 */   107,  108,  149,   93,  155,   95,   69,  167,  159,   99,
 /*   120 */   117,  118,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   130 */    13,   14,   15,   16,   17,   18,   19,   20,  167,   22,
 /*   140 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   150 */   130,  131,  132,  167,    5,    6,    7,    8,    9,   10,
 /*   160 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   170 */    53,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   180 */    31,   32,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   190 */    30,   31,   32,  216,  217,  218,  233,   80,  149,   50,
 /*   200 */   142,  143,  144,    5,    6,    7,    8,    9,   10,   11,
 /*   210 */    12,   13,   14,   15,   16,   17,   18,   19,   20,   54,
 /*   220 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   230 */    32,  182,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   240 */    13,   14,   15,   16,   17,   18,   19,   20,   50,   22,
 /*   250 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   260 */    95,    5,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   270 */    14,   15,   16,   17,   18,   19,   20,   50,   22,   23,
 /*   280 */    24,   25,   26,   27,   28,   29,   30,   31,   32,  149,
 /*   290 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*   300 */    15,   16,   17,   18,   19,   20,   50,   22,   23,   24,
 /*   310 */    25,   26,   27,   28,   29,   30,   31,   32,  180,    5,
 /*   320 */     6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
 /*   330 */    16,   17,   18,   19,   20,   50,   22,   23,   24,   25,
 /*   340 */    26,   27,   28,   29,   30,   31,   32,    5,    6,    7,
 /*   350 */     8,    9,   10,   11,   12,   13,   14,   15,   16,   17,
 /*   360 */    18,   19,   20,  149,   22,   23,   24,   25,   26,   27,
 /*   370 */    28,   29,   30,   31,   32,    5,    6,    7,    8,    9,
 /*   380 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   390 */    20,  149,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   400 */    30,   31,   32,   31,   32,    5,    6,    7,    8,    9,
 /*   410 */    10,   11,   12,   13,   14,   15,   16,   17,   18,   19,
 /*   420 */    20,   51,   22,   23,   24,   25,   26,   27,   28,   29,
 /*   430 */    30,   31,   32,  149,    5,    6,    7,    8,    9,   10,
 /*   440 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   450 */   136,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   460 */    31,   32,  149,    6,    7,    8,    9,   10,   11,   12,
 /*   470 */    13,   14,   15,   16,   17,   18,   19,   20,  136,   22,
 /*   480 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   490 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   500 */    17,   18,   19,   20,    7,   22,   23,   24,   25,   26,
 /*   510 */    27,   28,   29,   30,   31,   32,    7,   40,  149,  145,
 /*   520 */   146,   49,   50,   26,   27,  151,   51,  153,  159,   54,
 /*   530 */    33,   84,  158,   45,  149,   26,   27,   90,  189,  190,
 /*   540 */    26,   27,   33,  117,  118,   48,   49,   50,  149,  158,
 /*   550 */   201,   54,   55,  106,  107,  108,  149,   48,   49,   54,
 /*   560 */    55,   84,   65,   54,   55,  191,   94,   90,   54,   55,
 /*   570 */    95,  164,  165,  188,   65,   87,   88,   89,  113,   82,
 /*   580 */   115,  116,  191,  106,  107,  108,  149,   90,   83,  101,
 /*   590 */    93,   82,   95,   96,   60,  149,   99,  206,  207,   90,
 /*   600 */    95,   96,   93,   69,   95,   96,  185,  186,   99,   95,
 /*   610 */    96,  237,   98,  167,  168,  149,  194,   54,   55,  105,
 /*   620 */   235,  107,  149,  149,  158,  205,  204,  130,  131,  132,
 /*   630 */   133,  134,  225,  167,  168,    7,  216,  217,  218,  130,
 /*   640 */   131,  132,  133,  134,  130,  149,  132,    7,  202,  203,
 /*   650 */     7,   36,   37,   38,   26,   27,   28,  191,   95,   96,
 /*   660 */    97,   33,  149,  100,  101,  102,   26,   27,  202,  203,
 /*   670 */   166,  158,  149,   33,  111,   32,   48,   49,  174,   49,
 /*   680 */   167,  168,   54,   55,   54,   55,  213,  213,   48,   49,
 /*   690 */    90,   34,  149,   65,   54,   55,   39,   40,   41,   42,
 /*   700 */    43,  149,  149,   46,  191,   65,   90,   45,  212,  223,
 /*   710 */    82,  149,  226,  149,   98,  202,   59,  117,  118,  167,
 /*   720 */   168,   93,   82,   95,   96,   95,   96,   99,  149,  167,
 /*   730 */   168,  167,  168,   93,   53,   95,   96,  149,   81,   99,
 /*   740 */    97,   98,   99,  100,  101,  102,  103,   90,   91,   87,
 /*   750 */    88,   49,   28,  110,  202,  203,   54,  149,  130,  131,
 /*   760 */   132,  133,  134,  149,  202,  203,    7,  188,   41,  112,
 /*   770 */   130,  131,  132,  133,  134,   26,   27,  158,   54,   55,
 /*   780 */   149,  167,  168,   56,  220,   26,   27,  149,   49,   54,
 /*   790 */   189,  190,   33,   54,   55,  149,  139,   95,   49,   72,
 /*   800 */   138,   74,  201,   54,   55,  167,  168,   48,   49,  128,
 /*   810 */   191,   84,  149,   54,   55,  214,  202,  203,   97,   95,
 /*   820 */    96,  100,  101,  102,   65,   28,   29,   30,   31,   32,
 /*   830 */    95,   49,  111,  214,   95,   96,   54,   55,  170,  149,
 /*   840 */   202,   82,   93,  158,   95,    7,  227,    9,   99,   26,
 /*   850 */    27,  232,   93,  149,   95,   96,   54,   55,   99,   54,
 /*   860 */    55,   49,    9,   10,   11,   12,   54,   55,  170,  149,
 /*   870 */   176,  167,  168,  174,  175,  212,  191,   95,   96,  130,
 /*   880 */   131,  132,  183,  149,  216,  217,  218,  167,  168,  130,
 /*   890 */   131,  132,  133,  134,  149,  185,  186,   95,   96,  214,
 /*   900 */    95,   96,  149,  149,  149,  149,  202,   95,   96,   49,
 /*   910 */   149,   51,  167,  168,  216,  217,  218,  232,  164,  165,
 /*   920 */   167,  168,  167,  168,  122,  149,  181,  122,  167,  168,
 /*   930 */   149,  158,  149,  243,  181,  159,  181,  163,  164,  165,
 /*   940 */    49,   50,  181,  105,   53,  135,   64,  137,  110,  149,
 /*   950 */   167,  168,   99,  130,  178,  132,  180,   49,   50,   77,
 /*   960 */    49,   53,   51,   50,  191,   54,   53,   85,   41,  149,
 /*   970 */    53,  149,  240,  241,  149,  149,  149,  149,  149,  225,
 /*   980 */   163,  164,  165,   56,  149,   94,  149,  167,  168,  167,
 /*   990 */   168,  109,  167,  168,  167,  168,  167,  168,  149,   72,
 /*  1000 */   149,   74,   94,  149,  167,  168,   95,  149,  182,   96,
 /*  1010 */   149,  149,  149,   96,  149,  114,  167,  168,  167,  168,
 /*  1020 */   119,  167,  168,   28,  149,  167,  168,  149,  167,  168,
 /*  1030 */   167,  168,  167,  168,  149,  122,  149,    7,  149,  122,
 /*  1040 */   212,  149,  149,  149,  149,  167,  168,  149,  149,  149,
 /*  1050 */   188,  149,  167,  168,  167,  168,  167,  168,  149,  167,
 /*  1060 */   168,  167,  168,  149,  149,  167,  168,  167,  168,  167,
 /*  1070 */   168,  149,  149,  149,  149,  149,  167,  168,  149,  161,
 /*  1080 */   162,  167,  168,  188,  149,   55,  149,  188,  149,  167,
 /*  1090 */   168,  167,  168,  167,  168,   58,  167,  168,  149,   62,
 /*  1100 */   149,  149,  167,  168,  167,  168,  149,    9,  149,  114,
 /*  1110 */   149,  188,  149,   76,  119,  149,  167,  168,  167,  168,
 /*  1120 */   149,  149,  149,  149,  167,  168,  167,  168,  167,  168,
 /*  1130 */   167,  168,    7,  167,  168,  149,  243,  149,  158,  167,
 /*  1140 */   168,  106,  107,  108,   53,  106,  107,  108,  233,  158,
 /*  1150 */   158,  212,  190,  167,  168,  167,  168,  158,  233,  188,
 /*  1160 */   158,  188,  188,  201,  212,  161,  162,  158,  238,  239,
 /*  1170 */    53,  191,   50,  206,  207,   53,   50,   16,   50,   53,
 /*  1180 */    55,   53,  191,  191,   50,   50,   50,   53,   53,   53,
 /*  1190 */   191,   50,   54,  191,   53,  104,   50,   98,   99,   53,
 /*  1200 */   191,   36,   37,  105,   99,   50,   50,   54,   53,   53,
 /*  1210 */    49,   49,  130,   50,  132,  110,   53,  130,   50,  132,
 /*  1220 */   149,   53,  149,  149,  149,  176,  176,  149,  149,  208,
 /*  1230 */   149,  205,  149,   95,  149,  149,  229,  186,  121,  149,
 /*  1240 */   228,  149,  149,  149,  149,  149,  149,  205,   95,  149,
 /*  1250 */   149,  205,  205,  192,  147,  193,  196,  209,  234,  196,
 /*  1260 */   171,  209,  234,  179,  209,  222,  152,  195,   66,  193,
 /*  1270 */   236,   49,  215,  170,  170,    5,  170,  175,  128,   47,
 /*  1280 */    47,  154,  120,  135,  196,  231,   73,  154,  230,   49,
 /*  1290 */   105,  154,  184,  172,  154,  215,  187,  187,  187,  187,
 /*  1300 */   184,  154,  184,  172,   75,  104,  177,  177,  169,  196,
 /*  1310 */   123,  196,  169,  169,  172,  105,  171,   49,  169,  135,
 /*  1320 */   169,  169,   32,  169,  127,  126,   52,  124,   53,  125,
 /*  1330 */   211,  211,  156,  210,  210,  157,   42,   35,  172,  150,
 /*  1340 */   150,  148,  172,  211,  148,    4,  148,    3,  148,   49,
 /*  1350 */    44,  160,  160,   92,  173,  173,  140,   45,  105,   50,
 /*  1360 */    50,  120,  118,  129,  109,  160,  121,  210,  196,  211,
 /*  1370 */   210,  200,  199,  198,  197,   48,  105,  123,  219,   45,
 /*  1380 */    79,   53,   79,   86,   71,  221,  105,    1,  121,  129,
 /*  1390 */    16,  224,   16,  224,   16,   16,  109,   55,   63,  120,
 /*  1400 */     1,   34,   49,  105,  138,   51,   48,    7,  103,  110,
 /*  1410 */    49,   78,   49,   49,   78,   50,  242,   49,   49,  239,
 /*  1420 */    50,  242,   67,   57,   78,   50,   53,   50,  114,   49,
 /*  1430 */   122,   50,   49,    1,   50,   53,  120,   50,   49,  120,
 /*  1440 */    55,  120,  120,   53,   50,   53,   50,   50,   50,   40,
 /*  1450 */    49,   63,  120,   50,   63,   53,   63,   50,   53,   49,
 /*  1460 */     0,   49,   51,   50,   49,   51,   50,   50,   50,   49,
 /*  1470 */    44,   50,
};
#define YY_SHIFT_USE_DFLT (1472)
#define YY_SHIFT_COUNT    (437)
#define YY_SHIFT_MIN      (-22)
#define YY_SHIFT_MAX      (1460)
static const short yy_shift_ofst[] = {
 /*     0 */    43,  497,  657,  509,  640,  640,  640,  640,  447,   -5,
 /*    10 */    47,   47,  640,  640,  640,  640,  640,  640,  640,  514,
 /*    20 */   514,  563,    3,  477,  600,  117,  149,  198,  227,  256,
 /*    30 */   285,  314,  342,  370,  400,  400,  400,  400,  400,  400,
 /*    40 */   400,  400,  400,  400,  400,  400,  400,  400,  400,  429,
 /*    50 */   400,  457,  483,  483,  628,  640,  640,  640,  640,  640,
 /*    60 */   640,  640,  640,  640,  640,  640,  640,  640,  640,  640,
 /*    70 */   640,  640,  640,  640,  640,  640,  640,  640,  640,  640,
 /*    80 */   640,  640,  759,  640,  640,  640,  640,  640,  640,  640,
 /*    90 */   640,  640,  640,  640,  640,  640,   11,   74,   74,   74,
 /*   100 */    74,   74,  160,   54,  797,   -7,  838,  823,  823,   -7,
 /*   110 */   372,  426,  -16, 1472, 1472, 1472,  643,  643,  643,  630,
 /*   120 */   630,  727,  488,  727,  505,  891,  908,   -7,   -7,   -7,
 /*   130 */    -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,
 /*   140 */    -7,   -7,   -7,   -7,   -7,   -7,   -7,  616,  165,  165,
 /*   150 */   426,  -22,  -22,  -22,  -22,  -22,  -22, 1472, 1472, 1472,
 /*   160 */   749,   20,   20,  739,  721,  724,  782,  802,  805,  812,
 /*   170 */    -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,
 /*   180 */    -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,  882,  882,
 /*   190 */   882,   -7,   -7,  913,   -7,   -7,   -7,  911,   -7,  927,
 /*   200 */    -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,   -7,
 /*   210 */  1035, 1037,  475,  475,  475,  917,  465,  995,  615,  681,
 /*   220 */  1030, 1030, 1125,  681, 1125, 1091, 1122, 1098,  534, 1030,
 /*   230 */   810,  534,  534, 1117,  901,  860, 1202, 1222, 1270, 1150,
 /*   240 */  1232, 1232, 1232, 1232, 1233, 1148, 1213, 1233, 1150, 1222,
 /*   250 */  1270, 1270, 1150, 1233, 1240, 1233, 1233, 1240, 1185, 1185,
 /*   260 */  1185, 1229, 1240, 1185, 1201, 1185, 1229, 1185, 1185, 1187,
 /*   270 */  1210, 1187, 1210, 1187, 1210, 1187, 1210, 1268, 1184, 1240,
 /*   280 */  1290, 1290, 1240, 1197, 1203, 1199, 1204, 1150, 1274, 1275,
 /*   290 */  1294, 1294, 1302, 1302, 1302, 1302, 1472, 1472, 1472, 1472,
 /*   300 */  1472, 1472, 1472, 1472,  853,  662,    1,  472, 1039,  702,
 /*   310 */  1126, 1161, 1128, 1134, 1135, 1136, 1141, 1146,  735, 1099,
 /*   320 */  1165, 1105, 1155, 1156, 1138, 1163, 1082, 1087, 1168, 1153,
 /*   330 */  1162, 1341, 1344, 1300, 1216, 1306, 1261, 1312, 1253, 1309,
 /*   340 */  1310, 1241, 1244, 1234, 1255, 1245, 1271, 1327, 1254, 1334,
 /*   350 */  1301, 1328, 1303, 1297, 1313, 1281, 1386, 1267, 1260, 1374,
 /*   360 */  1376, 1378, 1379, 1287, 1342, 1335, 1279, 1399, 1367, 1353,
 /*   370 */  1298, 1266, 1354, 1358, 1400, 1299, 1305, 1361, 1333, 1363,
 /*   380 */  1364, 1365, 1368, 1336, 1366, 1369, 1346, 1355, 1370, 1375,
 /*   390 */  1377, 1373, 1314, 1380, 1381, 1383, 1382, 1316, 1384, 1387,
 /*   400 */  1385, 1388, 1389, 1308, 1390, 1391, 1392, 1393, 1394, 1390,
 /*   410 */  1396, 1397, 1398, 1402, 1403, 1401, 1409, 1407, 1410, 1411,
 /*   420 */  1405, 1413, 1412, 1414, 1405, 1416, 1415, 1417, 1418, 1420,
 /*   430 */  1319, 1321, 1322, 1332, 1421, 1426, 1432, 1460,
};
#define YY_REDUCE_USE_DFLT (-51)
#define YY_REDUCE_COUNT (303)
#define YY_REDUCE_MIN   (-50)
#define YY_REDUCE_MAX   (1205)
static const short yy_reduce_ofst[] = {
 /*     0 */    58,  466,  374,  513,  446,  552,  562,  614,  619,  420,
 /*    10 */   668,  698,  745,  753,  755,  638,  704,  761,  564,  407,
 /*    20 */   754,  776,  601,  685,  391,  -23,  -23,  -23,  -23,  -23,
 /*    30 */   -23,  -23,  -23,  -23,  -23,  -23,  -23,  -23,  -23,  -23,
 /*    40 */   -23,  -23,  -23,  -23,  -23,  -23,  -23,  -23,  -23,  -23,
 /*    50 */   -23,  -23,  -23,  -23,  720,  783,  820,  822,  825,  827,
 /*    60 */   829,  837,  849,  851,  854,  858,  861,  863,  865,  878,
 /*    70 */   885,  887,  889,  892,  894,  898,  900,  902,  909,  914,
 /*    80 */   922,  924,  926,  929,  935,  937,  949,  951,  957,  959,
 /*    90 */   961,  963,  966,  972,  986,  988,  -23,  -23,  -23,  -23,
 /*   100 */   -23,  -23,  -23,  -23,  -23,  385,  699,  774,  817,  -41,
 /*   110 */   -23,  349,  -23,  -23,  -23,  -23,  504,  504,  504,  473,
 /*   120 */   474,  421,  486,  710,  690,  732,  732,  369,  579,  862,
 /*   130 */   895,  899,  923,  971,  973,  496,  974,  -37,  663,  915,
 /*   140 */   828,  939,  925,   49,  952,  893,  826,  773,  918, 1004,
 /*   150 */   962,  980,  991,  992,  999, 1002, 1009,  930,  967,  422,
 /*   160 */   -50,  -29,  -14,  140,  138,  214,  242,  284,  313,  399,
 /*   170 */   437,  523,  543,  553,  588,  608,  631,  646,  734,  756,
 /*   180 */   781,  800,  835,  875, 1071, 1073, 1074, 1075,  694, 1049,
 /*   190 */  1050, 1078, 1079, 1021, 1081, 1083, 1085, 1026, 1086, 1051,
 /*   200 */  1090, 1092, 1093,  214, 1094, 1095, 1096, 1097, 1100, 1101,
 /*   210 */  1007, 1012, 1042, 1046, 1047, 1021, 1061, 1062, 1107, 1060,
 /*   220 */  1048, 1052, 1024, 1063, 1028, 1089, 1084, 1102, 1103, 1055,
 /*   230 */  1043, 1104, 1106, 1072, 1076, 1114, 1034, 1057, 1108, 1088,
 /*   240 */  1109, 1110, 1111, 1112, 1127, 1054, 1058, 1133, 1113, 1080,
 /*   250 */  1116, 1118, 1115, 1137, 1121, 1140, 1147, 1131, 1139, 1143,
 /*   260 */  1144, 1129, 1142, 1149, 1145, 1151, 1130, 1152, 1154, 1119,
 /*   270 */  1123, 1120, 1124, 1132, 1157, 1158, 1160, 1159, 1164, 1166,
 /*   280 */  1167, 1169, 1170, 1171, 1173, 1175, 1177, 1172, 1178, 1176,
 /*   290 */  1189, 1190, 1193, 1196, 1198, 1200, 1174, 1179, 1180, 1191,
 /*   300 */  1192, 1181, 1182, 1205,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1234, 1228, 1228, 1228, 1164, 1164, 1164, 1164, 1228, 1059,
 /*    10 */  1086, 1086, 1284, 1284, 1284, 1284, 1284, 1284, 1163, 1284,
 /*    20 */  1284, 1284, 1284, 1228, 1063, 1092, 1284, 1284, 1284, 1165,
 /*    30 */  1166, 1284, 1284, 1284, 1197, 1102, 1101, 1100, 1099, 1073,
 /*    40 */  1097, 1090, 1094, 1165, 1159, 1160, 1158, 1162, 1166, 1284,
 /*    50 */  1093, 1128, 1143, 1127, 1284, 1284, 1284, 1284, 1284, 1284,
 /*    60 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*    70 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*    80 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*    90 */  1284, 1284, 1284, 1284, 1284, 1284, 1137, 1142, 1149, 1141,
 /*   100 */  1138, 1130, 1129, 1131, 1132, 1284, 1030, 1284, 1284, 1284,
 /*   110 */  1133, 1284, 1134, 1146, 1145, 1144, 1219, 1243, 1242, 1284,
 /*   120 */  1284, 1284, 1171, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   130 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   140 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1228,  988,  988,
 /*   150 */  1284, 1228, 1228, 1228, 1228, 1228, 1228, 1224, 1063, 1054,
 /*   160 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   170 */  1284, 1216, 1284, 1213, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   180 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   190 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1059, 1284, 1284,
 /*   200 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1237,
 /*   210 */  1284, 1192, 1059, 1059, 1059, 1061, 1043, 1053,  969, 1096,
 /*   220 */  1075, 1075, 1275, 1096, 1275, 1005, 1257, 1002, 1086, 1075,
 /*   230 */  1161, 1086, 1086, 1060, 1053, 1284, 1276, 1107, 1033, 1096,
 /*   240 */  1039, 1039, 1039, 1039,  981, 1196, 1272,  981, 1096, 1107,
 /*   250 */  1033, 1033, 1096,  981, 1172,  981,  981, 1172, 1031, 1031,
 /*   260 */  1031, 1020, 1172, 1031, 1005, 1031, 1020, 1031, 1031, 1079,
 /*   270 */  1074, 1079, 1074, 1079, 1074, 1079, 1074, 1167, 1284, 1172,
 /*   280 */  1176, 1176, 1172, 1091, 1080, 1089, 1087, 1096,  985, 1023,
 /*   290 */  1240, 1240, 1236, 1236, 1236, 1236, 1281, 1281, 1224, 1252,
 /*   300 */  1252, 1007, 1007, 1252, 1284, 1284, 1284, 1284, 1284, 1247,
 /*   310 */  1284, 1179, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   320 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   330 */  1113, 1284,  966, 1221, 1284, 1284, 1220, 1284, 1214, 1284,
 /*   340 */  1284, 1267, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   350 */  1284, 1195, 1194, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   360 */  1284, 1284, 1284, 1284, 1284, 1284, 1274, 1284, 1284, 1284,
 /*   370 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   380 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   390 */  1284, 1284, 1045, 1284, 1284, 1284, 1261, 1284, 1284, 1284,
 /*   400 */  1284, 1284, 1284, 1284, 1088, 1284, 1081, 1284, 1284, 1265,
 /*   410 */  1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284, 1284,
 /*   420 */  1230, 1284, 1284, 1284, 1229, 1284, 1284, 1284, 1284, 1284,
 /*   430 */  1115, 1284, 1114, 1118, 1284,  975, 1284, 1284,
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
   54,  /*    EXPLAIN => ID */
   54,  /*      QUERY => ID */
   54,  /*       PLAN => ID */
    0,  /*         OR => nothing */
    0,  /*        AND => nothing */
    0,  /*        NOT => nothing */
    0,  /*         IS => nothing */
   54,  /*      MATCH => ID */
   54,  /*    LIKE_KW => ID */
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
   54,  /*      BEGIN => ID */
    0,  /* TRANSACTION => nothing */
   54,  /*   DEFERRED => ID */
   54,  /*  IMMEDIATE => ID */
   54,  /*  EXCLUSIVE => ID */
    0,  /*     COMMIT => nothing */
   54,  /*        END => ID */
   54,  /*   ROLLBACK => ID */
   54,  /*  SAVEPOINT => ID */
   54,  /*    RELEASE => ID */
    0,  /*         TO => nothing */
    0,  /*      TABLE => nothing */
    0,  /*     CREATE => nothing */
   54,  /*         IF => ID */
    0,  /*     EXISTS => nothing */
    0,  /*         LP => nothing */
    0,  /*         RP => nothing */
    0,  /*         AS => nothing */
   54,  /*    WITHOUT => ID */
    0,  /*      COMMA => nothing */
    0,  /*         ID => nothing */
    0,  /*    INDEXED => nothing */
   54,  /*      ABORT => ID */
   54,  /*     ACTION => ID */
   54,  /*      AFTER => ID */
   54,  /*    ANALYZE => ID */
   54,  /*        ASC => ID */
   54,  /*     ATTACH => ID */
   54,  /*     BEFORE => ID */
   54,  /*         BY => ID */
   54,  /*    CASCADE => ID */
   54,  /*       CAST => ID */
   54,  /*   COLUMNKW => ID */
   54,  /*   CONFLICT => ID */
   54,  /*   DATABASE => ID */
   54,  /*       DESC => ID */
   54,  /*     DETACH => ID */
   54,  /*       EACH => ID */
   54,  /*       FAIL => ID */
   54,  /*        FOR => ID */
   54,  /*     IGNORE => ID */
   54,  /*  INITIALLY => ID */
   54,  /*    INSTEAD => ID */
   54,  /*         NO => ID */
   54,  /*        KEY => ID */
   54,  /*         OF => ID */
   54,  /*     OFFSET => ID */
   54,  /*     PRAGMA => ID */
   54,  /*      RAISE => ID */
   54,  /*  RECURSIVE => ID */
   54,  /*    REPLACE => ID */
   54,  /*   RESTRICT => ID */
   54,  /*        ROW => ID */
   54,  /*    TRIGGER => ID */
   54,  /*       VIEW => ID */
   54,  /*    VIRTUAL => ID */
   54,  /*       WITH => ID */
   54,  /*    REINDEX => ID */
   54,  /*     RENAME => ID */
   54,  /*   CTIME_KW => ID */
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
  "PLAN",          "OR",            "AND",           "NOT",         
  "IS",            "MATCH",         "LIKE_KW",       "BETWEEN",     
  "IN",            "ISNULL",        "NOTNULL",       "NE",          
  "EQ",            "GT",            "LE",            "LT",          
  "GE",            "ESCAPE",        "BITAND",        "BITOR",       
  "LSHIFT",        "RSHIFT",        "PLUS",          "MINUS",       
  "STAR",          "SLASH",         "REM",           "CONCAT",      
  "COLLATE",       "BITNOT",        "BEGIN",         "TRANSACTION", 
  "DEFERRED",      "IMMEDIATE",     "EXCLUSIVE",     "COMMIT",      
  "END",           "ROLLBACK",      "SAVEPOINT",     "RELEASE",     
  "TO",            "TABLE",         "CREATE",        "IF",          
  "EXISTS",        "LP",            "RP",            "AS",          
  "WITHOUT",       "COMMA",         "ID",            "INDEXED",     
  "ABORT",         "ACTION",        "AFTER",         "ANALYZE",     
  "ASC",           "ATTACH",        "BEFORE",        "BY",          
  "CASCADE",       "CAST",          "COLUMNKW",      "CONFLICT",    
  "DATABASE",      "DESC",          "DETACH",        "EACH",        
  "FAIL",          "FOR",           "IGNORE",        "INITIALLY",   
  "INSTEAD",       "NO",            "KEY",           "OF",          
  "OFFSET",        "PRAGMA",        "RAISE",         "RECURSIVE",   
  "REPLACE",       "RESTRICT",      "ROW",           "TRIGGER",     
  "VIEW",          "VIRTUAL",       "WITH",          "REINDEX",     
  "RENAME",        "CTIME_KW",      "ANY",           "STRING",      
  "JOIN_KW",       "CONSTRAINT",    "DEFAULT",       "NULL",        
  "PRIMARY",       "UNIQUE",        "CHECK",         "REFERENCES",  
  "AUTOINCR",      "ON",            "INSERT",        "DELETE",      
  "UPDATE",        "SET",           "DEFERRABLE",    "FOREIGN",     
  "DROP",          "UNION",         "ALL",           "EXCEPT",      
  "INTERSECT",     "SELECT",        "VALUES",        "DISTINCT",    
  "DOT",           "FROM",          "JOIN",          "USING",       
  "ORDER",         "GROUP",         "HAVING",        "LIMIT",       
  "WHERE",         "INTO",          "FLOAT",         "BLOB",        
  "INTEGER",       "VARIABLE",      "CASE",          "WHEN",        
  "THEN",          "ELSE",          "INDEX",         "ALTER",       
  "ADD",           "error",         "input",         "ecmd",        
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
  "idlist",        "setlist",       "insert_cmd",    "idlist_opt",  
  "likeop",        "between_op",    "in_op",         "paren_exprlist",
  "case_operand",  "case_exprlist",  "case_else",     "uniqueflag",  
  "collate",       "nmnum",         "trigger_decl",  "trigger_cmd_list",
  "trigger_time",  "trigger_event",  "foreach_clause",  "when_clause", 
  "trigger_cmd",   "trnm",          "tridxby",       "add_column_fullname",
  "kwcolumn_opt",  "create_vtab",   "vtabarglist",   "vtabarg",     
  "vtabargtoken",  "lp",            "anylist",       "wqlist",      
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
 /*   7 */ "transtype ::= IMMEDIATE",
 /*   8 */ "transtype ::= EXCLUSIVE",
 /*   9 */ "cmd ::= COMMIT trans_opt",
 /*  10 */ "cmd ::= END trans_opt",
 /*  11 */ "cmd ::= ROLLBACK trans_opt",
 /*  12 */ "cmd ::= SAVEPOINT nm",
 /*  13 */ "cmd ::= RELEASE savepoint_opt nm",
 /*  14 */ "cmd ::= ROLLBACK trans_opt TO savepoint_opt nm",
 /*  15 */ "create_table ::= createkw TABLE ifnotexists nm",
 /*  16 */ "createkw ::= CREATE",
 /*  17 */ "ifnotexists ::=",
 /*  18 */ "ifnotexists ::= IF NOT EXISTS",
 /*  19 */ "create_table_args ::= LP columnlist conslist_opt RP table_options",
 /*  20 */ "create_table_args ::= AS select",
 /*  21 */ "table_options ::=",
 /*  22 */ "table_options ::= WITHOUT nm",
 /*  23 */ "columnname ::= nm typetoken",
 /*  24 */ "typetoken ::=",
 /*  25 */ "typetoken ::= typename LP signed RP",
 /*  26 */ "typetoken ::= typename LP signed COMMA signed RP",
 /*  27 */ "typename ::= typename ID|STRING",
 /*  28 */ "ccons ::= CONSTRAINT nm",
 /*  29 */ "ccons ::= DEFAULT term",
 /*  30 */ "ccons ::= DEFAULT LP expr RP",
 /*  31 */ "ccons ::= DEFAULT PLUS term",
 /*  32 */ "ccons ::= DEFAULT MINUS term",
 /*  33 */ "ccons ::= DEFAULT ID|INDEXED",
 /*  34 */ "ccons ::= NOT NULL onconf",
 /*  35 */ "ccons ::= PRIMARY KEY sortorder onconf autoinc",
 /*  36 */ "ccons ::= UNIQUE onconf",
 /*  37 */ "ccons ::= CHECK LP expr RP",
 /*  38 */ "ccons ::= REFERENCES nm eidlist_opt refargs",
 /*  39 */ "ccons ::= defer_subclause",
 /*  40 */ "ccons ::= COLLATE ID|STRING",
 /*  41 */ "autoinc ::=",
 /*  42 */ "autoinc ::= AUTOINCR",
 /*  43 */ "refargs ::=",
 /*  44 */ "refargs ::= refargs refarg",
 /*  45 */ "refarg ::= MATCH nm",
 /*  46 */ "refarg ::= ON INSERT refact",
 /*  47 */ "refarg ::= ON DELETE refact",
 /*  48 */ "refarg ::= ON UPDATE refact",
 /*  49 */ "refact ::= SET NULL",
 /*  50 */ "refact ::= SET DEFAULT",
 /*  51 */ "refact ::= CASCADE",
 /*  52 */ "refact ::= RESTRICT",
 /*  53 */ "refact ::= NO ACTION",
 /*  54 */ "defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt",
 /*  55 */ "defer_subclause ::= DEFERRABLE init_deferred_pred_opt",
 /*  56 */ "init_deferred_pred_opt ::=",
 /*  57 */ "init_deferred_pred_opt ::= INITIALLY DEFERRED",
 /*  58 */ "init_deferred_pred_opt ::= INITIALLY IMMEDIATE",
 /*  59 */ "conslist_opt ::=",
 /*  60 */ "tconscomma ::= COMMA",
 /*  61 */ "tcons ::= CONSTRAINT nm",
 /*  62 */ "tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf",
 /*  63 */ "tcons ::= UNIQUE LP sortlist RP onconf",
 /*  64 */ "tcons ::= CHECK LP expr RP onconf",
 /*  65 */ "tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt",
 /*  66 */ "defer_subclause_opt ::=",
 /*  67 */ "onconf ::=",
 /*  68 */ "onconf ::= ON CONFLICT resolvetype",
 /*  69 */ "orconf ::=",
 /*  70 */ "orconf ::= OR resolvetype",
 /*  71 */ "resolvetype ::= IGNORE",
 /*  72 */ "resolvetype ::= REPLACE",
 /*  73 */ "cmd ::= DROP TABLE ifexists fullname",
 /*  74 */ "ifexists ::= IF EXISTS",
 /*  75 */ "ifexists ::=",
 /*  76 */ "cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select",
 /*  77 */ "cmd ::= DROP VIEW ifexists fullname",
 /*  78 */ "cmd ::= select",
 /*  79 */ "select ::= with selectnowith",
 /*  80 */ "selectnowith ::= selectnowith multiselect_op oneselect",
 /*  81 */ "multiselect_op ::= UNION",
 /*  82 */ "multiselect_op ::= UNION ALL",
 /*  83 */ "multiselect_op ::= EXCEPT|INTERSECT",
 /*  84 */ "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
 /*  85 */ "values ::= VALUES LP nexprlist RP",
 /*  86 */ "values ::= values COMMA LP exprlist RP",
 /*  87 */ "distinct ::= DISTINCT",
 /*  88 */ "distinct ::= ALL",
 /*  89 */ "distinct ::=",
 /*  90 */ "sclp ::=",
 /*  91 */ "selcollist ::= sclp expr as",
 /*  92 */ "selcollist ::= sclp STAR",
 /*  93 */ "selcollist ::= sclp nm DOT STAR",
 /*  94 */ "as ::= AS nm",
 /*  95 */ "as ::=",
 /*  96 */ "from ::=",
 /*  97 */ "from ::= FROM seltablist",
 /*  98 */ "stl_prefix ::= seltablist joinop",
 /*  99 */ "stl_prefix ::=",
 /* 100 */ "seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt",
 /* 101 */ "seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt",
 /* 102 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
 /* 103 */ "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
 /* 104 */ "fullname ::= nm",
 /* 105 */ "joinop ::= COMMA|JOIN",
 /* 106 */ "joinop ::= JOIN_KW JOIN",
 /* 107 */ "joinop ::= JOIN_KW nm JOIN",
 /* 108 */ "joinop ::= JOIN_KW nm nm JOIN",
 /* 109 */ "on_opt ::= ON expr",
 /* 110 */ "on_opt ::=",
 /* 111 */ "indexed_opt ::=",
 /* 112 */ "indexed_opt ::= INDEXED BY nm",
 /* 113 */ "indexed_opt ::= NOT INDEXED",
 /* 114 */ "using_opt ::= USING LP idlist RP",
 /* 115 */ "using_opt ::=",
 /* 116 */ "orderby_opt ::=",
 /* 117 */ "orderby_opt ::= ORDER BY sortlist",
 /* 118 */ "sortlist ::= sortlist COMMA expr sortorder",
 /* 119 */ "sortlist ::= expr sortorder",
 /* 120 */ "sortorder ::= ASC",
 /* 121 */ "sortorder ::= DESC",
 /* 122 */ "sortorder ::=",
 /* 123 */ "groupby_opt ::=",
 /* 124 */ "groupby_opt ::= GROUP BY nexprlist",
 /* 125 */ "having_opt ::=",
 /* 126 */ "having_opt ::= HAVING expr",
 /* 127 */ "limit_opt ::=",
 /* 128 */ "limit_opt ::= LIMIT expr",
 /* 129 */ "limit_opt ::= LIMIT expr OFFSET expr",
 /* 130 */ "limit_opt ::= LIMIT expr COMMA expr",
 /* 131 */ "cmd ::= with DELETE FROM fullname indexed_opt where_opt",
 /* 132 */ "where_opt ::=",
 /* 133 */ "where_opt ::= WHERE expr",
 /* 134 */ "cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt",
 /* 135 */ "setlist ::= setlist COMMA nm EQ expr",
 /* 136 */ "setlist ::= setlist COMMA LP idlist RP EQ expr",
 /* 137 */ "setlist ::= nm EQ expr",
 /* 138 */ "setlist ::= LP idlist RP EQ expr",
 /* 139 */ "cmd ::= with insert_cmd INTO fullname idlist_opt select",
 /* 140 */ "cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES",
 /* 141 */ "insert_cmd ::= INSERT orconf",
 /* 142 */ "insert_cmd ::= REPLACE",
 /* 143 */ "idlist_opt ::=",
 /* 144 */ "idlist_opt ::= LP idlist RP",
 /* 145 */ "idlist ::= idlist COMMA nm",
 /* 146 */ "idlist ::= nm",
 /* 147 */ "expr ::= LP expr RP",
 /* 148 */ "term ::= NULL",
 /* 149 */ "expr ::= ID|INDEXED",
 /* 150 */ "expr ::= JOIN_KW",
 /* 151 */ "expr ::= nm DOT nm",
 /* 152 */ "expr ::= nm DOT nm DOT nm",
 /* 153 */ "term ::= FLOAT|BLOB",
 /* 154 */ "term ::= STRING",
 /* 155 */ "term ::= INTEGER",
 /* 156 */ "expr ::= VARIABLE",
 /* 157 */ "expr ::= expr COLLATE ID|STRING",
 /* 158 */ "expr ::= CAST LP expr AS typetoken RP",
 /* 159 */ "expr ::= ID|INDEXED LP distinct exprlist RP",
 /* 160 */ "expr ::= ID|INDEXED LP STAR RP",
 /* 161 */ "term ::= CTIME_KW",
 /* 162 */ "expr ::= LP nexprlist COMMA expr RP",
 /* 163 */ "expr ::= expr AND expr",
 /* 164 */ "expr ::= expr OR expr",
 /* 165 */ "expr ::= expr LT|GT|GE|LE expr",
 /* 166 */ "expr ::= expr EQ|NE expr",
 /* 167 */ "expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr",
 /* 168 */ "expr ::= expr PLUS|MINUS expr",
 /* 169 */ "expr ::= expr STAR|SLASH|REM expr",
 /* 170 */ "expr ::= expr CONCAT expr",
 /* 171 */ "likeop ::= LIKE_KW|MATCH",
 /* 172 */ "likeop ::= NOT LIKE_KW|MATCH",
 /* 173 */ "expr ::= expr likeop expr",
 /* 174 */ "expr ::= expr likeop expr ESCAPE expr",
 /* 175 */ "expr ::= expr ISNULL|NOTNULL",
 /* 176 */ "expr ::= expr NOT NULL",
 /* 177 */ "expr ::= expr IS expr",
 /* 178 */ "expr ::= expr IS NOT expr",
 /* 179 */ "expr ::= NOT expr",
 /* 180 */ "expr ::= BITNOT expr",
 /* 181 */ "expr ::= MINUS expr",
 /* 182 */ "expr ::= PLUS expr",
 /* 183 */ "between_op ::= BETWEEN",
 /* 184 */ "between_op ::= NOT BETWEEN",
 /* 185 */ "expr ::= expr between_op expr AND expr",
 /* 186 */ "in_op ::= IN",
 /* 187 */ "in_op ::= NOT IN",
 /* 188 */ "expr ::= expr in_op LP exprlist RP",
 /* 189 */ "expr ::= LP select RP",
 /* 190 */ "expr ::= expr in_op LP select RP",
 /* 191 */ "expr ::= expr in_op nm paren_exprlist",
 /* 192 */ "expr ::= EXISTS LP select RP",
 /* 193 */ "expr ::= CASE case_operand case_exprlist case_else END",
 /* 194 */ "case_exprlist ::= case_exprlist WHEN expr THEN expr",
 /* 195 */ "case_exprlist ::= WHEN expr THEN expr",
 /* 196 */ "case_else ::= ELSE expr",
 /* 197 */ "case_else ::=",
 /* 198 */ "case_operand ::= expr",
 /* 199 */ "case_operand ::=",
 /* 200 */ "exprlist ::=",
 /* 201 */ "nexprlist ::= nexprlist COMMA expr",
 /* 202 */ "nexprlist ::= expr",
 /* 203 */ "paren_exprlist ::=",
 /* 204 */ "paren_exprlist ::= LP exprlist RP",
 /* 205 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt",
 /* 206 */ "uniqueflag ::= UNIQUE",
 /* 207 */ "uniqueflag ::=",
 /* 208 */ "eidlist_opt ::=",
 /* 209 */ "eidlist_opt ::= LP eidlist RP",
 /* 210 */ "eidlist ::= eidlist COMMA nm collate sortorder",
 /* 211 */ "eidlist ::= nm collate sortorder",
 /* 212 */ "collate ::=",
 /* 213 */ "collate ::= COLLATE ID|STRING",
 /* 214 */ "cmd ::= DROP INDEX ifexists fullname ON nm",
 /* 215 */ "cmd ::= PRAGMA nm",
 /* 216 */ "cmd ::= PRAGMA nm EQ nmnum",
 /* 217 */ "cmd ::= PRAGMA nm LP nmnum RP",
 /* 218 */ "cmd ::= PRAGMA nm EQ minus_num",
 /* 219 */ "cmd ::= PRAGMA nm LP minus_num RP",
 /* 220 */ "cmd ::= PRAGMA nm EQ nm DOT nm",
 /* 221 */ "plus_num ::= PLUS INTEGER|FLOAT",
 /* 222 */ "minus_num ::= MINUS INTEGER|FLOAT",
 /* 223 */ "cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END",
 /* 224 */ "trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause",
 /* 225 */ "trigger_time ::= BEFORE",
 /* 226 */ "trigger_time ::= AFTER",
 /* 227 */ "trigger_time ::= INSTEAD OF",
 /* 228 */ "trigger_time ::=",
 /* 229 */ "trigger_event ::= DELETE|INSERT",
 /* 230 */ "trigger_event ::= UPDATE",
 /* 231 */ "trigger_event ::= UPDATE OF idlist",
 /* 232 */ "when_clause ::=",
 /* 233 */ "when_clause ::= WHEN expr",
 /* 234 */ "trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI",
 /* 235 */ "trigger_cmd_list ::= trigger_cmd SEMI",
 /* 236 */ "trnm ::= nm DOT nm",
 /* 237 */ "tridxby ::= INDEXED BY nm",
 /* 238 */ "tridxby ::= NOT INDEXED",
 /* 239 */ "trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt",
 /* 240 */ "trigger_cmd ::= insert_cmd INTO trnm idlist_opt select",
 /* 241 */ "trigger_cmd ::= DELETE FROM trnm tridxby where_opt",
 /* 242 */ "trigger_cmd ::= select",
 /* 243 */ "expr ::= RAISE LP IGNORE RP",
 /* 244 */ "expr ::= RAISE LP raisetype COMMA nm RP",
 /* 245 */ "raisetype ::= ROLLBACK",
 /* 246 */ "raisetype ::= ABORT",
 /* 247 */ "raisetype ::= FAIL",
 /* 248 */ "cmd ::= DROP TRIGGER ifexists fullname",
 /* 249 */ "cmd ::= REINDEX",
 /* 250 */ "cmd ::= REINDEX nm",
 /* 251 */ "cmd ::= REINDEX nm ON nm",
 /* 252 */ "cmd ::= ANALYZE",
 /* 253 */ "cmd ::= ANALYZE nm",
 /* 254 */ "cmd ::= ALTER TABLE fullname RENAME TO nm",
 /* 255 */ "cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist",
 /* 256 */ "add_column_fullname ::= fullname",
 /* 257 */ "cmd ::= create_vtab",
 /* 258 */ "cmd ::= create_vtab LP vtabarglist RP",
 /* 259 */ "create_vtab ::= createkw VIRTUAL TABLE ifnotexists nm USING nm",
 /* 260 */ "vtabarg ::=",
 /* 261 */ "vtabargtoken ::= ANY",
 /* 262 */ "vtabargtoken ::= lp anylist RP",
 /* 263 */ "lp ::= LP",
 /* 264 */ "with ::=",
 /* 265 */ "with ::= WITH wqlist",
 /* 266 */ "with ::= WITH RECURSIVE wqlist",
 /* 267 */ "wqlist ::= nm eidlist_opt AS LP select RP",
 /* 268 */ "wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP",
 /* 269 */ "input ::= ecmd",
 /* 270 */ "explain ::=",
 /* 271 */ "cmdx ::= cmd",
 /* 272 */ "trans_opt ::=",
 /* 273 */ "trans_opt ::= TRANSACTION",
 /* 274 */ "trans_opt ::= TRANSACTION nm",
 /* 275 */ "savepoint_opt ::= SAVEPOINT",
 /* 276 */ "savepoint_opt ::=",
 /* 277 */ "cmd ::= create_table create_table_args",
 /* 278 */ "columnlist ::= columnlist COMMA columnname carglist",
 /* 279 */ "columnlist ::= columnname carglist",
 /* 280 */ "nm ::= ID|INDEXED",
 /* 281 */ "nm ::= STRING",
 /* 282 */ "nm ::= JOIN_KW",
 /* 283 */ "typetoken ::= typename",
 /* 284 */ "typename ::= ID|STRING",
 /* 285 */ "signed ::= plus_num",
 /* 286 */ "signed ::= minus_num",
 /* 287 */ "carglist ::= carglist ccons",
 /* 288 */ "carglist ::=",
 /* 289 */ "ccons ::= NULL onconf",
 /* 290 */ "conslist_opt ::= COMMA conslist",
 /* 291 */ "conslist ::= conslist tconscomma tcons",
 /* 292 */ "conslist ::= tcons",
 /* 293 */ "tconscomma ::=",
 /* 294 */ "defer_subclause_opt ::= defer_subclause",
 /* 295 */ "resolvetype ::= raisetype",
 /* 296 */ "selectnowith ::= oneselect",
 /* 297 */ "oneselect ::= values",
 /* 298 */ "sclp ::= selcollist COMMA",
 /* 299 */ "as ::= ID|STRING",
 /* 300 */ "expr ::= term",
 /* 301 */ "exprlist ::= nexprlist",
 /* 302 */ "nmnum ::= plus_num",
 /* 303 */ "nmnum ::= nm",
 /* 304 */ "nmnum ::= ON",
 /* 305 */ "nmnum ::= DELETE",
 /* 306 */ "nmnum ::= DEFAULT",
 /* 307 */ "plus_num ::= INTEGER|FLOAT",
 /* 308 */ "foreach_clause ::=",
 /* 309 */ "foreach_clause ::= FOR EACH ROW",
 /* 310 */ "trnm ::= nm",
 /* 311 */ "tridxby ::=",
 /* 312 */ "kwcolumn_opt ::=",
 /* 313 */ "kwcolumn_opt ::= COLUMNKW",
 /* 314 */ "vtabarglist ::= vtabarg",
 /* 315 */ "vtabarglist ::= vtabarglist COMMA vtabarg",
 /* 316 */ "vtabarg ::= vtabarg vtabargtoken",
 /* 317 */ "anylist ::=",
 /* 318 */ "anylist ::= anylist LP anylist RP",
 /* 319 */ "anylist ::= anylist ANY",
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
    case 158: /* select */
    case 189: /* selectnowith */
    case 190: /* oneselect */
    case 201: /* values */
{
#line 393 "parse.y"
sqlite3SelectDelete(pParse->db, (yypminor->yy171));
#line 1535 "parse.c"
}
      break;
    case 167: /* term */
    case 168: /* expr */
{
#line 834 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy438).pExpr);
#line 1543 "parse.c"
}
      break;
    case 172: /* eidlist_opt */
    case 181: /* sortlist */
    case 182: /* eidlist */
    case 194: /* selcollist */
    case 197: /* groupby_opt */
    case 199: /* orderby_opt */
    case 202: /* nexprlist */
    case 203: /* exprlist */
    case 204: /* sclp */
    case 213: /* setlist */
    case 219: /* paren_exprlist */
    case 221: /* case_exprlist */
{
#line 1282 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy298));
#line 1561 "parse.c"
}
      break;
    case 188: /* fullname */
    case 195: /* from */
    case 206: /* seltablist */
    case 207: /* stl_prefix */
{
#line 621 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy339));
#line 1571 "parse.c"
}
      break;
    case 191: /* with */
    case 243: /* wqlist */
{
#line 1547 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy323));
#line 1579 "parse.c"
}
      break;
    case 196: /* where_opt */
    case 198: /* having_opt */
    case 210: /* on_opt */
    case 220: /* case_operand */
    case 222: /* case_else */
    case 231: /* when_clause */
{
#line 743 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy258));
#line 1591 "parse.c"
}
      break;
    case 211: /* using_opt */
    case 212: /* idlist */
    case 215: /* idlist_opt */
{
#line 655 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy224));
#line 1600 "parse.c"
}
      break;
    case 227: /* trigger_cmd_list */
    case 232: /* trigger_cmd */
{
#line 1401 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy251));
#line 1608 "parse.c"
}
      break;
    case 229: /* trigger_event */
{
#line 1387 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy114).b);
#line 1615 "parse.c"
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
#line 1788 "parse.c"
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
  { 143, 3 },
  { 143, 1 },
  { 144, 1 },
  { 144, 3 },
  { 146, 3 },
  { 147, 0 },
  { 147, 1 },
  { 147, 1 },
  { 147, 1 },
  { 146, 2 },
  { 146, 2 },
  { 146, 2 },
  { 146, 2 },
  { 146, 3 },
  { 146, 5 },
  { 151, 4 },
  { 153, 1 },
  { 154, 0 },
  { 154, 3 },
  { 152, 5 },
  { 152, 2 },
  { 157, 0 },
  { 157, 2 },
  { 159, 2 },
  { 161, 0 },
  { 161, 4 },
  { 161, 6 },
  { 162, 2 },
  { 166, 2 },
  { 166, 2 },
  { 166, 4 },
  { 166, 3 },
  { 166, 3 },
  { 166, 2 },
  { 166, 3 },
  { 166, 5 },
  { 166, 2 },
  { 166, 4 },
  { 166, 4 },
  { 166, 1 },
  { 166, 2 },
  { 171, 0 },
  { 171, 1 },
  { 173, 0 },
  { 173, 2 },
  { 175, 2 },
  { 175, 3 },
  { 175, 3 },
  { 175, 3 },
  { 176, 2 },
  { 176, 2 },
  { 176, 1 },
  { 176, 1 },
  { 176, 2 },
  { 174, 3 },
  { 174, 2 },
  { 177, 0 },
  { 177, 2 },
  { 177, 2 },
  { 156, 0 },
  { 179, 1 },
  { 180, 2 },
  { 180, 7 },
  { 180, 5 },
  { 180, 5 },
  { 180, 10 },
  { 183, 0 },
  { 169, 0 },
  { 169, 3 },
  { 184, 0 },
  { 184, 2 },
  { 185, 1 },
  { 185, 1 },
  { 146, 4 },
  { 187, 2 },
  { 187, 0 },
  { 146, 7 },
  { 146, 4 },
  { 146, 1 },
  { 158, 2 },
  { 189, 3 },
  { 192, 1 },
  { 192, 2 },
  { 192, 1 },
  { 190, 9 },
  { 201, 4 },
  { 201, 5 },
  { 193, 1 },
  { 193, 1 },
  { 193, 0 },
  { 204, 0 },
  { 194, 3 },
  { 194, 2 },
  { 194, 4 },
  { 205, 2 },
  { 205, 0 },
  { 195, 0 },
  { 195, 2 },
  { 207, 2 },
  { 207, 0 },
  { 206, 6 },
  { 206, 8 },
  { 206, 7 },
  { 206, 7 },
  { 188, 1 },
  { 208, 1 },
  { 208, 2 },
  { 208, 3 },
  { 208, 4 },
  { 210, 2 },
  { 210, 0 },
  { 209, 0 },
  { 209, 3 },
  { 209, 2 },
  { 211, 4 },
  { 211, 0 },
  { 199, 0 },
  { 199, 3 },
  { 181, 4 },
  { 181, 2 },
  { 170, 1 },
  { 170, 1 },
  { 170, 0 },
  { 197, 0 },
  { 197, 3 },
  { 198, 0 },
  { 198, 2 },
  { 200, 0 },
  { 200, 2 },
  { 200, 4 },
  { 200, 4 },
  { 146, 6 },
  { 196, 0 },
  { 196, 2 },
  { 146, 8 },
  { 213, 5 },
  { 213, 7 },
  { 213, 3 },
  { 213, 5 },
  { 146, 6 },
  { 146, 7 },
  { 214, 2 },
  { 214, 1 },
  { 215, 0 },
  { 215, 3 },
  { 212, 3 },
  { 212, 1 },
  { 168, 3 },
  { 167, 1 },
  { 168, 1 },
  { 168, 1 },
  { 168, 3 },
  { 168, 5 },
  { 167, 1 },
  { 167, 1 },
  { 167, 1 },
  { 168, 1 },
  { 168, 3 },
  { 168, 6 },
  { 168, 5 },
  { 168, 4 },
  { 167, 1 },
  { 168, 5 },
  { 168, 3 },
  { 168, 3 },
  { 168, 3 },
  { 168, 3 },
  { 168, 3 },
  { 168, 3 },
  { 168, 3 },
  { 168, 3 },
  { 216, 1 },
  { 216, 2 },
  { 168, 3 },
  { 168, 5 },
  { 168, 2 },
  { 168, 3 },
  { 168, 3 },
  { 168, 4 },
  { 168, 2 },
  { 168, 2 },
  { 168, 2 },
  { 168, 2 },
  { 217, 1 },
  { 217, 2 },
  { 168, 5 },
  { 218, 1 },
  { 218, 2 },
  { 168, 5 },
  { 168, 3 },
  { 168, 5 },
  { 168, 4 },
  { 168, 4 },
  { 168, 5 },
  { 221, 5 },
  { 221, 4 },
  { 222, 2 },
  { 222, 0 },
  { 220, 1 },
  { 220, 0 },
  { 203, 0 },
  { 202, 3 },
  { 202, 1 },
  { 219, 0 },
  { 219, 3 },
  { 146, 11 },
  { 223, 1 },
  { 223, 0 },
  { 172, 0 },
  { 172, 3 },
  { 182, 5 },
  { 182, 3 },
  { 224, 0 },
  { 224, 2 },
  { 146, 6 },
  { 146, 2 },
  { 146, 4 },
  { 146, 5 },
  { 146, 4 },
  { 146, 5 },
  { 146, 6 },
  { 164, 2 },
  { 165, 2 },
  { 146, 5 },
  { 226, 9 },
  { 228, 1 },
  { 228, 1 },
  { 228, 2 },
  { 228, 0 },
  { 229, 1 },
  { 229, 1 },
  { 229, 3 },
  { 231, 0 },
  { 231, 2 },
  { 227, 3 },
  { 227, 2 },
  { 233, 3 },
  { 234, 3 },
  { 234, 2 },
  { 232, 7 },
  { 232, 5 },
  { 232, 5 },
  { 232, 1 },
  { 168, 4 },
  { 168, 6 },
  { 186, 1 },
  { 186, 1 },
  { 186, 1 },
  { 146, 4 },
  { 146, 1 },
  { 146, 2 },
  { 146, 4 },
  { 146, 1 },
  { 146, 2 },
  { 146, 6 },
  { 146, 7 },
  { 235, 1 },
  { 146, 1 },
  { 146, 4 },
  { 237, 7 },
  { 239, 0 },
  { 240, 1 },
  { 240, 3 },
  { 241, 1 },
  { 191, 0 },
  { 191, 2 },
  { 191, 3 },
  { 243, 6 },
  { 243, 8 },
  { 142, 1 },
  { 144, 0 },
  { 145, 1 },
  { 148, 0 },
  { 148, 1 },
  { 148, 2 },
  { 150, 1 },
  { 150, 0 },
  { 146, 2 },
  { 155, 4 },
  { 155, 2 },
  { 149, 1 },
  { 149, 1 },
  { 149, 1 },
  { 161, 1 },
  { 162, 1 },
  { 163, 1 },
  { 163, 1 },
  { 160, 2 },
  { 160, 0 },
  { 166, 2 },
  { 156, 2 },
  { 178, 3 },
  { 178, 1 },
  { 179, 0 },
  { 183, 1 },
  { 185, 1 },
  { 189, 1 },
  { 190, 1 },
  { 204, 2 },
  { 205, 1 },
  { 168, 1 },
  { 203, 1 },
  { 225, 1 },
  { 225, 1 },
  { 225, 1 },
  { 225, 1 },
  { 225, 1 },
  { 164, 1 },
  { 230, 0 },
  { 230, 3 },
  { 233, 1 },
  { 234, 0 },
  { 236, 0 },
  { 236, 1 },
  { 238, 1 },
  { 238, 3 },
  { 239, 2 },
  { 242, 0 },
  { 242, 4 },
  { 242, 2 },
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
#line 107 "parse.y"
{ sqlite3FinishCoding(pParse); }
#line 2248 "parse.c"
        break;
      case 1: /* ecmd ::= SEMI */
#line 108 "parse.y"
{
  sqlite3ErrorMsg(pParse, "syntax error: empty request");
}
#line 2255 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN */
#line 113 "parse.y"
{ pParse->explain = 1; }
#line 2260 "parse.c"
        break;
      case 3: /* explain ::= EXPLAIN QUERY PLAN */
#line 114 "parse.y"
{ pParse->explain = 2; }
#line 2265 "parse.c"
        break;
      case 4: /* cmd ::= BEGIN transtype trans_opt */
#line 146 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy284);}
#line 2270 "parse.c"
        break;
      case 5: /* transtype ::= */
#line 151 "parse.y"
{yymsp[1].minor.yy284 = TK_DEFERRED;}
#line 2275 "parse.c"
        break;
      case 6: /* transtype ::= DEFERRED */
      case 7: /* transtype ::= IMMEDIATE */ yytestcase(yyruleno==7);
      case 8: /* transtype ::= EXCLUSIVE */ yytestcase(yyruleno==8);
#line 152 "parse.y"
{yymsp[0].minor.yy284 = yymsp[0].major; /*A-overwrites-X*/}
#line 2282 "parse.c"
        break;
      case 9: /* cmd ::= COMMIT trans_opt */
      case 10: /* cmd ::= END trans_opt */ yytestcase(yyruleno==10);
#line 155 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2288 "parse.c"
        break;
      case 11: /* cmd ::= ROLLBACK trans_opt */
#line 157 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2293 "parse.c"
        break;
      case 12: /* cmd ::= SAVEPOINT nm */
#line 161 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2300 "parse.c"
        break;
      case 13: /* cmd ::= RELEASE savepoint_opt nm */
#line 164 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2307 "parse.c"
        break;
      case 14: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 167 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2314 "parse.c"
        break;
      case 15: /* create_table ::= createkw TABLE ifnotexists nm */
#line 174 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[0].minor.yy0,0,0,0,yymsp[-1].minor.yy284);
}
#line 2321 "parse.c"
        break;
      case 16: /* createkw ::= CREATE */
#line 177 "parse.y"
{disableLookaside(pParse);}
#line 2326 "parse.c"
        break;
      case 17: /* ifnotexists ::= */
      case 21: /* table_options ::= */ yytestcase(yyruleno==21);
      case 41: /* autoinc ::= */ yytestcase(yyruleno==41);
      case 56: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==56);
      case 66: /* defer_subclause_opt ::= */ yytestcase(yyruleno==66);
      case 75: /* ifexists ::= */ yytestcase(yyruleno==75);
      case 89: /* distinct ::= */ yytestcase(yyruleno==89);
      case 212: /* collate ::= */ yytestcase(yyruleno==212);
#line 180 "parse.y"
{yymsp[1].minor.yy284 = 0;}
#line 2338 "parse.c"
        break;
      case 18: /* ifnotexists ::= IF NOT EXISTS */
#line 181 "parse.y"
{yymsp[-2].minor.yy284 = 1;}
#line 2343 "parse.c"
        break;
      case 19: /* create_table_args ::= LP columnlist conslist_opt RP table_options */
#line 183 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,yymsp[0].minor.yy284,0);
}
#line 2350 "parse.c"
        break;
      case 20: /* create_table_args ::= AS select */
#line 186 "parse.y"
{
  sqlite3EndTable(pParse,0,0,0,yymsp[0].minor.yy171);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy171);
}
#line 2358 "parse.c"
        break;
      case 22: /* table_options ::= WITHOUT nm */
#line 192 "parse.y"
{
  if( yymsp[0].minor.yy0.n==5 && sqlite3_strnicmp(yymsp[0].minor.yy0.z,"rowid",5)==0 ){
    yymsp[-1].minor.yy284 = TF_WithoutRowid | TF_NoVisibleRowid;
  }else{
    yymsp[-1].minor.yy284 = 0;
    sqlite3ErrorMsg(pParse, "unknown table option: %.*s", yymsp[0].minor.yy0.n, yymsp[0].minor.yy0.z);
  }
}
#line 2370 "parse.c"
        break;
      case 23: /* columnname ::= nm typetoken */
#line 202 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2375 "parse.c"
        break;
      case 24: /* typetoken ::= */
      case 59: /* conslist_opt ::= */ yytestcase(yyruleno==59);
      case 95: /* as ::= */ yytestcase(yyruleno==95);
#line 243 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2382 "parse.c"
        break;
      case 25: /* typetoken ::= typename LP signed RP */
#line 245 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2389 "parse.c"
        break;
      case 26: /* typetoken ::= typename LP signed COMMA signed RP */
#line 248 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2396 "parse.c"
        break;
      case 27: /* typename ::= typename ID|STRING */
#line 253 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2401 "parse.c"
        break;
      case 28: /* ccons ::= CONSTRAINT nm */
      case 61: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==61);
#line 262 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2407 "parse.c"
        break;
      case 29: /* ccons ::= DEFAULT term */
      case 31: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==31);
#line 263 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy438);}
#line 2413 "parse.c"
        break;
      case 30: /* ccons ::= DEFAULT LP expr RP */
#line 264 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy438);}
#line 2418 "parse.c"
        break;
      case 32: /* ccons ::= DEFAULT MINUS term */
#line 266 "parse.y"
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy438.pExpr, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy438.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2429 "parse.c"
        break;
      case 33: /* ccons ::= DEFAULT ID|INDEXED */
#line 273 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2438 "parse.c"
        break;
      case 34: /* ccons ::= NOT NULL onconf */
#line 283 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy284);}
#line 2443 "parse.c"
        break;
      case 35: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 285 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy284,yymsp[0].minor.yy284,yymsp[-2].minor.yy284);}
#line 2448 "parse.c"
        break;
      case 36: /* ccons ::= UNIQUE onconf */
#line 286 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[0].minor.yy284,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2454 "parse.c"
        break;
      case 37: /* ccons ::= CHECK LP expr RP */
#line 288 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy438.pExpr);}
#line 2459 "parse.c"
        break;
      case 38: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 290 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy298,yymsp[0].minor.yy284);}
#line 2464 "parse.c"
        break;
      case 39: /* ccons ::= defer_subclause */
#line 291 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy284);}
#line 2469 "parse.c"
        break;
      case 40: /* ccons ::= COLLATE ID|STRING */
#line 292 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2474 "parse.c"
        break;
      case 42: /* autoinc ::= AUTOINCR */
#line 297 "parse.y"
{yymsp[0].minor.yy284 = 1;}
#line 2479 "parse.c"
        break;
      case 43: /* refargs ::= */
#line 305 "parse.y"
{ yymsp[1].minor.yy284 = OE_None*0x0101; /* EV: R-19803-45884 */}
#line 2484 "parse.c"
        break;
      case 44: /* refargs ::= refargs refarg */
#line 306 "parse.y"
{ yymsp[-1].minor.yy284 = (yymsp[-1].minor.yy284 & ~yymsp[0].minor.yy431.mask) | yymsp[0].minor.yy431.value; }
#line 2489 "parse.c"
        break;
      case 45: /* refarg ::= MATCH nm */
#line 308 "parse.y"
{ yymsp[-1].minor.yy431.value = 0;     yymsp[-1].minor.yy431.mask = 0x000000; }
#line 2494 "parse.c"
        break;
      case 46: /* refarg ::= ON INSERT refact */
#line 309 "parse.y"
{ yymsp[-2].minor.yy431.value = 0;     yymsp[-2].minor.yy431.mask = 0x000000; }
#line 2499 "parse.c"
        break;
      case 47: /* refarg ::= ON DELETE refact */
#line 310 "parse.y"
{ yymsp[-2].minor.yy431.value = yymsp[0].minor.yy284;     yymsp[-2].minor.yy431.mask = 0x0000ff; }
#line 2504 "parse.c"
        break;
      case 48: /* refarg ::= ON UPDATE refact */
#line 311 "parse.y"
{ yymsp[-2].minor.yy431.value = yymsp[0].minor.yy284<<8;  yymsp[-2].minor.yy431.mask = 0x00ff00; }
#line 2509 "parse.c"
        break;
      case 49: /* refact ::= SET NULL */
#line 313 "parse.y"
{ yymsp[-1].minor.yy284 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2514 "parse.c"
        break;
      case 50: /* refact ::= SET DEFAULT */
#line 314 "parse.y"
{ yymsp[-1].minor.yy284 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2519 "parse.c"
        break;
      case 51: /* refact ::= CASCADE */
#line 315 "parse.y"
{ yymsp[0].minor.yy284 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2524 "parse.c"
        break;
      case 52: /* refact ::= RESTRICT */
#line 316 "parse.y"
{ yymsp[0].minor.yy284 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2529 "parse.c"
        break;
      case 53: /* refact ::= NO ACTION */
#line 317 "parse.y"
{ yymsp[-1].minor.yy284 = OE_None;     /* EV: R-33326-45252 */}
#line 2534 "parse.c"
        break;
      case 54: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 319 "parse.y"
{yymsp[-2].minor.yy284 = 0;}
#line 2539 "parse.c"
        break;
      case 55: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 70: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==70);
      case 141: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==141);
#line 320 "parse.y"
{yymsp[-1].minor.yy284 = yymsp[0].minor.yy284;}
#line 2546 "parse.c"
        break;
      case 57: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 74: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==74);
      case 184: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==184);
      case 187: /* in_op ::= NOT IN */ yytestcase(yyruleno==187);
      case 213: /* collate ::= COLLATE ID|STRING */ yytestcase(yyruleno==213);
#line 323 "parse.y"
{yymsp[-1].minor.yy284 = 1;}
#line 2555 "parse.c"
        break;
      case 58: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 324 "parse.y"
{yymsp[-1].minor.yy284 = 0;}
#line 2560 "parse.c"
        break;
      case 60: /* tconscomma ::= COMMA */
#line 330 "parse.y"
{pParse->constraintName.n = 0;}
#line 2565 "parse.c"
        break;
      case 62: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 334 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy298,yymsp[0].minor.yy284,yymsp[-2].minor.yy284,0);}
#line 2570 "parse.c"
        break;
      case 63: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 336 "parse.y"
{sqlite3CreateIndex(pParse,0,0,yymsp[-2].minor.yy298,yymsp[0].minor.yy284,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2576 "parse.c"
        break;
      case 64: /* tcons ::= CHECK LP expr RP onconf */
#line 339 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy438.pExpr);}
#line 2581 "parse.c"
        break;
      case 65: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 341 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy298, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy298, yymsp[-1].minor.yy284);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy284);
}
#line 2589 "parse.c"
        break;
      case 67: /* onconf ::= */
      case 69: /* orconf ::= */ yytestcase(yyruleno==69);
#line 355 "parse.y"
{yymsp[1].minor.yy284 = OE_Default;}
#line 2595 "parse.c"
        break;
      case 68: /* onconf ::= ON CONFLICT resolvetype */
#line 356 "parse.y"
{yymsp[-2].minor.yy284 = yymsp[0].minor.yy284;}
#line 2600 "parse.c"
        break;
      case 71: /* resolvetype ::= IGNORE */
#line 360 "parse.y"
{yymsp[0].minor.yy284 = OE_Ignore;}
#line 2605 "parse.c"
        break;
      case 72: /* resolvetype ::= REPLACE */
      case 142: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==142);
#line 361 "parse.y"
{yymsp[0].minor.yy284 = OE_Replace;}
#line 2611 "parse.c"
        break;
      case 73: /* cmd ::= DROP TABLE ifexists fullname */
#line 365 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy339, 0, yymsp[-1].minor.yy284);
}
#line 2618 "parse.c"
        break;
      case 76: /* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 376 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-6].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy298, yymsp[0].minor.yy171, yymsp[-4].minor.yy284);
}
#line 2625 "parse.c"
        break;
      case 77: /* cmd ::= DROP VIEW ifexists fullname */
#line 379 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy339, 1, yymsp[-1].minor.yy284);
}
#line 2632 "parse.c"
        break;
      case 78: /* cmd ::= select */
#line 386 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy171, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy171);
}
#line 2641 "parse.c"
        break;
      case 79: /* select ::= with selectnowith */
#line 423 "parse.y"
{
  Select *p = yymsp[0].minor.yy171;
  if( p ){
    p->pWith = yymsp[-1].minor.yy323;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, yymsp[-1].minor.yy323);
  }
  yymsp[-1].minor.yy171 = p; /*A-overwrites-W*/
}
#line 2655 "parse.c"
        break;
      case 80: /* selectnowith ::= selectnowith multiselect_op oneselect */
#line 436 "parse.y"
{
  Select *pRhs = yymsp[0].minor.yy171;
  Select *pLhs = yymsp[-2].minor.yy171;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,0,&x,pRhs,0,0);
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)yymsp[-1].minor.yy284;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( yymsp[-1].minor.yy284!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  yymsp[-2].minor.yy171 = pRhs;
}
#line 2681 "parse.c"
        break;
      case 81: /* multiselect_op ::= UNION */
      case 83: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==83);
#line 459 "parse.y"
{yymsp[0].minor.yy284 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2687 "parse.c"
        break;
      case 82: /* multiselect_op ::= UNION ALL */
#line 460 "parse.y"
{yymsp[-1].minor.yy284 = TK_ALL;}
#line 2692 "parse.c"
        break;
      case 84: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 464 "parse.y"
{
#if SELECTTRACE_ENABLED
  Token s = yymsp[-8].minor.yy0; /*A-overwrites-S*/
#endif
  yymsp[-8].minor.yy171 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy298,yymsp[-5].minor.yy339,yymsp[-4].minor.yy258,yymsp[-3].minor.yy298,yymsp[-2].minor.yy258,yymsp[-1].minor.yy298,yymsp[-7].minor.yy284,yymsp[0].minor.yy452.pLimit,yymsp[0].minor.yy452.pOffset);
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
  if( yymsp[-8].minor.yy171!=0 ){
    const char *z = s.z+6;
    int i;
    sqlite3_snprintf(sizeof(yymsp[-8].minor.yy171->zSelName), yymsp[-8].minor.yy171->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(yymsp[-8].minor.yy171->zSelName), yymsp[-8].minor.yy171->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
#line 2726 "parse.c"
        break;
      case 85: /* values ::= VALUES LP nexprlist RP */
#line 498 "parse.y"
{
  yymsp[-3].minor.yy171 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy298,0,0,0,0,0,SF_Values,0,0);
}
#line 2733 "parse.c"
        break;
      case 86: /* values ::= values COMMA LP exprlist RP */
#line 501 "parse.y"
{
  Select *pRight, *pLeft = yymsp[-4].minor.yy171;
  pRight = sqlite3SelectNew(pParse,yymsp[-1].minor.yy298,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    yymsp[-4].minor.yy171 = pRight;
  }else{
    yymsp[-4].minor.yy171 = pLeft;
  }
}
#line 2749 "parse.c"
        break;
      case 87: /* distinct ::= DISTINCT */
#line 518 "parse.y"
{yymsp[0].minor.yy284 = SF_Distinct;}
#line 2754 "parse.c"
        break;
      case 88: /* distinct ::= ALL */
#line 519 "parse.y"
{yymsp[0].minor.yy284 = SF_All;}
#line 2759 "parse.c"
        break;
      case 90: /* sclp ::= */
      case 116: /* orderby_opt ::= */ yytestcase(yyruleno==116);
      case 123: /* groupby_opt ::= */ yytestcase(yyruleno==123);
      case 200: /* exprlist ::= */ yytestcase(yyruleno==200);
      case 203: /* paren_exprlist ::= */ yytestcase(yyruleno==203);
      case 208: /* eidlist_opt ::= */ yytestcase(yyruleno==208);
#line 532 "parse.y"
{yymsp[1].minor.yy298 = 0;}
#line 2769 "parse.c"
        break;
      case 91: /* selcollist ::= sclp expr as */
#line 533 "parse.y"
{
   yymsp[-2].minor.yy298 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy298, yymsp[-1].minor.yy438.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy298, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy298,&yymsp[-1].minor.yy438);
}
#line 2778 "parse.c"
        break;
      case 92: /* selcollist ::= sclp STAR */
#line 538 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy298 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy298, p);
}
#line 2786 "parse.c"
        break;
      case 93: /* selcollist ::= sclp nm DOT STAR */
#line 542 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy298 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy298, pDot);
}
#line 2796 "parse.c"
        break;
      case 94: /* as ::= AS nm */
      case 221: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==221);
      case 222: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==222);
#line 553 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2803 "parse.c"
        break;
      case 96: /* from ::= */
#line 567 "parse.y"
{yymsp[1].minor.yy339 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy339));}
#line 2808 "parse.c"
        break;
      case 97: /* from ::= FROM seltablist */
#line 568 "parse.y"
{
  yymsp[-1].minor.yy339 = yymsp[0].minor.yy339;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy339);
}
#line 2816 "parse.c"
        break;
      case 98: /* stl_prefix ::= seltablist joinop */
#line 576 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy339 && yymsp[-1].minor.yy339->nSrc>0) ) yymsp[-1].minor.yy339->a[yymsp[-1].minor.yy339->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy284;
}
#line 2823 "parse.c"
        break;
      case 99: /* stl_prefix ::= */
#line 579 "parse.y"
{yymsp[1].minor.yy339 = 0;}
#line 2828 "parse.c"
        break;
      case 100: /* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 581 "parse.y"
{
  yymsp[-5].minor.yy339 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-5].minor.yy339,&yymsp[-4].minor.yy0,0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy258,yymsp[0].minor.yy224);
  sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy339, &yymsp[-2].minor.yy0);
}
#line 2836 "parse.c"
        break;
      case 101: /* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 586 "parse.y"
{
  yymsp[-7].minor.yy339 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-7].minor.yy339,&yymsp[-6].minor.yy0,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy258,yymsp[0].minor.yy224);
  sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy339, yymsp[-4].minor.yy298);
}
#line 2844 "parse.c"
        break;
      case 102: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 592 "parse.y"
{
    yymsp[-6].minor.yy339 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy339,0,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy171,yymsp[-1].minor.yy258,yymsp[0].minor.yy224);
  }
#line 2851 "parse.c"
        break;
      case 103: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 596 "parse.y"
{
    if( yymsp[-6].minor.yy339==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy258==0 && yymsp[0].minor.yy224==0 ){
      yymsp[-6].minor.yy339 = yymsp[-4].minor.yy339;
    }else if( yymsp[-4].minor.yy339->nSrc==1 ){
      yymsp[-6].minor.yy339 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy339,0,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy258,yymsp[0].minor.yy224);
      if( yymsp[-6].minor.yy339 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy339->a[yymsp[-6].minor.yy339->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy339->a;
        pNew->zName = pOld->zName;
        pNew->zDatabase = pOld->zDatabase;
        pNew->pSelect = pOld->pSelect;
        pOld->zName = pOld->zDatabase = 0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy339);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy339);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy339,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy339 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy339,0,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy258,yymsp[0].minor.yy224);
    }
  }
#line 2877 "parse.c"
        break;
      case 104: /* fullname ::= nm */
#line 623 "parse.y"
{yymsp[0].minor.yy339 = sqlite3SrcListAppend(pParse->db,0,&yymsp[0].minor.yy0,0); /*A-overwrites-X*/}
#line 2882 "parse.c"
        break;
      case 105: /* joinop ::= COMMA|JOIN */
#line 626 "parse.y"
{ yymsp[0].minor.yy284 = JT_INNER; }
#line 2887 "parse.c"
        break;
      case 106: /* joinop ::= JOIN_KW JOIN */
#line 628 "parse.y"
{yymsp[-1].minor.yy284 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2892 "parse.c"
        break;
      case 107: /* joinop ::= JOIN_KW nm JOIN */
#line 630 "parse.y"
{yymsp[-2].minor.yy284 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2897 "parse.c"
        break;
      case 108: /* joinop ::= JOIN_KW nm nm JOIN */
#line 632 "parse.y"
{yymsp[-3].minor.yy284 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2902 "parse.c"
        break;
      case 109: /* on_opt ::= ON expr */
      case 126: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==126);
      case 133: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==133);
      case 196: /* case_else ::= ELSE expr */ yytestcase(yyruleno==196);
#line 636 "parse.y"
{yymsp[-1].minor.yy258 = yymsp[0].minor.yy438.pExpr;}
#line 2910 "parse.c"
        break;
      case 110: /* on_opt ::= */
      case 125: /* having_opt ::= */ yytestcase(yyruleno==125);
      case 132: /* where_opt ::= */ yytestcase(yyruleno==132);
      case 197: /* case_else ::= */ yytestcase(yyruleno==197);
      case 199: /* case_operand ::= */ yytestcase(yyruleno==199);
#line 637 "parse.y"
{yymsp[1].minor.yy258 = 0;}
#line 2919 "parse.c"
        break;
      case 111: /* indexed_opt ::= */
#line 650 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2924 "parse.c"
        break;
      case 112: /* indexed_opt ::= INDEXED BY nm */
#line 651 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2929 "parse.c"
        break;
      case 113: /* indexed_opt ::= NOT INDEXED */
#line 652 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2934 "parse.c"
        break;
      case 114: /* using_opt ::= USING LP idlist RP */
#line 656 "parse.y"
{yymsp[-3].minor.yy224 = yymsp[-1].minor.yy224;}
#line 2939 "parse.c"
        break;
      case 115: /* using_opt ::= */
      case 143: /* idlist_opt ::= */ yytestcase(yyruleno==143);
#line 657 "parse.y"
{yymsp[1].minor.yy224 = 0;}
#line 2945 "parse.c"
        break;
      case 117: /* orderby_opt ::= ORDER BY sortlist */
      case 124: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==124);
#line 671 "parse.y"
{yymsp[-2].minor.yy298 = yymsp[0].minor.yy298;}
#line 2951 "parse.c"
        break;
      case 118: /* sortlist ::= sortlist COMMA expr sortorder */
#line 672 "parse.y"
{
  yymsp[-3].minor.yy298 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy298,yymsp[-1].minor.yy438.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy298,yymsp[0].minor.yy284);
}
#line 2959 "parse.c"
        break;
      case 119: /* sortlist ::= expr sortorder */
#line 676 "parse.y"
{
  yymsp[-1].minor.yy298 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy438.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy298,yymsp[0].minor.yy284);
}
#line 2967 "parse.c"
        break;
      case 120: /* sortorder ::= ASC */
#line 683 "parse.y"
{yymsp[0].minor.yy284 = SQLITE_SO_ASC;}
#line 2972 "parse.c"
        break;
      case 121: /* sortorder ::= DESC */
#line 684 "parse.y"
{yymsp[0].minor.yy284 = SQLITE_SO_DESC;}
#line 2977 "parse.c"
        break;
      case 122: /* sortorder ::= */
#line 685 "parse.y"
{yymsp[1].minor.yy284 = SQLITE_SO_UNDEFINED;}
#line 2982 "parse.c"
        break;
      case 127: /* limit_opt ::= */
#line 710 "parse.y"
{yymsp[1].minor.yy452.pLimit = 0; yymsp[1].minor.yy452.pOffset = 0;}
#line 2987 "parse.c"
        break;
      case 128: /* limit_opt ::= LIMIT expr */
#line 711 "parse.y"
{yymsp[-1].minor.yy452.pLimit = yymsp[0].minor.yy438.pExpr; yymsp[-1].minor.yy452.pOffset = 0;}
#line 2992 "parse.c"
        break;
      case 129: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 713 "parse.y"
{yymsp[-3].minor.yy452.pLimit = yymsp[-2].minor.yy438.pExpr; yymsp[-3].minor.yy452.pOffset = yymsp[0].minor.yy438.pExpr;}
#line 2997 "parse.c"
        break;
      case 130: /* limit_opt ::= LIMIT expr COMMA expr */
#line 715 "parse.y"
{yymsp[-3].minor.yy452.pOffset = yymsp[-2].minor.yy438.pExpr; yymsp[-3].minor.yy452.pLimit = yymsp[0].minor.yy438.pExpr;}
#line 3002 "parse.c"
        break;
      case 131: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 732 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy323, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy339, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy339,yymsp[0].minor.yy258);
}
#line 3014 "parse.c"
        break;
      case 134: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 765 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy323, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy339, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy298,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Update(pParse,yymsp[-4].minor.yy339,yymsp[-1].minor.yy298,yymsp[0].minor.yy258,yymsp[-5].minor.yy284);
}
#line 3027 "parse.c"
        break;
      case 135: /* setlist ::= setlist COMMA nm EQ expr */
#line 779 "parse.y"
{
  yymsp[-4].minor.yy298 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy298, yymsp[0].minor.yy438.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy298, &yymsp[-2].minor.yy0, 1);
}
#line 3035 "parse.c"
        break;
      case 136: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 783 "parse.y"
{
  yymsp[-6].minor.yy298 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy298, yymsp[-3].minor.yy224, yymsp[0].minor.yy438.pExpr);
}
#line 3042 "parse.c"
        break;
      case 137: /* setlist ::= nm EQ expr */
#line 786 "parse.y"
{
  yylhsminor.yy298 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy438.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy298, &yymsp[-2].minor.yy0, 1);
}
#line 3050 "parse.c"
  yymsp[-2].minor.yy298 = yylhsminor.yy298;
        break;
      case 138: /* setlist ::= LP idlist RP EQ expr */
#line 790 "parse.y"
{
  yymsp[-4].minor.yy298 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy224, yymsp[0].minor.yy438.pExpr);
}
#line 3058 "parse.c"
        break;
      case 139: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 796 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy323, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-2].minor.yy339, yymsp[0].minor.yy171, yymsp[-1].minor.yy224, yymsp[-4].minor.yy284);
}
#line 3069 "parse.c"
        break;
      case 140: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 804 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy323, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-3].minor.yy339, 0, yymsp[-2].minor.yy224, yymsp[-5].minor.yy284);
}
#line 3080 "parse.c"
        break;
      case 144: /* idlist_opt ::= LP idlist RP */
#line 822 "parse.y"
{yymsp[-2].minor.yy224 = yymsp[-1].minor.yy224;}
#line 3085 "parse.c"
        break;
      case 145: /* idlist ::= idlist COMMA nm */
#line 824 "parse.y"
{yymsp[-2].minor.yy224 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy224,&yymsp[0].minor.yy0);}
#line 3090 "parse.c"
        break;
      case 146: /* idlist ::= nm */
#line 826 "parse.y"
{yymsp[0].minor.yy224 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3095 "parse.c"
        break;
      case 147: /* expr ::= LP expr RP */
#line 876 "parse.y"
{spanSet(&yymsp[-2].minor.yy438,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy438.pExpr = yymsp[-1].minor.yy438.pExpr;}
#line 3100 "parse.c"
        break;
      case 148: /* term ::= NULL */
      case 153: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==153);
      case 154: /* term ::= STRING */ yytestcase(yyruleno==154);
#line 877 "parse.y"
{spanExpr(&yymsp[0].minor.yy438,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3107 "parse.c"
        break;
      case 149: /* expr ::= ID|INDEXED */
      case 150: /* expr ::= JOIN_KW */ yytestcase(yyruleno==150);
#line 878 "parse.y"
{spanExpr(&yymsp[0].minor.yy438,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3113 "parse.c"
        break;
      case 151: /* expr ::= nm DOT nm */
#line 880 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy438,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3123 "parse.c"
        break;
      case 152: /* expr ::= nm DOT nm DOT nm */
#line 886 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-4].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp3 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  Expr *temp4 = sqlite3PExpr(pParse, TK_DOT, temp2, temp3);
  spanSet(&yymsp[-4].minor.yy438,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-4].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp4);
}
#line 3135 "parse.c"
        break;
      case 155: /* term ::= INTEGER */
#line 896 "parse.y"
{
  yylhsminor.yy438.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy438.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy438.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy438.pExpr ) yylhsminor.yy438.pExpr->flags |= EP_Leaf;
}
#line 3145 "parse.c"
  yymsp[0].minor.yy438 = yylhsminor.yy438;
        break;
      case 156: /* expr ::= VARIABLE */
#line 902 "parse.y"
{
  if( !(yymsp[0].minor.yy0.z[0]=='#' && sqlite3Isdigit(yymsp[0].minor.yy0.z[1])) ){
    u32 n = yymsp[0].minor.yy0.n;
    spanExpr(&yymsp[0].minor.yy438, pParse, TK_VARIABLE, yymsp[0].minor.yy0);
    sqlite3ExprAssignVarNumber(pParse, yymsp[0].minor.yy438.pExpr, n);
  }else{
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    Token t = yymsp[0].minor.yy0; /*A-overwrites-X*/
    assert( t.n>=2 );
    spanSet(&yymsp[0].minor.yy438, &t, &t);
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &t);
      yymsp[0].minor.yy438.pExpr = 0;
    }else{
      yymsp[0].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0);
      if( yymsp[0].minor.yy438.pExpr ) sqlite3GetInt32(&t.z[1], &yymsp[0].minor.yy438.pExpr->iTable);
    }
  }
}
#line 3171 "parse.c"
        break;
      case 157: /* expr ::= expr COLLATE ID|STRING */
#line 923 "parse.y"
{
  yymsp[-2].minor.yy438.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy438.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy438.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3179 "parse.c"
        break;
      case 158: /* expr ::= CAST LP expr AS typetoken RP */
#line 928 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy438,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy438.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy438.pExpr, yymsp[-3].minor.yy438.pExpr, 0);
}
#line 3188 "parse.c"
        break;
      case 159: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 934 "parse.y"
{
  if( yymsp[-1].minor.yy298 && yymsp[-1].minor.yy298->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yylhsminor.yy438.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy298, &yymsp[-4].minor.yy0);
  spanSet(&yylhsminor.yy438,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy284==SF_Distinct && yylhsminor.yy438.pExpr ){
    yylhsminor.yy438.pExpr->flags |= EP_Distinct;
  }
}
#line 3202 "parse.c"
  yymsp[-4].minor.yy438 = yylhsminor.yy438;
        break;
      case 160: /* expr ::= ID|INDEXED LP STAR RP */
#line 944 "parse.y"
{
  yylhsminor.yy438.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy438,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3211 "parse.c"
  yymsp[-3].minor.yy438 = yylhsminor.yy438;
        break;
      case 161: /* term ::= CTIME_KW */
#line 948 "parse.y"
{
  yylhsminor.yy438.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy438, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3220 "parse.c"
  yymsp[0].minor.yy438 = yylhsminor.yy438;
        break;
      case 162: /* expr ::= LP nexprlist COMMA expr RP */
#line 977 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy298, yymsp[-1].minor.yy438.pExpr);
  yylhsminor.yy438.pExpr = sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
  if( yylhsminor.yy438.pExpr ){
    yylhsminor.yy438.pExpr->x.pList = pList;
    spanSet(&yylhsminor.yy438, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  }
}
#line 3235 "parse.c"
  yymsp[-4].minor.yy438 = yylhsminor.yy438;
        break;
      case 163: /* expr ::= expr AND expr */
      case 164: /* expr ::= expr OR expr */ yytestcase(yyruleno==164);
      case 165: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==165);
      case 166: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==166);
      case 167: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==167);
      case 168: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==168);
      case 169: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==169);
      case 170: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==170);
#line 988 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy438,&yymsp[0].minor.yy438);}
#line 3248 "parse.c"
        break;
      case 171: /* likeop ::= LIKE_KW|MATCH */
#line 1001 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3253 "parse.c"
        break;
      case 172: /* likeop ::= NOT LIKE_KW|MATCH */
#line 1002 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3258 "parse.c"
        break;
      case 173: /* expr ::= expr likeop expr */
#line 1003 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
  yymsp[-1].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy438.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy438.pExpr);
  yymsp[-2].minor.yy438.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-2].minor.yy438);
  yymsp[-2].minor.yy438.zEnd = yymsp[0].minor.yy438.zEnd;
  if( yymsp[-2].minor.yy438.pExpr ) yymsp[-2].minor.yy438.pExpr->flags |= EP_InfixFunc;
}
#line 3273 "parse.c"
        break;
      case 174: /* expr ::= expr likeop expr ESCAPE expr */
#line 1014 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
  yymsp[-3].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy438.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy438.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy438.pExpr);
  yymsp[-4].minor.yy438.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-4].minor.yy438);
  yymsp[-4].minor.yy438.zEnd = yymsp[0].minor.yy438.zEnd;
  if( yymsp[-4].minor.yy438.pExpr ) yymsp[-4].minor.yy438.pExpr->flags |= EP_InfixFunc;
}
#line 3289 "parse.c"
        break;
      case 175: /* expr ::= expr ISNULL|NOTNULL */
#line 1041 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy438,&yymsp[0].minor.yy0);}
#line 3294 "parse.c"
        break;
      case 176: /* expr ::= expr NOT NULL */
#line 1042 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy438,&yymsp[0].minor.yy0);}
#line 3299 "parse.c"
        break;
      case 177: /* expr ::= expr IS expr */
#line 1063 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy438,&yymsp[0].minor.yy438);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy438.pExpr, yymsp[-2].minor.yy438.pExpr, TK_ISNULL);
}
#line 3307 "parse.c"
        break;
      case 178: /* expr ::= expr IS NOT expr */
#line 1067 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy438,&yymsp[0].minor.yy438);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy438.pExpr, yymsp[-3].minor.yy438.pExpr, TK_NOTNULL);
}
#line 3315 "parse.c"
        break;
      case 179: /* expr ::= NOT expr */
      case 180: /* expr ::= BITNOT expr */ yytestcase(yyruleno==180);
#line 1091 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy438,pParse,yymsp[-1].major,&yymsp[0].minor.yy438,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3321 "parse.c"
        break;
      case 181: /* expr ::= MINUS expr */
#line 1095 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy438,pParse,TK_UMINUS,&yymsp[0].minor.yy438,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3326 "parse.c"
        break;
      case 182: /* expr ::= PLUS expr */
#line 1097 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy438,pParse,TK_UPLUS,&yymsp[0].minor.yy438,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3331 "parse.c"
        break;
      case 183: /* between_op ::= BETWEEN */
      case 186: /* in_op ::= IN */ yytestcase(yyruleno==186);
#line 1100 "parse.y"
{yymsp[0].minor.yy284 = 0;}
#line 3337 "parse.c"
        break;
      case 185: /* expr ::= expr between_op expr AND expr */
#line 1102 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy438.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy438.pExpr);
  yymsp[-4].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy438.pExpr, 0);
  if( yymsp[-4].minor.yy438.pExpr ){
    yymsp[-4].minor.yy438.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, yymsp[-3].minor.yy284, &yymsp[-4].minor.yy438);
  yymsp[-4].minor.yy438.zEnd = yymsp[0].minor.yy438.zEnd;
}
#line 3353 "parse.c"
        break;
      case 188: /* expr ::= expr in_op LP exprlist RP */
#line 1118 "parse.y"
{
    if( yymsp[-1].minor.yy298==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      sqlite3ExprDelete(pParse->db, yymsp[-4].minor.yy438.pExpr);
      yymsp[-4].minor.yy438.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER,&sqlite3IntTokens[yymsp[-3].minor.yy284],1);
    }else if( yymsp[-1].minor.yy298->nExpr==1 ){
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
      Expr *pRHS = yymsp[-1].minor.yy298->a[0].pExpr;
      yymsp[-1].minor.yy298->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy298);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      yymsp[-4].minor.yy438.pExpr = sqlite3PExpr(pParse, yymsp[-3].minor.yy284 ? TK_NE : TK_EQ, yymsp[-4].minor.yy438.pExpr, pRHS);
    }else{
      yymsp[-4].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy438.pExpr, 0);
      if( yymsp[-4].minor.yy438.pExpr ){
        yymsp[-4].minor.yy438.pExpr->x.pList = yymsp[-1].minor.yy298;
        sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy438.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy298);
      }
      exprNot(pParse, yymsp[-3].minor.yy284, &yymsp[-4].minor.yy438);
    }
    yymsp[-4].minor.yy438.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3408 "parse.c"
        break;
      case 189: /* expr ::= LP select RP */
#line 1169 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy438,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy438.pExpr, yymsp[-1].minor.yy171);
  }
#line 3417 "parse.c"
        break;
      case 190: /* expr ::= expr in_op LP select RP */
#line 1174 "parse.y"
{
    yymsp[-4].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy438.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy438.pExpr, yymsp[-1].minor.yy171);
    exprNot(pParse, yymsp[-3].minor.yy284, &yymsp[-4].minor.yy438);
    yymsp[-4].minor.yy438.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3427 "parse.c"
        break;
      case 191: /* expr ::= expr in_op nm paren_exprlist */
#line 1180 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0,0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy298 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy298);
    yymsp[-3].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy438.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-3].minor.yy438.pExpr, pSelect);
    exprNot(pParse, yymsp[-2].minor.yy284, &yymsp[-3].minor.yy438);
    yymsp[-3].minor.yy438.zEnd = &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
#line 3440 "parse.c"
        break;
      case 192: /* expr ::= EXISTS LP select RP */
#line 1189 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy438,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy171);
  }
#line 3450 "parse.c"
        break;
      case 193: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1198 "parse.y"
{
  spanSet(&yymsp[-4].minor.yy438,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-C*/
  yymsp[-4].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy258, 0);
  if( yymsp[-4].minor.yy438.pExpr ){
    yymsp[-4].minor.yy438.pExpr->x.pList = yymsp[-1].minor.yy258 ? sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy298,yymsp[-1].minor.yy258) : yymsp[-2].minor.yy298;
    sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy438.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy298);
    sqlite3ExprDelete(pParse->db, yymsp[-1].minor.yy258);
  }
}
#line 3465 "parse.c"
        break;
      case 194: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1211 "parse.y"
{
  yymsp[-4].minor.yy298 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy298, yymsp[-2].minor.yy438.pExpr);
  yymsp[-4].minor.yy298 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy298, yymsp[0].minor.yy438.pExpr);
}
#line 3473 "parse.c"
        break;
      case 195: /* case_exprlist ::= WHEN expr THEN expr */
#line 1215 "parse.y"
{
  yymsp[-3].minor.yy298 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy438.pExpr);
  yymsp[-3].minor.yy298 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy298, yymsp[0].minor.yy438.pExpr);
}
#line 3481 "parse.c"
        break;
      case 198: /* case_operand ::= expr */
#line 1225 "parse.y"
{yymsp[0].minor.yy258 = yymsp[0].minor.yy438.pExpr; /*A-overwrites-X*/}
#line 3486 "parse.c"
        break;
      case 201: /* nexprlist ::= nexprlist COMMA expr */
#line 1236 "parse.y"
{yymsp[-2].minor.yy298 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy298,yymsp[0].minor.yy438.pExpr);}
#line 3491 "parse.c"
        break;
      case 202: /* nexprlist ::= expr */
#line 1238 "parse.y"
{yymsp[0].minor.yy298 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy438.pExpr); /*A-overwrites-Y*/}
#line 3496 "parse.c"
        break;
      case 204: /* paren_exprlist ::= LP exprlist RP */
      case 209: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==209);
#line 1246 "parse.y"
{yymsp[-2].minor.yy298 = yymsp[-1].minor.yy298;}
#line 3502 "parse.c"
        break;
      case 205: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1253 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0,0), yymsp[-2].minor.yy298, yymsp[-9].minor.yy284,
                      &yymsp[-10].minor.yy0, yymsp[0].minor.yy258, SQLITE_SO_ASC, yymsp[-7].minor.yy284, SQLITE_IDXTYPE_APPDEF);
}
#line 3511 "parse.c"
        break;
      case 206: /* uniqueflag ::= UNIQUE */
      case 246: /* raisetype ::= ABORT */ yytestcase(yyruleno==246);
#line 1260 "parse.y"
{yymsp[0].minor.yy284 = OE_Abort;}
#line 3517 "parse.c"
        break;
      case 207: /* uniqueflag ::= */
#line 1261 "parse.y"
{yymsp[1].minor.yy284 = OE_None;}
#line 3522 "parse.c"
        break;
      case 210: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1311 "parse.y"
{
  yymsp[-4].minor.yy298 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy298, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy284, yymsp[0].minor.yy284);
}
#line 3529 "parse.c"
        break;
      case 211: /* eidlist ::= nm collate sortorder */
#line 1314 "parse.y"
{
  yymsp[-2].minor.yy298 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy284, yymsp[0].minor.yy284); /*A-overwrites-Y*/
}
#line 3536 "parse.c"
        break;
      case 214: /* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1325 "parse.y"
{
    sqlite3DropIndex(pParse, yymsp[-2].minor.yy339, &yymsp[0].minor.yy0, yymsp[-3].minor.yy284);
}
#line 3543 "parse.c"
        break;
      case 215: /* cmd ::= PRAGMA nm */
#line 1332 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[0].minor.yy0,0,0,0,0);
}
#line 3550 "parse.c"
        break;
      case 216: /* cmd ::= PRAGMA nm EQ nmnum */
#line 1335 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,0);
}
#line 3557 "parse.c"
        break;
      case 217: /* cmd ::= PRAGMA nm LP nmnum RP */
#line 1338 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,0);
}
#line 3564 "parse.c"
        break;
      case 218: /* cmd ::= PRAGMA nm EQ minus_num */
#line 1341 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,1);
}
#line 3571 "parse.c"
        break;
      case 219: /* cmd ::= PRAGMA nm LP minus_num RP */
#line 1344 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,1);
}
#line 3578 "parse.c"
        break;
      case 220: /* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1347 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,0,&yymsp[0].minor.yy0,&yymsp[-2].minor.yy0,0);
}
#line 3585 "parse.c"
        break;
      case 223: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1366 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy251, &all);
}
#line 3595 "parse.c"
        break;
      case 224: /* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1375 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy284, yymsp[-4].minor.yy114.a, yymsp[-4].minor.yy114.b, yymsp[-2].minor.yy339, yymsp[0].minor.yy258, yymsp[-7].minor.yy284);
  yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0; /*yymsp[-8].minor.yy0-overwrites-T*/
}
#line 3603 "parse.c"
        break;
      case 225: /* trigger_time ::= BEFORE */
#line 1381 "parse.y"
{ yymsp[0].minor.yy284 = TK_BEFORE; }
#line 3608 "parse.c"
        break;
      case 226: /* trigger_time ::= AFTER */
#line 1382 "parse.y"
{ yymsp[0].minor.yy284 = TK_AFTER;  }
#line 3613 "parse.c"
        break;
      case 227: /* trigger_time ::= INSTEAD OF */
#line 1383 "parse.y"
{ yymsp[-1].minor.yy284 = TK_INSTEAD;}
#line 3618 "parse.c"
        break;
      case 228: /* trigger_time ::= */
#line 1384 "parse.y"
{ yymsp[1].minor.yy284 = TK_BEFORE; }
#line 3623 "parse.c"
        break;
      case 229: /* trigger_event ::= DELETE|INSERT */
      case 230: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==230);
#line 1388 "parse.y"
{yymsp[0].minor.yy114.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy114.b = 0;}
#line 3629 "parse.c"
        break;
      case 231: /* trigger_event ::= UPDATE OF idlist */
#line 1390 "parse.y"
{yymsp[-2].minor.yy114.a = TK_UPDATE; yymsp[-2].minor.yy114.b = yymsp[0].minor.yy224;}
#line 3634 "parse.c"
        break;
      case 232: /* when_clause ::= */
#line 1397 "parse.y"
{ yymsp[1].minor.yy258 = 0; }
#line 3639 "parse.c"
        break;
      case 233: /* when_clause ::= WHEN expr */
#line 1398 "parse.y"
{ yymsp[-1].minor.yy258 = yymsp[0].minor.yy438.pExpr; }
#line 3644 "parse.c"
        break;
      case 234: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1402 "parse.y"
{
  assert( yymsp[-2].minor.yy251!=0 );
  yymsp[-2].minor.yy251->pLast->pNext = yymsp[-1].minor.yy251;
  yymsp[-2].minor.yy251->pLast = yymsp[-1].minor.yy251;
}
#line 3653 "parse.c"
        break;
      case 235: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1407 "parse.y"
{ 
  assert( yymsp[-1].minor.yy251!=0 );
  yymsp[-1].minor.yy251->pLast = yymsp[-1].minor.yy251;
}
#line 3661 "parse.c"
        break;
      case 236: /* trnm ::= nm DOT nm */
#line 1418 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3671 "parse.c"
        break;
      case 237: /* tridxby ::= INDEXED BY nm */
#line 1430 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3680 "parse.c"
        break;
      case 238: /* tridxby ::= NOT INDEXED */
#line 1435 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3689 "parse.c"
        break;
      case 239: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1448 "parse.y"
{yymsp[-6].minor.yy251 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy298, yymsp[0].minor.yy258, yymsp[-5].minor.yy284);}
#line 3694 "parse.c"
        break;
      case 240: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1452 "parse.y"
{yymsp[-4].minor.yy251 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy224, yymsp[0].minor.yy171, yymsp[-4].minor.yy284);/*A-overwrites-R*/}
#line 3699 "parse.c"
        break;
      case 241: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1456 "parse.y"
{yymsp[-4].minor.yy251 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy258);}
#line 3704 "parse.c"
        break;
      case 242: /* trigger_cmd ::= select */
#line 1460 "parse.y"
{yymsp[0].minor.yy251 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy171); /*A-overwrites-X*/}
#line 3709 "parse.c"
        break;
      case 243: /* expr ::= RAISE LP IGNORE RP */
#line 1463 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy438,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy438.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy438.pExpr ){
    yymsp[-3].minor.yy438.pExpr->affinity = OE_Ignore;
  }
}
#line 3720 "parse.c"
        break;
      case 244: /* expr ::= RAISE LP raisetype COMMA nm RP */
#line 1470 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy438,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy438.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy438.pExpr ) {
    yymsp[-5].minor.yy438.pExpr->affinity = (char)yymsp[-3].minor.yy284;
  }
}
#line 3731 "parse.c"
        break;
      case 245: /* raisetype ::= ROLLBACK */
#line 1480 "parse.y"
{yymsp[0].minor.yy284 = OE_Rollback;}
#line 3736 "parse.c"
        break;
      case 247: /* raisetype ::= FAIL */
#line 1482 "parse.y"
{yymsp[0].minor.yy284 = OE_Fail;}
#line 3741 "parse.c"
        break;
      case 248: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1487 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy339,yymsp[-1].minor.yy284);
}
#line 3748 "parse.c"
        break;
      case 249: /* cmd ::= REINDEX */
#line 1494 "parse.y"
{sqlite3Reindex(pParse, 0, 0);}
#line 3753 "parse.c"
        break;
      case 250: /* cmd ::= REINDEX nm */
#line 1495 "parse.y"
{sqlite3Reindex(pParse, &yymsp[0].minor.yy0, 0);}
#line 3758 "parse.c"
        break;
      case 251: /* cmd ::= REINDEX nm ON nm */
#line 1496 "parse.y"
{sqlite3Reindex(pParse, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);}
#line 3763 "parse.c"
        break;
      case 252: /* cmd ::= ANALYZE */
#line 1501 "parse.y"
{sqlite3Analyze(pParse, 0);}
#line 3768 "parse.c"
        break;
      case 253: /* cmd ::= ANALYZE nm */
#line 1502 "parse.y"
{sqlite3Analyze(pParse, &yymsp[0].minor.yy0);}
#line 3773 "parse.c"
        break;
      case 254: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1507 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy339,&yymsp[0].minor.yy0);
}
#line 3780 "parse.c"
        break;
      case 255: /* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist */
#line 1511 "parse.y"
{
  yymsp[-1].minor.yy0.n = (int)(pParse->sLastToken.z-yymsp[-1].minor.yy0.z) + pParse->sLastToken.n;
  sqlite3AlterFinishAddColumn(pParse, &yymsp[-1].minor.yy0);
}
#line 3788 "parse.c"
        break;
      case 256: /* add_column_fullname ::= fullname */
#line 1515 "parse.y"
{
  disableLookaside(pParse);
  sqlite3AlterBeginAddColumn(pParse, yymsp[0].minor.yy339);
}
#line 3796 "parse.c"
        break;
      case 257: /* cmd ::= create_vtab */
#line 1525 "parse.y"
{sqlite3VtabFinishParse(pParse,0);}
#line 3801 "parse.c"
        break;
      case 258: /* cmd ::= create_vtab LP vtabarglist RP */
#line 1526 "parse.y"
{sqlite3VtabFinishParse(pParse,&yymsp[0].minor.yy0);}
#line 3806 "parse.c"
        break;
      case 259: /* create_vtab ::= createkw VIRTUAL TABLE ifnotexists nm USING nm */
#line 1528 "parse.y"
{
    sqlite3VtabBeginParse(pParse, &yymsp[-2].minor.yy0, 0, &yymsp[0].minor.yy0, yymsp[-3].minor.yy284);
}
#line 3813 "parse.c"
        break;
      case 260: /* vtabarg ::= */
#line 1533 "parse.y"
{sqlite3VtabArgInit(pParse);}
#line 3818 "parse.c"
        break;
      case 261: /* vtabargtoken ::= ANY */
      case 262: /* vtabargtoken ::= lp anylist RP */ yytestcase(yyruleno==262);
      case 263: /* lp ::= LP */ yytestcase(yyruleno==263);
#line 1535 "parse.y"
{sqlite3VtabArgExtend(pParse,&yymsp[0].minor.yy0);}
#line 3825 "parse.c"
        break;
      case 264: /* with ::= */
#line 1550 "parse.y"
{yymsp[1].minor.yy323 = 0;}
#line 3830 "parse.c"
        break;
      case 265: /* with ::= WITH wqlist */
#line 1552 "parse.y"
{ yymsp[-1].minor.yy323 = yymsp[0].minor.yy323; }
#line 3835 "parse.c"
        break;
      case 266: /* with ::= WITH RECURSIVE wqlist */
#line 1553 "parse.y"
{ yymsp[-2].minor.yy323 = yymsp[0].minor.yy323; }
#line 3840 "parse.c"
        break;
      case 267: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1555 "parse.y"
{
  yymsp[-5].minor.yy323 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy298, yymsp[-1].minor.yy171); /*A-overwrites-X*/
}
#line 3847 "parse.c"
        break;
      case 268: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1558 "parse.y"
{
  yymsp[-7].minor.yy323 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy323, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy298, yymsp[-1].minor.yy171);
}
#line 3854 "parse.c"
        break;
      default:
      /* (269) input ::= ecmd */ yytestcase(yyruleno==269);
      /* (270) explain ::= */ yytestcase(yyruleno==270);
      /* (271) cmdx ::= cmd (OPTIMIZED OUT) */ assert(yyruleno!=271);
      /* (272) trans_opt ::= */ yytestcase(yyruleno==272);
      /* (273) trans_opt ::= TRANSACTION */ yytestcase(yyruleno==273);
      /* (274) trans_opt ::= TRANSACTION nm */ yytestcase(yyruleno==274);
      /* (275) savepoint_opt ::= SAVEPOINT */ yytestcase(yyruleno==275);
      /* (276) savepoint_opt ::= */ yytestcase(yyruleno==276);
      /* (277) cmd ::= create_table create_table_args */ yytestcase(yyruleno==277);
      /* (278) columnlist ::= columnlist COMMA columnname carglist */ yytestcase(yyruleno==278);
      /* (279) columnlist ::= columnname carglist */ yytestcase(yyruleno==279);
      /* (280) nm ::= ID|INDEXED */ yytestcase(yyruleno==280);
      /* (281) nm ::= STRING */ yytestcase(yyruleno==281);
      /* (282) nm ::= JOIN_KW */ yytestcase(yyruleno==282);
      /* (283) typetoken ::= typename */ yytestcase(yyruleno==283);
      /* (284) typename ::= ID|STRING */ yytestcase(yyruleno==284);
      /* (285) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=285);
      /* (286) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=286);
      /* (287) carglist ::= carglist ccons */ yytestcase(yyruleno==287);
      /* (288) carglist ::= */ yytestcase(yyruleno==288);
      /* (289) ccons ::= NULL onconf */ yytestcase(yyruleno==289);
      /* (290) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==290);
      /* (291) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==291);
      /* (292) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=292);
      /* (293) tconscomma ::= */ yytestcase(yyruleno==293);
      /* (294) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=294);
      /* (295) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=295);
      /* (296) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=296);
      /* (297) oneselect ::= values */ yytestcase(yyruleno==297);
      /* (298) sclp ::= selcollist COMMA */ yytestcase(yyruleno==298);
      /* (299) as ::= ID|STRING */ yytestcase(yyruleno==299);
      /* (300) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=300);
      /* (301) exprlist ::= nexprlist */ yytestcase(yyruleno==301);
      /* (302) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=302);
      /* (303) nmnum ::= nm */ yytestcase(yyruleno==303);
      /* (304) nmnum ::= ON */ yytestcase(yyruleno==304);
      /* (305) nmnum ::= DELETE */ yytestcase(yyruleno==305);
      /* (306) nmnum ::= DEFAULT */ yytestcase(yyruleno==306);
      /* (307) plus_num ::= INTEGER|FLOAT */ yytestcase(yyruleno==307);
      /* (308) foreach_clause ::= */ yytestcase(yyruleno==308);
      /* (309) foreach_clause ::= FOR EACH ROW */ yytestcase(yyruleno==309);
      /* (310) trnm ::= nm */ yytestcase(yyruleno==310);
      /* (311) tridxby ::= */ yytestcase(yyruleno==311);
      /* (312) kwcolumn_opt ::= */ yytestcase(yyruleno==312);
      /* (313) kwcolumn_opt ::= COLUMNKW */ yytestcase(yyruleno==313);
      /* (314) vtabarglist ::= vtabarg */ yytestcase(yyruleno==314);
      /* (315) vtabarglist ::= vtabarglist COMMA vtabarg */ yytestcase(yyruleno==315);
      /* (316) vtabarg ::= vtabarg vtabargtoken */ yytestcase(yyruleno==316);
      /* (317) anylist ::= */ yytestcase(yyruleno==317);
      /* (318) anylist ::= anylist LP anylist RP */ yytestcase(yyruleno==318);
      /* (319) anylist ::= anylist ANY */ yytestcase(yyruleno==319);
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
#line 3969 "parse.c"
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
