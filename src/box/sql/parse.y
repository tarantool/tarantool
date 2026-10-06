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
%extra_argument {struct sql_parser_context *ctx}

// This code runs whenever there is a syntax error
//
%syntax_error {
  UNUSED_PARAMETER(yymajor);  /* Silence some compiler warnings */
  assert( TOKEN.z[0] );  /* The tokenizer always gives us a token */
  if (yypParser->is_fallback_failed && TOKEN.isReserved) {
    const char *token = tt_cstr(TOKEN.z, TOKEN.n);
    diag_set(ClientError, ER_SQL_KEYWORD_IS_RESERVED, ctx->line,
             ctx->pos, token, token);
  } else {
    diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN, ctx->line,
             tt_cstr(TOKEN.z, TOKEN.n));
  }
  ctx->is_aborted = true;
}
%stack_overflow {
  diag_set(ClientError, ER_SQL_STACK_OVERFLOW);
  ctx->is_aborted = true;
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
#define PARSER_ERROR_CHECK && ! ctx->is_aborted

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

} // end %include

// Input is a single SQL command
input ::= ecmd(X). {
  ctx->ast = X;
}

%type ecmd {struct sql_ast *}
%type cmd {struct sql_ast *}
ecmd(A) ::= explain(E) cmd(X) SEMI. {
  A = X;
  A->explain = E;
}
ecmd(A) ::= SEMI. {
  A = NULL;
  diag_set(ClientError, ER_SQL_STATEMENT_EMPTY);
  ctx->is_aborted = true;
}

%type explain {enum ast_explain_type}
explain(A) ::= . {
  A = SQL_AST_EXPLAIN_NONE;
}
explain(A) ::= EXPLAIN. {
  A = SQL_AST_EXPLAIN_VDBE;
}
explain(A) ::= EXPLAIN QUERY PLAN. {
  A = SQL_AST_EXPLAIN_PLAN;
}

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

cmd(A) ::= START TRANSACTION. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TX_START;
}
cmd(A) ::= COMMIT. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TX_COMMIT;
}
cmd(A) ::= ROLLBACK. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TX_ROLLBACK;
}

savepoint_opt ::= SAVEPOINT.
savepoint_opt ::= .
cmd(A) ::= SAVEPOINT nm(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TX_SAVEPOINT_NEW;
  A->savepoint = X;
}
cmd(A) ::= RELEASE savepoint_opt nm(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TX_SAVEPOINT_RELEASE;
  A->savepoint = X;
}
cmd(A) ::= ROLLBACK TO savepoint_opt nm(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TX_SAVEPOINT_ROLLBACK;
  A->savepoint = X;
}

///////////////////// The CREATE TABLE statement ////////////////////////////
//
cmd(A) ::= CREATE TABLE ifnotexists(E) nm(Y) LP table_properties(P) RP. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_CREATE_TABLE;
  A->create_table.name = Y;
  A->create_table.properties = P;
  A->create_table.if_not_exists = E;
}
cmd(A) ::= CREATE TABLE ifnotexists(E) nm(Y) LP table_properties(P) RP
        WITH ENGINE EQ STRING(S). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_CREATE_TABLE;
  A->create_table.name = Y;
  A->create_table.engine = S;
  A->create_table.properties = P;
  A->create_table.if_not_exists = E;
}

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
  A = ast_table_properties_new(ctx->region);
  A = ast_table_properties_append_column(A, C);
}
table_properties(A) ::= table_constraint(C). {
  A = ast_table_properties_new(ctx->region);
  A = ast_table_properties_append_constraint(A, C);
}

%type column_def {struct ast_column *}
column_def(A) ::= nm(N) typedef(Y) column_property_list(L) autoinc(I). {
  A = ast_column_new(ctx->region);
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
    diag_set(ClientError, ER_SQL_KEYWORD_IS_RESERVED, ctx->line,
             ctx->pos, token, token);
    ctx->is_aborted = true;
  }
}

%type column_property_list {struct ast_property_list *}
column_property_list(A) ::= column_property_list(A) column_property(X). {
  A = ast_property_list_append(ctx->region, A, X);
}
column_property_list(A) ::= . {
  A = NULL;
}

%type column_property {struct ast_property *}
column_property(A) ::= DEFAULT span_start(S) expr(X) default_span_end(E). {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_DEFAULT;
  A->expr = X;
  A->span = S;
  A->span_len = E - S;
}

/**
 * Rule precedence [COLLATE] forces the parser to reduce this rule rather
 * than shift a follow-up NOT or COLLATE token into the DEFAULT expression:
 * the token NOT come earlier in the precedence table than COLLATE,
 * and COLLATE itself is %left - so both shift-reduce conflicts
 * with `expr NOT ...` / `expr COLLATE ...` resolve as reduce, and the
 * tokens go to the next column property instead.
 */
%type default_span_end {const char *}
default_span_end(A) ::= . [COLLATE] {
  A = ctx->prev_token_end;
}
column_property(A) ::= NULL. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_NULL;
}
column_property(A) ::= NOT NULL onconf(R). {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_NOT_NULL;
  A->action = R;
}
column_property(A) ::= cconsname(N) PRIMARY KEY sortorder(Z). {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_PRIMARY_KEY;
  A->name = N;
  A->order = Z;
}
column_property(A) ::= cconsname(N) UNIQUE. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_UNIQUE;
  A->name = N;
}
column_property(A) ::= cconsname(N) CHECK LP span_start(S) expr(X)
                       span_end(E) RP. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_CHECK;
  A->name = N;
  A->expr = X;
  A->span = S;
  A->span_len = E - S;
}
column_property(A) ::= cconsname(N) REFERENCES nm(T) idlist_opt(TA). {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_FOREIGN_KEY;
  A->name = N;
  A->foreign_key.foreign_table = T;
  A->foreign_key.foreign_columns = TA;
}
column_property(A) ::= COLLATE id(C). {
  A = ast_property_new(ctx->region);
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
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_FOREIGN_KEY;
  A->foreign_key.columns = FA;
  A->foreign_key.foreign_table = T;
  A->foreign_key.foreign_columns = TA;
}
table_constraint(A) ::= CHECK LP span_start(S) expr(X) span_end(E) RP. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_CHECK;
  A->expr = X;
  A->span = S;
  A->span_len = E - S;
}
table_constraint(A) ::= UNIQUE LP sortlist(L) RP. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_UNIQUE;
  A->columns = L;
}
table_constraint(A) ::= PRIMARY KEY LP sortlist_autoinc(L) RP. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_PRIMARY_KEY;
  A->columns = L;
}
table_constraint(A) ::= table_constraint_named(A).

%type table_constraint_named {struct ast_property *}
table_constraint_named(A) ::= CONSTRAINT nm(N) FOREIGN KEY LP idlist(FA) RP
                              REFERENCES nm(T) idlist_opt(TA). {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_FOREIGN_KEY;
  A->name = N;
  A->foreign_key.columns = FA;
  A->foreign_key.foreign_table = T;
  A->foreign_key.foreign_columns = TA;
}
table_constraint_named(A) ::= CONSTRAINT nm(N) CHECK LP span_start(S) expr(X)
                              span_end(E) RP. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_CHECK;
  A->name = N;
  A->expr = X;
  A->span = S;
  A->span_len = E - S;
}
table_constraint_named(A) ::= CONSTRAINT nm(N) UNIQUE LP sortlist(L) RP. {
  A = ast_property_new(ctx->region);
  A->type = SQL_AST_PROPERTY_UNIQUE;
  A->name = N;
  A->columns = L;
}
table_constraint_named(A) ::= CONSTRAINT nm(N) PRIMARY KEY
                              LP sortlist_autoinc(L) RP. {
  A = ast_property_new(ctx->region);
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

cmd(A) ::= DROP TABLE ifexists(E) nm(X) . {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_DROP_TABLE;
  A->drop_table.name = X;
  A->drop_table.if_exists = E;
}

cmd(A) ::= DROP VIEW ifexists(E) nm(X) . {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_DROP_VIEW;
  A->drop_table.name = X;
  A->drop_table.if_exists = E;
}

%type ifexists {int}
ifexists(A) ::= IF EXISTS.   {A = 1;}
ifexists(A) ::= .            {A = 0;}

///////////////////// The CREATE VIEW statement /////////////////////////////
//
cmd(A) ::= CREATE VIEW ifnotexists(E) nm(N) idlist_opt(C) AS select(S). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_CREATE_VIEW;
  A->create_view.name = N;
  A->create_view.select = S;
  A->create_view.columns = C;
  A->create_view.if_not_exists = E;
}
cmd(A) ::= VIEW_ENTRY CREATE VIEW ifnotexists nm idlist_opt AS select(S). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_VIEW;
  A->select = S;
}

//////////////////////// The SELECT statement /////////////////////////////////
//
cmd(A) ::= select(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_SELECT;
  A->select = X;
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
    struct ast_source *new = ast_source_new(ctx->region);
    new->select = Z;
    A = ast_select_new(ctx->region);
    A->sources = ast_source_list_append(ctx->region, NULL, new);
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
  A = ast_select_new(ctx->region);
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
  A = ast_select_new(ctx->region);
  A->columns = X;
  A->flags = SF_Values;
}
values(A) ::= values(X) COMMA LP exprlist(Y) RP. {
  X->flags |= SF_Compound;
  X->flags &= ~SF_MultiValue;
  A = ast_select_new(ctx->region);
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
select_list(A) ::= span_start(S) expr(X) span_end(E) as(Y). {
  A = ast_expr_list_append(ctx->region, NULL, X);
  ast_expr_list_set_name(A, &Y);
  ast_expr_list_set_span(A, S, E);
}
select_list(A) ::= STAR(X). {
  struct ast_expr *expr = ast_expr_new(ctx->region, TK_ASTERISK);
  A = ast_expr_list_append(ctx->region, NULL, expr);
  ast_expr_list_set_span(A, X.z, X.z + X.n);
}
select_list(A) ::= span_start nm(X) DOT STAR(Y). {
  struct ast_expr *dot = ast_expr_new(ctx->region, TK_DOT);
  dot->bin.left = ast_expr_new(ctx->region, TK_ID);
  dot->bin.left->val.str = X.z;
  dot->bin.left->val.len = X.n;
  dot->bin.right = ast_expr_new(ctx->region, TK_ASTERISK);
  A = ast_expr_list_append(ctx->region, NULL, dot);
  ast_expr_list_set_span(A, X.z, Y.z + Y.n);
}
select_list(A) ::= select_list(A) COMMA span_start(S) expr(X) span_end(E)
                   as(Y). {
  A = ast_expr_list_append(ctx->region, A, X);
  ast_expr_list_set_name(A, &Y);
  ast_expr_list_set_span(A, S, E);
}
select_list(A) ::= select_list(A) COMMA STAR(X). {
  struct ast_expr *expr = ast_expr_new(ctx->region, TK_ASTERISK);
  A = ast_expr_list_append(ctx->region, A, expr);
  ast_expr_list_set_span(A, X.z, X.z + X.n);
}
select_list(A) ::= select_list(A) COMMA span_start nm(X) DOT STAR(Y). {
  struct ast_expr *dot = ast_expr_new(ctx->region, TK_DOT);
  dot->bin.left = ast_expr_new(ctx->region, TK_ID);
  dot->bin.left->val.str = X.z;
  dot->bin.left->val.len = X.n;
  dot->bin.right = ast_expr_new(ctx->region, TK_ASTERISK);
  A = ast_expr_list_append(ctx->region, A, dot);
  ast_expr_list_set_span(A, X.z, Y.z + Y.n);
}

// The start of the text of the expression that follows. It also precedes
// `nm DOT STAR` in the column list of SELECT, where its value is not used:
// without it, the parser could not choose between reducing it and shifting
// the identifier that both alternatives start with.
%type span_start {const char *}
span_start(A) ::= . {
  A = ctx->token_start;
}

// The end of the text of the expression that precedes.
%type span_end {const char *}
span_end(A) ::= . {
  A = ctx->prev_token_end;
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
  A = ast_source_list_append(ctx->region, NULL, X);
}
source_list(A) ::= LP source_list(F) RP as(Z). {
  if (Z.n == 0) {
    A = F;
  } else if (F->len == 1) {
    struct ast_source *old = stailq_first_entry(&F->head, struct ast_source,
                                                link);
    struct ast_source *new = ast_source_new(ctx->region);
    new->name = old->name;
    new->alias = Z;
    new->select = old->select;
    A = ast_source_list_append(ctx->region, NULL, new);
  } else {
    struct ast_select *subquery = ast_select_new(ctx->region);
    subquery->sources = F;
    subquery->flags = SF_NestedFrom;
    struct ast_source *src = ast_source_new(ctx->region);
    src->alias = Z;
    src->select = subquery;
    A = ast_source_list_append(ctx->region, NULL, src);
  }
}
source_list(A) ::= source_list(A) joinop(Y) source(X) on_opt(N) using_opt(U). {
  X->join_type = Y;
  X->join_on = N;
  X->join_using = U;
  A = ast_source_list_append(ctx->region, A, X);
}
source_list(A) ::= source_list(X) joinop(Y) LP source_list(F) RP as(Z) on_opt(N)
                   using_opt(U). {
  if (F->len == 1) {
    struct ast_source *old = stailq_first_entry(&F->head, struct ast_source,
                                                link);
    struct ast_source *new = ast_source_new(ctx->region);
    new->name = old->name;
    new->alias = Z;
    new->select = old->select;
    new->join_type = Y;
    new->join_on = N;
    new->join_using = U;
    A = ast_source_list_append(ctx->region, X, new);
  } else {
    struct ast_select *subquery = ast_select_new(ctx->region);
    subquery->sources = F;
    subquery->flags = SF_NestedFrom;
    struct ast_source *src = ast_source_new(ctx->region);
    src->alias = Z;
    src->select = subquery;
    src->join_type = Y;
    src->join_on = N;
    src->join_using = U;
    A = ast_source_list_append(ctx->region, X, src);
  }
}

%type source {struct ast_source *}
source(A) ::= seqscan(X) nm(Y) as(Z) indexed_opt(I). {
  A = ast_source_new(ctx->region);
  A->name = Y;
  A->alias = Z;
  A->indexed_by = I;
  A->disallow_scan = X;
}
source(A) ::= LP select(S) RP as(Z). {
  A = ast_source_new(ctx->region);
  A->alias = Z;
  A->select = S;
}

%type joinop {int}
joinop(A) ::= COMMA|JOIN. {
  A = JT_INNER;
}
joinop(X) ::= join_type(A) JOIN. {
  X = A;
}
joinop(X) ::= join_type(A) join_type(B) JOIN. {
  X = A | B;
}
joinop(X) ::= join_type(A) join_type(B) join_type(C) JOIN. {
  X = A | B | C;
}

%type join_type {int}
join_type(A) ::= CROSS. {
  A = JT_INNER | JT_CROSS;
}
join_type(A) ::= INNER. {
  A = JT_INNER;
}
join_type(A) ::= LEFT. {
  A = JT_LEFT | JT_OUTER;
}
join_type(A) ::= NATURAL. {
  A = JT_NATURAL;
}
join_type(A) ::= OUTER. {
  A = JT_OUTER;
}
join_type(A) ::= RIGHT. {
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
  A = ast_expr_list_append(ctx->region, A, Y);
  ast_expr_list_set_order(A, Z);
}
sortlist(A) ::= expr(Y) sortorder(Z). {
  A = ast_expr_list_append(ctx->region, NULL, Y);
  ast_expr_list_set_order(A, Z);
}

%type sortlist_autoinc {struct ast_expr_list *}
sortlist_autoinc(A) ::= sortlist_autoinc(A) COMMA expr(Y) sortorder(Z)
                        autoinc(I). {
  A = ast_expr_list_append(ctx->region, A, Y);
  ast_expr_list_set_order(A, Z);
  ast_expr_list_set_autoinc(A, I != 0);
}
sortlist_autoinc(A) ::= expr(Y) sortorder(Z) autoinc(I). {
  A = ast_expr_list_append(ctx->region, NULL, Y);
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
cmd(A) ::= with(W) delete(D). {
  D->with = W;
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_DELETE;
  A->del = D;
}

%type delete {struct ast_delete *}
delete(A) ::= DELETE FROM nm(X) indexed_opt(I) where_opt(W). {
  A = ast_delete_new(ctx->region);
  A->table = X;
  A->indexed_by = I;
  A->where = W;
}

/////////////////////////// The TRUNCATE statement /////////////////////////////
//
cmd(A) ::= TRUNCATE TABLE nm(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TRUNCATE;
  A->truncate.table = X;
}

%type where_opt {struct ast_expr *}
where_opt(A) ::= .                    {A = 0;}
where_opt(A) ::= WHERE expr(X). {
  A = X;
}

////////////////////////// The UPDATE command ////////////////////////////////
//
cmd(A) ::= with(W) update(U). {
  U->with = W;
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_UPDATE;
  A->update = U;
}

%type update {struct ast_update *}
update(A) ::= UPDATE orconf(R) nm(X) indexed_opt(I) SET setlist(Y)
              where_opt(W). {
  A = ast_update_new(ctx->region);
  A->table = X;
  A->indexed_by = I;
  A->set_list = Y;
  A->where = W;
  A->action = R;
}

%type setlist {struct ast_set_list *}
setlist(A) ::= setlist(A) COMMA nm(X) EQ expr(Y). {
  A = ast_set_list_append_expr(ctx->region, A, &X, Y);
}
setlist(A) ::= setlist(A) COMMA LP idlist(X) RP EQ expr(Y). {
  A = ast_set_list_append_vector(ctx->region, A, X, Y);
}
setlist(A) ::= nm(X) EQ expr(Y). {
  A = ast_set_list_append_expr(ctx->region, NULL, &X, Y);
}
setlist(A) ::= LP idlist(X) RP EQ expr(Y). {
  A = ast_set_list_append_vector(ctx->region, NULL, X, Y);
}

////////////////////////// The INSERT command /////////////////////////////////
//
cmd(A) ::= with(W) insert(I). {
  I->with = W;
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_INSERT;
  A->insert = I;
}

%type insert {struct ast_insert *}
insert(A) ::= INSERT orconf(R) INTO nm(X) idlist_opt(F) select(S). {
  A = ast_insert_new(ctx->region);
  A->table = X;
  A->select = S;
  A->columns = F;
  A->action = R;
}
insert(A) ::= REPLACE INTO nm(X) idlist_opt(F) select(S). {
  A = ast_insert_new(ctx->region);
  A->table = X;
  A->select = S;
  A->columns = F;
  A->action = ON_CONFLICT_ACTION_REPLACE;
}
insert(A) ::= INSERT orconf(R) INTO nm(X) idlist_opt(F) DEFAULT VALUES. {
  A = ast_insert_new(ctx->region);
  A->table = X;
  A->columns = F;
  A->action = R;
}
insert(A) ::= REPLACE INTO nm(X) idlist_opt(F) DEFAULT VALUES. {
  A = ast_insert_new(ctx->region);
  A->table = X;
  A->columns = F;
  A->action = ON_CONFLICT_ACTION_REPLACE;
}

%type idlist_opt {struct ast_id_list *}
%type idlist {struct ast_id_list *}

idlist_opt(A) ::= .                       {A = 0;}
idlist_opt(A) ::= LP idlist(X) RP.    {A = X;}
idlist(A) ::= idlist(A) COMMA nm(Y). {
  A = ast_id_list_append(ctx->region, A, &Y);
}
idlist(A) ::= nm(Y). {
  A = ast_id_list_append(ctx->region, NULL, &Y);
}

/////////////////////////// Expression Processing /////////////////////////////
//
%type expr {struct ast_expr *}
%type term {struct ast_expr *}
expr(A) ::= term(A).
term(A) ::= NULL|BLOB|STRING|FALSE|TRUE|UNKNOWN|FLOAT|DECIMAL|INTEGER(X). {
  A = ast_expr_new(ctx->region, @X);
  A->val.str = X.z;
  A->val.len = X.n;
}
expr(A) ::= LP expr(X) RP. {
  A = X;
}
expr(A) ::= id(X). {
  A = ast_expr_new(ctx->region, TK_ID);
  A->val.str = X.z;
  A->val.len = X.n;
}
expr(A) ::= CROSS|INNER|LEFT|NATURAL|OUTER|RIGHT(X). {
  A = ast_expr_new(ctx->region, TK_ID);
  A->val.str = X.z;
  A->val.len = X.n;
}
expr(A) ::= nm(X) DOT nm(Y). {
  A = ast_expr_new(ctx->region, TK_DOT);
  A->bin.left = ast_expr_new(ctx->region, TK_ID);
  A->bin.left->val.str = X.z;
  A->bin.left->val.len = X.n;
  A->bin.right = ast_expr_new(ctx->region, TK_ID);
  A->bin.right->val.str = Y.z;
  A->bin.right->val.len = Y.n;
}
expr(A) ::= VAR_ANON|VAR_NUM|VAR_NAME(X). {
  A = ast_expr_new(ctx->region, @X);
  A->val.str = X.z;
  A->val.len = X.n;
}
expr(A) ::= COLON(X) id(Y). {
  A = ast_expr_new(ctx->region, TK_VAR_NAME);
  A->val.str = X.z;
  A->val.len = (Y.z - X.z) + Y.n;
}
expr(A) ::= expr(X) COLLATE id(C). {
  A = ast_expr_new(ctx->region, TK_COLLATE);
  A->coll.expr = X;
  A->coll.name = C.z;
  A->coll.name_len = C.n;
}
expr(A) ::= CAST LP expr(E) AS typedef(T) RP. {
  A = ast_expr_new(ctx->region, TK_CAST);
  A->cast.expr = E;
  A->cast.type = T;
}
expr(A) ::= expr(X) LB getlist(Y) RB. {
  A = ast_expr_new(ctx->region, TK_GETITEM);
  A->getitem.value = X;
  A->getitem.keys = Y;
}

%type getlist {struct ast_expr_list *}
getlist(A) ::= getlist(A) RB LB expr(X). {
  A = ast_expr_list_append(ctx->region, A, X);
}
getlist(A) ::= expr(X). {
  A = ast_expr_list_append(ctx->region, NULL, X);
}

expr(A) ::= LB exprlist(Y) RB. {
  A = ast_expr_new(ctx->region, TK_ARRAY);
  A->list = Y;
}
expr(A) ::= LCB maplist(Y) RCB. {
  A = ast_expr_new(ctx->region, TK_MAP);
  A->list = Y;
}

%type maplist {struct ast_expr_list *}
%type nmaplist {struct ast_expr_list *}
maplist(A) ::= nmaplist(A).
maplist(A) ::= . {
  A = NULL;
}
nmaplist(A) ::= nmaplist(A) COMMA expr(X) COLON expr(Y). {
  A = ast_expr_list_append(ctx->region, A, X);
  A = ast_expr_list_append(ctx->region, A, Y);
}
nmaplist(A) ::= expr(X) COLON expr(Y). {
  A = ast_expr_list_append(ctx->region, NULL, X);
  A = ast_expr_list_append(ctx->region, A, Y);
}

expr(A) ::= TRIM(X) LP trim_operands(Y) RP. {
  A = ast_expr_new(ctx->region, TK_FUNCTION);
  A->func.name = X.z;
  A->func.name_len = X.n;
  A->func.args = Y;
}

%type trim_operands {struct ast_expr_list *}
trim_operands(A) ::= LEADING|TRAILING|BOTH(N) expr(Z) FROM expr(Y). {
  struct ast_expr *spec = ast_expr_new(ctx->region, @N);
  spec->val.str = N.z;
  spec->val.len = N.n;
  A = ast_expr_list_append(ctx->region, NULL, Y);
  A = ast_expr_list_append(ctx->region, A, spec);
  A = ast_expr_list_append(ctx->region, A, Z);
}
trim_operands(A) ::= LEADING|TRAILING|BOTH(N) FROM expr(Y). {
  struct ast_expr *spec = ast_expr_new(ctx->region, @N);
  spec->val.str = N.z;
  spec->val.len = N.n;
  A = ast_expr_list_append(ctx->region, NULL, Y);
  A = ast_expr_list_append(ctx->region, A, spec);
}
trim_operands(A) ::= expr(Z) FROM expr(Y). {
  A = ast_expr_list_append(ctx->region, NULL, Y);
  A = ast_expr_list_append(ctx->region, A, Z);
}
trim_operands(A) ::= expr(Y). {
  A = ast_expr_list_append(ctx->region, NULL, Y);
}

expr(A) ::= id(X) LP distinct(D) exprlist(Y) RP. {
  A = ast_expr_new(ctx->region, TK_FUNCTION);
  A->func.name = X.z;
  A->func.name_len = X.n;
  A->func.is_distinct = D == SF_Distinct;
  A->func.args = Y;
}
expr(A) ::= CHAR(X) LP distinct(D) exprlist(Y) RP. {
  A = ast_expr_new(ctx->region, TK_FUNCTION);
  A->func.name = X.z;
  A->func.name_len = X.n;
  A->func.is_distinct = D == SF_Distinct;
  A->func.args = Y;
}
expr(A) ::= id(X) LP STAR RP. {
  A = ast_expr_new(ctx->region, TK_FUNCTION);
  A->func.name = X.z;
  A->func.name_len = X.n;
}
expr(A) ::= LP nexprlist(X) COMMA expr(Y) RP. {
  A = ast_expr_new(ctx->region, TK_VECTOR);
  A->list = ast_expr_list_append(ctx->region, X, Y);
}
expr(A) ::= expr(X) AND(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) OR(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) LT|GT|GE|LE(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) EQ|NE(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) BITAND|BITOR|LSHIFT|RSHIFT(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) PLUS|MINUS(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) STAR|SLASH|REM(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) CONCAT(OP) expr(Y). {
  A = ast_expr_new(ctx->region, @OP);
  A->bin.left = X;
  A->bin.right = Y;
}
expr(A) ::= expr(X) LIKE_KW|MATCH(OP) expr(Y). {
  A = ast_expr_new(ctx->region, TK_FUNCTION);
  A->func.name = OP.z;
  A->func.name_len = OP.n;
  A->func.args = ast_expr_list_append(ctx->region, NULL, Y);
  A->func.args = ast_expr_list_append(ctx->region, A->func.args, X);
}
expr(A) ::= expr(X) NOT LIKE_KW|MATCH(OP) expr(Y). {
  struct ast_expr *like = ast_expr_new(ctx->region, TK_FUNCTION);
  like->func.name = OP.z;
  like->func.name_len = OP.n;
  like->func.args = ast_expr_list_append(ctx->region, NULL, Y);
  like->func.args = ast_expr_list_append(ctx->region, like->func.args, X);
  A = ast_expr_new(ctx->region, TK_NOT);
  A->arg = like;
}
expr(A) ::= expr(X) LIKE_KW|MATCH(OP) expr(Y) ESCAPE expr(E). {
  A = ast_expr_new(ctx->region, TK_FUNCTION);
  A->func.name = OP.z;
  A->func.name_len = OP.n;
  A->func.args = ast_expr_list_append(ctx->region, NULL, Y);
  A->func.args = ast_expr_list_append(ctx->region, A->func.args, X);
  A->func.args = ast_expr_list_append(ctx->region, A->func.args, E);
}
expr(A) ::= expr(X) NOT LIKE_KW|MATCH(OP) expr(Y) ESCAPE expr(E). {
  struct ast_expr *like = ast_expr_new(ctx->region, TK_FUNCTION);
  like->func.name = OP.z;
  like->func.name_len = OP.n;
  like->func.args = ast_expr_list_append(ctx->region, NULL, Y);
  like->func.args = ast_expr_list_append(ctx->region, like->func.args, X);
  like->func.args = ast_expr_list_append(ctx->region, like->func.args, E);
  A = ast_expr_new(ctx->region, TK_NOT);
  A->arg = like;
}
expr(A) ::= expr(X) IS NULL. {
  A = ast_expr_new(ctx->region, TK_ISNULL);
  A->arg = X;
}
expr(A) ::= expr(X) IS NOT NULL. {
  A = ast_expr_new(ctx->region, TK_NOTNULL);
  A->arg = X;
}
expr(A) ::= NOT(B) expr(X). {
  A = ast_expr_new(ctx->region, @B);
  A->arg = X;
}
expr(A) ::= BITNOT(B) expr(X). {
  A = ast_expr_new(ctx->region, @B);
  A->arg = X;
}
expr(A) ::= MINUS expr(X). [BITNOT] {
  A = ast_expr_new(ctx->region, TK_UMINUS);
  A->arg = X;
}
expr(A) ::= PLUS expr(X). [BITNOT] {
  A = ast_expr_new(ctx->region, TK_UPLUS);
  A->arg = X;
}
expr(A) ::= expr(Z) BETWEEN(N) expr(X) AND expr(Y). {
  A = ast_expr_new(ctx->region, @N);
  A->between.value = Z;
  A->between.lower = X;
  A->between.upper = Y;
}
expr(A) ::= expr(Z) NOT BETWEEN(N) expr(X) AND expr(Y). {
  struct ast_expr *between = ast_expr_new(ctx->region, @N);
  between->between.value = Z;
  between->between.lower = X;
  between->between.upper = Y;
  A = ast_expr_new(ctx->region, TK_NOT);
  A->arg = between;
}
expr(A) ::= expr(X) IN LP exprlist(Y) RP. {
  A = ast_expr_new(ctx->region, TK_IN);
  A->in.value = X;
  A->in.list = Y;
}
expr(A) ::= expr(X) NOT IN LP exprlist(Y) RP. {
  struct ast_expr *in = ast_expr_new(ctx->region, TK_IN);
  in->in.value = X;
  in->in.list = Y;
  A = ast_expr_new(ctx->region, TK_NOT);
  A->arg = in;
}
expr(A) ::= expr(X) IN LP select(Y) RP. {
  A = ast_expr_new(ctx->region, TK_IN);
  A->in.value = X;
  A->in.select = Y;
}
expr(A) ::= expr(X) NOT IN LP select(Y) RP. {
  struct ast_expr *in = ast_expr_new(ctx->region, TK_IN);
  in->in.value = X;
  in->in.select = Y;
  A = ast_expr_new(ctx->region, TK_NOT);
  A->arg = in;
}
expr(A) ::= expr(X) IN nm(Y). {
  struct ast_source *src = ast_source_new(ctx->region);
  src->name = Y;
  struct ast_select *select = ast_select_new(ctx->region);
  select->sources = ast_source_list_append(ctx->region, NULL, src);
  A = ast_expr_new(ctx->region, TK_IN);
  A->in.value = X;
  A->in.select = select;
}
expr(A) ::= expr(X) NOT IN nm(Y). {
  struct ast_source *src = ast_source_new(ctx->region);
  src->name = Y;
  struct ast_select *select = ast_select_new(ctx->region);
  select->sources = ast_source_list_append(ctx->region, NULL, src);
  struct ast_expr *in = ast_expr_new(ctx->region, TK_IN);
  in->in.value = X;
  in->in.select = select;
  A = ast_expr_new(ctx->region, TK_NOT);
  A->arg = in;
}
expr(A) ::= LP select(X) RP. {
  A = ast_expr_new(ctx->region, TK_SELECT);
  A->select = X;
}
expr(A) ::= EXISTS LP select(Y) RP. {
  A = ast_expr_new(ctx->region, TK_EXISTS);
  A->select = Y;
}
expr(A) ::= CASE case_exprlist(Y) END. {
  A = ast_expr_new(ctx->region, TK_CASE);
  A->cs.list = Y;
}
expr(A) ::= CASE expr(X) case_exprlist(Y) END. {
  A = ast_expr_new(ctx->region, TK_CASE);
  A->cs.value = X;
  A->cs.list = Y;
}

%type case_exprlist_when {struct ast_expr_list *}
case_exprlist_when(A) ::= case_exprlist_when(A) WHEN expr(Y) THEN expr(Z). {
  A = ast_expr_list_append(ctx->region, A, Y);
  A = ast_expr_list_append(ctx->region, A, Z);
}
case_exprlist_when(A) ::= WHEN expr(Y) THEN expr(Z). {
  A = ast_expr_list_append(ctx->region, NULL, Y);
  A = ast_expr_list_append(ctx->region, A, Z);
}

%type case_exprlist {struct ast_expr_list *}
case_exprlist(A) ::= case_exprlist_when(A).
case_exprlist(A) ::= case_exprlist_when(A) ELSE expr(X). {
  A = ast_expr_list_append(ctx->region, A, X);
}

%type exprlist {struct ast_expr_list *}
%type nexprlist {struct ast_expr_list *}

exprlist(A) ::= nexprlist(A).
exprlist(A) ::= . {
  A = NULL;
}
nexprlist(A) ::= nexprlist(A) COMMA expr(Y). {
  A = ast_expr_list_append(ctx->region, A, Y);
}
nexprlist(A) ::= expr(Y). {
  A = ast_expr_list_append(ctx->region, NULL, Y);
}

///////////////////////////// The CREATE INDEX command ///////////////////////
//
cmd(A) ::= CREATE INDEX ifnotexists(E) nm(X) ON nm(Y) LP sortlist(Z) RP. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_CREATE_INDEX;
  A->create_index.name = X;
  A->create_index.table = Y;
  A->create_index.columns = Z;
  A->create_index.if_not_exists = E;
}
cmd(A) ::= CREATE UNIQUE INDEX ifnotexists(E) nm(X) ON nm(Y)
           LP sortlist(Z) RP. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_CREATE_INDEX;
  A->create_index.name = X;
  A->create_index.table = Y;
  A->create_index.columns = Z;
  A->create_index.if_not_exists = E;
  A->create_index.is_unique = true;
}

///////////////////////////// The DROP INDEX command /////////////////////////
//
cmd(A) ::= DROP INDEX ifexists(E) nm(X) ON nm(Y). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_DROP_INDEX;
  A->drop_index.name = X;
  A->drop_index.table = Y;
  A->drop_index.if_exists = E;
}

///////////////////////////// The SET SESSION command ////////////////////////
//
cmd(A) ::= SET SESSION nm(X) EQ term(Y). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_SET_SESSION;
  A->set_session.name = X;
  A->set_session.value = Y;
}

///////////////////////////// The PRAGMA command /////////////////////////////
//
cmd(A) ::= PRAGMA nm(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_PRAGMA;
  A->pragma.name = X;
}
cmd(A) ::= PRAGMA nm(X) LP nm(Y) RP. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_PRAGMA;
  A->pragma.name = X;
  A->pragma.table_name = Y;
}
cmd(A) ::= PRAGMA nm(X) LP nm(Y) DOT nm(Z) RP. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_PRAGMA;
  A->pragma.name = X;
  A->pragma.table_name = Y;
  A->pragma.index_name = Z;
}

///////////////////////////// The SQL expression function ////////////////////
//
cmd(A) ::= FUNCTION_ENTRY expr(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_FUNCTION;
  A->expr = X;
}

//////////////////////////// The SHOW CREATE TABLE command /////////////////////
cmd(A) ::= SHOW CREATE TABLE nm(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_SHOW_CREATE_TABLE;
  A->show_create_table = X;
}
cmd(A) ::= SHOW CREATE TABLE. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_SHOW_CREATE_TABLE;
}

//////////////////////////// The CREATE TRIGGER command /////////////////////
cmd(A) ::= CREATE TRIGGER ifnotexists(E) nm(N) trigger_time trigger_event
        ON nm(T) trigger_for_each(F) trigger_when
        BEGIN trigger_action_list(L) END. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_CREATE_TRIGGER;
  A->create_trigger.name = N;
  A->create_trigger.table = T;
  A->create_trigger.actions = L;
  A->create_trigger.is_for_each_row = F;
  A->create_trigger.if_not_exists = E;
}
cmd(A) ::= CREATE TRIGGER ifnotexists(E) nm(N) trigger_time UPDATE OF idlist
        ON nm(T) trigger_for_each(F) trigger_when
        BEGIN trigger_action_list(L) END. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_CREATE_TRIGGER;
  A->create_trigger.name = N;
  A->create_trigger.table = T;
  A->create_trigger.actions = L;
  A->create_trigger.is_for_each_row = F;
  A->create_trigger.if_not_exists = E;
}

cmd(A) ::= TRIGGER_ENTRY CREATE TRIGGER ifnotexists nm(N) trigger_time(C)
           trigger_event(D) ON nm(T) trigger_for_each(F) trigger_when(W)
           BEGIN trigger_action_list(L) END. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TRIGGER;
  A->trigger.name = N;
  A->trigger.time = C;
  A->trigger.event = D;
  A->trigger.table = T;
  A->trigger.is_for_each_row = F;
  A->trigger.when = W;
  A->trigger.actions = L;
}
cmd(A) ::= TRIGGER_ENTRY CREATE TRIGGER ifnotexists nm(N) trigger_time(C)
           UPDATE OF idlist(X) ON nm(T) trigger_for_each(F) trigger_when(W)
           BEGIN trigger_action_list(L) END. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_TRIGGER;
  A->trigger.name = N;
  A->trigger.time = C;
  A->trigger.event = TK_UPDATE;
  A->trigger.table = T;
  A->trigger.is_for_each_row = F;
  A->trigger.when = W;
  A->trigger.columns = X;
  A->trigger.actions = L;
}

%type trigger_event {uint8_t}
trigger_event(A) ::= DELETE|INSERT(E). {
  A = @E;
}
trigger_event(A) ::= UPDATE(E). {
  A = @E;
}

%type trigger_time {int}
trigger_time(A) ::= BEFORE.      { A = TK_BEFORE; }
trigger_time(A) ::= AFTER.       { A = TK_AFTER;  }
trigger_time(A) ::= INSTEAD OF.  { A = TK_INSTEAD;}
trigger_time(A) ::= .            { A = TK_BEFORE; }

%type trigger_for_each {bool}
trigger_for_each(A) ::= FOR EACH ROW. {
  A = true;
}
trigger_for_each(A) ::= . {
  A = false;
}

%type trigger_when {struct ast_expr *}
trigger_when(A) ::= . {
  A = NULL;
}
trigger_when(A) ::= WHEN expr(X). {
  A = X;
}

%type trigger_action_list {struct ast_trigger_action_list *}
trigger_action_list(A) ::= trigger_action(X). {
  A = ast_trigger_action_list_append(ctx->region, NULL, X);
}
trigger_action_list(A) ::= trigger_action_list(A) trigger_action(X). {
  A = ast_trigger_action_list_append(ctx->region, A, X);
}

%type trigger_action {struct ast_trigger_action *}
trigger_action(A) ::= update(X) SEMI. {
  A = ast_trigger_action_new(ctx->region);
  A->op = TK_UPDATE;
  A->update = X;
}
trigger_action(A) ::= insert(X) SEMI. {
  A = ast_trigger_action_new(ctx->region);
  A->op = TK_INSERT;
  A->insert = X;
}
trigger_action(A) ::= delete(X) SEMI. {
  A = ast_trigger_action_new(ctx->region);
  A->op = TK_DELETE;
  A->del = X;
}
trigger_action(A) ::= select(X) SEMI. {
  A = ast_trigger_action_new(ctx->region);
  A->op = TK_SELECT;
  A->select = X;
}

// The special RAISE expression that may occur in trigger programs
expr(A) ::= RAISE LP IGNORE RP.  {
  A = ast_expr_new(ctx->region, TK_RAISE);
  A->raise.action = ON_CONFLICT_ACTION_IGNORE;
}
expr(A) ::= RAISE LP raisetype(T) COMMA STRING(Z) RP.  {
  A = ast_expr_new(ctx->region, TK_RAISE);
  A->raise.str = Z.z;
  A->raise.len = Z.n;
  A->raise.action = T;
}

%type raisetype {enum on_conflict_action}
raisetype(A) ::= ROLLBACK.  {A = ON_CONFLICT_ACTION_ROLLBACK;}
raisetype(A) ::= ABORT.     {A = ON_CONFLICT_ACTION_ABORT;}
raisetype(A) ::= FAIL.      {A = ON_CONFLICT_ACTION_FAIL;}


////////////////////////  DROP TRIGGER statement //////////////////////////////
cmd(A) ::= DROP TRIGGER ifexists(E) nm(X). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_DROP_TRIGGER;
  A->drop_trigger.name = X;
  A->drop_trigger.if_exists = E;
}

//////////////////////// ALTER TABLE table ... ////////////////////////////////
cmd(A) ::= ALTER TABLE nm(T) ADD column_def(C). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_ADD_COLUMN;
  A->alter_add_column.table = T;
  A->alter_add_column.col = C;
}
cmd(A) ::= ALTER TABLE nm(T) ADD COLUMN column_def(C). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_ADD_COLUMN;
  A->alter_add_column.table = T;
  A->alter_add_column.col = C;
}

cmd(A) ::= ALTER TABLE nm(X) ADD table_constraint_named(C). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_ADD_CONSTRAINT;
  A->alter_add_constraint.table = X;
  A->alter_add_constraint.con = C;
}

cmd(A) ::= ALTER TABLE nm(T) RENAME TO nm(N). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_RENAME;
  A->alter_rename.old_name = T;
  A->alter_rename.new_name = N;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.table = X;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) FOREIGN KEY. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.table = X;
  A->alter_drop_constraint.type = SQL_AST_PROPERTY_FOREIGN_KEY;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) PRIMARY KEY. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.table = X;
  A->alter_drop_constraint.type = SQL_AST_PROPERTY_PRIMARY_KEY;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) UNIQUE. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.table = X;
  A->alter_drop_constraint.type = SQL_AST_PROPERTY_UNIQUE;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(Z) CHECK. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.table = X;
  A->alter_drop_constraint.type = SQL_AST_PROPERTY_CHECK;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z). {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.column = F;
  A->alter_drop_constraint.table = X;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z) FOREIGN KEY. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.column = F;
  A->alter_drop_constraint.table = X;
  A->alter_drop_constraint.type = SQL_AST_PROPERTY_FOREIGN_KEY;
}

cmd(A) ::= ALTER TABLE nm(X) DROP CONSTRAINT nm(F) DOT nm(Z) CHECK. {
  A = sql_ast_new(ctx->region);
  A->type = SQL_AST_ALTER_DROP_CONSTRAINT;
  A->alter_drop_constraint.name = Z;
  A->alter_drop_constraint.column = F;
  A->alter_drop_constraint.table = X;
  A->alter_drop_constraint.type = SQL_AST_PROPERTY_CHECK;
}

//////////////////////// COMMON TABLE EXPRESSIONS ////////////////////////////
%type with {struct ast_with_list *}
with(A) ::= . {A = 0;}
with(A) ::= WITH wqlist(W).              { A = W; }
with(A) ::= WITH RECURSIVE wqlist(W).    { A = W; }

%type wqlist {struct ast_with_list *}
wqlist(A) ::= nm(X) idlist_opt(Y) AS LP select(Z) RP. {
  A = ast_with_list_append(ctx->region, NULL, &X, Y, Z);
}
wqlist(A) ::= wqlist(A) COMMA nm(X) idlist_opt(Y) AS LP select(Z) RP. {
  A = ast_with_list_append(ctx->region, A, &X, Y, Z);
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
