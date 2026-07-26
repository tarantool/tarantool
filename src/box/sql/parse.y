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
  pParse->ast.type = SQL_AST_TX_START;
}
cmd ::= COMMIT. {
  pParse->ast.type = SQL_AST_TX_COMMIT;
}
cmd ::= ROLLBACK. {
  pParse->ast.type = SQL_AST_TX_ROLLBACK;
}

savepoint_opt ::= SAVEPOINT.
savepoint_opt ::= .
cmd ::= SAVEPOINT nm(X). {
  pParse->ast.type = SQL_AST_TX_SAVEPOINT_NEW;
  pParse->ast.savepoint = X;
}
cmd ::= RELEASE savepoint_opt nm(X). {
  pParse->ast.type = SQL_AST_TX_SAVEPOINT_RELEASE;
  pParse->ast.savepoint = X;
}
cmd ::= ROLLBACK TO savepoint_opt nm(X). {
  pParse->ast.type = SQL_AST_TX_SAVEPOINT_ROLLBACK;
  pParse->ast.savepoint = X;
}

///////////////////// The CREATE TABLE statement ////////////////////////////
//
cmd ::= CREATE TABLE ifnotexists(E) nm(Y) LP table_properties(P) RP. {
  pParse->ast.type = SQL_AST_CREATE_TABLE;
  pParse->ast.create_table.name = Y;
  pParse->ast.create_table.properties = P;
  pParse->ast.create_table.if_not_exists = E;
}
cmd ::= CREATE TABLE ifnotexists(E) nm(Y) LP table_properties(P) RP
        WITH ENGINE EQ STRING(A). {
  pParse->ast.type = SQL_AST_CREATE_TABLE;
  pParse->ast.create_table.name = Y;
  pParse->ast.create_table.engine = A;
  pParse->ast.create_table.properties = P;
  pParse->ast.create_table.if_not_exists = E;
}

createkw(A) ::= CREATE(A).  {disableLookaside(pParse);}

%type ifnotexists {int}
ifnotexists(A) ::= .              {A = 0;}
ifnotexists(A) ::= IF NOT EXISTS. {A = 1;}

%type table_properties {struct ast_table_properties *}
table_properties(A) ::= table_properties(A) COMMA column_def(C). {
  A = ast_table_properties_append_column(A, C);
}
table_properties(A) ::= table_properties(A) COMMA table_constraint(C). {
  A = ast_table_properties_append_constraint(A, C);
}
table_properties(A) ::= column_def(C). {
  A = ast_table_properties_new(&pParse->region);
  A = ast_table_properties_append_column(A, C);
}
table_properties(A) ::= table_constraint(C). {
  A = ast_table_properties_new(&pParse->region);
  A = ast_table_properties_append_constraint(A, C);
}

%type column_def {struct ast_column *}
column_def(A) ::= nm(N) typedef(Y) column_property_list(L) autoinc(I). {
  A = ast_column_new(&pParse->region);
  A->name = N;
  A->type = Y;
  A->properties = L;
  A->is_autoinc = I != 0;
}

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

%type column_property_list {struct ast_property_list *}
column_property_list(A) ::= column_property_list(A) column_property(X). {
  A = ast_property_list_append(&pParse->region, A, X);
}
column_property_list(A) ::= . {
  A = NULL;
}

%type column_property {struct ast_property *}
/**
 * Rule precedence [COLLATE] forces the parser to reduce this rule rather
 * than shift a follow-up NOT or COLLATE token into the expression: the
 * token NOT come earlier in the precedence table than COLLATE,
 * and COLLATE itself is %left - so both shift-reduce conflicts
 * with `expr NOT ...` / `expr COLLATE ...` resolve as reduce, and the
 * tokens go to the next column property instead.
 */
column_property(A) ::= DEFAULT expr(X). [COLLATE] {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_DEFAULT;
  A->expr = X;
}
column_property(A) ::= NULL. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_NULL;
}
column_property(A) ::= NOT NULL onconf(R). {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_NOT_NULL;
  A->action = R;
}
column_property(A) ::= cconsname(N) PRIMARY KEY sortorder(Z). {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_PRIMARY_KEY;
  A->name = N;
  A->order = Z;
}
column_property(A) ::= cconsname(N) UNIQUE. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_UNIQUE;
  A->name = N;
}
column_property(A) ::= cconsname(N) CHECK LP expr(X) RP. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_CHECK;
  A->name = N;
  A->expr = X;
}
column_property(A) ::= cconsname(N) REFERENCES nm(T) idlist_opt(TA). {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_FOREIGN_KEY;
  A->name = N;
  A->foreign_key.foreign_table = T;
  A->foreign_key.foreign_columns = TA;
}
column_property(A) ::= COLLATE id(C). {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_COLLATE;
  A->collate = C;
}

%type cconsname { struct Token }
cconsname(N) ::= CONSTRAINT nm(X). { N = X; }
cconsname(N) ::= . { N = Token_nil; }

// The optional AUTOINCREMENT keyword
%type autoinc {int}
autoinc(X) ::= .          {X = 0;}
autoinc(X) ::= AUTOINCR.  {X = 1;}

%type table_constraint {struct ast_property *}
table_constraint(A) ::= FOREIGN KEY LP idlist(FA) RP REFERENCES nm(T)
                        idlist_opt(TA). {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_FOREIGN_KEY;
  A->foreign_key.columns = FA;
  A->foreign_key.foreign_table = T;
  A->foreign_key.foreign_columns = TA;
}
table_constraint(A) ::= CHECK LP expr(X) RP. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_CHECK;
  A->expr = X;
}
table_constraint(A) ::= UNIQUE LP sortlist(L) RP. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_UNIQUE;
  A->columns = L;
}
table_constraint(A) ::= PRIMARY KEY LP sortlist_autoinc(L) RP. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_PRIMARY_KEY;
  A->columns = L;
}
table_constraint(A) ::= table_constraint_named(A).

%type table_constraint_named {struct ast_property *}
table_constraint_named(A) ::= CONSTRAINT nm(N) FOREIGN KEY LP idlist(FA) RP
                              REFERENCES nm(T) idlist_opt(TA). {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_FOREIGN_KEY;
  A->name = N;
  A->foreign_key.columns = FA;
  A->foreign_key.foreign_table = T;
  A->foreign_key.foreign_columns = TA;
}
table_constraint_named(A) ::= CONSTRAINT nm(N) CHECK LP expr(X) RP. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_CHECK;
  A->name = N;
  A->expr = X;
}
table_constraint_named(A) ::= CONSTRAINT nm(N) UNIQUE LP sortlist(L) RP. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_UNIQUE;
  A->name = N;
  A->columns = L;
}
table_constraint_named(A) ::= CONSTRAINT nm(N) PRIMARY KEY
                              LP sortlist_autoinc(L) RP. {
  A = ast_property_new(&pParse->region);
  A->type = SQL_AST_PROPERTY_PRIMARY_KEY;
  A->name = N;
  A->columns = L;
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
  pParse->ast.type = SQL_AST_DROP_TABLE;
  pParse->ast.drop_table.name = X;
  pParse->ast.drop_table.if_exists = E;
}

cmd ::= DROP VIEW ifexists(E) nm(X) . {
  pParse->ast.type = SQL_AST_DROP_VIEW;
  pParse->ast.drop_table.name = X;
  pParse->ast.drop_table.if_exists = E;
}

%type ifexists {int}
ifexists(A) ::= IF EXISTS.   {A = 1;}
ifexists(A) ::= .            {A = 0;}

///////////////////// The CREATE VIEW statement /////////////////////////////
//
cmd ::= CREATE VIEW ifnotexists(E) nm(N) idlist_opt(C) AS select(S). {
  pParse->ast.type = SQL_AST_CREATE_VIEW;
  pParse->ast.create_view.name = N;
  pParse->ast.create_view.select = S;
  pParse->ast.create_view.columns = C;
  pParse->ast.create_view.if_not_exists = E;
}
cmd ::= VIEW_ENTRY CREATE VIEW ifnotexists nm idlist_opt AS select(S). {
  pParse->ast.type = SQL_AST_VIEW;
  pParse->ast.select = S;
}

//////////////////////// The SELECT statement /////////////////////////////////
//
cmd ::= select(X). {
  pParse->ast.type = SQL_AST_SELECT;
  pParse->ast.select = X;
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
    struct ast_source *new = ast_source_new(&pParse->region);
    new->select = Z;
    A = ast_select_new(&pParse->region);
    A->sources = ast_source_list_append(&pParse->region, NULL, new);
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
  A = ast_select_new(&pParse->region);
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
  A = ast_select_new(&pParse->region);
  A->columns = X;
  A->flags = SF_Values;
}
values(A) ::= values(X) COMMA LP exprlist(Y) RP. {
  X->flags |= SF_Compound;
  X->flags &= ~SF_MultiValue;
  A = ast_select_new(&pParse->region);
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
  A = ast_expr_list_append(&pParse->region, NULL, X);
  ast_expr_list_set_name(A, &Y);
  A->is_select_list = true;
}
select_list(A) ::= STAR(X). {
  struct ast_expr *expr = ast_expr_new(&pParse->region, X.z, X.n, TK_ASTERISK);
  A = ast_expr_list_append(&pParse->region, NULL, expr);
  A->is_select_list = true;
}
select_list(A) ::= nm(X) DOT STAR(Y). {
  struct ast_expr *dot = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n,
                                      TK_DOT);
  dot->left = ast_expr_new(&pParse->region, X.z, X.n, TK_ID);
  dot->right = ast_expr_new(&pParse->region, Y.z, Y.n, TK_ASTERISK);
  A = ast_expr_list_append(&pParse->region, NULL, dot);
  A->is_select_list = true;
}
select_list(A) ::= select_list(A) COMMA expr(X) as(Y). {
  A = ast_expr_list_append(&pParse->region, A, X);
  ast_expr_list_set_name(A, &Y);
}
select_list(A) ::= select_list(A) COMMA STAR(X). {
  struct ast_expr *expr = ast_expr_new(&pParse->region, X.z, X.n, TK_ASTERISK);
  A = ast_expr_list_append(&pParse->region, A, expr);
}
select_list(A) ::= select_list(A) COMMA nm(X) DOT STAR(Y). {
  struct ast_expr *dot = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n,
                                      TK_DOT);
  dot->left = ast_expr_new(&pParse->region, X.z, X.n, TK_ID);
  dot->right = ast_expr_new(&pParse->region, Y.z, Y.n, TK_ASTERISK);
  A = ast_expr_list_append(&pParse->region, A, dot);
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
  A = ast_source_list_append(&pParse->region, NULL, X);
}
source_list(A) ::= LP source_list(F) RP as(Z). {
  if (Z.n == 0) {
    A = F;
  } else if (F->len == 1) {
    struct ast_source *old = stailq_first_entry(&F->head, struct ast_source,
                                                link);
    struct ast_source *new = ast_source_new(&pParse->region);
    new->name = old->name;
    new->alias = Z;
    new->select = old->select;
    A = ast_source_list_append(&pParse->region, NULL, new);
  } else {
    struct ast_select *subquery = ast_select_new(&pParse->region);
    subquery->sources = F;
    subquery->flags = SF_NestedFrom;
    struct ast_source *src = ast_source_new(&pParse->region);
    src->alias = Z;
    src->select = subquery;
    A = ast_source_list_append(&pParse->region, NULL, src);
  }
}
source_list(A) ::= source_list(A) joinop(Y) source(X) on_opt(N) using_opt(U). {
  X->join_type = Y;
  X->join_on = N;
  X->join_using = U;
  A = ast_source_list_append(&pParse->region, A, X);
}
source_list(A) ::= source_list(X) joinop(Y) LP source_list(F) RP as(Z) on_opt(N)
                   using_opt(U). {
  if (F->len == 1) {
    struct ast_source *old = stailq_first_entry(&F->head, struct ast_source,
                                                link);
    struct ast_source *new = ast_source_new(&pParse->region);
    new->name = old->name;
    new->alias = Z;
    new->select = old->select;
    new->join_type = Y;
    new->join_on = N;
    new->join_using = U;
    A = ast_source_list_append(&pParse->region, X, new);
  } else {
    struct ast_select *subquery = ast_select_new(&pParse->region);
    subquery->sources = F;
    subquery->flags = SF_NestedFrom;
    struct ast_source *src = ast_source_new(&pParse->region);
    src->alias = Z;
    src->select = subquery;
    src->join_type = Y;
    src->join_on = N;
    src->join_using = U;
    A = ast_source_list_append(&pParse->region, X, src);
  }
}

%type source {struct ast_source *}
source(A) ::= seqscan(X) nm(Y) as(Z) indexed_opt(I). {
  A = ast_source_new(&pParse->region);
  A->name = Y;
  A->alias = Z;
  A->indexed_by = I;
  A->disallow_scan = X;
}
source(A) ::= seqscan(X) nm(Y) LP exprlist(E) RP as(Z). {
  A = ast_source_new(&pParse->region);
  A->name = Y;
  A->alias = Z;
  A->func_args = E;
  A->is_tab_func = true;
  A->disallow_scan = X;
}
source(A) ::= LP select(S) RP as(Z). {
  A = ast_source_new(&pParse->region);
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
orderby_opt(A) ::= .                          {A = 0;}
orderby_opt(A) ::= ORDER BY sortlist(X).      {A = X;}

%type sortlist {struct ast_expr_list *}
sortlist(A) ::= sortlist(A) COMMA expr(Y) sortorder(Z). {
  A = ast_expr_list_append(&pParse->region, A, Y);
  ast_expr_list_set_order(A, Z);
}
sortlist(A) ::= expr(Y) sortorder(Z). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
  ast_expr_list_set_order(A, Z);
}

%type sortlist_autoinc {struct ast_expr_list *}
sortlist_autoinc(A) ::= sortlist_autoinc(A) COMMA expr(Y) sortorder(Z)
                        autoinc(I). {
  A = ast_expr_list_append(&pParse->region, A, Y);
  ast_expr_list_set_order(A, Z);
  ast_expr_list_set_autoinc(A, I != 0);
}
sortlist_autoinc(A) ::= expr(Y) sortorder(Z) autoinc(I). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
  ast_expr_list_set_order(A, Z);
  ast_expr_list_set_autoinc(A, I != 0);
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
cmd ::= with(W) insert(I). {
  I->with = W;
  pParse->ast.type = SQL_AST_INSERT;
  pParse->ast.insert = I;
}

%type insert {struct ast_insert *}
insert(A) ::= INSERT orconf(R) INTO nm(X) idlist_opt(F) select(S). {
  A = ast_insert_new(&pParse->region);
  A->table = X;
  A->select = S;
  A->columns = F;
  A->action = R;
}
insert(A) ::= REPLACE INTO nm(X) idlist_opt(F) select(S). {
  A = ast_insert_new(&pParse->region);
  A->table = X;
  A->select = S;
  A->columns = F;
  A->action = ON_CONFLICT_ACTION_REPLACE;
}
insert(A) ::= INSERT orconf(R) INTO nm(X) idlist_opt(F) DEFAULT VALUES. {
  A = ast_insert_new(&pParse->region);
  A->table = X;
  A->columns = F;
  A->action = R;
}
insert(A) ::= REPLACE INTO nm(X) idlist_opt(F) DEFAULT VALUES. {
  A = ast_insert_new(&pParse->region);
  A->table = X;
  A->columns = F;
  A->action = ON_CONFLICT_ACTION_REPLACE;
}

%type insert_cmd {int}
insert_cmd(A) ::= INSERT orconf(R).   {A = R;}
insert_cmd(A) ::= REPLACE.            {A = ON_CONFLICT_ACTION_REPLACE;}

%type idlist_opt {struct ast_id_list *}
%type idlist {struct ast_id_list *}

idlist_opt(A) ::= .                       {A = 0;}
idlist_opt(A) ::= LP idlist(X) RP.    {A = X;}
idlist(A) ::= idlist(A) COMMA nm(Y). {
  A = ast_id_list_append(&pParse->region, A, &Y);
}
idlist(A) ::= nm(Y). {
  A = ast_id_list_append(&pParse->region, NULL, &Y);
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
  A = ast_expr_new(&pParse->region, X.z, X.n, @X);
}
expr(A) ::= LP(B) expr(X) RP(E). {
  A = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, TK_PARENTHESES);
  A->left = X;
}
expr(A) ::= id(X). {
  A = ast_expr_new(&pParse->region, X.z, X.n, TK_ID);
}
expr(A) ::= CROSS|INNER|LEFT|NATURAL|OUTER|RIGHT(X). {
  A = ast_expr_new(&pParse->region, X.z, X.n, TK_ID);
}
expr(A) ::= nm(X) DOT nm(Y). {
  A = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n, TK_DOT);
  A->left = ast_expr_new(&pParse->region, X.z, X.n, TK_ID);
  A->right = ast_expr_new(&pParse->region, Y.z, Y.n, TK_ID);
}
expr(A) ::= VARNUM(X). {
  A = ast_expr_new(&pParse->region, X.z, X.n, TK_VARIABLE);
}
expr(A) ::= COLON|VARIABLE(X) id(Y).     {
  A = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n, TK_VARIABLE);
  A->left = ast_expr_new(&pParse->region, Y.z, Y.n, TK_STRING);
}
expr(A) ::= COLON|VARIABLE(X) INTEGER(Y).     {
  A = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n, TK_VARIABLE);
  A->left = ast_expr_new(&pParse->region, Y.z, Y.n, TK_INTEGER);
}
expr(A) ::= expr(X) COLLATE id(C). {
  A = ast_expr_new(&pParse->region, X->str, (C.z - X->str) + C.n, TK_COLLATE);
  A->left = X;
  A->right = ast_expr_new(&pParse->region, C.z, C.n, TK_ID);
}
expr(A) ::= CAST(X) LP expr(E) AS typedef(T) RP(Y). {
  A = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n, TK_CAST);
  A->type = T;
  A->left = E;
}
expr(A) ::= expr(X) LB getlist(Y) RB(E). {
  A = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_GETITEM);
  A->left = X;
  A->list = Y;
}

%type getlist {struct ast_expr_list *}
getlist(A) ::= getlist(A) RB LB expr(X). {
  A = ast_expr_list_append(&pParse->region, A, X);
}
getlist(A) ::= expr(X). {
  A = ast_expr_list_append(&pParse->region, NULL, X);
}

expr(A) ::= LB(X) exprlist(Y) RB(E). {
  A = ast_expr_new(&pParse->region, X.z, (E.z - X.z) + E.n, TK_ARRAY);
  A->list = Y;
}
expr(A) ::= LCB(X) maplist(Y) RCB(E). {
  A = ast_expr_new(&pParse->region, X.z, (E.z - X.z) + E.n, TK_MAP);
  A->list = Y;
}

%type maplist {struct ast_expr_list *}
%type nmaplist {struct ast_expr_list *}
maplist(A) ::= nmaplist(A).
maplist(A) ::= . {
  A = NULL;
}
nmaplist(A) ::= nmaplist(A) COMMA expr(X) COLON expr(Y). {
  A = ast_expr_list_append(&pParse->region, A, X);
  A = ast_expr_list_append(&pParse->region, A, Y);
}
nmaplist(A) ::= expr(X) COLON expr(Y). {
  A = ast_expr_list_append(&pParse->region, NULL, X);
  A = ast_expr_list_append(&pParse->region, A, Y);
}

expr(A) ::= TRIM(X) LP(B) trim_operands(Y) RP(E). {
  A = ast_expr_new(&pParse->region, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(&pParse->region, X.z, X.n, TK_STRING);
  A->right = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, TK_VECTOR);
  A->right->list = Y;
}

%type trim_operands {struct ast_expr_list *}
trim_operands(A) ::= LEADING|TRAILING|BOTH(N) expr(Z) FROM expr(Y). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
  A = ast_expr_list_append(&pParse->region, A, ast_expr_new(&pParse->region,
                           N.z, N.n, @N));
  A = ast_expr_list_append(&pParse->region, A, Z);
}
trim_operands(A) ::= LEADING|TRAILING|BOTH(N) FROM expr(Y). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
  A = ast_expr_list_append(&pParse->region, A, ast_expr_new(&pParse->region,
                           N.z, N.n, @N));
}
trim_operands(A) ::= expr(Z) FROM expr(Y). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
  A = ast_expr_list_append(&pParse->region, A, Z);
}
trim_operands(A) ::= expr(Y). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
}

expr(A) ::= id(X) LP(B) distinct(D) exprlist(Y) RP(E). {
  A = ast_expr_new(&pParse->region, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(&pParse->region, X.z, X.n, TK_STRING);
  uint8_t op = D == SF_Distinct ? TK_DISTINCT : TK_VECTOR;
  A->right = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, op);
  A->right->list = Y;
}
expr(A) ::= CHAR(X) LP(B) distinct(D) exprlist(Y) RP(E). {
  A = ast_expr_new(&pParse->region, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(&pParse->region, X.z, X.n, TK_STRING);
  uint8_t op = D == SF_Distinct ? TK_DISTINCT : TK_VECTOR;
  A->right = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, op);
  A->right->list = Y;
}
expr(A) ::= id(X) LP STAR RP(E). {
  A = ast_expr_new(&pParse->region, X.z, (E.z - X.z) + E.n, TK_FUNCTION);
  A->left = ast_expr_new(&pParse->region, X.z, X.n, TK_STRING);
}
expr(A) ::= LP(L) nexprlist(X) COMMA expr(Y) RP(R). {
  A = ast_expr_new(&pParse->region, L.z, (R.z - L.z) + R.n, TK_VECTOR);
  A->list = ast_expr_list_append(&pParse->region, X, Y);
}
expr(A) ::= expr(X) AND(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) OR(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) LT|GT|GE|LE(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) EQ|NE(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) BITAND|BITOR|LSHIFT|RSHIFT(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) PLUS|MINUS(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) STAR|SLASH|REM(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) CONCAT(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, @OP);
  A->left = X;
  A->right = Y;
}
expr(A) ::= expr(X) LIKE_KW|MATCH(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len,
                   TK_FUNCTION);
  A->left = ast_expr_new(&pParse->region, OP.z, OP.n, TK_STRING);
  A->right = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len,
                          TK_VECTOR);
  A->right->list = ast_expr_list_append(&pParse->region, NULL, Y);
  A->right->list = ast_expr_list_append(&pParse->region, A->right->list, X);
}
expr(A) ::= expr(X) NOT LIKE_KW|MATCH(OP) expr(Y). {
  A = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len, TK_NOT);
  A->left = ast_expr_new(&pParse->region, X->str, (Y->str - X->str) + Y->len,
                         TK_FUNCTION);
  A->left->left = ast_expr_new(&pParse->region, OP.z, OP.n, TK_STRING);
  A->left->right = ast_expr_new(&pParse->region, X->str,
                                (Y->str - X->str) + Y->len, TK_VECTOR);
  A->left->right->list = ast_expr_list_append(&pParse->region, NULL, Y);
  A->left->right->list = ast_expr_list_append(&pParse->region,
                                              A->left->right->list, X);
}
expr(A) ::= expr(X) LIKE_KW|MATCH(OP) expr(Y) ESCAPE expr(E). {
  A = ast_expr_new(&pParse->region, X->str, (E->str - X->str) + E->len,
                   TK_FUNCTION);
  A->left = ast_expr_new(&pParse->region, OP.z, OP.n, TK_STRING);
  A->right = ast_expr_new(&pParse->region, X->str, (E->str - X->str) + E->len,
                          TK_VECTOR);
  A->right->list = ast_expr_list_append(&pParse->region, NULL, Y);
  A->right->list = ast_expr_list_append(&pParse->region, A->right->list, X);
  A->right->list = ast_expr_list_append(&pParse->region, A->right->list, E);
}
expr(A) ::= expr(X) NOT LIKE_KW|MATCH(OP) expr(Y) ESCAPE expr(E). {
  A = ast_expr_new(&pParse->region, X->str, (E->str - X->str) + E->len, TK_NOT);
  A->left = ast_expr_new(&pParse->region, X->str, (E->str - X->str) + E->len,
                         TK_FUNCTION);
  A->left->left = ast_expr_new(&pParse->region, OP.z, OP.n, TK_STRING);
  A->left->right = ast_expr_new(&pParse->region, X->str,
                                (E->str - X->str) + E->len, TK_VECTOR);
  A->left->right->list = ast_expr_list_append(&pParse->region, NULL, Y);
  A->left->right->list = ast_expr_list_append(&pParse->region,
                                              A->left->right->list, X);
  A->left->right->list = ast_expr_list_append(&pParse->region,
                                              A->left->right->list, E);
}
expr(A) ::= expr(X) IS NULL(E). {
  A = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_ISNULL);
  A->left = X;
}
expr(A) ::= expr(X) IS NOT NULL(E). {
  A = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_NOTNULL);
  A->left = X;
}
expr(A) ::= NOT(B) expr(X). {
  A = ast_expr_new(&pParse->region, B.z, (X->str - B.z) + X->len, @B);
  A->left = X;
}
expr(A) ::= BITNOT(B) expr(X). {
  A = ast_expr_new(&pParse->region, B.z, (X->str - B.z) + X->len, @B);
  A->left = X;
}
expr(A) ::= MINUS(B) expr(X). [BITNOT] {
  A = ast_expr_new(&pParse->region, B.z, (X->str - B.z) + X->len, TK_UMINUS);
  A->left = X;
}
expr(A) ::= PLUS(B) expr(X). [BITNOT] {
  A = ast_expr_new(&pParse->region, B.z, (X->str - B.z) + X->len, TK_UPLUS);
  A->left = X;
}
expr(A) ::= expr(Z) BETWEEN(N) expr(X) AND expr(Y). {
  A = ast_expr_new(&pParse->region, Z->str, (Y->str - Z->str) + Y->len, @N);
  A->left = Z;
  A->list = ast_expr_list_append(&pParse->region, NULL, X);
  A->list = ast_expr_list_append(&pParse->region, A->list, Y);
}
expr(A) ::= expr(Z) NOT BETWEEN(N) expr(X) AND expr(Y). {
  A = ast_expr_new(&pParse->region, Z->str, (Y->str - Z->str) + Y->len, TK_NOT);
  A->left = ast_expr_new(&pParse->region, Z->str, (Y->str - Z->str) + Y->len,
                         @N);
  A->left->left = Z;
  A->left->list = ast_expr_list_append(&pParse->region, NULL, X);
  A->left->list = ast_expr_list_append(&pParse->region, A->left->list, Y);
}
expr(A) ::= expr(X) IN LP(B) exprlist(Y) RP(E). {
  A = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, TK_VECTOR);
  A->right->list = Y;
}
expr(A) ::= expr(X) NOT IN LP(B) exprlist(Y) RP(E). {
  A = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_NOT);
  A->left = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n,
                                TK_VECTOR);
  A->left->right->list = Y;
}
expr(A) ::= expr(X) IN LP(B) select(Y) RP(E). {
  A = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, TK_SELECT);
  A->right->select = Y;
}
expr(A) ::= expr(X) NOT IN LP(B) select(Y) RP(E). {
  A = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_NOT);
  A->left = ast_expr_new(&pParse->region, X->str, (E.z - X->str) + E.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n,
                                TK_SELECT);
  A->left->right->select = Y;
}
expr(A) ::= expr(X) IN nm(Y). {
  struct ast_source *src = ast_source_new(&pParse->region);
  src->name = Y;
  struct ast_select *select = ast_select_new(&pParse->region);
  select->sources = ast_source_list_append(&pParse->region, NULL, src);
  A = ast_expr_new(&pParse->region, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(&pParse->region, Y.z, Y.n, TK_SELECT);
  A->right->select = select;
}
expr(A) ::= expr(X) IN nm(Y) LP exprlist(E) RP. {
  struct ast_source *src = ast_source_new(&pParse->region);
  src->name = Y;
  if (E != NULL) {
    src->func_args = E;
    src->is_tab_func = true;
  }
  struct ast_select *select = ast_select_new(&pParse->region);
  select->sources = ast_source_list_append(&pParse->region, NULL, src);
  A = ast_expr_new(&pParse->region, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left = X;
  A->right = ast_expr_new(&pParse->region, Y.z, Y.n, TK_SELECT);
  A->right->select = select;
}
expr(A) ::= expr(X) NOT IN nm(Y). {
  struct ast_source *src = ast_source_new(&pParse->region);
  src->name = Y;
  struct ast_select *select = ast_select_new(&pParse->region);
  select->sources = ast_source_list_append(&pParse->region, NULL, src);
  A = ast_expr_new(&pParse->region, X->str, (Y.z - X->str) + Y.n, TK_NOT);
  A->left = ast_expr_new(&pParse->region, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(&pParse->region, Y.z, Y.n, TK_SELECT);
  A->left->right->select = select;
}
expr(A) ::= expr(X) NOT IN nm(Y) LP exprlist(E) RP. {
  struct ast_source *src = ast_source_new(&pParse->region);
  src->name = Y;
  if (E != NULL) {
    src->func_args = E;
    src->is_tab_func = true;
  }
  struct ast_select *select = ast_select_new(&pParse->region);
  select->sources = ast_source_list_append(&pParse->region, NULL, src);
  A = ast_expr_new(&pParse->region, X->str, (Y.z - X->str) + Y.n, TK_NOT);
  A->left = ast_expr_new(&pParse->region, X->str, (Y.z - X->str) + Y.n, TK_IN);
  A->left->left = X;
  A->left->right = ast_expr_new(&pParse->region, Y.z, Y.n, TK_SELECT);
  A->left->right->select = select;
}
expr(A) ::= LP(B) select(X) RP(E). {
  A = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, TK_SELECT);
  A->select = X;
}
expr(A) ::= EXISTS(B) LP select(Y) RP(E). {
  A = ast_expr_new(&pParse->region, B.z, (E.z - B.z) + E.n, TK_EXISTS);
  A->select = Y;
}
expr(A) ::= CASE(C) case_exprlist(Y) END(E). {
  A = ast_expr_new(&pParse->region, C.z, (E.z - C.z) + E.n, TK_CASE);
  A->list = Y;
}
expr(A) ::= CASE(C) expr(X) case_exprlist(Y) END(E). {
  A = ast_expr_new(&pParse->region, C.z, (E.z - C.z) + E.n, TK_CASE);
  A->left = X;
  A->list = Y;
}

%type case_exprlist_when {struct ast_expr_list *}
case_exprlist_when(A) ::= case_exprlist_when(A) WHEN expr(Y) THEN expr(Z). {
  A = ast_expr_list_append(&pParse->region, A, Y);
  A = ast_expr_list_append(&pParse->region, A, Z);
}
case_exprlist_when(A) ::= WHEN expr(Y) THEN expr(Z). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
  A = ast_expr_list_append(&pParse->region, A, Z);
}

%type case_exprlist {struct ast_expr_list *}
case_exprlist(A) ::= case_exprlist_when(A).
case_exprlist(A) ::= case_exprlist_when(A) ELSE expr(X). {
  A = ast_expr_list_append(&pParse->region, A, X);
}

%type exprlist {struct ast_expr_list *}
%type nexprlist {struct ast_expr_list *}

exprlist(A) ::= nexprlist(A).
exprlist(A) ::= . {
  A = NULL;
}
nexprlist(A) ::= nexprlist(A) COMMA expr(Y). {
  A = ast_expr_list_append(&pParse->region, A, Y);
}
nexprlist(A) ::= expr(Y). {
  A = ast_expr_list_append(&pParse->region, NULL, Y);
}

///////////////////////////// The CREATE INDEX command ///////////////////////
//
cmd ::= CREATE INDEX ifnotexists(E) nm(X) ON nm(Y) LP sortlist(Z) RP. {
  pParse->ast.type = SQL_AST_CREATE_INDEX;
  pParse->ast.create_index.name = X;
  pParse->ast.create_index.table = Y;
  pParse->ast.create_index.columns = Z;
  pParse->ast.create_index.if_not_exists = E;
}
cmd ::= CREATE UNIQUE INDEX ifnotexists(E) nm(X) ON nm(Y) LP sortlist(Z) RP. {
  pParse->ast.type = SQL_AST_CREATE_INDEX;
  pParse->ast.create_index.name = X;
  pParse->ast.create_index.table = Y;
  pParse->ast.create_index.columns = Z;
  pParse->ast.create_index.if_not_exists = E;
  pParse->ast.create_index.is_unique = true;
}

///////////////////////////// The DROP INDEX command /////////////////////////
//
cmd ::= DROP INDEX ifexists(E) nm(X) ON nm(Y). {
  pParse->ast.type = SQL_AST_DROP_INDEX;
  pParse->ast.drop_index.name = X;
  pParse->ast.drop_index.table = Y;
  pParse->ast.drop_index.if_exists = E;
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
cmd ::= FUNCTION_ENTRY expr_old(E). {
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

cmd ::= createkw TRIGGER ifnotexists(E) nm(N) trigger_time trigger_event
        ON nm(T) foreach_clause when_clause BEGIN trigger_cmd_list END. {
  pParse->initiateTTrans = true;
  vdbe_emit_create_trigger(pParse, pParse->zTail, &N, &T, E);
}
cmd ::= TRIGGER_ENTRY createkw TRIGGER ifnotexists nm(N) trigger_time(C)
        trigger_event(D) ON nm(T) foreach_clause when_clause(G)
        BEGIN trigger_cmd_list(S) END. {
  pParse->parsed_ast_type = AST_TYPE_TRIGGER;
  pParse->parsed_ast.trigger = sql_trigger_new(pParse, &N, &T, C, D.a,
                                               id_list_from_ast(D.b), G, S);
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
  A = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n, TK_RAISE);
  A->on_conflict_action = ON_CONFLICT_ACTION_IGNORE;
}
expr(A) ::= RAISE(X) LP raisetype(T) COMMA STRING(Z) RP(Y).  {
  A = ast_expr_new(&pParse->region, X.z, (Y.z - X.z) + Y.n, TK_RAISE);
  A->left = ast_expr_new(&pParse->region, Z.z, Z.n, @Z);
  A->on_conflict_action = T;
}

%type raisetype {enum on_conflict_action}
raisetype(A) ::= ROLLBACK.  {A = ON_CONFLICT_ACTION_ROLLBACK;}
raisetype(A) ::= ABORT.     {A = ON_CONFLICT_ACTION_ABORT;}
raisetype(A) ::= FAIL.      {A = ON_CONFLICT_ACTION_FAIL;}


////////////////////////  DROP TRIGGER statement //////////////////////////////
cmd ::= DROP TRIGGER ifexists(E) nm(X). {
  pParse->ast.type = SQL_AST_DROP_TRIGGER;
  pParse->ast.drop_trigger.name = X;
  pParse->ast.drop_trigger.if_exists = E;
}

//////////////////////// ALTER TABLE table ... ////////////////////////////////
cmd ::= ALTER TABLE nm(T) ADD column_def(C). {
  pParse->ast.type = SQL_AST_ALTER_ADD_COLUMN;
  pParse->ast.alter_add_column.table = T;
  pParse->ast.alter_add_column.col = C;
}
cmd ::= ALTER TABLE nm(T) ADD COLUMN column_def(C). {
  pParse->ast.type = SQL_AST_ALTER_ADD_COLUMN;
  pParse->ast.alter_add_column.table = T;
  pParse->ast.alter_add_column.col = C;
}

cmd ::= ALTER TABLE nm(X) ADD table_constraint_named(C). {
  pParse->ast.type = SQL_AST_ALTER_ADD_CONSTRAINT;
  pParse->ast.alter_add_constraint.table = X;
  pParse->ast.alter_add_constraint.con = C;
}

cmd ::= ALTER TABLE nm(T) RENAME TO nm(N). {
  pParse->ast.type = SQL_AST_ALTER_RENAME;
  pParse->ast.alter_rename.old_name = T;
  pParse->ast.alter_rename.new_name = N;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z). {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.table = X;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) FOREIGN KEY. {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.table = X;
  pParse->ast.alter_drop_constraint.type = SQL_AST_PROPERTY_FOREIGN_KEY;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) PRIMARY KEY. {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.table = X;
  pParse->ast.alter_drop_constraint.type = SQL_AST_PROPERTY_PRIMARY_KEY;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) UNIQUE. {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.table = X;
  pParse->ast.alter_drop_constraint.type = SQL_AST_PROPERTY_UNIQUE;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) CHECK. {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.table = X;
  pParse->ast.alter_drop_constraint.type = SQL_AST_PROPERTY_CHECK;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z). {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.column = F;
  pParse->ast.alter_drop_constraint.table = X;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z) FOREIGN KEY. {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.column = F;
  pParse->ast.alter_drop_constraint.table = X;
  pParse->ast.alter_drop_constraint.type = SQL_AST_PROPERTY_FOREIGN_KEY;
}

cmd ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z) CHECK. {
  pParse->ast.type = SQL_AST_ALTER_DROP_CONSTRAINT;
  pParse->ast.alter_drop_constraint.name = Z;
  pParse->ast.alter_drop_constraint.column = F;
  pParse->ast.alter_drop_constraint.table = X;
  pParse->ast.alter_drop_constraint.type = SQL_AST_PROPERTY_CHECK;
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
  A = ast_with_list_append(&pParse->region, NULL, &X, Y, Z);
}
wqlist(A) ::= wqlist(A) COMMA nm(X) idlist_opt(Y) AS LP select(Z) RP. {
  A = ast_with_list_append(&pParse->region, A, &X, Y, Z);
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
