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
 * This file contains C code routines that are called by the parser
 * to handle INSERT statements in sql.
 */
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "mem.h"
#include "vdbeInt.h"
#include "box/ck_constraint.h"
#include "bit/bit.h"
#include "box/box.h"
#include "box/schema.h"

enum field_type *
sql_index_type_str(struct sql *db, const struct index_def *idx_def)
{
	uint32_t column_count = idx_def->key_def->part_count;
	uint32_t sz = (column_count + 1) * sizeof(enum field_type);
	enum field_type *types = (enum field_type *) sqlDbMallocRaw(db, sz);
	if (types == NULL)
		return NULL;
	for (uint32_t i = 0; i < column_count; i++)
		types[i] = idx_def->key_def->parts[i].type;
	types[column_count] = field_type_MAX;
	return types;
}

void
sql_emit_table_types(struct Vdbe *v, struct space_def *def, int reg)
{
	assert(reg > 0);
	struct sql *db = sqlVdbeDb(v);
	uint32_t field_count = def->field_count;
	size_t sz = (field_count + 1) * sizeof(enum field_type);
	enum field_type *colls_type =
		(enum field_type *) sqlDbMallocZero(db, sz);
	if (colls_type == NULL)
		return;
	for (uint32_t i = 0; i < field_count; ++i)
		colls_type[i] = def->fields[i].type;
	colls_type[field_count] = field_type_MAX;
	sqlVdbeAddOp4(v, OP_ApplyType, reg, field_count, 0,
			  (char *)colls_type, P4_DYNAMIC);
}

/**
 * In SQL table can be created with AUTOINCREMENT.
 * In Tarantool it can be detected as primary key which consists
 * from one field with not NULL space's sequence.
 */
static uint32_t
sql_space_autoinc_fieldno(struct space *space)
{
	assert(space != NULL);
	if (space->sequence == NULL)
		return UINT32_MAX;
	return space->sequence_fieldno;
}

/**
 * This routine is used to see if a statement of the form
 * "INSERT INTO <table> SELECT ..." can run for the results of the
 * SELECT. Otherwise, it may fall into infinite loop.
 *
 * @param parser Parse context.
 * @param space_def Space definition.
 * @retval  true if the space (given by space_id) in database or
 *          any of its indices have been opened at any point in
 *          the VDBE program.
 * @retval  false else.
 */
static bool
vdbe_has_space_read(struct Parse *parser, const struct space_def *space_def)
{
	struct Vdbe *v = sqlGetVdbe(parser);
	int last_instr = sqlVdbeCurrentAddr(v);
	for (int i = 1; i < last_instr; i++) {
		struct VdbeOp *op = sqlVdbeGetOp(v, i);
		assert(op != NULL);
		/*
		 * Currently, there is no difference between Read
		 * and Write cursors.
		 */
		if (op->opcode == OP_IteratorOpen) {
			struct space *space = NULL;
			if (op->p4type == P4_SPACEPTR)
				space = op->p4.space;
			else
				continue;
			if (space->def->id == space_def->id)
				return true;
		}
	}
	return false;
}

/* Forward declaration */
static int
xferOptimization(Parse * pParse,	/* Parser context */
		 struct space *dest,
		 Select * pSelect,	/* A SELECT statement to use as the data source */
		 int onError);	/* How to handle constraint errors */

/*
 * This routine is called to handle SQL of the following forms:
 *
 *    insert into TABLE (IDLIST) values(EXPRLIST),(EXPRLIST),...
 *    insert into TABLE (IDLIST) select
 *    insert into TABLE (IDLIST) default values
 *
 * The IDLIST following the table name is always optional.  If omitted,
 * then a list of all columns for the table is substituted.
 * The IDLIST appears in the pColumn parameter.  pColumn is NULL if IDLIST
 * is omitted.
 *
 * For the pSelect parameter holds the values to be inserted for the
 * first two forms shown above.  A VALUES clause is really just short-hand
 * for a SELECT statement that omits the FROM clause and everything else
 * that follows.  If the pSelect parameter is NULL, that means that the
 * DEFAULT VALUES form of the INSERT statement is intended.
 *
 * The code generated follows one of four templates.  For a simple
 * insert with data coming from a single-row VALUES clause, the code executes
 * once straight down through.  Pseudo-code follows (we call this
 * the "1st template"):
 *
 *         open write cursor to <table> and its indices
 *         put VALUES clause expressions into registers
 *         write the resulting record into <table>
 *         cleanup
 *
 * The three remaining templates assume the statement is of the form
 *
 *   INSERT INTO <table> SELECT ...
 *
 * If the SELECT clause is of the restricted form "SELECT * FROM <table2>" -
 * in other words if the SELECT pulls all columns from a single table
 * and there is no WHERE or LIMIT or GROUP BY or ORDER BY clauses, and
 * if <table2> and <table1> are distinct tables but have identical
 * schemas, including all the same indices, then a special optimization
 * is invoked that copies raw records from <table2> over to <table1>.
 * See the xferOptimization() function for the implementation of this
 * template.  This is the 2nd template.
 *
 *         open a write cursor to <table>
 *         open read cursor on <table2>
 *         transfer all records in <table2> over to <table>
 *         close cursors
 *         foreach index on <table>
 *           open a write cursor on the <table> index
 *           open a read cursor on the corresponding <table2> index
 *           transfer all records from the read to the write cursors
 *           close cursors
 *         end foreach
 *
 * The 3rd template is for when the second template does not apply
 * and the SELECT clause does not read from <table> at any time.
 * The generated code follows this template:
 *
 *         X <- A
 *         goto B
 *      A: setup for the SELECT
 *         loop over the rows in the SELECT
 *           load values into registers R..R+n
 *           yield X
 *         end loop
 *         cleanup after the SELECT
 *         end-coroutine X
 *      B: open write cursor to <table> and its indices
 *      C: yield X, at EOF goto D
 *         insert the select result into <table> from R..R+n
 *         goto C
 *      D: cleanup
 *
 * The 4th template is used if the insert statement takes its
 * values from a SELECT but the data is being inserted into a table
 * that is also read as part of the SELECT.  In the third form,
 * we have to use an intermediate table to store the results of
 * the select.  The template is like this:
 *
 *         X <- A
 *         goto B
 *      A: setup for the SELECT
 *         loop over the tables in the SELECT
 *           load value into register R..R+n
 *           yield X
 *         end loop
 *         cleanup after the SELECT
 *         end co-routine R
 *      B: open temp table
 *      L: yield X, at EOF goto M
 *         insert row from R..R+n into temp table
 *         goto L
 *      M: open write cursor to <table> and its indices
 *         rewind temp table
 *      C: loop over rows of intermediate table
 *           transfer values form intermediate table into <table>
 *         end loop
 *      D: cleanup
 */
void
sqlInsert(Parse * pParse,	/* Parser context */
	      SrcList * pTabList,	/* Name of table into which we are inserting */
	      Select * pSelect,	/* A SELECT statement to use as the data source */
	      IdList * pColumn,	/* Column names corresponding to IDLIST. */
	      enum on_conflict_action on_error)
{
	sql *db;		/* The main database structure */
	char *zTab;		/* Name of the table into which we are inserting */
	int i, j;		/* Loop counters */
	Vdbe *v;		/* Generate code into this virtual machine */
	int nColumn;		/* Number of columns in the data */
	int endOfLoop;		/* Label for the end of the insertion loop */
	int srcTab = 0;		/* Data comes from this temporary cursor if >=0 */
	int addrInsTop = 0;	/* Jump to label "D" */
	int addrCont = 0;	/* Top of insert loop. Label "C" in templates 3 and 4 */
	SelectDest dest;	/* Destination for SELECT on rhs of INSERT */
	u8 useTempTable = 0;	/* Store SELECT results in intermediate table */
	u8 bIdListInOrder;	/* True if IDLIST is in table order */
	ExprList *pList = 0;	/* List of VALUES() to be inserted  */

	/* Register allocations */
	int regFromSelect = 0;	/* Base register for data coming from SELECT */
	int regIns;		/* Block of regs holding data being inserted */
	int regTupleid;		/* registers holding insert tupleid */
	int regData;		/* register holding first column to insert */
	int *aRegIdx = 0;	/* One register allocated to each index */
	/* List of triggers on pTab, if required. */
	struct sql_trigger *trigger;
	int tmask;		/* Mask of trigger times */

	db = pParse->db;
	memset(&dest, 0, sizeof(dest));
	if (pParse->is_aborted || db->mallocFailed) {
		goto insert_cleanup;
	}

	/* If the Select object is really just a simple VALUES() list with a
	 * single row (the common case) then keep that one row of values
	 * and discard the other (unused) parts of the pSelect object
	 */
	if (pSelect && (pSelect->selFlags & SF_Values) != 0
	    && pSelect->pPrior == 0) {
		pList = pSelect->pEList;
		pSelect->pEList = 0;
		sql_select_delete(db, pSelect);
		pSelect = 0;
	}

	/* Locate the table into which we will be inserting new information.
	 */
	assert(pTabList->nSrc == 1);
	zTab = pTabList->a[0].zName;
	if (NEVER(zTab == 0))
		goto insert_cleanup;
	struct space *space = sql_lookup_space(pParse, pTabList->a);

	if (space == NULL)
		goto insert_cleanup;

	/* Figure out if we have any triggers and if the table being
	 * inserted into is a view
	 */
	struct space_def *space_def = space->def;
	trigger = sql_triggers_exist(space_def, TK_INSERT, NULL,
				     pParse->sql_flags, &tmask);

	bool is_view = space_def->opts.is_view;
	assert((trigger != NULL && tmask != 0) ||
	       (trigger == NULL && tmask == 0));

	/* If pTab is really a view, make sure it has been initialized.
	 * ViewGetColumnNames() is a no-op if pTab is not a view.
	 */
	if (is_view && sql_view_assign_cursors(pParse, space_def->opts.sql) != 0)
		goto insert_cleanup;

	/* Cannot insert into a read-only table. */
	if (is_view && tmask == 0) {
		diag_set(ClientError, ER_ALTER_SPACE, space->def->name,
			 "space is a view");
		pParse->is_aborted = true;
		goto insert_cleanup;
	}

	/* Allocate a VDBE. */
	v = sqlGetVdbe(pParse);
	if (v == NULL)
		goto insert_cleanup;
	sqlVdbeCountChanges(v);
	sql_set_multi_write(pParse, pSelect != NULL || trigger != NULL);

	/* If the statement is of the form
	 *
	 *       INSERT INTO <table1> SELECT * FROM <table2>;
	 *
	 * Then special optimizations can be applied that make the transfer
	 * very fast and which reduce fragmentation of indices.
	 *
	 * This is the 2nd template.
	 */
	if (pColumn == NULL &&
	    xferOptimization(pParse, space, pSelect, on_error)) {
		assert(trigger == NULL);
		assert(pList == 0);
		goto insert_cleanup;
	}

	/*
	 * Allocate registers for holding the tupleid of the new
	 * row (if it isn't required first register will contain
	 * NULL), the content of the new row, and the assembled
	 * row record.
	 */
	regTupleid = regIns = ++pParse->nMem;
	pParse->nMem += space_def->field_count + 1;
	regData = regTupleid + 1;

	/* If the INSERT statement included an IDLIST term, then make sure
	 * all elements of the IDLIST really are columns of the table and
	 * remember the column indices.
	 */
	/* Create bitmask to mark used columns of the table. */
	void *used_columns = tt_static_buf();
	/* The size of used_columns buffer is checked during compilation time
	 * using SQL_MAX_COLUMN constant.
	 */
	memset(used_columns, 0, (space_def->field_count + 7) / 8);
	bIdListInOrder = 1;
	if (pColumn) {
		for (i = 0; i < pColumn->nId; i++) {
			pColumn->a[i].idx = -1;
		}
		for (i = 0; i < pColumn->nId; i++) {
			for (j = 0; j < (int) space_def->field_count; j++) {
				if (strcmp(pColumn->a[i].zName,
					   space_def->fields[j].name) == 0) {
					pColumn->a[i].idx = j;
					if (i != j)
						bIdListInOrder = 0;
					break;
				}
			}
			if (j >= (int) space_def->field_count) {
				diag_set(ClientError,
					 ER_NO_SUCH_FIELD_NAME_IN_SPACE,
					 pColumn->a[i].zName,
					 pTabList->a[0].zName);
				pParse->is_aborted = true;
				goto insert_cleanup;
			}
			if (bit_test(used_columns, j)) {
				const char *err = "table id list: duplicate "\
						  "column name %s";
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 tt_sprintf(err, pColumn->a[i].zName));
				pParse->is_aborted = true;
				goto insert_cleanup;
			}
			bit_set(used_columns, j);
		}
	}

	int reg_eph;
	/* Figure out how many columns of data are supplied.  If the data
	 * is coming from a SELECT statement, then generate a co-routine that
	 * produces a single row of the SELECT on each invocation.  The
	 * co-routine is the common header to the 3rd and 4th templates.
	 */
	if (pSelect) {
		/* Data is coming from a SELECT or from a multi-row VALUES clause.
		 * Generate a co-routine to run the SELECT.
		 */
		int regYield;	/* Register holding co-routine entry-point */
		int addrTop;	/* Top of the co-routine */
		int rc;		/* Result code */

		regYield = ++pParse->nMem;
		addrTop = sqlVdbeCurrentAddr(v) + 1;
		sqlVdbeAddOp3(v, OP_InitCoroutine, regYield, 0, addrTop);
		sqlSelectDestInit(&dest, SRT_Coroutine, regYield, -1);
		dest.iSdst = bIdListInOrder ? regData : 0;
		dest.nSdst = space_def->field_count;
		rc = sqlSelect(pParse, pSelect, &dest);
		regFromSelect = dest.iSdst;
		if (rc || db->mallocFailed || pParse->is_aborted)
			goto insert_cleanup;
		sqlVdbeEndCoroutine(v, regYield);
		sqlVdbeJumpHere(v, addrTop - 1);	/* label B: */
		assert(pSelect->pEList);
		nColumn = pSelect->pEList->nExpr;

		/*
		 * Set useTempTable to TRUE if the result of the
		 * SELECT statement should be written into a
		 * temporary table (template 4). Set to FALSE if
		 * each output row of the SELECT can be written
		 * directly into the destination table
		 * (template 3).
		 *
		 * A temp table must be used if the table being
		 * updated is also one of the tables being read by
		 * the SELECT statement. Also use a temp table in
		 * the case of row triggers.
		 */
		if (trigger != NULL || vdbe_has_space_read(pParse, space_def))
			useTempTable = 1;

		if (useTempTable) {
			/* Invoke the coroutine to extract information from the SELECT
			 * and add it to a transient table srcTab.  The code generated
			 * here is from the 4th template:
			 *
			 *      B: open temp table
			 *      L: yield X, goto M at EOF
			 *         insert row from R..R+n into temp table
			 *         goto L
			 *      M: ...
			 */
			int regRec;	/* Register to hold packed record */
			int regCopy;    /* Register to keep copy of registers from select */
			int addrL;	/* Label "L" */

			srcTab = pParse->nTab++;
			reg_eph = ++pParse->nMem;
			regRec = sqlGetTempReg(pParse);
			regCopy = sqlGetTempRange(pParse, nColumn + 1);
			sqlVdbeAddOp2(v, OP_OpenTEphemeral, reg_eph,
					  nColumn + 1);
			/*
			 * This key_info is used to show that
			 * rowid should be the first part of PK in
			 * case we used AUTOINCREMENT feature.
			 * This way we will save initial order of
			 * the inserted values. The order is
			 * important if we use the AUTOINCREMENT
			 * feature, since changing the order can
			 * change the number inserted instead of
			 * NULL.
			 */
			if (space->sequence != NULL) {
				struct sql_key_info *key_info =
					sql_key_info_new(pParse->db,
							 nColumn + 1);
				key_info->parts[nColumn].type =
					FIELD_TYPE_UNSIGNED;
				key_info->is_pk_rowid = true;
				sqlVdbeChangeP4(v, -1, (void *)key_info,
					        P4_KEYINFO);
			}
			addrL = sqlVdbeAddOp1(v, OP_Yield, dest.iSDParm);
			VdbeCoverage(v);
			sqlVdbeAddOp2(v, OP_NextIdEphemeral, reg_eph,
					  regCopy + nColumn);
			sqlVdbeAddOp3(v, OP_Copy, regFromSelect, regCopy, nColumn-1);
			sqlVdbeAddOp3(v, OP_MakeRecord, regCopy,
					  nColumn + 1, regRec);
			/* Set flag to save memory allocating one by malloc. */
			sqlVdbeChangeP5(v, 1);
			sqlVdbeAddOp2(v, OP_IdxInsert, regRec, reg_eph);

			sqlVdbeGoto(v, addrL);
			sqlVdbeJumpHere(v, addrL);
			sqlReleaseTempReg(pParse, regRec);
			sqlReleaseTempRange(pParse, regCopy, nColumn);
		}
	} else {
		/* This is the case if the data for the INSERT is coming from a
		 * single-row VALUES clause
		 */
		NameContext sNC;
		memset(&sNC, 0, sizeof(sNC));
		sNC.pParse = pParse;
		srcTab = -1;
		reg_eph = -1;
		assert(useTempTable == 0);
		if (pList) {
			nColumn = pList->nExpr;
			if (sqlResolveExprListNames(&sNC, pList)) {
				goto insert_cleanup;
			}
		} else {
			nColumn = 0;
		}
	}

	if (pColumn == NULL && nColumn != 0 &&
	    nColumn != (int)space_def->field_count) {
		const char *err =
			"table %s has %d columns but %d values were supplied";
		err = tt_sprintf(err, pTabList->a[0].zName,
				 space_def->field_count, nColumn);
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, err);
		pParse->is_aborted = true;
		goto insert_cleanup;
	}
	if (pColumn != 0 && nColumn != pColumn->nId) {
		const char *err = "%d values for %d columns";
		diag_set(ClientError, ER_SQL_PARSER_GENERIC,
			 tt_sprintf(err, nColumn, pColumn->nId));
		pParse->is_aborted = true;
		goto insert_cleanup;
	}

	/* This is the top of the main insertion loop */
	if (useTempTable) {
		/* This block codes the top of loop only.  The complete loop is the
		 * following pseudocode (template 4):
		 *
		 *         rewind temp table, if empty goto D
		 *      C: loop over rows of intermediate table
		 *           transfer values form intermediate table into <table>
		 *         end loop
		 *      D: ...
		 */
		sqlVdbeAddOp3(v, OP_IteratorOpen, srcTab, 0, reg_eph);
		addrInsTop = sqlVdbeAddOp1(v, OP_Rewind, srcTab);
		VdbeCoverage(v);
		addrCont = sqlVdbeCurrentAddr(v);
	} else if (pSelect) {
		/* This block codes the top of loop only.  The complete loop is the
		 * following pseudocode (template 3):
		 *
		 *      C: yield X, at EOF goto D
		 *         insert the select result into <table> from R..R+n
		 *         goto C
		 *      D: ...
		 */
		addrInsTop = addrCont =
		    sqlVdbeAddOp1(v, OP_Yield, dest.iSDParm);
		VdbeCoverage(v);
	}
	assert(space != NULL);
	uint32_t autoinc_fieldno = sql_space_autoinc_fieldno(space);
	/* Run the BEFORE and INSTEAD OF triggers, if there are any
	 */
	endOfLoop = sqlVdbeMakeLabel(v);
	if (tmask & TRIGGER_BEFORE) {
		int regCols =
			sqlGetTempRange(pParse, space_def->field_count + 1);

		/* Create the new column data
		 */
		for (i = j = 0; i < (int)space_def->field_count; i++) {
			if (pColumn) {
				for (j = 0; j < pColumn->nId; j++) {
					if (pColumn->a[j].idx == i)
						break;
				}
			}
			if ((!useTempTable && !pList)
			    || (pColumn && j >= pColumn->nId)) {
				if (i == (int) autoinc_fieldno) {
					sqlVdbeAddOp2(v, OP_Integer, -1,
							  regCols + i + 1);
				} else {
					struct Expr *dflt = NULL;
					dflt = space_def->fields[i].
						default_value_expr;
					sqlExprCode(pParse,
							dflt,
							regCols + i + 1);
				}
			} else if (useTempTable) {
				sqlVdbeAddOp3(v, OP_Column, srcTab, j,
						  regCols + i + 1);
			} else {
				assert(pSelect == 0);	/* Otherwise useTempTable is true */
				sqlExprCodeAndCache(pParse,
							pList->a[j].pExpr,
							regCols + i + 1);
			}
			if (pColumn == 0)
				j++;
		}

		/* If this is an INSERT on a view with an INSTEAD OF INSERT trigger,
		 * do not attempt any conversions before assembling the record.
		 * If this is a real table, attempt conversions as required by the
		 * table column types.
		 */
		if (!is_view)
			sql_emit_table_types(v, space_def, regCols + 1);

		/* Fire BEFORE or INSTEAD OF triggers */
		vdbe_code_row_trigger(pParse, trigger, TK_INSERT, 0,
				      TRIGGER_BEFORE, space,
				      regCols - space_def->field_count - 1, on_error,
				      endOfLoop);

		sqlReleaseTempRange(pParse, regCols, space_def->field_count + 1);
	}

	/* Compute the content of the next row to insert into a range of
	 * registers beginning at regIns.
	 */
	if (!is_view) {
		sqlVdbeAddOp2(v, OP_Null, 0, regTupleid);

		/* Compute data for all columns of the new entry, beginning
		 * with the first column.
		 */
		for (i = 0; i < (int) space_def->field_count; i++) {
			int iRegStore = regData + i;
			if (pColumn == 0) {
				j = i;
			} else {
				for (j = 0; j < pColumn->nId; j++) {
					if (pColumn->a[j].idx == i)
						break;
				}
			}
			if (j < 0 || nColumn == 0
			    || (pColumn && j >= pColumn->nId)) {
				if (i == (int) autoinc_fieldno) {
					sqlVdbeAddOp2(v, OP_Null, 0, iRegStore);
					continue;
				}
				struct Expr *dflt = NULL;
				dflt = space_def->fields[i].default_value_expr;
				sqlExprCodeFactorable(pParse,
							  dflt,
							  iRegStore);
			} else if (useTempTable) {
				if (i == (int) autoinc_fieldno) {
					int regTmp = ++pParse->nMem;
					/* Emit code which doesn't override
					 * autoinc-ed value with select result
					 * in case if result is NULL value.
					 */
					sqlVdbeAddOp3(v, OP_Column, srcTab,
							  j, regTmp);
					sqlVdbeAddOp2(v, OP_IsNull,
							  regTmp,
							  v->nOp + 2);
					sqlVdbeAddOp1(v, OP_MustBeInt,
							  regTmp);
					sqlVdbeAddOp2(v, OP_FCopy, regTmp,
							  iRegStore);
					sqlVdbeChangeP3(v, -1,
							    OPFLAG_SAME_FRAME |
							    OPFLAG_NOOP_IF_NULL);
				} else {
					sqlVdbeAddOp3(v, OP_Column, srcTab,
							  j, iRegStore);
				}
			} else if (pSelect) {
				if (regFromSelect != regData) {
					if (i == (int) autoinc_fieldno) {
						/* Emit code which doesn't override
						 * autoinc-ed value with select result
						 * in case that result is NULL
						 */
						sqlVdbeAddOp2(v, OP_IsNull,
								  regFromSelect
								  + j,
								  v->nOp + 2);
						sqlVdbeAddOp1(v,
								  OP_MustBeInt,
								  regFromSelect
								  + j);
						sqlVdbeAddOp2(v, OP_FCopy,
								  regFromSelect
								  + j,
								  iRegStore);
						sqlVdbeChangeP3(v, -1,
								    OPFLAG_SAME_FRAME
								    |
								    OPFLAG_NOOP_IF_NULL);
					} else {
						sqlVdbeAddOp2(v, OP_SCopy,
								  regFromSelect
								  + j,
								  iRegStore);
					}
				}
			} else {

				if (i == (int) autoinc_fieldno) {
					if (pList->a[j].pExpr->op == TK_NULL) {
						sqlVdbeAddOp2(v, OP_Null, 0, iRegStore);
						continue;
					}

					if (pList->a[j].pExpr->op ==
					    TK_REGISTER) {
						/* Emit code which doesn't override
						 * autoinc-ed value with select result
						 * in case that result is NULL
						 */
						sqlVdbeAddOp2(v, OP_IsNull,
								  pList->a[j].
								  pExpr->iTable,
								  v->nOp + 2);
						sqlVdbeAddOp1(v,
								  OP_MustBeInt,
								  pList->a[j].
								  pExpr->iTable);
						sqlVdbeAddOp2(v, OP_FCopy,
								  pList->a[j].
								  pExpr->iTable,
								  iRegStore);
						sqlVdbeChangeP3(v, -1,
								    OPFLAG_SAME_FRAME
								    |
								    OPFLAG_NOOP_IF_NULL);
						continue;
					}
				}

				sqlExprCode(pParse, pList->a[j].pExpr,
						iRegStore);
			}
		}

		int autoinc_reg = 0;
		if (autoinc_fieldno < UINT32_MAX &&
		    pParse->triggered_space == NULL)
			autoinc_reg = regData + autoinc_fieldno;
		/*
		 * Generate code to check constraints and process
		 * final insertion.
		 */
		vdbe_emit_constraint_checks(pParse, space, regIns + 1,
					    on_error, endOfLoop, 0);
		fk_constraint_emit_check(pParse, space, 0, regIns, 0);
		vdbe_emit_insertion_completion(v, space, regIns + 1,
					       space->def->field_count,
					       on_error, autoinc_reg);
	}

	if (trigger != NULL) {
		/* Code AFTER triggers */
		vdbe_code_row_trigger(pParse, trigger, TK_INSERT, 0,
				      TRIGGER_AFTER, space,
				      regData - 2 - space_def->field_count, on_error,
				      endOfLoop);
	}

	/* The bottom of the main insertion loop, if the data source
	 * is a SELECT statement.
	 */
	sqlVdbeResolveLabel(v, endOfLoop);
	if (useTempTable) {
		sqlVdbeAddOp2(v, OP_Next, srcTab, addrCont);
		VdbeCoverage(v);
		sqlVdbeJumpHere(v, addrInsTop);
		sqlVdbeAddOp1(v, OP_Close, srcTab);
	} else if (pSelect) {
		sqlVdbeGoto(v, addrCont);
		sqlVdbeJumpHere(v, addrInsTop);
	}

 insert_cleanup:
	sqlSrcListDelete(db, pTabList);
	sql_expr_list_delete(db, pList);
	sql_select_delete(db, pSelect);
	sqlIdListDelete(db, pColumn);
	sqlDbFree(db, aRegIdx);
}

void
vdbe_emit_ck_constraint(struct Parse *parser, struct Expr *expr,
			const char *name, const char *expr_str,
			int vdbe_field_ref_reg)
{
	parser->vdbe_field_ref_reg = vdbe_field_ref_reg;
	struct Vdbe *v = sqlGetVdbe(parser);
	const char *ck_constraint_name = sqlDbStrDup(parser->db, name);
	VdbeNoopComment((v, "BEGIN: ck constraint %s test",
			ck_constraint_name));
	int check_is_passed = sqlVdbeMakeLabel(v);
	sqlExprIfTrue(parser, expr, check_is_passed, SQL_JUMPIFNULL);
	const char *fmt = tnt_errcode_desc(ER_CK_CONSTRAINT_FAILED);
	const char *error_msg = tt_sprintf(fmt, ck_constraint_name, expr_str);
	sqlVdbeAddOp4(v, OP_SetDiag, ER_CK_CONSTRAINT_FAILED, 0, 0,
		      sqlDbStrDup(parser->db, error_msg), P4_DYNAMIC);
	sqlVdbeAddOp2(v, OP_Halt, -1, ON_CONFLICT_ACTION_ABORT);
	VdbeNoopComment((v, "END: ck constraint %s test", ck_constraint_name));
	sqlVdbeResolveLabel(v, check_is_passed);
}

void
vdbe_emit_constraint_checks(struct Parse *parse_context, struct space *space,
			    int new_tuple_reg,
			    enum on_conflict_action on_conflict,
			    int ignore_label, int *upd_cols)
{
	struct Vdbe *v = sqlGetVdbe(parse_context);
	assert(v != NULL);
	bool is_update = upd_cols != NULL;
	assert(space != NULL);
	struct space_def *def = space->def;
	const char *err;
	/* Insertion into VIEW is prohibited. */
	assert(!def->opts.is_view);
	uint32_t autoinc_fieldno = sql_space_autoinc_fieldno(space);
	/* Test all NOT NULL constraints. */
	for (uint32_t i = 0; i < def->field_count; i++) {
		/*
		 * Don't bother checking for NOT NULL on columns
		 * that do not change.
		 */
		if (is_update && upd_cols[i] < 0)
			continue;
		/* This column is allowed to be NULL. */
		if (def->fields[i].is_nullable || autoinc_fieldno == i)
			continue;
		enum on_conflict_action on_conflict_nullable =
			on_conflict != ON_CONFLICT_ACTION_DEFAULT ?
			on_conflict : def->fields[i].nullable_action;
		/* ABORT is a default error action. */
		if (on_conflict_nullable == ON_CONFLICT_ACTION_DEFAULT)
			on_conflict_nullable = ON_CONFLICT_ACTION_ABORT;
		struct Expr *dflt = space_column_default_expr(def->id, i);
		if (on_conflict_nullable == ON_CONFLICT_ACTION_REPLACE &&
		    dflt == NULL)
			on_conflict_nullable = ON_CONFLICT_ACTION_ABORT;
		int addr;
		switch (on_conflict_nullable) {
		case ON_CONFLICT_ACTION_ABORT:
		case ON_CONFLICT_ACTION_ROLLBACK:
		case ON_CONFLICT_ACTION_FAIL:
			err = tt_sprintf(tnt_errcode_desc(ER_SQL_EXECUTE),
					 tt_sprintf("NOT NULL constraint "\
						    "failed: %s.%s", def->name,
						    def->fields[i].name));
			addr = sqlVdbeAddOp1(v, OP_NotNull, new_tuple_reg + i);
			sqlVdbeAddOp4(v, OP_SetDiag, ER_SQL_EXECUTE, 0, 0, err,
				      P4_STATIC);
			sqlVdbeAddOp2(v, OP_Halt, -1, on_conflict_nullable);
			sqlVdbeJumpHere(v, addr);
			break;
		case ON_CONFLICT_ACTION_IGNORE:
			sqlVdbeAddOp2(v, OP_IsNull, new_tuple_reg + i,
					  ignore_label);
			break;
		case ON_CONFLICT_ACTION_REPLACE:
			addr = sqlVdbeAddOp1(v, OP_NotNull,
						  new_tuple_reg + i);
			sqlExprCode(parse_context, dflt, new_tuple_reg + i);
			sqlVdbeJumpHere(v, addr);
			break;
		default:
			unreachable();
		}
	}
	sql_emit_table_types(v, space->def, new_tuple_reg);
	/*
	 * Other actions except for REPLACE and UPDATE OR IGNORE
	 * can be handled by setting appropriate flag in OP_Halt.
	 */
	if (!(on_conflict == ON_CONFLICT_ACTION_IGNORE && is_update) &&
	    on_conflict != ON_CONFLICT_ACTION_REPLACE)
		return;
	/* Calculate MAX range of register we may occupy. */
	uint32_t reg_count = 0;
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		if (idx->def->key_def->part_count > reg_count)
			reg_count = idx->def->key_def->part_count;
	}
	int idx_key_reg = ++parse_context->nMem;
	parse_context->nMem += reg_count;
	/*
	 * To handle INSERT OR REPLACE statement we should check
	 * all unique secondary indexes on containing entry with
	 * the same key. If index contains it, we must invoke
	 * ON DELETE trigger and remove entry.
	 * For UPDATE OR IGNORE we must check that no entries
	 * exist in indexes which contain updated columns.
	 * Otherwise, we should skip removal of old entry and
	 * insertion of new one.
	 */
	for (uint32_t i = 0; i < space->index_count; ++i) {
		struct index *idx = space->index[i];
		/* Conflicts may occur only in UNIQUE indexes. */
		if (!idx->def->opts.is_unique)
			continue;
		if (on_conflict == ON_CONFLICT_ACTION_IGNORE) {
			/*
			 * We are interested only in indexes which
			 * contain updated columns.
			 */
			struct key_def *kd = idx->def->key_def;
			for (uint32_t i = 0; i < kd->part_count; ++i) {
				if (upd_cols[kd->parts[i].fieldno] >= 0)
					goto process_index;
			}
			continue;
		}
process_index:  ;
		int cursor = parse_context->nTab++;
		vdbe_emit_open_cursor(parse_context, cursor, idx->def->iid,
				      space);
		/*
		 * If there is no conflict in current index, just
		 * jump to the start of next iteration. Label is
		 * used for REPLACE action only.
		 */
		int skip_index = sqlVdbeMakeLabel(v);
		/*
		 * Copy index key to continuous range of
		 * registers. Initially whole tuple is located at
		 * [new_tuple_reg ... new_tuple_reg + field_count]
		 * We are copying key to [reg ... reg + part_count]
		 */
		uint32_t part_count = idx->def->key_def->part_count;
		for (uint32_t i = 0; i < part_count; ++i) {
			uint32_t fieldno = idx->def->key_def->parts[i].fieldno;
			int reg = fieldno + new_tuple_reg;
			sqlVdbeAddOp2(v, OP_SCopy, reg, idx_key_reg + i);
		}
		if (on_conflict == ON_CONFLICT_ACTION_IGNORE) {
			sqlVdbeAddOp4Int(v, OP_Found, cursor,
					     ignore_label, idx_key_reg,
					     part_count);
		} else {
			assert(on_conflict == ON_CONFLICT_ACTION_REPLACE);
			sqlVdbeAddOp4Int(v, OP_NoConflict, cursor,
					     skip_index, idx_key_reg,
					     part_count);
			sql_set_multi_write(parse_context, true);
			struct sql_trigger *trigger =
				sql_triggers_exist(space->def, TK_DELETE, NULL,
						   parse_context->sql_flags,
						   NULL);
			sql_generate_row_delete(parse_context, space, trigger,
						cursor, idx_key_reg, part_count,
						true,
						ON_CONFLICT_ACTION_REPLACE,
						ONEPASS_SINGLE, -1);
			sqlVdbeResolveLabel(v, skip_index);
		}
	}
}

void
vdbe_emit_insertion_completion(struct Vdbe *v, struct space *space,
			       int raw_data_reg, uint32_t tuple_len,
			       enum on_conflict_action on_conflict,
			       int autoinc_reg)
{
	assert(v != NULL);
	u16 pik_flags = OPFLAG_NCHANGE;
	SET_CONFLICT_FLAG(pik_flags, on_conflict);
	sqlVdbeAddOp3(v, OP_MakeRecord, raw_data_reg, tuple_len,
			  raw_data_reg + tuple_len);
	sqlVdbeAddOp3(v, OP_IdxInsert, raw_data_reg + tuple_len, 0,
		      autoinc_reg);
	sqlVdbeChangeP4(v, -1, (char *)space, P4_SPACEPTR);
	sqlVdbeChangeP5(v, pik_flags);
}

/**
 * Check to see if index @src is compatible as a source of data
 * for index @dest in an insert transfer optimization. The rules
 * for a compatible index:
 *
 * - The index is over the same set of columns;
 * - The same DESC and ASC markings occurs on all columns;
 * - The same collating sequence on each column.
 *
 * @param dest Index of destination space.
 * @param src Index of source space.
 *
 * @retval True, if two indexes are compatible in terms of
 *         xfer optimization.
 */
static bool
sql_index_is_xfer_compatible(const struct index_def *dest,
			     const struct index_def *src)
{
	assert(dest != NULL && src != NULL);
	assert(dest->space_id != src->space_id);
	return key_part_cmp(src->key_def->parts, src->key_def->part_count,
			    dest->key_def->parts,
			    dest->key_def->part_count) == 0;
}

/*
 * Attempt the transfer optimization on INSERTs of the form
 *
 *     INSERT INTO tab1 SELECT * FROM tab2;
 *
 * The xfer optimization transfers raw records from tab2 over to tab1.
 * Columns are not decoded and reassembled, which greatly improves
 * performance.  Raw index records are transferred in the same way.
 *
 * The xfer optimization is only attempted if tab1 and tab2 are compatible.
 * There are lots of rules for determining compatibility - see comments
 * embedded in the code for details.
 *
 * This routine returns TRUE if the optimization is guaranteed to be used.
 * Sometimes the xfer optimization will only work if the destination table
 * is empty - a factor that can only be determined at run-time.  In that
 * case, this routine generates code for the xfer optimization but also
 * does a test to see if the destination table is empty and jumps over the
 * xfer optimization code if the test fails.  In that case, this routine
 * returns FALSE so that the caller will know to go ahead and generate
 * an unoptimized transfer.  This routine also returns FALSE if there
 * is no chance that the xfer optimization can be applied.
 */
static int
xferOptimization(Parse * pParse,	/* Parser context */
		 struct space *dest,
		 Select * pSelect,	/* A SELECT statement to use as the data source */
		 int onError)		/* How to handle constraint errors */
{
	ExprList *pEList;	/* The result set of the SELECT */
	struct index *pSrcIdx, *pDestIdx;
	struct SrcList_item *pItem;	/* An element of pSelect->pSrc */
	int i;			/* Loop counter */
	int iSrc, iDest;	/* Cursors from source and destination */
	int addr1;		/* Loop addresses */
	int emptyDestTest = 0;	/* Address of test for empty pDest */
	int regData, regTupleid;	/* Registers holding data and tupleid */
	bool is_err_action_default = false;

	if (pSelect == NULL)
		return 0;	/* Must be of the form  INSERT INTO ... SELECT ... */
	if (pParse->pWith || pSelect->pWith) {
		/* Do not attempt to process this query if there are an WITH clauses
		 * attached to it. Proceeding may generate a false "no such table: xxx"
		 * error if pSelect reads from a CTE named "xxx".
		 */
		return 0;
	}
	/* The pDest must not have triggers. */
	if (dest->sql_triggers != NULL)
		return 0;
	if (onError == ON_CONFLICT_ACTION_DEFAULT) {
		onError = ON_CONFLICT_ACTION_ABORT;
		is_err_action_default = true;
	}
	assert(pSelect->pSrc);	/* allocated even if there is no FROM clause */
	if (pSelect->pSrc->nSrc != 1) {
		return 0;	/* FROM clause must have exactly one term */
	}
	if (pSelect->pSrc->a[0].pSelect) {
		return 0;	/* FROM clause cannot contain a subquery */
	}
	if (pSelect->pWhere) {
		return 0;	/* SELECT may not have a WHERE clause */
	}
	if (pSelect->pOrderBy) {
		return 0;	/* SELECT may not have an ORDER BY clause */
	}
	/* Do not need to test for a HAVING clause.  If HAVING is present but
	 * there is no ORDER BY, we will get an error.
	 */
	if (pSelect->pGroupBy) {
		return 0;	/* SELECT may not have a GROUP BY clause */
	}
	if (pSelect->pLimit) {
		return 0;	/* SELECT may not have a LIMIT clause */
	}
	assert(pSelect->pOffset == 0);	/* Must be so if pLimit==0 */
	if (pSelect->pPrior) {
		return 0;	/* SELECT may not be a compound query */
	}
	if (pSelect->selFlags & SF_Distinct) {
		return 0;	/* SELECT may not be DISTINCT */
	}
	pEList = pSelect->pEList;
	assert(pEList != 0);
	if (pEList->nExpr != 1) {
		return 0;	/* The result set must have exactly one column */
	}
	assert(pEList->a[0].pExpr);
	if (pEList->a[0].pExpr->op != TK_ASTERISK) {
		return 0;	/* The result set must be the special operator "*" */
	}

	/* At this point we have established that the statement is of the
	 * correct syntactic form to participate in this optimization.  Now
	 * we have to check the semantics.
	 */
	pItem = pSelect->pSrc->a;
	struct space *src = space_by_name(pItem->zName);
	/* FROM clause does not contain a real table. */
	if (src == NULL)
		return 0;
	/* Src and dest may not be the same table. */
	if (src->def->id == dest->def->id)
		return 0;
	/* Src may not be a view. */
	if (src->def->opts.is_view)
		return 0;
	/* Number of columns must be the same in src and dst. */
	if (dest->def->field_count != src->def->field_count)
		return 0;
	for (i = 0; i < (int)dest->def->field_count; i++) {
		enum field_type dest_type =
			dest->def->fields[i].type;
		enum field_type src_type =
			src->def->fields[i].type;
		/* Type must be the same on all columns. */
		if (dest_type != src_type)
			return 0;
		if (dest->def->fields[i].coll_id != src->def->fields[i].coll_id)
			return 0;
		if (!dest->def->fields[i].is_nullable &&
		    src->def->fields[i].is_nullable)
			return 0;
		/* Default values for second and subsequent columns need to match. */
		if (i > 0) {
			char *src_expr_str = src->def->fields[i].default_value;
			char *dest_expr_str =
				dest->def->fields[i].default_value;
			if ((dest_expr_str == NULL) != (src_expr_str == NULL) ||
			    (dest_expr_str &&
			     strcmp(src_expr_str, dest_expr_str) != 0)
			    ) {
				return 0;	/* Default values must be the same for all columns */
			}
		}
	}

	for (uint32_t i = 0; i < dest->index_count; ++i) {
		pDestIdx = dest->index[i];
		for (uint32_t j = 0; j < src->index_count; ++j) {
			pSrcIdx = src->index[j];
			if (sql_index_is_xfer_compatible(pDestIdx->def,
							 pSrcIdx->def))
				break;
		}
		/* pDestIdx has no corresponding index in pSrc. */
		if (pSrcIdx == NULL)
			return 0;
	}
	/*
	 * Dissallow the transfer optimization if the are check
	 * constraints.
	 */
	if (!rlist_empty(&dest->ck_constraint) ||
	    !rlist_empty(&src->ck_constraint))
		return 0;
	/* Disallow the transfer optimization if the destination table constains
	 * any foreign key constraints.  This is more restrictive than necessary.
	 * So the extra complication to make this rule less restrictive is probably
	 * not worth the effort.  Ticket [6284df89debdfa61db8073e062908af0c9b6118e]
	 */
	if (!rlist_empty(&dest->child_fk_constraint))
		return 0;

	/* If we get this far, it means that the xfer optimization is at
	 * least a possibility, though it might only work if the destination
	 * table (tab1) is initially empty.
	 */

	/* The Vdbe struct we're building. */
	struct Vdbe *v = sqlGetVdbe(pParse);
	iSrc = pParse->nTab++;
	iDest = pParse->nTab++;
	regData = sqlGetTempReg(pParse);
	regTupleid = sqlGetTempReg(pParse);

	vdbe_emit_open_cursor(pParse, iDest, 0, dest);
	VdbeComment((v, "%s", dest->def->name));

	/*
	 * Xfer optimization is unable to correctly insert data
	 * in case there's a conflict action other than *_ABORT,
	 * *_FAIL or *_IGNORE. This is the reason we want to only
	 * run it if the destination table is initially empty.
	 * That block generates code to make that determination.
	 */
	if (!(onError == ON_CONFLICT_ACTION_ABORT ||
	    onError == ON_CONFLICT_ACTION_FAIL ||
	    onError == ON_CONFLICT_ACTION_IGNORE) ||
	    is_err_action_default) {
		addr1 = sqlVdbeAddOp2(v, OP_Rewind, iDest, 0);
		VdbeCoverage(v);
		emptyDestTest = sqlVdbeAddOp0(v, OP_Goto);
		sqlVdbeJumpHere(v, addr1);
	}

	vdbe_emit_open_cursor(pParse, iSrc, 0, src);
	VdbeComment((v, "%s", src->def->name));
	addr1 = sqlVdbeAddOp2(v, OP_Rewind, iSrc, 0);
	VdbeCoverage(v);
	sqlVdbeAddOp2(v, OP_RowData, iSrc, regData);

#ifdef SQL_TEST
	sqlVdbeChangeP5(v, OPFLAG_XFER_OPT);
#endif

	sqlVdbeAddOp4(v, OP_IdxInsert, regData, 0, 0,
			  (char *)dest, P4_SPACEPTR);
	switch (onError) {
	case ON_CONFLICT_ACTION_IGNORE:
		sqlVdbeChangeP5(v, OPFLAG_OE_IGNORE | OPFLAG_NCHANGE);
		break;
	case ON_CONFLICT_ACTION_FAIL:
		sqlVdbeChangeP5(v, OPFLAG_OE_FAIL | OPFLAG_NCHANGE);
		break;
	default:
		sqlVdbeChangeP5(v, OPFLAG_NCHANGE);
		break;
	}
	sqlVdbeAddOp2(v, OP_Next, iSrc, addr1 + 1);
	VdbeCoverage(v);
	sqlVdbeJumpHere(v, addr1);
	sqlVdbeAddOp2(v, OP_Close, iSrc, 0);
	sqlVdbeAddOp2(v, OP_Close, iDest, 0);

	sqlReleaseTempReg(pParse, regTupleid);
	sqlReleaseTempReg(pParse, regData);
	if (emptyDestTest) {
		sqlVdbeAddOp2(v, OP_Halt, 0, 0);
		sqlVdbeJumpHere(v, emptyDestTest);
		sqlVdbeAddOp2(v, OP_Close, iDest, 0);
		return 0;
	} else {
		return 1;
	}
}
