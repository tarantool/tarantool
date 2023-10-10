/*
 * Copyright 2010-2017, Tarantool AUTHORS, please see AUTHORS file.
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

/*
 * This file contains code used to implement the PRAGMA command.
 */
#include "box/index.h"
#include "box/tuple.h"
#include "box/schema.h"
#include "box/coll_id_cache.h"
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "box/schema.h"
#include "box/session.h"
#include "pragma.h"

/** Set result column names and types for a pragma. */
static void
vdbe_set_pragma_result_columns(struct Vdbe *v, const struct PragmaName *pragma)
{
	int n = pragma->nPragCName;
	assert(n > 0);
	sqlVdbeSetNumCols(v, n);
	for (int i = 0, j = pragma->iPragCName; i < n; ++i) {
		vdbe_metadata_set_col_name(v, i, pragCName[j++]);
		vdbe_metadata_set_col_type(v, i, pragCName[j++]);
	}
}

/*
 * Locate a pragma in the aPragmaName[] array.
 */
static const struct PragmaName *
pragmaLocate(const char *zName)
{
	int upr, lwr, mid, rc;
	lwr = 0;
	upr = ArraySize(aPragmaName) - 1;
	while (lwr <= upr) {
		mid = (lwr + upr) / 2;
		rc = sql_stricmp(zName, aPragmaName[mid].zName);
		if (rc == 0)
			break;
		if (rc < 0) {
			upr = mid - 1;
		} else {
			lwr = mid + 1;
		}
	}
	return lwr > upr ? 0 : &aPragmaName[mid];
}

/**
 * This function handles PRAGMA TABLE_INFO(<table>).
 *
 * Return a single row for each column of the named table.
 * The columns of the returned data set are:
 *
 * - cid: Column id (numbered from left to right, starting at 0);
 * - name: Column name;
 * - type: Column declaration type;
 * - notnull: True if 'NOT NULL' is part of column declaration;
 * - dflt_value: The default value for the column, if any.
 */
static void
sql_pragma_table_info(struct Parse *parse, const struct space *space)
{
	if (space == NULL)
		return;
	parse->nMem = 6;
	if (space->def->opts.is_view)
		sql_view_assign_cursors(parse, space->def->opts.sql);
	struct Vdbe *v = sqlGetVdbe(parse);
	struct field_def *field = space->def->fields;
	for (uint32_t i = 0, k; i < space->def->field_count; ++i, ++field) {
		if (space->index_count == 0) {
			k = 1;
		} else if (!sql_space_column_is_in_pk(space, i)) {
			k = 0;
		} else {
			const struct index *pk = space->index[0];
			const struct key_def *kdef = pk->def->key_def;
			k = key_def_find_by_fieldno(kdef, i) - kdef->parts + 1;
		}
		sqlVdbeMultiLoad(v, 1, "issisi", i, field->name,
				     field_type_strs[field->type],
				     !field->is_nullable,
				     field->sql_default_value,
				     k);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 6);
	}
}

/**
 * This function handles PRAGMA stats.
 * It displays estimate (log) number of tuples in space and
 * average size of tuple in each index.
 *
 * @param space Space to be examined.
 * @param data Parsing context passed as callback argument.
 */
static int
sql_pragma_table_stats(struct space *space, void *data)
{
	if (space->def->opts.is_view)
		return 0;
	struct Parse *parse = (struct Parse *) data;
	struct index *pk = space_index(space, 0);
	if (pk == NULL)
		return 0;
	struct Vdbe *v = sqlGetVdbe(parse);
	LogEst tuple_count_est = sqlLogEst(index_size(pk));
	size_t avg_tuple_size_pk = sql_index_tuple_size(space, pk);
	parse->nMem = 4;
	sqlVdbeMultiLoad(v, 1, "ssii", space->def->name, 0,
			     avg_tuple_size_pk, tuple_count_est);
	sqlVdbeAddOp2(v, OP_ResultRow, 1, 4);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		assert(idx != NULL);
		size_t avg_tuple_size_idx = sql_index_tuple_size(space, idx);
		sqlVdbeMultiLoad(v, 2, "sii", idx->def->name,
				     avg_tuple_size_idx,
				     index_field_tuple_est(idx->def, 0));
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 4);
	}
	return 0;
}

/**
 * This function handles PRAGMA INDEX_INFO(<table>.<index>).
 *
 * Return a single row for each column of the index.
 * The columns of the returned data set are:
 *
 * - seqno: Zero-based column id within the index.
 * - cid: Zero-based column id within the table.
 * - name: Table column name.
 * - desc: Whether sorting by the column is descending (1 or 0).
 * - coll: Collation name.
 * - type: Type of a column value.
 */
static void
sql_pragma_index_info(struct Parse *parse, const struct space *space,
		      const struct index *idx)
{
	if (space == NULL || idx == NULL)
		return;
	parse->nMem = 6;
	struct Vdbe *v = sqlGetVdbe(parse);
	assert(v != NULL);
	uint32_t part_count = idx->def->key_def->part_count;
	struct key_part *part = idx->def->key_def->parts;
	for (uint32_t i = 0; i < part_count; i++, part++) {
		const char *c_n;
		uint32_t id = part->coll_id;
		struct coll *coll = part->coll;
		if (coll != NULL)
			c_n = coll_by_id(id)->name;
		else
			c_n = "BINARY";
		uint32_t fieldno = part->fieldno;
		enum field_type type = space->def->fields[fieldno].type;
		sqlVdbeMultiLoad(v, 1, "iisiss", i, fieldno,
				     space->def->fields[fieldno].name,
				     part->sort_order, c_n,
				     field_type_strs[type]);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, parse->nMem);
	}
}

/**
 * This function handles PRAGMA collation_list.
 *
 * Return a list of available collations.
 *
 * - seqno: Zero-based column id within the index.
 * - name: Collation name.
 *
 * @param parse_context Current parsing content.
 */
static void
sql_pragma_collation_list(struct Parse *parse_context)
{
	struct Vdbe *v = sqlGetVdbe(parse_context);
	assert(v != NULL);
	/* 16 is enough to encode 0 len array */
	char key_buf[16];
	char *key_end = mp_encode_array(key_buf, 0);
	box_tuple_t *tuple;
	box_iterator_t *it = box_index_iterator(BOX_VCOLLATION_ID, 0, ITER_ALL,
						key_buf, key_end);
	if (it == NULL) {
		parse_context->is_aborted = true;
		return;
	}
	int rc = box_iterator_next(it, &tuple);
	assert(rc == 0);
	(void) rc;
	for (int i = 0; tuple != NULL; i++, box_iterator_next(it, &tuple)) {
		const char *str = tuple_field_cstr(tuple,
						   BOX_COLLATION_FIELD_NAME);
		assert(str != NULL);
		/* this procedure should reallocate and copy str */
		sqlVdbeMultiLoad(v, 1, "is", i, str);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 2);
	}
	box_iterator_free(it);
}

/** This function handles PRAGMA INDEX_LIST statement. */
static void
sql_pragma_index_list(struct Parse *parse, const struct space *space)
{
	if (space == NULL)
		return;
	parse->nMem = 3;
	struct Vdbe *v = sqlGetVdbe(parse);
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		sqlVdbeMultiLoad(v, 1, "isi", i, idx->def->name,
				     idx->def->opts.is_unique);
		sqlVdbeAddOp2(v, OP_ResultRow, 1, 3);
	}
}

/** This function handles PRAGMA foreign_key_list(<table>). */
static void
sql_pragma_foreign_key_list(struct Parse *parser, const struct space *space)
{
	(void)parser;
	(void)space;
	return;
}

void
sqlPragma(struct Parse *pParse, struct Token *pragma, struct Token *table_name,
	  struct Token *index_name)
{
	const struct space *space = NULL;
	const struct index *index = NULL;
	struct Vdbe *v = sqlGetVdbe(pParse);

	sqlVdbeRunOnlyOnce(v);
	pParse->nMem = 2;

	char *pragma_name = sql_name_from_token(pragma);
	if (table_name != NULL)
		space = sql_space_by_token(table_name);
	if (space != NULL && index_name != NULL) {
		uint32_t index_id = sql_index_id_by_token(space, index_name);
		if (index_id <= space->index_id_max)
			index = space->index_map[index_id];
	}

	/* Locate the pragma in the lookup table */
	const struct PragmaName *pPragma = pragmaLocate(pragma_name);
	if (pPragma == 0) {
		diag_set(ClientError, ER_SQL_NO_SUCH_PRAGMA, pragma_name);
		pParse->is_aborted = true;
		goto pragma_out;
	}
	/* Register the result column names for pragmas that return results */
	vdbe_set_pragma_result_columns(v, pPragma);

	/* Jump to the appropriate pragma handler */
	switch (pPragma->ePragTyp) {
	case PRAGMA_TABLE_INFO:
		sql_pragma_table_info(pParse, space);
		break;
	case PRAGMA_STATS:
		space_foreach(sql_pragma_table_stats, (void *) pParse);
		break;
	case PRAGMA_INDEX_INFO:
		sql_pragma_index_info(pParse, space, index);
		break;
	case PRAGMA_INDEX_LIST:
		sql_pragma_index_list(pParse, space);
		break;
	case PRAGMA_COLLATION_LIST:
		sql_pragma_collation_list(pParse);
		break;
	case PRAGMA_FOREIGN_KEY_LIST:
		sql_pragma_foreign_key_list(pParse, space);
		break;
	default:
		unreachable();
	}

 pragma_out:
	sql_xfree(pragma_name);
}
