/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdint.h>

#include "parse_def.h"
#include "salad/stailq.h"
#include "small/rlist.h"

/** Type of parsed statement. */
enum sql_ast_type {
	/** Type of the statement is unknown. */
	SQL_AST_UNKNOWN = 0,

	/** START TRANSACTION statement. */
	SQL_AST_TX_START,
	/** COMMIT statement. */
	SQL_AST_TX_COMMIT,
	/** ROLLBACK statement. */
	SQL_AST_TX_ROLLBACK,
	/** SAVEPOINT statement. */
	SQL_AST_TX_SAVEPOINT_NEW,
	/** RELEASE SAVEPOINT statement. */
	SQL_AST_TX_SAVEPOINT_RELEASE,
	/** ROLLBACK TO SAVEPOINT statement. */
	SQL_AST_TX_SAVEPOINT_ROLLBACK,

	/** SELECT statement. */
	SQL_AST_SELECT,
	/** INSERT statement. */
	SQL_AST_INSERT,
	/** UPDATE statement. */
	SQL_AST_UPDATE,
	/** DELETE statement. */
	SQL_AST_DELETE,
	/** TRUNCATE statement. */
	SQL_AST_TRUNCATE,

	/** CREATE TABLE statement. */
	SQL_AST_CREATE_TABLE,
	/** CREATE VIEW statement. */
	SQL_AST_CREATE_VIEW,
	/** CREATE INDEX statement. */
	SQL_AST_CREATE_INDEX,

	/** DROP TABLE statement. */
	SQL_AST_DROP_TABLE,
	/** DROP VIEW statement. */
	SQL_AST_DROP_VIEW,
	/** DROP INDEX statement. */
	SQL_AST_DROP_INDEX,
	/** DROP TRIGGER statement. */
	SQL_AST_DROP_TRIGGER,

	/** ALTER TABLE RENAME statement. */
	SQL_AST_ALTER_RENAME,
	/** ALTER TABLE ADD COLUMN statement. */
	SQL_AST_ALTER_ADD_COLUMN,
	/** ALTER TABLE ADD CONSTRAINT statement. */
	SQL_AST_ALTER_ADD_CONSTRAINT,
	/** ALTER TABLE DROP CONSTRAINT statement. */
	SQL_AST_ALTER_DROP_CONSTRAINT,

	/** VIEW object definition. */
	SQL_AST_VIEW,
};

/** Columns and table properties. */
enum ast_property_type {
	/** Property type not set. */
	SQL_AST_PROPERTY_ANY = 0,
	/** Property is CHECK constraint. */
	SQL_AST_PROPERTY_CHECK,
	/** Property is UNIQUE constraint. */
	SQL_AST_PROPERTY_UNIQUE,
	/** Property is PRIMARY KEY constraint. */
	SQL_AST_PROPERTY_PRIMARY_KEY,
	/** Property is FOREIGN KEY constraint. */
	SQL_AST_PROPERTY_FOREIGN_KEY,
	/** Property is column collation. */
	SQL_AST_PROPERTY_COLLATE,
	/** Property is column default value or function. */
	SQL_AST_PROPERTY_DEFAULT,
	/** Property is column NOT NULL constraint. */
	SQL_AST_PROPERTY_NOT_NULL,
	/** Property is column nullability flag. */
	SQL_AST_PROPERTY_NULL,
};

/** List of IDs received from parser. */
struct ast_id_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** Element of the IDs list. */
struct ast_id_entry {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** ID from parser. */
	struct Token id;
};

/** List of sources received from parser. */
struct ast_source_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** Element of the sources list, describing one entry of a FROM clause. */
struct ast_source {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** Name of the table. Not set for subqueries. */
	struct Token name;
	/** Alias of the source, if any. */
	struct Token alias;
	/** Name of the index specified via INDEXED BY, if any. */
	struct Token indexed_by;
	/** SELECT statement of a subquery. */
	struct ast_select *select;
	/** Expression of a join's ON clause. */
	struct ast_expr *join_on;
	/** Column names of a join's USING clause. */
	struct ast_id_list *join_using;
	/** Arguments of a table-valued function. */
	struct ast_expr_list *func_args;
	/** Type of join between this source and the previous one. */
	int join_type;
	/** True if the source is a table-valued function. */
	bool is_tab_func;
	/** True if scanning is not allowed for this source. */
	bool disallow_scan;
};

/** Structure that describes SELECT. */
struct ast_select {
	/** Link to other SELECTs. */
	struct rlist link;
	/** The FROM clause of the SELECT. */
	struct ast_source_list *sources;
	/** Resulting expressions of the SELECT. */
	struct ast_expr_list *columns;
	/** GROUP BY clause of the SELECT. */
	struct ast_expr_list *group_by;
	/** ORDER BY clause of the SELECT. */
	struct ast_expr_list *order_by;
	/** WHERE clause of the SELECT. */
	struct ast_expr *where;
	/** HAVING clause of the SELECT. */
	struct ast_expr *having;
	/** LIMIT clause of the SELECT. */
	struct ast_expr *limit;
	/** OFFSET clause of the SELECT. */
	struct ast_expr *offset;
	/** WITH clause of the SELECT. */
	struct ast_with_list *with;
	/** Flags of the SELECT. */
	uint32_t flags;
	/** Link type between linked SELECTs. */
	uint8_t op;
};

/** List of WITH clauses received from parser. */
struct ast_with_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** Element of the WITH clause list, describing one entry of a WITH clause. */
struct ast_with_entry {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** Name of the table in the WITH clause. */
	struct Token name;
	/** Column names of the table, if specified explicitly. */
	struct ast_id_list *columns;
	/** SELECT statement of the WITH clause. */
	struct ast_select *select;
};

/** Description of list of expressions. */
struct ast_expr_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
	/** True if this is the column list of a SELECT statement. */
	bool is_select_list;
};

/** Description of a parsed expression. */
struct ast_expr {
	/** Left (or the only) operand of the expression. */
	struct ast_expr *left;
	union {
		/** Right operand of a binary expression. */
		struct ast_expr *right;
		/** Sub-expressions, e.g. function args or an IN list. */
		struct ast_expr_list *list;
		/** Subquery of an EXISTS, SELECT, or IN expression. */
		struct ast_select *select;
		/** Target type of a CAST expression. */
		enum field_type type;
		/** Conflict resolution action of a RAISE expression. */
		enum on_conflict_action on_conflict_action;
	};
	/** Pointer to the token text this expression is built from. */
	const char *str;
	/** Length of the token text pointed to by str. */
	uint32_t len;
	/** Parser token code identifying the kind of expression. */
	uint8_t op;
};

/** Element of the expressions list. */
struct ast_expr_list_entry {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/**
	 * Name linked to the expression, if any.
	 * This name can be an alias, a column name in the SET clause of
	 * an UPDATE statement, etc.
	 */
	struct Token name;
	/** The expression itself. */
	struct ast_expr *expr;
	/** Sort order of the entry, used for ORDER BY lists. */
	enum sort_order order;
	/** AUTOINCREMENT feature indicator for primary key columns. */
	bool autoinc;
};

/** Structure that describes INSERT. */
struct ast_insert {
	/** Name of table in INSERT statement. */
	struct Token table;
	/** SELECT that describes data that are inserted. */
	struct ast_select *select;
	/** Column to where data is inserted. */
	struct ast_id_list *columns;
	/** Action on conflict. */
	enum on_conflict_action action;
	/** WITH clause of the INSERT. */
	struct ast_with_list *with;
};

/** The list of names and expressions from SET clause of UPDATE statement. */
struct ast_set_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** UPDATE SET clause single entry. */
struct ast_set_list_entry {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** Column name. */
	struct Token name;
	/** List of names. */
	struct ast_id_list *ids;
	/** New value expression. */
	struct ast_expr *expr;
};

/** Structure that describes UPDATE. */
struct ast_update {
	/** Name of updated table. */
	struct Token table;
	/** Name of index in INDEXED BY clause. */
	struct Token indexed_by;
	/** UPDATE SET clause. */
	struct ast_set_list *set_list;
	/** WHERE clause. */
	struct ast_expr *where;
	/** Action on conflict. */
	enum on_conflict_action action;
	/** WITH clause of the UPDATE. */
	struct ast_with_list *with;
};

/** Structure that describes DELETE. */
struct ast_delete {
	/** Name of the table from which data is being deleted. */
	struct Token table;
	/** Name of index in INDEXED BY clause. */
	struct Token indexed_by;
	/** WHERE clause. */
	struct ast_expr *where;
	/** WITH clause of the DELETE. */
	struct ast_with_list *with;
};

/** Structure that describes TRUNCATE. */
struct ast_truncate {
	/** Name of table to truncate. */
	struct Token table;
};

/** List of table or columns properties received from parser. */
struct ast_property_list {
	/** Head of the list. */
	struct stailq head;
	/** Length of the list. */
	uint32_t len;
};

/** Description of FOREIGN KEY constraint. */
struct ast_foreign_key {
	/** Foreign table of FOREIGN KEY constraint. */
	struct Token foreign_table;
	/** Foreign columns of FOREIGN KEY constraint. */
	struct ast_id_list *foreign_columns;
	/** Local columns of FOREIGN KEY constraint. */
	struct ast_id_list *columns;
};

/** Description of table and column properties. */
struct ast_property {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** Property name. */
	struct Token name;
	union {
		/** Expression for DEFAULT property and CHECK constraint. */
		struct ast_expr *expr;
		/** Column list for PRIMARY KEY and UNIQUE table constraint. */
		struct ast_expr_list *columns;
		/** Description of FOREIGN KEY constraint. */
		struct ast_foreign_key foreign_key;
		/** Name of column collate for COLLATE property. */
		struct Token collate;
		/** Column order for column PRIMARY KEY property. */
		enum sort_order order;
		/** Action for NULL property and NOT NULL constraint. */
		enum on_conflict_action action;
	};
	/** Property type. */
	enum ast_property_type type;
};

/** Description of a column. */
struct ast_column {
	/** Link to the next element of the list. */
	struct stailq_entry link;
	/** Column name. */
	struct Token name;
	/** Column properties. */
	struct ast_property_list *properties;
	/** Column field type. */
	enum field_type type;
	/** Flag that shows if column is autoincremented. */
	bool is_autoinc;
};

/** Description of table properties. */
struct ast_table_properties {
	/** Head of the list of columns. */
	struct stailq columns;
	/** Head of the list of constraints. */
	struct stailq constraints;
};

/** Description of CREATE TABLE statement. */
struct ast_create_table {
	/** Name of new table. */
	struct Token name;
	/** Engine of new table. */
	struct Token engine;
	/** Columns and constraints of new table. */
	struct ast_table_properties *properties;
	/** Flag to throw an error if table exists. */
	bool if_not_exists;
};

/** Description of CREATE VIEW statement. */
struct ast_create_view {
	/** Name of new view. */
	struct Token name;
	/** Column names of the view. */
	struct ast_id_list *columns;
	/** SELECT that view represents. */
	struct ast_select *select;
	/** Flag to throw an error if view exists. */
	bool if_not_exists;
};

/** Description of CREATE INDEX statement. */
struct ast_create_index {
	/** New index name. */
	struct Token name;
	/** Name of table where index is created. */
	struct Token table;
	/** Columns that are parts of the index. */
	struct ast_expr_list *columns;
	/** Flag to throw an error if index exists. */
	bool if_not_exists;
	/** Flag to show if index is unique. */
	bool is_unique;
};

/** Description of DROP TABLE and DROP VIEW statements. */
struct ast_drop_table {
	/** Table or view name. */
	struct Token name;
	/** Flag to throw an error if table not exists. */
	bool if_exists;
};

/** Description of DROP INDEX statement. */
struct ast_drop_index {
	/** Index name. */
	struct Token name;
	/** Table name. */
	struct Token table;
	/** Flag to throw an error if index not exists. */
	bool if_exists;
};

/** Description of DROP TRIGGER statement. */
struct ast_drop_trigger {
	/** Trigger name. */
	struct Token name;
	/** Flag to throw an error if trigger not exists. */
	bool if_exists;
};

/** Description of ALTER TABLE RENAME statement. */
struct ast_alter_rename {
	/** Old name of the table. */
	struct Token old_name;
	/** New name of the table. */
	struct Token new_name;
};

/** Description of ALTER TABLE DROP CONSTRAINT statement. */
struct ast_alter_drop_constraint {
	/** Name of the constraint. */
	struct Token name;
	/** Constraint column name for column constraints. */
	struct Token column;
	/** Name of the table name that contains the constraint. */
	struct Token table;
	/** Type of constraint. */
	enum ast_property_type type;
};

/** Description of ALTER TABLE ADD CONSTRAINT statement. */
struct ast_alter_add_constraint {
	/** Name of table where constraint is created. */
	struct Token table;
	/** Description of the constraint. */
	struct ast_property *con;
};

/** Description of ALTER TABLE ADD COLUMN statement. */
struct ast_alter_add_column {
	/** Name of table where column is created. */
	struct Token table;
	/** Description of the column. */
	struct ast_column *col;
};

/** A structure describing the AST of the parsed SQL statement. */
struct sql_ast {
	/** Parsed statement type. */
	enum sql_ast_type type;
	/** Definition of the statement. */
	union {
		/** Name of the savepoint. */
		struct Token savepoint;
		/** SELECT statement or SELECT of VIEW. */
		struct ast_select *select;
		/** INSERT statement. */
		struct ast_insert *insert;
		/** UPDATE statement. */
		struct ast_update *update;
		/** DELETE statement. */
		struct ast_delete *del;
		/** TRUNCATE statement. */
		struct ast_truncate truncate;
		/** CREATE TABLE statement. */
		struct ast_create_table create_table;
		/** CREATE VIEW statement. */
		struct ast_create_view create_view;
		/** CREATE INDEX statement. */
		struct ast_create_index create_index;
		/** DROP TABLE and DROP VIEW statements. */
		struct ast_drop_table drop_table;
		/** DROP TRIGGER statement. */
		struct ast_drop_trigger drop_trigger;
		/** DROP INDEX statement. */
		struct ast_drop_index drop_index;
		/** ALTER TABLE RENAME statement. */
		struct ast_alter_rename alter_rename;
		/** ALTER TABLE ADD COLUMN statement. */
		struct ast_alter_add_column alter_add_column;
		/** ALTER TABLE ADD CONSTRAINT statement. */
		struct ast_alter_add_constraint alter_add_constraint;
		/** ALTER TABLE DROP CONSTRAINT statement. */
		struct ast_alter_drop_constraint alter_drop_constraint;
	};
};

/** Append an ID to ID list. */
struct ast_id_list *
ast_id_list_append(struct region *region, struct ast_id_list *list,
		   const struct Token *id);

/** Convert `struct ast_id_list` to `struct IdList`. */
struct IdList *
id_list_from_ast(struct ast_id_list *list);

/** Allocate a new, zero-initialized source. */
struct ast_source *
ast_source_new(struct region *region);

/** Append a source to the sources list, creating the list if needed. */
struct ast_source_list *
ast_source_list_append(struct region *region, struct ast_source_list *list,
		       struct ast_source *src);

/**
 * Convert `struct ast_source_list` to `struct SrcList`.
 *
 * Return NULL on error or if `list == NULL`.
 */
struct SrcList *
src_list_from_ast(struct Parse *parser, struct ast_source_list *list);

/** Create new empty SELECT structure. */
struct ast_select *
ast_select_new(struct region *region);

/**
 * Build `struct Select` object from `struct ast_select` object.
 *
 * Return NULL on error or if `select == NULL`.
 */
struct Select *
select_from_ast(struct Parse *parser, struct ast_select *select);

/**
 * Append a WITH clause to the WITH clause list, creating the list if needed.
 */
struct ast_with_list *
ast_with_list_append(struct region *region, struct ast_with_list *list,
		     const struct Token *name, struct ast_id_list *columns,
		     struct ast_select *select);

/**
 * Convert `struct ast_with_list` to `struct With`.
 *
 * Return NULL on error or if `list == NULL`.
 */
struct With *
with_from_ast(struct Parse *parser, struct ast_with_list *list);

/** Allocate a new expression node from a token's text. */
struct ast_expr *
ast_expr_new(struct region *region, const char *start, uint32_t len,
	     uint8_t op);

/** Append an expression to the expressions list, creating it if needed. */
struct ast_expr_list *
ast_expr_list_append(struct region *region, struct ast_expr_list *list,
		     struct ast_expr *expr);

/** Set the name of the last expression appended to the list. */
void
ast_expr_list_set_name(struct ast_expr_list *list, struct Token *name);

/** Set the sort order of the last expression appended to the list. */
void
ast_expr_list_set_order(struct ast_expr_list *list, enum sort_order order);

/** Set the `autoinc` flag of the last expression appended to the list. */
void
ast_expr_list_set_autoinc(struct ast_expr_list *list, bool autoinc);

/**
 * Convert `struct ast_expr` to `struct Expr`.
 *
 * Return NULL on error or if `expr == NULL`.
 */
struct Expr *
expr_from_ast(struct Parse *parser, struct ast_expr *expr);

/**
 * Convert `struct ast_expr_list` to `struct ExprList`.
 *
 * Return NULL on error or if `list == NULL`.
 */
struct ExprList *
expr_list_from_ast(struct Parse *parser, struct ast_expr_list *list);

/**
 * Convert `struct ast_id_list` to `struct ExprList` of column names.
 *
 * Return NULL on error or if `list == NULL`.
 */
struct ExprList *
expr_list_from_ids(struct Parse *parser, struct ast_id_list *list);

/**
 * Convert `struct ast_set_list` to `struct ExprList`.
 *
 * Return NULL on error.
 */
struct ExprList *
expr_list_from_set_list(struct Parse *parser, struct ast_set_list *list);

/** Create new empty INSERT structure. */
struct ast_insert *
ast_insert_new(struct region *region);

/**
 * Append a SET expression to the SET expressions list, creating it if needed.
 */
struct ast_set_list *
ast_set_list_append_expr(struct region *region, struct ast_set_list *list,
			 struct Token *name, struct ast_expr *expr);

/**
 * Append a SET vector expression to the SET expressions list,
 * creating it if needed.
 */
struct ast_set_list *
ast_set_list_append_vector(struct region *region, struct ast_set_list *list,
			   struct ast_id_list *ids, struct ast_expr *expr);

/** Create new empty UPDATE structure. */
struct ast_update *
ast_update_new(struct region *region);

/** Create new empty DELETE structure. */
struct ast_delete *
ast_delete_new(struct region *region);

/** Create new empty structure of table or column property. */
struct ast_property *
ast_property_new(struct region *region);

/** Append a property to property list. */
struct ast_property_list *
ast_property_list_append(struct region *region, struct ast_property_list *list,
			 struct ast_property *property);

/** Create new empty structure of column. */
struct ast_column *
ast_column_new(struct region *region);

/** Create new empty structure of table properties. */
struct ast_table_properties *
ast_table_properties_new(struct region *region);

/** Append a column to table properties. */
struct ast_table_properties *
ast_table_properties_append_column(struct ast_table_properties *properties,
				   struct ast_column *column);

/** Append a constraint to table properties. */
struct ast_table_properties *
ast_table_properties_append_constraint(struct ast_table_properties *properties,
				       struct ast_property *constraint);
