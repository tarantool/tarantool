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
#include <box/coll.h>
#include <box/index.h>
#include <box/box.h>
#include <box/tuple.h>
#include "box/schema.h"
#include "sqliteInt.h"
#include "vdbeInt.h"
#include "box/session.h"

#if !defined(SQLITE_ENABLE_LOCKING_STYLE)
#if defined(__APPLE__)
#define SQLITE_ENABLE_LOCKING_STYLE 1
#else
#define SQLITE_ENABLE_LOCKING_STYLE 0
#endif
#endif

/*
 ************************************************************************
 * pragma.h contains several pragmas, including utf's pragmas.
 * All that is not utf-8 should be omitted
 ************************************************************************
 */

/***************************************************************************
 * The "pragma.h" include file is an automatically generated file that
 * that includes the PragType_XXXX macro definitions and the aPragmaName[]
 * object.  This ensures that the aPragmaName[] table is arranged in
 * lexicographical order to facility a binary search of the pragma name.
 * Do not edit pragma.h directly.  Edit and rerun the script in at
 * ../tool/mkpragmatab.tcl.
 */
#include "pragma.h"
#include "tarantoolInt.h"

/*
 * Interpret the given string as a safety level.  Return 0 for OFF,
 * 1 for ON or NORMAL, 2 for FULL, and 3 for EXTRA.  Return 1 for an empty or
 * unrecognized string argument.  The FULL and EXTRA option is disallowed
 * if the omitFull parameter it 1.
 *
 * Note that the values returned are one less that the values that
 * should be passed into sqlite3BtreeSetSafetyLevel().  The is done
 * to support legacy SQL code.  The safety level used to be boolean
 * and older scripts may have used numbers 0 for OFF and 1 for ON.
 */
static u8
getSafetyLevel(const char *z, int omitFull, u8 dflt)
{
	/* 123456789 123456789 123 */
	static const char zText[] = "onoffalseyestruextrafull";
	static const u8 iOffset[] = { 0, 1, 2, 4, 9, 12, 15, 20 };
	static const u8 iLength[] = { 2, 2, 3, 5, 3, 4, 5, 4 };
	static const u8 iValue[] = { 1, 0, 0, 0, 1, 1, 3, 2 };
	/* on no off false yes true extra full */
	int i, n;
	if (sqlite3Isdigit(*z)) {
		return (u8) sqlite3Atoi(z);
	}
	n = sqlite3Strlen30(z);
	for (i = 0; i < ArraySize(iLength); i++) {
		if (iLength[i] == n
		    && sqlite3StrNICmp(&zText[iOffset[i]], z, n) == 0
		    && (!omitFull || iValue[i] <= 1)
		    ) {
			return iValue[i];
		}
	}
	return dflt;
}

/*
 * Interpret the given string as a boolean value.
 */
u8
sqlite3GetBoolean(const char *z, u8 dflt)
{
	return getSafetyLevel(z, 1, dflt) != 0;
}

/* The sqlite3GetBoolean() function is used by other modules but the
 * remainder of this file is specific to PRAGMA processing.  So omit
 * the rest of the file if PRAGMAs are omitted from the build.
 */

#if !defined(SQLITE_OMIT_PRAGMA)

/*
 * Set result column names for a pragma.
 */
static void
setPragmaResultColumnNames(Vdbe * v,	/* The query under construction */
			   const PragmaName * pPragma	/* The pragma */
    )
{
	u8 n = pPragma->nPragCName;
	sqlite3VdbeSetNumCols(v, n == 0 ? 1 : n);
	if (n == 0) {
		sqlite3VdbeSetColName(v, 0, COLNAME_NAME, pPragma->zName,
				      SQLITE_STATIC);
	} else {
		int i, j;
		for (i = 0, j = pPragma->iPragCName; i < n; i++, j++) {
			sqlite3VdbeSetColName(v, i, COLNAME_NAME, pragCName[j],
					      SQLITE_STATIC);
		}
	}
}

/*
 * Generate code to return a single integer value.
 */
static void
returnSingleInt(Vdbe * v, i64 value)
{
	sqlite3VdbeAddOp4Dup8(v, OP_Int64, 0, 1, 0, (const u8 *)&value,
			      P4_INT64);
	sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 1);
}

/*
 * Return a human-readable name for a constraint resolution action.
 */
#ifndef SQLITE_OMIT_FOREIGN_KEY
static const char *
actionName(u8 action)
{
	const char *zName;
	switch (action) {
	case OE_SetNull:
		zName = "SET NULL";
		break;
	case OE_SetDflt:
		zName = "SET DEFAULT";
		break;
	case OE_Cascade:
		zName = "CASCADE";
		break;
	case OE_Restrict:
		zName = "RESTRICT";
		break;
	default:
		zName = "NO ACTION";
		assert(action == ON_CONFLICT_ACTION_NONE);
		break;
	}
	return zName;
}
#endif

/*
 * Locate a pragma in the aPragmaName[] array.
 */
static const PragmaName *
pragmaLocate(const char *zName)
{
	int upr, lwr, mid, rc;
	lwr = 0;
	upr = ArraySize(aPragmaName) - 1;
	while (lwr <= upr) {
		mid = (lwr + upr) / 2;
		rc = sqlite3_stricmp(zName, aPragmaName[mid].zName);
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

#ifdef PRINT_PRAGMA
#undef PRINT_PRAGMA
#endif
#define PRINT_PRAGMA(pragma_name, pragma_flag) do {			       \
	int nCoolSpaces = 30 - strlen(pragma_name);			       \
	if (user_session->sql_flags & (pragma_flag)) {			       \
		printf("%s %*c --  [true] \n", pragma_name, nCoolSpaces, ' '); \
	} else {							       \
		printf("%s %*c --  [false] \n", pragma_name, nCoolSpaces, ' ');\
	}								       \
} while (0)

static void
printActivePragmas(struct session *user_session)
{
	int i;
	for (i = 0; i < ArraySize(aPragmaName); ++i) {
		if (aPragmaName[i].ePragTyp == PragTyp_FLAG)
			PRINT_PRAGMA(aPragmaName[i].zName, aPragmaName[i].iArg);
	}

	printf("Other available pragmas: \n");
	for (i = 0; i < ArraySize(aPragmaName); ++i) {
		if (aPragmaName[i].ePragTyp != PragTyp_FLAG)
			printf("-- %s \n", aPragmaName[i].zName);
	}
}

/*
 * Process a pragma statement.
 *
 * Pragmas are of this form:
 *
 *      PRAGMA [schema.]id [= value]
 *
 * The identifier might also be a string.  The value is a string, and
 * identifier, or a number.  If minusFlag is true, then the value is
 * a number that was preceded by a minus sign.
 *
 * If the left side is "database.id" then pId1 is the database name
 * and pId2 is the id.  If the left side is just "id" then pId1 is the
 * id and pId2 is any empty string.
 */
void
sqlite3Pragma(Parse * pParse, Token * pId,	/* First part of [schema.]id field */
	      Token * pValue,	/* Token for <value>, or NULL */
	      Token * pValue2,	/* Token for <value2>, or NULL */
	      int minusFlag	/* True if a '-' sign preceded <value> */
    )
{
	char *zLeft = 0;	/* Nul-terminated UTF-8 string <id> */
	char *zRight = 0;	/* Nul-terminated UTF-8 string <value>, or NULL */
	char *zTable = 0;	/* Nul-terminated UTF-8 string <value2> or NULL */
	int rc;			/* return value form SQLITE_FCNTL_PRAGMA */
	sqlite3 *db = pParse->db;	/* The database connection */
	Vdbe *v = sqlite3GetVdbe(pParse);	/* Prepared statement */
	const PragmaName *pPragma;	/* The pragma */
	struct session *user_session = current_session();

	if (v == 0)
		return;
	sqlite3VdbeRunOnlyOnce(v);
	pParse->nMem = 2;

	zLeft = sqlite3NameFromToken(db, pId);
	if (!zLeft) {
		printActivePragmas(user_session);
		return;
	}

	if (minusFlag) {
		zRight = sqlite3MPrintf(db, "-%T", pValue);
	} else {
		zRight = sqlite3NameFromToken(db, pValue);
	}
	zTable = sqlite3NameFromToken(db, pValue2);
	db->busyHandler.nBusy = 0;

	/* Locate the pragma in the lookup table */
	pPragma = pragmaLocate(zLeft);
	if (pPragma == 0) {
		sqlite3ErrorMsg(pParse, "no such pragma: %s", zLeft);
		goto pragma_out;
	}

	/* Make sure the database schema is loaded if the pragma requires that */
	if ((pPragma->mPragFlg & PragFlg_NeedSchema) != 0) {
		assert(db->pSchema != NULL);
	}
	/* Register the result column names for pragmas that return results */
	if ((pPragma->mPragFlg & PragFlg_NoColumns) == 0
	    && ((pPragma->mPragFlg & PragFlg_NoColumns1) == 0 || zRight == 0)
	    ) {
		setPragmaResultColumnNames(v, pPragma);
	}
	/* Jump to the appropriate pragma handler */
	switch (pPragma->ePragTyp) {

#ifndef SQLITE_OMIT_FLAG_PRAGMAS
	case PragTyp_FLAG:{
			if (zRight == 0) {
				setPragmaResultColumnNames(v, pPragma);
				returnSingleInt(v,
						(user_session->
						 sql_flags & pPragma->iArg) !=
						0);
			} else {
				int mask = pPragma->iArg;	/* Mask of bits to set
								 * or clear.
								 */

				if (sqlite3GetBoolean(zRight, 0)) {
					user_session->sql_flags |= mask;
				} else {
					user_session->sql_flags &= ~mask;
					if (mask == SQLITE_DeferFKs) {
						v->nDeferredImmCons = 0;
					}
				}

				/* Many of the flag-pragmas modify the code
				 * generated by the SQL * compiler (eg.
				 * count_changes). So add an opcode to expire
				 * all * compiled SQL statements after
				 * modifying a pragma value.
				 */
				sqlite3VdbeAddOp0(v, OP_Expire);
			}
			break;
		}
#endif				/* SQLITE_OMIT_FLAG_PRAGMAS */

#ifndef SQLITE_OMIT_SCHEMA_PRAGMAS
		/* *   PRAGMA table_info(<table>) *
		 *
		 * Return a single row for each column of the named table. The
		 * columns of * the returned data set are: *
		 *
		 * cid:        Column id (numbered from left to right, starting at
		 * 0) * name:       Column name * type:       Column
		 * declaration type. * notnull:    True if 'NOT NULL' is part
		 * of column declaration * dflt_value: The default value for
		 * the column, if any.
		 */
	case PragTyp_TABLE_INFO:
		if (zRight) {
			Table *pTab;
			pTab = sqlite3LocateTable(pParse, LOCATE_NOERR, zRight);
			if (pTab) {
				int i, k;
				Column *pCol;
				Index *pPk = sqlite3PrimaryKeyIndex(pTab);
				pParse->nMem = 6;
				sqlite3ViewGetColumnNames(pParse, pTab);
				for (i = 0, pCol = pTab->aCol; i < pTab->nCol;
				     i++, pCol++) {
					if (!table_column_is_in_pk(pTab, i)) {
						k = 0;
					} else if (pPk == 0) {
						k = 1;
					} else {
						for (k = 1;
						     k <= pTab->nCol
						     && pPk->aiColumn[k - 1] !=
						     i; k++) {
						}
					}
					bool nullable = table_column_is_nullable(pTab, i);
					uint32_t space_id =
						SQLITE_PAGENO_TO_SPACEID(
							pTab->tnum);
					struct space *space =
						space_cache_find(space_id);
					char *expr_str = space->
						def->fields[i].default_value;
					sqlite3VdbeMultiLoad(v, 1, "issisi",
							     i, pCol->zName,
							     field_type_strs[
							     sqlite3ColumnType
							     (pCol)],
							     nullable == 0,
							     expr_str, k);
					sqlite3VdbeAddOp2(v, OP_ResultRow, 1,
							  6);
				}
			}
		}
		break;

	case PragTyp_STATS:{
			Index *pIdx;
			HashElem *i;
			pParse->nMem = 4;
			for (i = sqliteHashFirst(&db->pSchema->tblHash); i;
			     i = sqliteHashNext(i)) {
				Table *pTab = sqliteHashData(i);
				sqlite3VdbeMultiLoad(v, 1, "ssii",
						     pTab->zName,
						     0,
						     pTab->szTabRow,
						     pTab->nRowLogEst);
				sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 4);
				for (pIdx = pTab->pIndex; pIdx;
				     pIdx = pIdx->pNext) {
					sqlite3VdbeMultiLoad(v, 2, "sii",
							     pIdx->zName,
							     pIdx->szIdxRow,
							     pIdx->
							     aiRowLogEst[0]);
					sqlite3VdbeAddOp2(v, OP_ResultRow, 1,
							  4);
				}
			}
			break;
		}

	case PragTyp_INDEX_INFO:{
			if (zRight && zTable) {
				Index *pIdx;
				pIdx = sqlite3LocateIndex(db, zRight, zTable);
				if (pIdx) {
					int i;
					int mx;
					if (pPragma->iArg) {
						/* PRAGMA index_xinfo (newer
						 * version with more rows and
						 * columns)
						 */
						pParse->nMem = 6;
					} else {
						/* PRAGMA index_info (legacy
						 * version)
						 */
						pParse->nMem = 3;
					}
					mx = index_column_count(pIdx);
					assert(pParse->nMem <=
					       pPragma->nPragCName);
					for (i = 0; i < mx; i++) {
						i16 cnum = pIdx->aiColumn[i];
						assert(pIdx->pTable);
						sqlite3VdbeMultiLoad(v, 1,
								     "iis", i,
								     cnum,
								     cnum <
								     0 ? 0 :
								     pIdx->
								     pTable->
								     aCol[cnum].
								     zName);
						if (pPragma->iArg) {
							const char *c_n;
							struct coll *coll;
							coll = sql_index_collation(pIdx, i);
							if (coll != NULL)
								c_n = coll->name;
							else
								c_n = "BINARY";
							sqlite3VdbeMultiLoad(v,
									     4,
									     "isi",
									     pIdx->
									     aSortOrder
									     [i],
									     c_n,
									     i <
									     mx);
						}
						sqlite3VdbeAddOp2(v,
								  OP_ResultRow,
								  1,
								  pParse->nMem);
					}
				}
			}
			break;
		}
	case PragTyp_INDEX_LIST:{
			if (zRight) {
				Index *pIdx;
				Table *pTab;
				int i;
				pTab = sqlite3HashFind(&db->pSchema->tblHash,
						       zRight);
				if (pTab != NULL) {
					pParse->nMem = 5;
					for (pIdx = pTab->pIndex, i = 0; pIdx;
					     pIdx = pIdx->pNext, i++) {
						const char *azOrigin[] =
						    { "c", "u", "pk" };
						sqlite3VdbeMultiLoad(v, 1,
								     "isisi", i,
								     pIdx->
								     zName,
								     index_is_unique
								     (pIdx),
								     azOrigin
								     [pIdx->
								      idxType],
								     pIdx->
								     pPartIdxWhere
								     != 0);
						sqlite3VdbeAddOp2(v,
								  OP_ResultRow,
								  1, 5);
					}
				}
			}
			break;
		}

	case PragTyp_COLLATION_LIST:{
		int i = 0;
		uint32_t space_id;
		space_id = box_space_id_by_name("_collation",
						(uint32_t) strlen("_collation"));
		char key_buf[16]; /* 16 is enough to encode 0 len array */
		char *key_end = key_buf;
		key_end = mp_encode_array(key_end, 0);
		box_tuple_t *tuple;
		box_iterator_t* iter;
		iter = box_index_iterator(space_id, 0,ITER_ALL, key_buf, key_end);
		rc = box_iterator_next(iter, &tuple);
		assert(rc==0);
		for (i = 0; tuple!=NULL; i++, box_iterator_next(iter, &tuple)){
			/* 1 is name field number */
			const char *str = tuple_field_cstr(tuple, 1);
			assert(str != NULL);
			/* this procedure should reallocate and copy str */
			sqlite3VdbeMultiLoad(v, 1, "is", i, str);
			sqlite3VdbeAddOp2(v, OP_ResultRow, 1, 2);
		}
		box_iterator_free(iter);
		break;
	}
#endif				/* SQLITE_OMIT_SCHEMA_PRAGMAS */

#ifndef SQLITE_OMIT_FOREIGN_KEY
	case PragTyp_FOREIGN_KEY_LIST:{
			if (zRight) {
				FKey *pFK;
				Table *pTab;
				pTab = sqlite3HashFind(&db->pSchema->tblHash,
						       zRight);
				if (pTab != NULL) {
					pFK = pTab->pFKey;
					if (pFK) {
						int i = 0;
						pParse->nMem = 8;
						while (pFK) {
							int j;
							for (j = 0;
							     j < pFK->nCol;
							     j++) {
								sqlite3VdbeMultiLoad(v, 1, "iissssss", i, j, pFK->zTo, pTab->aCol[pFK->aCol[j].iFrom].zName, pFK->aCol[j].zCol, actionName(pFK->aAction[1]),	/* ON UPDATE */
										     actionName(pFK->aAction[0]),	/* ON DELETE */
										     "NONE");
								sqlite3VdbeAddOp2
								    (v,
								     OP_ResultRow,
								     1, 8);
							}
							++i;
							pFK = pFK->pNextFrom;
						}
					}
				}
			}
			break;
		}
#endif				/* !defined(SQLITE_OMIT_FOREIGN_KEY) */

#ifndef SQLITE_OMIT_FOREIGN_KEY
#ifndef SQLITE_OMIT_TRIGGER
	case PragTyp_FOREIGN_KEY_CHECK:{
			FKey *pFK;	/* A foreign key constraint */
			Table *pTab;	/* Child table contain "REFERENCES"
					 * keyword
					 */
			Table *pParent;	/* Parent table that child points to */
			Index *pIdx;	/* Index in the parent table */
			int i;	/* Loop counter:  Foreign key number for pTab */
			int j;	/* Loop counter:  Field of the foreign key */
			HashElem *k;	/* Loop counter:  Next table in schema */
			int x;	/* result variable */
			int regResult;	/* 3 registers to hold a result row */
			int regKey;	/* Register to hold key for checking
					 * the FK
					 */
			int regRow;	/* Registers to hold a row from pTab */
			int addrTop;	/* Top of a loop checking foreign keys */
			int addrOk;	/* Jump here if the key is OK */
			int *aiCols;	/* child to parent column mapping */

			regResult = pParse->nMem + 1;
			pParse->nMem += 4;
			regKey = ++pParse->nMem;
			regRow = ++pParse->nMem;
			k = sqliteHashFirst(&db->pSchema->tblHash);
			while (k) {
				if (zRight) {
					pTab =
					    sqlite3LocateTable(pParse, 0,
							       zRight);
					k = 0;
				} else {
					pTab = (Table *) sqliteHashData(k);
					k = sqliteHashNext(k);
				}
				if (pTab == 0 || pTab->pFKey == 0)
					continue;
				if (pTab->nCol + regRow > pParse->nMem)
					pParse->nMem = pTab->nCol + regRow;
				sqlite3OpenTable(pParse, 0, pTab, OP_OpenRead);
				sqlite3VdbeLoadString(v, regResult,
						      pTab->zName);
				for (i = 1, pFK = pTab->pFKey; pFK;
				     i++, pFK = pFK->pNextFrom) {
					pParent =
						sqlite3HashFind(&db->pSchema->tblHash,
								pFK->zTo);
					if (pParent == NULL)
						continue;
					pIdx = 0;
					x = sqlite3FkLocateIndex(pParse,
								 pParent, pFK,
								 &pIdx, 0);
					if (x == 0) {
						if (pIdx == 0) {
							sqlite3OpenTable(pParse,
									 i,
									 pParent,
									 OP_OpenRead);
						} else {
							sqlite3VdbeAddOp3(v,
									  OP_OpenRead,
									  i,
									  pIdx->
									  tnum,
									  0);
							sqlite3VdbeSetP4KeyInfo
							    (pParse, pIdx);
						}
					} else {
						k = 0;
						break;
					}
				}
				assert(pParse->nErr > 0 || pFK == 0);
				if (pFK)
					break;
				if (pParse->nTab < i)
					pParse->nTab = i;
				addrTop = sqlite3VdbeAddOp1(v, OP_Rewind, 0);
				VdbeCoverage(v);
				for (i = 1, pFK = pTab->pFKey; pFK;
				     i++, pFK = pFK->pNextFrom) {
					pParent =
						sqlite3HashFind(&db->pSchema->tblHash,
								pFK->zTo);
					pIdx = 0;
					aiCols = 0;
					if (pParent) {
						x = sqlite3FkLocateIndex(pParse,
									 pParent,
									 pFK,
									 &pIdx,
									 &aiCols);
						assert(x == 0);
					}
					addrOk = sqlite3VdbeMakeLabel(v);
					if (pParent && pIdx == 0) {
						int iKey = pFK->aCol[0].iFrom;
						assert(iKey >= 0
						       && iKey < pTab->nCol);
						if (iKey != pTab->iPKey) {
							sqlite3VdbeAddOp3(v,
									  OP_Column,
									  0,
									  iKey,
									  regRow);
							sqlite3ColumnDefault(v,
									     pTab,
									     iKey,
									     regRow);
							sqlite3VdbeAddOp2(v,
									  OP_IsNull,
									  regRow,
									  addrOk);
							VdbeCoverage(v);
						}
						VdbeCoverage(v);
						sqlite3VdbeGoto(v, addrOk);
						sqlite3VdbeJumpHere(v,
								    sqlite3VdbeCurrentAddr
								    (v) - 2);
					} else {
						for (j = 0; j < pFK->nCol; j++) {
							sqlite3ExprCodeGetColumnOfTable
							    (v, pTab, 0,
							     aiCols ? aiCols[j]
							     : pFK->aCol[j].
							     iFrom, regRow + j);
							sqlite3VdbeAddOp2(v,
									  OP_IsNull,
									  regRow
									  + j,
									  addrOk);
							VdbeCoverage(v);
						}
						if (pParent) {
							sqlite3VdbeAddOp4(v,
									  OP_MakeRecord,
									  regRow,
									  pFK->
									  nCol,
									  regKey,
									  sqlite3IndexAffinityStr
									  (db,
									   pIdx),
									  pFK->
									  nCol);
							sqlite3VdbeAddOp4Int(v,
									     OP_Found,
									     i,
									     addrOk,
									     regKey,
									     0);
							VdbeCoverage(v);
						}
					}
					sqlite3VdbeMultiLoad(v, regResult + 2,
							     "si", pFK->zTo,
							     i - 1);
					sqlite3VdbeAddOp2(v, OP_ResultRow,
							  regResult, 4);
					sqlite3VdbeResolveLabel(v, addrOk);
					sqlite3DbFree(db, aiCols);
				}
				sqlite3VdbeAddOp2(v, OP_Next, 0, addrTop + 1);
				VdbeCoverage(v);
				sqlite3VdbeJumpHere(v, addrTop);
			}
			break;
		}
#endif				/* !defined(SQLITE_OMIT_TRIGGER) */
#endif				/* !defined(SQLITE_OMIT_FOREIGN_KEY) */

#ifndef NDEBUG
	case PragTyp_PARSER_TRACE:{
			if (zRight) {
				if (sqlite3GetBoolean(zRight, 0)) {
					sqlite3ParserTrace(stdout, "parser: ");
				} else {
					sqlite3ParserTrace(0, 0);
				}
			}
			break;
		}
#endif

		/* Reinstall the LIKE and GLOB functions.  The variant of LIKE *
		 * used will be case sensitive or not depending on the RHS.
		 */
	case PragTyp_CASE_SENSITIVE_LIKE:{
			if (zRight) {
				sqlite3RegisterLikeFunctions(db,
							     sqlite3GetBoolean
							     (zRight, 0));
			}
			break;
		}

#ifndef SQLITE_INTEGRITY_CHECK_ERROR_MAX
#define SQLITE_INTEGRITY_CHECK_ERROR_MAX 100
#endif

#ifndef SQLITE_OMIT_SCHEMA_VERSION_PRAGMAS
		/* *   PRAGMA [schema.]schema_version *   PRAGMA
		 * [schema.]schema_version = <integer> *
		 *
		 * PRAGMA [schema.]user_version *   PRAGMA
		 * [schema.]user_version = <integer> *
		 *
		 * PRAGMA [schema.]freelist_count *
		 *
		 * PRAGMA [schema.]data_version *
		 *
		 * PRAGMA [schema.]application_id *   PRAGMA
		 * [schema.]application_id = <integer> *
		 *
		 * The pragma's schema_version and user_version are used
		 * to set or get * the value of the schema-version and
		 * user-version, respectively. Both * the
		 * schema-version and the user-version are 32-bit
		 * signed integers * stored in the database header. *
		 *
		 * The schema-cookie is usually only manipulated
		 * internally by SQLite. It * is incremented by SQLite
		 * whenever the database schema is modified (by *
		 * creating or dropping a table or index). The schema
		 * version is used by * SQLite each time a query is
		 * executed to ensure that the internal cache * of the
		 * schema used when compiling the SQL query matches the
		 * schema of * the database against which the compiled
		 * query is actually executed. * Subverting this
		 * mechanism by using "PRAGMA schema_version" to modify *
		 * the schema-version is potentially dangerous and may
		 * lead to program * crashes or database corruption.
		 * Use with caution! *
		 *
		 * The user-version is not used internally by SQLite. It
		 * may be used by * applications for any purpose.
		 */
	case PragTyp_HEADER_VALUE:{
			int iCookie = pPragma->iArg;	/* Which cookie to read
							 * or write
							 */
			if (zRight
			    && (pPragma->mPragFlg & PragFlg_ReadOnly) == 0) {
				/* Write the specified cookie value */
				static const VdbeOpList setCookie[] = {
					{OP_SetCookie, 0, 0, 0},	/* 1 */
				};
				VdbeOp *aOp;
				sqlite3VdbeVerifyNoMallocRequired(v,
								  ArraySize
								  (setCookie));
				aOp =
				    sqlite3VdbeAddOpList(v,
							 ArraySize(setCookie),
							 setCookie, 0);
				if (ONLY_IF_REALLOC_STRESS(aOp == 0))
					break;
				aOp[0].p1 = 0;
				aOp[0].p2 = iCookie;
				aOp[0].p3 = sqlite3Atoi(zRight);
			} else {
				/* Read the specified cookie value */
				static const VdbeOpList readCookie[] = {
					{OP_ReadCookie, 0, 1, 0},	/* 1 */
					{OP_ResultRow, 1, 1, 0}
				};
				VdbeOp *aOp;
				sqlite3VdbeVerifyNoMallocRequired(v,
								  ArraySize
								  (readCookie));
				aOp =
				    sqlite3VdbeAddOpList(v,
							 ArraySize(readCookie),
							 readCookie, 0);
				if (ONLY_IF_REALLOC_STRESS(aOp == 0))
					break;
				aOp[0].p1 = 0;
				aOp[1].p1 = 0;
				aOp[1].p3 = iCookie;
				sqlite3VdbeReusable(v);
			}
			break;
		}

		/* *   PRAGMA busy_timeout *   PRAGMA busy_timeout = N *
		 *
		 * Call sqlite3_busy_timeout(db, N).  Return the current
		 * timeout value * if one is set.  If no busy handler
		 * or a different busy handler is set * then 0 is
		 * returned.  Setting the busy_timeout to 0 or negative *
		 * disables the timeout.
		 */
		/* case PragTyp_BUSY_TIMEOUT */
	default:{
			assert(pPragma->ePragTyp == PragTyp_BUSY_TIMEOUT);
			if (zRight) {
				sqlite3_busy_timeout(db, sqlite3Atoi(zRight));
			}
			returnSingleInt(v, db->busyTimeout);
			break;
		}
	}			/* End of the PRAGMA switch */

	/* The following block is a no-op unless SQLITE_DEBUG is
	 * defined. Its only * purpose is to execute assert()
	 * statements to verify that if the * PragFlg_NoColumns1 flag
	 * is set and the caller specified an argument * to the PRAGMA,
	 * the implementation has not added any OP_ResultRow *
	 * instructions to the VM.
	 */
	if ((pPragma->mPragFlg & PragFlg_NoColumns1) && zRight) {
		sqlite3VdbeVerifyNoResultRow(v);
	}
 pragma_out:
	sqlite3DbFree(db, zLeft);
	sqlite3DbFree(db, zRight);
	sqlite3DbFree(db, zTable);
}

#endif				/* SQLITE_OMIT_PRAGMA */
#endif
