#ifndef INCLUDES_BOX_SQL_H
#define INCLUDES_BOX_SQL_H
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

void
sql_init();

void
sql_load_schema();

void
sql_free();

/**
 * struct sqlite3 *
 * sql_get();
 *
 * Currently, this is the only SQL execution interface provided.
 * If not yet initialised, returns NULL.
 * Use the regular sqlite3_* API with this handle, but
 * don't do anything finicky like sqlite3_close.
 * Behind the scenes, this sqlite was rigged to use Tarantool
 * as a data source.
 * @retval SQL handle.
 */
struct sqlite3 *
sql_get();

struct Expr;
struct Parse;
struct Select;
struct Table;
struct sql_trigger;

/**
 * Perform parsing of provided expression. This is done by
 * surrounding the expression w/ 'SELECT ' prefix and perform
 * convetional parsing. Then extract result expression value from
 * stuct Select and return it.
 * @param db SQL context handle.
 * @param expr Expression to parse.
 * @param expr_len Length of @an expr.
 *
 * @retval NULL on error.
 * @retval not NULL Expr AST pointer on success.
 */
struct Expr *
sql_expr_compile(struct sqlite3 *db, const char *expr, int expr_len);

/**
 * This routine executes parser on 'CREATE VIEW ...' statement
 * and loads content of SELECT into internal structs as result.
 *
 * @param db Current SQL context.
 * @param view_stmt String containing 'CREATE VIEW' statement.
 * @retval AST of SELECT statement on success, NULL otherwise.
 */
struct Select *
sql_view_compile(struct sqlite3 *db, const char *view_stmt);

/**
 * Perform parsing of provided SQL request and construct trigger AST.
 * @param db SQL context handle.
 * @param sql request to parse.
 *
 * @retval NULL on error
 * @retval not NULL sql_trigger AST pointer on success.
 */
struct sql_trigger *
sql_trigger_compile(struct sqlite3 *db, const char *sql);

/**
 * Free AST pointed by trigger.
 * @param db SQL handle.
 * @param trigger AST object.
 */
void
sql_trigger_delete(struct sqlite3 *db, struct sql_trigger *trigger);

/**
 * Get server triggers list by space_id.
 * @param space_id valid Space ID.
 *
 * @retval trigger AST list.
 */
struct sql_trigger *
space_trigger_list(uint32_t space_id);

/**
 * Perform replace trigger in SQL internals with new AST object.
 * @param name a name of the trigger.
 * @param space_id of the space to insert trigger.
 * @param trigger AST object to insert.
 * @param[out] old_trigger Old object if exists.
 *
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
sql_trigger_replace(const char *name, uint32_t space_id,
		    struct sql_trigger *trigger,
		    struct sql_trigger **old_trigger);

/**
 * Get trigger name by trigger AST object.
 * @param trigger AST object.
 * @return trigger name string.
 */
const char *
sql_trigger_name(struct sql_trigger *trigger);

/**
 * Get space_id of the space that trigger has been built for.
 * @param trigger AST object.
 * @return space identifier.
 */
uint32_t
sql_trigger_space_id(struct sql_trigger *trigger);

/**
 * Store duplicate of a parsed expression into @a parser.
 * @param parser Parser context.
 * @param select Select to extract from.
 */
void
sql_expr_extract_select(struct Parse *parser, struct Select *select);

/**
 * Given space_id and field number, return default value
 * for the field.
 * @param space_id Space ID.
 * @param fieldno Field index.
 * @retval Pointer to AST corresponding to default value.
 * Can be NULL if no DEFAULT specified or it is a view.
 */
struct Expr*
space_column_default_expr(uint32_t space_id, uint32_t fieldno);

/**
 * Get server checks list by space_id.
 * @param space_id Space ID.
 * @retval Checks list.
 */
struct ExprList *
space_checks_expr_list(uint32_t space_id);

/**
 * Return the number of bytes required to create a duplicate of the
 * expression passed as the first argument. The second argument is a
 * mask containing EXPRDUP_XXX flags.
 *
 * The value returned includes space to create a copy of the Expr struct
 * itself and the buffer referred to by Expr.u.zToken, if any.
 *
 * If the EXPRDUP_REDUCE flag is set, then the return value includes
 * space to duplicate all Expr nodes in the tree formed by Expr.pLeft
 * and Expr.pRight variables (but not for any structures pointed to or
 * descended from the Expr.x.pList or Expr.x.pSelect variables).
 * @param expr Root expression of AST.
 * @param flags The only possible flag is REDUCED, 0 otherwise.
 * @retval Size in bytes needed to duplicate AST and all private
 * strings.
 */
int
sql_expr_sizeof(struct Expr *p, int flags);

/**
 * This function is similar to sqlite3ExprDup(), except that if pzBuffer
 * is not NULL then *pzBuffer is assumed to point to a buffer large enough
 * to store the copy of expression p, the copies of p->u.zToken
 * (if applicable), and the copies of the p->pLeft and p->pRight expressions,
 * if any. Before returning, *pzBuffer is set to the first byte past the
 * portion of the buffer copied into by this function.
 * @param db SQL handle.
 * @param p Root of expression's AST.
 * @param dupFlags EXPRDUP_REDUCE or 0.
 * @param pzBuffer If not NULL, then buffer to store duplicate.
 */
struct Expr *
sql_expr_dup(struct sqlite3 *db, struct Expr *p, int flags, char **buffer);

/**
 * Free AST pointed by expr.
 * @param db SQL handle.
 * @param expr Root pointer of ASR
 * @param extern_alloc True if skeleton was allocated externally.
 */
void
sql_expr_delete(struct sqlite3 *db, struct Expr *expr, bool extern_alloc);

/**
 * Create and initialize a new ephemeral SQL Table object.
 * @param parser SQL Parser object.
 * @param name Table to create name.
 * @retval NULL on memory allocation error, Parser state changed.
 * @retval not NULL on success.
 */
struct Table *
sql_ephemeral_table_new(struct Parse *parser, const char *name);

/**
 * Create and initialize a new ephemeral space_def object.
 * @param parser SQL Parser object.
 * @param name Table to create name.
 * @retval NULL on memory allocation error, Parser state changed.
 * @retval not NULL on success.
 */
struct space_def *
sql_ephemeral_space_def_new(struct Parse *parser, const char *name);

/**
 * Rebuild struct def in Table with memory allocated on a single
 * malloc.
 * @param db The database connection.
 * @param table The Table with fragmented def to rebuild.
 * @retval 1 on memory allocation error.
 * @retval 0 on success.
 */
int
sql_table_def_rebuild(struct sqlite3 *db, struct Table *table);

/**
 * Duplicate Expr list.
 * The flags parameter contains a combination of the EXPRDUP_XXX
 * flags. If the EXPRDUP_REDUCE flag is set, then the structure
 * returned is a truncated version of the usual Expr structure
 * that will be stored as part of the in-memory representation of
 * the database schema.
 * @param db The database connection.
 * @param p The ExprList to duplicate.
 * @param flags EXPRDUP_XXX flags.
 * @retval NULL on memory allocation error.
 * @retval not NULL on success.
 */
struct ExprList *
sql_expr_list_dup(struct sqlite3 *db, struct ExprList *p, int flags);

/**
 * Free AST pointed by expr list.
 * @param db SQL handle.
 * @param expr_list Root pointer of ExprList.
 */
void
sql_expr_list_delete(struct sqlite3 *db, struct ExprList *expr_list);

/**
 * Add a new element to the end of an expression list.
 * @param db SQL handle.
 * @param expr_list List to which to append. Might be NULL.
 * @param expr Expression to be appended. Might be NULL.
 * @retval NULL on memory allocation error. The list is freed.
 * @retval not NULL on success.
 */
struct ExprList *
sql_expr_list_append(struct sqlite3 *db, struct ExprList *expr_list,
		     struct Expr *expr);

/**
 * Resolve names in expressions that can only reference a single
 * table:
 * *   CHECK constraints
 * *   WHERE clauses on partial indices
 * The Expr.iTable value for Expr.op==TK_COLUMN nodes of the
 * expression is set to -1 and the Expr.iColumn value is set to
 * the column number. Any errors cause an error message to be set
 * in parser.
 * @param parser Parsing context.
 * @param table The table being referenced.
 * @param type NC_IsCheck or NC_IdxExpr.
 * @param expr Expression to resolve.  May be NULL.
 * @param expr_list Expression list to resolve.  May be NUL.
 */
void
sql_resolve_self_reference(struct Parse *parser, struct Table *table, int type,
			   struct Expr *expr, struct ExprList *expr_list);

/**
 * Initialize check_list_item.
 * @param expr_list ExprList with item.
 * @param column index.
 * @param expr_name expression name (optional).
 * @param expr_name_len expresson name length (optional).
 * @param expr_str expression to build string.
 * @param expr_str_len expression to build string length.
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
sql_check_list_item_init(struct ExprList *expr_list, int column,
			 const char *expr_name, uint32_t expr_name_len,
			 const char *expr_str, uint32_t expr_str_len);

/**
 * Resolve space_def references checks for expr_list.
 * @param expr_list to modify.
 * @param def to refer to.
 * @retval 0 on success.
 * @retval -1 on error.
 */
int
sql_checks_resolve_space_def_reference(struct ExprList *expr_list,
				       struct space_def *def);

/**
 * Update space_def references for expr_list.
 * @param expr_list to modify.
 * @param def to refer to.
 */
void
sql_checks_update_space_def_reference(struct ExprList *expr_list,
				      struct space_def *def);

/**
 * Initialize a new parser object.
 * A number of service allocations are performed on the region,
 * which is also cleared in the destroy function.
 * @param parser object to initialize.
 * @param db SQLite object.
 */
void
sql_parser_create(struct Parse *parser, struct sqlite3 *db);

/**
 * Release the parser object resources.
 * @param parser object to release.
 */
void
sql_parser_destroy(struct Parse *parser);

/**
 * Release memory allocated for given SELECT and all of its
 * substructures. It accepts NULL pointers.
 *
 * @param db Database handler.
 * @param select Select to be freed.
 */
void
sql_select_delete(struct sqlite3 *db, struct Select *select);

/**
 * Expand all spaces names from 'FROM' clause, including
 * ones from subqueries, and add those names to the original
 * select.
 *
 * @param select Select to be expanded.
 */
void
sql_select_expand_from_tables(struct Select *select);

/**
 * Temporary getter in order to avoid including sqliteInt.h
 * in alter.cc.
 *
 * @param select Select to be examined.
 * @retval Count of 'FROM' tables in given select.
 */
int
sql_select_from_table_count(const struct Select *select);

/**
 * Temporary getter in order to avoid including sqliteInt.h
 * in alter.cc.
 *
 * @param select Select to be examined.
 * @param i Ordinal number of 'FROM' table.
 * @retval Name of i-th 'FROM' table.
 */
const char *
sql_select_from_table_name(const struct Select *select, int i);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

struct info_handler;
void
sql_debug_info(struct info_handler *handler);
#endif
