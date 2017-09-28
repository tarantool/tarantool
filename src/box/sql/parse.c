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

#line 397 "parse.y"

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
#line 834 "parse.y"

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
#line 951 "parse.y"

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
#line 1025 "parse.y"

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
#line 1042 "parse.y"

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
#line 1070 "parse.y"

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
#line 1275 "parse.y"

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
#define YYNOCODE 237
#define YYACTIONTYPE unsigned short int
#define YYWILDCARD 92
#define sqlite3ParserTOKENTYPE Token
typedef union {
  int yyinit;
  sqlite3ParserTOKENTYPE yy0;
  SrcList* yy3;
  TriggerStep* yy19;
  struct LimitVal yy196;
  With* yy211;
  int yy228;
  struct {int value; int mask;} yy231;
  ExprList* yy258;
  ExprSpan yy326;
  Select* yy387;
  IdList* yy400;
  Expr* yy402;
  struct TrigEvent yy466;
} YYMINORTYPE;
#ifndef YYSTACKDEPTH
#define YYSTACKDEPTH 100
#endif
#define sqlite3ParserARG_SDECL Parse *pParse;
#define sqlite3ParserARG_PDECL ,Parse *pParse
#define sqlite3ParserARG_FETCH Parse *pParse = yypParser->pParse
#define sqlite3ParserARG_STORE yypParser->pParse = pParse
#define YYFALLBACK 1
#define YYNSTATE             423
#define YYNRULE              305
#define YY_MAX_SHIFT         422
#define YY_MIN_SHIFTREDUCE   619
#define YY_MAX_SHIFTREDUCE   923
#define YY_MIN_REDUCE        924
#define YY_MAX_REDUCE        1228
#define YY_ERROR_ACTION      1229
#define YY_ACCEPT_ACTION     1230
#define YY_NO_ACTION         1231
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
#define YY_ACTTAB_COUNT (1440)
static const YYACTIONTYPE yy_action[] = {
 /*     0 */    91,   92,  295,   82,  788,  788,  800,  803,  792,  792,
 /*    10 */    89,   89,   90,   90,   90,   90,  317,   88,   88,   88,
 /*    20 */    88,   87,   87,   86,   86,   86,   85,  317,   90,   90,
 /*    30 */    90,   90,   83,   88,   88,   88,   88,   87,   87,   86,
 /*    40 */    86,   86,   85,  317,  191,  890,  890,  909,  620,  320,
 /*    50 */    91,   92,  295,   82,  788,  788,  800,  803,  792,  792,
 /*    60 */    89,   89,   90,   90,   90,   90,  908,   88,   88,   88,
 /*    70 */    88,   87,   87,   86,   86,   86,   85,  317,   87,   87,
 /*    80 */    86,   86,   86,   85,  317,  646,  891,  892,  909,  123,
 /*    90 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   100 */   317,  421,  421,  737,  708,   85,  317,  229,  377,  140,
 /*   110 */   700,  700,  738,  724,  695,   84,   81,  166,   91,   92,
 /*   120 */   295,   82,  788,  788,  800,  803,  792,  792,   89,   89,
 /*   130 */    90,   90,   90,   90,  155,   88,   88,   88,   88,   87,
 /*   140 */    87,   86,   86,   86,   85,  317,  237,   22,   91,   92,
 /*   150 */   295,   82,  788,  788,  800,  803,  792,  792,   89,   89,
 /*   160 */    90,   90,   90,   90,   67,   88,   88,   88,   88,   87,
 /*   170 */    87,   86,   86,   86,   85,  317,   86,   86,   86,   85,
 /*   180 */   317, 1230,  422,    3,  890,  890,  682,  661,  137,  234,
 /*   190 */   236,  251,   93,  890,  890,  682,  763,   91,   92,  295,
 /*   200 */    82,  788,  788,  800,  803,  792,  792,   89,   89,   90,
 /*   210 */    90,   90,   90,  345,   88,   88,   88,   88,   87,   87,
 /*   220 */    86,   86,   86,   85,  317,  891,  892,  182,  315,  314,
 /*   230 */   368,  365,  364,  736,  891,  892,  778,  735,  771,  235,
 /*   240 */   654,  363,  765,   91,   92,  295,   82,  788,  788,  800,
 /*   250 */   803,  792,  792,   89,   89,   90,   90,   90,   90,  336,
 /*   260 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   270 */   317,  416,  345,  770,  770,  772,  360,  213,  186,   84,
 /*   280 */    81,  166,  138,   84,   81,  166,  647,  890,  890,   48,
 /*   290 */    48,  649,   91,   92,  295,   82,  788,  788,  800,  803,
 /*   300 */   792,  792,   89,   89,   90,   90,   90,   90,  416,   88,
 /*   310 */    88,   88,   88,   87,   87,   86,   86,   86,   85,  317,
 /*   320 */   416,  413,  413,  413,  394,  379,   10,   10,  891,  892,
 /*   330 */   917,  160,  917,  228,  378,  779,  217,  383,   48,   48,
 /*   340 */   303,   91,   92,  295,   82,  788,  788,  800,  803,  792,
 /*   350 */   792,   89,   89,   90,   90,   90,   90,  416,   88,   88,
 /*   360 */    88,   88,   87,   87,   86,   86,   86,   85,  317,  382,
 /*   370 */   111,  763,  297,  394,  384,   10,   10,  254,   84,   81,
 /*   380 */   166,  316,  316,  316,  764,  188,  163,    9,    9,  305,
 /*   390 */    91,   92,  295,   82,  788,  788,  800,  803,  792,  792,
 /*   400 */    89,   89,   90,   90,   90,   90,   64,   88,   88,   88,
 /*   410 */    88,   87,   87,   86,   86,   86,   85,  317,   91,   92,
 /*   420 */   295,   82,  788,  788,  800,  803,  792,  792,   89,   89,
 /*   430 */    90,   90,   90,   90,  300,   88,   88,   88,   88,   87,
 /*   440 */    87,   86,   86,   86,   85,  317,   91,   92,  295,   82,
 /*   450 */   788,  788,  800,  803,  792,  792,   89,   89,   90,   90,
 /*   460 */    90,   90,  648,   88,   88,   88,   88,   87,   87,   86,
 /*   470 */    86,   86,   85,  317,   91,   92,  295,   82,  788,  788,
 /*   480 */   800,  803,  792,  792,   89,   89,   90,   90,   90,   90,
 /*   490 */   147,   88,   88,   88,   88,   87,   87,   86,   86,   86,
 /*   500 */    85,  317,  272,   91,   80,  295,   82,  788,  788,  800,
 /*   510 */   803,  792,  792,   89,   89,   90,   90,   90,   90,   70,
 /*   520 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   530 */   317,  351,   92,  295,   82,  788,  788,  800,  803,  792,
 /*   540 */   792,   89,   89,   90,   90,   90,   90,   73,   88,   88,
 /*   550 */    88,   88,   87,   87,   86,   86,   86,   85,  317,  295,
 /*   560 */    82,  788,  788,  800,  803,  792,  792,   89,   89,   90,
 /*   570 */    90,   90,   90,   78,   88,   88,   88,   88,   87,   87,
 /*   580 */    86,   86,   86,   85,  317,  789,  789,  801,  804,  123,
 /*   590 */   409,  351,   75,   76,  347,   90,   90,   90,   90,   77,
 /*   600 */    88,   88,   88,   88,   87,   87,   86,   86,   86,   85,
 /*   610 */   317,   66,  411,    2, 1122,  216, 1179, 1179,  318,  318,
 /*   620 */   416,  846,  846,  337,  901,  416,   78,  759, 1205,  414,
 /*   630 */   289,  677,  123,  287,  286,  285,  202,  283,   47,   47,
 /*   640 */   633,  315,  314,   48,   48,   75,   76,  400,  244,  342,
 /*   650 */   243,  415,   77,  168,  123,  208,  897,  778,  722,  418,
 /*   660 */   417,  416,  659,  765,  656,  411,    2,  890,  890,  416,
 /*   670 */   375,  318,  318,  793,  309,  242,  172,  327,  310,   30,
 /*   680 */    30,  249,  414,  192,  123,  170,  416,   48,   48,  890,
 /*   690 */   890,  271,  327,  326,  770,  770,  772,  773,   18,  306,
 /*   700 */   400,  165,  186,  111,   48,   48,  296,  123,  891,  892,
 /*   710 */   778,  916,  418,  417,  376,   78,  765,  697,  914,  143,
 /*   720 */   915,  388,  394,  393,  240,  248,  859,  722,  381,  373,
 /*   730 */   891,  892,  146,  324,   75,   76,  709,  823,  282,  394,
 /*   740 */   374,   77,  291,  917,  763,  917,  146,  770,  770,  772,
 /*   750 */   773,   18,  721,  327,  411,    2,  290,  328,   78,  111,
 /*   760 */   318,  318,  199,  157,  261,  371,  256,  370,  187,  687,
 /*   770 */   687,  414,  833,  835,  416,  254,  182,   75,   76,  368,
 /*   780 */   365,  364,  343,  395,   77,  159,  158,  759,  763,  400,
 /*   790 */   363,  231,   48,   48,  674,   23,  297,  411,    2,  778,
 /*   800 */   341,  418,  417,  318,  318,  765,   95,  301,  244,  332,
 /*   810 */   232,  650,  650,  705,  414,  722,  111,  164,  704,  228,
 /*   820 */   378,  890,  890,  185,  184,  183,  862,  394,  396,   24,
 /*   830 */   710,  675,  400,  833,  890,  890,  770,  770,  772,  773,
 /*   840 */    18,  863,  778,  227,  418,  417,   68,  270,  765,  840,
 /*   850 */   778,  302,  771,   54,  890,  890,  765,  864,  165,  688,
 /*   860 */   871,  191,  891,  892,  909,   75,   76,  890,  890,  870,
 /*   870 */   689,  351,   77,  272,  721,  891,  892,  685,  685,  770,
 /*   880 */   770,  772,  773,   18,  722,  411,    2,  770,  770,  772,
 /*   890 */   868,  318,  318,  416,  759,  891,  892,  356,    1,  123,
 /*   900 */   353,  721,  414,  890,  890,  909,  859,  721,  891,  892,
 /*   910 */   416,   10,   10,  323,  763,  244,  342,  243,   20,  416,
 /*   920 */   400,  146,  211,  211,  325,  219,  862,  721,   48,   48,
 /*   930 */   778,  294,  418,  417,  381,  723,  765,   10,   10,  111,
 /*   940 */   214,  863,  668,  108,  891,  892,  215,  330,  644,   19,
 /*   950 */   220,  391,  220,  211,  211,  218,  369,  864,  665,  399,
 /*   960 */   321,  405,  343,  389,  669,  381,  865,  770,  770,  772,
 /*   970 */   773,   18,  721,    5,  416,  191,  416,  311,  909,  837,
 /*   980 */   352,  416,  721,  416,  273,  416,  894,  307,  416,  644,
 /*   990 */   416,  299,   34,   34,   35,   35,  416,  210,  163,   36,
 /*  1000 */    36,   37,   37,   38,   38,  416,   26,   26,   27,   27,
 /*  1010 */   416,  333,  273,  416,   29,   29,  416,  304,  416,  909,
 /*  1020 */   416,  694,  843,   39,   39,  385,  842,  894,   40,   40,
 /*  1030 */   416,   41,   41,  416,   11,   11,   42,   42,   97,   97,
 /*  1040 */   416,  335,  416,  223,  416,  313,  416,  349,   43,   43,
 /*  1050 */   416,   44,   44,  109,  416,  151,  416,  737,   31,   31,
 /*  1060 */    45,   45,   46,   46,   32,   32,  738,  416,  113,  113,
 /*  1070 */   416,  386,  114,  114,  115,  115,  416,  721,  416,  721,
 /*  1080 */   416,  715,  416,  123,  416,   52,   52,  416,   33,   33,
 /*  1090 */   416,  329,  416,  350,   98,   98,   49,   49,   99,   99,
 /*  1100 */   100,  100,   96,   96,  416,  112,  112,  416,  110,  110,
 /*  1110 */   104,  104,  416,  664,  756,  416,  690,  416,  239,  416,
 /*  1120 */   857,  416,  103,  103,  416,  101,  101,  693,  640,  298,
 /*  1130 */   102,  102,  637,   51,   51,   53,   53,   50,   50,   25,
 /*  1140 */    25,  212,   28,   28,  412,  298,  402,  111,  657,  406,
 /*  1150 */   410,  705,   74,  111,   72,  761,  704,  830,  190,  344,
 /*  1160 */   111,  346,  190,  245,  190,  111,   66,  361,  252,  260,
 /*  1170 */   195,   66,  667,  666,  702,  731,  774,   69,  190,  111,
 /*  1180 */   259,  826,  111,  111,  195,  839,  838,  839,  838,  657,
 /*  1190 */   831,  642,  206,  854,  107,  853,  354,  355,  830,  247,
 /*  1200 */   632,  250,  678,  663,  398,  340,  662,  255,  729,  762,
 /*  1210 */   711,  397, 1195,  274,  275,  769,  645,  774,  639,  263,
 /*  1220 */   630,  629,  631,  205,  884,  265,  267,  148,  751,    7,
 /*  1230 */   241,  331,  233,  348,  366,  856,  887,  269,  923,  124,
 /*  1240 */   280,  401,  156,  135,  145,  121,  334,  161,  258,  828,
 /*  1250 */   827,  661,  339,  359,   64,   55,  841,  149,  174,  144,
 /*  1260 */   238,  126,  178,  372,  179,  357,  180,  292,  681,  128,
 /*  1270 */   129,  680,  130,  131,  139,  758,  679,  748,  308,  659,
 /*  1280 */   387,   63,    6,  858,  822,  653,   71,  293,  312,   94,
 /*  1290 */   652,  672,  257,  392,   65,  390,  651,  899,  671,  200,
 /*  1300 */   719,  636,   21,  419,  885,  404,  262,  204,  720,  718,
 /*  1310 */   264,  266,  408,  201,  203,  420,  625,  717,  106,  627,
 /*  1320 */   116,  626,  623,  221,  288,  117,  118,  622,  319,  230,
 /*  1330 */   167,  322,  105,  169,  836,  277,  701,  276,  279,  268,
 /*  1340 */   278,  834,  171,  757,  125,  119,  173,  127,  691,  808,
 /*  1350 */   190,  844,  132,  224,  919,  133,  338,  852,  225,  226,
 /*  1360 */   136,  134,   56,   57,   58,   59,  120,  855,  175,  851,
 /*  1370 */     8,  176,  246,   12,  177,  635,  150,  358,  259,  181,
 /*  1380 */   141,  362,   60,  367,   13,  670,  253,   14,   61,  222,
 /*  1390 */   777,  776,  806,  122,  699,  162,   62,   15,  619,  703,
 /*  1400 */     4,  725,  207,  380, 1184,  209,  142,  926,  730,  197,
 /*  1410 */   198,   69,  821,   66,  807,  805,  193,  861,  194,  189,
 /*  1420 */   860,  810,   16,  877,   17,  152,  403,  196,  878,  153,
 /*  1430 */   407, 1197,  809,  154, 1196,  775,  643,   79,  284,  281,
};
static const YYCODETYPE yy_lookahead[] = {
 /*     0 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*    10 */    15,   16,   17,   18,   19,   20,   32,   22,   23,   24,
 /*    20 */    25,   26,   27,   28,   29,   30,   31,   32,   17,   18,
 /*    30 */    19,   20,   21,   22,   23,   24,   25,   26,   27,   28,
 /*    40 */    29,   30,   31,   32,   49,   52,   53,   52,    1,    2,
 /*    50 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*    60 */    15,   16,   17,   18,   19,   20,   51,   22,   23,   24,
 /*    70 */    25,   26,   27,   28,   29,   30,   31,   32,   26,   27,
 /*    80 */    28,   29,   30,   31,   32,  165,   93,   94,   93,   88,
 /*    90 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   100 */    32,  143,  144,   58,  203,   31,   32,  149,  111,  151,
 /*   110 */   113,  114,   67,  120,  156,  214,  215,  216,    5,    6,
 /*   120 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   130 */    17,   18,   19,   20,  119,   22,   23,   24,   25,   26,
 /*   140 */    27,   28,   29,   30,   31,   32,   43,  189,    5,    6,
 /*   150 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   160 */    17,   18,   19,   20,   51,   22,   23,   24,   25,   26,
 /*   170 */    27,   28,   29,   30,   31,   32,   28,   29,   30,   31,
 /*   180 */    32,  140,  141,  142,   52,   53,  172,  173,   47,   86,
 /*   190 */    87,   48,   79,   52,   53,  181,  147,    5,    6,    7,
 /*   200 */     8,    9,   10,   11,   12,   13,   14,   15,   16,   17,
 /*   210 */    18,   19,   20,  147,   22,   23,   24,   25,   26,   27,
 /*   220 */    28,   29,   30,   31,   32,   93,   94,   95,   26,   27,
 /*   230 */    98,   99,  100,  168,   93,   94,   91,  168,   93,  136,
 /*   240 */    48,  109,   97,    5,    6,    7,    8,    9,   10,   11,
 /*   250 */    12,   13,   14,   15,   16,   17,   18,   19,   20,  210,
 /*   260 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   270 */    32,  147,  147,  128,  129,  130,    7,  211,    9,  214,
 /*   280 */   215,  216,   47,  214,  215,  216,   48,   52,   53,  165,
 /*   290 */   166,  165,    5,    6,    7,    8,    9,   10,   11,   12,
 /*   300 */    13,   14,   15,   16,   17,   18,   19,   20,  147,   22,
 /*   310 */    23,   24,   25,   26,   27,   28,   29,   30,   31,   32,
 /*   320 */   147,  161,  162,  163,  200,  201,  165,  166,   93,   94,
 /*   330 */   128,   51,  130,  115,  116,   48,  211,  156,  165,  166,
 /*   340 */   179,    5,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   350 */    14,   15,   16,   17,   18,   19,   20,  147,   22,   23,
 /*   360 */    24,   25,   26,   27,   28,   29,   30,   31,   32,  147,
 /*   370 */   189,  147,  103,  200,  201,  165,  166,  108,  214,  215,
 /*   380 */   216,  161,  162,  163,   48,  204,  205,  165,  166,  179,
 /*   390 */     5,    6,    7,    8,    9,   10,   11,   12,   13,   14,
 /*   400 */    15,   16,   17,   18,   19,   20,  126,   22,   23,   24,
 /*   410 */    25,   26,   27,   28,   29,   30,   31,   32,    5,    6,
 /*   420 */     7,    8,    9,   10,   11,   12,   13,   14,   15,   16,
 /*   430 */    17,   18,   19,   20,  210,   22,   23,   24,   25,   26,
 /*   440 */    27,   28,   29,   30,   31,   32,    5,    6,    7,    8,
 /*   450 */     9,   10,   11,   12,   13,   14,   15,   16,   17,   18,
 /*   460 */    19,   20,  165,   22,   23,   24,   25,   26,   27,   28,
 /*   470 */    29,   30,   31,   32,    5,    6,    7,    8,    9,   10,
 /*   480 */    11,   12,   13,   14,   15,   16,   17,   18,   19,   20,
 /*   490 */    49,   22,   23,   24,   25,   26,   27,   28,   29,   30,
 /*   500 */    31,   32,  147,    5,    6,    7,    8,    9,   10,   11,
 /*   510 */    12,   13,   14,   15,   16,   17,   18,   19,   20,  134,
 /*   520 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   530 */    32,  147,    6,    7,    8,    9,   10,   11,   12,   13,
 /*   540 */    14,   15,   16,   17,   18,   19,   20,  134,   22,   23,
 /*   550 */    24,   25,   26,   27,   28,   29,   30,   31,   32,    7,
 /*   560 */     8,    9,   10,   11,   12,   13,   14,   15,   16,   17,
 /*   570 */    18,   19,   20,    7,   22,   23,   24,   25,   26,   27,
 /*   580 */    28,   29,   30,   31,   32,    9,   10,   11,   12,   88,
 /*   590 */   235,  147,   26,   27,  147,   17,   18,   19,   20,   33,
 /*   600 */    22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
 /*   610 */    32,   51,   46,   47,   48,  231,  115,  116,   52,   53,
 /*   620 */   147,  104,  105,  106,  178,  147,    7,   83,   48,   63,
 /*   630 */    34,   51,   88,   37,   38,   39,   40,   41,  165,  166,
 /*   640 */    44,   26,   27,  165,  166,   26,   27,   81,  104,  105,
 /*   650 */   106,  147,   33,   57,   88,   48,  164,   91,   51,   93,
 /*   660 */    94,  147,  102,   97,  172,   46,   47,   52,   53,  147,
 /*   670 */   156,   52,   53,   97,    7,  231,   80,  147,  200,  165,
 /*   680 */   166,   43,   63,  147,   88,   89,  147,  165,  166,   52,
 /*   690 */    53,  218,  162,  163,  128,  129,  130,  131,  132,   32,
 /*   700 */    81,   94,    9,  189,  165,  166,  110,   88,   93,   94,
 /*   710 */    91,   96,   93,   94,  200,    7,   97,  188,  103,   82,
 /*   720 */   105,  147,  200,  201,   86,   87,  156,  120,  199,   28,
 /*   730 */    93,   94,  147,  137,   26,   27,   28,   99,  153,  200,
 /*   740 */   201,   33,  157,  128,  147,  130,  147,  128,  129,  130,
 /*   750 */   131,  132,  147,  223,   46,   47,  157,  147,    7,  189,
 /*   760 */    52,   53,   95,   96,   97,   98,   99,  100,  101,  183,
 /*   770 */   184,   63,  162,  163,  147,  108,   95,   26,   27,   98,
 /*   780 */    99,  100,  212,  156,   33,   26,   27,   83,  147,   81,
 /*   790 */   109,  186,  165,  166,   36,  225,  103,   46,   47,   91,
 /*   800 */   230,   93,   94,   52,   53,   97,   47,  210,  104,  105,
 /*   810 */   106,   52,   53,  112,   63,   51,  189,  147,  117,  115,
 /*   820 */   116,   52,   53,  104,  105,  106,   39,  200,  201,   47,
 /*   830 */    28,   73,   81,  223,   52,   53,  128,  129,  130,  131,
 /*   840 */   132,   54,   91,  192,   93,   94,    7,  147,   97,   38,
 /*   850 */    91,  210,   93,  202,   52,   53,   97,   70,   94,   72,
 /*   860 */   147,   49,   93,   94,   52,   26,   27,   52,   53,  147,
 /*   870 */    83,  147,   33,  147,  147,   93,   94,  183,  184,  128,
 /*   880 */   129,  130,  131,  132,  120,   46,   47,  128,  129,  130,
 /*   890 */   147,   52,   53,  147,   83,   93,   94,  221,   47,   88,
 /*   900 */   224,  147,   63,   52,   53,   93,  156,  147,   93,   94,
 /*   910 */   147,  165,  166,  186,  147,  104,  105,  106,   16,  147,
 /*   920 */    81,  147,  187,  188,  147,  179,   39,  147,  165,  166,
 /*   930 */    91,  157,   93,   94,  199,  120,   97,  165,  166,  189,
 /*   940 */   186,   54,   62,   47,   93,   94,  186,  212,   52,   47,
 /*   950 */   176,  179,  178,  187,  188,  231,   76,   70,  174,   72,
 /*   960 */   233,  235,  212,  200,   84,  199,  186,  128,  129,  130,
 /*   970 */   131,  132,  147,   47,  147,   49,  147,  210,   52,  147,
 /*   980 */   230,  147,  147,  147,  147,  147,   52,  107,  147,   93,
 /*   990 */   147,  147,  165,  166,  165,  166,  147,  204,  205,  165,
 /*  1000 */   166,  165,  166,  165,  166,  147,  165,  166,  165,  166,
 /*  1010 */   147,  186,  147,  147,  165,  166,  147,  180,  147,   93,
 /*  1020 */   147,  186,   56,  165,  166,    7,   60,   93,  165,  166,
 /*  1030 */   147,  165,  166,  147,  165,  166,  165,  166,  165,  166,
 /*  1040 */   147,   75,  147,  203,  147,  180,  147,    7,  165,  166,
 /*  1050 */   147,  165,  166,   47,  147,   49,  147,   58,  165,  166,
 /*  1060 */   165,  166,  165,  166,  165,  166,   67,  147,  165,  166,
 /*  1070 */   147,   53,  165,  166,  165,  166,  147,  147,  147,  147,
 /*  1080 */   147,  206,  147,   88,  147,  165,  166,  147,  165,  166,
 /*  1090 */   147,   96,  147,   53,  165,  166,  165,  166,  165,  166,
 /*  1100 */   165,  166,  165,  166,  147,  165,  166,  147,  165,  166,
 /*  1110 */   165,  166,  147,  174,  156,  147,  186,  147,  186,  147,
 /*  1120 */   156,  147,  165,  166,  147,  165,  166,  156,  159,  160,
 /*  1130 */   165,  166,  156,  165,  166,  165,  166,  165,  166,  165,
 /*  1140 */   166,   47,  165,  166,  159,  160,  156,  189,   52,  156,
 /*  1150 */   156,  112,  133,  189,  135,   48,  117,   52,   51,   48,
 /*  1160 */   189,   48,   51,   48,   51,  189,   51,   48,   48,   97,
 /*  1170 */    51,   51,   96,   97,   48,   48,   52,   51,   51,  189,
 /*  1180 */   108,   48,  189,  189,   51,  128,  128,  130,  130,   93,
 /*  1190 */   147,   48,  147,  147,   51,  147,  147,  147,   93,  147,
 /*  1200 */   147,  147,  147,  174,  184,  227,  147,  147,  147,  147,
 /*  1210 */   147,  147,  118,  147,  147,  147,  147,   93,  147,  203,
 /*  1220 */   147,  147,  147,  226,  147,  203,  203,  190,  194,  191,
 /*  1230 */   232,  207,  207,  232,  169,  194,  150,  207,   64,  234,
 /*  1240 */   193,  220,  191,   47,  213,    5,   45,  177,  168,  168,
 /*  1250 */   168,  173,   71,   45,  126,  133,  229,  213,  152,   47,
 /*  1260 */   228,  182,  152,  103,  152,  170,  152,  170,  167,  185,
 /*  1270 */   185,  167,  185,  185,  182,  182,  167,  194,   74,  102,
 /*  1280 */   121,  103,   47,  194,  194,  167,  133,  170,   32,  125,
 /*  1290 */   169,  175,  167,  122,  124,  123,  167,  167,  175,   50,
 /*  1300 */   209,  155,   51,  154,   40,  170,  208,   35,  209,  209,
 /*  1310 */   208,  208,  170,  148,  148,  146,   36,  209,  171,  146,
 /*  1320 */   158,  146,  146,  171,  145,  158,  158,    4,    3,  138,
 /*  1330 */    42,   90,   43,  103,   48,  196,  198,  197,  194,  208,
 /*  1340 */   195,   48,  118,  116,  127,  107,  103,  119,   46,  217,
 /*  1350 */    51,   78,   78,  219,   85,  103,   69,    1,  222,  222,
 /*  1360 */   127,  119,   16,   16,   16,   16,  107,   53,   61,    1,
 /*  1370 */    34,  118,  136,   47,  103,   46,   49,    7,  108,  101,
 /*  1380 */    47,   77,   47,   77,   47,   55,   48,   47,   47,   77,
 /*  1390 */    48,   48,   48,   65,  112,  118,   51,   47,    1,   48,
 /*  1400 */    47,  120,   48,   51,    0,   48,   47,  236,   53,  118,
 /*  1410 */   118,   51,   48,   51,   48,   48,   51,   48,   47,   61,
 /*  1420 */    48,   38,   61,   48,   61,   47,   49,   51,   48,   47,
 /*  1430 */    49,  118,   48,   47,  118,   48,   48,   47,   42,   48,
};
#define YY_SHIFT_USE_DFLT (1440)
#define YY_SHIFT_COUNT    (422)
#define YY_SHIFT_MIN      (-16)
#define YY_SHIFT_MAX      (1404)
static const short yy_shift_ofst[] = {
 /*     0 */    47,  566,  619,  596,  751,  751,  751,  751,  544,   -5,
 /*    10 */    45,   45,  751,  751,  751,  751,  751,  751,  751,  615,
 /*    20 */   615,  132,  704,  811,  501,  113,  143,  192,  238,  287,
 /*    30 */   336,  385,  413,  441,  469,  469,  469,  469,  469,  469,
 /*    40 */   469,  469,  469,  469,  469,  469,  469,  469,  469,  498,
 /*    50 */   469,  526,  552,  552,  708,  751,  751,  751,  751,  751,
 /*    60 */   751,  751,  751,  751,  751,  751,  751,  751,  751,  751,
 /*    70 */   751,  751,  751,  751,  751,  751,  751,  751,  751,  751,
 /*    80 */   751,  751,  839,  751,  751,  751,  751,  751,  751,  751,
 /*    90 */   751,  751,  751,  751,  751,  751,   11,  578,  578,  578,
 /*   100 */   578,  578,   68,   52,  148,  769,  269,  202,  202,  769,
 /*   110 */    74,  218,  -16, 1440, 1440, 1440,  667,  667,  667,  141,
 /*   120 */   141,  787,  787,  637,  769,  769,  769,  769,  769,  769,
 /*   130 */   769,  769,  769,  769,  769,  769,  769,  769,  769,  769,
 /*   140 */   638,  769,  769,  769,  769,  995,  934,  934,  218,    1,
 /*   150 */     1,    1,    1,    1,    1, 1440, 1440,  759,  145,  145,
 /*   160 */   235,  681,  802,  782,   -7,  815,  851,  769,  769,  769,
 /*   170 */   769,  769,  769,  769,  769,  769,  769,  769,  769,  769,
 /*   180 */   769,  769,  769,  880,  880,  880,  769,  769,  607,  769,
 /*   190 */   769,  769,  926,  769,  887,  769,  769,  769,  769,  769,
 /*   200 */   769,  769,  769,  769,  769,  517,  966,  812,  812,  812,
 /*   210 */   764,   -3,  701,  280, 1018, 1018, 1040,  280, 1040,  560,
 /*   220 */   580,  693,  999, 1018, 1019,  999,  999,   15, 1039, 1006,
 /*   230 */  1174, 1196, 1240, 1128, 1201, 1201, 1201, 1201, 1122, 1181,
 /*   240 */  1208, 1128, 1196, 1240, 1240, 1128, 1208, 1212, 1208, 1208,
 /*   250 */  1212, 1160, 1160, 1160, 1204, 1212, 1160, 1177, 1160, 1204,
 /*   260 */  1160, 1160, 1159, 1178, 1159, 1178, 1159, 1178, 1159, 1178,
 /*   270 */  1235, 1153, 1212, 1256, 1256, 1212, 1164, 1171, 1170, 1172,
 /*   280 */  1128, 1249, 1251, 1264, 1264, 1272, 1272, 1272, 1272, 1280,
 /*   290 */  1440, 1440, 1440, 1440, 1440,  576,  103,  719,  896,  902,
 /*   300 */  1107, 1111, 1113, 1115, 1119, 1120, 1096, 1076,  758, 1072,
 /*   310 */  1126, 1127, 1105, 1133, 1057, 1058, 1143, 1124, 1094, 1323,
 /*   320 */  1325, 1191, 1288, 1241, 1289, 1230, 1286, 1293, 1224, 1227,
 /*   330 */  1217, 1238, 1228, 1243, 1302, 1273, 1299, 1274, 1269, 1287,
 /*   340 */  1252, 1356, 1242, 1233, 1346, 1347, 1348, 1349, 1259, 1314,
 /*   350 */  1307, 1253, 1368, 1336, 1326, 1271, 1236, 1327, 1329, 1370,
 /*   360 */  1270, 1278, 1333, 1304, 1335, 1337, 1338, 1340, 1306, 1330,
 /*   370 */  1341, 1312, 1328, 1342, 1343, 1344, 1345, 1282, 1350, 1351,
 /*   380 */  1353, 1352, 1277, 1354, 1357, 1355, 1358, 1359, 1281, 1360,
 /*   390 */  1361, 1362, 1363, 1364, 1360, 1366, 1367, 1369, 1365, 1372,
 /*   400 */  1371, 1383, 1375, 1378, 1377, 1376, 1380, 1382, 1381, 1376,
 /*   410 */  1384, 1386, 1387, 1388, 1390, 1291, 1292, 1313, 1316, 1391,
 /*   420 */  1396, 1397, 1404,
};
#define YY_REDUCE_USE_DFLT (-100)
#define YY_REDUCE_COUNT (294)
#define YY_REDUCE_MIN   (-99)
#define YY_REDUCE_MAX   (1179)
static const short yy_reduce_ofst[] = {
 /*     0 */    41,  627,  514,  -42,  124,  173,  522,  539,  570,  -99,
 /*    10 */    65,   69,  161,  210,  746,  478,  763,  772,  473,  530,
 /*    20 */   610,  774,  735,  750,  181,  164,  164,  164,  164,  164,
 /*    30 */   164,  164,  164,  164,  164,  164,  164,  164,  164,  164,
 /*    40 */   164,  164,  164,  164,  164,  164,  164,  164,  164,  164,
 /*    50 */   164,  164,  164,  164,  222,  827,  829,  834,  836,  838,
 /*    60 */   841,  843,  849,  858,  863,  866,  869,  871,  873,  883,
 /*    70 */   886,  893,  895,  897,  899,  903,  907,  909,  920,  923,
 /*    80 */   929,  931,  933,  935,  937,  940,  943,  945,  957,  960,
 /*    90 */   965,  968,  970,  972,  974,  977,  164,  164,  164,  164,
 /*   100 */   164,  164,  164,  164,  164,  727,   14,  160,  220,  585,
 /*   110 */   164,  766,  164,  164,  164,  164,  492,  492,  492,   66,
 /*   120 */   125,  586,  694,  355,  599,  605,  754,  760,  780,  825,
 /*   130 */   835,  930,   49,  932,  384,  224,  444,  597,  641,  724,
 /*   140 */   676,  837,  767,  726,  865,  958,  969,  985,  529,  964,
 /*   150 */   971,  976,  990,  993,  994,  793,  651,  -80,  126,  297,
 /*   160 */   447,  446,  504,  536,  574,  670,  700,  713,  722,  743,
 /*   170 */   777,  832,  844, 1043, 1045, 1046, 1048, 1049, 1050, 1052,
 /*   180 */  1053, 1054, 1055,  784,  939, 1029, 1059, 1060,  875, 1061,
 /*   190 */  1062, 1063,  840, 1064, 1020, 1066, 1067, 1068,  504, 1069,
 /*   200 */  1071, 1073, 1074, 1075, 1077,  978,  997, 1016, 1022, 1023,
 /*   210 */   875, 1037, 1038, 1034, 1024, 1025,  998, 1041, 1001, 1065,
 /*   220 */  1070, 1078, 1080, 1030, 1021, 1081, 1082, 1047, 1051, 1086,
 /*   230 */  1005, 1031, 1079, 1083, 1084, 1085, 1087, 1088, 1027, 1032,
 /*   240 */  1106, 1089, 1044, 1092, 1093, 1090, 1110, 1095, 1112, 1114,
 /*   250 */  1097, 1101, 1104, 1109, 1116, 1117, 1118, 1121, 1125, 1123,
 /*   260 */  1129, 1130, 1091, 1098, 1099, 1102, 1100, 1103, 1108, 1131,
 /*   270 */  1132, 1134, 1135, 1136, 1137, 1142, 1138, 1140, 1139, 1145,
 /*   280 */  1144, 1146, 1149, 1165, 1166, 1169, 1173, 1175, 1176, 1179,
 /*   290 */  1162, 1167, 1147, 1152, 1168,
};
static const YYACTIONTYPE yy_default[] = {
 /*     0 */  1185, 1179, 1179, 1179, 1122, 1122, 1122, 1122, 1179, 1017,
 /*    10 */  1044, 1044, 1229, 1229, 1229, 1229, 1229, 1229, 1121, 1229,
 /*    20 */  1229, 1229, 1229, 1179, 1021, 1050, 1229, 1229, 1229, 1123,
 /*    30 */  1124, 1229, 1229, 1229, 1155, 1060, 1059, 1058, 1057, 1031,
 /*    40 */  1055, 1048, 1052, 1123, 1117, 1118, 1116, 1120, 1124, 1229,
 /*    50 */  1051, 1086, 1101, 1085, 1229, 1229, 1229, 1229, 1229, 1229,
 /*    60 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*    70 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*    80 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*    90 */  1229, 1229, 1229, 1229, 1229, 1229, 1095, 1100, 1107, 1099,
 /*   100 */  1096, 1088, 1087, 1089, 1090, 1229,  988, 1229, 1229, 1229,
 /*   110 */  1091, 1229, 1092, 1104, 1103, 1102, 1177, 1194, 1193, 1229,
 /*   120 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   130 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   140 */  1129, 1229, 1229, 1229, 1229, 1179,  946,  946, 1229, 1179,
 /*   150 */  1179, 1179, 1179, 1179, 1179, 1021, 1012, 1229, 1229, 1229,
 /*   160 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1174, 1229,
 /*   170 */  1171, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   180 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   190 */  1229, 1229, 1017, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   200 */  1229, 1229, 1229, 1229, 1188, 1229, 1150, 1017, 1017, 1017,
 /*   210 */  1019, 1001, 1011, 1054, 1033, 1033, 1226, 1054, 1226,  963,
 /*   220 */  1208,  960, 1044, 1033, 1119, 1044, 1044, 1018, 1011, 1229,
 /*   230 */  1227, 1065,  991, 1054,  997,  997,  997,  997, 1154, 1223,
 /*   240 */   939, 1054, 1065,  991,  991, 1054,  939, 1130,  939,  939,
 /*   250 */  1130,  989,  989,  989,  978, 1130,  989,  963,  989,  978,
 /*   260 */   989,  989, 1037, 1032, 1037, 1032, 1037, 1032, 1037, 1032,
 /*   270 */  1125, 1229, 1130, 1134, 1134, 1130, 1049, 1038, 1047, 1045,
 /*   280 */  1054,  943,  981, 1191, 1191, 1187, 1187, 1187, 1187,  929,
 /*   290 */  1203, 1203,  965,  965, 1203, 1229, 1229, 1229, 1198, 1137,
 /*   300 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   310 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1071, 1229,
 /*   320 */   926, 1229, 1229, 1178, 1229, 1172, 1229, 1229, 1218, 1229,
 /*   330 */  1229, 1229, 1229, 1229, 1229, 1229, 1153, 1152, 1229, 1229,
 /*   340 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   350 */  1229, 1225, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   360 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1229,
 /*   370 */  1229, 1229, 1229, 1229, 1229, 1229, 1229, 1003, 1229, 1229,
 /*   380 */  1229, 1212, 1229, 1229, 1229, 1229, 1229, 1229, 1229, 1046,
 /*   390 */  1229, 1039, 1229, 1229, 1216, 1229, 1229, 1229, 1229, 1229,
 /*   400 */  1229, 1229, 1229, 1229, 1229, 1181, 1229, 1229, 1229, 1180,
 /*   410 */  1229, 1229, 1229, 1229, 1229, 1073, 1229, 1072, 1076, 1229,
 /*   420 */   933, 1229, 1229,
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
   52,  /*    EXPLAIN => ID */
   52,  /*      QUERY => ID */
   52,  /*       PLAN => ID */
    0,  /*         OR => nothing */
    0,  /*        AND => nothing */
    0,  /*        NOT => nothing */
    0,  /*         IS => nothing */
   52,  /*      MATCH => ID */
   52,  /*    LIKE_KW => ID */
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
   52,  /*      BEGIN => ID */
    0,  /* TRANSACTION => nothing */
   52,  /*   DEFERRED => ID */
    0,  /*     COMMIT => nothing */
   52,  /*        END => ID */
   52,  /*   ROLLBACK => ID */
   52,  /*  SAVEPOINT => ID */
   52,  /*    RELEASE => ID */
    0,  /*         TO => nothing */
    0,  /*      TABLE => nothing */
    0,  /*     CREATE => nothing */
   52,  /*         IF => ID */
    0,  /*     EXISTS => nothing */
    0,  /*         LP => nothing */
    0,  /*         RP => nothing */
    0,  /*         AS => nothing */
   52,  /*    WITHOUT => ID */
    0,  /*      COMMA => nothing */
    0,  /*         ID => nothing */
    0,  /*    INDEXED => nothing */
   52,  /*      ABORT => ID */
   52,  /*     ACTION => ID */
   52,  /*      AFTER => ID */
   52,  /*    ANALYZE => ID */
   52,  /*        ASC => ID */
   52,  /*     ATTACH => ID */
   52,  /*     BEFORE => ID */
   52,  /*         BY => ID */
   52,  /*    CASCADE => ID */
   52,  /*       CAST => ID */
   52,  /*   COLUMNKW => ID */
   52,  /*   CONFLICT => ID */
   52,  /*   DATABASE => ID */
   52,  /*       DESC => ID */
   52,  /*     DETACH => ID */
   52,  /*       EACH => ID */
   52,  /*       FAIL => ID */
   52,  /*        FOR => ID */
   52,  /*     IGNORE => ID */
   52,  /*  IMMEDIATE => ID */
   52,  /*  INITIALLY => ID */
   52,  /*    INSTEAD => ID */
   52,  /*         NO => ID */
   52,  /*        KEY => ID */
   52,  /*         OF => ID */
   52,  /*     OFFSET => ID */
   52,  /*     PRAGMA => ID */
   52,  /*      RAISE => ID */
   52,  /*  RECURSIVE => ID */
   52,  /*    REPLACE => ID */
   52,  /*   RESTRICT => ID */
   52,  /*        ROW => ID */
   52,  /*    TRIGGER => ID */
   52,  /*       VIEW => ID */
   52,  /*       WITH => ID */
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
  "AFTER",         "ANALYZE",       "ASC",           "ATTACH",      
  "BEFORE",        "BY",            "CASCADE",       "CAST",        
  "COLUMNKW",      "CONFLICT",      "DATABASE",      "DESC",        
  "DETACH",        "EACH",          "FAIL",          "FOR",         
  "IGNORE",        "IMMEDIATE",     "INITIALLY",     "INSTEAD",     
  "NO",            "KEY",           "OF",            "OFFSET",      
  "PRAGMA",        "RAISE",         "RECURSIVE",     "REPLACE",     
  "RESTRICT",      "ROW",           "TRIGGER",       "VIEW",        
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
  "ifnotexists",   "columnlist",    "conslist_opt",  "table_options",
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
  "on_opt",        "using_opt",     "idlist",        "setlist",     
  "insert_cmd",    "idlist_opt",    "likeop",        "between_op",  
  "in_op",         "paren_exprlist",  "case_operand",  "case_exprlist",
  "case_else",     "uniqueflag",    "collate",       "nmnum",       
  "trigger_decl",  "trigger_cmd_list",  "trigger_time",  "trigger_event",
  "foreach_clause",  "when_clause",   "trigger_cmd",   "trnm",        
  "tridxby",       "add_column_fullname",  "kwcolumn_opt",  "wqlist",      
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
 /*  63 */ "tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt",
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
 /*  82 */ "oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt",
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
 /*  98 */ "seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt",
 /*  99 */ "seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt",
 /* 100 */ "seltablist ::= stl_prefix LP select RP as on_opt using_opt",
 /* 101 */ "seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt",
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
 /* 132 */ "cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt",
 /* 133 */ "setlist ::= setlist COMMA nm EQ expr",
 /* 134 */ "setlist ::= setlist COMMA LP idlist RP EQ expr",
 /* 135 */ "setlist ::= nm EQ expr",
 /* 136 */ "setlist ::= LP idlist RP EQ expr",
 /* 137 */ "cmd ::= with insert_cmd INTO fullname idlist_opt select",
 /* 138 */ "cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES",
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
 /* 203 */ "cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt",
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
 /* 271 */ "nm ::= ID|INDEXED",
 /* 272 */ "nm ::= STRING",
 /* 273 */ "nm ::= JOIN_KW",
 /* 274 */ "typetoken ::= typename",
 /* 275 */ "typename ::= ID|STRING",
 /* 276 */ "signed ::= plus_num",
 /* 277 */ "signed ::= minus_num",
 /* 278 */ "carglist ::= carglist ccons",
 /* 279 */ "carglist ::=",
 /* 280 */ "ccons ::= NULL onconf",
 /* 281 */ "conslist_opt ::= COMMA conslist",
 /* 282 */ "conslist ::= conslist tconscomma tcons",
 /* 283 */ "conslist ::= tcons",
 /* 284 */ "tconscomma ::=",
 /* 285 */ "defer_subclause_opt ::= defer_subclause",
 /* 286 */ "resolvetype ::= raisetype",
 /* 287 */ "selectnowith ::= oneselect",
 /* 288 */ "oneselect ::= values",
 /* 289 */ "sclp ::= selcollist COMMA",
 /* 290 */ "as ::= ID|STRING",
 /* 291 */ "expr ::= term",
 /* 292 */ "exprlist ::= nexprlist",
 /* 293 */ "nmnum ::= plus_num",
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
    case 156: /* select */
    case 187: /* selectnowith */
    case 188: /* oneselect */
    case 199: /* values */
{
#line 391 "parse.y"
sqlite3SelectDelete(pParse->db, (yypminor->yy387));
#line 1505 "parse.c"
}
      break;
    case 165: /* term */
    case 166: /* expr */
{
#line 832 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy326).pExpr);
#line 1513 "parse.c"
}
      break;
    case 170: /* eidlist_opt */
    case 179: /* sortlist */
    case 180: /* eidlist */
    case 192: /* selcollist */
    case 195: /* groupby_opt */
    case 197: /* orderby_opt */
    case 200: /* nexprlist */
    case 201: /* exprlist */
    case 202: /* sclp */
    case 211: /* setlist */
    case 217: /* paren_exprlist */
    case 219: /* case_exprlist */
{
#line 1273 "parse.y"
sqlite3ExprListDelete(pParse->db, (yypminor->yy258));
#line 1531 "parse.c"
}
      break;
    case 186: /* fullname */
    case 193: /* from */
    case 204: /* seltablist */
    case 205: /* stl_prefix */
{
#line 619 "parse.y"
sqlite3SrcListDelete(pParse->db, (yypminor->yy3));
#line 1541 "parse.c"
}
      break;
    case 189: /* with */
    case 235: /* wqlist */
{
#line 1517 "parse.y"
sqlite3WithDelete(pParse->db, (yypminor->yy211));
#line 1549 "parse.c"
}
      break;
    case 194: /* where_opt */
    case 196: /* having_opt */
    case 208: /* on_opt */
    case 218: /* case_operand */
    case 220: /* case_else */
    case 229: /* when_clause */
{
#line 741 "parse.y"
sqlite3ExprDelete(pParse->db, (yypminor->yy402));
#line 1561 "parse.c"
}
      break;
    case 209: /* using_opt */
    case 210: /* idlist */
    case 213: /* idlist_opt */
{
#line 653 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy400));
#line 1570 "parse.c"
}
      break;
    case 225: /* trigger_cmd_list */
    case 230: /* trigger_cmd */
{
#line 1392 "parse.y"
sqlite3DeleteTriggerStep(pParse->db, (yypminor->yy19));
#line 1578 "parse.c"
}
      break;
    case 227: /* trigger_event */
{
#line 1378 "parse.y"
sqlite3IdListDelete(pParse->db, (yypminor->yy466).b);
#line 1585 "parse.c"
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
#line 1758 "parse.c"
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
  { 141, 3 },
  { 141, 1 },
  { 142, 1 },
  { 142, 3 },
  { 144, 3 },
  { 145, 0 },
  { 145, 1 },
  { 144, 2 },
  { 144, 2 },
  { 144, 2 },
  { 144, 2 },
  { 144, 3 },
  { 144, 5 },
  { 149, 4 },
  { 151, 1 },
  { 152, 0 },
  { 152, 3 },
  { 150, 5 },
  { 150, 2 },
  { 155, 0 },
  { 155, 2 },
  { 157, 2 },
  { 159, 0 },
  { 159, 4 },
  { 159, 6 },
  { 160, 2 },
  { 164, 2 },
  { 164, 2 },
  { 164, 4 },
  { 164, 3 },
  { 164, 3 },
  { 164, 2 },
  { 164, 3 },
  { 164, 5 },
  { 164, 2 },
  { 164, 4 },
  { 164, 4 },
  { 164, 1 },
  { 164, 2 },
  { 169, 0 },
  { 169, 1 },
  { 171, 0 },
  { 171, 2 },
  { 173, 2 },
  { 173, 3 },
  { 173, 3 },
  { 173, 3 },
  { 174, 2 },
  { 174, 2 },
  { 174, 1 },
  { 174, 1 },
  { 174, 2 },
  { 172, 3 },
  { 172, 2 },
  { 175, 0 },
  { 175, 2 },
  { 175, 2 },
  { 154, 0 },
  { 177, 1 },
  { 178, 2 },
  { 178, 7 },
  { 178, 5 },
  { 178, 5 },
  { 178, 10 },
  { 181, 0 },
  { 167, 0 },
  { 167, 3 },
  { 182, 0 },
  { 182, 2 },
  { 183, 1 },
  { 183, 1 },
  { 144, 4 },
  { 185, 2 },
  { 185, 0 },
  { 144, 7 },
  { 144, 4 },
  { 144, 1 },
  { 156, 2 },
  { 187, 3 },
  { 190, 1 },
  { 190, 2 },
  { 190, 1 },
  { 188, 9 },
  { 199, 4 },
  { 199, 5 },
  { 191, 1 },
  { 191, 1 },
  { 191, 0 },
  { 202, 0 },
  { 192, 3 },
  { 192, 2 },
  { 192, 4 },
  { 203, 2 },
  { 203, 0 },
  { 193, 0 },
  { 193, 2 },
  { 205, 2 },
  { 205, 0 },
  { 204, 6 },
  { 204, 8 },
  { 204, 7 },
  { 204, 7 },
  { 186, 1 },
  { 206, 1 },
  { 206, 2 },
  { 206, 3 },
  { 206, 4 },
  { 208, 2 },
  { 208, 0 },
  { 207, 0 },
  { 207, 3 },
  { 207, 2 },
  { 209, 4 },
  { 209, 0 },
  { 197, 0 },
  { 197, 3 },
  { 179, 4 },
  { 179, 2 },
  { 168, 1 },
  { 168, 1 },
  { 168, 0 },
  { 195, 0 },
  { 195, 3 },
  { 196, 0 },
  { 196, 2 },
  { 198, 0 },
  { 198, 2 },
  { 198, 4 },
  { 198, 4 },
  { 144, 6 },
  { 194, 0 },
  { 194, 2 },
  { 144, 8 },
  { 211, 5 },
  { 211, 7 },
  { 211, 3 },
  { 211, 5 },
  { 144, 6 },
  { 144, 7 },
  { 212, 2 },
  { 212, 1 },
  { 213, 0 },
  { 213, 3 },
  { 210, 3 },
  { 210, 1 },
  { 166, 3 },
  { 165, 1 },
  { 166, 1 },
  { 166, 1 },
  { 166, 3 },
  { 166, 5 },
  { 165, 1 },
  { 165, 1 },
  { 165, 1 },
  { 166, 1 },
  { 166, 3 },
  { 166, 6 },
  { 166, 5 },
  { 166, 4 },
  { 165, 1 },
  { 166, 5 },
  { 166, 3 },
  { 166, 3 },
  { 166, 3 },
  { 166, 3 },
  { 166, 3 },
  { 166, 3 },
  { 166, 3 },
  { 166, 3 },
  { 214, 1 },
  { 214, 2 },
  { 166, 3 },
  { 166, 5 },
  { 166, 2 },
  { 166, 3 },
  { 166, 3 },
  { 166, 4 },
  { 166, 2 },
  { 166, 2 },
  { 166, 2 },
  { 166, 2 },
  { 215, 1 },
  { 215, 2 },
  { 166, 5 },
  { 216, 1 },
  { 216, 2 },
  { 166, 5 },
  { 166, 3 },
  { 166, 5 },
  { 166, 4 },
  { 166, 4 },
  { 166, 5 },
  { 219, 5 },
  { 219, 4 },
  { 220, 2 },
  { 220, 0 },
  { 218, 1 },
  { 218, 0 },
  { 201, 0 },
  { 200, 3 },
  { 200, 1 },
  { 217, 0 },
  { 217, 3 },
  { 144, 11 },
  { 221, 1 },
  { 221, 0 },
  { 170, 0 },
  { 170, 3 },
  { 180, 5 },
  { 180, 3 },
  { 222, 0 },
  { 222, 2 },
  { 144, 6 },
  { 144, 2 },
  { 144, 4 },
  { 144, 5 },
  { 144, 4 },
  { 144, 5 },
  { 144, 6 },
  { 162, 2 },
  { 163, 2 },
  { 144, 5 },
  { 224, 9 },
  { 226, 1 },
  { 226, 1 },
  { 226, 2 },
  { 226, 0 },
  { 227, 1 },
  { 227, 1 },
  { 227, 3 },
  { 229, 0 },
  { 229, 2 },
  { 225, 3 },
  { 225, 2 },
  { 231, 3 },
  { 232, 3 },
  { 232, 2 },
  { 230, 7 },
  { 230, 5 },
  { 230, 5 },
  { 230, 1 },
  { 166, 4 },
  { 166, 6 },
  { 184, 1 },
  { 184, 1 },
  { 184, 1 },
  { 144, 4 },
  { 144, 1 },
  { 144, 2 },
  { 144, 4 },
  { 144, 1 },
  { 144, 2 },
  { 144, 6 },
  { 144, 7 },
  { 233, 1 },
  { 189, 0 },
  { 189, 2 },
  { 189, 3 },
  { 235, 6 },
  { 235, 8 },
  { 140, 1 },
  { 142, 0 },
  { 143, 1 },
  { 146, 0 },
  { 146, 1 },
  { 146, 2 },
  { 148, 1 },
  { 148, 0 },
  { 144, 2 },
  { 153, 4 },
  { 153, 2 },
  { 147, 1 },
  { 147, 1 },
  { 147, 1 },
  { 159, 1 },
  { 160, 1 },
  { 161, 1 },
  { 161, 1 },
  { 158, 2 },
  { 158, 0 },
  { 164, 2 },
  { 154, 2 },
  { 176, 3 },
  { 176, 1 },
  { 177, 0 },
  { 181, 1 },
  { 183, 1 },
  { 187, 1 },
  { 188, 1 },
  { 202, 2 },
  { 203, 1 },
  { 166, 1 },
  { 201, 1 },
  { 223, 1 },
  { 223, 1 },
  { 223, 1 },
  { 223, 1 },
  { 223, 1 },
  { 162, 1 },
  { 228, 0 },
  { 228, 3 },
  { 231, 1 },
  { 232, 0 },
  { 234, 0 },
  { 234, 1 },
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
#line 2203 "parse.c"
        break;
      case 1: /* ecmd ::= SEMI */
#line 108 "parse.y"
{
  sqlite3ErrorMsg(pParse, "syntax error: empty request");
}
#line 2210 "parse.c"
        break;
      case 2: /* explain ::= EXPLAIN */
#line 113 "parse.y"
{ pParse->explain = 1; }
#line 2215 "parse.c"
        break;
      case 3: /* explain ::= EXPLAIN QUERY PLAN */
#line 114 "parse.y"
{ pParse->explain = 2; }
#line 2220 "parse.c"
        break;
      case 4: /* cmd ::= BEGIN transtype trans_opt */
#line 146 "parse.y"
{sqlite3BeginTransaction(pParse, yymsp[-1].minor.yy228);}
#line 2225 "parse.c"
        break;
      case 5: /* transtype ::= */
#line 151 "parse.y"
{yymsp[1].minor.yy228 = TK_DEFERRED;}
#line 2230 "parse.c"
        break;
      case 6: /* transtype ::= DEFERRED */
#line 152 "parse.y"
{yymsp[0].minor.yy228 = yymsp[0].major; /*A-overwrites-X*/}
#line 2235 "parse.c"
        break;
      case 7: /* cmd ::= COMMIT trans_opt */
      case 8: /* cmd ::= END trans_opt */ yytestcase(yyruleno==8);
#line 153 "parse.y"
{sqlite3CommitTransaction(pParse);}
#line 2241 "parse.c"
        break;
      case 9: /* cmd ::= ROLLBACK trans_opt */
#line 155 "parse.y"
{sqlite3RollbackTransaction(pParse);}
#line 2246 "parse.c"
        break;
      case 10: /* cmd ::= SAVEPOINT nm */
#line 159 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_BEGIN, &yymsp[0].minor.yy0);
}
#line 2253 "parse.c"
        break;
      case 11: /* cmd ::= RELEASE savepoint_opt nm */
#line 162 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_RELEASE, &yymsp[0].minor.yy0);
}
#line 2260 "parse.c"
        break;
      case 12: /* cmd ::= ROLLBACK trans_opt TO savepoint_opt nm */
#line 165 "parse.y"
{
  sqlite3Savepoint(pParse, SAVEPOINT_ROLLBACK, &yymsp[0].minor.yy0);
}
#line 2267 "parse.c"
        break;
      case 13: /* create_table ::= createkw TABLE ifnotexists nm */
#line 172 "parse.y"
{
   sqlite3StartTable(pParse,&yymsp[0].minor.yy0,yymsp[-1].minor.yy228);
}
#line 2274 "parse.c"
        break;
      case 14: /* createkw ::= CREATE */
#line 175 "parse.y"
{disableLookaside(pParse);}
#line 2279 "parse.c"
        break;
      case 15: /* ifnotexists ::= */
      case 19: /* table_options ::= */ yytestcase(yyruleno==19);
      case 39: /* autoinc ::= */ yytestcase(yyruleno==39);
      case 54: /* init_deferred_pred_opt ::= */ yytestcase(yyruleno==54);
      case 64: /* defer_subclause_opt ::= */ yytestcase(yyruleno==64);
      case 73: /* ifexists ::= */ yytestcase(yyruleno==73);
      case 87: /* distinct ::= */ yytestcase(yyruleno==87);
      case 210: /* collate ::= */ yytestcase(yyruleno==210);
#line 178 "parse.y"
{yymsp[1].minor.yy228 = 0;}
#line 2291 "parse.c"
        break;
      case 16: /* ifnotexists ::= IF NOT EXISTS */
#line 179 "parse.y"
{yymsp[-2].minor.yy228 = 1;}
#line 2296 "parse.c"
        break;
      case 17: /* create_table_args ::= LP columnlist conslist_opt RP table_options */
#line 181 "parse.y"
{
  sqlite3EndTable(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,yymsp[0].minor.yy228,0);
}
#line 2303 "parse.c"
        break;
      case 18: /* create_table_args ::= AS select */
#line 184 "parse.y"
{
  sqlite3EndTable(pParse,0,0,0,yymsp[0].minor.yy387);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy387);
}
#line 2311 "parse.c"
        break;
      case 20: /* table_options ::= WITHOUT nm */
#line 190 "parse.y"
{
  if( yymsp[0].minor.yy0.n==5 && sqlite3_strnicmp(yymsp[0].minor.yy0.z,"rowid",5)==0 ){
    yymsp[-1].minor.yy228 = TF_WithoutRowid | TF_NoVisibleRowid;
  }else{
    yymsp[-1].minor.yy228 = 0;
    sqlite3ErrorMsg(pParse, "unknown table option: %.*s", yymsp[0].minor.yy0.n, yymsp[0].minor.yy0.z);
  }
}
#line 2323 "parse.c"
        break;
      case 21: /* columnname ::= nm typetoken */
#line 200 "parse.y"
{sqlite3AddColumn(pParse,&yymsp[-1].minor.yy0,&yymsp[0].minor.yy0);}
#line 2328 "parse.c"
        break;
      case 22: /* typetoken ::= */
      case 57: /* conslist_opt ::= */ yytestcase(yyruleno==57);
      case 93: /* as ::= */ yytestcase(yyruleno==93);
#line 241 "parse.y"
{yymsp[1].minor.yy0.n = 0; yymsp[1].minor.yy0.z = 0;}
#line 2335 "parse.c"
        break;
      case 23: /* typetoken ::= typename LP signed RP */
#line 243 "parse.y"
{
  yymsp[-3].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-3].minor.yy0.z);
}
#line 2342 "parse.c"
        break;
      case 24: /* typetoken ::= typename LP signed COMMA signed RP */
#line 246 "parse.y"
{
  yymsp[-5].minor.yy0.n = (int)(&yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n] - yymsp[-5].minor.yy0.z);
}
#line 2349 "parse.c"
        break;
      case 25: /* typename ::= typename ID|STRING */
#line 251 "parse.y"
{yymsp[-1].minor.yy0.n=yymsp[0].minor.yy0.n+(int)(yymsp[0].minor.yy0.z-yymsp[-1].minor.yy0.z);}
#line 2354 "parse.c"
        break;
      case 26: /* ccons ::= CONSTRAINT nm */
      case 59: /* tcons ::= CONSTRAINT nm */ yytestcase(yyruleno==59);
#line 260 "parse.y"
{pParse->constraintName = yymsp[0].minor.yy0;}
#line 2360 "parse.c"
        break;
      case 27: /* ccons ::= DEFAULT term */
      case 29: /* ccons ::= DEFAULT PLUS term */ yytestcase(yyruleno==29);
#line 261 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[0].minor.yy326);}
#line 2366 "parse.c"
        break;
      case 28: /* ccons ::= DEFAULT LP expr RP */
#line 262 "parse.y"
{sqlite3AddDefaultValue(pParse,&yymsp[-1].minor.yy326);}
#line 2371 "parse.c"
        break;
      case 30: /* ccons ::= DEFAULT MINUS term */
#line 264 "parse.y"
{
  ExprSpan v;
  v.pExpr = sqlite3PExpr(pParse, TK_UMINUS, yymsp[0].minor.yy326.pExpr, 0);
  v.zStart = yymsp[-1].minor.yy0.z;
  v.zEnd = yymsp[0].minor.yy326.zEnd;
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2382 "parse.c"
        break;
      case 31: /* ccons ::= DEFAULT ID|INDEXED */
#line 271 "parse.y"
{
  ExprSpan v;
  spanExpr(&v, pParse, TK_STRING, yymsp[0].minor.yy0);
  sqlite3AddDefaultValue(pParse,&v);
}
#line 2391 "parse.c"
        break;
      case 32: /* ccons ::= NOT NULL onconf */
#line 281 "parse.y"
{sqlite3AddNotNull(pParse, yymsp[0].minor.yy228);}
#line 2396 "parse.c"
        break;
      case 33: /* ccons ::= PRIMARY KEY sortorder onconf autoinc */
#line 283 "parse.y"
{sqlite3AddPrimaryKey(pParse,0,yymsp[-1].minor.yy228,yymsp[0].minor.yy228,yymsp[-2].minor.yy228);}
#line 2401 "parse.c"
        break;
      case 34: /* ccons ::= UNIQUE onconf */
#line 284 "parse.y"
{sqlite3CreateIndex(pParse,0,0,0,yymsp[0].minor.yy228,0,0,0,0,
                                   SQLITE_IDXTYPE_UNIQUE);}
#line 2407 "parse.c"
        break;
      case 35: /* ccons ::= CHECK LP expr RP */
#line 286 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-1].minor.yy326.pExpr);}
#line 2412 "parse.c"
        break;
      case 36: /* ccons ::= REFERENCES nm eidlist_opt refargs */
#line 288 "parse.y"
{sqlite3CreateForeignKey(pParse,0,&yymsp[-2].minor.yy0,yymsp[-1].minor.yy258,yymsp[0].minor.yy228);}
#line 2417 "parse.c"
        break;
      case 37: /* ccons ::= defer_subclause */
#line 289 "parse.y"
{sqlite3DeferForeignKey(pParse,yymsp[0].minor.yy228);}
#line 2422 "parse.c"
        break;
      case 38: /* ccons ::= COLLATE ID|STRING */
#line 290 "parse.y"
{sqlite3AddCollateType(pParse, &yymsp[0].minor.yy0);}
#line 2427 "parse.c"
        break;
      case 40: /* autoinc ::= AUTOINCR */
#line 295 "parse.y"
{yymsp[0].minor.yy228 = 1;}
#line 2432 "parse.c"
        break;
      case 41: /* refargs ::= */
#line 303 "parse.y"
{ yymsp[1].minor.yy228 = OE_None*0x0101; /* EV: R-19803-45884 */}
#line 2437 "parse.c"
        break;
      case 42: /* refargs ::= refargs refarg */
#line 304 "parse.y"
{ yymsp[-1].minor.yy228 = (yymsp[-1].minor.yy228 & ~yymsp[0].minor.yy231.mask) | yymsp[0].minor.yy231.value; }
#line 2442 "parse.c"
        break;
      case 43: /* refarg ::= MATCH nm */
#line 306 "parse.y"
{ yymsp[-1].minor.yy231.value = 0;     yymsp[-1].minor.yy231.mask = 0x000000; }
#line 2447 "parse.c"
        break;
      case 44: /* refarg ::= ON INSERT refact */
#line 307 "parse.y"
{ yymsp[-2].minor.yy231.value = 0;     yymsp[-2].minor.yy231.mask = 0x000000; }
#line 2452 "parse.c"
        break;
      case 45: /* refarg ::= ON DELETE refact */
#line 308 "parse.y"
{ yymsp[-2].minor.yy231.value = yymsp[0].minor.yy228;     yymsp[-2].minor.yy231.mask = 0x0000ff; }
#line 2457 "parse.c"
        break;
      case 46: /* refarg ::= ON UPDATE refact */
#line 309 "parse.y"
{ yymsp[-2].minor.yy231.value = yymsp[0].minor.yy228<<8;  yymsp[-2].minor.yy231.mask = 0x00ff00; }
#line 2462 "parse.c"
        break;
      case 47: /* refact ::= SET NULL */
#line 311 "parse.y"
{ yymsp[-1].minor.yy228 = OE_SetNull;  /* EV: R-33326-45252 */}
#line 2467 "parse.c"
        break;
      case 48: /* refact ::= SET DEFAULT */
#line 312 "parse.y"
{ yymsp[-1].minor.yy228 = OE_SetDflt;  /* EV: R-33326-45252 */}
#line 2472 "parse.c"
        break;
      case 49: /* refact ::= CASCADE */
#line 313 "parse.y"
{ yymsp[0].minor.yy228 = OE_Cascade;  /* EV: R-33326-45252 */}
#line 2477 "parse.c"
        break;
      case 50: /* refact ::= RESTRICT */
#line 314 "parse.y"
{ yymsp[0].minor.yy228 = OE_Restrict; /* EV: R-33326-45252 */}
#line 2482 "parse.c"
        break;
      case 51: /* refact ::= NO ACTION */
#line 315 "parse.y"
{ yymsp[-1].minor.yy228 = OE_None;     /* EV: R-33326-45252 */}
#line 2487 "parse.c"
        break;
      case 52: /* defer_subclause ::= NOT DEFERRABLE init_deferred_pred_opt */
#line 317 "parse.y"
{yymsp[-2].minor.yy228 = 0;}
#line 2492 "parse.c"
        break;
      case 53: /* defer_subclause ::= DEFERRABLE init_deferred_pred_opt */
      case 68: /* orconf ::= OR resolvetype */ yytestcase(yyruleno==68);
      case 139: /* insert_cmd ::= INSERT orconf */ yytestcase(yyruleno==139);
#line 318 "parse.y"
{yymsp[-1].minor.yy228 = yymsp[0].minor.yy228;}
#line 2499 "parse.c"
        break;
      case 55: /* init_deferred_pred_opt ::= INITIALLY DEFERRED */
      case 72: /* ifexists ::= IF EXISTS */ yytestcase(yyruleno==72);
      case 182: /* between_op ::= NOT BETWEEN */ yytestcase(yyruleno==182);
      case 185: /* in_op ::= NOT IN */ yytestcase(yyruleno==185);
      case 211: /* collate ::= COLLATE ID|STRING */ yytestcase(yyruleno==211);
#line 321 "parse.y"
{yymsp[-1].minor.yy228 = 1;}
#line 2508 "parse.c"
        break;
      case 56: /* init_deferred_pred_opt ::= INITIALLY IMMEDIATE */
#line 322 "parse.y"
{yymsp[-1].minor.yy228 = 0;}
#line 2513 "parse.c"
        break;
      case 58: /* tconscomma ::= COMMA */
#line 328 "parse.y"
{pParse->constraintName.n = 0;}
#line 2518 "parse.c"
        break;
      case 60: /* tcons ::= PRIMARY KEY LP sortlist autoinc RP onconf */
#line 332 "parse.y"
{sqlite3AddPrimaryKey(pParse,yymsp[-3].minor.yy258,yymsp[0].minor.yy228,yymsp[-2].minor.yy228,0);}
#line 2523 "parse.c"
        break;
      case 61: /* tcons ::= UNIQUE LP sortlist RP onconf */
#line 334 "parse.y"
{sqlite3CreateIndex(pParse,0,0,yymsp[-2].minor.yy258,yymsp[0].minor.yy228,0,0,0,0,
                                       SQLITE_IDXTYPE_UNIQUE);}
#line 2529 "parse.c"
        break;
      case 62: /* tcons ::= CHECK LP expr RP onconf */
#line 337 "parse.y"
{sqlite3AddCheckConstraint(pParse,yymsp[-2].minor.yy326.pExpr);}
#line 2534 "parse.c"
        break;
      case 63: /* tcons ::= FOREIGN KEY LP eidlist RP REFERENCES nm eidlist_opt refargs defer_subclause_opt */
#line 339 "parse.y"
{
    sqlite3CreateForeignKey(pParse, yymsp[-6].minor.yy258, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy258, yymsp[-1].minor.yy228);
    sqlite3DeferForeignKey(pParse, yymsp[0].minor.yy228);
}
#line 2542 "parse.c"
        break;
      case 65: /* onconf ::= */
      case 67: /* orconf ::= */ yytestcase(yyruleno==67);
#line 353 "parse.y"
{yymsp[1].minor.yy228 = OE_Default;}
#line 2548 "parse.c"
        break;
      case 66: /* onconf ::= ON CONFLICT resolvetype */
#line 354 "parse.y"
{yymsp[-2].minor.yy228 = yymsp[0].minor.yy228;}
#line 2553 "parse.c"
        break;
      case 69: /* resolvetype ::= IGNORE */
#line 358 "parse.y"
{yymsp[0].minor.yy228 = OE_Ignore;}
#line 2558 "parse.c"
        break;
      case 70: /* resolvetype ::= REPLACE */
      case 140: /* insert_cmd ::= REPLACE */ yytestcase(yyruleno==140);
#line 359 "parse.y"
{yymsp[0].minor.yy228 = OE_Replace;}
#line 2564 "parse.c"
        break;
      case 71: /* cmd ::= DROP TABLE ifexists fullname */
#line 363 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy3, 0, yymsp[-1].minor.yy228);
}
#line 2571 "parse.c"
        break;
      case 74: /* cmd ::= createkw VIEW ifnotexists nm eidlist_opt AS select */
#line 374 "parse.y"
{
  sqlite3CreateView(pParse, &yymsp[-6].minor.yy0, &yymsp[-3].minor.yy0, yymsp[-2].minor.yy258, yymsp[0].minor.yy387, yymsp[-4].minor.yy228);
}
#line 2578 "parse.c"
        break;
      case 75: /* cmd ::= DROP VIEW ifexists fullname */
#line 377 "parse.y"
{
  sqlite3DropTable(pParse, yymsp[0].minor.yy3, 1, yymsp[-1].minor.yy228);
}
#line 2585 "parse.c"
        break;
      case 76: /* cmd ::= select */
#line 384 "parse.y"
{
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0};
  sqlite3Select(pParse, yymsp[0].minor.yy387, &dest);
  sqlite3SelectDelete(pParse->db, yymsp[0].minor.yy387);
}
#line 2594 "parse.c"
        break;
      case 77: /* select ::= with selectnowith */
#line 421 "parse.y"
{
  Select *p = yymsp[0].minor.yy387;
  if( p ){
    p->pWith = yymsp[-1].minor.yy211;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlite3WithDelete(pParse->db, yymsp[-1].minor.yy211);
  }
  yymsp[-1].minor.yy387 = p; /*A-overwrites-W*/
}
#line 2608 "parse.c"
        break;
      case 78: /* selectnowith ::= selectnowith multiselect_op oneselect */
#line 434 "parse.y"
{
  Select *pRhs = yymsp[0].minor.yy387;
  Select *pLhs = yymsp[-2].minor.yy387;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlite3SrcListAppendFromTerm(pParse,0,0,0,&x,pRhs,0,0);
    pRhs = sqlite3SelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)yymsp[-1].minor.yy228;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( yymsp[-1].minor.yy228!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sqlite3SelectDelete(pParse->db, pLhs);
  }
  yymsp[-2].minor.yy387 = pRhs;
}
#line 2634 "parse.c"
        break;
      case 79: /* multiselect_op ::= UNION */
      case 81: /* multiselect_op ::= EXCEPT|INTERSECT */ yytestcase(yyruleno==81);
#line 457 "parse.y"
{yymsp[0].minor.yy228 = yymsp[0].major; /*A-overwrites-OP*/}
#line 2640 "parse.c"
        break;
      case 80: /* multiselect_op ::= UNION ALL */
#line 458 "parse.y"
{yymsp[-1].minor.yy228 = TK_ALL;}
#line 2645 "parse.c"
        break;
      case 82: /* oneselect ::= SELECT distinct selcollist from where_opt groupby_opt having_opt orderby_opt limit_opt */
#line 462 "parse.y"
{
#if SELECTTRACE_ENABLED
  Token s = yymsp[-8].minor.yy0; /*A-overwrites-S*/
#endif
  yymsp[-8].minor.yy387 = sqlite3SelectNew(pParse,yymsp[-6].minor.yy258,yymsp[-5].minor.yy3,yymsp[-4].minor.yy402,yymsp[-3].minor.yy258,yymsp[-2].minor.yy402,yymsp[-1].minor.yy258,yymsp[-7].minor.yy228,yymsp[0].minor.yy196.pLimit,yymsp[0].minor.yy196.pOffset);
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
  if( yymsp[-8].minor.yy387!=0 ){
    const char *z = s.z+6;
    int i;
    sqlite3_snprintf(sizeof(yymsp[-8].minor.yy387->zSelName), yymsp[-8].minor.yy387->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlite3Isalnum(z[i]); i++){}
      sqlite3_snprintf(sizeof(yymsp[-8].minor.yy387->zSelName), yymsp[-8].minor.yy387->zSelName, "%.*s", i, z);
    }
  }
#endif /* SELECTRACE_ENABLED */
}
#line 2679 "parse.c"
        break;
      case 83: /* values ::= VALUES LP nexprlist RP */
#line 496 "parse.y"
{
  yymsp[-3].minor.yy387 = sqlite3SelectNew(pParse,yymsp[-1].minor.yy258,0,0,0,0,0,SF_Values,0,0);
}
#line 2686 "parse.c"
        break;
      case 84: /* values ::= values COMMA LP exprlist RP */
#line 499 "parse.y"
{
  Select *pRight, *pLeft = yymsp[-4].minor.yy387;
  pRight = sqlite3SelectNew(pParse,yymsp[-1].minor.yy258,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    yymsp[-4].minor.yy387 = pRight;
  }else{
    yymsp[-4].minor.yy387 = pLeft;
  }
}
#line 2702 "parse.c"
        break;
      case 85: /* distinct ::= DISTINCT */
#line 516 "parse.y"
{yymsp[0].minor.yy228 = SF_Distinct;}
#line 2707 "parse.c"
        break;
      case 86: /* distinct ::= ALL */
#line 517 "parse.y"
{yymsp[0].minor.yy228 = SF_All;}
#line 2712 "parse.c"
        break;
      case 88: /* sclp ::= */
      case 114: /* orderby_opt ::= */ yytestcase(yyruleno==114);
      case 121: /* groupby_opt ::= */ yytestcase(yyruleno==121);
      case 198: /* exprlist ::= */ yytestcase(yyruleno==198);
      case 201: /* paren_exprlist ::= */ yytestcase(yyruleno==201);
      case 206: /* eidlist_opt ::= */ yytestcase(yyruleno==206);
#line 530 "parse.y"
{yymsp[1].minor.yy258 = 0;}
#line 2722 "parse.c"
        break;
      case 89: /* selcollist ::= sclp expr as */
#line 531 "parse.y"
{
   yymsp[-2].minor.yy258 = sqlite3ExprListAppend(pParse, yymsp[-2].minor.yy258, yymsp[-1].minor.yy326.pExpr);
   if( yymsp[0].minor.yy0.n>0 ) sqlite3ExprListSetName(pParse, yymsp[-2].minor.yy258, &yymsp[0].minor.yy0, 1);
   sqlite3ExprListSetSpan(pParse,yymsp[-2].minor.yy258,&yymsp[-1].minor.yy326);
}
#line 2731 "parse.c"
        break;
      case 90: /* selcollist ::= sclp STAR */
#line 536 "parse.y"
{
  Expr *p = sqlite3Expr(pParse->db, TK_ASTERISK, 0);
  yymsp[-1].minor.yy258 = sqlite3ExprListAppend(pParse, yymsp[-1].minor.yy258, p);
}
#line 2739 "parse.c"
        break;
      case 91: /* selcollist ::= sclp nm DOT STAR */
#line 540 "parse.y"
{
  Expr *pRight = sqlite3PExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pLeft = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *pDot = sqlite3PExpr(pParse, TK_DOT, pLeft, pRight);
  yymsp[-3].minor.yy258 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy258, pDot);
}
#line 2749 "parse.c"
        break;
      case 92: /* as ::= AS nm */
      case 219: /* plus_num ::= PLUS INTEGER|FLOAT */ yytestcase(yyruleno==219);
      case 220: /* minus_num ::= MINUS INTEGER|FLOAT */ yytestcase(yyruleno==220);
#line 551 "parse.y"
{yymsp[-1].minor.yy0 = yymsp[0].minor.yy0;}
#line 2756 "parse.c"
        break;
      case 94: /* from ::= */
#line 565 "parse.y"
{yymsp[1].minor.yy3 = sqlite3DbMallocZero(pParse->db, sizeof(*yymsp[1].minor.yy3));}
#line 2761 "parse.c"
        break;
      case 95: /* from ::= FROM seltablist */
#line 566 "parse.y"
{
  yymsp[-1].minor.yy3 = yymsp[0].minor.yy3;
  sqlite3SrcListShiftJoinType(yymsp[-1].minor.yy3);
}
#line 2769 "parse.c"
        break;
      case 96: /* stl_prefix ::= seltablist joinop */
#line 574 "parse.y"
{
   if( ALWAYS(yymsp[-1].minor.yy3 && yymsp[-1].minor.yy3->nSrc>0) ) yymsp[-1].minor.yy3->a[yymsp[-1].minor.yy3->nSrc-1].fg.jointype = (u8)yymsp[0].minor.yy228;
}
#line 2776 "parse.c"
        break;
      case 97: /* stl_prefix ::= */
#line 577 "parse.y"
{yymsp[1].minor.yy3 = 0;}
#line 2781 "parse.c"
        break;
      case 98: /* seltablist ::= stl_prefix nm as indexed_opt on_opt using_opt */
#line 579 "parse.y"
{
  yymsp[-5].minor.yy3 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-5].minor.yy3,&yymsp[-4].minor.yy0,0,&yymsp[-3].minor.yy0,0,yymsp[-1].minor.yy402,yymsp[0].minor.yy400);
  sqlite3SrcListIndexedBy(pParse, yymsp[-5].minor.yy3, &yymsp[-2].minor.yy0);
}
#line 2789 "parse.c"
        break;
      case 99: /* seltablist ::= stl_prefix nm LP exprlist RP as on_opt using_opt */
#line 584 "parse.y"
{
  yymsp[-7].minor.yy3 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-7].minor.yy3,&yymsp[-6].minor.yy0,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy402,yymsp[0].minor.yy400);
  sqlite3SrcListFuncArgs(pParse, yymsp[-7].minor.yy3, yymsp[-4].minor.yy258);
}
#line 2797 "parse.c"
        break;
      case 100: /* seltablist ::= stl_prefix LP select RP as on_opt using_opt */
#line 590 "parse.y"
{
    yymsp[-6].minor.yy3 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy3,0,0,&yymsp[-2].minor.yy0,yymsp[-4].minor.yy387,yymsp[-1].minor.yy402,yymsp[0].minor.yy400);
  }
#line 2804 "parse.c"
        break;
      case 101: /* seltablist ::= stl_prefix LP seltablist RP as on_opt using_opt */
#line 594 "parse.y"
{
    if( yymsp[-6].minor.yy3==0 && yymsp[-2].minor.yy0.n==0 && yymsp[-1].minor.yy402==0 && yymsp[0].minor.yy400==0 ){
      yymsp[-6].minor.yy3 = yymsp[-4].minor.yy3;
    }else if( yymsp[-4].minor.yy3->nSrc==1 ){
      yymsp[-6].minor.yy3 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy3,0,0,&yymsp[-2].minor.yy0,0,yymsp[-1].minor.yy402,yymsp[0].minor.yy400);
      if( yymsp[-6].minor.yy3 ){
        struct SrcList_item *pNew = &yymsp[-6].minor.yy3->a[yymsp[-6].minor.yy3->nSrc-1];
        struct SrcList_item *pOld = yymsp[-4].minor.yy3->a;
        pNew->zName = pOld->zName;
        pNew->zDatabase = pOld->zDatabase;
        pNew->pSelect = pOld->pSelect;
        pOld->zName = pOld->zDatabase = 0;
        pOld->pSelect = 0;
      }
      sqlite3SrcListDelete(pParse->db, yymsp[-4].minor.yy3);
    }else{
      Select *pSubquery;
      sqlite3SrcListShiftJoinType(yymsp[-4].minor.yy3);
      pSubquery = sqlite3SelectNew(pParse,0,yymsp[-4].minor.yy3,0,0,0,0,SF_NestedFrom,0,0);
      yymsp[-6].minor.yy3 = sqlite3SrcListAppendFromTerm(pParse,yymsp[-6].minor.yy3,0,0,&yymsp[-2].minor.yy0,pSubquery,yymsp[-1].minor.yy402,yymsp[0].minor.yy400);
    }
  }
#line 2830 "parse.c"
        break;
      case 102: /* fullname ::= nm */
#line 621 "parse.y"
{yymsp[0].minor.yy3 = sqlite3SrcListAppend(pParse->db,0,&yymsp[0].minor.yy0,0); /*A-overwrites-X*/}
#line 2835 "parse.c"
        break;
      case 103: /* joinop ::= COMMA|JOIN */
#line 624 "parse.y"
{ yymsp[0].minor.yy228 = JT_INNER; }
#line 2840 "parse.c"
        break;
      case 104: /* joinop ::= JOIN_KW JOIN */
#line 626 "parse.y"
{yymsp[-1].minor.yy228 = sqlite3JoinType(pParse,&yymsp[-1].minor.yy0,0,0);  /*X-overwrites-A*/}
#line 2845 "parse.c"
        break;
      case 105: /* joinop ::= JOIN_KW nm JOIN */
#line 628 "parse.y"
{yymsp[-2].minor.yy228 = sqlite3JoinType(pParse,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0,0); /*X-overwrites-A*/}
#line 2850 "parse.c"
        break;
      case 106: /* joinop ::= JOIN_KW nm nm JOIN */
#line 630 "parse.y"
{yymsp[-3].minor.yy228 = sqlite3JoinType(pParse,&yymsp[-3].minor.yy0,&yymsp[-2].minor.yy0,&yymsp[-1].minor.yy0);/*X-overwrites-A*/}
#line 2855 "parse.c"
        break;
      case 107: /* on_opt ::= ON expr */
      case 124: /* having_opt ::= HAVING expr */ yytestcase(yyruleno==124);
      case 131: /* where_opt ::= WHERE expr */ yytestcase(yyruleno==131);
      case 194: /* case_else ::= ELSE expr */ yytestcase(yyruleno==194);
#line 634 "parse.y"
{yymsp[-1].minor.yy402 = yymsp[0].minor.yy326.pExpr;}
#line 2863 "parse.c"
        break;
      case 108: /* on_opt ::= */
      case 123: /* having_opt ::= */ yytestcase(yyruleno==123);
      case 130: /* where_opt ::= */ yytestcase(yyruleno==130);
      case 195: /* case_else ::= */ yytestcase(yyruleno==195);
      case 197: /* case_operand ::= */ yytestcase(yyruleno==197);
#line 635 "parse.y"
{yymsp[1].minor.yy402 = 0;}
#line 2872 "parse.c"
        break;
      case 109: /* indexed_opt ::= */
#line 648 "parse.y"
{yymsp[1].minor.yy0.z=0; yymsp[1].minor.yy0.n=0;}
#line 2877 "parse.c"
        break;
      case 110: /* indexed_opt ::= INDEXED BY nm */
#line 649 "parse.y"
{yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;}
#line 2882 "parse.c"
        break;
      case 111: /* indexed_opt ::= NOT INDEXED */
#line 650 "parse.y"
{yymsp[-1].minor.yy0.z=0; yymsp[-1].minor.yy0.n=1;}
#line 2887 "parse.c"
        break;
      case 112: /* using_opt ::= USING LP idlist RP */
#line 654 "parse.y"
{yymsp[-3].minor.yy400 = yymsp[-1].minor.yy400;}
#line 2892 "parse.c"
        break;
      case 113: /* using_opt ::= */
      case 141: /* idlist_opt ::= */ yytestcase(yyruleno==141);
#line 655 "parse.y"
{yymsp[1].minor.yy400 = 0;}
#line 2898 "parse.c"
        break;
      case 115: /* orderby_opt ::= ORDER BY sortlist */
      case 122: /* groupby_opt ::= GROUP BY nexprlist */ yytestcase(yyruleno==122);
#line 669 "parse.y"
{yymsp[-2].minor.yy258 = yymsp[0].minor.yy258;}
#line 2904 "parse.c"
        break;
      case 116: /* sortlist ::= sortlist COMMA expr sortorder */
#line 670 "parse.y"
{
  yymsp[-3].minor.yy258 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy258,yymsp[-1].minor.yy326.pExpr);
  sqlite3ExprListSetSortOrder(yymsp[-3].minor.yy258,yymsp[0].minor.yy228);
}
#line 2912 "parse.c"
        break;
      case 117: /* sortlist ::= expr sortorder */
#line 674 "parse.y"
{
  yymsp[-1].minor.yy258 = sqlite3ExprListAppend(pParse,0,yymsp[-1].minor.yy326.pExpr); /*A-overwrites-Y*/
  sqlite3ExprListSetSortOrder(yymsp[-1].minor.yy258,yymsp[0].minor.yy228);
}
#line 2920 "parse.c"
        break;
      case 118: /* sortorder ::= ASC */
#line 681 "parse.y"
{yymsp[0].minor.yy228 = SQLITE_SO_ASC;}
#line 2925 "parse.c"
        break;
      case 119: /* sortorder ::= DESC */
#line 682 "parse.y"
{yymsp[0].minor.yy228 = SQLITE_SO_DESC;}
#line 2930 "parse.c"
        break;
      case 120: /* sortorder ::= */
#line 683 "parse.y"
{yymsp[1].minor.yy228 = SQLITE_SO_UNDEFINED;}
#line 2935 "parse.c"
        break;
      case 125: /* limit_opt ::= */
#line 708 "parse.y"
{yymsp[1].minor.yy196.pLimit = 0; yymsp[1].minor.yy196.pOffset = 0;}
#line 2940 "parse.c"
        break;
      case 126: /* limit_opt ::= LIMIT expr */
#line 709 "parse.y"
{yymsp[-1].minor.yy196.pLimit = yymsp[0].minor.yy326.pExpr; yymsp[-1].minor.yy196.pOffset = 0;}
#line 2945 "parse.c"
        break;
      case 127: /* limit_opt ::= LIMIT expr OFFSET expr */
#line 711 "parse.y"
{yymsp[-3].minor.yy196.pLimit = yymsp[-2].minor.yy326.pExpr; yymsp[-3].minor.yy196.pOffset = yymsp[0].minor.yy326.pExpr;}
#line 2950 "parse.c"
        break;
      case 128: /* limit_opt ::= LIMIT expr COMMA expr */
#line 713 "parse.y"
{yymsp[-3].minor.yy196.pOffset = yymsp[-2].minor.yy326.pExpr; yymsp[-3].minor.yy196.pLimit = yymsp[0].minor.yy326.pExpr;}
#line 2955 "parse.c"
        break;
      case 129: /* cmd ::= with DELETE FROM fullname indexed_opt where_opt */
#line 730 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy211, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-2].minor.yy3, &yymsp[-1].minor.yy0);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3DeleteFrom(pParse,yymsp[-2].minor.yy3,yymsp[0].minor.yy402);
}
#line 2967 "parse.c"
        break;
      case 132: /* cmd ::= with UPDATE orconf fullname indexed_opt SET setlist where_opt */
#line 763 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-7].minor.yy211, 1);
  sqlite3SrcListIndexedBy(pParse, yymsp[-4].minor.yy3, &yymsp[-3].minor.yy0);
  sqlite3ExprListCheckLength(pParse,yymsp[-1].minor.yy258,"set list"); 
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Update(pParse,yymsp[-4].minor.yy3,yymsp[-1].minor.yy258,yymsp[0].minor.yy402,yymsp[-5].minor.yy228);
}
#line 2980 "parse.c"
        break;
      case 133: /* setlist ::= setlist COMMA nm EQ expr */
#line 777 "parse.y"
{
  yymsp[-4].minor.yy258 = sqlite3ExprListAppend(pParse, yymsp[-4].minor.yy258, yymsp[0].minor.yy326.pExpr);
  sqlite3ExprListSetName(pParse, yymsp[-4].minor.yy258, &yymsp[-2].minor.yy0, 1);
}
#line 2988 "parse.c"
        break;
      case 134: /* setlist ::= setlist COMMA LP idlist RP EQ expr */
#line 781 "parse.y"
{
  yymsp[-6].minor.yy258 = sqlite3ExprListAppendVector(pParse, yymsp[-6].minor.yy258, yymsp[-3].minor.yy400, yymsp[0].minor.yy326.pExpr);
}
#line 2995 "parse.c"
        break;
      case 135: /* setlist ::= nm EQ expr */
#line 784 "parse.y"
{
  yylhsminor.yy258 = sqlite3ExprListAppend(pParse, 0, yymsp[0].minor.yy326.pExpr);
  sqlite3ExprListSetName(pParse, yylhsminor.yy258, &yymsp[-2].minor.yy0, 1);
}
#line 3003 "parse.c"
  yymsp[-2].minor.yy258 = yylhsminor.yy258;
        break;
      case 136: /* setlist ::= LP idlist RP EQ expr */
#line 788 "parse.y"
{
  yymsp[-4].minor.yy258 = sqlite3ExprListAppendVector(pParse, 0, yymsp[-3].minor.yy400, yymsp[0].minor.yy326.pExpr);
}
#line 3011 "parse.c"
        break;
      case 137: /* cmd ::= with insert_cmd INTO fullname idlist_opt select */
#line 794 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-5].minor.yy211, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-2].minor.yy3, yymsp[0].minor.yy387, yymsp[-1].minor.yy400, yymsp[-4].minor.yy228);
}
#line 3022 "parse.c"
        break;
      case 138: /* cmd ::= with insert_cmd INTO fullname idlist_opt DEFAULT VALUES */
#line 802 "parse.y"
{
  sqlite3WithPush(pParse, yymsp[-6].minor.yy211, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlite3Insert(pParse, yymsp[-3].minor.yy3, 0, yymsp[-2].minor.yy400, yymsp[-5].minor.yy228);
}
#line 3033 "parse.c"
        break;
      case 142: /* idlist_opt ::= LP idlist RP */
#line 820 "parse.y"
{yymsp[-2].minor.yy400 = yymsp[-1].minor.yy400;}
#line 3038 "parse.c"
        break;
      case 143: /* idlist ::= idlist COMMA nm */
#line 822 "parse.y"
{yymsp[-2].minor.yy400 = sqlite3IdListAppend(pParse->db,yymsp[-2].minor.yy400,&yymsp[0].minor.yy0);}
#line 3043 "parse.c"
        break;
      case 144: /* idlist ::= nm */
#line 824 "parse.y"
{yymsp[0].minor.yy400 = sqlite3IdListAppend(pParse->db,0,&yymsp[0].minor.yy0); /*A-overwrites-Y*/}
#line 3048 "parse.c"
        break;
      case 145: /* expr ::= LP expr RP */
#line 874 "parse.y"
{spanSet(&yymsp[-2].minor.yy326,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/  yymsp[-2].minor.yy326.pExpr = yymsp[-1].minor.yy326.pExpr;}
#line 3053 "parse.c"
        break;
      case 146: /* term ::= NULL */
      case 151: /* term ::= FLOAT|BLOB */ yytestcase(yyruleno==151);
      case 152: /* term ::= STRING */ yytestcase(yyruleno==152);
#line 875 "parse.y"
{spanExpr(&yymsp[0].minor.yy326,pParse,yymsp[0].major,yymsp[0].minor.yy0);/*A-overwrites-X*/}
#line 3060 "parse.c"
        break;
      case 147: /* expr ::= ID|INDEXED */
      case 148: /* expr ::= JOIN_KW */ yytestcase(yyruleno==148);
#line 876 "parse.y"
{spanExpr(&yymsp[0].minor.yy326,pParse,TK_ID,yymsp[0].minor.yy0); /*A-overwrites-X*/}
#line 3066 "parse.c"
        break;
      case 149: /* expr ::= nm DOT nm */
#line 878 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  spanSet(&yymsp[-2].minor.yy326,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-2].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp2);
}
#line 3076 "parse.c"
        break;
      case 150: /* expr ::= nm DOT nm DOT nm */
#line 884 "parse.y"
{
  Expr *temp1 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-4].minor.yy0, 1);
  Expr *temp2 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[-2].minor.yy0, 1);
  Expr *temp3 = sqlite3ExprAlloc(pParse->db, TK_ID, &yymsp[0].minor.yy0, 1);
  Expr *temp4 = sqlite3PExpr(pParse, TK_DOT, temp2, temp3);
  spanSet(&yymsp[-4].minor.yy326,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-4].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_DOT, temp1, temp4);
}
#line 3088 "parse.c"
        break;
      case 153: /* term ::= INTEGER */
#line 894 "parse.y"
{
  yylhsminor.yy326.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER, &yymsp[0].minor.yy0, 1);
  yylhsminor.yy326.zStart = yymsp[0].minor.yy0.z;
  yylhsminor.yy326.zEnd = yymsp[0].minor.yy0.z + yymsp[0].minor.yy0.n;
  if( yylhsminor.yy326.pExpr ) yylhsminor.yy326.pExpr->flags |= EP_Leaf;
}
#line 3098 "parse.c"
  yymsp[0].minor.yy326 = yylhsminor.yy326;
        break;
      case 154: /* expr ::= VARIABLE */
#line 900 "parse.y"
{
  if( !(yymsp[0].minor.yy0.z[0]=='#' && sqlite3Isdigit(yymsp[0].minor.yy0.z[1])) ){
    u32 n = yymsp[0].minor.yy0.n;
    spanExpr(&yymsp[0].minor.yy326, pParse, TK_VARIABLE, yymsp[0].minor.yy0);
    sqlite3ExprAssignVarNumber(pParse, yymsp[0].minor.yy326.pExpr, n);
  }else{
    /* When doing a nested parse, one can include terms in an expression
    ** that look like this:   #1 #2 ...  These terms refer to registers
    ** in the virtual machine.  #N is the N-th register. */
    Token t = yymsp[0].minor.yy0; /*A-overwrites-X*/
    assert( t.n>=2 );
    spanSet(&yymsp[0].minor.yy326, &t, &t);
    if( pParse->nested==0 ){
      sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &t);
      yymsp[0].minor.yy326.pExpr = 0;
    }else{
      yymsp[0].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_REGISTER, 0, 0);
      if( yymsp[0].minor.yy326.pExpr ) sqlite3GetInt32(&t.z[1], &yymsp[0].minor.yy326.pExpr->iTable);
    }
  }
}
#line 3124 "parse.c"
        break;
      case 155: /* expr ::= expr COLLATE ID|STRING */
#line 921 "parse.y"
{
  yymsp[-2].minor.yy326.pExpr = sqlite3ExprAddCollateToken(pParse, yymsp[-2].minor.yy326.pExpr, &yymsp[0].minor.yy0, 1);
  yymsp[-2].minor.yy326.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
}
#line 3132 "parse.c"
        break;
      case 156: /* expr ::= CAST LP expr AS typetoken RP */
#line 926 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy326,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-X*/
  yymsp[-5].minor.yy326.pExpr = sqlite3ExprAlloc(pParse->db, TK_CAST, &yymsp[-1].minor.yy0, 1);
  sqlite3ExprAttachSubtrees(pParse->db, yymsp[-5].minor.yy326.pExpr, yymsp[-3].minor.yy326.pExpr, 0);
}
#line 3141 "parse.c"
        break;
      case 157: /* expr ::= ID|INDEXED LP distinct exprlist RP */
#line 932 "parse.y"
{
  if( yymsp[-1].minor.yy258 && yymsp[-1].minor.yy258->nExpr>pParse->db->aLimit[SQLITE_LIMIT_FUNCTION_ARG] ){
    sqlite3ErrorMsg(pParse, "too many arguments on function %T", &yymsp[-4].minor.yy0);
  }
  yylhsminor.yy326.pExpr = sqlite3ExprFunction(pParse, yymsp[-1].minor.yy258, &yymsp[-4].minor.yy0);
  spanSet(&yylhsminor.yy326,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);
  if( yymsp[-2].minor.yy228==SF_Distinct && yylhsminor.yy326.pExpr ){
    yylhsminor.yy326.pExpr->flags |= EP_Distinct;
  }
}
#line 3155 "parse.c"
  yymsp[-4].minor.yy326 = yylhsminor.yy326;
        break;
      case 158: /* expr ::= ID|INDEXED LP STAR RP */
#line 942 "parse.y"
{
  yylhsminor.yy326.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[-3].minor.yy0);
  spanSet(&yylhsminor.yy326,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);
}
#line 3164 "parse.c"
  yymsp[-3].minor.yy326 = yylhsminor.yy326;
        break;
      case 159: /* term ::= CTIME_KW */
#line 946 "parse.y"
{
  yylhsminor.yy326.pExpr = sqlite3ExprFunction(pParse, 0, &yymsp[0].minor.yy0);
  spanSet(&yylhsminor.yy326, &yymsp[0].minor.yy0, &yymsp[0].minor.yy0);
}
#line 3173 "parse.c"
  yymsp[0].minor.yy326 = yylhsminor.yy326;
        break;
      case 160: /* expr ::= LP nexprlist COMMA expr RP */
#line 975 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse, yymsp[-3].minor.yy258, yymsp[-1].minor.yy326.pExpr);
  yylhsminor.yy326.pExpr = sqlite3PExpr(pParse, TK_VECTOR, 0, 0);
  if( yylhsminor.yy326.pExpr ){
    yylhsminor.yy326.pExpr->x.pList = pList;
    spanSet(&yylhsminor.yy326, &yymsp[-4].minor.yy0, &yymsp[0].minor.yy0);
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  }
}
#line 3188 "parse.c"
  yymsp[-4].minor.yy326 = yylhsminor.yy326;
        break;
      case 161: /* expr ::= expr AND expr */
      case 162: /* expr ::= expr OR expr */ yytestcase(yyruleno==162);
      case 163: /* expr ::= expr LT|GT|GE|LE expr */ yytestcase(yyruleno==163);
      case 164: /* expr ::= expr EQ|NE expr */ yytestcase(yyruleno==164);
      case 165: /* expr ::= expr BITAND|BITOR|LSHIFT|RSHIFT expr */ yytestcase(yyruleno==165);
      case 166: /* expr ::= expr PLUS|MINUS expr */ yytestcase(yyruleno==166);
      case 167: /* expr ::= expr STAR|SLASH|REM expr */ yytestcase(yyruleno==167);
      case 168: /* expr ::= expr CONCAT expr */ yytestcase(yyruleno==168);
#line 986 "parse.y"
{spanBinaryExpr(pParse,yymsp[-1].major,&yymsp[-2].minor.yy326,&yymsp[0].minor.yy326);}
#line 3201 "parse.c"
        break;
      case 169: /* likeop ::= LIKE_KW|MATCH */
#line 999 "parse.y"
{yymsp[0].minor.yy0=yymsp[0].minor.yy0;/*A-overwrites-X*/}
#line 3206 "parse.c"
        break;
      case 170: /* likeop ::= NOT LIKE_KW|MATCH */
#line 1000 "parse.y"
{yymsp[-1].minor.yy0=yymsp[0].minor.yy0; yymsp[-1].minor.yy0.n|=0x80000000; /*yymsp[-1].minor.yy0-overwrite-yymsp[0].minor.yy0*/}
#line 3211 "parse.c"
        break;
      case 171: /* expr ::= expr likeop expr */
#line 1001 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-1].minor.yy0.n & 0x80000000;
  yymsp[-1].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[0].minor.yy326.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-2].minor.yy326.pExpr);
  yymsp[-2].minor.yy326.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-1].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-2].minor.yy326);
  yymsp[-2].minor.yy326.zEnd = yymsp[0].minor.yy326.zEnd;
  if( yymsp[-2].minor.yy326.pExpr ) yymsp[-2].minor.yy326.pExpr->flags |= EP_InfixFunc;
}
#line 3226 "parse.c"
        break;
      case 172: /* expr ::= expr likeop expr ESCAPE expr */
#line 1012 "parse.y"
{
  ExprList *pList;
  int bNot = yymsp[-3].minor.yy0.n & 0x80000000;
  yymsp[-3].minor.yy0.n &= 0x7fffffff;
  pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy326.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[-4].minor.yy326.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy326.pExpr);
  yymsp[-4].minor.yy326.pExpr = sqlite3ExprFunction(pParse, pList, &yymsp[-3].minor.yy0);
  exprNot(pParse, bNot, &yymsp[-4].minor.yy326);
  yymsp[-4].minor.yy326.zEnd = yymsp[0].minor.yy326.zEnd;
  if( yymsp[-4].minor.yy326.pExpr ) yymsp[-4].minor.yy326.pExpr->flags |= EP_InfixFunc;
}
#line 3242 "parse.c"
        break;
      case 173: /* expr ::= expr ISNULL|NOTNULL */
#line 1039 "parse.y"
{spanUnaryPostfix(pParse,yymsp[0].major,&yymsp[-1].minor.yy326,&yymsp[0].minor.yy0);}
#line 3247 "parse.c"
        break;
      case 174: /* expr ::= expr NOT NULL */
#line 1040 "parse.y"
{spanUnaryPostfix(pParse,TK_NOTNULL,&yymsp[-2].minor.yy326,&yymsp[0].minor.yy0);}
#line 3252 "parse.c"
        break;
      case 175: /* expr ::= expr IS expr */
#line 1061 "parse.y"
{
  spanBinaryExpr(pParse,TK_IS,&yymsp[-2].minor.yy326,&yymsp[0].minor.yy326);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy326.pExpr, yymsp[-2].minor.yy326.pExpr, TK_ISNULL);
}
#line 3260 "parse.c"
        break;
      case 176: /* expr ::= expr IS NOT expr */
#line 1065 "parse.y"
{
  spanBinaryExpr(pParse,TK_ISNOT,&yymsp[-3].minor.yy326,&yymsp[0].minor.yy326);
  binaryToUnaryIfNull(pParse, yymsp[0].minor.yy326.pExpr, yymsp[-3].minor.yy326.pExpr, TK_NOTNULL);
}
#line 3268 "parse.c"
        break;
      case 177: /* expr ::= NOT expr */
      case 178: /* expr ::= BITNOT expr */ yytestcase(yyruleno==178);
#line 1089 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy326,pParse,yymsp[-1].major,&yymsp[0].minor.yy326,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3274 "parse.c"
        break;
      case 179: /* expr ::= MINUS expr */
#line 1093 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy326,pParse,TK_UMINUS,&yymsp[0].minor.yy326,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3279 "parse.c"
        break;
      case 180: /* expr ::= PLUS expr */
#line 1095 "parse.y"
{spanUnaryPrefix(&yymsp[-1].minor.yy326,pParse,TK_UPLUS,&yymsp[0].minor.yy326,&yymsp[-1].minor.yy0);/*A-overwrites-B*/}
#line 3284 "parse.c"
        break;
      case 181: /* between_op ::= BETWEEN */
      case 184: /* in_op ::= IN */ yytestcase(yyruleno==184);
#line 1098 "parse.y"
{yymsp[0].minor.yy228 = 0;}
#line 3290 "parse.c"
        break;
      case 183: /* expr ::= expr between_op expr AND expr */
#line 1100 "parse.y"
{
  ExprList *pList = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy326.pExpr);
  pList = sqlite3ExprListAppend(pParse,pList, yymsp[0].minor.yy326.pExpr);
  yymsp[-4].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_BETWEEN, yymsp[-4].minor.yy326.pExpr, 0);
  if( yymsp[-4].minor.yy326.pExpr ){
    yymsp[-4].minor.yy326.pExpr->x.pList = pList;
  }else{
    sqlite3ExprListDelete(pParse->db, pList);
  } 
  exprNot(pParse, yymsp[-3].minor.yy228, &yymsp[-4].minor.yy326);
  yymsp[-4].minor.yy326.zEnd = yymsp[0].minor.yy326.zEnd;
}
#line 3306 "parse.c"
        break;
      case 186: /* expr ::= expr in_op LP exprlist RP */
#line 1116 "parse.y"
{
    if( yymsp[-1].minor.yy258==0 ){
      /* Expressions of the form
      **
      **      expr1 IN ()
      **      expr1 NOT IN ()
      **
      ** simplify to constants 0 (false) and 1 (true), respectively,
      ** regardless of the value of expr1.
      */
      sqlite3ExprDelete(pParse->db, yymsp[-4].minor.yy326.pExpr);
      yymsp[-4].minor.yy326.pExpr = sqlite3ExprAlloc(pParse->db, TK_INTEGER,&sqlite3IntTokens[yymsp[-3].minor.yy228],1);
    }else if( yymsp[-1].minor.yy258->nExpr==1 ){
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
      Expr *pRHS = yymsp[-1].minor.yy258->a[0].pExpr;
      yymsp[-1].minor.yy258->a[0].pExpr = 0;
      sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy258);
      /* pRHS cannot be NULL because a malloc error would have been detected
      ** before now and control would have never reached this point */
      if( ALWAYS(pRHS) ){
        pRHS->flags &= ~EP_Collate;
        pRHS->flags |= EP_Generic;
      }
      yymsp[-4].minor.yy326.pExpr = sqlite3PExpr(pParse, yymsp[-3].minor.yy228 ? TK_NE : TK_EQ, yymsp[-4].minor.yy326.pExpr, pRHS);
    }else{
      yymsp[-4].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy326.pExpr, 0);
      if( yymsp[-4].minor.yy326.pExpr ){
        yymsp[-4].minor.yy326.pExpr->x.pList = yymsp[-1].minor.yy258;
        sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy326.pExpr);
      }else{
        sqlite3ExprListDelete(pParse->db, yymsp[-1].minor.yy258);
      }
      exprNot(pParse, yymsp[-3].minor.yy228, &yymsp[-4].minor.yy326);
    }
    yymsp[-4].minor.yy326.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3361 "parse.c"
        break;
      case 187: /* expr ::= LP select RP */
#line 1167 "parse.y"
{
    spanSet(&yymsp[-2].minor.yy326,&yymsp[-2].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    yymsp[-2].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_SELECT, 0, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-2].minor.yy326.pExpr, yymsp[-1].minor.yy387);
  }
#line 3370 "parse.c"
        break;
      case 188: /* expr ::= expr in_op LP select RP */
#line 1172 "parse.y"
{
    yymsp[-4].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-4].minor.yy326.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-4].minor.yy326.pExpr, yymsp[-1].minor.yy387);
    exprNot(pParse, yymsp[-3].minor.yy228, &yymsp[-4].minor.yy326);
    yymsp[-4].minor.yy326.zEnd = &yymsp[0].minor.yy0.z[yymsp[0].minor.yy0.n];
  }
#line 3380 "parse.c"
        break;
      case 189: /* expr ::= expr in_op nm paren_exprlist */
#line 1178 "parse.y"
{
    SrcList *pSrc = sqlite3SrcListAppend(pParse->db, 0,&yymsp[-1].minor.yy0,0);
    Select *pSelect = sqlite3SelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
    if( yymsp[0].minor.yy258 )  sqlite3SrcListFuncArgs(pParse, pSelect ? pSrc : 0, yymsp[0].minor.yy258);
    yymsp[-3].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_IN, yymsp[-3].minor.yy326.pExpr, 0);
    sqlite3PExprAddSelect(pParse, yymsp[-3].minor.yy326.pExpr, pSelect);
    exprNot(pParse, yymsp[-2].minor.yy228, &yymsp[-3].minor.yy326);
    yymsp[-3].minor.yy326.zEnd = &yymsp[-1].minor.yy0.z[yymsp[-1].minor.yy0.n];
  }
#line 3393 "parse.c"
        break;
      case 190: /* expr ::= EXISTS LP select RP */
#line 1187 "parse.y"
{
    Expr *p;
    spanSet(&yymsp[-3].minor.yy326,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0); /*A-overwrites-B*/
    p = yymsp[-3].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_EXISTS, 0, 0);
    sqlite3PExprAddSelect(pParse, p, yymsp[-1].minor.yy387);
  }
#line 3403 "parse.c"
        break;
      case 191: /* expr ::= CASE case_operand case_exprlist case_else END */
#line 1196 "parse.y"
{
  spanSet(&yymsp[-4].minor.yy326,&yymsp[-4].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-C*/
  yymsp[-4].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_CASE, yymsp[-3].minor.yy402, 0);
  if( yymsp[-4].minor.yy326.pExpr ){
    yymsp[-4].minor.yy326.pExpr->x.pList = yymsp[-1].minor.yy402 ? sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy258,yymsp[-1].minor.yy402) : yymsp[-2].minor.yy258;
    sqlite3ExprSetHeightAndFlags(pParse, yymsp[-4].minor.yy326.pExpr);
  }else{
    sqlite3ExprListDelete(pParse->db, yymsp[-2].minor.yy258);
    sqlite3ExprDelete(pParse->db, yymsp[-1].minor.yy402);
  }
}
#line 3418 "parse.c"
        break;
      case 192: /* case_exprlist ::= case_exprlist WHEN expr THEN expr */
#line 1209 "parse.y"
{
  yymsp[-4].minor.yy258 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy258, yymsp[-2].minor.yy326.pExpr);
  yymsp[-4].minor.yy258 = sqlite3ExprListAppend(pParse,yymsp[-4].minor.yy258, yymsp[0].minor.yy326.pExpr);
}
#line 3426 "parse.c"
        break;
      case 193: /* case_exprlist ::= WHEN expr THEN expr */
#line 1213 "parse.y"
{
  yymsp[-3].minor.yy258 = sqlite3ExprListAppend(pParse,0, yymsp[-2].minor.yy326.pExpr);
  yymsp[-3].minor.yy258 = sqlite3ExprListAppend(pParse,yymsp[-3].minor.yy258, yymsp[0].minor.yy326.pExpr);
}
#line 3434 "parse.c"
        break;
      case 196: /* case_operand ::= expr */
#line 1223 "parse.y"
{yymsp[0].minor.yy402 = yymsp[0].minor.yy326.pExpr; /*A-overwrites-X*/}
#line 3439 "parse.c"
        break;
      case 199: /* nexprlist ::= nexprlist COMMA expr */
#line 1234 "parse.y"
{yymsp[-2].minor.yy258 = sqlite3ExprListAppend(pParse,yymsp[-2].minor.yy258,yymsp[0].minor.yy326.pExpr);}
#line 3444 "parse.c"
        break;
      case 200: /* nexprlist ::= expr */
#line 1236 "parse.y"
{yymsp[0].minor.yy258 = sqlite3ExprListAppend(pParse,0,yymsp[0].minor.yy326.pExpr); /*A-overwrites-Y*/}
#line 3449 "parse.c"
        break;
      case 202: /* paren_exprlist ::= LP exprlist RP */
      case 207: /* eidlist_opt ::= LP eidlist RP */ yytestcase(yyruleno==207);
#line 1244 "parse.y"
{yymsp[-2].minor.yy258 = yymsp[-1].minor.yy258;}
#line 3455 "parse.c"
        break;
      case 203: /* cmd ::= createkw uniqueflag INDEX ifnotexists nm ON nm LP sortlist RP where_opt */
#line 1251 "parse.y"
{
  sqlite3CreateIndex(pParse, &yymsp[-6].minor.yy0, 
                     sqlite3SrcListAppend(pParse->db,0,&yymsp[-4].minor.yy0,0), yymsp[-2].minor.yy258, yymsp[-9].minor.yy228,
                      &yymsp[-10].minor.yy0, yymsp[0].minor.yy402, SQLITE_SO_ASC, yymsp[-7].minor.yy228, SQLITE_IDXTYPE_APPDEF);
}
#line 3464 "parse.c"
        break;
      case 204: /* uniqueflag ::= UNIQUE */
      case 244: /* raisetype ::= ABORT */ yytestcase(yyruleno==244);
#line 1258 "parse.y"
{yymsp[0].minor.yy228 = OE_Abort;}
#line 3470 "parse.c"
        break;
      case 205: /* uniqueflag ::= */
#line 1259 "parse.y"
{yymsp[1].minor.yy228 = OE_None;}
#line 3475 "parse.c"
        break;
      case 208: /* eidlist ::= eidlist COMMA nm collate sortorder */
#line 1302 "parse.y"
{
  yymsp[-4].minor.yy258 = parserAddExprIdListTerm(pParse, yymsp[-4].minor.yy258, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy228, yymsp[0].minor.yy228);
}
#line 3482 "parse.c"
        break;
      case 209: /* eidlist ::= nm collate sortorder */
#line 1305 "parse.y"
{
  yymsp[-2].minor.yy258 = parserAddExprIdListTerm(pParse, 0, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy228, yymsp[0].minor.yy228); /*A-overwrites-Y*/
}
#line 3489 "parse.c"
        break;
      case 212: /* cmd ::= DROP INDEX ifexists fullname ON nm */
#line 1316 "parse.y"
{
    sqlite3DropIndex(pParse, yymsp[-2].minor.yy3, &yymsp[0].minor.yy0, yymsp[-3].minor.yy228);
}
#line 3496 "parse.c"
        break;
      case 213: /* cmd ::= PRAGMA nm */
#line 1323 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[0].minor.yy0,0,0,0,0);
}
#line 3503 "parse.c"
        break;
      case 214: /* cmd ::= PRAGMA nm EQ nmnum */
#line 1326 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,0);
}
#line 3510 "parse.c"
        break;
      case 215: /* cmd ::= PRAGMA nm LP nmnum RP */
#line 1329 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,0);
}
#line 3517 "parse.c"
        break;
      case 216: /* cmd ::= PRAGMA nm EQ minus_num */
#line 1332 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-2].minor.yy0,0,&yymsp[0].minor.yy0,0,1);
}
#line 3524 "parse.c"
        break;
      case 217: /* cmd ::= PRAGMA nm LP minus_num RP */
#line 1335 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-3].minor.yy0,0,&yymsp[-1].minor.yy0,0,1);
}
#line 3531 "parse.c"
        break;
      case 218: /* cmd ::= PRAGMA nm EQ nm DOT nm */
#line 1338 "parse.y"
{
    sqlite3Pragma(pParse,&yymsp[-4].minor.yy0,0,&yymsp[0].minor.yy0,&yymsp[-2].minor.yy0,0);
}
#line 3538 "parse.c"
        break;
      case 221: /* cmd ::= createkw trigger_decl BEGIN trigger_cmd_list END */
#line 1357 "parse.y"
{
  Token all;
  all.z = yymsp[-3].minor.yy0.z;
  all.n = (int)(yymsp[0].minor.yy0.z - yymsp[-3].minor.yy0.z) + yymsp[0].minor.yy0.n;
  sqlite3FinishTrigger(pParse, yymsp[-1].minor.yy19, &all);
}
#line 3548 "parse.c"
        break;
      case 222: /* trigger_decl ::= TRIGGER ifnotexists nm trigger_time trigger_event ON fullname foreach_clause when_clause */
#line 1366 "parse.y"
{
  sqlite3BeginTrigger(pParse, &yymsp[-6].minor.yy0, yymsp[-5].minor.yy228, yymsp[-4].minor.yy466.a, yymsp[-4].minor.yy466.b, yymsp[-2].minor.yy3, yymsp[0].minor.yy402, yymsp[-7].minor.yy228);
  yymsp[-8].minor.yy0 = yymsp[-6].minor.yy0; /*yymsp[-8].minor.yy0-overwrites-T*/
}
#line 3556 "parse.c"
        break;
      case 223: /* trigger_time ::= BEFORE */
#line 1372 "parse.y"
{ yymsp[0].minor.yy228 = TK_BEFORE; }
#line 3561 "parse.c"
        break;
      case 224: /* trigger_time ::= AFTER */
#line 1373 "parse.y"
{ yymsp[0].minor.yy228 = TK_AFTER;  }
#line 3566 "parse.c"
        break;
      case 225: /* trigger_time ::= INSTEAD OF */
#line 1374 "parse.y"
{ yymsp[-1].minor.yy228 = TK_INSTEAD;}
#line 3571 "parse.c"
        break;
      case 226: /* trigger_time ::= */
#line 1375 "parse.y"
{ yymsp[1].minor.yy228 = TK_BEFORE; }
#line 3576 "parse.c"
        break;
      case 227: /* trigger_event ::= DELETE|INSERT */
      case 228: /* trigger_event ::= UPDATE */ yytestcase(yyruleno==228);
#line 1379 "parse.y"
{yymsp[0].minor.yy466.a = yymsp[0].major; /*A-overwrites-X*/ yymsp[0].minor.yy466.b = 0;}
#line 3582 "parse.c"
        break;
      case 229: /* trigger_event ::= UPDATE OF idlist */
#line 1381 "parse.y"
{yymsp[-2].minor.yy466.a = TK_UPDATE; yymsp[-2].minor.yy466.b = yymsp[0].minor.yy400;}
#line 3587 "parse.c"
        break;
      case 230: /* when_clause ::= */
#line 1388 "parse.y"
{ yymsp[1].minor.yy402 = 0; }
#line 3592 "parse.c"
        break;
      case 231: /* when_clause ::= WHEN expr */
#line 1389 "parse.y"
{ yymsp[-1].minor.yy402 = yymsp[0].minor.yy326.pExpr; }
#line 3597 "parse.c"
        break;
      case 232: /* trigger_cmd_list ::= trigger_cmd_list trigger_cmd SEMI */
#line 1393 "parse.y"
{
  assert( yymsp[-2].minor.yy19!=0 );
  yymsp[-2].minor.yy19->pLast->pNext = yymsp[-1].minor.yy19;
  yymsp[-2].minor.yy19->pLast = yymsp[-1].minor.yy19;
}
#line 3606 "parse.c"
        break;
      case 233: /* trigger_cmd_list ::= trigger_cmd SEMI */
#line 1398 "parse.y"
{ 
  assert( yymsp[-1].minor.yy19!=0 );
  yymsp[-1].minor.yy19->pLast = yymsp[-1].minor.yy19;
}
#line 3614 "parse.c"
        break;
      case 234: /* trnm ::= nm DOT nm */
#line 1409 "parse.y"
{
  yymsp[-2].minor.yy0 = yymsp[0].minor.yy0;
  sqlite3ErrorMsg(pParse, 
        "qualified table names are not allowed on INSERT, UPDATE, and DELETE "
        "statements within triggers");
}
#line 3624 "parse.c"
        break;
      case 235: /* tridxby ::= INDEXED BY nm */
#line 1421 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the INDEXED BY clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3633 "parse.c"
        break;
      case 236: /* tridxby ::= NOT INDEXED */
#line 1426 "parse.y"
{
  sqlite3ErrorMsg(pParse,
        "the NOT INDEXED clause is not allowed on UPDATE or DELETE statements "
        "within triggers");
}
#line 3642 "parse.c"
        break;
      case 237: /* trigger_cmd ::= UPDATE orconf trnm tridxby SET setlist where_opt */
#line 1439 "parse.y"
{yymsp[-6].minor.yy19 = sqlite3TriggerUpdateStep(pParse->db, &yymsp[-4].minor.yy0, yymsp[-1].minor.yy258, yymsp[0].minor.yy402, yymsp[-5].minor.yy228);}
#line 3647 "parse.c"
        break;
      case 238: /* trigger_cmd ::= insert_cmd INTO trnm idlist_opt select */
#line 1443 "parse.y"
{yymsp[-4].minor.yy19 = sqlite3TriggerInsertStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[-1].minor.yy400, yymsp[0].minor.yy387, yymsp[-4].minor.yy228);/*A-overwrites-R*/}
#line 3652 "parse.c"
        break;
      case 239: /* trigger_cmd ::= DELETE FROM trnm tridxby where_opt */
#line 1447 "parse.y"
{yymsp[-4].minor.yy19 = sqlite3TriggerDeleteStep(pParse->db, &yymsp[-2].minor.yy0, yymsp[0].minor.yy402);}
#line 3657 "parse.c"
        break;
      case 240: /* trigger_cmd ::= select */
#line 1451 "parse.y"
{yymsp[0].minor.yy19 = sqlite3TriggerSelectStep(pParse->db, yymsp[0].minor.yy387); /*A-overwrites-X*/}
#line 3662 "parse.c"
        break;
      case 241: /* expr ::= RAISE LP IGNORE RP */
#line 1454 "parse.y"
{
  spanSet(&yymsp[-3].minor.yy326,&yymsp[-3].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-3].minor.yy326.pExpr = sqlite3PExpr(pParse, TK_RAISE, 0, 0); 
  if( yymsp[-3].minor.yy326.pExpr ){
    yymsp[-3].minor.yy326.pExpr->affinity = OE_Ignore;
  }
}
#line 3673 "parse.c"
        break;
      case 242: /* expr ::= RAISE LP raisetype COMMA nm RP */
#line 1461 "parse.y"
{
  spanSet(&yymsp[-5].minor.yy326,&yymsp[-5].minor.yy0,&yymsp[0].minor.yy0);  /*A-overwrites-X*/
  yymsp[-5].minor.yy326.pExpr = sqlite3ExprAlloc(pParse->db, TK_RAISE, &yymsp[-1].minor.yy0, 1); 
  if( yymsp[-5].minor.yy326.pExpr ) {
    yymsp[-5].minor.yy326.pExpr->affinity = (char)yymsp[-3].minor.yy228;
  }
}
#line 3684 "parse.c"
        break;
      case 243: /* raisetype ::= ROLLBACK */
#line 1471 "parse.y"
{yymsp[0].minor.yy228 = OE_Rollback;}
#line 3689 "parse.c"
        break;
      case 245: /* raisetype ::= FAIL */
#line 1473 "parse.y"
{yymsp[0].minor.yy228 = OE_Fail;}
#line 3694 "parse.c"
        break;
      case 246: /* cmd ::= DROP TRIGGER ifexists fullname */
#line 1478 "parse.y"
{
  sqlite3DropTrigger(pParse,yymsp[0].minor.yy3,yymsp[-1].minor.yy228);
}
#line 3701 "parse.c"
        break;
      case 247: /* cmd ::= REINDEX */
#line 1485 "parse.y"
{sqlite3Reindex(pParse, 0, 0);}
#line 3706 "parse.c"
        break;
      case 248: /* cmd ::= REINDEX nm */
#line 1486 "parse.y"
{sqlite3Reindex(pParse, &yymsp[0].minor.yy0, 0);}
#line 3711 "parse.c"
        break;
      case 249: /* cmd ::= REINDEX nm ON nm */
#line 1487 "parse.y"
{sqlite3Reindex(pParse, &yymsp[-2].minor.yy0, &yymsp[0].minor.yy0);}
#line 3716 "parse.c"
        break;
      case 250: /* cmd ::= ANALYZE */
#line 1492 "parse.y"
{sqlite3Analyze(pParse, 0);}
#line 3721 "parse.c"
        break;
      case 251: /* cmd ::= ANALYZE nm */
#line 1493 "parse.y"
{sqlite3Analyze(pParse, &yymsp[0].minor.yy0);}
#line 3726 "parse.c"
        break;
      case 252: /* cmd ::= ALTER TABLE fullname RENAME TO nm */
#line 1498 "parse.y"
{
  sqlite3AlterRenameTable(pParse,yymsp[-3].minor.yy3,&yymsp[0].minor.yy0);
}
#line 3733 "parse.c"
        break;
      case 253: /* cmd ::= ALTER TABLE add_column_fullname ADD kwcolumn_opt columnname carglist */
#line 1502 "parse.y"
{
  yymsp[-1].minor.yy0.n = (int)(pParse->sLastToken.z-yymsp[-1].minor.yy0.z) + pParse->sLastToken.n;
  sqlite3AlterFinishAddColumn(pParse, &yymsp[-1].minor.yy0);
}
#line 3741 "parse.c"
        break;
      case 254: /* add_column_fullname ::= fullname */
#line 1506 "parse.y"
{
  disableLookaside(pParse);
  sqlite3AlterBeginAddColumn(pParse, yymsp[0].minor.yy3);
}
#line 3749 "parse.c"
        break;
      case 255: /* with ::= */
#line 1520 "parse.y"
{yymsp[1].minor.yy211 = 0;}
#line 3754 "parse.c"
        break;
      case 256: /* with ::= WITH wqlist */
#line 1522 "parse.y"
{ yymsp[-1].minor.yy211 = yymsp[0].minor.yy211; }
#line 3759 "parse.c"
        break;
      case 257: /* with ::= WITH RECURSIVE wqlist */
#line 1523 "parse.y"
{ yymsp[-2].minor.yy211 = yymsp[0].minor.yy211; }
#line 3764 "parse.c"
        break;
      case 258: /* wqlist ::= nm eidlist_opt AS LP select RP */
#line 1525 "parse.y"
{
  yymsp[-5].minor.yy211 = sqlite3WithAdd(pParse, 0, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy258, yymsp[-1].minor.yy387); /*A-overwrites-X*/
}
#line 3771 "parse.c"
        break;
      case 259: /* wqlist ::= wqlist COMMA nm eidlist_opt AS LP select RP */
#line 1528 "parse.y"
{
  yymsp[-7].minor.yy211 = sqlite3WithAdd(pParse, yymsp[-7].minor.yy211, &yymsp[-5].minor.yy0, yymsp[-4].minor.yy258, yymsp[-1].minor.yy387);
}
#line 3778 "parse.c"
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
      /* (271) nm ::= ID|INDEXED */ yytestcase(yyruleno==271);
      /* (272) nm ::= STRING */ yytestcase(yyruleno==272);
      /* (273) nm ::= JOIN_KW */ yytestcase(yyruleno==273);
      /* (274) typetoken ::= typename */ yytestcase(yyruleno==274);
      /* (275) typename ::= ID|STRING */ yytestcase(yyruleno==275);
      /* (276) signed ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=276);
      /* (277) signed ::= minus_num (OPTIMIZED OUT) */ assert(yyruleno!=277);
      /* (278) carglist ::= carglist ccons */ yytestcase(yyruleno==278);
      /* (279) carglist ::= */ yytestcase(yyruleno==279);
      /* (280) ccons ::= NULL onconf */ yytestcase(yyruleno==280);
      /* (281) conslist_opt ::= COMMA conslist */ yytestcase(yyruleno==281);
      /* (282) conslist ::= conslist tconscomma tcons */ yytestcase(yyruleno==282);
      /* (283) conslist ::= tcons (OPTIMIZED OUT) */ assert(yyruleno!=283);
      /* (284) tconscomma ::= */ yytestcase(yyruleno==284);
      /* (285) defer_subclause_opt ::= defer_subclause (OPTIMIZED OUT) */ assert(yyruleno!=285);
      /* (286) resolvetype ::= raisetype (OPTIMIZED OUT) */ assert(yyruleno!=286);
      /* (287) selectnowith ::= oneselect (OPTIMIZED OUT) */ assert(yyruleno!=287);
      /* (288) oneselect ::= values */ yytestcase(yyruleno==288);
      /* (289) sclp ::= selcollist COMMA */ yytestcase(yyruleno==289);
      /* (290) as ::= ID|STRING */ yytestcase(yyruleno==290);
      /* (291) expr ::= term (OPTIMIZED OUT) */ assert(yyruleno!=291);
      /* (292) exprlist ::= nexprlist */ yytestcase(yyruleno==292);
      /* (293) nmnum ::= plus_num (OPTIMIZED OUT) */ assert(yyruleno!=293);
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
  sqlite3ErrorMsg(pParse, "near \"%T\": syntax error", &TOKEN);
#line 3887 "parse.c"
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
