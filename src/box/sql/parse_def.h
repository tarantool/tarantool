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

#include "box/fk_constraint.h"
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

enum entity_type {
	ENTITY_TYPE_TABLE = 0,
	ENTITY_TYPE_COLUMN,
	ENTITY_TYPE_VIEW,
	ENTITY_TYPE_INDEX,
	ENTITY_TYPE_TRIGGER,
	ENTITY_TYPE_CK,
	ENTITY_TYPE_FK,
	/**
	 * For assertion checks that constraint definition is
	 * created before initialization of a term constraint.
	 */
	ENTITY_TYPE_CONSTRAINT,
};

enum alter_action {
	ALTER_ACTION_CREATE = 0,
	ALTER_ACTION_DROP,
	ALTER_ACTION_RENAME,
	ALTER_ACTION_ENABLE,
};

struct alter_entity_def {
	/** Type of topmost entity. */
	enum entity_type entity_type;
	/** Action to be performed using current entity. */
	enum alter_action alter_action;
	/** As a rule it is a name of table to be altered. */
	struct SrcList *entity_name;
};

struct enable_entity_def {
	struct alter_entity_def base;
	/** Name of constraint to be enabled/disabled. */
	struct Token name;
	/** A new state to be set for entity found. */
	bool is_enabled;
};

struct rename_entity_def {
	struct alter_entity_def base;
	struct Token new_name;
};

struct create_entity_def {
	struct alter_entity_def base;
	struct Token name;
	/** Statement comes with IF NOT EXISTS clause. */
	bool if_not_exist;
};

struct create_table_def {
	struct create_entity_def base;
	struct space *new_space;
};

struct create_column_def {
	struct create_entity_def base;
	/** Shallow space copy. */
	struct space *space;
	/** Column type. */
	struct type_def *type_def;
};

struct create_ck_constraint_parse_def {
	/** List of ck_constraint_parse_def objects. */
	struct rlist checks;
	/** Count of ck_constraint_parse_def objects. */
	uint32_t count;
};

struct create_fk_constraint_parse_def {
	/** List of fk_constraint_parse_def objects. */
	struct rlist fkeys;
	/** Count of fk_constraint_parse_def objects. */
	uint32_t count;
};

struct create_view_def {
	struct create_entity_def base;
	/**
	 * Starting position of CREATE VIEW ... statement.
	 * It is used to fetch whole statement, which is
	 * saved as raw string to space options.
	 */
	struct Token *create_start;
	/** List of column aliases (SELECT x AS y ...). */
	struct ExprList *aliases;
	struct Select *select;
};

struct drop_entity_def {
	struct alter_entity_def base;
	/** Name of index/trigger/constraint to be dropped. */
	struct Token name;
	/** Statement comes with IF EXISTS clause. */
	bool if_exist;
};

/**
 * Identical wrappers around drop_entity_def to make hierarchy of
 * structures be consistent. Arguments for drop procedures are
 * the same.
 */
struct drop_table_def {
	struct drop_entity_def base;
};

struct drop_view_def {
	struct drop_entity_def base;
};

struct drop_trigger_def {
	struct drop_entity_def base;
};

struct drop_constraint_def {
	struct drop_entity_def base;
};

struct drop_index_def {
	struct drop_entity_def base;
};

struct create_trigger_def {
	struct create_entity_def base;
	/** One of TK_BEFORE, TK_AFTER, TK_INSTEAD. */
	int tr_tm;
	/** One of TK_INSERT, TK_UPDATE, TK_DELETE. */
	int op;
	/** Column list if this is an UPDATE trigger. */
	struct IdList *cols;
	/** When clause. */
	struct Expr *when;
};

struct create_constraint_def {
	struct create_entity_def base;
	/** One of DEFERRED, IMMEDIATE. */
	bool is_deferred;
};

struct create_ck_def {
	struct create_constraint_def base;
	/** AST representing check expression. */
	struct ExprSpan *expr;
};

struct create_fk_def {
	struct create_constraint_def base;
	struct ExprList *child_cols;
	struct Token *parent_name;
	struct ExprList *parent_cols;
	/** Encoded actions for ON DELETE and ON UPDATE clauses. */
	int actions;
	int match;
};

struct create_index_def {
	struct create_constraint_def base;
	/** List of indexed columns. */
	struct ExprList *cols;
	/** One of _PRIMARY_KEY, _UNIQUE, _NON_UNIQUE. */
	enum sql_index_type idx_type;
	enum sort_order sort_order;
};

/** Basic initialisers of parse structures.*/
static inline void
alter_entity_def_init(struct alter_entity_def *alter_def,
		      struct SrcList *entity_name, enum entity_type type,
		      enum alter_action action)
{
	alter_def->entity_name = entity_name;
	alter_def->entity_type = type;
	alter_def->alter_action = action;
}

static inline void
rename_entity_def_init(struct rename_entity_def *rename_def,
		       struct SrcList *table_name, struct Token *new_name)
{
	alter_entity_def_init(&rename_def->base, table_name, ENTITY_TYPE_TABLE,
			      ALTER_ACTION_RENAME);
	rename_def->new_name = *new_name;
}

static inline void
enable_entity_def_init(struct enable_entity_def *enable_def,
		       enum entity_type type, struct SrcList *parent_name,
		       struct Token *name, bool is_enabled)
{
	alter_entity_def_init(&enable_def->base, parent_name, type,
			      ALTER_ACTION_ENABLE);
	enable_def->name = *name;
	enable_def->is_enabled = is_enabled;
}

static inline void
create_entity_def_init(struct create_entity_def *create_def,
		       enum entity_type type, struct SrcList *parent_name,
		       struct Token *name, bool if_not_exist)
{
	alter_entity_def_init(&create_def->base, parent_name, type,
			      ALTER_ACTION_CREATE);
	create_def->name = *name;
	create_def->if_not_exist = if_not_exist;
}

static inline void
create_constraint_def_init(struct create_constraint_def *constr_def,
			   struct SrcList *parent_name, struct Token *name,
			   bool if_not_exists, bool is_deferred,
			   enum entity_type entity_type)
{
	create_entity_def_init(&constr_def->base, entity_type,
			       parent_name, name, if_not_exists);
	constr_def->is_deferred = is_deferred;
}

static inline void
drop_entity_def_init(struct drop_entity_def *drop_def,
		     struct SrcList *parent_name, struct Token *name,
		     bool if_exist, enum entity_type entity_type)
{
	alter_entity_def_init(&drop_def->base, parent_name, entity_type,
			      ALTER_ACTION_DROP);
	drop_def->name = *name;
	drop_def->if_exist = if_exist;
}

static inline void
drop_table_def_init(struct drop_table_def *drop_table_def,
		    struct SrcList *parent_name, struct Token *name,
		    bool if_exist)
{
	drop_entity_def_init(&drop_table_def->base, parent_name, name, if_exist,
			     ENTITY_TYPE_TABLE);
}

static inline void
drop_view_def_init(struct drop_view_def *drop_view_def,
		   struct SrcList *parent_name, struct Token *name,
		   bool if_exist)
{
	drop_entity_def_init(&drop_view_def->base, parent_name, name, if_exist,
			     ENTITY_TYPE_VIEW);
}

static inline void
drop_trigger_def_init(struct drop_trigger_def *drop_trigger_def,
		      struct SrcList *parent_name, struct Token *name,
		      bool if_exist)
{
	drop_entity_def_init(&drop_trigger_def->base, parent_name, name,
			     if_exist, ENTITY_TYPE_TRIGGER);
}

static inline void
drop_constraint_def_init(struct drop_constraint_def *drop_constraint_def,
			 struct SrcList *parent_name, struct Token *name,
			 bool if_exist)
{
	drop_entity_def_init(&drop_constraint_def->base, parent_name, name,
			     if_exist, ENTITY_TYPE_CONSTRAINT);
}

static inline void
drop_index_def_init(struct drop_index_def *drop_index_def,
		    struct SrcList *parent_name, struct Token *name,
		    bool if_exist)
{
	drop_entity_def_init(&drop_index_def->base, parent_name, name, if_exist,
			     ENTITY_TYPE_INDEX);
}

static inline void
create_trigger_def_init(struct create_trigger_def *trigger_def,
			struct SrcList *table_name, struct Token *name,
			int tr_tm, int op, struct IdList *cols,
			struct Expr *when, bool if_not_exists)
{
	create_entity_def_init(&trigger_def->base, ENTITY_TYPE_TRIGGER,
			       table_name, name, if_not_exists);
	trigger_def->tr_tm = tr_tm;
	trigger_def->op = op;
	trigger_def->cols = cols;
	trigger_def->when = when;
}

static inline void
create_ck_def_init(struct create_ck_def *ck_def, struct SrcList *table_name,
		   struct Token *name, struct ExprSpan *expr)
{
	create_constraint_def_init(&ck_def->base, table_name, name, false,
				   false, ENTITY_TYPE_CK);
	ck_def->expr = expr;
}

static inline void
create_index_def_init(struct create_index_def *index_def,
		      struct SrcList *table_name,  struct Token *name,
		      struct ExprList *cols, enum sql_index_type idx_type,
		      enum sort_order sort_order, bool if_not_exists)
{
	create_constraint_def_init(&index_def->base, table_name, name,
				   if_not_exists, false, ENTITY_TYPE_INDEX);
	index_def->cols = cols;
	index_def->idx_type = idx_type;
	index_def->sort_order = sort_order;
}

static inline void
create_fk_def_init(struct create_fk_def *fk_def, struct SrcList *table_name,
		   struct Token *name, struct ExprList *child_cols,
		   struct Token *parent_name, struct ExprList *parent_cols,
		   int match, int actions, bool is_deferred)
{
	create_constraint_def_init(&fk_def->base, table_name, name,
				   false, is_deferred, ENTITY_TYPE_FK);
	fk_def->child_cols = child_cols;
	fk_def->parent_name = parent_name;
	fk_def->parent_cols = parent_cols;
	fk_def->actions = actions;
	fk_def->match = match;
}

static inline void
create_table_def_init(struct create_table_def *table_def, struct Token *name,
		      bool if_not_exists)
{
	create_entity_def_init(&table_def->base, ENTITY_TYPE_TABLE, NULL, name,
			       if_not_exists);
}

static inline void
create_column_def_init(struct create_column_def *column_def,
		       struct SrcList *table_name, struct Token *name,
		       struct type_def *type_def)
{
	create_entity_def_init(&column_def->base, ENTITY_TYPE_COLUMN,
			       table_name, name, false);
	column_def->type_def = type_def;
}

static inline void
create_ck_constraint_parse_def_init(struct create_ck_constraint_parse_def *def)
{
	rlist_create(&def->checks);
	def->count = 0;
}

static inline void
create_fk_constraint_parse_def_init(struct create_fk_constraint_parse_def *def)
{
	rlist_create(&def->fkeys);
	def->count = 0;
}

static inline void
create_view_def_init(struct create_view_def *view_def, struct Token *name,
		     struct Token *create, struct ExprList *aliases,
		     struct Select *select, bool if_not_exists)
{
	create_entity_def_init(&view_def->base, ENTITY_TYPE_VIEW, NULL, name,
			       if_not_exists);
	view_def->create_start = create;
	view_def->select = select;
	view_def->aliases = aliases;
}

static inline void
create_fk_constraint_parse_def_destroy(struct create_fk_constraint_parse_def *d)
{
	if (d->count == 0)
		return;
	struct fk_constraint_parse *fk;
	rlist_foreach_entry(fk, &d->fkeys, link)
		sql_expr_list_delete(sql_get(), fk->selfref_cols);
}

#endif /* TARANTOOL_BOX_SQL_PARSE_DEF_H_INCLUDED */
