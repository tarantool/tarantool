/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains sql's grammar for SQL.  Process this file
** using the lemon parser generator to generate C code that runs
** the parser.  Lemon will also generate a header file containing
** numeric codes for all of the tokens.
*/

// All token codes are small integers with #defines that begin with "TK_"
%token_prefix TK_

// The type of the data attached to each token is Token.  This is also the
// default type for non-terminals.
//
%token_type {Token}
%default_type {Token}

// The generated parser function takes a 4th argument as follows:
%extra_argument {Parse *pParse}

// This code runs whenever there is a syntax error
//
%syntax_error {
  UNUSED_PARAMETER(yymajor);  /* Silence some compiler warnings */
  assert( TOKEN.z[0] );  /* The tokenizer always gives us a token */
  if (yypParser->is_fallback_failed && TOKEN.isReserved) {
    diag_set(ClientError, ER_SQL_KEYWORD_IS_RESERVED, pParse->line_count,
             pParse->line_pos, TOKEN.n, TOKEN.z, TOKEN.n, TOKEN.z);
  } else {
    diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN, pParse->line_count, TOKEN.n,
             TOKEN.z);
  }
  pParse->is_aborted = true;
}
%stack_overflow {
  diag_set(ClientError, ER_SQL_STACK_OVERFLOW);
  pParse->is_aborted = true;
}

// The name of the generated procedure that implements the parser
// is as follows:
%name sqlParser

// The following text is included near the beginning of the C source
// code file that implements the parser.
//
%include {
#include "sqlInt.h"
#include "box/fk_constraint.h"

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
** Indicate that sqlParserFree() will never be called with a null
** pointer.
*/
#define YYPARSEFREENEVERNULL 1

/*
** Alternative datatype for the argument to the malloc() routine passed
** into sqlParserAlloc().  The default is size_t.
*/
#define YYMALLOCARGTYPE  u64

/*
 * Stop the parser if an error occurs. This macro adds an
 * additional check that allows the parser to be stopped if any
 * error was noticed.
 */
#define PARSER_ERROR_CHECK && ! pParse->is_aborted

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

} // end %include

// Input is a single SQL command
input ::= ecmd.
ecmd ::= explain cmdx SEMI. {
  if (!pParse->parse_only || !SQL_PARSE_VALID_AST(pParse))
    sql_finish_coding(pParse);
}
ecmd ::= SEMI. {
  diag_set(ClientError, ER_SQL_STATEMENT_EMPTY);
  pParse->is_aborted = true;
}
explain ::= .
explain ::= EXPLAIN.              { pParse->explain = 1; }
explain ::= EXPLAIN QUERY PLAN.   { pParse->explain = 2; }
cmdx ::= cmd.

// Define operator precedence early so that this is the first occurrence
// of the operator tokens in the grammer.  Keeping the operators together
// causes them to be assigned integer values that are close together,
// which keeps parser tables smaller.
//
// The token values assigned to these symbols is determined by the order in
// which lemon first sees them.  It must be the case that NE/EQ, GT/LE, and
// GE/LT are separated by only a single value.  See the sqlExprIfFalse()
// routine for additional information on this constraint.
//
%left OR.
%left AND.
%right NOT.
%left IS MATCH LIKE_KW BETWEEN IN NE EQ.
%left GT LE LT GE.
%right ESCAPE.
%left BITAND BITOR LSHIFT RSHIFT.
%left PLUS MINUS.
%left STAR SLASH REM.
%left CONCAT.
%left COLLATE.
%right BITNOT.


///////////////////// Begin and end transactions. ////////////////////////////
//

cmd ::= START TRANSACTION.  {sql_transaction_begin(pParse);}
cmd ::= COMMIT.      {sql_transaction_commit(pParse);}
cmd ::= ROLLBACK.    {sql_transaction_rollback(pParse);}

savepoint_opt ::= SAVEPOINT.
savepoint_opt ::= .
cmd ::= SAVEPOINT nm(X). {
  sqlSavepoint(pParse, SAVEPOINT_BEGIN, &X);
}
cmd ::= RELEASE savepoint_opt nm(X). {
  sqlSavepoint(pParse, SAVEPOINT_RELEASE, &X);
}
cmd ::= ROLLBACK TO savepoint_opt nm(X). {
  sqlSavepoint(pParse, SAVEPOINT_ROLLBACK, &X);
}

///////////////////// The CREATE TABLE statement ////////////////////////////
//
cmd ::= create_table create_table_args with_opts create_table_end.
create_table ::= createkw TABLE ifnotexists(E) nm(Y). {
  create_table_def_init(&pParse->create_table_def, &Y, E);
  create_ck_constraint_parse_def_init(&pParse->create_ck_constraint_parse_def);
  create_fk_constraint_parse_def_init(&pParse->create_fk_constraint_parse_def);
  pParse->create_table_def.new_space = sqlStartTable(pParse, &Y);
  pParse->initiateTTrans = true;
}
createkw(A) ::= CREATE(A).  {disableLookaside(pParse);}

%type ifnotexists {int}
ifnotexists(A) ::= .              {A = 0;}
ifnotexists(A) ::= IF NOT EXISTS. {A = 1;}

create_table_args ::= LP columnlist RP.

with_opts ::= WITH engine_opts.
with_opts ::= .

engine_opts ::= ENGINE EQ STRING(A). {
  /* Note that specifying engine clause overwrites default engine. */
  if (A.n > ENGINE_NAME_MAX) {
    diag_set(ClientError, ER_CREATE_SPACE,
             pParse->create_table_def.new_space->def->name,
             "space engine name is too long");
    pParse->is_aborted = true;
    return;
  }
  /* Need to dequote name. */
  char *normalized_name = sql_name_from_token(pParse->db, &A);
  if (normalized_name == NULL)
    return;
  memcpy(pParse->create_table_def.new_space->def->engine_name, normalized_name,
         strlen(normalized_name) + 1);
  sqlDbFree(pParse->db, normalized_name);
}

create_table_end ::= . { sqlEndTable(pParse); }

/*
 * CREATE TABLE AS SELECT is broken. To be re-implemented
 * in gh-3223.
 *
 * create_table_args ::= AS select(S). {
 *   sqlEndTable(pParse);
 *   sql_select_delete(pParse->db, S);
 * }
 */

columnlist ::= columnlist COMMA tcons.
columnlist ::= columnlist COMMA column_def create_column_end.
columnlist ::= column_def create_column_end.

column_def ::= column_name_and_type carglist.

column_name_and_type ::= nm(A) typedef(Y). {
  create_column_def_init(&pParse->create_column_def, NULL, &A, &Y);
  sql_create_column_start(pParse);
}

create_column_end ::= autoinc(I). {
  uint32_t fieldno = pParse->create_column_def.space->def->field_count - 1;
  if (I == 1 && sql_add_autoincrement(pParse, fieldno) != 0)
    return;
  if (pParse->create_table_def.new_space == NULL)
    sql_create_column_end(pParse);
}
columnlist ::= tcons.

// An IDENTIFIER can be a generic identifier, or one of several
// keywords.  Any non-standard keyword can also be an identifier.
//
%token_class id  ID|INDEXED.

// The following directive causes tokens ABORT, AFTER, ASC, etc. to
// fallback to ID if they will not parse as their original value.
// This obviates the need for the "id" nonterminal.
//
// A keyword is checked for being a reserve one in `nm`, before
// processing of this %fallback directive. Reserved keywords included
// here to avoid the situation when a keyword has no usages within
// `parse.y` file (a keyword can have more or less usages depending on
// compiler defines). When a keyword has no usages it is excluded
// from autogenerated file `parse.h` that lead to compile-time error.
//
%fallback ID
  ABORT ACTION ADD AFTER AUTOINCREMENT BEFORE CASCADE
  CONFLICT DEFERRED END ENGINE FAIL
  IGNORE INITIALLY INSTEAD NO MATCH PLAN
  QUERY KEY OFFSET RAISE RELEASE REPLACE RESTRICT
  RENAME CTIME_KW IF ENABLE DISABLE
  .
%wildcard ANY.


// And "ids" is an identifer-or-string.
//
%token_class ids  ID|STRING.

// The name of a column or table can be any of the following:
//
%type nm {Token}
nm(A) ::= id(A). {
  if(A.isReserved) {
    diag_set(ClientError, ER_SQL_KEYWORD_IS_RESERVED, pParse->line_count,
             pParse->line_pos, A.n, A.z, A.n, A.z);
    pParse->is_aborted = true;
  }
}

/**
 * "carglist" is a list of additional constraints and clauses that
 * come after the column name and column type in a <CREATE TABLE>
 * or <ALTER TABLE ADD COLUMN> statement.
 */
carglist ::= carglist ccons.
carglist ::= .
%type cconsname { struct Token }
cconsname(N) ::= CONSTRAINT nm(X). { N = X; }
cconsname(N) ::= . { N = Token_nil; }
ccons ::= DEFAULT term(X).            {sqlAddDefaultValue(pParse,&X);}
ccons ::= DEFAULT LP expr(X) RP.      {sqlAddDefaultValue(pParse,&X);}
ccons ::= DEFAULT PLUS term(X).       {sqlAddDefaultValue(pParse,&X);}
ccons ::= DEFAULT MINUS(A) term(X).      {
  ExprSpan v;
  v.pExpr = sqlPExpr(pParse, TK_UMINUS, X.pExpr, 0);
  v.zStart = A.z;
  v.zEnd = X.zEnd;
  sqlAddDefaultValue(pParse,&v);
}

// In addition to the type name, we also care about the primary key and
// UNIQUE constraints.
//
ccons ::= NULL onconf(R).        {
    sql_column_add_nullable_action(pParse, ON_CONFLICT_ACTION_NONE);
    /* Trigger nullability mismatch error if required. */
    if (R != ON_CONFLICT_ACTION_ABORT)
        sql_column_add_nullable_action(pParse, R);
}
ccons ::= NOT NULL onconf(R).    {sql_column_add_nullable_action(pParse, R);}
ccons ::= cconsname(N) PRIMARY KEY sortorder(Z). {
  create_index_def_init(&pParse->create_index_def, NULL, &N, NULL,
                        SQL_INDEX_TYPE_CONSTRAINT_PK, Z, false);
  sqlAddPrimaryKey(pParse);
}
ccons ::= cconsname(N) UNIQUE. {
  create_index_def_init(&pParse->create_index_def, NULL, &N, NULL,
                        SQL_INDEX_TYPE_CONSTRAINT_UNIQUE, SORT_ORDER_ASC,
                        false);
  sql_create_index(pParse);
}

ccons ::= check_constraint_def .

check_constraint_def ::= cconsname(N) CHECK LP expr(X) RP. {
  create_ck_def_init(&pParse->create_ck_def, NULL, &N, &X);
  sql_create_check_contraint(pParse);
}

ccons ::= cconsname(N) REFERENCES nm(T) eidlist_opt(TA) matcharg(M) refargs(R). {
  create_fk_def_init(&pParse->create_fk_def, NULL, &N, NULL, &T, TA, M, R,
                     false);
  sql_create_foreign_key(pParse);
}
ccons ::= defer_subclause(D).    {fk_constraint_change_defer_mode(pParse, D);}
ccons ::= COLLATE id(C).        {sqlAddCollateType(pParse, &C);}

// The optional AUTOINCREMENT keyword
%type autoinc {int}
autoinc(X) ::= .          {X = 0;}
autoinc(X) ::= AUTOINCR.  {X = 1;}

// The next group of rules parses the arguments to a REFERENCES clause
// that determine if the referential integrity checking is deferred or
// or immediate and which determine what action to take if a ref-integ
// check fails.
//
%type refargs {int}
refargs(A) ::= refact_update(X) . { A = (X << 8); }
refargs(A) ::= refact_delete(X) . { A = X; }
refargs(A) ::= refact_delete(X) refact_update(Y) . { A = (Y << 8) | (X) ; }
refargs(A) ::= refact_update(X) refact_delete(Y) . { A = (X << 8) | (Y) ; }
refargs(A) ::= . { A = 0; }

%type refact_update {int}
refact_update(A) ::= ON UPDATE refact(X). { A = X; }
%type refact_delete {int}
refact_delete(A) ::= ON DELETE refact(X). { A = X; }

%type matcharg {int}
matcharg(A) ::= MATCH SIMPLE.  { A = FKEY_MATCH_SIMPLE; }
matcharg(A) ::= MATCH PARTIAL. { A = FKEY_MATCH_PARTIAL; }
matcharg(A) ::= MATCH FULL.    { A = FKEY_MATCH_FULL; }
matcharg(A) ::= .              { A = FKEY_MATCH_SIMPLE; }

%type refact {int}
refact(A) ::= SET NULL.              { A = FKEY_ACTION_SET_NULL; }
refact(A) ::= SET DEFAULT.           { A = FKEY_ACTION_SET_DEFAULT; }
refact(A) ::= CASCADE.               { A = FKEY_ACTION_CASCADE; }
refact(A) ::= RESTRICT.              { A = FKEY_ACTION_RESTRICT; }
refact(A) ::= NO ACTION.             { A = FKEY_NO_ACTION; }
%type defer_subclause {int}
defer_subclause(A) ::= NOT DEFERRABLE init_deferred_pred_opt.     {A = 0;}
defer_subclause(A) ::= DEFERRABLE init_deferred_pred_opt(X).      {A = X;}
%type init_deferred_pred_opt {int}
init_deferred_pred_opt(A) ::= .                       {A = 0;}
init_deferred_pred_opt(A) ::= INITIALLY DEFERRED.     {A = 1;}
init_deferred_pred_opt(A) ::= INITIALLY IMMEDIATE.    {A = 0;}

tcons ::= cconsname(N) PRIMARY KEY LP col_list_with_autoinc(X) RP. {
  create_index_def_init(&pParse->create_index_def, NULL, &N, X,
                        SQL_INDEX_TYPE_CONSTRAINT_PK, SORT_ORDER_ASC, false);
  sqlAddPrimaryKey(pParse);
}
tcons ::= cconsname(N) UNIQUE LP sortlist(X) RP. {
  create_index_def_init(&pParse->create_index_def, NULL, &N, X,
                        SQL_INDEX_TYPE_CONSTRAINT_UNIQUE, SORT_ORDER_ASC,
                        false);
  sql_create_index(pParse);
}
tcons ::= check_constraint_def .
tcons ::= cconsname(N) FOREIGN KEY LP eidlist(FA) RP
          REFERENCES nm(T) eidlist_opt(TA) matcharg(M) refargs(R) defer_subclause_opt(D). {
  create_fk_def_init(&pParse->create_fk_def, NULL, &N, FA, &T, TA, M, R, D);
  sql_create_foreign_key(pParse);
}
%type defer_subclause_opt {int}
defer_subclause_opt(A) ::= .                    {A = 0;}
defer_subclause_opt(A) ::= defer_subclause(A).

// The following is a non-standard extension that allows us to declare the
// default behavior when there is a constraint conflict.
//
%type onconf {int}
%type index_onconf {int}
%type orconf {int}
%type resolvetype {int}
onconf(A) ::= .                              {A = ON_CONFLICT_ACTION_ABORT;}
onconf(A) ::= ON CONFLICT resolvetype(X).    {A = X;}
orconf(A) ::= .                              {A = ON_CONFLICT_ACTION_DEFAULT;}
orconf(A) ::= OR resolvetype(X).             {A = X;}
resolvetype(A) ::= raisetype(A).
resolvetype(A) ::= IGNORE.                   {A = ON_CONFLICT_ACTION_IGNORE;}
resolvetype(A) ::= REPLACE.                  {A = ON_CONFLICT_ACTION_REPLACE;}

////////////////////////// The DROP TABLE /////////////////////////////////////
//

cmd ::= DROP TABLE ifexists(E) fullname(X) . {
  struct Token t = Token_nil;
  drop_table_def_init(&pParse->drop_table_def, X, &t, E);
  pParse->initiateTTrans = true;
  sql_drop_table(pParse);
}

cmd ::= DROP VIEW ifexists(E) fullname(X) . {
  struct Token t = Token_nil;
  drop_view_def_init(&pParse->drop_view_def, X, &t, E);
  pParse->initiateTTrans = true;
  sql_drop_table(pParse);
}

%type ifexists {int}
ifexists(A) ::= IF EXISTS.   {A = 1;}
ifexists(A) ::= .            {A = 0;}

///////////////////// The CREATE VIEW statement /////////////////////////////
//
cmd ::= createkw(X) VIEW ifnotexists(E) nm(Y) eidlist_opt(C)
          AS select(S). {
  if (!pParse->parse_only) {
    create_view_def_init(&pParse->create_view_def, &Y, &X, C, S, E);
    pParse->initiateTTrans = true;
    sql_create_view(pParse);
  } else {
    sql_store_select(pParse, S);
  }
}

//////////////////////// The SELECT statement /////////////////////////////////
//
cmd ::= select(X).  {
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0, 0};
  if(!pParse->parse_only)
          sqlSelect(pParse, X, &dest);
  else
          sql_expr_extract_select(pParse, X);
  sql_select_delete(pParse->db, X);
}

%type select {Select*}
%destructor select {sql_select_delete(pParse->db, $$);}
%type selectnowith {Select*}
%destructor selectnowith {sql_select_delete(pParse->db, $$);}
%type oneselect {Select*}
%destructor oneselect {sql_select_delete(pParse->db, $$);}

%include {
  /**
   * For a compound SELECT statement, make sure
   * p->pPrior->pNext==p for all elements in the list. And make
   * sure list length does not exceed SQL_LIMIT_COMPOUND_SELECT.
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
        (mxSelect = pParse->db->aLimit[SQL_LIMIT_COMPOUND_SELECT])>0 &&
        cnt>mxSelect
      ){
         diag_set(ClientError, ER_SQL_PARSER_LIMIT, "The number of UNION or "\
                  "EXCEPT or INTERSECT operations", cnt,
                  pParse->db->aLimit[SQL_LIMIT_COMPOUND_SELECT]);
         pParse->is_aborted = true;
      }
    }
  }
}

select(A) ::= with(W) selectnowith(X). {
  Select *p = X;
  if( p ){
    p->pWith = W;
    parserDoubleLinkSelect(pParse, p);
  }else{
    sqlWithDelete(pParse->db, W);
  }
  A = p; /*A-overwrites-W*/
}

selectnowith(A) ::= oneselect(A).

selectnowith(A) ::= selectnowith(A) multiselect_op(Y) oneselect(Z).  {
  Select *pRhs = Z;
  Select *pLhs = A;
  if( pRhs && pRhs->pPrior ){
    SrcList *pFrom;
    Token x;
    x.n = 0;
    parserDoubleLinkSelect(pParse, pRhs);
    pFrom = sqlSrcListAppendFromTerm(pParse,0,0,&x,pRhs,0,0);
    pRhs = sqlSelectNew(pParse,0,pFrom,0,0,0,0,0,0,0);
  }
  if( pRhs ){
    pRhs->op = (u8)Y;
    pRhs->pPrior = pLhs;
    if( ALWAYS(pLhs) ) pLhs->selFlags &= ~SF_MultiValue;
    pRhs->selFlags &= ~SF_MultiValue;
    if( Y!=TK_ALL ) pParse->hasCompound = 1;
  }else{
    sql_select_delete(pParse->db, pLhs);
  }
  A = pRhs;
}
%type multiselect_op {int}
multiselect_op(A) ::= UNION(OP).             {A = @OP; /*A-overwrites-OP*/}
multiselect_op(A) ::= UNION ALL.             {A = TK_ALL;}
multiselect_op(A) ::= EXCEPT|INTERSECT(OP).  {A = @OP; /*A-overwrites-OP*/}

oneselect(A) ::= SELECT(S) distinct(D) selcollist(W) from(X) where_opt(Y)
                 groupby_opt(P) having_opt(Q) orderby_opt(Z) limit_opt(L). {
#ifdef SQL_DEBUG
  Token s = S; /*A-overwrites-S*/
#endif
  if (L.pLimit != NULL)
    sql_expr_check_sort_orders(pParse, Z);
  A = sqlSelectNew(pParse,W,X,Y,P,Q,Z,D,L.pLimit,L.pOffset);
#ifdef SQL_DEBUG
  /* Populate the Select.zSelName[] string that is used to help with
  ** query planner debugging, to differentiate between multiple Select
  ** objects in a complex query.
  **
  ** If the SELECT keyword is immediately followed by a C-style comment
  ** then extract the first few alphanumeric characters from within that
  ** comment to be the zSelName value.  Otherwise, the label is #N where
  ** is an integer that is incremented with each SELECT statement seen.
  */
  if( A!=0 ){
    const char *z = s.z+6;
    int i;
    sql_snprintf(sizeof(A->zSelName), A->zSelName, "#%d",
                     ++pParse->nSelect);
    while( z[0]==' ' ) z++;
    if( z[0]=='/' && z[1]=='*' ){
      z += 2;
      while( z[0]==' ' ) z++;
      for(i=0; sqlIsalnum(z[i]); i++){}
      sql_snprintf(sizeof(A->zSelName), A->zSelName, "%.*s", i, z);
    }
  }
#endif /* SQL_DEBUG */
}
oneselect(A) ::= values(A).

%type values {Select*}
%destructor values {sql_select_delete(pParse->db, $$);}
values(A) ::= VALUES LP nexprlist(X) RP. {
  A = sqlSelectNew(pParse,X,0,0,0,0,0,SF_Values,0,0);
}
values(A) ::= values(A) COMMA LP exprlist(Y) RP. {
  Select *pRight, *pLeft = A;
  pRight = sqlSelectNew(pParse,Y,0,0,0,0,0,SF_Values|SF_MultiValue,0,0);
  if( ALWAYS(pLeft) ) pLeft->selFlags &= ~SF_MultiValue;
  if( pRight ){
    pRight->op = TK_ALL;
    pRight->pPrior = pLeft;
    A = pRight;
  }else{
    A = pLeft;
  }
}

// The "distinct" nonterminal is true (1) if the DISTINCT keyword is
// present and false (0) if it is not.
//
%type distinct {int}
distinct(A) ::= DISTINCT.   {A = SF_Distinct;}
distinct(A) ::= ALL.        {A = SF_All;}
distinct(A) ::= .           {A = 0;}

// selcollist is a list of expressions that are to become the return
// values of the SELECT statement.  The "*" in statements like
// "SELECT * FROM ..." is encoded as a special expression with an
// opcode of TK_ASTERISK.
//
%type selcollist {ExprList*}
%destructor selcollist {sql_expr_list_delete(pParse->db, $$);}
%type sclp {ExprList*}
%destructor sclp {sql_expr_list_delete(pParse->db, $$);}
sclp(A) ::= selcollist(A) COMMA.
sclp(A) ::= .                                {A = 0;}
selcollist(A) ::= sclp(A) expr(X) as(Y).     {
   A = sql_expr_list_append(pParse->db, A, X.pExpr);
   if( Y.n>0 ) sqlExprListSetName(pParse, A, &Y, 1);
   sqlExprListSetSpan(pParse,A,&X);
}
selcollist(A) ::= sclp(A) STAR. {
  struct Expr *p = sql_expr_new_anon(pParse->db, TK_ASTERISK);
  if (p == NULL) {
    pParse->is_aborted = true;
    return;
  }
  A = sql_expr_list_append(pParse->db, A, p);
}
selcollist(A) ::= sclp(A) nm(X) DOT STAR. {
  struct Expr *pLeft = sql_expr_new_dequoted(pParse->db, TK_ID, &X);
  if (pLeft == NULL) {
    pParse->is_aborted = true;
    return;
  }
  Expr *pRight = sqlPExpr(pParse, TK_ASTERISK, 0, 0);
  Expr *pDot = sqlPExpr(pParse, TK_DOT, pLeft, pRight);
  A = sql_expr_list_append(pParse->db,A, pDot);
}

// An option "AS <id>" phrase that can follow one of the expressions that
// define the result set, or one of the tables in the FROM clause.
//
%type as {Token}
as(X) ::= AS nm(Y).    {X = Y;}
as(X) ::= ids(X).
as(X) ::= .            {X.n = 0; X.z = 0;}


%type seltablist {SrcList*}
%destructor seltablist {sqlSrcListDelete(pParse->db, $$);}
%type stl_prefix {SrcList*}
%destructor stl_prefix {sqlSrcListDelete(pParse->db, $$);}
%type from {SrcList*}
%destructor from {sqlSrcListDelete(pParse->db, $$);}

// A complete FROM clause.
//
from(A) ::= .                {A = sqlDbMallocZero(pParse->db, sizeof(*A));}
from(A) ::= FROM seltablist(X). {
  A = X;
  sqlSrcListShiftJoinType(A);
}

// "seltablist" is a "Select Table List" - the content of the FROM clause
// in a SELECT statement.  "stl_prefix" is a prefix of this list.
//
stl_prefix(A) ::= seltablist(A) joinop(Y).    {
   if( ALWAYS(A && A->nSrc>0) ) A->a[A->nSrc-1].fg.jointype = (u8)Y;
}
stl_prefix(A) ::= .                           {A = 0;}
seltablist(A) ::= stl_prefix(A) nm(Y) as(Z) indexed_opt(I)
                  on_opt(N) using_opt(U). {
  A = sqlSrcListAppendFromTerm(pParse,A,&Y,&Z,0,N,U);
  sqlSrcListIndexedBy(pParse, A, &I);
}
seltablist(A) ::= stl_prefix(A) nm(Y) LP exprlist(E) RP as(Z)
                  on_opt(N) using_opt(U). {
  A = sqlSrcListAppendFromTerm(pParse,A,&Y,&Z,0,N,U);
  sqlSrcListFuncArgs(pParse, A, E);
}
seltablist(A) ::= stl_prefix(A) LP select(S) RP
                  as(Z) on_opt(N) using_opt(U). {
  A = sqlSrcListAppendFromTerm(pParse,A,0,&Z,S,N,U);
}
seltablist(A) ::= stl_prefix(A) LP seltablist(F) RP
                  as(Z) on_opt(N) using_opt(U). {
  if( A==0 && Z.n==0 && N==0 && U==0 ){
    A = F;
  }else if( F->nSrc==1 ){
    A = sqlSrcListAppendFromTerm(pParse,A,0,&Z,0,N,U);
    if( A ){
      struct SrcList_item *pNew = &A->a[A->nSrc-1];
      struct SrcList_item *pOld = F->a;
      pNew->zName = pOld->zName;
      pNew->pSelect = pOld->pSelect;
      pOld->zName =  0;
      pOld->pSelect = 0;
    }
    sqlSrcListDelete(pParse->db, F);
  }else{
    Select *pSubquery;
    sqlSrcListShiftJoinType(F);
    pSubquery = sqlSelectNew(pParse,0,F,0,0,0,0,SF_NestedFrom,0,0);
    A = sqlSrcListAppendFromTerm(pParse,A,0,&Z,pSubquery,N,U);
  }
}

%type fullname {SrcList*}
%destructor fullname {sqlSrcListDelete(pParse->db, $$);}
fullname(A) ::= nm(X). {
  /* A-overwrites-X. */
  A = sql_src_list_append(pParse->db,0,&X);
  if (A == NULL) {
    pParse->is_aborted = true;
    return;
  }
}

%type joinop {int}
join_nm(A) ::= id(A).
join_nm(A) ::= JOIN_KW(A).

joinop(X) ::= COMMA|JOIN.              { X = JT_INNER; }
joinop(X) ::= JOIN_KW(A) JOIN.
                  {X = sqlJoinType(pParse,&A,0,0);  /*X-overwrites-A*/}
joinop(X) ::= JOIN_KW(A) join_nm(B) JOIN.
                  {X = sqlJoinType(pParse,&A,&B,0); /*X-overwrites-A*/}
joinop(X) ::= JOIN_KW(A) join_nm(B) join_nm(C) JOIN.
                  {X = sqlJoinType(pParse,&A,&B,&C);/*X-overwrites-A*/}

%type on_opt {Expr*}
%destructor on_opt {sql_expr_delete(pParse->db, $$, false);}
on_opt(N) ::= ON expr(E).   {N = E.pExpr;}
on_opt(N) ::= .             {N = 0;}

// Note that this block abuses the Token type just a little. If there is
// no "INDEXED BY" clause, the returned token is empty (z==0 && n==0). If
// there is an INDEXED BY clause, then the token is populated as per normal,
// with z pointing to the token data and n containing the number of bytes
// in the token.
//
// If there is a "NOT INDEXED" clause, then (z==0 && n==1), which is 
// normally illegal. The sqlSrcListIndexedBy() function
// recognizes and interprets this as a special case.
//
%type indexed_opt {Token}
indexed_opt(A) ::= .                 {A.z=0; A.n=0;}
indexed_opt(A) ::= INDEXED BY nm(X). {A = X;}
indexed_opt(A) ::= NOT INDEXED.      {A.z=0; A.n=1;}

%type using_opt {IdList*}
%destructor using_opt {sqlIdListDelete(pParse->db, $$);}
using_opt(U) ::= USING LP idlist(L) RP.  {U = L;}
using_opt(U) ::= .                        {U = 0;}


%type orderby_opt {ExprList*}
%destructor orderby_opt {sql_expr_list_delete(pParse->db, $$);}

// the sortlist non-terminal stores a list of expression where each
// expression is optionally followed by ASC or DESC to indicate the
// sort order.
//
%type sortlist {ExprList*}
%destructor sortlist {sql_expr_list_delete(pParse->db, $$);}

orderby_opt(A) ::= .                          {A = 0;}
orderby_opt(A) ::= ORDER BY sortlist(X).      {A = X;}
sortlist(A) ::= sortlist(A) COMMA expr(Y) sortorder(Z). {
  A = sql_expr_list_append(pParse->db,A,Y.pExpr);
  sqlExprListSetSortOrder(A,Z);
}
sortlist(A) ::= expr(Y) sortorder(Z). {
  /* A-overwrites-Y. */
  A = sql_expr_list_append(pParse->db,NULL,Y.pExpr);
  sqlExprListSetSortOrder(A,Z);
}

/**
 * Non-terminal rule to store a list of columns within PRIMARY KEY
 * declaration.
 */
%type col_list_with_autoinc {ExprList*}
%destructor col_list_with_autoinc {sql_expr_list_delete(pParse->db, $$);}

col_list_with_autoinc(A) ::= col_list_with_autoinc(A) COMMA expr(Y)
                             autoinc(I). {
  uint32_t fieldno;
  if (I == 1) {
    if (sql_fieldno_by_name(pParse, Y.pExpr, &fieldno) != 0)
      return;
    if (sql_add_autoincrement(pParse, fieldno) != 0)
      return;
  }
  A = sql_expr_list_append(pParse->db, A, Y.pExpr);
}

col_list_with_autoinc(A) ::= expr(Y) autoinc(I). {
  if (I == 1) {
    uint32_t fieldno = 0;
    if (sql_fieldno_by_name(pParse, Y.pExpr, &fieldno) != 0)
      return;
    if (sql_add_autoincrement(pParse, fieldno) != 0)
      return;
  }
  /* A-overwrites-Y. */
  A = sql_expr_list_append(pParse->db, NULL, Y.pExpr);
}

%type enable {bool}
enable(A) ::= ENABLE.           {A = true;}
enable(A) ::= DISABLE.          {A = false;}

%type sortorder {int}

sortorder(A) ::= ASC.           {A = SORT_ORDER_ASC;}
sortorder(A) ::= DESC.          {A = SORT_ORDER_DESC;}
sortorder(A) ::= .              {A = SORT_ORDER_UNDEF;}

%type groupby_opt {ExprList*}
%destructor groupby_opt {sql_expr_list_delete(pParse->db, $$);}
groupby_opt(A) ::= .                      {A = 0;}
groupby_opt(A) ::= GROUP BY nexprlist(X). {A = X;}

%type having_opt {Expr*}
%destructor having_opt {sql_expr_delete(pParse->db, $$, false);}
having_opt(A) ::= .                {A = 0;}
having_opt(A) ::= HAVING expr(X).  {A = X.pExpr;}

%type limit_opt {struct LimitVal}

// The destructor for limit_opt will never fire in the current grammar.
// The limit_opt non-terminal only occurs at the end of a single production
// rule for SELECT statements.  As soon as the rule that create the 
// limit_opt non-terminal reduces, the SELECT statement rule will also
// reduce.  So there is never a limit_opt non-terminal on the stack 
// except as a transient.  So there is never anything to destroy.
//
//%destructor limit_opt {
//  sqlExprDelete(pParse->db, $$.pLimit);
//  sqlExprDelete(pParse->db, $$.pOffset);
//}
limit_opt(A) ::= .                    {A.pLimit = 0; A.pOffset = 0;}
limit_opt(A) ::= LIMIT expr(X).       {A.pLimit = X.pExpr; A.pOffset = 0;}
limit_opt(A) ::= LIMIT expr(X) OFFSET expr(Y). 
                                      {A.pLimit = X.pExpr; A.pOffset = Y.pExpr;}
limit_opt(A) ::= LIMIT expr(X) COMMA expr(Y). 
                                      {A.pOffset = X.pExpr; A.pLimit = Y.pExpr;}

/////////////////////////// The DELETE statement /////////////////////////////
//
cmd ::= with(C) DELETE FROM fullname(X) indexed_opt(I) where_opt(W). {
  sqlWithPush(pParse, C, 1);
  sqlSrcListIndexedBy(pParse, X, &I);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sql_table_delete_from(pParse,X,W);
}

/////////////////////////// The TRUNCATE statement /////////////////////////////
//
cmd ::= TRUNCATE TABLE fullname(X). {
  pParse->initiateTTrans = true;
  sql_table_truncate(pParse, X);
}

%type where_opt {Expr*}
%destructor where_opt {sql_expr_delete(pParse->db, $$, false);}

where_opt(A) ::= .                    {A = 0;}
where_opt(A) ::= WHERE expr(X).       {A = X.pExpr;}

////////////////////////// The UPDATE command ////////////////////////////////
//
cmd ::= with(C) UPDATE orconf(R) fullname(X) indexed_opt(I) SET setlist(Y)
        where_opt(W).  {
  sqlWithPush(pParse, C, 1);
  sqlSrcListIndexedBy(pParse, X, &I);
  if (Y != NULL && Y->nExpr > pParse->db->aLimit[SQL_LIMIT_COLUMN]) {
    diag_set(ClientError, ER_SQL_PARSER_LIMIT, "The number of columns in set "\
             "list", Y->nExpr, pParse->db->aLimit[SQL_LIMIT_COLUMN]);
    pParse->is_aborted = true;
  }
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlUpdate(pParse,X,Y,W,R);
}

%type setlist {ExprList*}
%destructor setlist {sql_expr_list_delete(pParse->db, $$);}

setlist(A) ::= setlist(A) COMMA nm(X) EQ expr(Y). {
  A = sql_expr_list_append(pParse->db, A, Y.pExpr);
  sqlExprListSetName(pParse, A, &X, 1);
}
setlist(A) ::= setlist(A) COMMA LP idlist(X) RP EQ expr(Y). {
  A = sqlExprListAppendVector(pParse, A, X, Y.pExpr);
}
setlist(A) ::= nm(X) EQ expr(Y). {
  A = sql_expr_list_append(pParse->db, NULL, Y.pExpr);
  sqlExprListSetName(pParse, A, &X, 1);
}
setlist(A) ::= LP idlist(X) RP EQ expr(Y). {
  A = sqlExprListAppendVector(pParse, 0, X, Y.pExpr);
}

////////////////////////// The INSERT command /////////////////////////////////
//
cmd ::= with(W) insert_cmd(R) INTO fullname(X) idlist_opt(F) select(S). {
  sqlWithPush(pParse, W, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlInsert(pParse, X, S, F, R);
}
cmd ::= with(W) insert_cmd(R) INTO fullname(X) idlist_opt(F) DEFAULT VALUES.
{
  sqlWithPush(pParse, W, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlInsert(pParse, X, 0, F, R);
}

%type insert_cmd {int}
insert_cmd(A) ::= INSERT orconf(R).   {A = R;}
insert_cmd(A) ::= REPLACE.            {A = ON_CONFLICT_ACTION_REPLACE;}

%type idlist_opt {IdList*}
%destructor idlist_opt {sqlIdListDelete(pParse->db, $$);}
%type idlist {IdList*}
%destructor idlist {sqlIdListDelete(pParse->db, $$);}

idlist_opt(A) ::= .                       {A = 0;}
idlist_opt(A) ::= LP idlist(X) RP.    {A = X;}
idlist(A) ::= idlist(A) COMMA nm(Y). {
  A = sql_id_list_append(pParse->db,A,&Y);
  if (A == NULL) {
    pParse->is_aborted = true;
    return;
  }
}
idlist(A) ::= nm(Y). {
  /* A-overwrites-Y. */
  A = sql_id_list_append(pParse->db,0,&Y);
  if (A == NULL) {
    pParse->is_aborted = true;
    return;
  }
}

/////////////////////////// Expression Processing /////////////////////////////
//

%type expr {ExprSpan}
%destructor expr {sql_expr_delete(pParse->db, $$.pExpr, false);}
%type term {ExprSpan}
%destructor term {sql_expr_delete(pParse->db, $$.pExpr, false);}

%include {
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
    struct Expr *p = NULL;
    int name_sz = t.n + 1;
    p = sqlDbMallocRawNN(pParse->db, sizeof(Expr) + name_sz);
    if( p ){
      memset(p, 0, sizeof(Expr));
      switch (op) {
      case TK_STRING:
        p->type = FIELD_TYPE_STRING;
        break;
      case TK_BLOB:
        p->type = FIELD_TYPE_VARBINARY;
        break;
      case TK_INTEGER:
        p->type = FIELD_TYPE_INTEGER;
        break;
      case TK_FLOAT:
        p->type = FIELD_TYPE_DOUBLE;
        break;
      case TK_TRUE:
      case TK_FALSE:
      case TK_UNKNOWN:
        p->type = FIELD_TYPE_BOOLEAN;
        break;
      case TK_VARIABLE:
        /*
         * For variables we set BOOLEAN type since
         * unassigned bindings will be replaced
         * with NULL automatically, i.e. without
         * explicit call of sql_bind_*().
         */
        p->type = FIELD_TYPE_BOOLEAN;
        break;
      default:
        p->type = FIELD_TYPE_SCALAR;
        break;
      }
      p->op = (u8)op;
      p->flags = EP_Leaf;
      p->iAgg = -1;
      p->u.zToken = (char*)&p[1];
      if (op != TK_VARIABLE) {
        int rc = sql_normalize_name(p->u.zToken, name_sz, t.z, t.n);
        if (rc > name_sz) {
          name_sz = rc;
          p = sqlDbReallocOrFree(pParse->db, p, sizeof(*p) + name_sz);
          if (p == NULL)
            goto tarantool_error;
          p->u.zToken = (char *) &p[1];
          if (sql_normalize_name(p->u.zToken, name_sz, t.z, t.n) > name_sz)
              unreachable();
        }
      } else {
        memcpy(p->u.zToken, t.z, t.n);
        p->u.zToken[t.n] = 0;
      }
#if SQL_MAX_EXPR_DEPTH>0
      p->nHeight = 1;
#endif  
    }
    pOut->pExpr = p;
    pOut->zStart = t.z;
    pOut->zEnd = &t.z[t.n];
    return;
tarantool_error:
    sqlDbFree(pParse->db, p);
    pParse->is_aborted = true;
  }
}

expr(A) ::= term(A).
expr(A) ::= LP(B) expr(X) RP(E).
            {spanSet(&A,&B,&E); /*A-overwrites-B*/  A.pExpr = X.pExpr;}
term(A) ::= NULL(X).        {spanExpr(&A,pParse,@X,X);/*A-overwrites-X*/}
expr(A) ::= id(X).          {spanExpr(&A,pParse,TK_ID,X); /*A-overwrites-X*/}
expr(A) ::= JOIN_KW(X).     {spanExpr(&A,pParse,TK_ID,X); /*A-overwrites-X*/}
expr(A) ::= nm(X) DOT nm(Y). {
  struct Expr *temp1 = sql_expr_new_dequoted(pParse->db, TK_ID, &X);
  if (temp1 == NULL) {
    pParse->is_aborted = true;
    return;
  }
  struct Expr *temp2 = sql_expr_new_dequoted(pParse->db, TK_ID, &Y);
  if (temp2 == NULL) {
    sql_expr_delete(pParse->db, temp1, false);
    pParse->is_aborted = true;
    return;
  }
  spanSet(&A,&X,&Y); /*A-overwrites-X*/
  A.pExpr = sqlPExpr(pParse, TK_DOT, temp1, temp2);
}
term(A) ::= FLOAT|BLOB(X). {spanExpr(&A,pParse,@X,X);/*A-overwrites-X*/}
term(A) ::= STRING(X).     {spanExpr(&A,pParse,@X,X);/*A-overwrites-X*/}
term(A) ::= FALSE(X) . {spanExpr(&A,pParse,@X,X);/*A-overwrites-X*/}
term(A) ::= TRUE(X) . {spanExpr(&A,pParse,@X,X);/*A-overwrites-X*/}
term(A) ::= UNKNOWN(X) . {spanExpr(&A,pParse,@X,X);/*A-overwrites-X*/}

term(A) ::= INTEGER(X). {
  A.pExpr = sql_expr_new_dequoted(pParse->db, TK_INTEGER, &X);
  if (A.pExpr == NULL) {
    pParse->is_aborted = true;
    return;
  }
  A.pExpr->type = FIELD_TYPE_INTEGER;
  A.zStart = X.z;
  A.zEnd = X.z + X.n;
  if( A.pExpr ) A.pExpr->flags |= EP_Leaf;
}
expr(A) ::= VARIABLE(X).     {
  Token t = X;
  if (pParse->parse_only) {
    spanSet(&A, &t, &t);
    diag_set(ClientError, ER_SQL_PARSER_GENERIC_WITH_POS, pParse->line_count,
             pParse->line_pos, "bindings are not allowed in DDL");
    pParse->is_aborted = true;
    A.pExpr = NULL;
  } else if (!(X.z[0]=='#' && sqlIsdigit(X.z[1]))) {
    u32 n = X.n;
    spanExpr(&A, pParse, TK_VARIABLE, X);
    if (A.pExpr->u.zToken[0] == '?' && n > 1) {
      diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN, pParse->line_count, t.n, t.z);
      pParse->is_aborted = true;
    } else {
      sqlExprAssignVarNumber(pParse, A.pExpr, n);
    }
  }else{
    assert( t.n>=2 );
    spanSet(&A, &t, &t);
    diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN, pParse->line_count, t.n, t.z);
    pParse->is_aborted = true;
    A.pExpr = NULL;
  }
}
expr(A) ::= expr(A) COLLATE id(C). {
  A.pExpr = sqlExprAddCollateToken(pParse, A.pExpr, &C, 1);
  A.zEnd = &C.z[C.n];
}

expr(A) ::= CAST(X) LP expr(E) AS typedef(T) RP(Y). {
  spanSet(&A,&X,&Y); /*A-overwrites-X*/
  A.pExpr = sql_expr_new_dequoted(pParse->db, TK_CAST, NULL);
  if (A.pExpr == NULL) {
    pParse->is_aborted = true;
    return;
  }
  A.pExpr->type = T.type;
  sqlExprAttachSubtrees(pParse->db, A.pExpr, E.pExpr, 0);
}

expr(A) ::= TRIM(X) LP trim_operands(Y) RP(E). {
  A.pExpr = sqlExprFunction(pParse, Y, &X);
  spanSet(&A, &X, &E);
}

%type trim_operands {struct ExprList *}
%destructor trim_operands {sql_expr_list_delete(pParse->db, $$);}

trim_operands(A) ::= trim_from_clause(F) expr(Y). {
  A = sql_expr_list_append(pParse->db, F, Y.pExpr);
}

trim_operands(A) ::= expr(Y). {
  A = sql_expr_list_append(pParse->db, NULL, Y.pExpr);
}

%type trim_from_clause {struct ExprList *}
%destructor trim_from_clause {sql_expr_list_delete(pParse->db, $$);}

/*
 * The following two rules cover three cases of keyword
 * (LEADING/TRAILING/BOTH) and <trim_character_set> combination.
 * The case when both of them are absent is disallowed.
 */
trim_from_clause(A) ::= expr(Y) FROM. {
  A = sql_expr_list_append(pParse->db, NULL, Y.pExpr);
}

trim_from_clause(A) ::= trim_specification(N) expr_optional(Y) FROM. {
  struct Expr *p = sql_expr_new_dequoted(pParse->db, TK_INTEGER,
                                         &sqlIntTokens[N]);
  A = sql_expr_list_append(pParse->db, NULL, p);
  if (Y != NULL)
    A = sql_expr_list_append(pParse->db, A, Y);
}

%type expr_optional {struct Expr *}
%destructor expr_optional {sql_expr_delete(pParse->db, $$, false);}

expr_optional(A) ::= .        { A = NULL; }
expr_optional(A) ::= expr(X). { A = X.pExpr; }

%type trim_specification {enum trim_side_mask}

trim_specification(A) ::= LEADING.  { A = TRIM_LEADING; }
trim_specification(A) ::= TRAILING. { A = TRIM_TRAILING; }
trim_specification(A) ::= BOTH.     { A = TRIM_BOTH; }

expr(A) ::= id(X) LP distinct(D) exprlist(Y) RP(E). {
  if( Y && Y->nExpr>pParse->db->aLimit[SQL_LIMIT_FUNCTION_ARG] ){
    const char *err =
      tt_sprintf("Number of arguments to function %.*s", X.n, X.z);
    diag_set(ClientError, ER_SQL_PARSER_LIMIT, err, Y->nExpr,
             pParse->db->aLimit[SQL_LIMIT_FUNCTION_ARG]);
    pParse->is_aborted = true;
  }
  A.pExpr = sqlExprFunction(pParse, Y, &X);
  spanSet(&A,&X,&E);
  if( D==SF_Distinct && A.pExpr ){
    A.pExpr->flags |= EP_Distinct;
  }
}

/*
 * type_func(A) ::= DATE(A) .
 * type_func(A) ::= DATETIME(A) .
 */
type_func(A) ::= CHAR(A) .
expr(A) ::= type_func(X) LP distinct(D) exprlist(Y) RP(E). {
  if( Y && Y->nExpr>pParse->db->aLimit[SQL_LIMIT_FUNCTION_ARG] ){
    const char *err =
      tt_sprintf("Number of arguments to function %.*s", X.n, X.z);
    diag_set(ClientError, ER_SQL_PARSER_LIMIT, err, Y->nExpr,
             pParse->db->aLimit[SQL_LIMIT_FUNCTION_ARG]);
    pParse->is_aborted = true;
  }
  A.pExpr = sqlExprFunction(pParse, Y, &X);
  spanSet(&A,&X,&E);
  if( D==SF_Distinct && A.pExpr ){
    A.pExpr->flags |= EP_Distinct;
  }
}

expr(A) ::= id(X) LP STAR RP(E). {
  A.pExpr = sqlExprFunction(pParse, 0, &X);
  spanSet(&A,&X,&E);
}
/*
 * term(A) ::= CTIME_KW(OP). {
 *   A.pExpr = sqlExprFunction(pParse, 0, &OP);
 *   spanSet(&A, &OP, &OP);
 * }
 */

%include {
  /* This routine constructs a binary expression node out of two ExprSpan
  ** objects and uses the result to populate a new ExprSpan object.
  */
  static void spanBinaryExpr(
    Parse *pParse,      /* The parsing context.  Errors accumulate here */
    int op,             /* The binary operation */
    ExprSpan *pLeft,    /* The left operand, and output */
    ExprSpan *pRight    /* The right operand */
  ){
    pLeft->pExpr = sqlPExpr(pParse, op, pLeft->pExpr, pRight->pExpr);
    pLeft->zEnd = pRight->zEnd;
  }

  /* If doNot is true, then add a TK_NOT Expr-node wrapper around the
  ** outside of *ppExpr.
  */
  static void exprNot(Parse *pParse, int doNot, ExprSpan *pSpan){
    if( doNot ){
      pSpan->pExpr = sqlPExpr(pParse, TK_NOT, pSpan->pExpr, 0);
    }
  }
}

expr(A) ::= LP(L) nexprlist(X) COMMA expr(Y) RP(R). {
  ExprList *pList = sql_expr_list_append(pParse->db, X, Y.pExpr);
  A.pExpr = sqlPExpr(pParse, TK_VECTOR, 0, 0);
  if( A.pExpr ){
    A.pExpr->x.pList = pList;
    spanSet(&A, &L, &R);
  }else{
    sql_expr_list_delete(pParse->db, pList);
  }
}

expr(A) ::= expr(A) AND(OP) expr(Y).    {spanBinaryExpr(pParse,@OP,&A,&Y);}
expr(A) ::= expr(A) OR(OP) expr(Y).     {spanBinaryExpr(pParse,@OP,&A,&Y);}
expr(A) ::= expr(A) LT|GT|GE|LE(OP) expr(Y).
                                        {spanBinaryExpr(pParse,@OP,&A,&Y);}
expr(A) ::= expr(A) EQ|NE(OP) expr(Y).  {spanBinaryExpr(pParse,@OP,&A,&Y);}
expr(A) ::= expr(A) BITAND|BITOR|LSHIFT|RSHIFT(OP) expr(Y).
                                        {spanBinaryExpr(pParse,@OP,&A,&Y);}
expr(A) ::= expr(A) PLUS|MINUS(OP) expr(Y).
                                        {spanBinaryExpr(pParse,@OP,&A,&Y);}
expr(A) ::= expr(A) STAR|SLASH|REM(OP) expr(Y).
                                        {spanBinaryExpr(pParse,@OP,&A,&Y);}
expr(A) ::= expr(A) CONCAT(OP) expr(Y). {spanBinaryExpr(pParse,@OP,&A,&Y);}
%type likeop {Token}
likeop(A) ::= LIKE_KW|MATCH(X).     {A=X;/*A-overwrites-X*/}
likeop(A) ::= NOT LIKE_KW|MATCH(X). {A=X; A.n|=0x80000000; /*A-overwrite-X*/}
expr(A) ::= expr(A) likeop(OP) expr(Y).  [LIKE_KW]  {
  ExprList *pList;
  int bNot = OP.n & 0x80000000;
  OP.n &= 0x7fffffff;
  pList = sql_expr_list_append(pParse->db,NULL, Y.pExpr);
  pList = sql_expr_list_append(pParse->db,pList, A.pExpr);
  A.pExpr = sqlExprFunction(pParse, pList, &OP);
  exprNot(pParse, bNot, &A);
  A.zEnd = Y.zEnd;
  if( A.pExpr ) A.pExpr->flags |= EP_InfixFunc;
}
expr(A) ::= expr(A) likeop(OP) expr(Y) ESCAPE expr(E).  [LIKE_KW]  {
  ExprList *pList;
  int bNot = OP.n & 0x80000000;
  OP.n &= 0x7fffffff;
  pList = sql_expr_list_append(pParse->db,NULL, Y.pExpr);
  pList = sql_expr_list_append(pParse->db,pList, A.pExpr);
  pList = sql_expr_list_append(pParse->db,pList, E.pExpr);
  A.pExpr = sqlExprFunction(pParse, pList, &OP);
  exprNot(pParse, bNot, &A);
  A.zEnd = E.zEnd;
  if( A.pExpr ) A.pExpr->flags |= EP_InfixFunc;
}

%include {
  /* Construct an expression node for a unary postfix operator
  */
  static void spanUnaryPostfix(
    Parse *pParse,         /* Parsing context to record errors */
    int op,                /* The operator */
    ExprSpan *pOperand,    /* The operand, and output */
    Token *pPostOp         /* The operand token for setting the span */
  ){
    pOperand->pExpr = sqlPExpr(pParse, op, pOperand->pExpr, 0);
    pOperand->zEnd = &pPostOp->z[pPostOp->n];
  }                           
}

// Tokens TK_ISNULL and TK_NOTNULL defined in extra tokens and are identifiers
// for operations IS NULL and IS NOT NULL.

expr(A) ::= expr(A) IS NULL(E).   {spanUnaryPostfix(pParse,TK_ISNULL,&A,&E);}
expr(A) ::= expr(A) IS NOT NULL(E).   {spanUnaryPostfix(pParse,TK_NOTNULL,&A,&E);}


%include {
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
    pOut->pExpr = sqlPExpr(pParse, op, pOperand->pExpr, 0);
    pOut->zEnd = pOperand->zEnd;
  }
}



expr(A) ::= NOT(B) expr(X).  
              {spanUnaryPrefix(&A,pParse,@B,&X,&B);/*A-overwrites-B*/}
expr(A) ::= BITNOT(B) expr(X).
              {spanUnaryPrefix(&A,pParse,@B,&X,&B);/*A-overwrites-B*/}
expr(A) ::= MINUS(B) expr(X). [BITNOT]
              {spanUnaryPrefix(&A,pParse,TK_UMINUS,&X,&B);/*A-overwrites-B*/}
expr(A) ::= PLUS(B) expr(X). [BITNOT]
              {spanUnaryPrefix(&A,pParse,TK_UPLUS,&X,&B);/*A-overwrites-B*/}

%type between_op {int}
between_op(A) ::= BETWEEN.     {A = 0;}
between_op(A) ::= NOT BETWEEN. {A = 1;}
expr(A) ::= expr(A) between_op(N) expr(X) AND expr(Y). [BETWEEN] {
  ExprList *pList = sql_expr_list_append(pParse->db,NULL, X.pExpr);
  pList = sql_expr_list_append(pParse->db,pList, Y.pExpr);
  A.pExpr = sqlPExpr(pParse, TK_BETWEEN, A.pExpr, 0);
  if( A.pExpr ){
    A.pExpr->x.pList = pList;
  }else{
    sql_expr_list_delete(pParse->db, pList);
  } 
  exprNot(pParse, N, &A);
  A.zEnd = Y.zEnd;
}
%type in_op {int}
in_op(A) ::= IN.      {A = 0;}
in_op(A) ::= NOT IN.  {A = 1;}
expr(A) ::= expr(A) in_op(N) LP exprlist(Y) RP(E). [IN] {
  if( Y==0 ){
    /* Expressions of the form
    **
    **      expr1 IN ()
    **      expr1 NOT IN ()
    **
    ** simplify to constants 0 (false) and 1 (true), respectively,
    ** regardless of the value of expr1.
    */
    sql_expr_delete(pParse->db, A.pExpr, false);
    int tk = N == 0 ? TK_FALSE : TK_TRUE;
    A.pExpr = sql_expr_new_anon(pParse->db, tk);
    if (A.pExpr == NULL) {
      pParse->is_aborted = true;
      return;
    }
    A.pExpr->type = FIELD_TYPE_BOOLEAN;
  }else if( Y->nExpr==1 ){
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
    */
    Expr *pRHS = Y->a[0].pExpr;
    Y->a[0].pExpr = 0;
    sql_expr_list_delete(pParse->db, Y);
    A.pExpr = sqlPExpr(pParse, N ? TK_NE : TK_EQ, A.pExpr, pRHS);
  }else{
    A.pExpr = sqlPExpr(pParse, TK_IN, A.pExpr, 0);
    if( A.pExpr ){
      A.pExpr->x.pList = Y;
      sqlExprSetHeightAndFlags(pParse, A.pExpr);
    }else{
      sql_expr_list_delete(pParse->db, Y);
    }
    exprNot(pParse, N, &A);
  }
  A.zEnd = &E.z[E.n];
}
expr(A) ::= LP(B) select(X) RP(E). {
  spanSet(&A,&B,&E); /*A-overwrites-B*/
  A.pExpr = sqlPExpr(pParse, TK_SELECT, 0, 0);
  sqlPExprAddSelect(pParse, A.pExpr, X);
}
expr(A) ::= expr(A) in_op(N) LP select(Y) RP(E).  [IN] {
  A.pExpr = sqlPExpr(pParse, TK_IN, A.pExpr, 0);
  sqlPExprAddSelect(pParse, A.pExpr, Y);
  exprNot(pParse, N, &A);
  A.zEnd = &E.z[E.n];
}
expr(A) ::= expr(A) in_op(N) nm(Y) paren_exprlist(E). [IN] {
  struct SrcList *pSrc = sql_src_list_append(pParse->db, 0,&Y);
  if (pSrc == NULL) {
    pParse->is_aborted = true;
    return;
  }
  Select *pSelect = sqlSelectNew(pParse, 0,pSrc,0,0,0,0,0,0,0);
  if( E )  sqlSrcListFuncArgs(pParse, pSelect ? pSrc : 0, E);
  A.pExpr = sqlPExpr(pParse, TK_IN, A.pExpr, 0);
  sqlPExprAddSelect(pParse, A.pExpr, pSelect);
  exprNot(pParse, N, &A);
  A.zEnd = &Y.z[Y.n];
}
expr(A) ::= EXISTS(B) LP select(Y) RP(E). {
  Expr *p;
  spanSet(&A,&B,&E); /*A-overwrites-B*/
  p = A.pExpr = sqlPExpr(pParse, TK_EXISTS, 0, 0);
  sqlPExprAddSelect(pParse, p, Y);
}

/* CASE expressions */
expr(A) ::= CASE(C) expr_optional(X) case_exprlist(Y) case_else(Z) END(E). {
  spanSet(&A,&C,&E);  /*A-overwrites-C*/
  A.pExpr = sqlPExpr(pParse, TK_CASE, X, 0);
  if( A.pExpr ){
    A.pExpr->x.pList = Z ? sql_expr_list_append(pParse->db,Y,Z) : Y;
    sqlExprSetHeightAndFlags(pParse, A.pExpr);
  }else{
    sql_expr_list_delete(pParse->db, Y);
    sql_expr_delete(pParse->db, Z, false);
  }
}
%type case_exprlist {ExprList*}
%destructor case_exprlist {sql_expr_list_delete(pParse->db, $$);}
case_exprlist(A) ::= case_exprlist(A) WHEN expr(Y) THEN expr(Z). {
  A = sql_expr_list_append(pParse->db,A, Y.pExpr);
  A = sql_expr_list_append(pParse->db,A, Z.pExpr);
}
case_exprlist(A) ::= WHEN expr(Y) THEN expr(Z). {
  A = sql_expr_list_append(pParse->db,NULL, Y.pExpr);
  A = sql_expr_list_append(pParse->db,A, Z.pExpr);
}
%type case_else {Expr*}
%destructor case_else {sql_expr_delete(pParse->db, $$, false);}
case_else(A) ::=  ELSE expr(X).         {A = X.pExpr;}
case_else(A) ::=  .                     {A = 0;} 

%type exprlist {ExprList*}
%destructor exprlist {sql_expr_list_delete(pParse->db, $$);}
%type nexprlist {ExprList*}
%destructor nexprlist {sql_expr_list_delete(pParse->db, $$);}

exprlist(A) ::= nexprlist(A).
exprlist(A) ::= .                            {A = 0;}
nexprlist(A) ::= nexprlist(A) COMMA expr(Y).
    {A = sql_expr_list_append(pParse->db,A,Y.pExpr);}
nexprlist(A) ::= expr(Y).
    {A = sql_expr_list_append(pParse->db,NULL,Y.pExpr); /*A-overwrites-Y*/}

/* A paren_exprlist is an optional expression list contained inside
** of parenthesis */
%type paren_exprlist {ExprList*}
%destructor paren_exprlist {sql_expr_list_delete(pParse->db, $$);}
paren_exprlist(A) ::= .   {A = 0;}
paren_exprlist(A) ::= LP exprlist(X) RP.  {A = X;}


///////////////////////////// The CREATE INDEX command ///////////////////////
//
cmd ::= createkw uniqueflag(U) INDEX ifnotexists(NE) nm(X)
        ON nm(Y) LP sortlist(Z) RP. {
  struct SrcList *src_list = sql_src_list_append(pParse->db,0,&Y);
  if (src_list == NULL) {
    pParse->is_aborted = true;
    return;
  }
  create_index_def_init(&pParse->create_index_def, src_list, &X, Z, U,
                        SORT_ORDER_ASC, NE);
  pParse->initiateTTrans = true;
  sql_create_index(pParse);
}

%type uniqueflag {int}
uniqueflag(A) ::= UNIQUE.  {A = SQL_INDEX_TYPE_UNIQUE;}
uniqueflag(A) ::= .        {A = SQL_INDEX_TYPE_NON_UNIQUE;}


// The eidlist non-terminal (Expression Id List) generates an ExprList
// from a list of identifiers.  The identifier names are in ExprList.a[].zName.
// This list is stored in an ExprList rather than an IdList so that it
// can be easily sent to sqlColumnsExprList().
//
// eidlist is grouped with CREATE INDEX because it used to be the non-terminal
// used for the arguments to an index.  That is just an historical accident.
//
%type eidlist {ExprList*}
%destructor eidlist {sql_expr_list_delete(pParse->db, $$);}
%type eidlist_opt {ExprList*}
%destructor eidlist_opt {sql_expr_list_delete(pParse->db, $$);}

%include {
  /* Add a single new term to an ExprList that is used to store a
  ** list of identifiers.  Report an error if the ID list contains
  ** a COLLATE clause or an ASC or DESC keyword, except ignore the
  ** error while parsing a legacy schema.
  */
  static ExprList *parserAddExprIdListTerm(
    Parse *pParse,
    ExprList *pPrior,
    Token *pIdToken
  ){
    ExprList *p = sql_expr_list_append(pParse->db, pPrior, NULL);
    sqlExprListSetName(pParse, p, pIdToken, 1);
    return p;
  }
} // end %include

eidlist_opt(A) ::= .                         {A = 0;}
eidlist_opt(A) ::= LP eidlist(X) RP.         {A = X;}
eidlist(A) ::= eidlist(A) COMMA nm(Y).  {
  A = parserAddExprIdListTerm(pParse, A, &Y);
}
eidlist(A) ::= nm(Y). {
  A = parserAddExprIdListTerm(pParse, 0, &Y); /*A-overwrites-Y*/
}


///////////////////////////// The DROP INDEX command /////////////////////////
//
cmd ::= DROP INDEX ifexists(E) nm(X) ON fullname(Y).   {
  drop_index_def_init(&pParse->drop_index_def, Y, &X, E);
  pParse->initiateTTrans = true;
  sql_drop_index(pParse);
}

///////////////////////////// The SET SESSION command ////////////////////////
//
cmd ::= SET SESSION nm(X) EQ term(Y).  {
    sql_setting_set(pParse,&X,Y.pExpr);
}

///////////////////////////// The PRAGMA command /////////////////////////////
//
cmd ::= PRAGMA nm(X).                        {
    sqlPragma(pParse,&X,0,0);
}
cmd ::= PRAGMA nm(X) LP nm(Y) RP.         {
    sqlPragma(pParse,&X,&Y,0);
}
cmd ::= PRAGMA nm(X) LP nm(Z) DOT nm(Y) RP.  {
    sqlPragma(pParse,&X,&Y,&Z);
}
//////////////////////////// The CREATE TRIGGER command /////////////////////

cmd ::= createkw trigger_decl(A) BEGIN trigger_cmd_list(S) END(Z). {
  Token all;
  all.z = A.z;
  all.n = (int)(Z.z - A.z) + Z.n;
  pParse->initiateTTrans = true;
  sql_trigger_finish(pParse, S, &all);
}

trigger_decl(A) ::= TRIGGER ifnotexists(NOERR) nm(B)
                    trigger_time(C) trigger_event(D)
                    ON fullname(E) foreach_clause when_clause(G). {
  create_trigger_def_init(&pParse->create_trigger_def, E, &B, C, D.a, D.b, G,
                          NOERR);
  sql_trigger_begin(pParse);
  A = B; /*A-overwrites-T*/
}

%type trigger_time {int}
trigger_time(A) ::= BEFORE.      { A = TK_BEFORE; }
trigger_time(A) ::= AFTER.       { A = TK_AFTER;  }
trigger_time(A) ::= INSTEAD OF.  { A = TK_INSTEAD;}
trigger_time(A) ::= .            { A = TK_BEFORE; }

%type trigger_event {struct TrigEvent}
%destructor trigger_event {sqlIdListDelete(pParse->db, $$.b);}
trigger_event(A) ::= DELETE|INSERT(X).   {A.a = @X; /*A-overwrites-X*/ A.b = 0;}
trigger_event(A) ::= UPDATE(X).          {A.a = @X; /*A-overwrites-X*/ A.b = 0;}
trigger_event(A) ::= UPDATE OF idlist(X).{A.a = TK_UPDATE; A.b = X;}

foreach_clause ::= . {
  diag_set(ClientError, ER_SQL_PARSER_GENERIC_WITH_POS, pParse->line_count,
           pParse->line_pos, "FOR EACH STATEMENT triggers are not implemented, "
           "please supply FOR EACH ROW clause");
  pParse->is_aborted = true;
}
foreach_clause ::= FOR EACH ROW.

%type when_clause {Expr*}
%destructor when_clause {sql_expr_delete(pParse->db, $$, false);}
when_clause(A) ::= .             { A = 0; }
when_clause(A) ::= WHEN expr(X). { A = X.pExpr; }

%type trigger_cmd_list {TriggerStep*}
%destructor trigger_cmd_list {sqlDeleteTriggerStep(pParse->db, $$);}
trigger_cmd_list(A) ::= trigger_cmd_list(A) trigger_cmd(X) SEMI. {
  assert( A!=0 );
  A->pLast->pNext = X;
  A->pLast = X;
}
trigger_cmd_list(A) ::= trigger_cmd(A) SEMI. { 
  assert( A!=0 );
  A->pLast = A;
}

// Disallow qualified table names on INSERT, UPDATE, and DELETE statements
// within a trigger.  The table to INSERT, UPDATE, or DELETE is always in 
// the same database as the table that the trigger fires on.
//
%type trnm {Token}
trnm(A) ::= nm(A).
trnm(A) ::= nm DOT nm(X). {
  A = X;
  diag_set(ClientError, ER_SQL_PARSER_GENERIC_WITH_POS, pParse->line_count,
           pParse->line_pos, "qualified table names are not allowed on INSERT, "
           "UPDATE, and DELETE statements within triggers");
  pParse->is_aborted = true;
}

// Disallow the INDEX BY and NOT INDEXED clauses on UPDATE and DELETE
// statements within triggers.  We make a specific error message for this
// since it is an exception to the default grammar rules.
//
tridxby ::= .
tridxby ::= INDEXED BY nm. {
  diag_set(ClientError, ER_SQL_SYNTAX_WITH_POS, pParse->line_count,
           pParse->line_pos, "the INDEXED BY clause is not allowed on UPDATE "\
           "or DELETE statements within triggers");
  pParse->is_aborted = true;
}
tridxby ::= NOT INDEXED. {
  diag_set(ClientError, ER_SQL_SYNTAX_WITH_POS, pParse->line_count,
           pParse->line_pos, "the NOT INDEXED BY clause is not allowed on "\
           "UPDATE or DELETE statements within triggers");
  pParse->is_aborted = true;
}



%type trigger_cmd {TriggerStep*}
%destructor trigger_cmd {sqlDeleteTriggerStep(pParse->db, $$);}
// UPDATE 
trigger_cmd(A) ::=
   UPDATE orconf(R) trnm(X) tridxby SET setlist(Y) where_opt(Z). {
     A = sql_trigger_update_step(pParse->db, &X, Y, Z, R);
     if (A == NULL) {
        pParse->is_aborted = true;
        return;
     }
   }

// INSERT
trigger_cmd(A) ::= insert_cmd(R) INTO trnm(X) idlist_opt(F) select(S). {
  /*A-overwrites-R. */
  A = sql_trigger_insert_step(pParse->db, &X, F, S, R);
  if (A == NULL) {
    pParse->is_aborted = true;
    return;
  }
}

// DELETE
trigger_cmd(A) ::= DELETE FROM trnm(X) tridxby where_opt(Y). {
  A = sql_trigger_delete_step(pParse->db, &X, Y);
  if (A == NULL) {
    pParse->is_aborted = true;
    return;
  }
}

// SELECT
trigger_cmd(A) ::= select(X). {
  /* A-overwrites-X. */
  A = sql_trigger_select_step(pParse->db, X);
  if (A == NULL) {
    pParse->is_aborted = true;
    return;
  }
}

// The special RAISE expression that may occur in trigger programs
expr(A) ::= RAISE(X) LP IGNORE RP(Y).  {
  spanSet(&A,&X,&Y);  /*A-overwrites-X*/
  A.pExpr = sqlPExpr(pParse, TK_RAISE, 0, 0);
  if( A.pExpr ){
    A.pExpr->on_conflict_action = ON_CONFLICT_ACTION_IGNORE;
  }
}
expr(A) ::= RAISE(X) LP raisetype(T) COMMA STRING(Z) RP(Y).  {
  spanSet(&A,&X,&Y);  /*A-overwrites-X*/
  A.pExpr = sql_expr_new_dequoted(pParse->db, TK_RAISE, &Z);
  if(A.pExpr == NULL) {
    pParse->is_aborted = true;
    return;
  }
  A.pExpr->on_conflict_action = (enum on_conflict_action) T;
}

%type raisetype {int}
raisetype(A) ::= ROLLBACK.  {A = ON_CONFLICT_ACTION_ROLLBACK;}
raisetype(A) ::= ABORT.     {A = ON_CONFLICT_ACTION_ABORT;}
raisetype(A) ::= FAIL.      {A = ON_CONFLICT_ACTION_FAIL;}


////////////////////////  DROP TRIGGER statement //////////////////////////////
cmd ::= DROP TRIGGER ifexists(NOERR) fullname(X). {
  struct Token t = Token_nil;
  drop_trigger_def_init(&pParse->drop_trigger_def, X, &t, NOERR);
  pParse->initiateTTrans = true;
  sql_drop_trigger(pParse);
}

//////////////////////// ALTER TABLE table ... ////////////////////////////////
%include {
  struct alter_args {
    struct SrcList *table_name;
    /** Name of constraint OR new name of table in case of RENAME. */
    struct Token name;
  };
}

%type alter_table_start {struct SrcList *}
alter_table_start(A) ::= ALTER TABLE fullname(T) . { A = T; }

%type alter_add_constraint {struct alter_args}
alter_add_constraint(A) ::= alter_table_start(T) ADD CONSTRAINT nm(N). {
   A.table_name = T;
   A.name = N;
   pParse->initiateTTrans = true;
 }

%type alter_add_column {struct alter_args}
alter_add_column(A) ::= alter_table_start(T) ADD column_name(N). {
  A.table_name = T;
  A.name = N;
  pParse->initiateTTrans = true;
}

column_name(N) ::= COLUMN nm(A). { N = A; }
column_name(N) ::= nm(A). { N = A; }

cmd ::= alter_column_def carglist create_column_end.

alter_column_def ::= alter_add_column(N) typedef(Y). {
  create_column_def_init(&pParse->create_column_def, N.table_name, &N.name, &Y);
  create_ck_constraint_parse_def_init(&pParse->create_ck_constraint_parse_def);
  create_fk_constraint_parse_def_init(&pParse->create_fk_constraint_parse_def);
  sql_create_column_start(pParse);
}

cmd ::= alter_add_constraint(N) FOREIGN KEY LP eidlist(FA) RP REFERENCES
        nm(T) eidlist_opt(TA) matcharg(M) refargs(R) defer_subclause_opt(D). {
  create_fk_def_init(&pParse->create_fk_def, N.table_name, &N.name, FA, &T, TA,
                     M, R, D);
  sql_create_foreign_key(pParse);
}

cmd ::= alter_add_constraint(N) CHECK LP expr(X) RP. {
    create_ck_def_init(&pParse->create_ck_def, N.table_name, &N.name, &X);
    sql_create_check_contraint(pParse);
}

cmd ::= alter_add_constraint(N) unique_spec(U) LP sortlist(X) RP. {
  create_index_def_init(&pParse->create_index_def, N.table_name, &N.name, X, U,
                        SORT_ORDER_ASC, false);
  sql_create_index(pParse);
}

%type unique_spec {int}
unique_spec(U) ::= UNIQUE.      { U = SQL_INDEX_TYPE_CONSTRAINT_UNIQUE; }
unique_spec(U) ::= PRIMARY KEY. { U = SQL_INDEX_TYPE_CONSTRAINT_PK; }

cmd ::= alter_table_start(A) RENAME TO nm(N). {
    rename_entity_def_init(&pParse->rename_entity_def, A, &N);
    pParse->initiateTTrans = true;
    sql_alter_table_rename(pParse);
}

cmd ::= ALTER TABLE fullname(X) DROP CONSTRAINT nm(Z). {
  drop_constraint_def_init(&pParse->drop_constraint_def, X, &Z, false);
  pParse->initiateTTrans = true;
  sql_drop_constraint(pParse);
}

cmd ::= alter_table_start(A) enable(E) CHECK CONSTRAINT nm(Z). {
    enable_entity_def_init(&pParse->enable_entity_def, ENTITY_TYPE_CK, A,
                           &Z, E);
    sql_alter_ck_constraint_enable(pParse);
}

//////////////////////// COMMON TABLE EXPRESSIONS ////////////////////////////
%type with {With*}
%type wqlist {With*}
%destructor with {sqlWithDelete(pParse->db, $$);}
%destructor wqlist {sqlWithDelete(pParse->db, $$);}

with(A) ::= . {A = 0;}
with(A) ::= WITH wqlist(W).              { A = W; }
with(A) ::= WITH RECURSIVE wqlist(W).    { A = W; }

wqlist(A) ::= nm(X) eidlist_opt(Y) AS LP select(Z) RP. {
  A = sqlWithAdd(pParse, 0, &X, Y, Z); /*A-overwrites-X*/
}
wqlist(A) ::= wqlist(A) COMMA nm(X) eidlist_opt(Y) AS LP select(Z) RP. {
  A = sqlWithAdd(pParse, A, &X, Y, Z);
}

////////////////////////////// TYPE DECLARATION ///////////////////////////////
%type typedef {struct type_def}
typedef(A) ::= TEXT . { A.type = FIELD_TYPE_STRING; }
typedef(A) ::= STRING_KW . { A.type = FIELD_TYPE_STRING; }
typedef(A) ::= SCALAR . { A.type = FIELD_TYPE_SCALAR; }
/** BOOL | BOOLEAN is not used due to possible bug in Lemon. */
typedef(A) ::= BOOL . { A.type = FIELD_TYPE_BOOLEAN; }
typedef(A) ::= BOOLEAN . { A.type = FIELD_TYPE_BOOLEAN; }
typedef(A) ::= VARBINARY . { A.type = FIELD_TYPE_VARBINARY; }

/**
 * Time-like types are temporary disabled, until they are
 * implemented as a native Tarantool types (gh-3694).
 *
 typedef(A) ::= DATE . { A.type = FIELD_TYPE_NUMBER; }
 typedef(A) ::= TIME . { A.type = FIELD_TYPE_NUMBER; }
 typedef(A) ::= DATETIME . { A.type = FIELD_TYPE_NUMBER; }
*/


char_len(A) ::= LP INTEGER(B) RP . {
  (void) A;
  (void) B;
}

%type char_len {int}
typedef(A) ::= VARCHAR char_len(B) . {
  A.type = FIELD_TYPE_STRING;
  (void) B;
}

%type number_typedef {struct type_def}
typedef(A) ::= number_typedef(A) .
number_typedef(A) ::= NUMBER . { A.type = FIELD_TYPE_NUMBER; }
number_typedef(A) ::= DOUBLE . { A.type = FIELD_TYPE_DOUBLE; }
number_typedef(A) ::= INT|INTEGER_KW . { A.type = FIELD_TYPE_INTEGER; }
number_typedef(A) ::= UNSIGNED . { A.type = FIELD_TYPE_UNSIGNED; }

/**
 * NUMERIC type is temporary disabled. To be enabled when
 * it will be implemented as native Tarantool type.
 *
 * %type number_len_typedef {struct type_def}
 * number_typedef(A) ::= DECIMAL|NUMERIC|NUM number_len_typedef(B) . {
 *   A.type = FIELD_TYPE_NUMBER;
 *   (void) B;
 * }
 *
 *
 * number_len_typedef(A) ::= . { (void) A; }
 * number_len_typedef(A) ::= LP INTEGER(B) RP . {
 *   (void) A;
 *   (void) B;
 * }
 *
 * number_len_typedef(A) ::= LP INTEGER(B) COMMA INTEGER(C) RP . {
 *   (void) A;
 *   (void) B;
 *   (void) C;
 *}
 */
