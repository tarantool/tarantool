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
    const char *token = tt_cstr(TOKEN.z, TOKEN.n);
    diag_set(ClientError, ER_SQL_KEYWORD_IS_RESERVED, pParse->line_count,
             pParse->line_pos, token, token);
  } else {
    diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN, pParse->line_count,
             tt_cstr(TOKEN.z, TOKEN.n));
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

/*
** Disable all error recovery processing in the parser push-down
** automaton.
*/
#define YYNOERRORRECOVERY 1

/*
** Indicate that sqlParserFree() will never be called with a null
** pointer.
*/
#define YYPARSEFREENEVERNULL 1

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
  /** The LIMIT expression. NULL if there is no limit. */
  struct ast_expr *limit;
  /** The OFFSET expression. NULL if there is no offset. */
  struct ast_expr *offset;
};

/*
** An instance of the following structure describes the event of a
** TRIGGER.  "a" is the event type, one of TK_UPDATE, TK_INSERT,
** TK_DELETE, or TK_INSTEAD.  If the event is of the form
**
**      UPDATE ON (a,b,c)
**
** Then the "b" records the list "a,b,c".
*/
struct TrigEvent {
  int a;
  struct ast_id_list *b;
};

/*
** Disable lookaside memory allocation for objects that might be
** shared across database connections.
*/
static void disableLookaside(Parse *pParse){
  pParse->disableLookaside++;
  sql_get()->lookaside.bDisable++;
}

} // end %include

// Input is a single SQL command
input ::= ecmd.
ecmd ::= explain cmdx SEMI.
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
%right LB.


///////////////////// Begin and end transactions. ////////////////////////////
//

cmd ::= START TRANSACTION. {
  sql_ast_init_start_transaction(pParse);
}
cmd ::= COMMIT. {
  sql_ast_init_commit(pParse);
}
cmd ::= ROLLBACK. {
  sql_ast_init_rollback(pParse);
}

savepoint_opt ::= SAVEPOINT.
savepoint_opt ::= .
cmd ::= SAVEPOINT nm(X). {
  sql_ast_init_savepoint(pParse, &X);
}
cmd ::= RELEASE savepoint_opt nm(X). {
  sql_ast_init_release_savepoint(pParse, &X);
}
cmd ::= ROLLBACK TO savepoint_opt nm(X). {
  sql_ast_init_rollback_to_savepoint(pParse, &X);
}

///////////////////// The CREATE TABLE statement ////////////////////////////
//
cmd ::= createkw TABLE ifnotexists(E) create_table create_table_args
        with_opts. {
  vdbe_emit_create_table(pParse, E);
}
create_table ::= nm(Y). {
  create_ck_constraint_parse_def_init(&pParse->create_ck_constraint_parse_def);
  create_fk_constraint_parse_def_init(&pParse->create_fk_constraint_parse_def);
  pParse->new_space = sqlStartTable(pParse, &Y);
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
    diag_set(ClientError, ER_CREATE_SPACE, pParse->new_space->def->name,
             "space engine name is too long");
    pParse->is_aborted = true;
    return;
  }
  /* Need to dequote name. */
  char *normalized_name = sql_name_from_token(&A);
  memcpy(pParse->new_space->def->engine_name, normalized_name,
         strlen(normalized_name) + 1);
  sql_xfree(normalized_name);
}

columnlist ::= columnlist COMMA tcons.
columnlist ::= columnlist COMMA column_def create_column_end.
columnlist ::= column_def create_column_end.

column_def ::= column_name_and_type carglist.

column_name_and_type ::= nm(A) typedef(Y). {
  sql_create_column_start(pParse, NULL, &A, Y);
}

create_column_end ::= autoinc(I). {
  uint32_t fieldno = pParse->space->def->field_count - 1;
  if (I == 1 && sql_add_autoincrement(pParse, fieldno) != 0)
    return;
  if (pParse->new_space == NULL)
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
  RENAME CTIME_KW IF ENABLE DISABLE UUID SHOW
  .
%wildcard WILDCARD.


// And "ids" is an identifer-or-string.
//
%token_class ids  ID|STRING.

// The name of a column or table can be any of the following:
//
%type nm {Token}
nm(A) ::= id(A). {
  if(A.isReserved) {
    const char *token = tt_cstr(A.z, A.n);
    diag_set(ClientError, ER_SQL_KEYWORD_IS_RESERVED, pParse->line_count,
             pParse->line_pos, token, token);
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

/**
 * Rule precedence [COLLATE] forces the parser to reduce this rule rather
 * than shift a follow-up NOT or COLLATE token into the expression: the
 * token NOT come earlier in the precedence table than COLLATE,
 * and COLLATE itself is %left - so both shift-reduce conflicts
 * with `expr NOT ...` / `expr COLLATE ...` resolve as reduce, and the
 * tokens go to the next ccons instead.
 */
ccons ::= DEFAULT expr(X). [COLLATE] {
  sql_column_add_default(pParse, expr_from_ast(pParse, X), X->str, X->len);
}

// In addition to the type name, we also care about the primary key and
// UNIQUE constraints.
//
ccons ::= NULL.        {
    sql_column_add_nullable_action(pParse, ON_CONFLICT_ACTION_NONE);
}
ccons ::= NOT NULL onconf(R).    {sql_column_add_nullable_action(pParse, R);}
ccons ::= cconsname(N) PRIMARY KEY sortorder(Z). {
  sqlAddPrimaryKey(pParse, &N, NULL, Z);
}
ccons ::= cconsname(N) UNIQUE. {
  sql_create_index(pParse, &Token_nil, &N, NULL,
                   SQL_INDEX_TYPE_CONSTRAINT_UNIQUE, SORT_ORDER_ASC, false);
}

ccons ::= cconsname(N) CHECK LP expr_old(X) RP. {
  sql_expr_delete(X.pExpr);
  sql_create_check_constraint(pParse, NULL, &N, X.zStart, X.zEnd - X.zStart,
                              true);
}

ccons ::= cconsname(N) REFERENCES nm(T) eidlist_opt(TA). {
  sql_create_foreign_key(pParse, NULL, &N, NULL, &T, TA);
}
ccons ::= COLLATE id(C).        {sqlAddCollateType(pParse, &C);}

// The optional AUTOINCREMENT keyword
%type autoinc {int}
autoinc(X) ::= .          {X = 0;}
autoinc(X) ::= AUTOINCR.  {X = 1;}

// The next group of rules parses the arguments to a REFERENCES clause.
tcons ::= cconsname(N) PRIMARY KEY LP sortlist_autoinc(X) RP. {
  struct ExprList *columns = expr_list_from_ast(pParse, X);
  if (!pParse->is_aborted)
    sqlAddPrimaryKey(pParse, &N, columns, SORT_ORDER_ASC);
}
tcons ::= cconsname(N) UNIQUE LP sortlist_old(X) RP. {
  sql_create_index(pParse, &Token_nil, &N, X, SQL_INDEX_TYPE_CONSTRAINT_UNIQUE,
                   SORT_ORDER_ASC, false);
}
tcons ::= cconsname(N) CHECK LP expr_old(X) RP. {
  sql_expr_delete(X.pExpr);
  sql_create_check_constraint(pParse, NULL, &N, X.zStart, X.zEnd - X.zStart,
                              false);
}
tcons ::= cconsname(N) FOREIGN KEY LP eidlist(FA) RP
          REFERENCES nm(T) eidlist_opt(TA). {
  sql_create_foreign_key(pParse, NULL, &N, FA, &T, TA);
}

// The following is a non-standard extension that allows us to declare the
// default behavior when there is a constraint conflict.
//
%type onconf {int}
%type index_onconf {int}
%type orconf {int}
%type resolvetype {enum on_conflict_action}
onconf(A) ::= .                              {A = ON_CONFLICT_ACTION_ABORT;}
onconf(A) ::= ON CONFLICT resolvetype(X).    {A = X;}
orconf(A) ::= .                              {A = ON_CONFLICT_ACTION_DEFAULT;}
orconf(A) ::= OR resolvetype(X).             {A = X;}
resolvetype(A) ::= raisetype(A).
resolvetype(A) ::= IGNORE.                   {A = ON_CONFLICT_ACTION_IGNORE;}
resolvetype(A) ::= REPLACE.                  {A = ON_CONFLICT_ACTION_REPLACE;}

////////////////////////// The DROP TABLE /////////////////////////////////////
//

cmd ::= DROP TABLE ifexists(E) nm(X) . {
  pParse->initiateTTrans = true;
  sql_drop_table(pParse, &X, E, false);
}

cmd ::= DROP VIEW ifexists(E) nm(X) . {
  pParse->initiateTTrans = true;
  sql_drop_table(pParse, &X, E, true);
}

%type ifexists {int}
ifexists(A) ::= IF EXISTS.   {A = 1;}
ifexists(A) ::= .            {A = 0;}

///////////////////// The CREATE VIEW statement /////////////////////////////
//
cmd ::= createkw VIEW ifnotexists(E) nm(Y) eidlist_opt(C) AS select_old(S). {
  if (!pParse->parse_only) {
    pParse->initiateTTrans = true;
    sql_create_view(pParse, pParse->zTail, &Y, C, S, E);
  } else {
    sql_expr_list_delete(C);
    pParse->parsed_ast_type = AST_TYPE_SELECT;
    pParse->parsed_ast.select = S;
  }
}

//////////////////////// The SELECT statement /////////////////////////////////
//
cmd ::= select_old(X).  {
  SelectDest dest = {SRT_Output, 0, 0, 0, 0, 0, 0};
  if(pParse->parse_only) {
    diag_set(ClientError, ER_SQL_PARSER_GENERIC,
             "Failed to parse SQL expression");
    pParse->is_aborted = true;
    return;
  }
  sqlSelect(pParse, X, &dest);
  sql_select_delete(X);
}

/**
 * A temporary rule that converts `struct ast_select` values to `struct Select`.
 */
%type select_old {Select*}
%destructor select_old {sql_select_delete($$);}
select_old(A) ::= select(X). {
  A = select_from_ast(pParse, X);
}

%type select {struct ast_select *}
%type selectnowith {struct ast_select *}
%type oneselect {struct ast_select *}

select(A) ::= with(W) selectnowith(X). {
  A = X;
  A->with = W;
}

selectnowith(A) ::= oneselect(A).

selectnowith(A) ::= selectnowith(X) multiselect_op(Y) oneselect(Z).  {
  A = Z;
  if (!rlist_empty(&Z->link)) {
    struct ast_source *new = ast_source_new(pParse);
    new->select = Z;
    A = ast_select_new(pParse);
    A->sources = ast_source_list_append(pParse, NULL, new);
  }
  A->op = Y;
  A->flags |= SF_Compound;
  A->flags &= ~SF_MultiValue;
  X->flags |= SF_Compound;
  X->flags &= ~SF_MultiValue;
  rlist_add(&X->link, &A->link);
}
%type multiselect_op {uint8_t}
multiselect_op(A) ::= UNION(OP).             {A = @OP; /*A-overwrites-OP*/}
multiselect_op(A) ::= UNION ALL.             {A = TK_ALL;}
multiselect_op(A) ::= EXCEPT|INTERSECT(OP).  {A = @OP; /*A-overwrites-OP*/}

oneselect(A) ::= SELECT distinct(D) select_list(W) from(X) where_opt(Y)
                 groupby_opt(P) having_opt(Q) orderby_opt(Z) limit_opt(L). {
  A = ast_select_new(pParse);
  A->columns = W;
  A->sources = X;
  A->where = Y;
  A->group_by = P;
  A->having = Q;
  A->order_by = Z;
  A->flags = D;
  A->limit = L.limit;
  A->offset = L.offset;
}
oneselect(A) ::= values(A).

%type values {struct ast_select *}
values(A) ::= VALUES LP nexprlist(X) RP. {
  A = ast_select_new(pParse);
  A->columns = X;
  A->flags = SF_Values;
}
values(A) ::= values(X) COMMA LP exprlist(Y) RP. {
  X->flags |= SF_Compound;
  X->flags &= ~SF_MultiValue;
  A = ast_select_new(pParse);
  A->columns = Y;
  A->flags = SF_Values|SF_MultiValue|SF_Compound;
  A->op = TK_ALL;
  rlist_add(&X->link, &A->link);
}

// The "distinct" nonterminal is true (1) if the DISTINCT keyword is
// present and false (0) if it is not.
//
%type distinct {int}
distinct(A) ::= DISTINCT.   {A = SF_Distinct;}
distinct(A) ::= ALL.        {A = SF_All;}
distinct(A) ::= .           {A = 0;}

%type select_list {struct ast_expr_list *}
select_list(A) ::= expr(X) as(Y). {
  A = ast_expr_list_append(pParse, NULL, X);
  ast_expr_list_set_name(A, &Y);
  A->is_select_list = true;
}
select_list(A) ::= STAR(X). {
  struct ast_expr *expr = ast_expr_new(pParse, X.z, X.n, TK_ASTERISK);
  A = ast_expr_list_append(pParse, NULL, expr);
  A->is_select_list = true;
}
select_list(A) ::= nm(X) DOT STAR(Y). {
  struct ast_expr *dot = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_DOT);
  dot->left = ast_expr_new(pParse, X.z, X.n, TK_ID);
  dot->right = ast_expr_new(pParse, Y.z, Y.n, TK_ASTERISK);
  A = ast_expr_list_append(pParse, NULL, dot);
  A->is_select_list = true;
}
select_list(A) ::= select_list(A) COMMA expr(X) as(Y). {
  A = ast_expr_list_append(pParse, A, X);
  ast_expr_list_set_name(A, &Y);
}
select_list(A) ::= select_list(A) COMMA STAR(X). {
  struct ast_expr *expr = ast_expr_new(pParse, X.z, X.n, TK_ASTERISK);
  A = ast_expr_list_append(pParse, A, expr);
}
select_list(A) ::= select_list(A) COMMA nm(X) DOT STAR(Y). {
  struct ast_expr *dot = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_DOT);
  dot->left = ast_expr_new(pParse, X.z, X.n, TK_ID);
  dot->right = ast_expr_new(pParse, Y.z, Y.n, TK_ASTERISK);
  A = ast_expr_list_append(pParse, A, dot);
}

// An option "AS <id>" phrase that can follow one of the expressions that
// define the result set, or one of the tables in the FROM clause.
//
%type as {Token}
as(X) ::= AS nm(Y).    {X = Y;}
as(X) ::= ids(X).
as(X) ::= .            {X.n = 0; X.z = 0;}

%type seqscan {int}
seqscan(X) ::= SEQSCAN.     {X = 0;}
seqscan(X) ::= .            {X = 1;}

%type from {struct ast_source_list *}
from(A) ::= . {
  A = NULL;
}
from(A) ::= FROM source_list(X). {
  A = X;
}

%type source_list {struct ast_source_list *}
source_list(A) ::= source(X). {
  A = ast_source_list_append(pParse, NULL, X);
}
source_list(A) ::= LP source_list(F) RP as(Z). {
  if (Z.n == 0) {
    A = F;
  } else if (F->len == 1) {
    struct ast_source *old = stailq_first_entry(&F->head, struct ast_source,
                                                link);
    struct ast_source *new = ast_source_new(pParse);
    new->name = old->name;
    new->alias = Z;
    new->select = old->select;
    A = ast_source_list_append(pParse, NULL, new);
  } else {
    struct ast_select *subquery = ast_select_new(pParse);
    subquery->sources = F;
    subquery->flags = SF_NestedFrom;
    struct ast_source *src = ast_source_new(pParse);
    src->alias = Z;
    src->select = subquery;
    A = ast_source_list_append(pParse, NULL, src);
  }
}
source_list(A) ::= source_list(A) joinop(Y) source(X) on_opt(N) using_opt(U). {
  X->join_type = Y;
  X->join_on = N;
  X->join_using = U;
  A = ast_source_list_append(pParse, A, X);
}
source_list(A) ::= source_list(X) joinop(Y) LP source_list(F) RP as(Z) on_opt(N)
                   using_opt(U). {
  if (F->len == 1) {
    struct ast_source *old = stailq_first_entry(&F->head, struct ast_source,
                                                link);
    struct ast_source *new = ast_source_new(pParse);
    new->name = old->name;
    new->alias = Z;
    new->select = old->select;
    new->join_type = Y;
    new->join_on = N;
    new->join_using = U;
    A = ast_source_list_append(pParse, X, new);
  } else {
    struct ast_select *subquery = ast_select_new(pParse);
    subquery->sources = F;
    subquery->flags = SF_NestedFrom;
    struct ast_source *src = ast_source_new(pParse);
    src->alias = Z;
    src->select = subquery;
    src->join_type = Y;
    src->join_on = N;
    src->join_using = U;
    A = ast_source_list_append(pParse, X, src);
  }
}

%type source {struct ast_source *}
source(A) ::= seqscan(X) nm(Y) as(Z) indexed_opt(I). {
  A = ast_source_new(pParse);
  A->name = Y;
  A->alias = Z;
  A->indexed_by = I;
  A->disallow_scan = X;
}
source(A) ::= seqscan(X) nm(Y) LP exprlist(E) RP as(Z). {
  A = ast_source_new(pParse);
  A->name = Y;
  A->alias = Z;
  A->func_args = E;
  A->is_tab_func = true;
  A->disallow_scan = X;
}
source(A) ::= LP select(S) RP as(Z). {
  A = ast_source_new(pParse);
  A->alias = Z;
  A->select = S;
}

%type fullname {SrcList*}
%destructor fullname {sqlSrcListDelete($$);}
fullname(A) ::= nm(X). {
  /* A-overwrites-X. */
  A = sql_src_list_append(NULL ,&X);
}

%type joinop {int}
joinop(A) ::= COMMA|JOIN. {
  A = JT_INNER;
}
joinop(X) ::= join_nm(A) JOIN. {
  X = A;
}
joinop(X) ::= join_nm(A) join_nm_full(B) JOIN. {
  X = A | B;
}
joinop(X) ::= join_nm(A) join_nm_full(B) join_nm_full(C) JOIN. {
  X = A | B | C;
}

%type join_nm_full {int}
join_nm_full(A) ::= join_nm(A).
join_nm_full(A) ::= FULL. {
  A = JT_LEFT | JT_RIGHT | JT_OUTER;
}

%type join_nm {int}
join_nm(A) ::= CROSS. {
  A = JT_INNER | JT_CROSS;
}
join_nm(A) ::= INNER. {
  A = JT_INNER;
}
join_nm(A) ::= LEFT. {
  A = JT_LEFT | JT_OUTER;
}
join_nm(A) ::= NATURAL. {
  A = JT_NATURAL;
}
join_nm(A) ::= OUTER. {
  A = JT_OUTER;
}
join_nm(A) ::= RIGHT. {
  A = JT_RIGHT | JT_OUTER;
}

%type on_opt {struct ast_expr *}
on_opt(N) ::= ON expr(E). {
  N = E;
}
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

%type using_opt {struct ast_id_list *}
using_opt(U) ::= USING LP idlist(L) RP.  {U = L;}
using_opt(U) ::= .                        {U = 0;}

%type orderby_opt {struct ast_expr_list *}

// the sortlist non-terminal stores a list of expression where each
// expression is optionally followed by ASC or DESC to indicate the
// sort order.
//
%type sortlist_old {ExprList*}
%destructor sortlist_old {sql_expr_list_delete($$);}

orderby_opt(A) ::= .                          {A = 0;}
orderby_opt(A) ::= ORDER BY sortlist(X).      {A = X;}

%type sortlist {struct ast_expr_list *}
sortlist(A) ::= sortlist(A) COMMA expr(Y) sortorder(Z). {
  A = ast_expr_list_append(pParse, A, Y);
  ast_expr_list_set_order(A, Z);
}
sortlist(A) ::= expr(Y) sortorder(Z). {
  A = ast_expr_list_append(pParse, NULL, Y);
  ast_expr_list_set_order(A, Z);
}

%type sortlist_autoinc {struct ast_expr_list *}
sortlist_autoinc(A) ::= sortlist_autoinc(A) COMMA expr(Y) sortorder(Z)
                        autoinc(I). {
  A = ast_expr_list_append(pParse, A, Y);
  ast_expr_list_set_order(A, Z);
  ast_expr_list_set_autoinc(A, I != 0);
}
sortlist_autoinc(A) ::= expr(Y) sortorder(Z) autoinc(I). {
  A = ast_expr_list_append(pParse, NULL, Y);
  ast_expr_list_set_order(A, Z);
  ast_expr_list_set_autoinc(A, I != 0);
}

sortlist_old(A) ::= sortlist_old(A) COMMA expr_old(Y) sortorder(Z). {
  A = sql_expr_list_append(A, Y.pExpr);
  sqlExprListSetSortOrder(A,Z);
}
sortlist_old(A) ::= expr_old(Y) sortorder(Z). {
  /* A-overwrites-Y. */
  A = sql_expr_list_append(NULL, Y.pExpr);
  sqlExprListSetSortOrder(A,Z);
}

%type sortorder {int}

sortorder(A) ::= ASC.           {A = SORT_ORDER_ASC;}
sortorder(A) ::= DESC.          {A = SORT_ORDER_DESC;}
sortorder(A) ::= .              {A = SORT_ORDER_UNDEF;}

%type groupby_opt {struct ast_expr_list *}
groupby_opt(A) ::= .                      {A = 0;}
groupby_opt(A) ::= GROUP BY nexprlist(X). {A = X;}

%type having_opt {struct ast_expr *}
having_opt(A) ::= .                {A = 0;}
having_opt(A) ::= HAVING expr(X). {
  A = X;
}

%type limit_opt {struct LimitVal}

limit_opt(A) ::= . {
  A.limit = NULL;
  A.offset = NULL;
}
limit_opt(A) ::= LIMIT expr(X). {
  A.limit = X;
  A.offset = NULL;
}
limit_opt(A) ::= LIMIT expr(X) OFFSET expr(Y). {
  A.limit = X;
  A.offset = Y;
}
limit_opt(A) ::= LIMIT expr(X) COMMA expr(Y). {
  A.offset = X;
  A.limit = Y;
}

/////////////////////////// The DELETE statement /////////////////////////////
//
cmd ::= with_old(C) DELETE FROM fullname(X) indexed_opt(I) where_opt_old(W). {
  sqlWithPush(pParse, C, 1);
  sqlSrcListIndexedBy(X, &I);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sql_table_delete_from(pParse,X,W);
}

/////////////////////////// The TRUNCATE statement /////////////////////////////
//
cmd ::= TRUNCATE TABLE nm(X). {
  pParse->initiateTTrans = true;
  sql_table_truncate(pParse, &X);
}

%type where_opt_old {Expr*}
%destructor where_opt_old {sql_expr_delete($$);}
where_opt_old(A) ::= where_opt(X). {
  A = expr_from_ast(pParse, X);
}

%type where_opt {struct ast_expr *}
where_opt(A) ::= .                    {A = 0;}
where_opt(A) ::= WHERE expr(X). {
  A = X;
}

////////////////////////// The UPDATE command ////////////////////////////////
//
cmd ::= with_old(C) UPDATE orconf(R) fullname(X) indexed_opt(I) SET setlist(Y)
        where_opt_old(W).  {
  sqlWithPush(pParse, C, 1);
  sqlSrcListIndexedBy(X, &I);
  if (Y != NULL && Y->nExpr > SQL_MAX_COLUMN) {
    diag_set(ClientError, ER_SQL_PARSER_LIMIT, "The number of columns in set "\
             "list", Y->nExpr, SQL_MAX_COLUMN);
    pParse->is_aborted = true;
  }
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlUpdate(pParse,X,Y,W,R);
}

%type setlist {ExprList*}
%destructor setlist {sql_expr_list_delete($$);}

setlist(A) ::= setlist(A) COMMA nm(X) EQ expr_old(Y). {
  A = sql_expr_list_append(A, Y.pExpr);
  sqlExprListSetName(pParse, A, &X, 1);
}
setlist(A) ::= setlist(A) COMMA LP idlist(X) RP EQ expr_old(Y). {
  A = sqlExprListAppendVector(pParse, A, X, Y.pExpr);
}
setlist(A) ::= nm(X) EQ expr_old(Y). {
  A = sql_expr_list_append(NULL, Y.pExpr);
  sqlExprListSetName(pParse, A, &X, 1);
}
setlist(A) ::= LP idlist(X) RP EQ expr_old(Y). {
  A = sqlExprListAppendVector(pParse, 0, X, Y.pExpr);
}

////////////////////////// The INSERT command /////////////////////////////////
//
cmd ::= with_old(W) insert_cmd(R) INTO fullname(X) idlist_opt(F)
        select_old(S). {
  sqlWithPush(pParse, W, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlInsert(pParse, X, S, id_list_from_ast(F), R);
}
cmd ::= with_old(W) insert_cmd(R) INTO fullname(X) idlist_opt(F) DEFAULT VALUES.
{
  sqlWithPush(pParse, W, 1);
  sqlSubProgramsRemaining = SQL_MAX_COMPILING_TRIGGERS;
  /* Instruct SQL to initate Tarantool's transaction.  */
  pParse->initiateTTrans = true;
  sqlInsert(pParse, X, 0, id_list_from_ast(F), R);
}

%type insert_cmd {int}
insert_cmd(A) ::= INSERT orconf(R).   {A = R;}
insert_cmd(A) ::= REPLACE.            {A = ON_CONFLICT_ACTION_REPLACE;}

%type idlist_opt {struct ast_id_list *}
%type idlist {struct ast_id_list *}

idlist_opt(A) ::= .                       {A = 0;}
idlist_opt(A) ::= LP idlist(X) RP.    {A = X;}
idlist(A) ::= idlist(A) COMMA nm(Y). {
  A = ast_id_list_append(pParse, A, &Y);
}
idlist(A) ::= nm(Y). {
  A = ast_id_list_append(pParse, NULL, &Y);
}

/////////////////////////// Expression Processing /////////////////////////////
//

%type expr_old {ExprSpan}
%destructor expr_old {sql_expr_delete($$.pExpr);}
expr_old(A) ::= expr(X). {
  struct Expr *e = expr_from_ast(pParse, X);
  A.pExpr = e;
  A.zStart = X->str;
  A.zEnd = &X->str[X->len];
}

%type expr {struct ast_expr *}
%type term {struct ast_expr *}
expr(A) ::= term(A).
term(A) ::= NULL|BLOB|STRING|FALSE|TRUE|UNKNOWN|FLOAT|DECIMAL|INTEGER(X). {
  A = ast_expr_new(pParse, X.z, X.n, @X);
}
expr(A) ::= LP(B) expr(X) RP(E). {
  A = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_PARENTHESES);
  A->left = X;
}
expr(A) ::= id(X). {
  A = ast_expr_new(pParse, X.z, X.n, TK_ID);
}
expr(A) ::= CROSS|INNER|LEFT|NATURAL|OUTER|RIGHT(X). {
  A = ast_expr_new(pParse, X.z, X.n, TK_ID);
}
expr(A) ::= nm(X) DOT nm(Y). {
  A = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_DOT);
  A->left = ast_expr_new(pParse, X.z, X.n, TK_ID);
  A->right = ast_expr_new(pParse, Y.z, Y.n, TK_ID);
}
expr(A) ::= VARNUM(X). {
  A = ast_expr_new(pParse, X.z, X.n, TK_VARIABLE);
}
expr(A) ::= COLON|VARIABLE(X) id(Y).     {
  A = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_VARIABLE);
  A->left = ast_expr_new(pParse, Y.z, Y.n, TK_STRING);
}
expr(A) ::= COLON|VARIABLE(X) INTEGER(Y).     {
  A = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_VARIABLE);
  A->left = ast_expr_new(pParse, Y.z, Y.n, TK_INTEGER);
}
expr(A) ::= expr(X) COLLATE id(C). {
  A = ast_expr_new(pParse, X->str, (C.z - X->str) + C.n, TK_COLLATE);
  A->left = X;
  A->right = ast_expr_new(pParse, C.z, C.n, TK_ID);
}
expr(A) ::= CAST(X) LP expr(E) AS typedef(T) RP(Y). {
  A = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_CAST);
  A->type = T;
  A->left = E;
}
expr(A) ::= expr(X) LB getlist(Y) RB(E). {
  A = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_GETITEM);
  A->left = X;
  A->list = Y;
}

%type getlist {struct ast_expr_list *}
getlist(A) ::= getlist(A) RB LB expr(X). {
  A = ast_expr_list_append(pParse, A, X);
}
getlist(A) ::= expr(X). {
  A = ast_expr_list_append(pParse, NULL, X);
}

expr(A) ::= LB(X) exprlist(Y) RB(E). {
  A = ast_expr_new(pParse, X.z, (E.z - X.z) + E.n, TK_ARRAY);
  A->list = Y;
}
expr(A) ::= LCB(X) maplist(Y) RCB(E). {
  A = ast_expr_new(pParse, X.z, (E.z - X.z) + E.n, TK_MAP);
  A->list = Y;
}

%type maplist {struct ast_expr_list *}
%type nmaplist {struct ast_expr_list *}
maplist(A) ::= nmaplist(A).
maplist(A) ::= . {
  A = NULL;
}
nmaplist(A) ::= nmaplist(A) COMMA expr(X) COLON expr(Y). {
  A = ast_expr_list_append(pParse, A, X);
  A = ast_expr_list_append(pParse, A, Y);
}
nmaplist(A) ::= expr(X) COLON expr(Y). {
  A = ast_expr_list_append(pParse, NULL, X);
  A = ast_expr_list_append(pParse, A, Y);
}

expr(A) ::= TRIM(X) LP(B) trim_operands(Y) RP(E). {
  A = ast_expr_new(pParse, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(pParse, X.z, X.n, TK_STRING);
  A->right = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_VECTOR);
  A->right->list = Y;
}

%type trim_operands {struct ast_expr_list *}
trim_operands(A) ::= LEADING|TRAILING|BOTH(N) expr(Z) FROM expr(Y). {
  A = ast_expr_list_append(pParse, NULL, Y);
  A = ast_expr_list_append(pParse, A, ast_expr_new(pParse, N.z, N.n, @N));
  A = ast_expr_list_append(pParse, A, Z);
}
trim_operands(A) ::= LEADING|TRAILING|BOTH(N) FROM expr(Y). {
  A = ast_expr_list_append(pParse, NULL, Y);
  A = ast_expr_list_append(pParse, A, ast_expr_new(pParse, N.z, N.n, @N));
}
trim_operands(A) ::= expr(Z) FROM expr(Y). {
  A = ast_expr_list_append(pParse, NULL, Y);
  A = ast_expr_list_append(pParse, A, Z);
}
trim_operands(A) ::= expr(Y). {
  A = ast_expr_list_append(pParse, NULL, Y);
}

expr(A) ::= id(X) LP(B) distinct(D) exprlist(Y) RP(E). {
  A = ast_expr_new(pParse, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(pParse, X.z, X.n, TK_STRING);
  uint8_t op = D == SF_Distinct ? TK_DISTINCT : TK_VECTOR;
  A->right = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, op);
  A->right->list = Y;
}
expr(A) ::= CHAR(X) LP(B) distinct(D) exprlist(Y) RP(E). {
  A = ast_expr_new(pParse, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(pParse, X.z, X.n, TK_STRING);
  uint8_t op = D == SF_Distinct ? TK_DISTINCT : TK_VECTOR;
  A->right = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, op);
  A->right->list = Y;
}
expr(A) ::= id(X) LP STAR RP(E). {
  A = ast_expr_new(pParse, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(pParse, X.z, X.n, TK_STRING);
}
expr(A) ::= LP(L) nexprlist(X) COMMA expr(Y) RP(R). {
  A = ast_expr_new(pParse, L.z, (R.z - L.z) + R.n, TK_VECTOR);
  A->list = ast_expr_list_append(pParse, X, Y);
}
expr(A) ::= expr(X) AND(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) OR(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) LT|GT|GE|LE(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) EQ|NE(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) BITAND|BITOR|LSHIFT|RSHIFT(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) PLUS|MINUS(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) STAR|SLASH|REM(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) CONCAT(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) LIKE_KW|MATCH(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, TK_FUNCTION);
  A->left = ast_expr_new(pParse, OP.z, OP.n, TK_STRING);
  A->right = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len,
                          TK_VECTOR);
  A->right->list = ast_expr_list_append(pParse, NULL, Y);
  A->right->list = ast_expr_list_append(pParse, A->right->list, X);
}
expr(A) ::= expr(X) NOT LIKE_KW|MATCH(OP) expr(Y). {
  A = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len, TK_NOT);
  A->left = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len,
                         TK_FUNCTION);
  A->left->left = ast_expr_new(pParse, OP.z, OP.n, TK_STRING);
  A->left->right = ast_expr_new(pParse, X->str, (Y->str - X->str) + Y->len,
                                TK_VECTOR);
  A->left->right->list = ast_expr_list_append(pParse, NULL, Y);
  A->left->right->list = ast_expr_list_append(pParse, A->left->right->list, X);
}
expr(A) ::= expr(X) LIKE_KW|MATCH(OP) expr(Y) ESCAPE expr(E). {
  A = ast_expr_new(pParse, X->str, (E->str - X->str) + E->len, TK_FUNCTION);
  A->left = ast_expr_new(pParse, OP.z, OP.n, TK_STRING);
  A->right = ast_expr_new(pParse, X->str, (E->str - X->str) + E->len,
                          TK_VECTOR);
  A->right->list = ast_expr_list_append(pParse, NULL, Y);
  A->right->list = ast_expr_list_append(pParse, A->right->list, X);
  A->right->list = ast_expr_list_append(pParse, A->right->list, E);
}
expr(A) ::= expr(X) NOT LIKE_KW|MATCH(OP) expr(Y) ESCAPE expr(E). {
  A = ast_expr_new(pParse, X->str, (E->str - X->str) + E->len, TK_NOT);
  A->left = ast_expr_new(pParse, X->str, (E->str - X->str) + E->len,
                         TK_FUNCTION);
  A->left->left = ast_expr_new(pParse, OP.z, OP.n, TK_STRING);
  A->left->right = ast_expr_new(pParse, X->str, (E->str - X->str) + E->len,
                                TK_VECTOR);
  A->left->right->list = ast_expr_list_append(pParse, NULL, Y);
  A->left->right->list = ast_expr_list_append(pParse, A->left->right->list, X);
  A->left->right->list = ast_expr_list_append(pParse, A->left->right->list, E);
}
expr(A) ::= expr(X) IS NULL(E). {
  A = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_ISNULL);
  A->left = X;
}
expr(A) ::= expr(X) IS NOT NULL(E). {
  A = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_NOTNULL);
  A->left = X;
}
expr(A) ::= NOT(B) expr(X). {
  A = ast_expr_new(pParse, B.z, (X->str - B.z) + X->len, @B);
  A->left = X;
}
expr(A) ::= BITNOT(B) expr(X). {
  A = ast_expr_new(pParse, B.z, (X->str - B.z) + X->len, @B);
  A->left = X;
}
expr(A) ::= MINUS(B) expr(X). [BITNOT] {
  A = ast_expr_new(pParse, B.z, (X->str - B.z) + X->len, TK_UMINUS);
  A->left = X;
}
expr(A) ::= PLUS(B) expr(X). [BITNOT] {
  A = ast_expr_new(pParse, B.z, (X->str - B.z) + X->len, TK_UPLUS);
  A->left = X;
}
expr(A) ::= expr(Z) BETWEEN(N) expr(X) AND expr(Y). {
  A = ast_expr_new(pParse, Z->str, (Y->str - Z->str) + Y->len, @N);
  A->left = Z;
  A->list = ast_expr_list_append(pParse, NULL, X);
  A->list = ast_expr_list_append(pParse, A->list, Y);
}
expr(A) ::= expr(Z) NOT BETWEEN(N) expr(X) AND expr(Y). {
  A = ast_expr_new(pParse, Z->str, (Y->str - Z->str) + Y->len, TK_NOT);
  A->left = ast_expr_new(pParse, Z->str, (Y->str - Z->str) + Y->len, @N);
  A->left->left = Z;
  A->left->list = ast_expr_list_append(pParse, NULL, X);
  A->left->list = ast_expr_list_append(pParse, A->left->list, Y);
}
expr(A) ::= expr(X) IN LP(B) exprlist(Y) RP(E). {
  A = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_VECTOR);
  A->right->list = Y;
}
expr(A) ::= expr(X) NOT IN LP(B) exprlist(Y) RP(E). {
  A = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_NOT);
  A->left = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_VECTOR);
  A->left->right->list = Y;
}
expr(A) ::= expr(X) IN LP(B) select(Y) RP(E). {
  A = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_SELECT);
  A->right->select = Y;
}
expr(A) ::= expr(X) NOT IN LP(B) select(Y) RP(E). {
  A = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_NOT);
  A->left = ast_expr_new(pParse, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_SELECT);
  A->left->right->select = Y;
}
expr(A) ::= expr(X) IN nm(Y). {
  struct ast_source *src = ast_source_new(pParse);
  src->name = Y;
  struct ast_select *select = ast_select_new(pParse);
  select->sources = ast_source_list_append(pParse, NULL, src);
  A = ast_expr_new(pParse, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(pParse, Y.z, Y.n, TK_SELECT);
  A->right->select = select;
}
expr(A) ::= expr(X) IN nm(Y) LP exprlist(E) RP. {
  struct ast_source *src = ast_source_new(pParse);
  src->name = Y;
  if (E != NULL) {
    src->func_args = E;
    src->is_tab_func = true;
  }
  struct ast_select *select = ast_select_new(pParse);
  select->sources = ast_source_list_append(pParse, NULL, src);
  A = ast_expr_new(pParse, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(pParse, Y.z, Y.n, TK_SELECT);
  A->right->select = select;
}
expr(A) ::= expr(X) NOT IN nm(Y). {
  struct ast_source *src = ast_source_new(pParse);
  src->name = Y;
  struct ast_select *select = ast_select_new(pParse);
  select->sources = ast_source_list_append(pParse, NULL, src);
  A = ast_expr_new(pParse, X->str, (Y.z - X->str) + Y.n, TK_NOT);
  A->left = ast_expr_new(pParse, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(pParse, Y.z, Y.n, TK_SELECT);
  A->left->right->select = select;
}
expr(A) ::= expr(X) NOT IN nm(Y) LP exprlist(E) RP. {
  struct ast_source *src = ast_source_new(pParse);
  src->name = Y;
  if (E != NULL) {
    src->func_args = E;
    src->is_tab_func = true;
  }
  struct ast_select *select = ast_select_new(pParse);
  select->sources = ast_source_list_append(pParse, NULL, src);
  A = ast_expr_new(pParse, X->str, (Y.z - X->str) + Y.n, TK_NOT);
  A->left = ast_expr_new(pParse, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(pParse, Y.z, Y.n, TK_SELECT);
  A->left->right->select = select;
}
expr(A) ::= LP(B) select(X) RP(E). {
  A = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_SELECT);
  A->select = X;
}
expr(A) ::= EXISTS(B) LP select(Y) RP(E). {
  A = ast_expr_new(pParse, B.z, (E.z - B.z) + E.n, TK_EXISTS);
  A->select = Y;
}
expr(A) ::= CASE(C) case_exprlist(Y) END(E). {
  A = ast_expr_new(pParse, C.z, (E.z - C.z) + E.n, TK_CASE);
  A->list = Y;
}
expr(A) ::= CASE(C) expr(X) case_exprlist(Y) END(E). {
  A = ast_expr_new(pParse, C.z, (E.z - C.z) + E.n, TK_CASE);
  A->left = X;
  A->list = Y;
}

%type case_exprlist_when {struct ast_expr_list *}
case_exprlist_when(A) ::= case_exprlist_when(A) WHEN expr(Y) THEN expr(Z). {
  A = ast_expr_list_append(pParse, A, Y);
  A = ast_expr_list_append(pParse, A, Z);
}
case_exprlist_when(A) ::= WHEN expr(Y) THEN expr(Z). {
  A = ast_expr_list_append(pParse, NULL, Y);
  A = ast_expr_list_append(pParse, A, Z);
}

%type case_exprlist {struct ast_expr_list *}
case_exprlist(A) ::= case_exprlist_when(A).
case_exprlist(A) ::= case_exprlist_when(A) ELSE expr(X). {
  A = ast_expr_list_append(pParse, A, X);
}

%type exprlist {struct ast_expr_list *}
%type nexprlist {struct ast_expr_list *}

exprlist(A) ::= nexprlist(A).
exprlist(A) ::= . {
  A = NULL;
}
nexprlist(A) ::= nexprlist(A) COMMA expr(Y). {
  A = ast_expr_list_append(pParse, A, Y);
}
nexprlist(A) ::= expr(Y). {
  A = ast_expr_list_append(pParse, NULL, Y);
}

///////////////////////////// The CREATE INDEX command ///////////////////////
//
cmd ::= createkw uniqueflag(U) INDEX ifnotexists(NE) nm(X)
        ON nm(Y) LP sortlist_old(Z) RP. {
  pParse->initiateTTrans = true;
  sql_create_index(pParse, &Y, &X, Z, U, SORT_ORDER_ASC, NE);
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
%destructor eidlist {sql_expr_list_delete($$);}
%type eidlist_opt {ExprList*}
%destructor eidlist_opt {sql_expr_list_delete($$);}

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
    ExprList *p = sql_expr_list_append(pPrior, NULL);
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
cmd ::= DROP INDEX ifexists(E) nm(X) ON nm(Y). {
  pParse->initiateTTrans = true;
  sql_drop_index(pParse, &X, &Y, E);
}

///////////////////////////// The SET SESSION command ////////////////////////
//
cmd ::= SET SESSION nm(X) EQ term(Y).  {
    struct Expr *e = expr_from_ast(pParse, Y);
    sql_setting_set(pParse, &X, e);
}

///////////////////////////// The PRAGMA command /////////////////////////////
//
cmd ::= PRAGMA nm(X).                        {
    sqlPragma(pParse,&X,0,0);
}
cmd ::= PRAGMA nm(X) LP nm(Y) RP.         {
    sqlPragma(pParse,&X,&Y,0);
}
cmd ::= PRAGMA nm(X) LP nm(Y) DOT nm(Z) RP.  {
    sqlPragma(pParse,&X,&Y,&Z);
}
cmd ::= FUNCTION_KW(T) expr_old(E). {
  if (!pParse->is_expr) {
    diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN, pParse->line_count,
             tt_cstr(T.z, T.n));
    pParse->is_aborted = true;
    return;
  }
  pParse->parsed_ast_type = AST_TYPE_EXPR;
  pParse->parsed_ast.expr = E.pExpr;
}

//////////////////////////// The SHOW CREATE TABLE command /////////////////////
cmd ::= SHOW CREATE TABLE nm(X). {
  sql_emit_show_create_table_one(pParse, &X);
}
cmd ::= SHOW CREATE TABLE. {
  sql_emit_show_create_table_all(pParse);
}

//////////////////////////// The CREATE TRIGGER command /////////////////////

cmd ::= createkw TRIGGER ifnotexists(E) nm(N) trigger_time(C) trigger_event(D)
        ON nm(T) foreach_clause when_clause(G) BEGIN trigger_cmd_list(S) END. {
  if (pParse->parse_only) {
    pParse->parsed_ast_type = AST_TYPE_TRIGGER;
    pParse->parsed_ast.trigger = sql_trigger_new(pParse, &N, &T, C, D.a,
                                                 id_list_from_ast(D.b), G, S);
  } else {
    sql_expr_delete(G);
    sqlDeleteTriggerStep(S);
    pParse->initiateTTrans = true;
    vdbe_emit_create_trigger(pParse, pParse->zTail, &N, &T, E);
  }
}

%type trigger_time {int}
trigger_time(A) ::= BEFORE.      { A = TK_BEFORE; }
trigger_time(A) ::= AFTER.       { A = TK_AFTER;  }
trigger_time(A) ::= INSTEAD OF.  { A = TK_INSTEAD;}
trigger_time(A) ::= .            { A = TK_BEFORE; }

%type trigger_event {struct TrigEvent}
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
%destructor when_clause {sql_expr_delete($$);}
when_clause(A) ::= .             { A = 0; }
when_clause(A) ::= WHEN expr_old(X). {
  A = X.pExpr;
}

%type trigger_cmd_list {TriggerStep*}
%destructor trigger_cmd_list {sqlDeleteTriggerStep($$);}
trigger_cmd_list(A) ::= trigger_cmd_list(A) trigger_cmd(X) SEMI. {
  assert( A!=0 );
  A->pLast->pNext = X;
  A->pLast = X;
}
trigger_cmd_list(A) ::= trigger_cmd(A) SEMI. { 
  assert( A!=0 );
  A->pLast = A;
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
%destructor trigger_cmd {sqlDeleteTriggerStep($$);}
// UPDATE 
trigger_cmd(A) ::=
   UPDATE orconf(R) nm(X) tridxby SET setlist(Y) where_opt_old(Z). {
     A = sql_trigger_update_step(&X, Y, Z, R);
     if (A == NULL) {
        pParse->is_aborted = true;
        return;
     }
   }

// INSERT
trigger_cmd(A) ::= insert_cmd(R) INTO nm(X) idlist_opt(F) select_old(S). {
  /*A-overwrites-R. */
  A = sql_trigger_insert_step(&X, F, S, R);
}
trigger_cmd(A) ::= insert_cmd(R) INTO nm(X) idlist_opt(F) DEFAULT VALUES. {
  /*A-overwrites-R. */
  A = sql_trigger_insert_step(&X, F, NULL, R);
}

// DELETE
trigger_cmd(A) ::= DELETE FROM nm(X) tridxby where_opt_old(Y). {
  A = sql_trigger_delete_step(&X, Y);
}

// SELECT
trigger_cmd(A) ::= select_old(X). {
  /* A-overwrites-X. */
  A = sql_trigger_select_step(X);
}

// The special RAISE expression that may occur in trigger programs
expr(A) ::= RAISE(X) LP IGNORE RP(Y).  {
  A = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_RAISE);
  A->on_conflict_action = ON_CONFLICT_ACTION_IGNORE;
}
expr(A) ::= RAISE(X) LP raisetype(T) COMMA STRING(Z) RP(Y).  {
  A = ast_expr_new(pParse, X.z, (Y.z - X.z) + Y.n, TK_RAISE);
  A->left = ast_expr_new(pParse, Z.z, Z.n, @Z);
  A->on_conflict_action = T;
}

%type raisetype {enum on_conflict_action}
raisetype(A) ::= ROLLBACK.  {A = ON_CONFLICT_ACTION_ROLLBACK;}
raisetype(A) ::= ABORT.     {A = ON_CONFLICT_ACTION_ABORT;}
raisetype(A) ::= FAIL.      {A = ON_CONFLICT_ACTION_FAIL;}


////////////////////////  DROP TRIGGER statement //////////////////////////////
cmd ::= DROP TRIGGER ifexists(E) nm(X). {
  pParse->initiateTTrans = true;
  sql_drop_trigger(pParse, &X, E);
}

//////////////////////// ALTER TABLE table ... ////////////////////////////////
column_name(N) ::= COLUMN nm(A). { N = A; }
column_name(N) ::= nm(A). { N = A; }

cmd ::= alter_column_def carglist create_column_end.

alter_column_def ::= ALTER TABLE nm(T) ADD column_name(N) typedef(Y). {
  pParse->initiateTTrans = true;
  create_ck_constraint_parse_def_init(&pParse->create_ck_constraint_parse_def);
  create_fk_constraint_parse_def_init(&pParse->create_fk_constraint_parse_def);
  sql_create_column_start(pParse, &T, &N, Y);
}

cmd ::= ALTER TABLE nm(X) ADD CONSTRAINT nm(N) FOREIGN KEY
        LP eidlist(FA) RP REFERENCES nm(T) eidlist_opt(TA). {
  pParse->initiateTTrans = true;
  sql_create_foreign_key(pParse, &X, &N, FA, &T, TA);
}

cmd ::= ALTER TABLE nm(T) ADD CONSTRAINT nm(N) CHECK LP expr_old(X) RP. {
  sql_expr_delete(X.pExpr);
  pParse->initiateTTrans = true;
  sql_create_check_constraint(pParse, &T, &N, X.zStart, X.zEnd - X.zStart,
                              false);
}

cmd ::= ALTER TABLE nm(T) ADD CONSTRAINT nm(N) UNIQUE LP sortlist_old(X) RP. {
  pParse->initiateTTrans = true;
  sql_create_index(pParse, &T, &N, X, SQL_INDEX_TYPE_CONSTRAINT_UNIQUE,
                   SORT_ORDER_ASC, false);
}

cmd ::= ALTER TABLE nm(T) ADD CONSTRAINT nm(N) PRIMARY KEY
        LP sortlist_autoinc(X) RP. {
  pParse->initiateTTrans = true;
  struct ExprList *columns = expr_list_from_ast(pParse, X);
  sql_create_index(pParse, &T, &N, columns, SQL_INDEX_TYPE_CONSTRAINT_PK,
                   SORT_ORDER_ASC, false);
}

cmd ::= ALTER TABLE nm(T) RENAME TO nm(N). {
    pParse->initiateTTrans = true;
    sql_alter_table_rename(pParse, &T, &N);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z). {
  pParse->initiateTTrans = true;
  sql_drop_table_constraint(pParse, &X, &Z);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) FOREIGN KEY. {
  pParse->initiateTTrans = true;
  sql_drop_tuple_foreign_key(pParse, &X, &Z);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) PRIMARY KEY. {
  pParse->initiateTTrans = true;
  sql_drop_primary_key(pParse, &X, &Z);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) UNIQUE. {
  pParse->initiateTTrans = true;
  sql_drop_unique(pParse, &X, &Z);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) CHECK. {
  pParse->initiateTTrans = true;
  sql_drop_tuple_check(pParse, &X, &Z);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z). {
  pParse->initiateTTrans = true;
  sql_drop_field_constraint(pParse, &X, &F, &Z);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z) FOREIGN KEY. {
  pParse->initiateTTrans = true;
  sql_drop_field_foreign_key(pParse, &X, &F, &Z);
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z) CHECK. {
  pParse->initiateTTrans = true;
  sql_drop_field_check(pParse, &X, &F, &Z);
}

//////////////////////// COMMON TABLE EXPRESSIONS ////////////////////////////
%type with_old {With*}
%destructor with_old {sqlWithDelete($$);}
with_old(A) ::= with(X). {
  A = with_from_ast(pParse, X);
}

%type with {struct ast_with_list *}
with(A) ::= . {A = 0;}
with(A) ::= WITH wqlist(W).              { A = W; }
with(A) ::= WITH RECURSIVE wqlist(W).    { A = W; }

%type wqlist {struct ast_with_list *}
wqlist(A) ::= nm(X) idlist_opt(Y) AS LP select(Z) RP. {
  A = ast_with_list_append(pParse, NULL, &X, Y, Z);
}
wqlist(A) ::= wqlist(A) COMMA nm(X) idlist_opt(Y) AS LP select(Z) RP. {
  A = ast_with_list_append(pParse, A, &X, Y, Z);
}

////////////////////////////// TYPE DECLARATION ///////////////////////////////
%type typedef {enum field_type}
typedef(A) ::= TEXT . { A = FIELD_TYPE_STRING; }
typedef(A) ::= STRING_KW . { A = FIELD_TYPE_STRING; }
typedef(A) ::= SCALAR . { A = FIELD_TYPE_SCALAR; }
/** BOOL | BOOLEAN is not used due to possible bug in Lemon. */
typedef(A) ::= BOOL . { A = FIELD_TYPE_BOOLEAN; }
typedef(A) ::= BOOLEAN . { A = FIELD_TYPE_BOOLEAN; }
typedef(A) ::= VARBINARY . { A = FIELD_TYPE_VARBINARY; }
typedef(A) ::= UUID . { A = FIELD_TYPE_UUID; }
typedef(A) ::= ANY . { A = FIELD_TYPE_ANY; }
typedef(A) ::= ARRAY . { A = FIELD_TYPE_ARRAY; }
typedef(A) ::= MAP . { A = FIELD_TYPE_MAP; }
typedef(A) ::= DATETIME . { A = FIELD_TYPE_DATETIME; }
typedef(A) ::= INTERVAL . { A = FIELD_TYPE_INTERVAL; }
typedef(A) ::= VARCHAR LP INTEGER RP . {
  A = FIELD_TYPE_STRING;
}
typedef(A) ::= NUMBER . {
  A = FIELD_TYPE_NUMBER;
}
typedef(A) ::= DOUBLE . {
  A = FIELD_TYPE_DOUBLE;
}
typedef(A) ::= INT|INTEGER_KW . {
  A = FIELD_TYPE_INTEGER;
}
typedef(A) ::= UNSIGNED . {
  A = FIELD_TYPE_UNSIGNED;
}
typedef(A) ::= DECIMAL . {
  A = FIELD_TYPE_DECIMAL;
}
