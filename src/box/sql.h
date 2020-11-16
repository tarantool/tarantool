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
sql_init(void);

/**
 * Initialize SQL statistic system.
 *
 * Currently unused.
 */
void
sql_load_schema(void);

/**
 * struct sql *
 * sql_get();
 *
 * Currently, this is the only SQL execution interface provided.
 * If not yet initialised, returns NULL.
 * Use the regular sql_* API with this handle, but
 * don't do anything finicky like sql_close.
 * Behind the scenes, this sql was rigged to use Tarantool
 * as a data source.
 * @retval SQL handle.
 */
struct sql *
sql_get(void);

struct Expr;
struct Parse;
struct Select;
struct Table;
struct sql_parsed_ast;
struct sql_trigger;
struct space_def;

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
sql_expr_compile(struct sql *db, const char *expr, int expr_len);

/**
 * This routine executes parser on 'CREATE VIEW ...' statement
 * and loads content of SELECT into internal structs as result.
 *
 * @param db Current SQL context.
 * @param view_stmt String containing 'CREATE VIEW' statement.
 * @retval AST of SELECT statement on success, NULL otherwise.
 */
struct Select *
sql_view_compile(struct sql *db, const char *view_stmt);

/**
 * Perform parsing of provided SQL request and construct trigger AST.
 * @param db SQL context handle.
 * @param sql request to parse.
 *
 * @retval NULL on error
 * @retval not NULL sql_trigger AST pointer on success.
 */
struct sql_trigger *
sql_trigger_compile(struct sql *db, const char *sql);

/**
 * Free AST pointed by trigger.
 * @param db SQL handle.
 * @param trigger AST object.
 */
void
sql_trigger_delete(struct sql *db, struct sql_trigger *trigger);

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
 * This function is similar to sqlExprDup(), except that if pzBuffer
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
sql_expr_dup(struct sql *db, struct Expr *p, int flags, char **buffer);

/**
 * Free AST pointed by expr.
 * @param db SQL handle.
 * @param expr Root pointer of ASR
 * @param extern_alloc True if skeleton was allocated externally.
 */
void
sql_expr_delete(struct sql *db, struct Expr *expr, bool extern_alloc);

/**
 * Create and initialize a new ephemeral space object.
 * @param parser SQL Parser object.
 * @param name Name of space to be created.
 * @retval NULL on memory allocation error, Parser state changed.
 * @retval not NULL on success.
 */
struct space *
sql_ephemeral_space_new(struct Parse *parser, const char *name);

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
sql_expr_list_dup(struct sql *db, struct ExprList *p, int flags);

/**
 * Free AST pointed by expr list.
 * @param db SQL handle.
 * @param expr_list Root pointer of ExprList.
 */
void
sql_expr_list_delete(struct sql *db, struct ExprList *expr_list);

/**
 * Add a new element to the end of an expression list.
 * @param db SQL handle.
 * @param expr_list List to which to append. Might be NULL.
 * @param expr Expression to be appended. Might be NULL.
 * @retval NULL on memory allocation error. The list is freed.
 * @retval not NULL on success.
 */
struct ExprList *
sql_expr_list_append(struct sql *db, struct ExprList *expr_list,
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
 * @param def The definition of space being referenced.
 * @param type NC_IsCheck or NC_IdxExpr.
 * @param expr Expression to resolve.  May be NULL.
 */
void
sql_resolve_self_reference(struct Parse *parser, struct space_def *def,
			   int type, struct Expr *expr);

/**
 * Initialize a new parser object.
 * A number of service allocations are performed on the region,
 * which is also cleared in the destroy function.
 * @param parser object to initialize.
 * @param db sql object.
 * @param sql_flags flags to control parser behaviour.
 */
void
sql_parser_create(struct Parse *parser, struct sql *db, uint32_t sql_flags);

/**
 * Release the parser object resources.
 * @param parser object to release.
 */
void
sql_parser_destroy(struct Parse *parser);

/**
 * Release allocated AST data structures. Usually called
 * as part of @ref sql_parser_destroy call flow.
 * @param db
 * @param ast AST data structures to release
 */
void
sql_parsed_ast_destroy(struct sql *db, struct sql_parsed_ast *ast);

/**
 * Release memory allocated for given SELECT and all of its
 * substructures. It accepts NULL pointers.
 *
 * @param db Database handler.
 * @param select Select to be freed.
 */
void
sql_select_delete(struct sql *db, struct Select *select);

/**
 * Collect all table names within given select, including
 * ones from sub-queries, expressions, FROM clause etc.
 * Return those names as a list.
 *
 * @param select Select to be investigated.
 * @retval List containing all involved table names.
 *         NULL in case of OOM.
 */
struct SrcList *
sql_select_expand_from_tables(struct Select *select);

/**
 * Check if @a name matches with at least one of CTE names typed
 * in <WITH> clauses within @a select except <WITH>s that are
 * nested within other <WITH>s.
 *
 * @param select Select which may contain CTE.
 * @param name The name of CTE, that may contained.
 * @retval true Has CTE with @a name.
 * @retval false Hasn't CTE with @a name.
*/
bool
sql_select_constains_cte(struct Select *select, const char *name);

/**
 * Temporary getter in order to avoid including sqlInt.h
 * in alter.cc.
 *
 * @param list List to be examined.
 * @retval Count of 'FROM' tables in given select.
 */
int
sql_src_list_entry_count(const struct SrcList *list);

/**
 * Temporary getter in order to avoid including sqlInt.h
 * in alter.cc.
 *
 * @param list List to be examined.
 * @param i Ordinal number of entry.
 * @retval Name of i-th entry.
 */
const char *
sql_src_list_entry_name(const struct SrcList *list, int i);

/** Delete an entire SrcList including all its substructure. */
void
sqlSrcListDelete(struct sql *db, struct SrcList *list);

/**
 * Auxiliary VDBE structure to speed-up tuple data field access.
 * A memory allocation that manage this structure must have
 * trailing unused bytes that extends the last 'slots' array.
 * The amount of reserved memory should correspond to the problem
 * to be solved and is usually equal to the greatest number of
 * fields in the tuple.
 *
 * +-------------------------+
 * |  struct vdbe_field_ref  |
 * +-------------------------+
 * |      RESERVED MEMORY    |
 * +-------------------------+
 */
struct vdbe_field_ref {
	/** Tuple pointer or NULL when undefined. */
	struct tuple *tuple;
	/** Tuple data pointer. */
	const char *data;
	/** Tuple data size. */
	uint32_t data_sz;
	/** Count of fields in tuple. */
	uint32_t field_count;
	/**
	 * Bitmask of initialized slots. The fieldno == 0 slot
	 * must be initialized in vdbe_field_ref constructor.
	 * This bitmap allows to lookup for the nearest
	 * initialized slot for a given fieldno to perform as few
	 * extra tuple decoding as possible.
	 */
	uint64_t slot_bitmask;
	/**
	 * Array of offsets of tuple fields.
	 * Only values <= rightmost_slot are valid.
	 */
	uint32_t slots[1];
};

/**
 * Initialize a new vdbe_field_ref instance with given tuple
 * data.
 * @param field_ref The vdbe_field_ref instance to initialize.
 * @param data The tuple data.
 * @param data_sz The size of tuple data.
 */
void
vdbe_field_ref_prepare_data(struct vdbe_field_ref *field_ref, const char *data,
			    uint32_t data_sz);

/**
 * Initialize a new vdbe_field_ref instance with given tuple
 * data.
 * @param field_ref The vdbe_field_ref instance to initialize.
 * @param tuple The tuple object pointer.
 */
void
vdbe_field_ref_prepare_tuple(struct vdbe_field_ref *field_ref,
			     struct tuple *tuple);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

struct info_handler;
void
sql_debug_info(struct info_handler *handler);
#endif
