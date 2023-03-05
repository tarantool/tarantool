#ifndef TARANTOOL_BOX_SQL_PARSE_DEF_H_INCLUDED
#define TARANTOOL_BOX_SQL_PARSE_DEF_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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
#include <assert.h>

#include "box/key_def.h"
#include "box/sql.h"

/**
 * This file contains auxiliary structures and functions which
 * are used only during parsing routine (see parse.y).
 * Their main purpose is to assemble common parts of altered
 * entities (such as name, or IF EXISTS clause) and pass them
 * as a one object to further functions.
 *
 * Hierarchy is following:
 *
 * Base structure is ALTER.
 * ALTER is omitted only for CREATE TABLE since table is filled
 * with meta-information just-in-time of parsing:
 * for instance, as soon as field's name and type are recognized
 * they are added to space definition.
 *
 * DROP is general for all existing objects and includes
 * name of object itself, name of parent object (table),
 * IF EXISTS clause and may contain on-drop behaviour
 * (CASCADE/RESTRICT, but now it is always RESTRICT).
 * Hence, it terms of grammar - it is a terminal symbol.
 *
 * RENAME can be applied only to table (at least now, since it is
 * ANSI extension), so it is also terminal symbol.
 *
 * CREATE in turn can be expanded to nonterminal symbol
 * CREATE CONSTRAINT or to terminal CREATE TABLE/INDEX/TRIGGER.
 * CREATE CONSTRAINT unfolds to FOREIGN KEY or UNIQUE/PRIMARY KEY.
 *
 * For instance:
 * ALTER TABLE t ADD CONSTRAINT c FOREIGN KEY REFERENCES t2(id);
 * ALTER *TABLE* -> CREATE ENTITY -> CREATE CONSTRAINT -> CREATE FK
 *
 * CREATE TRIGGER tr1 ...
 * ALTER *TABLE* -> CREATE ENTITY -> CREATE TRIGGER
 *
 * All terminal symbols are stored as a union within
 * parsing context (struct Parse).
 */

/** Type of parsed statement. */
enum parse_type {
	/** Type of the statement is unknown. */
	PARSE_TYPE_UNKNOWN = 0,
	/** START TRANSACTION statement. */
	PARSE_TYPE_START_TRANSACTION,
	/** COMMIT statement. */
	PARSE_TYPE_COMMIT,
	/** ROLLBACK statement. */
	PARSE_TYPE_ROLLBACK,
	/** SAVEPOINT statement. */
	PARSE_TYPE_SAVEPOINT,
	/** RELEASE SAVEPOINT statement. */
	PARSE_TYPE_RELEASE_SAVEPOINT,
	/** ROLLBACK TO SAVEPOINT statement. */
	PARSE_TYPE_ROLLBACK_TO_SAVEPOINT,
	/** CREATE TABLE statement. */
	PARSE_TYPE_CREATE_TABLE,
	/** CREATE INDEX statement. */
	PARSE_TYPE_CREATE_INDEX,
	/** CREATE VIEW statement. */
	PARSE_TYPE_CREATE_VIEW,
	/** CREATE TRIGGER statement. */
	PARSE_TYPE_CREATE_TRIGGER,
	/** ALTER TABLE ADD COLUMN statement. */
	PARSE_TYPE_ADD_COLUMN,
	/** ALTER TABLE ADD CONSTAINT FOREIGN KEY statement. */
	PARSE_TYPE_ADD_FOREIGN_KEY,
	/** ALTER TABLE ADD CONSTAINT CHECK statement. */
	PARSE_TYPE_ADD_CHECK,
	/** ALTER TABLE ADD CONSTAINT UNIQUE statement. */
	PARSE_TYPE_ADD_UNIQUE,
	/** ALTER TABLE ADD CONSTAINT PRIMARY KEY statement. */
	PARSE_TYPE_ADD_PRIMARY_KEY,
	/** ALTER TABLE RENAME statement. */
	PARSE_TYPE_RENAME_TABLE,
	/** ALTER TABLE RENAME statement. */
	PARSE_TYPE_DROP_CONSTRAINT,
	/** DROP INDEX statement. */
	PARSE_TYPE_DROP_INDEX,
	/** DROP VIEW statement. */
	PARSE_TYPE_DROP_VIEW,
	/** DROP TABLE statement. */
	PARSE_TYPE_DROP_TABLE,
	/** DROP TRIGGER statement. */
	PARSE_TYPE_DROP_TRIGGER,
};

/**
 * Each token coming out of the lexer is an instance of
 * this structure. Tokens are also used as part of an expression.
 */
struct Token {
	/** Text of the token. Not NULL-terminated! */
	const char *z;
	/** Number of characters in this token. */
	unsigned int n;
	bool isReserved;
};

/** Description of a SAVEPOINT. */
struct sql_parse_savepoint {
	/** Name of the SAVEPOINT. */
	struct Token name;
};

/**
 * An instance of this structure is used by the parser to record both the parse
 * tree for an expression and the span of input text for an expression.
 */
struct ExprSpan {
	/* The expression parse tree. */
	struct Expr *pExpr;
	/* First character of input text. */
	const char *zStart;
	/* One character past the end of input text. */
	const char *zEnd;
};

/** Description of the FOREIGN KEY constraint being created. */
struct sql_parse_foreign_key {
	/** List child columns. */
	struct ExprList *child_cols;
	/** List parent columns. */
	struct ExprList *parent_cols;
	/** Name of the parent table. */
	struct Token parent_name;
	/** Constraint name. */
	struct Token name;
	/**
	 * Flag indicating whether the constraint is a column constraint or a
	 * table constraint.
	 */
	bool is_column_constraint;
};

/** FOREIGN KEY descriptions list. */
struct sql_parse_foreign_key_list {
	/** Array containing all FOREIGN KEY descriptions from the list. */
	struct sql_parse_foreign_key *a;
	/** Number of FOREIGN KEY descriptions in the list. */
	uint32_t n;
};

/** Description of the CHECK constraint being created. */
struct sql_parse_check {
	/** Expression. */
	struct ExprSpan expr;
	/** Constraint name. */
	struct Token name;
	/** Column name for column constraint, empty for table constraint. */
	struct Token column_name;
};

/** CHECK descriptions list. */
struct sql_parse_check_list {
	/** Array containing all CHECK descriptions from the list. */
	struct sql_parse_check *a;
	/** Number of CHECK descriptions in the list. */
	uint32_t n;
};

/** Description of the UNIQUE constraint being created. */
struct sql_parse_unique {
	/** Constraint name. */
	struct Token name;
	/** Unique columns. */
	struct ExprList *cols;
};

/** UNIQUE descriptions list. */
struct sql_parse_unique_list {
	/** Array containing all UNIQUE descriptions from the list. */
	struct sql_parse_unique *a;
	/** Number of UNIQUE descriptions in the list. */
	uint32_t n;
};

/** Description of the CREATE INDEX statement. */
struct sql_parse_index {
	/** Index name. */
	struct Token name;
	/** Index columns. */
	struct ExprList *cols;
	/** Flag to show if the index is unique. */
	bool is_unique;
	/**
	 * Flag indicating whether to throw an error if an index with the same
	 * name exists.
	 */
	bool if_not_exists;
};

/** Description of the column being created. */
struct sql_parse_column {
	/** Column name. */
	struct Token name;
	/** Collation name. */
	struct Token collate_name;
	/** Expression for DEFAULT. */
	struct ExprSpan default_expr;
	/** Column data type. */
	enum field_type type;
	/** NULL and NOT NULL constraints. */
	enum on_conflict_action action;
	/** Flag to show if nullable action is set. */
	bool is_action_set;
};

/** Column descriptions list. */
struct sql_parse_column_list {
	/** Array containing all column descriptions from the list. */
	struct sql_parse_column *a;
	/** Number of column descriptions in the list. */
	uint32_t n;
};

/** Description of the table being created. */
struct sql_parse_table {
	/** Table name. */
	struct Token name;
	/** Table engine name. */
	struct Token engine_name;
	/** IF NOT EXISTS flag. */
	bool if_not_exists;
};

/** Description of the view being created. */
struct sql_parse_view {
	/** View name. */
	struct Token name;
	/** The query that defines the view. */
	struct Token str;
	/** List of aliases. */
	struct ExprList *aliases;
	/** Select of the view. */
	struct Select *select;
	/** IF NOT EXISTS flag. */
	bool if_not_exists;
};

/** Description of the trigger being created. */
struct sql_parse_trigger {
	/** Trigger name. */
	struct Token name;
	/** The query that defines the trigger. */
	struct Token all;
	/** One of TRIGGER_BEFORE, TRIGGER_AFTER. */
	int time;
	/** One of TK_DELETE, TK_UPDATE, TK_INSERT. */
	int op;
	/** The <column-list> of UPDATE OF trigger. */
	struct IdList *cols;
	/** The WHEN clause of the expression. */
	struct Expr *when;
	/** Trigger program step. */
	struct TriggerStep *step;
	/** IF NOT EXISTS flag. */
	bool if_not_exists;
};

/**
 * Description of the object to drop from ALTER TABLE DROP CONSTRAINT,
 * DROP INDEX, DROP VIEW, DROP TABLE or DROP TRIGGER statement.
 */
struct sql_parse_drop {
	/** Drop object name. */
	struct Token name;
	/** IF EXISTS flag. */
	bool if_exists;
};

/** Constant tokens for integer values. */
extern const struct Token sqlIntTokens[];

/** Generate a Token object from a string. */
void
sqlTokenInit(struct Token *p, char *z);

#define Token_nil ((struct Token) {NULL, 0, false})

/**
 * Structure representing foreign keys constraints appeared
 * within CREATE TABLE statement. Used only during parsing.
 */
struct fk_constraint_parse {
	/**
	 * Foreign keys constraint declared in <CREATE TABLE ...>
	 * statement. They must be coded after space creation.
	 */
	struct fk_constraint_def *fk_def;
	/**
	 * If inside <CREATE TABLE> or <ALTER TABLE ADD COLUMN>
	 * statement we want to declare self-referenced FK
	 * constraint, we must delay their resolution until the
	 * end of parsing of all columns.
	 * E.g.: CREATE TABLE t1(id REFERENCES t1(b), b);
	 */
	struct ExprList *selfref_cols;
	/**
	 * Still, self-referenced columns might be NULL, if
	 * we declare FK constraints referencing PK:
	 * CREATE TABLE t1(id REFERENCES t1) - it is a valid case.
	 */
	bool is_self_referenced;
	/** Organize these structs into linked list. */
	struct rlist link;
};

/**
 * Structure representing check constraint appeared within
 * CREATE TABLE statement. Used only during parsing.
 * All allocations are performed on region, so no cleanups are
 * required.
 */
struct ck_constraint_parse {
	/**
	 * Check constraint declared in <CREATE TABLE ...>
	 * statement. Must be coded after space creation.
	 */
	struct ck_constraint_def *ck_def;
	/** Organize these structs into linked list. */
	struct rlist link;
};

/**
 * Possible SQL index types. Note that PK and UNIQUE constraints
 * are implemented as indexes and have their own types:
 * _CONSTRAINT_PK and _CONSTRAINT_UNIQUE.
 */
enum sql_index_type {
	SQL_INDEX_TYPE_NON_UNIQUE = 0,
	SQL_INDEX_TYPE_UNIQUE,
	SQL_INDEX_TYPE_CONSTRAINT_UNIQUE,
	SQL_INDEX_TYPE_CONSTRAINT_PK,
	sql_index_type_MAX
};

struct create_ck_constraint_parse_def {
	/** List of ck_constraint_parse_def objects. */
	struct rlist checks;
};

struct create_fk_constraint_parse_def {
	/** List of fk_constraint_parse_def objects. */
	struct rlist fkeys;
	/**
	 * True if a list of foreign keys is used and should be cleaned up
	 * properly.
	 */
	bool is_used;
};

static inline void
create_ck_constraint_parse_def_init(struct create_ck_constraint_parse_def *def)
{
	rlist_create(&def->checks);
}

static inline void
create_fk_constraint_parse_def_init(struct create_fk_constraint_parse_def *def)
{
	rlist_create(&def->fkeys);
	def->is_used = true;
}

static inline void
create_fk_constraint_parse_def_destroy(struct create_fk_constraint_parse_def *d)
{
	if (!d->is_used)
		return;
	struct fk_constraint_parse *fk;
	rlist_foreach_entry(fk, &d->fkeys, link)
		sql_expr_list_delete(fk->selfref_cols);
}

/** Save parsed START TRANSACTION statement. */
void
sql_parse_transaction_start(struct Parse *parse);

/** Save parsed COMMIT statement. */
void
sql_parse_transaction_commit(struct Parse *parse);

/** Save parsed ROLLBACK statement. */
void
sql_parse_transaction_rollback(struct Parse *parse);

/** Save parsed SAVEPOINT statement. */
void
sql_parse_savepoint_create(struct Parse *parse, const struct Token *name);

/** Save parsed RELEASE SAVEPOINT statement. */
void
sql_parse_savepoint_release(struct Parse *parse, const struct Token *name);

/** Save parsed ROLLBACK TO SAVEPOINT statement. */
void
sql_parse_savepoint_rollback(struct Parse *parse, const struct Token *name);

/** Save parsed CREATE TABLE statement. */
void
sql_parse_create_table(struct Parse *parse, struct Token *name,
		       bool if_not_exists);

/** Save parsed CREATE VIEW statement. */
void
sql_parse_create_view(struct Parse *parse, struct Token *name,
		      struct Token *create_start, struct ExprList *aliases,
		      struct Select *select, bool if_not_exists);

/** Save parsed CREATE INDEX statement. */
void
sql_parse_create_index(struct Parse *parse, struct Token *table_name,
		       const struct Token *index_name, struct ExprList *cols,
		       bool is_unique, bool if_not_exists);

/** Save parsed CREATE TRIGGER statement. */
void
sql_parse_create_trigger(struct Parse *parse, struct SrcList *table_name,
			 struct Token *name, int time, int op,
			 struct IdList *cols, struct Expr *when,
			 struct TriggerStep *step, struct Token *all,
			 bool if_not_exists);

/** Save parsed ADD COLUMN statement. */
void
sql_parse_add_column(struct Parse *parse, struct SrcList *table_name,
		     struct Token *name, enum field_type type);

/** Save parsed column FOREIGN KEY. */
void
sql_parse_column_foreign_key(struct Parse *parse, const struct Token *name,
			     const struct Token *parent_name,
			     struct ExprList *parent_cols);

/** Save parsed table FOREIGN KEY from CREATE TABLE statement. */
void
sql_parse_table_foreign_key(struct Parse *parse, const struct Token *name,
			    struct ExprList *child_cols,
			    const struct Token *parent_name,
			    struct ExprList *parent_cols);

/** Save parsed table FOREIGN KEY from ALTER TABLE ADD CONSTRAINT statement. */
void
sql_parse_add_foreign_key(struct Parse *parse, struct SrcList *src_list,
			  const struct Token *name, struct ExprList *child_cols,
			  const struct Token *parent_name,
			  struct ExprList *parent_cols);

/** Save parsed column CHECK. */
void
sql_parse_column_check(struct Parse *parse, const struct Token *name,
		       struct ExprSpan *expr);

/** Save parsed table CHECK from CREATE TABLE statement. */
void
sql_parse_table_check(struct Parse *parse, const struct Token *name,
		      struct ExprSpan *expr);

/** Save parsed table CHECK from ALTER TABLE ADD CONSTRAINT statement. */
void
sql_parse_add_check(struct Parse *parse, struct SrcList *table_name,
		    const struct Token *name, struct ExprSpan *expr);

/** Save parsed column UNIQUE. */
void
sql_parse_column_unique(struct Parse *parse, const struct Token *name);

/** Save parsed table UNIQUE from CREATE TABLE statement. */
void
sql_parse_table_unique(struct Parse *parse, const struct Token *name,
		       struct ExprList *cols);

/** Save parsed table UNIQUE from ALTER TABLE ADD CONSTRAINT statement. */
void
sql_parse_add_unique(struct Parse *parse, struct SrcList *table_name,
		     const struct Token *name, struct ExprList *cols);

/** Save parsed column PRIMARY KEY. */
void
sql_parse_column_primary_key(struct Parse *parse, const struct Token *name,
			     enum sort_order sort_order);

/** Save parsed table PRIMARY KEY from CREATE TABLE statement. */
void
sql_parse_table_primary_key(struct Parse *parse, const struct Token *name,
			    struct ExprList *cols);

/** Save parsed table PRIMARY KEY from ALTER TABLE ADD CONSTRAINT statement. */
void
sql_parse_add_primary_key(struct Parse *parse, struct SrcList *table_name,
			  const struct Token *name, struct ExprList *cols);

/** Save parsed column AUTOINCREMENT clause. */
void
sql_parse_column_autoincrement(struct Parse *parse);

/** Save parsed AUTOINCREMENT clause from table PRIMARY KEY clause. */
void
sql_parse_table_autoincrement(struct Parse *parse, struct Expr *column_name);

/** Save parsed column from CREATE TABLE statement. */
void
sql_parse_table_column(struct Parse *parse, struct Token *name,
		       enum field_type type);

/** Save parsed column COLLATE clause. */
void
sql_parse_column_collate(struct Parse *parse, struct Token *collate_name);

/** Save parsed column NULL or NOT NULL constraint. */
void
sql_parse_column_nullable_action(struct Parse *parse, int action,
				 int on_conflict);

/** Save parsed column DEFAULT clause. */
void
sql_parse_column_default(struct Parse *parse, struct ExprSpan *expr);

/** Save parsed table engine clause. */
void
sql_parse_table_engine(struct Parse *parse, struct Token *engine_name);

/** Save parsed ALTER TABLE RENAME statement. */
void
sql_parse_table_rename(struct Parse *parse, struct SrcList *table_name,
		       struct Token *new_name);

/** Save parsed ALTER TABLE DROP CONSTRAINT statement. */
void
sql_parse_drop_constraint(struct Parse *parse, struct SrcList *table_name,
			  struct Token *name);

/** Save parsed DROP INDEX statement. */
void
sql_parse_drop_index(struct Parse *parse, struct SrcList *table_name,
		     struct Token *name, bool if_exists);

/** Save parsed DROP VIEW statement. */
void
sql_parse_drop_view(struct Parse *parse, struct SrcList *table_name,
		    bool if_exists);

/** Save parsed DROP TABLE statement. */
void
sql_parse_drop_table(struct Parse *parse, struct SrcList *table_name,
		     bool if_exists);

/** Save parsed DROP TRIGGER statement. */
void
sql_parse_drop_trigger(struct Parse *parse, struct SrcList *table_name,
		       bool if_exists);

#endif /* TARANTOOL_BOX_SQL_PARSE_DEF_H_INCLUDED */
