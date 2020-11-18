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
 * to handle SELECT statements in sql.
 */
#include "coll/coll.h"
#include "sqlInt.h"
#include "tarantoolInt.h"
#include "vdbeInt.h"
#include "box/box.h"
#include "box/coll_id_cache.h"
#include "box/schema.h"

/*
 * Trace output macros
 */
#ifdef SQL_DEBUG
/***/ int sqlSelectTrace = 0;
#define SELECTTRACE(K,P,S,X)  \
  if(sqlSelectTrace&(K))   \
    sqlDebugPrintf("%*s%s.%p: ",(P)->nSelectIndent*2-2,"",\
        (S)->zSelName,(S)),\
    sqlDebugPrintf X
#else
#define SELECTTRACE(K,P,S,X)
#endif

/*
 * An instance of the following object is used to record information about
 * how to process the DISTINCT keyword, to simplify passing that information
 * into the selectInnerLoop() routine.
 */
typedef struct DistinctCtx DistinctCtx;
struct DistinctCtx {
	u8 isTnct;		/* True if the DISTINCT keyword is present */
	u8 eTnctType;		/* One of the WHERE_DISTINCT_* operators */
	/**
	 * Ephemeral table's cursor used for DISTINCT processing.
	 * It is used for reading from ephemeral space.
	 */
	int cur_eph;
	/**
	 * Register, containing a pointer to ephemeral space.
	 * It is used for insertions while procesing DISTINCT.
	 */
	int reg_eph;
	int addrTnct;		/* Address of OP_OpenEphemeral opcode for cur_eph */
};

/*
 * An instance of the following object is used to record information about
 * the ORDER BY (or GROUP BY) clause of query is being coded.
 */
typedef struct SortCtx SortCtx;
struct SortCtx {
	ExprList *pOrderBy;	/* The ORDER BY (or GROUP BY clause) */
	int nOBSat;		/* Number of ORDER BY terms satisfied by indices */
	int iECursor;		/* Cursor number for the sorter */
	/** Register, containing pointer to ephemeral space. */
	int reg_eph;
	int regReturn;		/* Register holding block-output return address */
	int labelBkOut;		/* Start label for the block-output subroutine */
	int addrSortIndex;	/* Address of the OP_SorterOpen or OP_OpenEphemeral */
	int labelDone;		/* Jump here when done, ex: LIMIT reached */
	u8 sortFlags;		/* Zero or more SORTFLAG_* bits */
	u8 bOrderedInnerLoop;	/* ORDER BY correctly sorts the inner loop */
};
#define SORTFLAG_UseSorter  0x01	/* Use SorterOpen instead of OpenEphemeral */
#define SORTFLAG_DESC 0xF0

/*
 * Delete all the content of a Select structure.  Deallocate the structure
 * itself only if bFree is true.
 */
static void
clearSelect(sql * db, Select * p, int bFree)
{
	while (p) {
		Select *pPrior = p->pPrior;
		sql_expr_list_delete(db, p->pEList);
		sqlSrcListDelete(db, p->pSrc);
		sql_expr_delete(db, p->pWhere, false);
		sql_expr_list_delete(db, p->pGroupBy);
		sql_expr_delete(db, p->pHaving, false);
		sql_expr_list_delete(db, p->pOrderBy);
		sql_expr_delete(db, p->pLimit, false);
		sql_expr_delete(db, p->pOffset, false);
		if (p->pWith)
			sqlWithDelete(db, p->pWith);
		if (bFree)
			sqlDbFree(db, p);
		p = pPrior;
		bFree = 1;
	}
}

/*
 * Initialize a SelectDest structure.
 */
void
sqlSelectDestInit(SelectDest * pDest, int eDest, int iParm, int reg_eph)
{
	pDest->eDest = (u8) eDest;
	pDest->iSDParm = iParm;
	pDest->reg_eph = reg_eph;
	pDest->dest_type = NULL;
	pDest->iSdst = 0;
	pDest->nSdst = 0;
}

/*
 * Allocate a new Select structure and return a pointer to that
 * structure.
 */
Select *
sqlSelectNew(Parse * pParse,	/* Parsing context */
		 ExprList * pEList,	/* which columns to include in the result */
		 SrcList * pSrc,	/* the FROM clause -- which tables to scan */
		 Expr * pWhere,		/* the WHERE clause */
		 ExprList * pGroupBy,	/* the GROUP BY clause */
		 Expr * pHaving,	/* the HAVING clause */
		 ExprList * pOrderBy,	/* the ORDER BY clause */
		 u32 selFlags,		/* Flag parameters, such as SF_Distinct */
		 Expr * pLimit,		/* LIMIT value.  NULL means not used */
		 Expr * pOffset)	/* OFFSET value.  NULL means no offset */
{
	Select standin;
	sql *db = pParse->db;
	if (pEList == 0) {
		struct Expr *expr = sql_expr_new_anon(db, TK_ASTERISK);
		if (expr == NULL)
			pParse->is_aborted = true;
		pEList = sql_expr_list_append(db, NULL, expr);
	}
	standin.pEList = pEList;
	standin.op = TK_SELECT;
	standin.selFlags = selFlags;
	standin.iLimit = 0;
	standin.iOffset = 0;
#ifdef SQL_DEBUG
	standin.zSelName[0] = 0;
	if ((pParse->sql_flags & SQL_SelectTrace) != 0)
		sqlSelectTrace = 0xfff;
	else
		sqlSelectTrace = 0;
#endif
	standin.addrOpenEphm[0] = -1;
	standin.addrOpenEphm[1] = -1;
	standin.nSelectRow = 0;
	if (pSrc == 0)
		pSrc = sqlDbMallocZero(db, sizeof(*pSrc));
	standin.pSrc = pSrc;
	standin.pWhere = pWhere;
	standin.pGroupBy = pGroupBy;
	standin.pHaving = pHaving;
	standin.pOrderBy = pOrderBy;
	standin.pPrior = 0;
	standin.pNext = 0;
	standin.pLimit = pLimit;
	standin.pOffset = pOffset;
	standin.pWith = 0;
	assert(pOffset == 0 || pLimit != 0 || pParse->is_aborted
	       || db->mallocFailed != 0);
	Select *pNew = sqlDbMallocRawNN(db, sizeof(*pNew));
	if (db->mallocFailed) {
		clearSelect(db, &standin, 0);
		if (pNew != NULL)
			sqlDbFree(db, pNew);
		return NULL;
	}
	assert(standin.pSrc != 0 || pParse->is_aborted);
	memcpy(pNew, &standin, sizeof(standin));
	return pNew;
}

#ifdef SQL_DEBUG
/*
 * Set the name of a Select object
 */
void
sqlSelectSetName(Select * p, const char *zName)
{
	if (p && zName) {
		sql_snprintf(sizeof(p->zSelName), p->zSelName, "%s", zName);
	}
}
#endif

void
sql_select_delete(sql *db, Select *p)
{
	if (p)
		clearSelect(db, p, 1);
}

int
sql_src_list_entry_count(const struct SrcList *list)
{
	assert(list != NULL);
	return list->nSrc;
}

const char *
sql_src_list_entry_name(const struct SrcList *list, int i)
{
	assert(list != NULL);
	assert(i >= 0 && i < list->nSrc);
	return list->a[i].zName;
}

/*
 * Return a pointer to the right-most SELECT statement in a compound.
 */
static Select *
findRightmost(Select * p)
{
	while (p->pNext)
		p = p->pNext;
	return p;
}


/**
 * Work the same as sql_src_list_append(), but before adding to
 * list provide check on name duplicates: only values with unique
 * names are appended. Moreover, names of tables are not
 * normalized: it is parser's business and in struct Select they
 * are already in uppercased or unquoted form.
 *
 * @param db Database handler.
 * @param list List of entries.
 * @param new_name Name of entity to be added.
 * @retval @list with new element on success, old one otherwise.
 */
static struct SrcList *
src_list_append_unique(struct sql *db, struct SrcList *list,
		       const char *new_name)
{
	assert(list != NULL);
	assert(new_name != NULL);

	for (int i = 0; i < list->nSrc; ++i) {
		const char *name = list->a[i].zName;
		if (name != NULL && strcmp(new_name, name) == 0)
			return list;
	}
	struct SrcList *new_list =
		sql_src_list_enlarge(db, list, 1, list->nSrc);
	if (new_list == NULL) {
		sqlSrcListDelete(db, list);
		return NULL;
	}
	list = new_list;
	struct SrcList_item *pItem = &list->a[list->nSrc - 1];
	pItem->zName = sqlDbStrNDup(db, new_name, strlen(new_name));
	if (pItem->zName == NULL) {
		diag_set(OutOfMemory, strlen(new_name), "sqlDbStrNDup",
			 "pItem->zName");
		sqlSrcListDelete(db, list);
		return NULL;
	}
	return list;
}

static int
select_collect_table_names(struct Walker *walker, struct Select *select)
{
	assert(walker != NULL);
	assert(select != NULL);
	for (int i = 0; i < select->pSrc->nSrc; ++i) {
		if (select->pSrc->a[i].zName == NULL)
			continue;
		walker->u.pSrcList =
			src_list_append_unique(sql_get(), walker->u.pSrcList,
					       select->pSrc->a[i].zName);
		if (walker->u.pSrcList == NULL)
			return WRC_Abort;
	}
	return WRC_Continue;
}

struct SrcList *
sql_select_expand_from_tables(struct Select *select)
{
	assert(select != NULL);
	struct Walker walker;
	struct SrcList *table_names = sql_src_list_new(sql_get());
	if (table_names == NULL)
		return NULL;
	memset(&walker, 0, sizeof(walker));
	walker.xExprCallback = sqlExprWalkNoop;
	walker.xSelectCallback = select_collect_table_names;
	walker.u.pSrcList = table_names;
	if (sqlWalkSelect(&walker, select) != 0) {
		sqlSrcListDelete(sql_get(), walker.u.pSrcList);
		return NULL;
	}
	return walker.u.pSrcList;
}

bool
sql_select_constains_cte(struct Select *select, const char *name)
{
	assert(select != NULL && name != NULL);
	struct With *with = select->pWith;
	if (with != NULL) {
		for (int i = 0; i < with->nCte; i++) {
			const struct Cte *cte = &with->a[i];
			/*
			 * Don't use recursive call for
			 * cte->pSelect, because this function is
			 * used during view creation. Consider
			 * the nested <WITH>s query schema:
			 * CREATE VIEW v AS
			 *     WITH w AS (
			 *         WITH w_nested AS
			 *             (...)
			 *         SELECT ...)
			 *     SELECT ... FROM ...;
			 * The use of CTE "w_nested" after the
			 * external select's <FROM> is disallowed.
			 * So, it is pointless to check <WITH>,
			 * which is nested to other <WITH>.
			 */
			if (memcmp(name, cte->zName, strlen(name)) == 0)
				return true;
		}
	}
	struct SrcList *list = select->pSrc;
	int item_count = sql_src_list_entry_count(list);
	for (int i = 0; i < item_count; ++i) {
		if (list->a[i].pSelect != NULL) {
			if (sql_select_constains_cte(list->a[i].pSelect,
							 name))
				return true;
		}
	}
	return false;
}

/*
 * Given 1 to 3 identifiers preceding the JOIN keyword, determine the
 * type of join.  Return an integer constant that expresses that type
 * in terms of the following bit values:
 *
 *     JT_INNER
 *     JT_CROSS
 *     JT_OUTER
 *     JT_NATURAL
 *     JT_LEFT
 *     JT_RIGHT
 *
 * A full outer join is the combination of JT_LEFT and JT_RIGHT.
 *
 * If an illegal or unsupported join type is seen, then still return
 * a join type, but put an error in the pParse structure.
 */
int
sqlJoinType(Parse * pParse, Token * pA, Token * pB, Token * pC)
{
	int jointype = 0;
	Token *apAll[3];
	Token *p;
	/*   0123456789 123456789 123456789 123 */
	static const char zKeyText[] = "naturaleftouterightfullinnercross";
	static const struct {
		u8 i;		/* Beginning of keyword text in zKeyText[] */
		u8 nChar;	/* Length of the keyword in characters */
		u8 code;	/* Join type mask */
	} aKeyword[] = {
		/* natural */  {
		0, 7, JT_NATURAL},
		    /* left    */  {
		6, 4, JT_LEFT | JT_OUTER},
		    /* outer   */  {
		10, 5, JT_OUTER},
		    /* right   */  {
		14, 5, JT_RIGHT | JT_OUTER},
		    /* full    */  {
		19, 4, JT_LEFT | JT_RIGHT | JT_OUTER},
		    /* inner   */  {
		23, 5, JT_INNER},
		    /* cross   */  {
	28, 5, JT_INNER | JT_CROSS},};
	int i, j;
	apAll[0] = pA;
	apAll[1] = pB;
	apAll[2] = pC;
	for (i = 0; i < 3 && apAll[i]; i++) {
		p = apAll[i];
		for (j = 0; j < ArraySize(aKeyword); j++) {
			if (p->n == aKeyword[j].nChar
			    && sqlStrNICmp((char *)p->z,
					       &zKeyText[aKeyword[j].i],
					       p->n) == 0) {
				jointype |= aKeyword[j].code;
				break;
			}
		}
		testcase(j == 0 || j == 1 || j == 2 || j == 3 || j == 4
			 || j == 5 || j == 6);
		if (j >= ArraySize(aKeyword)) {
			jointype |= JT_ERROR;
			break;
		}
	}
	if ((jointype & (JT_INNER | JT_OUTER)) == (JT_INNER | JT_OUTER) ||
	    (jointype & JT_ERROR) != 0) {
		assert(pB != 0);
		const char *err;
		if (pC == NULL) {
			err = tt_sprintf("unknown or unsupported join type: "\
					 "%.*s %.*s", pA->n, pA->z, pB->n,
					 pB->z);
		} else {
			err = tt_sprintf("unknown or unsupported join type: "\
					 "%.*s %.*s %.*s", pA->n, pA->z, pB->n,
					 pB->z, pC->n, pC->z);
		}
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, err);
		pParse->is_aborted = true;
		jointype = JT_INNER;
	} else if ((jointype & JT_OUTER) != 0
		   && (jointype & (JT_LEFT | JT_RIGHT)) != JT_LEFT) {
		diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
			 "RIGHT and FULL OUTER JOINs");
		pParse->is_aborted = true;
		jointype = JT_INNER;
	}
	return jointype;
}

/*
 * Return the index of a column in a table.  Return -1 if the column
 * is not contained in the table.
 */
static int
columnIndex(struct space_def *def, const char *zCol)
{
	for (uint32_t i = 0; i < def->field_count; i++) {
		if (strcmp(def->fields[i].name, zCol) == 0)
			return i;
	}
	return -1;
}

/*
 * Search the first N tables in pSrc, from left to right, looking for a
 * table that has a column named zCol.
 *
 * When found, set *piTab and *piCol to the table index and column index
 * of the matching column and return TRUE.
 *
 * If not found, return FALSE.
 */
static int
tableAndColumnIndex(SrcList * pSrc,	/* Array of tables to search */
		    int N,		/* Number of tables in pSrc->a[] to search */
		    const char *zCol,	/* Name of the column we are looking for */
		    int *piTab,		/* Write index of pSrc->a[] here */
		    int *piCol)		/* Write index of pSrc->a[*piTab].pTab->aCol[] here */
{
	int i;			/* For looping over tables in pSrc */
	int iCol;		/* Index of column matching zCol */

	assert((piTab == 0) == (piCol == 0));	/* Both or neither are NULL */
	for (i = 0; i < N; i++) {
		iCol = columnIndex(pSrc->a[i].space->def, zCol);
		if (iCol >= 0) {
			if (piTab) {
				*piTab = i;
				*piCol = iCol;
			}
			return 1;
		}
	}
	return 0;
}

/*
 * This function is used to add terms implied by JOIN syntax to the
 * WHERE clause expression of a SELECT statement. The new term, which
 * is ANDed with the existing WHERE clause, is of the form:
 *
 *    (tab1.col1 = tab2.col2)
 *
 * where tab1 is the iSrc'th table in SrcList pSrc and tab2 is the
 * (iSrc+1)'th. Column col1 is column iColLeft of tab1, and col2 is
 * column iColRight of tab2.
 */
static void
addWhereTerm(Parse * pParse,	/* Parsing context */
	     SrcList * pSrc,	/* List of tables in FROM clause */
	     int iLeft,		/* Index of first table to join in pSrc */
	     int iColLeft,	/* Index of column in first table */
	     int iRight,	/* Index of second table in pSrc */
	     int iColRight,	/* Index of column in second table */
	     int isOuterJoin,	/* True if this is an OUTER join */
	     Expr ** ppWhere)	/* IN/OUT: The WHERE clause to add to */
{
	struct sql *db = pParse->db;
	Expr *pEq;

	assert(iLeft < iRight);
	assert(pSrc->nSrc > iRight);
	assert(pSrc->a[iLeft].space != NULL);
	assert(pSrc->a[iRight].space != NULL);

	struct Expr *pE1 = sql_expr_new_column(db, pSrc, iLeft, iColLeft);
	struct Expr *pE2 = sql_expr_new_column(db, pSrc, iRight, iColRight);
	if (pE1 == NULL || pE2 == NULL)
		pParse->is_aborted = true;
	pEq = sqlPExpr(pParse, TK_EQ, pE1, pE2);
	if (pEq && isOuterJoin) {
		ExprSetProperty(pEq, EP_FromJoin);
		assert(!ExprHasProperty(pEq, EP_TokenOnly | EP_Reduced));
		ExprSetVVAProperty(pEq, EP_NoReduce);
		pEq->iRightJoinTable = (i16) pE2->iTable;
	}
	*ppWhere = sql_and_expr_new(db, *ppWhere, pEq);
	if (*ppWhere == NULL)
		pParse->is_aborted = true;
}

/*
 * Set the EP_FromJoin property on all terms of the given expression.
 * And set the Expr.iRightJoinTable to iTable for every term in the
 * expression.
 *
 * The EP_FromJoin property is used on terms of an expression to tell
 * the LEFT OUTER JOIN processing logic that this term is part of the
 * join restriction specified in the ON or USING clause and not a part
 * of the more general WHERE clause.  These terms are moved over to the
 * WHERE clause during join processing but we need to remember that they
 * originated in the ON or USING clause.
 *
 * The Expr.iRightJoinTable tells the WHERE clause processing that the
 * expression depends on table iRightJoinTable even if that table is not
 * explicitly mentioned in the expression.  That information is needed
 * for cases like this:
 *
 *    SELECT * FROM t1 LEFT JOIN t2 ON t1.a=t2.b AND t1.x=5
 *
 * The where clause needs to defer the handling of the t1.x=5
 * term until after the t2 loop of the join.  In that way, a
 * NULL t2 row will be inserted whenever t1.x!=5.  If we do not
 * defer the handling of t1.x=5, it will be processed immediately
 * after the t1 loop and rows with t1.x!=5 will never appear in
 * the output, which is incorrect.
 */
static void
setJoinExpr(Expr * p, int iTable)
{
	while (p) {
		ExprSetProperty(p, EP_FromJoin);
		assert(!ExprHasProperty(p, EP_TokenOnly | EP_Reduced));
		ExprSetVVAProperty(p, EP_NoReduce);
		p->iRightJoinTable = (i16) iTable;
		if (p->op == TK_FUNCTION && p->x.pList) {
			int i;
			for (i = 0; i < p->x.pList->nExpr; i++) {
				setJoinExpr(p->x.pList->a[i].pExpr, iTable);
			}
		}
		setJoinExpr(p->pLeft, iTable);
		p = p->pRight;
	}
}

/*
 * This routine processes the join information for a SELECT statement.
 * ON and USING clauses are converted into extra terms of the WHERE clause.
 * NATURAL joins also create extra WHERE clause terms.
 *
 * The terms of a FROM clause are contained in the Select.pSrc structure.
 * The left most table is the first entry in Select.pSrc.  The right-most
 * table is the last entry.  The join operator is held in the entry to
 * the left.  Thus entry 0 contains the join operator for the join between
 * entries 0 and 1.  Any ON or USING clauses associated with the join are
 * also attached to the left entry.
 *
 * This routine returns the number of errors encountered.
 */
static int
sqlProcessJoin(Parse * pParse, Select * p)
{
	SrcList *pSrc;		/* All tables in the FROM clause */
	int i, j;		/* Loop counters */
	struct SrcList_item *pLeft;	/* Left table being joined */
	struct SrcList_item *pRight;	/* Right table being joined */

	pSrc = p->pSrc;
	pLeft = &pSrc->a[0];
	pRight = &pLeft[1];
	for (i = 0; i < pSrc->nSrc - 1; i++, pRight++, pLeft++) {
		struct space *left_space = pLeft->space;
		struct space *right_space = pRight->space;
		int isOuter;

		if (NEVER(left_space == NULL || right_space == NULL))
			continue;
		isOuter = (pRight->fg.jointype & JT_OUTER) != 0;

		/* When the NATURAL keyword is present, add WHERE clause terms for
		 * every column that the two tables have in common.
		 */
		if (pRight->fg.jointype & JT_NATURAL) {
			if (pRight->pOn || pRight->pUsing) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 "a NATURAL join may not have "
					 "an ON or USING clause");
				pParse->is_aborted = true;
				return 1;
			}
			for (j = 0; j < (int)right_space->def->field_count; j++) {
				char *zName;	/* Name of column in the right table */
				int iLeft;	/* Matching left table */
				int iLeftCol;	/* Matching column in the left table */

				zName = right_space->def->fields[j].name;
				if (tableAndColumnIndex
				    (pSrc, i + 1, zName, &iLeft, &iLeftCol)) {
					addWhereTerm(pParse, pSrc, iLeft,
						     iLeftCol, i + 1, j,
						     isOuter, &p->pWhere);
				}
			}
		}

		/* Disallow both ON and USING clauses in the same join
		 */
		if (pRight->pOn && pRight->pUsing) {
			diag_set(ClientError, ER_SQL_PARSER_GENERIC,
				 "cannot have both ON and USING clauses in "\
				 "the same join");
			pParse->is_aborted = true;
			return 1;
		}

		/* Add the ON clause to the end of the WHERE clause, connected by
		 * an AND operator.
		 */
		if (pRight->pOn) {
			if (isOuter)
				setJoinExpr(pRight->pOn, pRight->iCursor);
			p->pWhere = sql_and_expr_new(pParse->db, p->pWhere,
						     pRight->pOn);
			if (p->pWhere == NULL)
				pParse->is_aborted = true;
			pRight->pOn = 0;
		}

		/* Create extra terms on the WHERE clause for each column named
		 * in the USING clause.  Example: If the two tables to be joined are
		 * A and B and the USING clause names X, Y, and Z, then add this
		 * to the WHERE clause:    A.X=B.X AND A.Y=B.Y AND A.Z=B.Z
		 * Report an error if any column mentioned in the USING clause is
		 * not contained in both tables to be joined.
		 */
		if (pRight->pUsing) {
			const char *err = "cannot join using column %s - "\
					  "column not present in both tables";
			IdList *pList = pRight->pUsing;
			for (j = 0; j < pList->nId; j++) {
				char *zName;	/* Name of the term in the USING clause */
				int iLeft;	/* Table on the left with matching column name */
				int iLeftCol;	/* Column number of matching column on the left */
				int iRightCol;	/* Column number of matching column on the right */

				zName = pList->a[j].zName;
				iRightCol = columnIndex(right_space->def, zName);
				if (iRightCol < 0
				    || !tableAndColumnIndex(pSrc, i + 1, zName,
							    &iLeft, &iLeftCol)
				    ) {
					err = tt_sprintf(err, zName);
					diag_set(ClientError,
						 ER_SQL_PARSER_GENERIC, err);
					pParse->is_aborted = true;
					return 1;
				}
				addWhereTerm(pParse, pSrc, iLeft, iLeftCol,
					     i + 1, iRightCol, isOuter,
					     &p->pWhere);
			}
		}
	}
	return 0;
}

/**
 * Given an expression list, generate a key_info structure that
 * records the collating sequence for each expression in that
 * expression list.
 *
 * If the ExprList is an ORDER BY or GROUP BY clause then the
 * resulting key_info structure is appropriate for initializing
 * a virtual index to implement that clause.  If the ExprList is
 * the result set of a SELECT then the key_info structure is
 * appropriate for initializing a virtual index to implement a
 * DISTINCT test.
 *
 * Space to hold the key_info structure is obtained from malloc.
 * The calling function is responsible for seeing that this
 * structure is eventually freed.
 *
 * @param parse Parsing context.
 * @param list Expression list.
 * @param start No of leading parts to skip.
 *
 * @retval Allocated key_info, NULL in case of OOM.
 */
static struct sql_key_info *
sql_expr_list_to_key_info(struct Parse *parse, struct ExprList *list, int start);


/*
 * Generate code that will push the record in registers regData
 * through regData+nData-1 onto the sorter.
 */
static void
pushOntoSorter(Parse * pParse,		/* Parser context */
	       SortCtx * pSort,		/* Information about the ORDER BY clause */
	       Select * pSelect,	/* The whole SELECT statement */
	       int regData,		/* First register holding data to be sorted */
	       int regOrigData,		/* First register holding data before packing */
	       int nData,		/* Number of elements in the data array */
	       int nPrefixReg)		/* No. of reg prior to regData available for use */
{
	Vdbe *v = pParse->pVdbe;	/* Stmt under construction */
	int bSeq = ((pSort->sortFlags & SORTFLAG_UseSorter) == 0);
	int nExpr = pSort->pOrderBy->nExpr;	/* No. of ORDER BY terms */
	int nBase = nExpr + bSeq + nData;	/* Fields in sorter record */
	int regBase;		/* Regs for sorter record */
	int regRecord = ++pParse->nMem;	/* Assembled sorter record */
	int nOBSat = pSort->nOBSat;	/* ORDER BY terms to skip */
	int iLimit;		/* LIMIT counter */

	assert(bSeq == 0 || bSeq == 1);
	assert(nData == 1 || regData == regOrigData || regOrigData == 0);
	if (nPrefixReg) {
		assert(nPrefixReg == nExpr + bSeq);
		regBase = regData - nExpr - bSeq;
	} else {
		regBase = pParse->nMem + 1;
		pParse->nMem += nBase;
	}
	assert(pSelect->iOffset == 0 || pSelect->iLimit != 0);
	iLimit = pSelect->iOffset ? pSelect->iOffset + 1 : pSelect->iLimit;
	pSort->labelDone = sqlVdbeMakeLabel(v);
	sqlExprCodeExprList(pParse, pSort->pOrderBy, regBase, regOrigData,
				SQL_ECEL_DUP | (regOrigData ? SQL_ECEL_REF
						   : 0));
	if (bSeq) {
		sqlVdbeAddOp2(v, OP_Sequence, pSort->iECursor,
				  regBase + nExpr);
	}
	if (nPrefixReg == 0 && nData > 0) {
		sqlExprCodeMove(pParse, regData, regBase + nExpr + bSeq,
				    nData);
	}
	sqlVdbeAddOp3(v, OP_MakeRecord, regBase + nOBSat, nBase - nOBSat,
			  regRecord);
	if (nOBSat > 0) {
		int regPrevKey;	/* The first nOBSat columns of the previous row */
		int addrFirst;	/* Address of the OP_IfNot opcode */
		int addrJmp;	/* Address of the OP_Jump opcode */
		VdbeOp *pOp;	/* Opcode that opens the sorter */
		int nKey;	/* Number of sorting key columns, including OP_Sequence */

		regPrevKey = pParse->nMem + 1;
		pParse->nMem += pSort->nOBSat;
		nKey = nExpr - pSort->nOBSat + bSeq;
		if (bSeq) {
			int r1 = sqlGetTempReg(pParse);
			sqlVdbeAddOp2(v, OP_Integer, 0, r1);
			addrFirst = sqlVdbeAddOp3(v, OP_Eq, r1, 0, regBase + nExpr);
			sqlReleaseTempReg(pParse, r1);
		} else {
			addrFirst =
			    sqlVdbeAddOp1(v, OP_SequenceTest,
					      pSort->iECursor);
		}
		VdbeCoverage(v);
		sqlVdbeAddOp3(v, OP_Compare, regPrevKey, regBase,
				  pSort->nOBSat);
		pOp = sqlVdbeGetOp(v, pSort->addrSortIndex);
		if (pParse->db->mallocFailed)
			return;
		pOp->p2 = nKey + nData;
		struct sql_key_info *key_info = pOp->p4.key_info;
		for (uint32_t i = 0; i < key_info->part_count; i++)
			key_info->parts[i].sort_order = SORT_ORDER_ASC;
		sqlVdbeChangeP4(v, -1, (char *)key_info, P4_KEYINFO);
		pOp->p4.key_info = sql_expr_list_to_key_info(pParse,
							     pSort->pOrderBy,
							     nOBSat);
		addrJmp = sqlVdbeCurrentAddr(v);
		sqlVdbeAddOp3(v, OP_Jump, addrJmp + 1, 0, addrJmp + 1);
		VdbeCoverage(v);
		pSort->labelBkOut = sqlVdbeMakeLabel(v);
		pSort->regReturn = ++pParse->nMem;
		sqlVdbeAddOp2(v, OP_Gosub, pSort->regReturn,
				  pSort->labelBkOut);
		sqlVdbeAddOp1(v, OP_ResetSorter, pSort->iECursor);
		if (iLimit) {
			int r1 = sqlGetTempReg(pParse);
			sqlVdbeAddOp2(v, OP_Integer, 0, r1);
			sqlVdbeAddOp3(v, OP_Eq, r1, pSort->labelDone, iLimit);
			sqlReleaseTempReg(pParse, r1);
		}
		sqlVdbeJumpHere(v, addrFirst);
		sqlExprCodeMove(pParse, regBase, regPrevKey, pSort->nOBSat);
		sqlVdbeJumpHere(v, addrJmp);
	}
	if (pSort->sortFlags & SORTFLAG_UseSorter) {
		sqlVdbeAddOp2(v, OP_SorterInsert, pSort->iECursor,
				  regRecord);
	} else {
		sqlVdbeAddOp2(v, OP_IdxInsert, regRecord, pSort->reg_eph);
	}

	if (iLimit) {
		int addr;
		int r1 = 0;
		/* Fill the sorter until it contains LIMIT+OFFSET entries.  (The iLimit
		 * register is initialized with value of LIMIT+OFFSET.)  After the sorter
		 * fills up, delete the least entry in the sorter after each insert.
		 * Thus we never hold more than the LIMIT+OFFSET rows in memory at once
		 */
		addr = sqlVdbeAddOp1(v, OP_IfNotZero, iLimit);
		VdbeCoverage(v);
		if (pSort->sortFlags & SORTFLAG_DESC) {
			int iNextInstr = sqlVdbeCurrentAddr(v) + 1;
			sqlVdbeAddOp2(v, OP_Rewind, pSort->iECursor, iNextInstr);
		} else {
			sqlVdbeAddOp1(v, OP_Last, pSort->iECursor);
		}
		if (pSort->bOrderedInnerLoop) {
			r1 = ++pParse->nMem;
			sqlVdbeAddOp3(v, OP_Column, pSort->iECursor, nExpr,
					  r1);
			VdbeComment((v, "seq"));
		}
		sqlVdbeAddOp1(v, OP_Delete, pSort->iECursor);
		if (pSort->bOrderedInnerLoop) {
			/* If the inner loop is driven by an index such that values from
			 * the same iteration of the inner loop are in sorted order, then
			 * immediately jump to the next iteration of an inner loop if the
			 * entry from the current iteration does not fit into the top
			 * LIMIT+OFFSET entries of the sorter.
			 */
			int iBrk = sqlVdbeCurrentAddr(v) + 2;
			sqlVdbeAddOp3(v, OP_Eq, regBase + nExpr, iBrk, r1);
			sqlVdbeChangeP5(v, SQL_NULLEQ);
			VdbeCoverage(v);
		}
		sqlVdbeJumpHere(v, addr);
	}
}

/*
 * Add code to implement the OFFSET
 */
static void
codeOffset(Vdbe * v,		/* Generate code into this VM */
	   int iOffset,		/* Register holding the offset counter */
	   int iContinue)	/* Jump here to skip the current record */
{
	if (iOffset > 0) {
		sqlVdbeAddOp3(v, OP_IfPos, iOffset, iContinue, 1);
		VdbeCoverage(v);
		VdbeComment((v, "OFFSET"));
	}
}

/**
 * Add code that will check to make sure the @n registers starting
 * at @reg_data form a distinct entry. @cursor is a sorting index
 * that holds previously seen combinations of the @n values.
 * A new entry is made in @cursor if the current n values are new.
 *
 * A jump to @addr_repeat is made and the @n+1 values are popped
 * from the stack if the top n elements are not distinct.
 *
 * @param parse Parsing and code generating context.
 * @param cursor A sorting index cursor used to test for
 *               distinctness.
 * @param reg_eph Register holding ephemeral space's pointer.
 * @param addr_repeat Jump here if not distinct.
 * @param n Number of elements in record.
 * @param reg_data First register holding the data.
 */
static void
vdbe_insert_distinct(struct Parse *parse, int cursor, int reg_eph,
		     int addr_repeat, int n, int reg_data)
{
	struct Vdbe *v = parse->pVdbe;
	int r1 = sqlGetTempReg(parse);
	sqlVdbeAddOp4Int(v, OP_Found, cursor, addr_repeat, reg_data, n);
	sqlVdbeAddOp3(v, OP_MakeRecord, reg_data, n, r1);
	sqlVdbeAddOp2(v, OP_IdxInsert, r1, reg_eph);
	sqlReleaseTempReg(parse, r1);
}

/*
 * This routine generates the code for the inside of the inner loop
 * of a SELECT.
 *
 * If srcTab is negative, then the pEList expressions
 * are evaluated in order to get the data for this row.  If srcTab is
 * zero or more, then data is pulled from srcTab and pEList is used only
 * to get the number of columns and the collation sequence for each column.
 */
static void
selectInnerLoop(Parse * pParse,		/* The parser context */
		Select * p,		/* The complete select statement being coded */
		ExprList * pEList,	/* List of values being extracted */
		int srcTab,		/* Pull data from this table */
		SortCtx * pSort,	/* If not NULL, info on how to process ORDER BY */
		DistinctCtx * pDistinct,	/* If not NULL, info on how to process DISTINCT */
		SelectDest * pDest,	/* How to dispose of the results */
		int iContinue,		/* Jump here to continue with next row */
		int iBreak)		/* Jump here to break out of the inner loop */
{
	Vdbe *v = pParse->pVdbe;
	int i;
	int hasDistinct;		/* True if the DISTINCT keyword is present */
	int eDest = pDest->eDest;	/* How to dispose of results */
	int iParm = pDest->iSDParm;	/* First argument to disposal method */
	int nResultCol;			/* Number of result columns */
	int nPrefixReg = 0;		/* Number of extra registers before regResult */

	/* Usually, regResult is the first cell in an array of memory cells
	 * containing the current result row. In this case regOrig is set to the
	 * same value. However, if the results are being sent to the sorter, the
	 * values for any expressions that are also part of the sort-key are omitted
	 * from this array. In this case regOrig is set to zero.
	 */
	int regResult;		/* Start of memory holding current results */
	int regOrig;		/* Start of memory holding full result (or 0) */

	assert(v);
	assert(pEList != 0);
	hasDistinct = pDistinct ? pDistinct->eTnctType : WHERE_DISTINCT_NOOP;
	if (pSort && pSort->pOrderBy == 0)
		pSort = 0;
	if (pSort == 0 && !hasDistinct) {
		assert(iContinue != 0);
		codeOffset(v, p->iOffset, iContinue);
	}

	/* Pull the requested columns.
	 */
	nResultCol = pEList->nExpr;

	if (pDest->iSdst == 0) {
		if (pSort) {
			nPrefixReg = pSort->pOrderBy->nExpr;
			if (!(pSort->sortFlags & SORTFLAG_UseSorter))
				nPrefixReg++;
			pParse->nMem += nPrefixReg;
		}
		pDest->iSdst = pParse->nMem + 1;
		pParse->nMem += nResultCol;
	} else if (pDest->iSdst + nResultCol > pParse->nMem) {
		/* This is an error condition that can result, for example, when a SELECT
		 * on the right-hand side of an INSERT contains more result columns than
		 * there are columns in the table on the left.  The error will be caught
		 * and reported later.  But we need to make sure enough memory is allocated
		 * to avoid other spurious errors in the meantime.
		 */
		pParse->nMem += nResultCol;
	}
	pDest->nSdst = nResultCol;
	regOrig = regResult = pDest->iSdst;
	if (srcTab >= 0) {
		for (i = 0; i < nResultCol; i++) {
			sqlVdbeAddOp3(v, OP_Column, srcTab, i,
					  regResult + i);
			VdbeComment((v, "%s", pEList->a[i].zName));
		}
	} else if (eDest != SRT_Exists) {
		/* If the destination is an EXISTS(...) expression, the actual
		 * values returned by the SELECT are not required.
		 */
		u8 ecelFlags;
		if (eDest == SRT_Mem || eDest == SRT_Output
		    || eDest == SRT_Coroutine) {
			ecelFlags = SQL_ECEL_DUP;
		} else {
			ecelFlags = 0;
		}
		if (pSort && hasDistinct == 0 && eDest != SRT_EphemTab
		    && eDest != SRT_Table) {
			/* For each expression in pEList that is a copy of an expression in
			 * the ORDER BY clause (pSort->pOrderBy), set the associated
			 * iOrderByCol value to one more than the index of the ORDER BY
			 * expression within the sort-key that pushOntoSorter() will generate.
			 * This allows the pEList field to be omitted from the sorted record,
			 * saving space and CPU cycles.
			 */
			ecelFlags |= (SQL_ECEL_OMITREF | SQL_ECEL_REF);
			/*
			 * Format for ephemeral space has been
			 * already set with field count calculated
			 * as orderBy->nExpr + pEList->nExpr + 1,
			 * where pEList is a select list.
			 * Since we want to reduce number of fields
			 * in format of ephemeral space, we should
			 * fix corresponding opcode's argument.
			 * Otherwise, tuple format won't match
			 * space format.
			 */
			uint32_t excess_field_count = 0;
			for (i = pSort->nOBSat; i < pSort->pOrderBy->nExpr;
			     i++) {
				int j = pSort->pOrderBy->a[i].u.x.iOrderByCol;
				if (j > 0) {
					excess_field_count++;
					pEList->a[j - 1].u.x.iOrderByCol =
						(u16) (i + 1 - pSort->nOBSat);
				}
			}
			struct VdbeOp *open_eph_op =
				sqlVdbeGetOp(v, pSort->addrSortIndex);
			assert(open_eph_op->p2 - excess_field_count > 0);
			sqlVdbeChangeP2(v, pSort->addrSortIndex,
					open_eph_op->p2 -
					excess_field_count);
			regOrig = 0;
			assert(eDest == SRT_Set || eDest == SRT_Mem
			       || eDest == SRT_Coroutine
			       || eDest == SRT_Output);
		}
		nResultCol =
		    sqlExprCodeExprList(pParse, pEList, regResult, 0,
					    ecelFlags);
	}

	/* If the DISTINCT keyword was present on the SELECT statement
	 * and this row has been seen before, then do not make this row
	 * part of the result.
	 */
	if (hasDistinct) {
		switch (pDistinct->eTnctType) {
		case WHERE_DISTINCT_ORDERED:{
				VdbeOp *pOp;	/* No longer required OpenEphemeral instr. */
				int iJump;	/* Jump destination */
				int regPrev;	/* Previous row content */

				/* Allocate space for the previous row */
				regPrev = pParse->nMem + 1;
				pParse->nMem += nResultCol;

				/*
				 * Actually, for DISTINCT handling
				 * two op-codes were emitted here:
				 * OpenTEphemeral & IteratorOpen.
				 * So, we need to Noop one and
				 * re-use second for Null op-code.
				 *
				 * Change to an OP_Null sets the
				 * MEM_Cleared bit on the first
				 * register of the previous value. 
				 * This will cause the OP_Ne below
				 * to always fail on the first
				 * iteration of the loop even if
				 * the first row is all NULLs.
				 */
				sqlVdbeChangeToNoop(v, pDistinct->addrTnct);
				pOp = sqlVdbeGetOp(v, pDistinct->addrTnct + 1);
				pOp->opcode = OP_Null;
				pOp->p1 = 1;
				pOp->p2 = regPrev;

				iJump = sqlVdbeCurrentAddr(v) + nResultCol;
				for (i = 0; i < nResultCol; i++) {
					bool is_found;
					uint32_t id;
					struct coll *coll;
					if (sql_expr_coll(pParse,
							  pEList->a[i].pExpr,
							  &is_found, &id,
							  &coll) != 0)
						break;
					if (i < nResultCol - 1) {
						sqlVdbeAddOp3(v, OP_Ne,
								  regResult + i,
								  iJump,
								  regPrev + i);
						VdbeCoverage(v);
					} else {
						sqlVdbeAddOp3(v, OP_Eq,
								  regResult + i,
								  iContinue,
								  regPrev + i);
						VdbeCoverage(v);
					}
					if (coll != NULL) {
						sqlVdbeChangeP4(v, -1,
								    (const char *)coll,
								    P4_COLLSEQ);
					}
					sqlVdbeChangeP5(v, SQL_NULLEQ);
				}
				assert(sqlVdbeCurrentAddr(v) == iJump
				       || pParse->db->mallocFailed);
				sqlVdbeAddOp3(v, OP_Copy, regResult,
						  regPrev, nResultCol - 1);
				break;
			}

		case WHERE_DISTINCT_UNIQUE:{
			/**
			 * To handle DISTINCT two op-codes are
			 * emitted: OpenTEphemeral & IteratorOpen.
			 * addrTnct is address of first insn in
			 * a couple. To evict ephemral space,
			 * need to noop both op-codes.
			 */
			sqlVdbeChangeToNoop(v, pDistinct->addrTnct);
			sqlVdbeChangeToNoop(v, pDistinct->addrTnct + 1);
			break;
			}

		default:{
			assert(pDistinct->eTnctType ==
			       WHERE_DISTINCT_UNORDERED);
			vdbe_insert_distinct(pParse, pDistinct->cur_eph,
					     pDistinct->reg_eph, iContinue,
					     nResultCol, regResult);
			break;
		}
		}
		if (pSort == 0) {
			codeOffset(v, p->iOffset, iContinue);
		}
	}

	switch (eDest) {
		/* In this mode, write each query result to the key of the temporary
		 * table iParm.
		 */
	case SRT_Union:{
			int r1;
			r1 = sqlGetTempReg(pParse);
			sqlVdbeAddOp3(v, OP_MakeRecord, regResult,
					  nResultCol, r1);
			sqlVdbeAddOp2(v, OP_IdxInsert,  r1, pDest->reg_eph);
			sqlReleaseTempReg(pParse, r1);
			break;
		}

		/* Construct a record from the query result, but instead of
		 * saving that record, use it as a key to delete elements from
		 * the temporary table iParm.
		 */
	case SRT_Except:{
			sqlVdbeAddOp3(v, OP_IdxDelete, iParm, regResult,
					  nResultCol);
			break;
		}

		/* Store the result as data using a unique key.
		 */
	case SRT_Fifo:
	case SRT_DistFifo:
	case SRT_Table:
	case SRT_EphemTab:{
			int r1 = sqlGetTempRange(pParse, nPrefixReg + 1);
			testcase(eDest == SRT_Table);
			testcase(eDest == SRT_EphemTab);
			testcase(eDest == SRT_Fifo);
			testcase(eDest == SRT_DistFifo);
			sqlVdbeAddOp3(v, OP_MakeRecord, regResult,
					  nResultCol, r1 + nPrefixReg);
			/* Set flag to save memory allocating one by malloc. */
			sqlVdbeChangeP5(v, 1);

			if (eDest == SRT_DistFifo) {
				/* If the destination is DistFifo, then cursor (iParm+1) is open
				 * on an ephemeral index. If the current row is already present
				 * in the index, do not write it to the output. If not, add the
				 * current row to the index and proceed with writing it to the
				 * output table as well.
				 */
				int addr = sqlVdbeCurrentAddr(v) + 6;
				sqlVdbeAddOp4Int(v, OP_Found, iParm + 1,
						     addr, r1, 0);
				VdbeCoverage(v);
				sqlVdbeAddOp2(v, OP_IdxInsert, r1,
						  pDest->reg_eph + 1);
				assert(pSort == 0);
			}

			if (pSort) {
				pushOntoSorter(pParse, pSort, p,
					       r1 + nPrefixReg, regResult, 1,
					       nPrefixReg);
			} else {
				int regRec = sqlGetTempReg(pParse);
				/* Last column is required for ID. */
				int regCopy = sqlGetTempRange(pParse, nResultCol + 1);
				sqlVdbeAddOp2(v, OP_NextIdEphemeral, pDest->reg_eph,
						  regCopy + nResultCol);
				/* Positioning ID column to be last in inserted tuple.
				 * NextId -> regCopy + n + 1
				 * Copy [regResult, regResult + n] -> [regCopy, regCopy + n]
				 * MakeRecord -> [regCopy, regCopy + n + 1] -> regRec
				 * IdxInsert -> regRec
				 */
				sqlVdbeAddOp3(v, OP_Copy, regResult, regCopy, nResultCol - 1);
				sqlVdbeAddOp3(v, OP_MakeRecord, regCopy, nResultCol + 1, regRec);
				/* Set flag to save memory allocating one by malloc. */
				sqlVdbeChangeP5(v, 1);
				sqlVdbeAddOp2(v, OP_IdxInsert, regRec, pDest->reg_eph);
				sqlReleaseTempReg(pParse, regRec);
				sqlReleaseTempRange(pParse, regCopy, nResultCol + 1);
			}
			sqlReleaseTempRange(pParse, r1, nPrefixReg + 1);
			break;
		}
		/* If we are creating a set for an "expr IN (SELECT ...)" construct,
		 * then there should be a single item on the stack.  Write this
		 * item into the set table with bogus data.
		 */
	case SRT_Set:{
			if (pSort) {
				/* At first glance you would think we could optimize out the
				 * ORDER BY in this case since the order of entries in the set
				 * does not matter.  But there might be a LIMIT clause, in which
				 * case the order does matter.
				 */
				pushOntoSorter(pParse, pSort, p, regResult,
					       regOrig, nResultCol, nPrefixReg);
			} else {
				int r1 = sqlGetTempReg(pParse);
				enum field_type *types =
					field_type_sequence_dup(pParse,
								pDest->dest_type,
								nResultCol);
				sqlVdbeAddOp4(v, OP_MakeRecord, regResult,
						  nResultCol, r1, (char *)types,
						  P4_DYNAMIC);
				sql_expr_type_cache_change(pParse,
							   regResult,
							   nResultCol);
				sqlVdbeAddOp2(v, OP_IdxInsert, r1, pDest->reg_eph);
				sqlReleaseTempReg(pParse, r1);
			}
			break;
		}

		/* If any row exist in the result set, record that fact and abort.
		 */
	case SRT_Exists:{
			sqlVdbeAddOp2(v, OP_Bool, true, iParm);
			/* The LIMIT clause will terminate the loop for us */
			break;
		}

		/* If this is a scalar select that is part of an expression, then
		 * store the results in the appropriate memory cell or array of
		 * memory cells and break out of the scan loop.
		 */
	case SRT_Mem:{
			if (pSort) {
				assert(nResultCol <= pDest->nSdst);
				pushOntoSorter(pParse, pSort, p, regResult,
					       regOrig, nResultCol, nPrefixReg);
			} else {
				assert(nResultCol == pDest->nSdst);
				assert(regResult == iParm);
				/* The LIMIT clause will jump out of the loop for us */
			}
			break;
		}

	case SRT_Coroutine:	/* Send data to a co-routine */
	case SRT_Output:{	/* Return the results */
			testcase(eDest == SRT_Coroutine);
			testcase(eDest == SRT_Output);
			if (pSort) {
				pushOntoSorter(pParse, pSort, p, regResult,
					       regOrig, nResultCol, nPrefixReg);
			} else if (eDest == SRT_Coroutine) {
				sqlVdbeAddOp1(v, OP_Yield, pDest->iSDParm);
			} else {
				sqlVdbeAddOp2(v, OP_ResultRow, regResult,
						  nResultCol);
				sql_expr_type_cache_change(pParse,
							   regResult,
							   nResultCol);
			}
			break;
		}

		/* Write the results into a priority queue that is order according to
		 * pDest->pOrderBy (in pSO).  pDest->iSDParm (in iParm) is the cursor for an
		 * index with pSO->nExpr+2 columns.  Build a key using pSO for the first
		 * pSO->nExpr columns, then make sure all keys are unique by adding a
		 * final OP_Sequence column.  The last column is the record as a blob.
		 */
	case SRT_DistQueue:
	case SRT_Queue:{
			int nKey;
			int r1, r2, r3;
			int addrTest = 0;
			ExprList *pSO;
			pSO = pDest->pOrderBy;
			assert(pSO);
			nKey = pSO->nExpr;
			r1 = sqlGetTempReg(pParse);
			r2 = sqlGetTempRange(pParse, nKey + 2);
			r3 = r2 + nKey + 1;
			if (eDest == SRT_DistQueue) {
				/* If the destination is DistQueue, then cursor (iParm+1) is open
				 * on a second ephemeral index that holds all values every previously
				 * added to the queue.
				 */
				addrTest =
				    sqlVdbeAddOp4Int(v, OP_Found, iParm + 1,
							 0, regResult,
							 nResultCol);
				VdbeCoverage(v);
			}
			sqlVdbeAddOp3(v, OP_MakeRecord, regResult,
					  nResultCol, r3);
			if (eDest == SRT_DistQueue) {
				sqlVdbeAddOp2(v, OP_IdxInsert, r3,
						  pDest->reg_eph + 1);
			}
			for (i = 0; i < nKey; i++) {
				sqlVdbeAddOp2(v, OP_SCopy,
						  regResult +
						  pSO->a[i].u.x.iOrderByCol - 1,
						  r2 + i);
			}
			sqlVdbeAddOp2(v, OP_Sequence, iParm, r2 + nKey);
			sqlVdbeAddOp3(v, OP_MakeRecord, r2, nKey + 2, r1);
			sqlVdbeAddOp2(v, OP_IdxInsert, r1, pDest->reg_eph);
			if (addrTest)
				sqlVdbeJumpHere(v, addrTest);
			sqlReleaseTempReg(pParse, r1);
			sqlReleaseTempRange(pParse, r2, nKey + 2);
			break;
		}

		/* Discard the results.  This is used for SELECT statements inside
		 * the body of a TRIGGER.  The purpose of such selects is to call
		 * user-defined functions that have side effects.  We do not care
		 * about the actual results of the select.
		 */
	default:{
			assert(eDest == SRT_Discard);
			break;
		}
	}

	/* Jump to the end of the loop if the LIMIT is reached.  Except, if
	 * there is a sorter, in which case the sorter has already limited
	 * the output for us.
	 */
	if (pSort == 0 && p->iLimit) {
		sqlVdbeAddOp2(v, OP_DecrJumpZero, p->iLimit, iBreak);
		VdbeCoverage(v);
	}
}

static inline size_t
sql_key_info_sizeof(uint32_t part_count)
{
	return sizeof(struct sql_key_info) +
		part_count * sizeof(struct key_part_def);
}

struct sql_key_info *
sql_key_info_new(sql *db, uint32_t part_count)
{
	struct sql_key_info *key_info = sqlDbMallocRawNN(db,
				sql_key_info_sizeof(part_count));
	if (key_info == NULL) {
		sqlOomFault(db);
		return NULL;
	}
	key_info->db = db;
	key_info->key_def = NULL;
	key_info->refs = 1;
	key_info->part_count = part_count;
	key_info->is_pk_rowid = false;
	for (uint32_t i = 0; i < part_count; i++) {
		struct key_part_def *part = &key_info->parts[i];
		part->fieldno = i;
		part->type = FIELD_TYPE_SCALAR;
		part->coll_id = COLL_NONE;
		part->is_nullable = false;
		part->nullable_action = ON_CONFLICT_ACTION_ABORT;
		part->sort_order = SORT_ORDER_ASC;
		part->path = NULL;
	}
	return key_info;
}

struct sql_key_info *
sql_key_info_new_from_key_def(sql *db, const struct key_def *key_def)
{
	struct sql_key_info *key_info = sqlDbMallocRawNN(db,
				sql_key_info_sizeof(key_def->part_count));
	if (key_info == NULL) {
		sqlOomFault(db);
		return NULL;
	}
	key_info->db = db;
	key_info->key_def = NULL;
	key_info->refs = 1;
	key_info->part_count = key_def->part_count;
	key_info->is_pk_rowid = false;
	key_def_dump_parts(key_def, key_info->parts, NULL);
	return key_info;
}

struct sql_key_info *
sql_key_info_ref(struct sql_key_info *key_info)
{
	assert(key_info->refs > 0);
	key_info->refs++;
	return key_info;
}

void
sql_key_info_unref(struct sql_key_info *key_info)
{
	if (key_info == NULL)
		return;
	assert(key_info->refs > 0);
	if (--key_info->refs == 0) {
		if (key_info->key_def != NULL)
			key_def_delete(key_info->key_def);
		sqlDbFree(key_info->db, key_info);
	}
}

struct key_def *
sql_key_info_to_key_def(struct sql_key_info *key_info)
{
	if (key_info->key_def == NULL) {
		key_info->key_def = key_def_new(key_info->parts,
						key_info->part_count, false);
	}
	return key_info->key_def;
}

static struct sql_key_info *
sql_expr_list_to_key_info(struct Parse *parse, struct ExprList *list, int start)
{
	int expr_count = list->nExpr;
	struct sql_key_info *key_info = sql_key_info_new(parse->db, expr_count);
	if (key_info == NULL)
		return NULL;
	struct ExprList_item *item = list->a + start;
	for (int i = start; i < expr_count; ++i, ++item) {
		struct key_part_def *part = &key_info->parts[i - start];
		bool unused;
		uint32_t id;
		struct coll *unused_coll;
		if (sql_expr_coll(parse, item->pExpr, &unused, &id,
				  &unused_coll) != 0) {
			sqlDbFree(parse->db, key_info);
			return NULL;
		}
		part->coll_id = id;
		part->sort_order = item->sort_order;
		part->type = sql_expr_type(item->pExpr);
	}
	return key_info;
}

const char *
sql_select_op_name(int id)
{
	char *z;
	switch (id) {
	case TK_ALL:
		z = "UNION ALL";
		break;
	case TK_INTERSECT:
		z = "INTERSECT";
		break;
	case TK_EXCEPT:
		z = "EXCEPT";
		break;
	default:
		z = "UNION";
		break;
	}
	return z;
}

/*
 * Unless an "EXPLAIN QUERY PLAN" command is being processed, this function
 * is a no-op. Otherwise, it adds a single row of output to the EQP result,
 * where the caption is of the form:
 *
 *   "USE TEMP B-TREE FOR xxx"
 *
 * where xxx is one of "DISTINCT", "ORDER BY" or "GROUP BY". Exactly which
 * is determined by the zUsage argument.
 */
static void
explainTempTable(Parse * pParse, const char *zUsage)
{
	if (pParse->explain == 2) {
		Vdbe *v = pParse->pVdbe;
		char *zMsg =
		    sqlMPrintf(pParse->db, "USE TEMP B-TREE FOR %s",
				   zUsage);
		sqlVdbeAddOp4(v, OP_Explain, pParse->iSelectId, 0, 0, zMsg,
				  P4_DYNAMIC);
	}
}

/*
 * Unless an "EXPLAIN QUERY PLAN" command is being processed, this function
 * is a no-op. Otherwise, it adds a single row of output to the EQP result,
 * where the caption is of one of the two forms:
 *
 *   "COMPOSITE SUBQUERIES iSub1 and iSub2 (op)"
 *   "COMPOSITE SUBQUERIES iSub1 and iSub2 USING TEMP B-TREE (op)"
 *
 * where iSub1 and iSub2 are the integers passed as the corresponding
 * function parameters, and op is the text representation of the parameter
 * of the same name. The parameter "op" must be one of TK_UNION, TK_EXCEPT,
 * TK_INTERSECT or TK_ALL. The first form is used if argument bUseTmp is
 * false, or the second form if it is true.
 */
static void
explainComposite(Parse * pParse,	/* Parse context */
		 int op,	/* One of TK_UNION, TK_EXCEPT etc. */
		 int iSub1,	/* Subquery id 1 */
		 int iSub2,	/* Subquery id 2 */
		 int bUseTmp	/* True if a temp table was used */
    )
{
	assert(op == TK_UNION || op == TK_EXCEPT || op == TK_INTERSECT
	       || op == TK_ALL);
	if (pParse->explain == 2) {
		Vdbe *v = pParse->pVdbe;
		char *zMsg =
		    sqlMPrintf(pParse->db,
				   "COMPOUND SUBQUERIES %d AND %d %s(%s)",
				   iSub1, iSub2,
				   bUseTmp ? "USING TEMP B-TREE " : "",
				   sql_select_op_name(op)
		    );
		sqlVdbeAddOp4(v, OP_Explain, pParse->iSelectId, 0, 0, zMsg,
				  P4_DYNAMIC);
	}
}

/*
 * If the inner loop was generated using a non-null pOrderBy argument,
 * then the results were placed in a sorter.  After the loop is terminated
 * we need to run the sorter and output the results.  The following
 * routine generates the code needed to do that.
 */
static void
generateSortTail(Parse * pParse,	/* Parsing context */
		 Select * p,		/* The SELECT statement */
		 SortCtx * pSort,	/* Information on the ORDER BY clause */
		 int nColumn,		/* Number of columns of data */
		 SelectDest * pDest)	/* Write the sorted results here */
{
	Vdbe *v = pParse->pVdbe;	/* The prepared statement */
	int addrBreak = pSort->labelDone;	/* Jump here to exit loop */
	int addrContinue = sqlVdbeMakeLabel(v);	/* Jump here for next cycle */
	int addr;
	int addrOnce = 0;
	int iTab;
	ExprList *pOrderBy = pSort->pOrderBy;
	int eDest = pDest->eDest;
	int regRow;
	int regTupleid;
	int iCol;
	int nKey;
	int iSortTab;		/* Sorter cursor to read from */
	int nSortData;		/* Trailing values to read from sorter */
	int i;
	int bSeq;		/* True if sorter record includes seq. no. */
	struct ExprList_item *aOutEx = p->pEList->a;

	assert(addrBreak < 0);
	if (pSort->labelBkOut) {
		sqlVdbeAddOp2(v, OP_Gosub, pSort->regReturn,
				  pSort->labelBkOut);
		sqlVdbeGoto(v, addrBreak);
		sqlVdbeResolveLabel(v, pSort->labelBkOut);
	}
	iTab = pSort->iECursor;
	if (eDest == SRT_Output || eDest == SRT_Coroutine || eDest == SRT_Mem) {
		regTupleid = 0;
		regRow = pDest->iSdst;
		nSortData = nColumn;
	} else {
		regTupleid = sqlGetTempReg(pParse);
		regRow = sqlGetTempRange(pParse, nColumn);
		nSortData = nColumn;
	}
	nKey = pOrderBy->nExpr - pSort->nOBSat;
	if (pSort->sortFlags & SORTFLAG_UseSorter) {
		int regSortOut = ++pParse->nMem;
		iSortTab = pParse->nTab++;
		if (pSort->labelBkOut) {
			addrOnce = sqlVdbeAddOp0(v, OP_Once);
			VdbeCoverage(v);
		}
		sqlVdbeAddOp3(v, OP_OpenPseudo, iSortTab, regSortOut,
				  nKey + 1 + nSortData);
		if (addrOnce)
			sqlVdbeJumpHere(v, addrOnce);
		addr = 1 + sqlVdbeAddOp2(v, OP_SorterSort, iTab, addrBreak);
		VdbeCoverage(v);
		codeOffset(v, p->iOffset, addrContinue);
		sqlVdbeAddOp3(v, OP_SorterData, iTab, regSortOut, iSortTab);
		bSeq = 0;
	} else {
		/* In case of DESC sorting order data should be taken from
		 * the end of table. */
		int opPositioning = (pSort->sortFlags & SORTFLAG_DESC) ?
				    OP_Last : OP_Sort;
		addr = 1 + sqlVdbeAddOp2(v, opPositioning, iTab, addrBreak);
		VdbeCoverage(v);
		codeOffset(v, p->iOffset, addrContinue);
		iSortTab = iTab;
		bSeq = 1;
	}
	for (i = 0, iCol = nKey + bSeq; i < nSortData; i++) {
		int iRead;
		if (aOutEx[i].u.x.iOrderByCol) {
			iRead = aOutEx[i].u.x.iOrderByCol - 1;
		} else {
			iRead = iCol++;
		}
		sqlVdbeAddOp3(v, OP_Column, iSortTab, iRead, regRow + i);
		VdbeComment((v, "%s",
			     aOutEx[i].zName ? aOutEx[i].zName : aOutEx[i].
			     zSpan));
	}
	switch (eDest) {
	case SRT_Table:
	case SRT_EphemTab: {
			int regCopy = sqlGetTempRange(pParse,  nColumn);
			sqlVdbeAddOp2(v, OP_NextIdEphemeral, pDest->reg_eph,
					  regTupleid);
			sqlVdbeAddOp3(v, OP_Copy, regRow, regCopy, nSortData - 1);
			sqlVdbeAddOp3(v, OP_MakeRecord, regCopy, nColumn + 1, regRow);
			sqlVdbeAddOp2(v, OP_IdxInsert, regRow, pDest->reg_eph);
			sqlReleaseTempReg(pParse, regCopy);
			break;
		}
	case SRT_Set:{
			enum field_type *types =
				field_type_sequence_dup(pParse, pDest->dest_type,
							nColumn);
			sqlVdbeAddOp4(v, OP_MakeRecord, regRow, nColumn,
					  regTupleid, (char *)types,
					  P4_DYNAMIC);
			sql_expr_type_cache_change(pParse, regRow, nColumn);
			sqlVdbeAddOp2(v, OP_IdxInsert, regTupleid, pDest->reg_eph);
			break;
		}
	case SRT_Mem:{
			/* The LIMIT clause will terminate the loop for us */
			break;
		}
	default: {
			assert(eDest == SRT_Output || eDest == SRT_Coroutine);
			testcase(eDest == SRT_Output);
			testcase(eDest == SRT_Coroutine);
			if (eDest == SRT_Output) {
				sqlVdbeAddOp2(v, OP_ResultRow, pDest->iSdst,
						  nColumn);
				sql_expr_type_cache_change(pParse,
							   pDest->iSdst,
							   nColumn);
			} else {
				sqlVdbeAddOp1(v, OP_Yield, pDest->iSDParm);
			}
			break;
		}
	}
	if (regTupleid) {
		if (eDest == SRT_Set) {
			sqlReleaseTempRange(pParse, regRow, nColumn);
		} else {
			sqlReleaseTempReg(pParse, regRow);
		}
		sqlReleaseTempReg(pParse, regTupleid);
	}
	/* The bottom of the loop
	 */
	sqlVdbeResolveLabel(v, addrContinue);
	if (pSort->sortFlags & SORTFLAG_UseSorter) {
		sqlVdbeAddOp2(v, OP_SorterNext, iTab, addr);
		VdbeCoverage(v);
	} else {
		/* In case of DESC sorting cursor should move backward. */
		int opPositioning = (pSort->sortFlags & SORTFLAG_DESC) ?
				    OP_Prev : OP_Next;
		sqlVdbeAddOp2(v, opPositioning, iTab, addr);
		VdbeCoverage(v);
	}
	if (pSort->regReturn)
		sqlVdbeAddOp1(v, OP_Return, pSort->regReturn);
	sqlVdbeResolveLabel(v, addrBreak);
}

/**
 * Generate code that will tell the VDBE the names of columns
 * in the result set. This information is used to provide the
 * metadata during/after statement execution.
 *
 * @param pParse Parsing context.
 * @param pTabList List of tables.
 * @param pEList Expressions defining the result set.
 */
static void
generate_column_metadata(struct Parse *pParse, struct SrcList *pTabList,
			 struct ExprList *pEList)
{
	Vdbe *v = pParse->pVdbe;
	int i, j;
	sql *db = pParse->db;
	/* If this is an EXPLAIN, skip this step */
	if (pParse->explain) {
		return;
	}

	if (pParse->colNamesSet || db->mallocFailed)
		return;
	assert(v != 0);
	size_t size;
	uint32_t *var_pos =
		region_alloc_array(&pParse->region, typeof(var_pos[0]),
				   pParse->nVar, &size);
	if (var_pos == NULL) {
		diag_set(OutOfMemory, size, "region_alloc_array", "var_pos");
		return;
	}
	assert(pTabList != 0);
	pParse->colNamesSet = 1;
	bool is_full_meta = (pParse->sql_flags & SQL_FullMetadata) != 0;
	sqlVdbeSetNumCols(v, pEList->nExpr);
	uint32_t var_count = 0;
	for (i = 0; i < pEList->nExpr; i++) {
		Expr *p;
		p = pEList->a[i].pExpr;
		if (NEVER(p == 0))
			continue;
		if (p->op == TK_VARIABLE)
			var_pos[var_count++] = i;
		enum field_type type = sql_expr_type(p);
		vdbe_metadata_set_col_type(v, i, field_type_strs[type]);
		if (is_full_meta && (type == FIELD_TYPE_STRING ||
		    type == FIELD_TYPE_SCALAR)) {
			bool unused;
			uint32_t id = 0;
			struct coll *coll = NULL;
			/*
			 * If sql_expr_coll fails then it fails somewhere
			 * above the call stack.
			 */
			int rc =  sql_expr_coll(pParse, p, &unused, &id, &coll);
			assert(rc == 0);
			(void) rc;
			if (id != COLL_NONE) {
				struct coll_id *coll_id = coll_by_id(id);
				vdbe_metadata_set_col_collation(v, i,
								coll_id->name,
								coll_id->name_len);
			}
		}
		vdbe_metadata_set_col_nullability(v, i, -1);
		const char *colname = pEList->a[i].zName;
		const char *span = pEList->a[i].zSpan;
		if (p->op == TK_COLUMN_REF || p->op == TK_AGG_COLUMN) {
			char *zCol;
			int iCol = p->iColumn;
			for (j = 0; ALWAYS(j < pTabList->nSrc); j++) {
				if (pTabList->a[j].iCursor == p->iTable)
					break;
			}
			assert(j < pTabList->nSrc);
			struct space *space = pTabList->a[j].space;
			struct space_def *space_def = space->def;
			assert(iCol >= 0 && iCol < (int)space_def->field_count);
			zCol = space_def->fields[iCol].name;
			const char *name = colname;
			if (name == NULL) {
				int flags = pParse->sql_flags;
				if ((flags & SQL_FullColNames) != 0) {
					name = tt_sprintf("%s.%s",
							  space_def->name,
							  zCol);
				} else {
					name = zCol;
				}
			}
			vdbe_metadata_set_col_name(v, i, name);
			if (is_full_meta) {
				bool is_nullable =
					space_def->fields[iCol].is_nullable;
				vdbe_metadata_set_col_nullability(v, i,
								  is_nullable);
				if (space->sequence != NULL &&
				    space->sequence_fieldno == (uint32_t) iCol)
					vdbe_metadata_set_col_autoincrement(v, i);
				if (span != NULL)
					vdbe_metadata_set_col_span(v, i, span);
			}
		} else {
			const char *z = NULL;
			if (colname != NULL) {
				z = colname;
			} else {
				uint32_t idx = ++pParse->autoname_i;
				z = sql_generate_column_name(idx);
			}
			vdbe_metadata_set_col_name(v, i, z);
			if (is_full_meta)
				vdbe_metadata_set_col_span(v, i, span);
		}
	}
	if (var_count == 0)
		return;
	v->var_pos = (uint32_t *) malloc(var_count * sizeof(uint32_t));
	if (v->var_pos ==  NULL) {
		diag_set(OutOfMemory, var_count * sizeof(uint32_t),
			 "malloc", "v->var_pos");
		return;
	}
	memcpy(v->var_pos, var_pos, var_count * sizeof(uint32_t));
	v->res_var_count = var_count;
}

/*
 * Given an expression list (which is really the list of expressions
 * that form the result set of a SELECT statement) compute appropriate
 * column names for a table that would hold the expression list.
 *
 * All column names will be unique.
 *
 * Only the column names are computed.  Column.zType, Column.zColl,
 * and other fields of Column are zeroed.
 *
 * Return 0 on success.  If a memory allocation error occurs,
 * store NULL in *paCol and 0 in *pnCol and return -1.
 */
int
sqlColumnsFromExprList(Parse * parse, ExprList * expr_list,
			   struct space_def *space_def)
{
	/* Database connection */
	sql *db = parse->db;
	u32 cnt;		/* Index added to make the name unique */
	char *zName;		/* Column name */
	int nName;		/* Size of name in zName[] */
	Hash ht;		/* Hash table of column names */

	sqlHashInit(&ht);
	uint32_t column_count =
		expr_list != NULL ? (uint32_t)expr_list->nExpr : 0;
	/*
	 * This should be a table without resolved columns.
	 * sqlViewGetColumnNames could use it to resolve
	 * names for existing table.
	 */
	assert(space_def->fields == NULL);
	struct region *region = &parse->region;
	size_t size;
	space_def->fields =
		region_alloc_array(region, typeof(space_def->fields[0]),
				   column_count, &size);
	if (space_def->fields == NULL) {
		sqlOomFault(db);
		goto cleanup;
	}
	for (uint32_t i = 0; i < column_count; i++) {
		memcpy(&space_def->fields[i], &field_def_default,
		       sizeof(field_def_default));
		space_def->fields[i].nullable_action = ON_CONFLICT_ACTION_NONE;
		space_def->fields[i].is_nullable = true;
	}
	space_def->field_count = column_count;

	for (uint32_t i = 0; i < column_count; i++) {
		/*
		 * Check if the column contains an "AS <name>"
		 * phrase.
		 */
		if ((zName = expr_list->a[i].zName) == 0) {
			struct Expr *pColExpr = expr_list->a[i].pExpr;
			struct space_def *space_def = NULL;
			while (pColExpr->op == TK_DOT) {
				pColExpr = pColExpr->pRight;
				assert(pColExpr != 0);
			}
			if (pColExpr->op == TK_COLUMN_REF
			    && ALWAYS(pColExpr->space_def != NULL)) {
				/* For columns use the column name name */
				int iCol = pColExpr->iColumn;
				assert(iCol >= 0);
				space_def = pColExpr->space_def;
				zName = space_def->fields[iCol].name;
			} else if (pColExpr->op == TK_ID) {
				assert(!ExprHasProperty(pColExpr, EP_IntValue));
				zName = pColExpr->u.zToken;
			}
		}
		if (zName == NULL) {
			uint32_t idx = ++parse->autoname_i;
			zName = sqlDbStrDup(db, sql_generate_column_name(idx));
		} else {
			zName = sqlDbStrDup(db, zName);
		}

		/* Make sure the column name is unique.  If the name is not unique,
		 * append an integer to the name so that it becomes unique.
		 */
		cnt = 0;
		while (zName && sqlHashFind(&ht, zName) != 0) {
			nName = sqlStrlen30(zName);
			if (nName > 0) {
				int j;
				for (j = nName - 1;
				     j > 0 && sqlIsdigit(zName[j]); j--);
				if (zName[j] == '_')
					nName = j;
			}
			zName =
			    sqlMPrintf(db, "%.*z_%u", nName, zName, ++cnt);
		}
		size_t name_len = strlen(zName);
		void *field = &space_def->fields[i];
		if (zName != NULL &&
		    sqlHashInsert(&ht, zName, field) == field)
			sqlOomFault(db);
		space_def->fields[i].name = region_alloc(region, name_len + 1);
		if (space_def->fields[i].name == NULL) {
			sqlOomFault(db);
			goto cleanup;
		} else {
			memcpy(space_def->fields[i].name, zName, name_len);
			space_def->fields[i].name[name_len] = '\0';
		}
	}
cleanup:
	sqlHashClear(&ht);
	if (db->mallocFailed) {
		/*
		 * pTable->def could be not temporal in
		 * sqlViewGetColumnNames so we need clean-up.
		 */
		space_def->fields = NULL;
		space_def->field_count = 0;
		return -1;
	}
	return 0;

}

/*
 * Add type and collation information to a column list based on
 * a SELECT statement.
 *
 * The column list presumably came from selectColumnNamesFromExprList().
 * The column list has only names, not types or collations.  This
 * routine goes through and adds the types and collations.
 *
 * This routine requires that all identifiers in the SELECT
 * statement be resolved.
 */
void
sqlSelectAddColumnTypeAndCollation(struct Parse *pParse,
				       struct space_def *def,
				       struct Select *pSelect)
{
	sql *db = pParse->db;
	NameContext sNC;
	Expr *p;
	struct ExprList_item *a;

	assert(pSelect != 0);
	assert((pSelect->selFlags & SF_Resolved) != 0);
	assert((int)def->field_count == pSelect->pEList->nExpr ||
	       db->mallocFailed);
	if (db->mallocFailed)
		return;
	memset(&sNC, 0, sizeof(sNC));
	sNC.pSrcList = pSelect->pSrc;
	a = pSelect->pEList->a;
	for (uint32_t i = 0; i < def->field_count; i++) {
		p = a[i].pExpr;
		def->fields[i].type = sql_expr_type(p);
		bool is_found;
		uint32_t coll_id;
		struct coll *unused;
		if (def->fields[i].coll_id == COLL_NONE &&
		    sql_expr_coll(pParse, p, &is_found, &coll_id,
				  &unused) == 0 && coll_id != COLL_NONE)
			def->fields[i].coll_id = coll_id;
	}
}

/*
 * Given a SELECT statement, generate a space structure that describes
 * the result set of that SELECT.
 */
struct space *
sqlResultSetOfSelect(Parse * pParse, Select * pSelect)
{
	sql *db = pParse->db;

	uint32_t saved_flags = pParse->sql_flags;
	pParse->sql_flags = 0;
	sqlSelectPrep(pParse, pSelect, 0);
	if (pParse->is_aborted)
		return NULL;
	while (pSelect->pPrior)
		pSelect = pSelect->pPrior;
	pParse->sql_flags = saved_flags;
	struct space *space = sql_ephemeral_space_new(pParse, NULL);
	if (space == NULL)
		return NULL;
	/* The sqlResultSetOfSelect() is only used in contexts where lookaside
	 * is disabled
	 */
	assert(db->lookaside.bDisable);
	sqlColumnsFromExprList(pParse, pSelect->pEList, space->def);
	sqlSelectAddColumnTypeAndCollation(pParse, space->def, pSelect);
	if (db->mallocFailed)
		return NULL;
	return space;
}

/*
 * Get a VDBE for the given parser context.  Create a new one if necessary.
 * If an error occurs, return NULL and leave a message in pParse.
 */
static SQL_NOINLINE Vdbe *
allocVdbe(Parse * pParse)
{
	Vdbe *v = pParse->pVdbe = sqlVdbeCreate(pParse);
	if (v == NULL)
		return NULL;
	v->sql_flags = pParse->sql_flags;
	sqlVdbeAddOp2(v, OP_Init, 0, 1);
	if (pParse->pToplevel == 0
	    && OptimizationEnabled(pParse->db, SQL_FactorOutConst)
	    ) {
		pParse->okConstFactor = 1;
	}
	return v;
}

Vdbe *
sqlGetVdbe(Parse * pParse)
{
	Vdbe *v = pParse->pVdbe;
	return v ? v : allocVdbe(pParse);
}

/*
 * Compute the iLimit and iOffset fields of the SELECT based on the
 * pLimit and pOffset expressions.  pLimit and pOffset hold the expressions
 * that appear in the original SQL statement after the LIMIT and OFFSET
 * keywords.  Or NULL if those keywords are omitted. iLimit and iOffset
 * are the integer memory register numbers for counters used to compute
 * the limit and offset.  If there is no limit and/or offset, then
 * iLimit and iOffset are negative.
 *
 * This routine changes the values of iLimit and iOffset only if
 * a limit or offset is defined by pLimit and pOffset.  iLimit and
 * iOffset should have been preset to appropriate default values (zero)
 * prior to calling this routine.
 *
 * The iOffset register (if it exists) is initialized to the value
 * of the OFFSET.  The iLimit register is initialized to LIMIT.  Register
 * iOffset+1 is initialized to LIMIT+OFFSET.
 *
 * Only if pLimit!=0 or pOffset!=0 do the limit registers get
 * redefined.  The UNION ALL operator uses this property to force
 * the reuse of the same limit and offset registers across multiple
 * SELECT statements.
 */
static void
computeLimitRegisters(Parse * pParse, Select * p, int iBreak)
{
	Vdbe *v = 0;
	int iLimit = 0;
	int iOffset;
	if (p->iLimit)
		return;

	/*
	 * "LIMIT -1" always shows all rows.  There is some
	 * controversy about what the correct behavior should be.
	 * The current implementation interprets "LIMIT 0" to mean
	 * no rows.
	 */
	sqlExprCacheClear(pParse);
	assert(p->pOffset == 0 || p->pLimit != 0);
	if (p->pLimit) {
		if((p->pLimit->flags & EP_Collate) != 0 ||
		   (p->pOffset != NULL &&
		   (p->pOffset->flags & EP_Collate) != 0)) {
			diag_set(ClientError, ER_SQL_SYNTAX_NEAR_TOKEN,
				 pParse->line_count, sizeof("COLLATE"),
				 "COLLATE");
			pParse->is_aborted = true;
			return;
		}
		p->iLimit = iLimit = ++pParse->nMem;
		v = sqlGetVdbe(pParse);
		assert(v != 0);
		int positive_limit_label = sqlVdbeMakeLabel(v);
		int halt_label = sqlVdbeMakeLabel(v);
		sqlExprCode(pParse, p->pLimit, iLimit);
		sqlVdbeAddOp2(v, OP_MustBeInt, iLimit, halt_label);
		/* If LIMIT clause >= 0 continue execution */
		int r1 = sqlGetTempReg(pParse);
		sqlVdbeAddOp2(v, OP_Integer, 0, r1);
		sqlVdbeAddOp3(v, OP_Ge, r1, positive_limit_label, iLimit);
		/* Otherwise return an error and stop */
		const char *err = tt_sprintf(tnt_errcode_desc(ER_SQL_EXECUTE),
					     "Only positive integers are "\
					     "allowed in the LIMIT clause");
		sqlVdbeResolveLabel(v, halt_label);
		sqlVdbeAddOp4(v, OP_SetDiag, ER_SQL_EXECUTE, 0, 0, err,
			      P4_STATIC);
		sqlVdbeAddOp1(v, OP_Halt, -1);

		sqlVdbeResolveLabel(v, positive_limit_label);
		VdbeCoverage(v);
		VdbeComment((v, "LIMIT counter"));
		sqlVdbeAddOp3(v, OP_Eq, r1, iBreak, iLimit);
		sqlReleaseTempReg(pParse, r1);

		if ((p->selFlags & SF_SingleRow) != 0) {
			if (ExprHasProperty(p->pLimit, EP_System)) {
				/*
				 * Indirect LIMIT 1 is allowed only for
				 * requests returning only 1 row.
				 * To test this, we change LIMIT 1 to
				 * LIMIT 2 and will look up LIMIT 1 overflow
				 * at the sqlSelect end.
				 */
				sqlVdbeAddOp2(v, OP_Integer, 2, iLimit);
			} else {
				/*
				 * User-defined complex limit for subquery
				 * could be only 1 as resulting value.
				 */
				int r1 = sqlGetTempReg(pParse);
				sqlVdbeAddOp2(v, OP_Integer, 1, r1);
				int no_err = sqlVdbeMakeLabel(v);
				sqlVdbeAddOp3(v, OP_Eq, iLimit, no_err, r1);
				err = tnt_errcode_desc(ER_SQL_EXECUTE);
				err = tt_sprintf(err, "Expression subquery "\
						 "could be limited only "\
						 "with 1");
				sqlVdbeAddOp4(v, OP_SetDiag, ER_SQL_EXECUTE, 0,
					      0, err, P4_STATIC);
				sqlVdbeAddOp1(v, OP_Halt, -1);
				sqlVdbeResolveLabel(v, no_err);
				sqlReleaseTempReg(pParse, r1);

				/* Runtime checks are no longer needed. */
				p->selFlags &= ~SF_SingleRow;
			}
		}
		if (p->pOffset) {
			int positive_offset_label = sqlVdbeMakeLabel(v);
			int offset_error_label = sqlVdbeMakeLabel(v);
			p->iOffset = iOffset = ++pParse->nMem;
			pParse->nMem++;	/* Allocate an extra register for limit+offset */
			sqlExprCode(pParse, p->pOffset, iOffset);
			sqlVdbeAddOp2(v, OP_MustBeInt, iOffset, offset_error_label);
			/* If OFFSET clause >= 0 continue execution */
            		int r1 = sqlGetTempReg(pParse);
            		sqlVdbeAddOp2(v, OP_Integer, 0, r1);

            		sqlVdbeAddOp3(v, OP_Ge, r1, positive_offset_label, iOffset);
			/* Otherwise return an error and stop */
			err = tt_sprintf(tnt_errcode_desc(ER_SQL_EXECUTE),
					 "Only positive integers are allowed "\
					 "in the OFFSET clause");
			sqlVdbeResolveLabel(v, offset_error_label);
			sqlVdbeAddOp4(v, OP_SetDiag, ER_SQL_EXECUTE, 0, 0, err,
				      P4_STATIC);
			sqlVdbeAddOp1(v, OP_Halt, -1);

			sqlVdbeResolveLabel(v, positive_offset_label);
            		sqlReleaseTempReg(pParse, r1);
			VdbeCoverage(v);
			VdbeComment((v, "OFFSET counter"));
			sqlVdbeAddOp3(v, OP_OffsetLimit, iLimit,
					  iOffset + 1, iOffset);
			VdbeComment((v, "LIMIT+OFFSET"));
		}
	}
}

/**
 * This function determines resulting collation sequence for
 * @n-th column of the result set for the compound SELECT
 * statement. Since compound SELECT performs implicit comparisons
 * between values, all parts of compound queries must use
 * the same collation. Otherwise, an error is raised.
 *
 * @param parser Parse context.
 * @param p Select meta-information.
 * @param n Column number of the result set.
 * @param is_forced_coll Used if we fall into recursion.
 *        For most-outer call it is unused. Used to indicate that
 *        explicit COLLATE clause is used.
 * @retval Id of collation to be used during string comparison.
 */
static uint32_t
multi_select_coll_seq_r(struct Parse *parser, struct Select *p, int n,
			bool *is_forced_coll)
{
	bool is_prior_forced = false;
	bool is_current_forced;
	uint32_t prior_coll_id = COLL_NONE;
	uint32_t current_coll_id;
	if (p->pPrior != NULL) {
		prior_coll_id = multi_select_coll_seq_r(parser, p->pPrior, n,
							&is_prior_forced);
	}
	/*
	 * Column number must be less than p->pEList->nExpr.
	 * Otherwise an error would have been thrown during name
	 * resolution and we would not have got this far.
	 */
	assert(n >= 0 && n < p->pEList->nExpr);
	struct coll *unused;
	if (sql_expr_coll(parser, p->pEList->a[n].pExpr, &is_current_forced,
			  &current_coll_id, &unused) != 0)
		return 0;
	uint32_t res_coll_id;
	if (collations_check_compatibility(prior_coll_id, is_prior_forced,
					   current_coll_id, is_current_forced,
					   &res_coll_id) != 0) {
		parser->is_aborted = true;
		return 0;
	}
	*is_forced_coll = (is_prior_forced || is_current_forced);
	return res_coll_id;
}

static inline uint32_t
multi_select_coll_seq(struct Parse *parser, struct Select *p, int n)
{
	bool unused;
	return multi_select_coll_seq_r(parser, p, n, &unused);
}

/**
 * The select statement passed as the second parameter is a
 * compound SELECT with an ORDER BY clause. This function
 * allocates and returns a sql_key_info structure suitable for
 * implementing the ORDER BY.
 *
 * Space to hold the sql_key_info structure is obtained from malloc.
 * The calling function is responsible for ensuring that this
 * structure is eventually freed.
 *
 * @param parse Parsing context.
 * @param s Select struct to analyze.
 * @param extra No of extra slots to allocate.
 *
 * @retval Allocated key_info, NULL in case of OOM.
 */
static struct sql_key_info *
sql_multiselect_orderby_to_key_info(struct Parse *parse, struct Select *s,
				   int extra)
{
	int ob_count = s->pOrderBy->nExpr;
	struct sql_key_info *key_info = sql_key_info_new(parse->db,
							 ob_count + extra);
	if (key_info == NULL) {
		sqlOomFault(parse->db);
		return NULL;
	}

	ExprList *order_by = s->pOrderBy;
	for (int i = 0; i < ob_count; i++) {
		struct key_part_def *part = &key_info->parts[i];
		struct ExprList_item *item = &order_by->a[i];
		struct Expr *term = item->pExpr;
		uint32_t id;
		bool unused;
		if ((term->flags & EP_Collate) != 0) {
			struct coll *unused_coll;
			if (sql_expr_coll(parse, term, &unused, &id,
					  &unused_coll) != 0)
				return 0;
		} else {
			id = multi_select_coll_seq(parse, s,
						   item->u.x.iOrderByCol - 1);
			if (id != COLL_NONE) {
				const char *name = coll_by_id(id)->name;
				order_by->a[i].pExpr =
					sqlExprAddCollateString(parse, term,
								    name);
			}
		}
		part->coll_id = id;
		part->sort_order = order_by->a[i].sort_order;
	}

	return key_info;
}

/*
 * This routine generates VDBE code to compute the content of a WITH RECURSIVE
 * query of the form:
 *
 *   <recursive-table> AS (<setup-query> UNION [ALL] <recursive-query>)
 *                         \___________/             \_______________/
 *                           p->pPrior                      p
 *
 *
 * There is exactly one reference to the recursive-table in the FROM clause
 * of recursive-query, marked with the SrcList->a[].fg.isRecursive flag.
 *
 * The setup-query runs once to generate an initial set of rows that go
 * into a Queue table.  Rows are extracted from the Queue table one by
 * one.  Each row extracted from Queue is output to pDest.  Then the single
 * extracted row (now in the iCurrent table) becomes the content of the
 * recursive-table for a recursive-query run.  The output of the recursive-query
 * is added back into the Queue table.  Then another row is extracted from Queue
 * and the iteration continues until the Queue table is empty.
 *
 * If the compound query operator is UNION then no duplicate rows are ever
 * inserted into the Queue table.  The iDistinct table keeps a copy of all rows
 * that have ever been inserted into Queue and causes duplicates to be
 * discarded.  If the operator is UNION ALL, then duplicates are allowed.
 *
 * If the query has an ORDER BY, then entries in the Queue table are kept in
 * ORDER BY order and the first entry is extracted for each cycle.  Without
 * an ORDER BY, the Queue table is just a FIFO.
 *
 * If a LIMIT clause is provided, then the iteration stops after LIMIT rows
 * have been output to pDest.  A LIMIT of zero means to output no rows and a
 * negative LIMIT means to output all rows.  If there is also an OFFSET clause
 * with a positive value, then the first OFFSET outputs are discarded rather
 * than being sent to pDest.  The LIMIT count does not begin until after OFFSET
 * rows have been skipped.
 */
static void
generateWithRecursiveQuery(Parse * pParse,	/* Parsing context */
			   Select * p,		/* The recursive SELECT to be coded */
			   SelectDest * pDest)	/* What to do with query results */
{
	SrcList *pSrc = p->pSrc;	/* The FROM clause of the recursive query */
	int nCol = p->pEList->nExpr;	/* Number of columns in the recursive table */
	Vdbe *v = pParse->pVdbe;	/* The prepared statement under construction */
	Select *pSetup = p->pPrior;	/* The setup query */
	int addrTop;		/* Top of the loop */
	int addrCont, addrBreak;	/* CONTINUE and BREAK addresses */
	int iCurrent = 0;	/* The Current table */
	int regCurrent;		/* Register holding Current table */
	int iQueue;		/* The Queue table */
	int iDistinct = 0;	/* To ensure unique results if UNION */
	int eDest = SRT_Fifo;	/* How to write to Queue */
	SelectDest destQueue;	/* SelectDest targetting the Queue table */
	int i;			/* Loop counter */
	int rc;			/* Result code */
	ExprList *pOrderBy;	/* The ORDER BY clause */
	Expr *pLimit, *pOffset;	/* Saved LIMIT and OFFSET */
	int regLimit, regOffset;	/* Registers used by LIMIT and OFFSET */

	/* Process the LIMIT and OFFSET clauses, if they exist */
	addrBreak = sqlVdbeMakeLabel(v);
	p->nSelectRow = 320;	/* 4 billion rows */
	computeLimitRegisters(pParse, p, addrBreak);
	pLimit = p->pLimit;
	pOffset = p->pOffset;
	regLimit = p->iLimit;
	regOffset = p->iOffset;
	p->pLimit = p->pOffset = 0;
	p->iLimit = p->iOffset = 0;
	pOrderBy = p->pOrderBy;

	/* Locate the cursor number of the Current table */
	for (i = 0; ALWAYS(i < pSrc->nSrc); i++) {
		if (pSrc->a[i].fg.isRecursive) {
			iCurrent = pSrc->a[i].iCursor;
			break;
		}
	}

	/* Allocate cursors numbers for Queue and Distinct.  The cursor number for
	 * the Distinct table must be exactly one greater than Queue in order
	 * for the SRT_DistFifo and SRT_DistQueue destinations to work.
	 */
	iQueue = pParse->nTab++;
	int reg_queue = ++pParse->nMem;
	int reg_dist = 0;
	if (p->op == TK_UNION) {
		eDest = pOrderBy ? SRT_DistQueue : SRT_DistFifo;
		iDistinct = pParse->nTab++;
		reg_dist = ++pParse->nMem;
	} else {
		eDest = pOrderBy ? SRT_Queue : SRT_Fifo;
	}
	sqlSelectDestInit(&destQueue, eDest, iQueue, reg_queue);

	/* Allocate cursors for Current, Queue, and Distinct. */
	regCurrent = ++pParse->nMem;
	sqlVdbeAddOp3(v, OP_OpenPseudo, iCurrent, regCurrent, nCol);
	if (pOrderBy) {
		struct sql_key_info *key_info =
			sql_multiselect_orderby_to_key_info(pParse, p, 1);
		sqlVdbeAddOp4(v, OP_OpenTEphemeral, reg_queue,
				  pOrderBy->nExpr + 2, 0, (char *)key_info,
				  P4_KEYINFO);
		VdbeComment((v, "Orderby table"));
		destQueue.pOrderBy = pOrderBy;
	} else {
		sqlVdbeAddOp2(v, OP_OpenTEphemeral, reg_queue, nCol + 1);
		VdbeComment((v, "Queue table"));
	}
	sqlVdbeAddOp3(v, OP_IteratorOpen, iQueue, 0, reg_queue);
	if (iDistinct) {
		p->addrOpenEphm[0] =
		    sqlVdbeAddOp2(v, OP_OpenTEphemeral, reg_dist, 1);
		sqlVdbeAddOp3(v, OP_IteratorOpen, iDistinct, 0, reg_dist);
		p->selFlags |= SF_UsesEphemeral;
		VdbeComment((v, "Distinct table"));
	}

	/* Detach the ORDER BY clause from the compound SELECT */
	p->pOrderBy = 0;

	/* Store the results of the setup-query in Queue. */
	pSetup->pNext = 0;
	rc = sqlSelect(pParse, pSetup, &destQueue);
	pSetup->pNext = p;
	if (rc)
		goto end_of_recursive_query;

	/* Find the next row in the Queue and output that row */
	addrTop = sqlVdbeAddOp2(v, OP_Rewind, iQueue, addrBreak);
	VdbeCoverage(v);

	/* Transfer the next row in Queue over to Current */
	sqlVdbeAddOp1(v, OP_NullRow, iCurrent);	/* To reset column cache */
	if (pOrderBy) {
		sqlVdbeAddOp3(v, OP_Column, iQueue, pOrderBy->nExpr + 1,
				  regCurrent);
	} else {
		sqlVdbeAddOp2(v, OP_RowData, iQueue, regCurrent);
	}
	sqlVdbeAddOp1(v, OP_Delete, iQueue);

	/* Output the single row in Current */
	addrCont = sqlVdbeMakeLabel(v);
	codeOffset(v, regOffset, addrCont);
	selectInnerLoop(pParse, p, p->pEList, iCurrent,
			0, 0, pDest, addrCont, addrBreak);
	if (regLimit) {
		sqlVdbeAddOp2(v, OP_DecrJumpZero, regLimit, addrBreak);
		VdbeCoverage(v);
	}
	sqlVdbeResolveLabel(v, addrCont);

	/* Execute the recursive SELECT taking the single row in Current as
	 * the value for the recursive-table. Store the results in the Queue.
	 */
	if (p->selFlags & SF_Aggregate) {
		diag_set(ClientError, ER_UNSUPPORTED, "Tarantool",
			 "recursive aggregate queries");
		pParse->is_aborted = true;
	} else {
		p->pPrior = 0;
		sqlSelect(pParse, p, &destQueue);
		assert(p->pPrior == 0);
		p->pPrior = pSetup;
	}

	/* Keep running the loop until the Queue is empty */
	sqlVdbeGoto(v, addrTop);
	sqlVdbeResolveLabel(v, addrBreak);

 end_of_recursive_query:
	sql_expr_list_delete(pParse->db, p->pOrderBy);
	p->pOrderBy = pOrderBy;
	p->pLimit = pLimit;
	p->pOffset = pOffset;
	return;
}

/* Forward references */
static int multiSelectOrderBy(Parse * pParse,	/* Parsing context */
			      Select * p,	/* The right-most of SELECTs to be coded */
			      SelectDest * pDest	/* What to do with query results */
    );

/**
 * Handle the special case of a compound-select that originates
 * from a VALUES clause.  By handling this as a special case, we
 * avoid deep recursion, and thus do not need to enforce the
 * SQL_LIMIT_COMPOUND_SELECT on a VALUES clause.
 *
 * Because the Select object originates from a VALUES clause:
 *   (1) It has no LIMIT or OFFSET
 *   (2) All terms are UNION ALL
 *   (3) There is no ORDER BY clause
 *
 * @param pParse Parsing context.
 * @param p The right-most of SELECTs to be coded.
 * @param pDest What to do with query results.
 * @retval 0 On success, not 0 elsewhere.
 */
static int
multiSelectValues(struct Parse *pParse, struct Select *p,
		  struct SelectDest *pDest)
{
	Select *pPrior;
	int nRow = 1;
	int rc = 0;
	assert(p->selFlags & SF_MultiValue);
	do {
		assert(p->selFlags & SF_Values);
		assert(p->op == TK_ALL
		       || (p->op == TK_SELECT && p->pPrior == 0));
		assert(p->pLimit == 0);
		assert(p->pOffset == 0);
		assert(p->pNext == 0
		       || p->pEList->nExpr == p->pNext->pEList->nExpr);
		if (p->pPrior == 0)
			break;
		assert(p->pPrior->pNext == p);
		p = p->pPrior;
		nRow++;
	} while (1);
	while (p) {
		pPrior = p->pPrior;
		p->pPrior = 0;
		rc = sqlSelect(pParse, p, pDest);
		p->pPrior = pPrior;
		if (rc)
			break;
		p->nSelectRow = nRow;
		p = p->pNext;
	}
	return rc;
}

/*
 * This routine is called to process a compound query form from
 * two or more separate queries using UNION, UNION ALL, EXCEPT, or
 * INTERSECT
 *
 * "p" points to the right-most of the two queries.  the query on the
 * left is p->pPrior.  The left query could also be a compound query
 * in which case this routine will be called recursively.
 *
 * The results of the total query are to be written into a destination
 * of type eDest with parameter iParm.
 *
 * Example 1:  Consider a three-way compound SQL statement.
 *
 *     SELECT a FROM t1 UNION SELECT b FROM t2 UNION SELECT c FROM t3
 *
 * This statement is parsed up as follows:
 *
 *     SELECT c FROM t3
 *      |
 *      `----->  SELECT b FROM t2
 *                |
 *                `------>  SELECT a FROM t1
 *
 * The arrows in the diagram above represent the Select.pPrior pointer.
 * So if this routine is called with p equal to the t3 query, then
 * pPrior will be the t2 query.  p->op will be TK_UNION in this case.
 *
 * Notice that because of the way sql parses compound SELECTs, the
 * individual selects always group from left to right.
 */
static int
multiSelect(Parse * pParse,	/* Parsing context */
	    Select * p,		/* The right-most of SELECTs to be coded */
	    SelectDest * pDest)	/* What to do with query results */
{
	int rc = 0;	/* Success code from a subroutine */
	Select *pPrior;		/* Another SELECT immediately to our left */
	Vdbe *v;		/* Generate code to this VDBE */
	SelectDest dest;	/* Alternative data destination */
	Select *pDelete = 0;	/* Chain of simple selects to delete */
	sql *db;		/* Database connection */
	int iSub1 = 0;		/* EQP id of left-hand query */
	int iSub2 = 0;		/* EQP id of right-hand query */

	/* Make sure there is no ORDER BY or LIMIT clause on prior SELECTs.  Only
	 * the last (right-most) SELECT in the series may have an ORDER BY or LIMIT.
	 */
	assert(p && p->pPrior);	/* Calling function guarantees this much */
	assert((p->selFlags & SF_Recursive) == 0 || p->op == TK_ALL
	       || p->op == TK_UNION);
	db = pParse->db;
	pPrior = p->pPrior;
	dest = *pDest;
	if (pPrior->pOrderBy) {
		const char *err_msg =
			tt_sprintf("ORDER BY clause should come after %s not "\
				   "before", sql_select_op_name(p->op));
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, err_msg);
		pParse->is_aborted = true;
		rc = 1;
		goto multi_select_end;
	}
	if (pPrior->pLimit) {
		const char *err_msg =
			tt_sprintf("LIMIT clause should come after %s not "\
				   "before", sql_select_op_name(p->op));
		diag_set(ClientError, ER_SQL_PARSER_GENERIC, err_msg);
		pParse->is_aborted = true;
		rc = 1;
		goto multi_select_end;
	}

	v = sqlGetVdbe(pParse);
	assert(v != 0);		/* The VDBE already created by calling function */

	/* Create the destination temporary table if necessary
	 */
	if (dest.eDest == SRT_EphemTab) {
		assert(p->pEList);
		int nCols = p->pEList->nExpr;
		sqlVdbeAddOp2(v, OP_OpenTEphemeral, dest.reg_eph, nCols + 1);
		sqlVdbeAddOp3(v, OP_IteratorOpen, dest.iSDParm, 0, dest.reg_eph);
		VdbeComment((v, "Destination temp"));
		dest.eDest = SRT_Table;
	}

	/* Special handling for a compound-select that originates as a VALUES clause.
	 */
	if (p->selFlags & SF_MultiValue) {
		rc = multiSelectValues(pParse, p, &dest);
		goto multi_select_end;
	}

	/* Make sure all SELECTs in the statement have the same number of elements
	 * in their result sets.
	 */
	assert(p->pEList && pPrior->pEList);
	assert(p->pEList->nExpr == pPrior->pEList->nExpr);

	if (p->selFlags & SF_Recursive) {
		generateWithRecursiveQuery(pParse, p, &dest);
	} else

		/* Compound SELECTs that have an ORDER BY clause are handled separately.
		 */
	if (p->pOrderBy) {
		return multiSelectOrderBy(pParse, p, pDest);
	} else
		/* Generate code for the left and right SELECT statements.
		 */
		switch (p->op) {
		case TK_ALL:{
				int addr = 0;
				int nLimit;
				assert(!pPrior->pLimit);
				pPrior->iLimit = p->iLimit;
				pPrior->iOffset = p->iOffset;
				pPrior->pLimit = p->pLimit;
				pPrior->pOffset = p->pOffset;
				iSub1 = pParse->iNextSelectId;
				rc = sqlSelect(pParse, pPrior, &dest);
				p->pLimit = 0;
				p->pOffset = 0;
				if (rc) {
					goto multi_select_end;
				}
				p->pPrior = 0;
				p->iLimit = pPrior->iLimit;
				p->iOffset = pPrior->iOffset;
				if (p->iLimit) {
					int r1 = sqlGetTempReg(pParse);
					sqlVdbeAddOp2(v, OP_Integer, 0, r1);
					addr = sqlVdbeAddOp3(v, OP_Eq, r1, 0,
							     p->iLimit);
					sqlReleaseTempReg(pParse, r1);
					VdbeComment((v,
						     "Jump ahead if LIMIT reached"));
					if (p->iOffset) {
						sqlVdbeAddOp3(v,
								  OP_OffsetLimit,
								  p->iLimit,
								  p->iOffset +
								  1,
								  p->iOffset);
					}
				}
				iSub2 = pParse->iNextSelectId;
				rc = sqlSelect(pParse, p, &dest);
				testcase(rc != 0);
				pDelete = p->pPrior;
				p->pPrior = pPrior;
				p->nSelectRow =
				    sqlLogEstAdd(p->nSelectRow,
						     pPrior->nSelectRow);
				if (pPrior->pLimit
				    && sqlExprIsInteger(pPrior->pLimit,
							    &nLimit)
				    && nLimit > 0
				    && p->nSelectRow > sqlLogEst((u64) nLimit)
				    ) {
					p->nSelectRow =
					    sqlLogEst((u64) nLimit);
				}
				if (addr) {
					sqlVdbeJumpHere(v, addr);
				}
				break;
			}
		case TK_EXCEPT:
		case TK_UNION:{
				int unionTab;	/* Cursor number of the temporary table holding result */
				int reg_union;
				u8 op = 0;	/* One of the SRT_ operations to apply to self */
				int priorOp;	/* The SRT_ operation to apply to prior selects */
				Expr *pLimit, *pOffset;	/* Saved values of p->nLimit and p->nOffset */
				int addr;
				SelectDest uniondest;

				testcase(p->op == TK_EXCEPT);
				testcase(p->op == TK_UNION);
				priorOp = SRT_Union;
				if (dest.eDest == priorOp) {
					/* We can reuse a temporary table generated by a SELECT to our
					 * right.
					 */
					assert(p->pLimit == 0);	/* Not allowed on leftward elements */
					assert(p->pOffset == 0);	/* Not allowed on leftward elements */
					unionTab = dest.iSDParm;
					reg_union = dest.reg_eph;
				} else {
					/* We will need to create our own temporary table to hold the
					 * intermediate results.
					 */
					unionTab = pParse->nTab++;
					reg_union = ++pParse->nMem;
					assert(p->pOrderBy == 0);
					addr =
					    sqlVdbeAddOp2(v,
							      OP_OpenTEphemeral,
							      reg_union, 0);
					sqlVdbeAddOp3(v, OP_IteratorOpen, unionTab, 0, reg_union);
					assert(p->addrOpenEphm[0] == -1);
					p->addrOpenEphm[0] = addr;
					findRightmost(p)->selFlags |=
					    SF_UsesEphemeral;
					assert(p->pEList);
				}

				/* Code the SELECT statements to our left
				 */
				assert(!pPrior->pOrderBy);
				sqlSelectDestInit(&uniondest, priorOp,
						      unionTab, reg_union);
				iSub1 = pParse->iNextSelectId;
				rc = sqlSelect(pParse, pPrior, &uniondest);
				if (rc) {
					goto multi_select_end;
				}

				/* Code the current SELECT statement
				 */
				if (p->op == TK_EXCEPT) {
					op = SRT_Except;
				} else {
					assert(p->op == TK_UNION);
					op = SRT_Union;
				}
				p->pPrior = 0;
				pLimit = p->pLimit;
				p->pLimit = 0;
				pOffset = p->pOffset;
				p->pOffset = 0;
				uniondest.eDest = op;
				iSub2 = pParse->iNextSelectId;
				rc = sqlSelect(pParse, p, &uniondest);
				testcase(rc != 0);
				/* Query flattening in sqlSelect() might refill p->pOrderBy.
				 * Be sure to delete p->pOrderBy, therefore, to avoid a memory leak.
				 */
				sql_expr_list_delete(db, p->pOrderBy);
				pDelete = p->pPrior;
				p->pPrior = pPrior;
				p->pOrderBy = 0;
				if (p->op == TK_UNION) {
					p->nSelectRow =
					    sqlLogEstAdd(p->nSelectRow,
							     pPrior->
							     nSelectRow);
				}
				sql_expr_delete(db, p->pLimit, false);
				p->pLimit = pLimit;
				p->pOffset = pOffset;
				p->iLimit = 0;
				p->iOffset = 0;

				/* Convert the data in the temporary table into whatever form
				 * it is that we currently need.
				 */
				assert(unionTab == dest.iSDParm
				       || dest.eDest != priorOp);
				if (dest.eDest != priorOp) {
					int iCont, iBreak, iStart;
					assert(p->pEList);
					if (dest.eDest == SRT_Output) {
						Select *pFirst = p;
						while (pFirst->pPrior)
							pFirst = pFirst->pPrior;
						generate_column_metadata(pParse,
									 pFirst->pSrc,
									 pFirst->pEList);
					}
					iBreak = sqlVdbeMakeLabel(v);
					iCont = sqlVdbeMakeLabel(v);
					computeLimitRegisters(pParse, p,
							      iBreak);
					sqlVdbeAddOp2(v, OP_Rewind,
							  unionTab, iBreak);
					VdbeCoverage(v);
					iStart = sqlVdbeCurrentAddr(v);
					selectInnerLoop(pParse, p, p->pEList,
							unionTab, 0, 0, &dest,
							iCont, iBreak);
					sqlVdbeResolveLabel(v, iCont);
					sqlVdbeAddOp2(v, OP_Next, unionTab,
							  iStart);
					VdbeCoverage(v);
					sqlVdbeResolveLabel(v, iBreak);
					sqlVdbeAddOp2(v, OP_Close, unionTab,
							  0);
				}
				break;
			}
		default:
			assert(p->op == TK_INTERSECT); {
				int tab1, tab2;
				int reg_eph1, reg_eph2;
				int iCont, iBreak, iStart;
				Expr *pLimit, *pOffset;
				int addr;
				SelectDest intersectdest;
				int r1;

				/* INTERSECT is different from the others since it requires
				 * two temporary tables.  Hence it has its own case.  Begin
				 * by allocating the tables we will need.
				 */
				tab1 = pParse->nTab++;
				reg_eph1 = ++pParse->nMem;
				tab2 = pParse->nTab++;
				reg_eph2 = ++pParse->nMem;
				assert(p->pOrderBy == 0);

				addr =
				    sqlVdbeAddOp2(v, OP_OpenTEphemeral, reg_eph1,
						      0);
				sqlVdbeAddOp3(v, OP_IteratorOpen, tab1, 0, reg_eph1);
				assert(p->addrOpenEphm[0] == -1);
				p->addrOpenEphm[0] = addr;
				findRightmost(p)->selFlags |= SF_UsesEphemeral;
				assert(p->pEList);

				/* Code the SELECTs to our left into temporary table "tab1".
				 */
				sqlSelectDestInit(&intersectdest, SRT_Union,
						      tab1, reg_eph1);
				iSub1 = pParse->iNextSelectId;
				rc = sqlSelect(pParse, pPrior,
						   &intersectdest);
				if (rc) {
					goto multi_select_end;
				}

				/* Code the current SELECT into temporary table "tab2"
				 */
				addr =
				    sqlVdbeAddOp2(v, OP_OpenTEphemeral, reg_eph2,
						      0);
				sqlVdbeAddOp3(v, OP_IteratorOpen, tab2, 0, reg_eph2);
				assert(p->addrOpenEphm[1] == -1);
				p->addrOpenEphm[1] = addr;
				p->pPrior = 0;
				pLimit = p->pLimit;
				p->pLimit = 0;
				pOffset = p->pOffset;
				p->pOffset = 0;
				intersectdest.iSDParm = tab2;
				intersectdest.reg_eph = reg_eph2;
				iSub2 = pParse->iNextSelectId;
				rc = sqlSelect(pParse, p, &intersectdest);
				testcase(rc != 0);
				pDelete = p->pPrior;
				p->pPrior = pPrior;
				if (p->nSelectRow > pPrior->nSelectRow)
					p->nSelectRow = pPrior->nSelectRow;
				sql_expr_delete(db, p->pLimit, false);
				p->pLimit = pLimit;
				p->pOffset = pOffset;

				/* Generate code to take the intersection of the two temporary
				 * tables.
				 */
				assert(p->pEList);
				if (dest.eDest == SRT_Output) {
					Select *pFirst = p;
					while (pFirst->pPrior)
						pFirst = pFirst->pPrior;
					generate_column_metadata(pParse,
								 pFirst->pSrc,
								 pFirst->pEList);
				}
				iBreak = sqlVdbeMakeLabel(v);
				iCont = sqlVdbeMakeLabel(v);
				computeLimitRegisters(pParse, p, iBreak);
				sqlVdbeAddOp2(v, OP_Rewind, tab1, iBreak);
				VdbeCoverage(v);
				r1 = sqlGetTempReg(pParse);
				iStart =
				    sqlVdbeAddOp2(v, OP_RowData, tab1, r1);
				sqlVdbeAddOp4Int(v, OP_NotFound, tab2,
						     iCont, r1, 0);
				VdbeCoverage(v);
				sqlReleaseTempReg(pParse, r1);
				selectInnerLoop(pParse, p, p->pEList, tab1,
						0, 0, &dest, iCont, iBreak);
				sqlVdbeResolveLabel(v, iCont);
				sqlVdbeAddOp2(v, OP_Next, tab1, iStart);
				VdbeCoverage(v);
				sqlVdbeResolveLabel(v, iBreak);
				sqlVdbeAddOp2(v, OP_Close, tab2, 0);
				sqlVdbeAddOp2(v, OP_Close, tab1, 0);
				break;
			}
		}

	explainComposite(pParse, p->op, iSub1, iSub2, p->op != TK_ALL);

	/* Compute collating sequences used by
	 * temporary tables needed to implement the compound select.
	 * Attach the key_info structure to all temporary tables.
	 *
	 * This section is run by the right-most SELECT statement only.
	 * SELECT statements to the left always skip this part.  The right-most
	 * SELECT might also skip this part if it has no ORDER BY clause and
	 * no temp tables are required.
	 */
	if (p->selFlags & SF_UsesEphemeral) {
		assert(p->pNext == NULL);
		int nCol = p->pEList->nExpr;
		struct sql_key_info *key_info = sql_key_info_new(db, nCol);
		if (key_info == NULL)
			goto multi_select_end;
		for (int i = 0; i < nCol; i++) {
			key_info->parts[i].coll_id =
				multi_select_coll_seq(pParse, p, i);
		}

		for (struct Select *pLoop = p; pLoop; pLoop = pLoop->pPrior) {
			for (int i = 0; i < 2; i++) {
				int addr = pLoop->addrOpenEphm[i];
				if (addr < 0) {
					/* If [0] is unused then [1] is also unused.  So we can
					 * always safely abort as soon as the first unused slot is found
					 */
					assert(pLoop->addrOpenEphm[1] < 0);
					break;
				}
				sqlVdbeChangeP2(v, addr, nCol);
				sqlVdbeChangeP4(v, addr,
						    (char *)sql_key_info_ref(key_info),
						    P4_KEYINFO);
				pLoop->addrOpenEphm[i] = -1;
			}
		}
		sql_key_info_unref(key_info);
	}

 multi_select_end:
	pDest->iSdst = dest.iSdst;
	pDest->nSdst = dest.nSdst;
	sql_select_delete(db, pDelete);
	return rc;
}

/**
 * Code an output subroutine for a coroutine implementation of a
 * SELECT statment.
 *
 * The data to be output is contained in pIn->iSdst.  There are
 * pIn->nSdst columns to be output.  pDest is where the output
 * should be sent.
 *
 * regReturn is the number of the register holding the subroutine
 * return address.
 *
 * If regPrev>0 then it is the first register in a vector that
 * records the previous output.  mem[regPrev] is a flag that is
 * false if there has been no previous output.  If regPrev>0 then
 * code is generated to suppress duplicates. key_info is used for
 * comparing keys.
 *
 * If the LIMIT found in p->iLimit is reached, jump immediately to
 * iBreak.
 *
 * @param parse Parsing context.
 * @param p The SELECT statement.
 * @param in Coroutine supplying data.
 * @param dest Where to send the data.
 * @param reg_ret The return address register.
 * @param reg_prev Previous result register.  No uniqueness if 0.
 * @param key_info For comparing with previous entry.
 * @param break_addr Jump here if we hit the LIMIT.
 *
 * @retval Address of generated routine.
 */
static int
generateOutputSubroutine(struct Parse *parse, struct Select *p,
			 struct SelectDest *in, struct SelectDest *dest,
			 int reg_ret, int reg_prev,
			 struct sql_key_info *key_info,
			 int break_addr)
{
	Vdbe *v = parse->pVdbe;
	int iContinue;
	int addr;

	addr = sqlVdbeCurrentAddr(v);
	iContinue = sqlVdbeMakeLabel(v);

	/* Suppress duplicates for UNION, EXCEPT, and INTERSECT
	 */
	if (reg_prev) {
		int addr1, addr2;
		addr1 = sqlVdbeAddOp1(v, OP_IfNot, reg_prev);
		VdbeCoverage(v);
		addr2 =
		    sqlVdbeAddOp4(v, OP_Compare, in->iSdst, reg_prev + 1,
				      in->nSdst,
				      (char *)sql_key_info_ref(key_info),
				      P4_KEYINFO);
		sqlVdbeAddOp3(v, OP_Jump, addr2 + 2, iContinue, addr2 + 2);
		VdbeCoverage(v);
		sqlVdbeJumpHere(v, addr1);
		sqlVdbeAddOp3(v, OP_Copy, in->iSdst, reg_prev + 1,
				  in->nSdst - 1);
		sqlVdbeAddOp2(v, OP_Bool, true, reg_prev);
	}
	if (parse->db->mallocFailed)
		return 0;

	/* Suppress the first OFFSET entries if there is an OFFSET clause
	 */
	codeOffset(v, p->iOffset, iContinue);

	assert(dest->eDest != SRT_Exists);
	assert(dest->eDest != SRT_Table);
	switch (dest->eDest) {
		/* Store the result as data using a unique key.
		 */
	case SRT_EphemTab:{
			int regRec = sqlGetTempReg(parse);
			int regCopy = sqlGetTempRange(parse, in->nSdst + 1);
			sqlVdbeAddOp2(v, OP_NextIdEphemeral, dest->reg_eph,
					  regCopy + in->nSdst);
			sqlVdbeAddOp3(v, OP_Copy, in->iSdst, regCopy,
					  in->nSdst - 1);
			sqlVdbeAddOp3(v, OP_MakeRecord, regCopy,
					  in->nSdst + 1, regRec);
			/* Set flag to save memory allocating one by malloc. */
			sqlVdbeChangeP5(v, 1);
			sqlVdbeAddOp2(v, OP_IdxInsert, regRec, dest->reg_eph);
			sqlReleaseTempRange(parse, regCopy, in->nSdst + 1);
			sqlReleaseTempReg(parse, regRec);
			break;
		}
		/* If we are creating a set for an "expr IN (SELECT ...)".
		 */
	case SRT_Set:{
			int r1;
			testcase(in->nSdst > 1);
			r1 = sqlGetTempReg(parse);
			enum field_type *types =
				field_type_sequence_dup(parse, dest->dest_type,
							in->nSdst);
			sqlVdbeAddOp4(v, OP_MakeRecord, in->iSdst,
					  in->nSdst, r1, (char *)types,
					  P4_DYNAMIC);
			sql_expr_type_cache_change(parse, in->iSdst,
						   in->nSdst);
			sqlVdbeAddOp2(v, OP_IdxInsert, r1, dest->reg_eph);
			sqlReleaseTempReg(parse, r1);
			break;
		}

		/* If this is a scalar select that is part of an expression, then
		 * store the results in the appropriate memory cell and break out
		 * of the scan loop.
		 */
	case SRT_Mem:{
			assert(in->nSdst == 1 || parse->is_aborted);
			testcase(in->nSdst != 1);
			sqlExprCodeMove(parse, in->iSdst, dest->iSDParm,
					    1);
			/* The LIMIT clause will jump out of the loop for us */
			break;
		}
		/* The results are stored in a sequence of registers
		 * starting at dest->iSdst.  Then the co-routine yields.
		 */
	case SRT_Coroutine:{
			if (dest->iSdst == 0) {
				dest->iSdst =
				    sqlGetTempRange(parse, in->nSdst);
				dest->nSdst = in->nSdst;
			}
			sqlExprCodeMove(parse, in->iSdst, dest->iSdst,
					    in->nSdst);
			sqlVdbeAddOp1(v, OP_Yield, dest->iSDParm);
			break;
		}

		/* If none of the above, then the result destination must be
		 * SRT_Output.  This routine is never called with any other
		 * destination other than the ones handled above or SRT_Output.
		 *
		 * For SRT_Output, results are stored in a sequence of registers.
		 * Then the OP_ResultRow opcode is used to cause sql_step() to
		 * return the next row of result.
		 */
	default:{
			assert(dest->eDest == SRT_Output);
			sqlVdbeAddOp2(v, OP_ResultRow, in->iSdst,
					  in->nSdst);
			sql_expr_type_cache_change(parse, in->iSdst, in->nSdst);
			break;
		}
	}

	/* Jump to the end of the loop if the LIMIT is reached.
	 */
	if (p->iLimit) {
		sqlVdbeAddOp2(v, OP_DecrJumpZero, p->iLimit, break_addr);
		VdbeCoverage(v);
	}

	/* Generate the subroutine return
	 */
	sqlVdbeResolveLabel(v, iContinue);
	sqlVdbeAddOp1(v, OP_Return, reg_ret);

	return addr;
}

/*
 * Alternative compound select code generator for cases when there
 * is an ORDER BY clause.
 *
 * We assume a query of the following form:
 *
 *      <selectA>  <operator>  <selectB>  ORDER BY <orderbylist>
 *
 * <operator> is one of UNION ALL, UNION, EXCEPT, or INTERSECT.  The idea
 * is to code both <selectA> and <selectB> with the ORDER BY clause as
 * co-routines.  Then run the co-routines in parallel and merge the results
 * into the output.  In addition to the two coroutines (called selectA and
 * selectB) there are 7 subroutines:
 *
 *    outA:    Move the output of the selectA coroutine into the output
 *             of the compound query.
 *
 *    outB:    Move the output of the selectB coroutine into the output
 *             of the compound query.  (Only generated for UNION and
 *             UNION ALL.  EXCEPT and INSERTSECT never output a row that
 *             appears only in B.)
 *
 *    AltB:    Called when there is data from both coroutines and A<B.
 *
 *    AeqB:    Called when there is data from both coroutines and A==B.
 *
 *    AgtB:    Called when there is data from both coroutines and A>B.
 *
 *    EofA:    Called when data is exhausted from selectA.
 *
 *    EofB:    Called when data is exhausted from selectB.
 *
 * The implementation of the latter five subroutines depend on which
 * <operator> is used:
 *
 *
 *             UNION ALL         UNION            EXCEPT          INTERSECT
 *          -------------  -----------------  --------------  -----------------
 *   AltB:   outA, nextA      outA, nextA       outA, nextA         nextA
 *
 *   AeqB:   outA, nextA         nextA             nextA         outA, nextA
 *
 *   AgtB:   outB, nextB      outB, nextB          nextB            nextB
 *
 *   EofA:   outB, nextB      outB, nextB          halt             halt
 *
 *   EofB:   outA, nextA      outA, nextA       outA, nextA         halt
 *
 * In the AltB, AeqB, and AgtB subroutines, an EOF on A following nextA
 * causes an immediate jump to EofA and an EOF on B following nextB causes
 * an immediate jump to EofB.  Within EofA and EofB, and EOF on entry or
 * following nextX causes a jump to the end of the select processing.
 *
 * Duplicate removal in the UNION, EXCEPT, and INTERSECT cases is handled
 * within the output subroutine.  The regPrev register set holds the previously
 * output value.  A comparison is made against this value and the output
 * is skipped if the next results would be the same as the previous.
 *
 * The implementation plan is to implement the two coroutines and seven
 * subroutines first, then put the control logic at the bottom.  Like this:
 *
 *          goto Init
 *     coA: coroutine for left query (A)
 *     coB: coroutine for right query (B)
 *    outA: output one row of A
 *    outB: output one row of B (UNION and UNION ALL only)
 *    EofA: ...
 *    EofB: ...
 *    AltB: ...
 *    AeqB: ...
 *    AgtB: ...
 *    Init: initialize coroutine registers
 *          yield coA
 *          if eof(A) goto EofA
 *          yield coB
 *          if eof(B) goto EofB
 *    Cmpr: Compare A, B
 *          Jump AltB, AeqB, AgtB
 *     End: ...
 *
 * We call AltB, AeqB, AgtB, EofA, and EofB "subroutines" but they are not
 * actually called using Gosub and they do not Return.  EofA and EofB loop
 * until all data is exhausted then jump to the "end" labe.  AltB, AeqB,
 * and AgtB jump to either L2 or to one of EofA or EofB.
 */
static int
multiSelectOrderBy(Parse * pParse,	/* Parsing context */
		   Select * p,		/* The right-most of SELECTs to be coded */
		   SelectDest * pDest)	/* What to do with query results */
{
	int i, j;		/* Loop counters */
	Select *pPrior;		/* Another SELECT immediately to our left */
	Vdbe *v;		/* Generate code to this VDBE */
	SelectDest destA;	/* Destination for coroutine A */
	SelectDest destB;	/* Destination for coroutine B */
	int regAddrA;		/* Address register for select-A coroutine */
	int regAddrB;		/* Address register for select-B coroutine */
	int addrSelectA;	/* Address of the select-A coroutine */
	int addrSelectB;	/* Address of the select-B coroutine */
	int regOutA;		/* Address register for the output-A subroutine */
	int regOutB;		/* Address register for the output-B subroutine */
	int addrOutA;		/* Address of the output-A subroutine */
	int addrOutB = 0;	/* Address of the output-B subroutine */
	int addrEofA;		/* Address of the select-A-exhausted subroutine */
	int addrEofA_noB;	/* Alternate addrEofA if B is uninitialized */
	int addrEofB;		/* Address of the select-B-exhausted subroutine */
	int addrAltB;		/* Address of the A<B subroutine */
	int addrAeqB;		/* Address of the A==B subroutine */
	int addrAgtB;		/* Address of the A>B subroutine */
	int regLimitA;		/* Limit register for select-A */
	int regLimitB;		/* Limit register for select-A */
	int regPrev;		/* A range of registers to hold previous output */
	int savedLimit;		/* Saved value of p->iLimit */
	int savedOffset;	/* Saved value of p->iOffset */
	int labelCmpr;		/* Label for the start of the merge algorithm */
	int labelEnd;		/* Label for the end of the overall SELECT stmt */
	int addr1;		/* Jump instructions that get retargetted */
	int op;			/* One of TK_ALL, TK_UNION, TK_EXCEPT, TK_INTERSECT */
	/* Comparison information for duplicate removal */
	struct sql_key_info *key_info_dup = NULL;
	/* Comparison information for merging rows */
	struct sql_key_info *key_info_merge;
	sql *db;		/* Database connection */
	ExprList *pOrderBy;	/* The ORDER BY clause */
	int nOrderBy;		/* Number of terms in the ORDER BY clause */
	int *aPermute;		/* Mapping from ORDER BY terms to result set columns */
	int iSub1;		/* EQP id of left-hand query */
	int iSub2;		/* EQP id of right-hand query */

	assert(p->pOrderBy != 0);
	db = pParse->db;
	v = pParse->pVdbe;
	assert(v != 0);		/* Already thrown the error if VDBE alloc failed */
	labelEnd = sqlVdbeMakeLabel(v);
	labelCmpr = sqlVdbeMakeLabel(v);

	/* Patch up the ORDER BY clause
	 */
	op = p->op;
	pPrior = p->pPrior;
	assert(pPrior->pOrderBy == 0);
	pOrderBy = p->pOrderBy;
	assert(pOrderBy);
	nOrderBy = pOrderBy->nExpr;

	/* For operators other than UNION ALL we have to make sure that
	 * the ORDER BY clause covers every term of the result set.  Add
	 * terms to the ORDER BY clause as necessary.
	 */
	if (op != TK_ALL) {
		for (i = 1; db->mallocFailed == 0 && i <= p->pEList->nExpr; i++) {
			struct ExprList_item *pItem;
			for (j = 0, pItem = pOrderBy->a; j < nOrderBy;
			     j++, pItem++) {
				assert(pItem->u.x.iOrderByCol > 0);
				if (pItem->u.x.iOrderByCol == i)
					break;
			}
			if (j == nOrderBy) {
				struct Expr *pNew =
					sql_expr_new_anon(db, TK_INTEGER);
				if (pNew == NULL) {
					pParse->is_aborted = true;
					return 1;
				}
				pNew->flags |= EP_IntValue;
				pNew->u.iValue = i;
				pOrderBy = sql_expr_list_append(pParse->db,
								pOrderBy, pNew);
				if (pOrderBy)
					pOrderBy->a[nOrderBy++].u.x.
					    iOrderByCol = (u16) i;
			}
		}
	}

	/* Compute the comparison permutation and key_info that is used with
	 * the permutation used to determine if the next
	 * row of results comes from selectA or selectB.  Also add explicit
	 * collations to the ORDER BY clause terms so that when the subqueries
	 * to the right and the left are evaluated, they use the correct
	 * collation.
	 */
	aPermute = sqlDbMallocRawNN(db, sizeof(int) * (nOrderBy + 1));
	if (aPermute) {
		struct ExprList_item *pItem;
		aPermute[0] = nOrderBy;
		for (i = 1, pItem = pOrderBy->a; i <= nOrderBy; i++, pItem++) {
			assert(pItem->u.x.iOrderByCol > 0);
			assert(pItem->u.x.iOrderByCol <= p->pEList->nExpr);
			aPermute[i] = pItem->u.x.iOrderByCol - 1;
		}
		key_info_merge = sql_multiselect_orderby_to_key_info(pParse,
								     p, 1);
	} else {
		key_info_merge = NULL;
	}

	/* Reattach the ORDER BY clause to the query.
	 */
	p->pOrderBy = pOrderBy;
	pPrior->pOrderBy = sql_expr_list_dup(pParse->db, pOrderBy, 0);

	/* Allocate a range of temporary registers and the key_info needed
	 * for the logic that removes duplicate result rows when the
	 * operator is UNION, EXCEPT, or INTERSECT (but not UNION ALL).
	 */
	if (op == TK_ALL) {
		regPrev = 0;
	} else {
		int expr_count = p->pEList->nExpr;
		assert(nOrderBy >= expr_count || db->mallocFailed);
		regPrev = pParse->nMem + 1;
		pParse->nMem += expr_count + 1;
		sqlVdbeAddOp2(v, OP_Bool, 0, regPrev);
		key_info_dup = sql_key_info_new(db, expr_count);
		if (key_info_dup != NULL) {
			for (int i = 0; i < expr_count; i++) {
				key_info_dup->parts[i].coll_id =
					multi_select_coll_seq(pParse, p, i);
			}
		}
	}

	/* Separate the left and the right query from one another
	 */
	p->pPrior = 0;
	pPrior->pNext = 0;
	sqlResolveOrderGroupBy(pParse, p, p->pOrderBy, "ORDER");
	if (pPrior->pPrior == 0) {
		sqlResolveOrderGroupBy(pParse, pPrior, pPrior->pOrderBy,
					   "ORDER");
	}

	/* Compute the limit registers */
	computeLimitRegisters(pParse, p, labelEnd);
	if (p->iLimit && op == TK_ALL) {
		regLimitA = ++pParse->nMem;
		regLimitB = ++pParse->nMem;
		sqlVdbeAddOp2(v, OP_Copy,
				  p->iOffset ? p->iOffset + 1 : p->iLimit,
				  regLimitA);
		sqlVdbeAddOp2(v, OP_Copy, regLimitA, regLimitB);
	} else {
		regLimitA = regLimitB = 0;
	}
	sql_expr_delete(db, p->pLimit, false);
	p->pLimit = 0;
	sql_expr_delete(db, p->pOffset, false);
	p->pOffset = 0;

	regAddrA = ++pParse->nMem;
	regAddrB = ++pParse->nMem;
	regOutA = ++pParse->nMem;
	regOutB = ++pParse->nMem;
	sqlSelectDestInit(&destA, SRT_Coroutine, regAddrA, -1);
	sqlSelectDestInit(&destB, SRT_Coroutine, regAddrB, -1);

	/* Generate a coroutine to evaluate the SELECT statement to the
	 * left of the compound operator - the "A" select.
	 */
	addrSelectA = sqlVdbeCurrentAddr(v) + 1;
	addr1 =
	    sqlVdbeAddOp3(v, OP_InitCoroutine, regAddrA, 0, addrSelectA);
	VdbeComment((v, "left SELECT"));
	pPrior->iLimit = regLimitA;
	iSub1 = pParse->iNextSelectId;
	sqlSelect(pParse, pPrior, &destA);
	sqlVdbeEndCoroutine(v, regAddrA);
	sqlVdbeJumpHere(v, addr1);

	/* Generate a coroutine to evaluate the SELECT statement on
	 * the right - the "B" select
	 */
	addrSelectB = sqlVdbeCurrentAddr(v) + 1;
	addr1 =
	    sqlVdbeAddOp3(v, OP_InitCoroutine, regAddrB, 0, addrSelectB);
	VdbeComment((v, "right SELECT"));
	savedLimit = p->iLimit;
	savedOffset = p->iOffset;
	p->iLimit = regLimitB;
	p->iOffset = 0;
	iSub2 = pParse->iNextSelectId;
	sqlSelect(pParse, p, &destB);
	p->iLimit = savedLimit;
	p->iOffset = savedOffset;
	sqlVdbeEndCoroutine(v, regAddrB);

	/* Generate a subroutine that outputs the current row of the A
	 * select as the next output row of the compound select.
	 */
	VdbeNoopComment((v, "Output routine for A"));
	addrOutA = generateOutputSubroutine(pParse,
					    p, &destA, pDest, regOutA,
					    regPrev, key_info_dup, labelEnd);

	/* Generate a subroutine that outputs the current row of the B
	 * select as the next output row of the compound select.
	 */
	if (op == TK_ALL || op == TK_UNION) {
		VdbeNoopComment((v, "Output routine for B"));
		addrOutB = generateOutputSubroutine(pParse,
						    p, &destB, pDest, regOutB,
						    regPrev, key_info_dup,
						    labelEnd);
	}

	sql_key_info_unref(key_info_dup);

	/* Generate a subroutine to run when the results from select A
	 * are exhausted and only data in select B remains.
	 */
	if (op == TK_EXCEPT || op == TK_INTERSECT) {
		addrEofA_noB = addrEofA = labelEnd;
	} else {
		VdbeNoopComment((v, "eof-A subroutine"));
		addrEofA = sqlVdbeAddOp2(v, OP_Gosub, regOutB, addrOutB);
		addrEofA_noB =
		    sqlVdbeAddOp2(v, OP_Yield, regAddrB, labelEnd);
		VdbeCoverage(v);
		sqlVdbeGoto(v, addrEofA);
		p->nSelectRow =
		    sqlLogEstAdd(p->nSelectRow, pPrior->nSelectRow);
	}

	/* Generate a subroutine to run when the results from select B
	 * are exhausted and only data in select A remains.
	 */
	if (op == TK_INTERSECT) {
		addrEofB = addrEofA;
		if (p->nSelectRow > pPrior->nSelectRow)
			p->nSelectRow = pPrior->nSelectRow;
	} else {
		VdbeNoopComment((v, "eof-B subroutine"));
		addrEofB = sqlVdbeAddOp2(v, OP_Gosub, regOutA, addrOutA);
		sqlVdbeAddOp2(v, OP_Yield, regAddrA, labelEnd);
		VdbeCoverage(v);
		sqlVdbeGoto(v, addrEofB);
	}

	/* Generate code to handle the case of A<B
	 */
	VdbeNoopComment((v, "A-lt-B subroutine"));
	addrAltB = sqlVdbeAddOp2(v, OP_Gosub, regOutA, addrOutA);
	sqlVdbeAddOp2(v, OP_Yield, regAddrA, addrEofA);
	VdbeCoverage(v);
	sqlVdbeGoto(v, labelCmpr);

	/* Generate code to handle the case of A==B
	 */
	if (op == TK_ALL) {
		addrAeqB = addrAltB;
	} else if (op == TK_INTERSECT) {
		addrAeqB = addrAltB;
		addrAltB++;
	} else {
		VdbeNoopComment((v, "A-eq-B subroutine"));
		addrAeqB = sqlVdbeAddOp2(v, OP_Yield, regAddrA, addrEofA);
		VdbeCoverage(v);
		sqlVdbeGoto(v, labelCmpr);
	}

	/* Generate code to handle the case of A>B
	 */
	VdbeNoopComment((v, "A-gt-B subroutine"));
	addrAgtB = sqlVdbeCurrentAddr(v);
	if (op == TK_ALL || op == TK_UNION) {
		sqlVdbeAddOp2(v, OP_Gosub, regOutB, addrOutB);
	}
	sqlVdbeAddOp2(v, OP_Yield, regAddrB, addrEofB);
	VdbeCoverage(v);
	sqlVdbeGoto(v, labelCmpr);

	/* This code runs once to initialize everything.
	 */
	sqlVdbeJumpHere(v, addr1);
	sqlVdbeAddOp2(v, OP_Yield, regAddrA, addrEofA_noB);
	VdbeCoverage(v);
	sqlVdbeAddOp2(v, OP_Yield, regAddrB, addrEofB);
	VdbeCoverage(v);

	/* Implement the main merge loop
	 */
	sqlVdbeResolveLabel(v, labelCmpr);
	sqlVdbeAddOp4(v, OP_Permutation, 0, 0, 0, (char *)aPermute,
			  P4_INTARRAY);
	sqlVdbeAddOp4(v, OP_Compare, destA.iSdst, destB.iSdst, nOrderBy,
			  (char *)key_info_merge, P4_KEYINFO);
	sqlVdbeChangeP5(v, OPFLAG_PERMUTE);
	sqlVdbeAddOp3(v, OP_Jump, addrAltB, addrAeqB, addrAgtB);
	VdbeCoverage(v);

	/* Jump to the this point in order to terminate the query.
	 */
	sqlVdbeResolveLabel(v, labelEnd);

	/* Set the number of output columns
	 */
	if (pDest->eDest == SRT_Output) {
		Select *pFirst = pPrior;
		while (pFirst->pPrior)
			pFirst = pFirst->pPrior;
		generate_column_metadata(pParse, pFirst->pSrc, pFirst->pEList);
	}

	/* Reassembly the compound query so that it will be freed correctly
	 * by the calling function
	 */
	if (p->pPrior) {
		sql_select_delete(db, p->pPrior);
	}
	p->pPrior = pPrior;
	pPrior->pNext = p;

  /*** TBD:  Insert subroutine calls to close cursors on incomplete
  *** subqueries ***
  */
	explainComposite(pParse, p->op, iSub1, iSub2, 0);
	return pParse->is_aborted;
}

/* Forward Declarations */
static void substExprList(Parse *, ExprList *, int, ExprList *);
static void substSelect(Parse *, Select *, int, ExprList *, int);

/*
 * Scan through the expression pExpr.  Replace every reference to
 * a column in table number iTable with a copy of the iColumn-th
 * entry in pEList.
 *
 * This routine is part of the flattening procedure.  A subquery
 * whose result set is defined by pEList appears as entry in the
 * FROM clause of a SELECT such that the VDBE cursor assigned to that
 * FORM clause entry is iTable.  This routine make the necessary
 * changes to pExpr so that it refers directly to the source table
 * of the subquery rather the result set of the subquery.
 */
static Expr *
substExpr(Parse * pParse,	/* Report errors here */
	  Expr * pExpr,		/* Expr in which substitution occurs */
	  int iTable,		/* Table to be substituted */
	  ExprList * pEList)	/* Substitute expressions */
{
	sql *db = pParse->db;
	if (pExpr == 0)
		return 0;
	if (pExpr->op == TK_COLUMN_REF && pExpr->iTable == iTable) {
		if (pExpr->iColumn < 0) {
			pExpr->op = TK_NULL;
		} else {
			Expr *pNew;
			Expr *pCopy = pEList->a[pExpr->iColumn].pExpr;
			assert(pEList != 0 && pExpr->iColumn < pEList->nExpr);
			assert(pExpr->pLeft == 0 && pExpr->pRight == 0);
			if (sqlExprIsVector(pCopy)) {
				assert((pCopy->flags & EP_xIsSelect) != 0);
				int expr_count =
					pCopy->x.pSelect->pEList->nExpr;
				diag_set(ClientError, ER_SQL_COLUMN_COUNT,
					 expr_count, 1);
				pParse->is_aborted = true;
			} else {
				pNew = sqlExprDup(db, pCopy, 0);
				if (pNew && (pExpr->flags & EP_FromJoin)) {
					pNew->iRightJoinTable =
					    pExpr->iRightJoinTable;
					pNew->flags |= EP_FromJoin;
				}
				sql_expr_delete(db, pExpr, false);
				pExpr = pNew;
			}
		}
	} else {
		pExpr->pLeft = substExpr(pParse, pExpr->pLeft, iTable, pEList);
		pExpr->pRight =
		    substExpr(pParse, pExpr->pRight, iTable, pEList);
		if (ExprHasProperty(pExpr, EP_xIsSelect)) {
			substSelect(pParse, pExpr->x.pSelect, iTable, pEList,
				    1);
		} else {
			substExprList(pParse, pExpr->x.pList, iTable, pEList);
		}
	}
	return pExpr;
}

static void
substExprList(Parse * pParse,		/* Report errors here */
	      ExprList * pList,		/* List to scan and in which to make substitutes */
	      int iTable,		/* Table to be substituted */
	      ExprList * pEList)	/* Substitute values */
{
	int i;
	if (pList == 0)
		return;
	for (i = 0; i < pList->nExpr; i++) {
		pList->a[i].pExpr =
		    substExpr(pParse, pList->a[i].pExpr, iTable, pEList);
	}
}

static void
substSelect(Parse * pParse,	/* Report errors here */
	    Select * p,		/* SELECT statement in which to make substitutions */
	    int iTable,		/* Table to be replaced */
	    ExprList * pEList,	/* Substitute values */
	    int doPrior)	/* Do substitutes on p->pPrior too */
{
	SrcList *pSrc;
	struct SrcList_item *pItem;
	int i;
	if (!p)
		return;
	do {
		substExprList(pParse, p->pEList, iTable, pEList);
		substExprList(pParse, p->pGroupBy, iTable, pEList);
		substExprList(pParse, p->pOrderBy, iTable, pEList);
		p->pHaving = substExpr(pParse, p->pHaving, iTable, pEList);
		p->pWhere = substExpr(pParse, p->pWhere, iTable, pEList);
		pSrc = p->pSrc;
		assert(pSrc != 0);
		for (i = pSrc->nSrc, pItem = pSrc->a; i > 0; i--, pItem++) {
			substSelect(pParse, pItem->pSelect, iTable, pEList, 1);
			if (pItem->fg.isTabFunc) {
				substExprList(pParse, pItem->u1.pFuncArg,
					      iTable, pEList);
			}
		}
	} while (doPrior && (p = p->pPrior) != 0);
}

/*
 * This routine attempts to flatten subqueries as a performance optimization.
 * This routine returns 1 if it makes changes and 0 if no flattening occurs.
 *
 * To understand the concept of flattening, consider the following
 * query:
 *
 *     SELECT a FROM (SELECT x+y AS a FROM t1 WHERE z<100) WHERE a>5
 *
 * The default way of implementing this query is to execute the
 * subquery first and store the results in a temporary table, then
 * run the outer query on that temporary table.  This requires two
 * passes over the data.  Furthermore, because the temporary table
 * has no indices, the WHERE clause on the outer query cannot be
 * optimized.
 *
 * This routine attempts to rewrite queries such as the above into
 * a single flat select, like this:
 *
 *     SELECT x+y AS a FROM t1 WHERE z<100 AND a>5
 *
 * The code generated for this simplification gives the same result
 * but only has to scan the data once.  And because indices might
 * exist on the table t1, a complete scan of the data might be
 * avoided.
 *
 * Flattening is only attempted if all of the following are true:
 *
 *   (1)  The subquery and the outer query do not both use aggregates.
 *
 *   (2)  The subquery is not an aggregate or (2a) the outer query is not a join
 *        and (2b) the outer query does not use subqueries other than the one
 *        FROM-clause subquery that is a candidate for flattening.  (2b is
 *        due to ticket [2f7170d73bf9abf80] from 2015-02-09.)
 *
 *   (3)  The subquery is not the right operand of a left outer join
 *        (Originally ticket #306.  Strengthened by ticket #3300)
 *
 *   (4)  The subquery is not DISTINCT.
 *
 *  (**)  At one point restrictions (4) and (5) defined a subset of DISTINCT
 *        sub-queries that were excluded from this optimization. Restriction
 *        (4) has since been expanded to exclude all DISTINCT subqueries.
 *
 *   (6)  The subquery does not use aggregates or the outer query is not
 *        DISTINCT.
 *
 *   (7)  The subquery has a FROM clause.  TODO:  For subqueries without
 *        A FROM clause, consider adding a FROM close with the special
 *        table sql_once that consists of a single row containing a
 *        single NULL.
 *
 *   (8)  The subquery does not use LIMIT or the outer query is not a join.
 *
 *   (9)  The subquery does not use LIMIT or the outer query does not use
 *        aggregates.
 *
 *  (**)  Restriction (10) was removed from the code on 2005-02-05 but we
 *        accidently carried the comment forward until 2014-09-15.  Original
 *        text: "The subquery does not use aggregates or the outer query
 *        does not use LIMIT."
 *
 *  (11)  The subquery and the outer query do not both have ORDER BY clauses.
 *
 *  (**)  Not implemented.  Subsumed into restriction (3).  Was previously
 *        a separate restriction deriving from ticket #350.
 *
 *  (13)  The subquery and outer query do not both use LIMIT.
 *
 *  (14)  The subquery does not use OFFSET.
 *
 *  (15)  The outer query is not part of a compound select or the
 *        subquery does not have a LIMIT clause.
 *        (See ticket #2339 and ticket [02a8e81d44]).
 *
 *  (16)  The outer query is not an aggregate or the subquery does
 *        not contain ORDER BY.  (Ticket #2942)  This used to not matter
 *        until we introduced the group_concat() function.
 *
 *  (17)  The sub-query is not a compound select, or it is a UNION ALL
 *        compound clause made up entirely of non-aggregate queries, and
 *        the parent query:
 *
 *          * is not itself part of a compound select,
 *          * is not an aggregate or DISTINCT query, and
 *          * is not a join
 *
 *        The parent and sub-query may contain WHERE clauses. Subject to
 *        rules (11), (13) and (14), they may also contain ORDER BY,
 *        LIMIT and OFFSET clauses.  The subquery cannot use any compound
 *        operator other than UNION ALL because all the other compound
 *        operators have an implied DISTINCT which is disallowed by
 *        restriction (4).
 *
 *        Also, each component of the sub-query must return the same number
 *        of result columns. This is actually a requirement for any compound
 *        SELECT statement, but all the code here does is make sure that no
 *        such (illegal) sub-query is flattened. The caller will detect the
 *        syntax error and return a detailed message.
 *
 *  (18)  If the sub-query is a compound select, then all terms of the
 *        ORDER by clause of the parent must be simple references to
 *        columns of the sub-query.
 *
 *  (19)  The subquery does not use LIMIT or the outer query does not
 *        have a WHERE clause.
 *
 *  (20)  If the sub-query is a compound select, then it must not use
 *        an ORDER BY clause.  Ticket #3773.  We could relax this constraint
 *        somewhat by saying that the terms of the ORDER BY clause must
 *        appear as unmodified result columns in the outer query.  But we
 *        have other optimizations in mind to deal with that case.
 *
 *  (21)  The subquery does not use LIMIT or the outer query is not
 *        DISTINCT.  (See ticket [752e1646fc]).
 *
 *  (22)  The subquery is not a recursive CTE.
 *
 *  (23)  The parent is not a recursive CTE, or the sub-query is not a
 *        compound query. This restriction is because transforming the
 *        parent to a compound query confuses the code that handles
 *        recursive queries in multiSelect().
 *
 *  (24)  The subquery is not an aggregate that uses the built-in min() or
 *        or max() functions.  (Without this restriction, a query like:
 *        "SELECT x FROM (SELECT max(y), x FROM t1)" would not necessarily
 *        return the value X for which Y was maximal.)
 *
 *
 * In this routine, the "p" parameter is a pointer to the outer query.
 * The subquery is p->pSrc->a[iFrom].  isAgg is true if the outer query
 * uses aggregates and subqueryIsAgg is true if the subquery uses aggregates.
 *
 * If flattening is not attempted, this routine is a no-op and returns 0.
 * If flattening is attempted this routine returns 1.
 *
 * All of the expression analysis must occur on both the outer query and
 * the subquery before this routine runs.
 */
static int
flattenSubquery(Parse * pParse,		/* Parsing context */
		Select * p,		/* The parent or outer SELECT statement */
		int iFrom,		/* Index in p->pSrc->a[] of the inner subquery */
		int isAgg,		/* True if outer SELECT uses aggregate functions */
		int subqueryIsAgg)	/* True if the subquery uses aggregate functions */
{
	Select *pParent;	/* Current UNION ALL term of the other query */
	Select *pSub;		/* The inner query or "subquery" */
	Select *pSub1;		/* Pointer to the rightmost select in sub-query */
	SrcList *pSrc;		/* The FROM clause of the outer query */
	SrcList *pSubSrc;	/* The FROM clause of the subquery */
	ExprList *pList;	/* The result set of the outer query */
	int iParent;		/* VDBE cursor number of the pSub result set temp table */
	int i;			/* Loop counter */
	Expr *pWhere;		/* The WHERE clause */
	struct SrcList_item *pSubitem;	/* The subquery */
	sql *db = pParse->db;

	/* Check to see if flattening is permitted.  Return 0 if not.
	 */
	assert(p != 0);
	assert(p->pPrior == 0);	/* Unable to flatten compound queries */
	if (OptimizationDisabled(db, SQL_QueryFlattener))
		return 0;
	pSrc = p->pSrc;
	assert(pSrc && iFrom >= 0 && iFrom < pSrc->nSrc);
	pSubitem = &pSrc->a[iFrom];
	iParent = pSubitem->iCursor;
	pSub = pSubitem->pSelect;
	assert(pSub != 0);
	if (subqueryIsAgg) {
		if (isAgg)
			return 0;	/* Restriction (1)   */
		if (pSrc->nSrc > 1)
			return 0;	/* Restriction (2a)  */
		if ((p->pWhere && ExprHasProperty(p->pWhere, EP_Subquery))
		    || (sqlExprListFlags(p->pEList) & EP_Subquery) != 0
		    || (sqlExprListFlags(p->pOrderBy) & EP_Subquery) != 0) {
			return 0;	/* Restriction (2b)  */
		}
	}

	pSubSrc = pSub->pSrc;
	assert(pSubSrc);
	/* Prior to version 3.1.2, when LIMIT and OFFSET had to be simple constants,
	 * not arbitrary expressions, we allowed some combining of LIMIT and OFFSET
	 * because they could be computed at compile-time.  But when LIMIT and OFFSET
	 * became arbitrary expressions, we were forced to add restrictions (13)
	 * and (14).
	 */
	if (pSub->pLimit && p->pLimit)
		return 0;	/* Restriction (13) */
	if (pSub->pOffset)
		return 0;	/* Restriction (14) */
	if ((p->selFlags & SF_Compound) != 0 && pSub->pLimit) {
		return 0;	/* Restriction (15) */
	}
	if (pSubSrc->nSrc == 0)
		return 0;	/* Restriction (7)  */
	if (pSub->selFlags & SF_Distinct)
		return 0;	/* Restriction (5)  */
	if (pSub->pLimit && (pSrc->nSrc > 1 || isAgg)) {
		return 0;	/* Restrictions (8)(9) */
	}
	if ((p->selFlags & SF_Distinct) != 0 && subqueryIsAgg) {
		return 0;	/* Restriction (6)  */
	}
	if (p->pOrderBy && pSub->pOrderBy) {
		return 0;	/* Restriction (11) */
	}
	if (isAgg && pSub->pOrderBy)
		return 0;	/* Restriction (16) */
	if (pSub->pLimit && p->pWhere)
		return 0;	/* Restriction (19) */
	if (pSub->pLimit && (p->selFlags & SF_Distinct) != 0) {
		return 0;	/* Restriction (21) */
	}
	testcase(pSub->selFlags & SF_Recursive);
	testcase(pSub->selFlags & SF_MinMaxAgg);
	if (pSub->selFlags & (SF_Recursive | SF_MinMaxAgg)) {
		return 0;	/* Restrictions (22) and (24) */
	}
	if ((p->selFlags & SF_Recursive) && pSub->pPrior) {
		return 0;	/* Restriction (23) */
	}

	/* OBSOLETE COMMENT 1:
	 * Restriction 3:  If the subquery is a join, make sure the subquery is
	 * not used as the right operand of an outer join.  Examples of why this
	 * is not allowed:
	 *
	 *         t1 LEFT OUTER JOIN (t2 JOIN t3)
	 *
	 * If we flatten the above, we would get
	 *
	 *         (t1 LEFT OUTER JOIN t2) JOIN t3
	 *
	 * which is not at all the same thing.
	 *
	 * OBSOLETE COMMENT 2:
	 * Restriction 12:  If the subquery is the right operand of a left outer
	 * join, make sure the subquery has no WHERE clause.
	 * An examples of why this is not allowed:
	 *
	 *         t1 LEFT OUTER JOIN (SELECT * FROM t2 WHERE t2.x>0)
	 *
	 * If we flatten the above, we would get
	 *
	 *         (t1 LEFT OUTER JOIN t2) WHERE t2.x>0
	 *
	 * But the t2.x>0 test will always fail on a NULL row of t2, which
	 * effectively converts the OUTER JOIN into an INNER JOIN.
	 *
	 * THIS OVERRIDES OBSOLETE COMMENTS 1 AND 2 ABOVE:
	 * Ticket #3300 shows that flattening the right term of a LEFT JOIN
	 * is fraught with danger.  Best to avoid the whole thing.  If the
	 * subquery is the right term of a LEFT JOIN, then do not flatten.
	 */
	if ((pSubitem->fg.jointype & JT_OUTER) != 0) {
		return 0;
	}

	/* Restriction 17: If the sub-query is a compound SELECT, then it must
	 * use only the UNION ALL operator. And none of the simple select queries
	 * that make up the compound SELECT are allowed to be aggregate or distinct
	 * queries.
	 */
	if (pSub->pPrior) {
		if (isAgg || (p->selFlags & SF_Distinct) != 0
		    || pSrc->nSrc != 1) {
			return 0;
		}
		for (pSub1 = pSub; pSub1; pSub1 = pSub1->pPrior) {
			/* Restriction 20 */
			if (pSub1->pOrderBy != NULL)
				return 0;
			testcase((pSub1->
				  selFlags & (SF_Distinct | SF_Aggregate)) ==
				 SF_Distinct);
			testcase((pSub1->
				  selFlags & (SF_Distinct | SF_Aggregate)) ==
				 SF_Aggregate);
			assert(pSub->pSrc != 0);
			assert(pSub->pEList->nExpr == pSub1->pEList->nExpr);
			if ((pSub1->selFlags & (SF_Distinct | SF_Aggregate)) !=
			    0 || (pSub1->pPrior && pSub1->op != TK_ALL)
			    || pSub1->pSrc->nSrc < 1) {
				return 0;
			}
			testcase(pSub1->pSrc->nSrc > 1);
		}

		/* Restriction 18. */
		if (p->pOrderBy) {
			int ii;
			for (ii = 0; ii < p->pOrderBy->nExpr; ii++) {
				if (p->pOrderBy->a[ii].u.x.iOrderByCol == 0)
					return 0;
			}
		}
	}

	/***** If we reach this point, flattening is permitted. *****/
	SELECTTRACE(1, pParse, p, ("flatten %s.%p from term %d\n",
				   pSub->zSelName, pSub, iFrom));

	/* If the sub-query is a compound SELECT statement, then (by restrictions
	 * 17 and 18 above) it must be a UNION ALL and the parent query must
	 * be of the form:
	 *
	 *     SELECT <expr-list> FROM (<sub-query>) <where-clause>
	 *
	 * followed by any ORDER BY, LIMIT and/or OFFSET clauses. This block
	 * creates N-1 copies of the parent query without any ORDER BY, LIMIT or
	 * OFFSET clauses and joins them to the left-hand-side of the original
	 * using UNION ALL operators. In this case N is the number of simple
	 * select statements in the compound sub-query.
	 *
	 * Example:
	 *
	 *     SELECT a+1 FROM (
	 *        SELECT x FROM tab
	 *        UNION ALL
	 *        SELECT y FROM tab
	 *        UNION ALL
	 *        SELECT abs(z*2) FROM tab2
	 *     ) WHERE a!=5 ORDER BY 1
	 *
	 * Transformed into:
	 *
	 *     SELECT x+1 FROM tab WHERE x+1!=5
	 *     UNION ALL
	 *     SELECT y+1 FROM tab WHERE y+1!=5
	 *     UNION ALL
	 *     SELECT abs(z*2)+1 FROM tab2 WHERE abs(z*2)+1!=5
	 *     ORDER BY 1
	 *
	 * We call this the "compound-subquery flattening".
	 */
	for (pSub = pSub->pPrior; pSub; pSub = pSub->pPrior) {
		Select *pNew;
		ExprList *pOrderBy = p->pOrderBy;
		Expr *pLimit = p->pLimit;
		Expr *pOffset = p->pOffset;
		Select *pPrior = p->pPrior;
		p->pOrderBy = 0;
		p->pSrc = 0;
		p->pPrior = 0;
		p->pLimit = 0;
		p->pOffset = 0;
		pNew = sqlSelectDup(db, p, 0);
		sqlSelectSetName(pNew, pSub->zSelName);
		p->pOffset = pOffset;
		p->pLimit = pLimit;
		p->pOrderBy = pOrderBy;
		p->pSrc = pSrc;
		p->op = TK_ALL;
		if (pNew == 0) {
			p->pPrior = pPrior;
		} else {
			pNew->pPrior = pPrior;
			if (pPrior)
				pPrior->pNext = pNew;
			pNew->pNext = p;
			p->pPrior = pNew;
			SELECTTRACE(2, pParse, p,
				    ("compound-subquery flattener creates %s.%p as peer\n",
				     pNew->zSelName, pNew));
		}
		if (db->mallocFailed)
			return 1;
	}

	/*
	 * Begin flattening the iFrom-th entry of the FROM clause
	 * in the outer query.
	 */
	pSub = pSub1 = pSubitem->pSelect;

	/* Delete the transient table structure associated with the
	 * subquery
	 */
	sqlDbFree(db, pSubitem->zName);
	sqlDbFree(db, pSubitem->zAlias);
	pSubitem->zName = 0;
	pSubitem->zAlias = 0;
	pSubitem->pSelect = 0;

	/* Deletion of the pSubitem->space will be done when a corresponding
	 * region will be freed.
	 */

	/* The following loop runs once for each term in a compound-subquery
	 * flattening (as described above).  If we are doing a different kind
	 * of flattening - a flattening other than a compound-subquery flattening -
	 * then this loop only runs once.
	 *
	 * This loop moves all of the FROM elements of the subquery into the
	 * the FROM clause of the outer query.  Before doing this, remember
	 * the cursor number for the original outer query FROM element in
	 * iParent.  The iParent cursor will never be used.  Subsequent code
	 * will scan expressions looking for iParent references and replace
	 * those references with expressions that resolve to the subquery FROM
	 * elements we are now copying in.
	 */
	for (pParent = p; pParent;
	     pParent = pParent->pPrior, pSub = pSub->pPrior) {
		int nSubSrc;
		u8 jointype = 0;
		pSubSrc = pSub->pSrc;	/* FROM clause of subquery */
		nSubSrc = pSubSrc->nSrc;	/* Number of terms in subquery FROM clause */
		pSrc = pParent->pSrc;	/* FROM clause of the outer query */

		if (pSrc) {
			assert(pParent == p);	/* First time through the loop */
			jointype = pSubitem->fg.jointype;
		} else {
			assert(pParent != p);	/* 2nd and subsequent times through the loop */
			pSrc = pParent->pSrc =
			    sql_src_list_append(db, 0, 0);
			if (pSrc == NULL) {
				pParse->is_aborted = true;
				break;
			}
		}

		/* The subquery uses a single slot of the FROM clause of the outer
		 * query.  If the subquery has more than one element in its FROM clause,
		 * then expand the outer query to make space for it to hold all elements
		 * of the subquery.
		 *
		 * Example:
		 *
		 *    SELECT * FROM tabA, (SELECT * FROM sub1, sub2), tabB;
		 *
		 * The outer query has 3 slots in its FROM clause.  One slot of the
		 * outer query (the middle slot) is used by the subquery.  The next
		 * block of code will expand the outer query FROM clause to 4 slots.
		 * The middle slot is expanded to two slots in order to make space
		 * for the two elements in the FROM clause of the subquery.
		 */
		if (nSubSrc > 1) {
			struct SrcList *new_list =
				sql_src_list_enlarge(db, pSrc, nSubSrc - 1,
						     iFrom + 1);
			if (new_list == NULL) {
				pParse->is_aborted = true;
				break;
			}
			pParent->pSrc = pSrc = new_list;
		}

		/* Transfer the FROM clause terms from the subquery into the
		 * outer query.
		 */
		for (i = 0; i < nSubSrc; i++) {
			sqlIdListDelete(db, pSrc->a[i + iFrom].pUsing);
			assert(pSrc->a[i + iFrom].fg.isTabFunc == 0);
			pSrc->a[i + iFrom] = pSubSrc->a[i];
			memset(&pSubSrc->a[i], 0, sizeof(pSubSrc->a[i]));
		}
		pSrc->a[iFrom].fg.jointype = jointype;

		/* Now begin substituting subquery result set expressions for
		 * references to the iParent in the outer query.
		 *
		 * Example:
		 *
		 *   SELECT a+5, b*10 FROM (SELECT x*3 AS a, y+10 AS b FROM t1) WHERE a>b;
		 *   \                     \_____________ subquery __________/          /
		 *    \_____________________ outer query ______________________________/
		 *
		 * We look at every expression in the outer query and every place we see
		 * "a" we substitute "x*3" and every place we see "b" we substitute "y+10".
		 */
		pList = pParent->pEList;
		for (i = 0; i < pList->nExpr; i++) {
			if (pList->a[i].zName == 0) {
				char *str = pList->a[i].zSpan;
				int len = strlen(str);
				char *name =
					sql_normalized_name_db_new(db, str,
								   len);
				if (name == NULL)
					pParse->is_aborted = true;
				pList->a[i].zName = name;
			}
		}
		if (pSub->pOrderBy) {
			/* At this point, any non-zero iOrderByCol values indicate that the
			 * ORDER BY column expression is identical to the iOrderByCol'th
			 * expression returned by SELECT statement pSub. Since these values
			 * do not necessarily correspond to columns in SELECT statement pParent,
			 * zero them before transfering the ORDER BY clause.
			 *
			 * Not doing this may cause an error if a subsequent call to this
			 * function attempts to flatten a compound sub-query into pParent
			 * (the only way this can happen is if the compound sub-query is
			 * currently part of pSub->pSrc). See ticket [d11a6e908f].
			 */
			ExprList *pOrderBy = pSub->pOrderBy;
			for (i = 0; i < pOrderBy->nExpr; i++) {
				pOrderBy->a[i].u.x.iOrderByCol = 0;
			}
			assert(pParent->pOrderBy == 0);
			assert(pSub->pPrior == 0);
			pParent->pOrderBy = pOrderBy;
			pSub->pOrderBy = 0;
		}
		pWhere = sqlExprDup(db, pSub->pWhere, 0);
		if (subqueryIsAgg) {
			assert(pParent->pHaving == 0);
			pParent->pHaving = pParent->pWhere;
			pParent->pWhere = pWhere;
			struct Expr *sub_having =
				sqlExprDup(db, pSub->pHaving, 0);
			if (sub_having != NULL || pParent->pHaving != NULL) {
				pParent->pHaving =
					sql_and_expr_new(db, sub_having,
							 pParent->pHaving);
				if (pParent->pHaving == NULL)
					pParse->is_aborted = true;
			}
			assert(pParent->pGroupBy == 0);
			pParent->pGroupBy =
			    sql_expr_list_dup(db, pSub->pGroupBy, 0);
		} else if (pWhere != NULL || pParent->pWhere != NULL) {
			pParent->pWhere =
				sql_and_expr_new(db, pWhere, pParent->pWhere);
			if (pParent->pWhere == NULL)
				pParse->is_aborted = true;
		}
		substSelect(pParse, pParent, iParent, pSub->pEList, 0);

		/* The flattened query is distinct if either the inner or the
		 * outer query is distinct.
		 */
		pParent->selFlags |= pSub->selFlags & SF_Distinct;

		/*
		 * SELECT ... FROM (SELECT ... LIMIT a OFFSET b) LIMIT x OFFSET y;
		 *
		 * One is tempted to try to add a and b to combine the limits.  But this
		 * does not work if either limit is negative.
		 */
		if (pSub->pLimit) {
			pParent->pLimit = pSub->pLimit;
			pSub->pLimit = 0;
		}
	}

	/* Finially, delete what is left of the subquery and return
	 * success.
	 */
	sql_select_delete(db, pSub1);

#ifdef SQL_DEBUG
	if (sqlSelectTrace & 0x100) {
		SELECTTRACE(0x100, pParse, p, ("After flattening:\n"));
		sqlTreeViewSelect(0, p, 0);
	}
#endif

	return 1;
}

/*
 * Make copies of relevant WHERE clause terms of the outer query into
 * the WHERE clause of subquery.  Example:
 *
 *    SELECT * FROM (SELECT a AS x, c-d AS y FROM t1) WHERE x=5 AND y=10;
 *
 * Transformed into:
 *
 *    SELECT * FROM (SELECT a AS x, c-d AS y FROM t1 WHERE a=5 AND c-d=10)
 *     WHERE x=5 AND y=10;
 *
 * The hope is that the terms added to the inner query will make it more
 * efficient.
 *
 * Do not attempt this optimization if:
 *
 *   (1) The inner query is an aggregate.  (In that case, we'd really want
 *       to copy the outer WHERE-clause terms onto the HAVING clause of the
 *       inner query.  But they probably won't help there so do not bother.)
 *
 *   (2) The inner query is the recursive part of a common table expression.
 *
 *   (3) The inner query has a LIMIT clause (since the changes to the WHERE
 *       close would change the meaning of the LIMIT).
 *
 *   (4) The inner query is the right operand of a LEFT JOIN.  (The caller
 *       enforces this restriction since this routine does not have enough
 *       information to know.)
 *
 *   (5) The WHERE clause expression originates in the ON or USING clause
 *       of a LEFT JOIN.
 *
 * Return 0 if no changes are made and non-zero if one or more WHERE clause
 * terms are duplicated into the subquery.
 */
static int
pushDownWhereTerms(Parse * pParse,	/* Parse context (for malloc() and error reporting) */
		   Select * pSubq,	/* The subquery whose WHERE clause is to be augmented */
		   Expr * pWhere,	/* The WHERE clause of the outer query */
		   int iCursor)		/* Cursor number of the subquery */
{
	Expr *pNew;
	int nChng = 0;
	Select *pX;		/* For looping over compound SELECTs in pSubq */
	if (pWhere == 0)
		return 0;
	for (pX = pSubq; pX; pX = pX->pPrior) {
		if ((pX->selFlags & (SF_Aggregate | SF_Recursive)) != 0) {
			testcase(pX->selFlags & SF_Aggregate);
			testcase(pX->selFlags & SF_Recursive);
			testcase(pX != pSubq);
			return 0;	/* restrictions (1) and (2) */
		}
	}
	if (pSubq->pLimit != 0) {
		return 0;	/* restriction (3) */
	}
	while (pWhere->op == TK_AND) {
		nChng +=
		    pushDownWhereTerms(pParse, pSubq, pWhere->pRight, iCursor);
		pWhere = pWhere->pLeft;
	}
	if (ExprHasProperty(pWhere, EP_FromJoin))
		return 0;	/* restriction 5 */
	if (sqlExprIsTableConstant(pWhere, iCursor)) {
		nChng++;
		while (pSubq) {
			pNew = sqlExprDup(pParse->db, pWhere, 0);
			pNew = substExpr(pParse, pNew, iCursor, pSubq->pEList);
			pSubq->pWhere = sql_and_expr_new(pParse->db,
							 pSubq->pWhere, pNew);
			if (pSubq->pWhere == NULL)
				pParse->is_aborted = true;
			pSubq = pSubq->pPrior;
		}
	}
	return nChng;
}

/*
 * Based on the contents of the AggInfo structure indicated by the first
 * argument, this function checks if the following are true:
 *
 *    * the query contains just a single aggregate function,
 *    * the aggregate function is either min() or max(), and
 *    * the argument to the aggregate function is a column value.
 *
 * If all of the above are true, then WHERE_ORDERBY_MIN or WHERE_ORDERBY_MAX
 * is returned as appropriate. Also, *ppMinMax is set to point to the
 * list of arguments passed to the aggregate before returning.
 *
 * Or, if the conditions above are not met, *ppMinMax is set to 0 and
 * WHERE_ORDERBY_NORMAL is returned.
 */
static u8
minMaxQuery(AggInfo * pAggInfo, ExprList ** ppMinMax)
{
	int eRet = WHERE_ORDERBY_NORMAL;	/* Return value */

	*ppMinMax = 0;
	if (pAggInfo->nFunc == 1) {
		Expr *pExpr = pAggInfo->aFunc[0].pExpr;	/* Aggregate function */
		ExprList *pEList = pExpr->x.pList;	/* Arguments to agg function */

		assert(pExpr->op == TK_AGG_FUNCTION);
		if (pEList && pEList->nExpr == 1
		    && pEList->a[0].pExpr->op == TK_AGG_COLUMN) {
			const char *zFunc = pExpr->u.zToken;
			if (sqlStrICmp(zFunc, "min") == 0) {
				eRet = WHERE_ORDERBY_MIN;
				*ppMinMax = pEList;
			} else if (sqlStrICmp(zFunc, "max") == 0) {
				eRet = WHERE_ORDERBY_MAX;
				*ppMinMax = pEList;
			}
		}
	}

	assert(*ppMinMax == 0 || (*ppMinMax)->nExpr == 1);
	return eRet;
}

/**
 * The second argument is the associated aggregate-info object.
 * This function tests if the SELECT is of the form:
 *
 *   SELECT count(*) FROM <tbl>
 *
 * where table is not a sub-select or view.
 *
 * @param select The select statement in form of aggregate query.
 * @param agg_info The associated aggregate-info object.
 * @retval Pointer to space representing the table,
 *         if the query matches this pattern. NULL otherwise.
 */
static struct space*
is_simple_count(struct Select *select, struct AggInfo *agg_info)
{
	assert(select->pGroupBy == NULL);
	if (select->pWhere != NULL || select->pEList->nExpr != 1 ||
	    select->pSrc->nSrc != 1 || select->pSrc->a[0].pSelect != NULL) {
		return NULL;
	}
	struct space *space = select->pSrc->a[0].space;
	assert(space != NULL && !space->def->opts.is_view);
	struct Expr *expr = select->pEList->a[0].pExpr;
	assert(expr != NULL);
	if (expr->op != TK_AGG_FUNCTION)
		return NULL;
	if (NEVER(agg_info->nFunc == 0))
		return NULL;
	assert(agg_info->aFunc->func->def->language ==
	       FUNC_LANGUAGE_SQL_BUILTIN);
	if (sql_func_flag_is_set(agg_info->aFunc->func, SQL_FUNC_COUNT) ||
	    (agg_info->aFunc->pExpr->x.pList != NULL &&
	     agg_info->aFunc->pExpr->x.pList->nExpr > 0))
		return NULL;
	if (expr->flags & EP_Distinct)
		return NULL;
	return space;
}

/*
 * If the source-list item passed as an argument was augmented with an
 * INDEXED BY clause, then try to locate the specified index. If there
 * was such a clause and the named index cannot be found, return
 * -1 and set an error. Otherwise, populate
 * pFrom->pIndex and return 0.
 */
int
sqlIndexedByLookup(Parse * pParse, struct SrcList_item *pFrom)
{
	if (pFrom->space != NULL && pFrom->fg.isIndexedBy) {
		struct space *space = pFrom->space;
		char *zIndexedBy = pFrom->u1.zIndexedBy;
		struct index *idx = NULL;
		for (uint32_t i = 0; i < space->index_count; ++i) {
			if (strcmp(space->index[i]->def->name,
				   zIndexedBy) == 0) {
				idx = space->index[i];
				break;
			}
		}
		if (idx == NULL) {
			diag_set(ClientError, ER_NO_SUCH_INDEX_NAME,
				 zIndexedBy, space->def->name);
			pParse->is_aborted = true;
			return -1;
		}
		pFrom->pIBIndex = idx->def;
	}
	return 0;
}

/*
 * Detect compound SELECT statements that use an ORDER BY clause with
 * an alternative collating sequence.
 *
 *    SELECT ... FROM t1 EXCEPT SELECT ... FROM t2 ORDER BY .. COLLATE ...
 *
 * These are rewritten as a subquery:
 *
 *    SELECT * FROM (SELECT ... FROM t1 EXCEPT SELECT ... FROM t2)
 *     ORDER BY ... COLLATE ...
 *
 * This transformation is necessary because the multiSelectOrderBy() routine
 * above that generates the code for a compound SELECT with an ORDER BY clause
 * uses a merge algorithm that requires the same collating sequence on the
 * result columns as on the ORDER BY clause.
 *
 * This transformation is only needed for EXCEPT, INTERSECT, and UNION.
 * The UNION ALL operator works fine with multiSelectOrderBy() even when
 * there are COLLATE terms in the ORDER BY.
 */
static int
convertCompoundSelectToSubquery(Walker * pWalker, Select * p)
{
	int i;
	Select *pNew;
	Select *pX;
	sql *db;
	struct ExprList_item *a;
	SrcList *pNewSrc;
	Parse *pParse;
	Token dummy;

	if (p->pPrior == 0)
		return WRC_Continue;
	if (p->pOrderBy == 0)
		return WRC_Continue;
	for (pX = p; pX && (pX->op == TK_ALL || pX->op == TK_SELECT);
	     pX = pX->pPrior) {
	}
	if (pX == 0)
		return WRC_Continue;
	a = p->pOrderBy->a;
	for (i = p->pOrderBy->nExpr - 1; i >= 0; i--) {
		if (a[i].pExpr->flags & EP_Collate)
			break;
	}
	if (i < 0)
		return WRC_Continue;

	/* If we reach this point, that means the transformation is required. */

	pParse = pWalker->pParse;
	db = pParse->db;
	pNew = sqlDbMallocZero(db, sizeof(*pNew));
	if (pNew == 0)
		return WRC_Abort;
	memset(&dummy, 0, sizeof(dummy));
	pNewSrc =
	    sqlSrcListAppendFromTerm(pParse, 0, 0, &dummy, pNew, 0, 0);
	if (pNewSrc == 0)
		return WRC_Abort;
	*pNew = *p;
	p->pSrc = pNewSrc;
	struct Expr *expr = sql_expr_new_anon(db, TK_ASTERISK);
	if (expr == NULL)
		pParse->is_aborted = true;
	p->pEList = sql_expr_list_append(pParse->db, NULL, expr);
	p->op = TK_SELECT;
	p->pWhere = 0;
	pNew->pGroupBy = 0;
	pNew->pHaving = 0;
	pNew->pOrderBy = 0;
	p->pPrior = 0;
	p->pNext = 0;
	p->pWith = 0;
	p->selFlags &= ~SF_Compound;
	assert((p->selFlags & SF_Converted) == 0);
	p->selFlags |= SF_Converted;
	assert(pNew->pPrior != 0);
	pNew->pPrior->pNext = pNew;
	pNew->pLimit = 0;
	pNew->pOffset = 0;
	return WRC_Continue;
}

/*
 * Argument pWith (which may be NULL) points to a linked list of nested
 * WITH contexts, from inner to outermost. If the table identified by
 * FROM clause element pItem is really a common-table-expression (CTE)
 * then return a pointer to the CTE definition for that table. Otherwise
 * return NULL.
 *
 * If a non-NULL value is returned, set *ppContext to point to the With
 * object that the returned CTE belongs to.
 */
static struct Cte *
searchWith(With * pWith,		/* Current innermost WITH clause */
	   struct SrcList_item *pItem,	/* FROM clause element to resolve */
	   With ** ppContext)		/* OUT: WITH clause return value belongs to */
{
	const char *zName;
	if ((zName = pItem->zName) != 0) {
		With *p;
		for (p = pWith; p; p = p->pOuter) {
			int i;
			for (i = 0; i < p->nCte; i++) {
				if (strcmp(zName, p->a[i].zName) == 0) {
					*ppContext = p;
					return &p->a[i];
				}
			}
		}
	}
	return 0;
}

/* The code generator maintains a stack of active WITH clauses
 * with the inner-most WITH clause being at the top of the stack.
 *
 * This routine pushes the WITH clause passed as the second argument
 * onto the top of the stack. If argument bFree is true, then this
 * WITH clause will never be popped from the stack. In this case it
 * should be freed along with the Parse object. In other cases, when
 * bFree==0, the With object will be freed along with the SELECT
 * statement with which it is associated.
 */
void
sqlWithPush(Parse * pParse, With * pWith, u8 bFree)
{
	assert(bFree == 0 || (pParse->pWith == 0 && pParse->pWithToFree == 0));
	if (pWith) {
		assert(pParse->pWith != pWith);
		pWith->pOuter = pParse->pWith;
		pParse->pWith = pWith;
		if (bFree)
			pParse->pWithToFree = pWith;
	}
}

/*
 * This function checks if argument pFrom refers to a CTE declared by
 * a WITH clause on the stack currently maintained by the parser. And,
 * if currently processing a CTE expression, if it is a recursive
 * reference to the current CTE.
 *
 * If pFrom falls into either of the two categories above, pFrom->space
 * and other fields are populated accordingly. The caller should check
 * (pFrom->space!=0) to determine whether or not a successful match
 * was found.
 *
 * Whether or not a match is found, 0 is returned if no error
 * occurs. If an error does occur, an error message is stored in the
 * parser and some error code other than 0 returned.
 */
static int
withExpand(Walker * pWalker, struct SrcList_item *pFrom)
{
	Parse *pParse = pWalker->pParse;
	sql *db = pParse->db;
	struct Cte *pCte;	/* Matched CTE (or NULL if no match) */
	With *pWith;		/* WITH clause that pCte belongs to */

	assert(pFrom->space == NULL);

	pCte = searchWith(pParse->pWith, pFrom, &pWith);
	if (pCte) {
		ExprList *pEList;
		Select *pSel;
		Select *pLeft;	/* Left-most SELECT statement */
		int bMayRecursive;	/* True if compound joined by UNION [ALL] */
		With *pSavedWith;	/* Initial value of pParse->pWith */

		/* If pCte->zCteErr is non-NULL at this point, then this is an illegal
		 * recursive reference to CTE pCte. Leave an error in pParse and return
		 * early. If pCte->zCteErr is NULL, then this is not a recursive reference.
		 * In this case, proceed.
		 */
		if (pCte->zCteErr) {
			diag_set(ClientError, ER_SQL_PARSER_GENERIC,
				 tt_sprintf(pCte->zCteErr, pCte->zName));
			pParse->is_aborted = true;
			return -1;
		}
		if (pFrom->fg.isTabFunc) {
			const char *err = "'%s' is not a function";
			diag_set(ClientError, ER_SQL_PARSER_GENERIC,
				 tt_sprintf(err, pFrom->zName));
			pParse->is_aborted = true;
			return -1;
		}

		assert(pFrom->space == NULL);
		pFrom->space = sql_ephemeral_space_new(pParse, pCte->zName);
		if (pFrom->space == NULL)
			return WRC_Abort;
		pFrom->pSelect = sqlSelectDup(db, pCte->pSelect, 0);
		if (db->mallocFailed)
			return -1;
		assert(pFrom->pSelect);

		/* Check if this is a recursive CTE. */
		pSel = pFrom->pSelect;
		bMayRecursive = (pSel->op == TK_ALL || pSel->op == TK_UNION);
		uint32_t ref_counter = 0;
		if (bMayRecursive) {
			int i;
			SrcList *pSrc = pFrom->pSelect->pSrc;
			for (i = 0; i < pSrc->nSrc; i++) {
				struct SrcList_item *pItem = &pSrc->a[i];
				if (pItem->zName != 0
				    && 0 == sqlStrICmp(pItem->zName,
							   pCte->zName)
				    ) {
					pItem->space = pFrom->space;
					pItem->fg.isRecursive = 1;
					ref_counter++;
					pSel->selFlags |= SF_Recursive;
				}
			}
		}
		if (ref_counter > 1) {
			const char *err_msg =
				tt_sprintf("multiple references to recursive "\
					   "table: %s", pCte->zName);
			diag_set(ClientError, ER_SQL_PARSER_GENERIC, err_msg);
			pParse->is_aborted = true;
			return -1;
		}
		assert(ref_counter == 0 ||
			((pSel->selFlags & SF_Recursive) && ref_counter == 1));

		pCte->zCteErr = "circular reference: %s";
		pSavedWith = pParse->pWith;
		pParse->pWith = pWith;
		sqlWalkSelect(pWalker, bMayRecursive ? pSel->pPrior : pSel);
		pParse->pWith = pWith;

		for (pLeft = pSel; pLeft->pPrior; pLeft = pLeft->pPrior) ;
		pEList = pLeft->pEList;
		if (pCte->pCols) {
			if (pEList && pEList->nExpr != pCte->pCols->nExpr) {
				const char *err_msg =
					tt_sprintf("table %s has %d values "\
						   "for %d columns",
						   pCte->zName, pEList->nExpr,
						   pCte->pCols->nExpr);
				diag_set(ClientError, ER_SQL_PARSER_GENERIC, err_msg);
				pParse->is_aborted = true;
				pParse->pWith = pSavedWith;
				return -1;
			}
			pEList = pCte->pCols;
		}

		sqlColumnsFromExprList(pParse, pEList, pFrom->space->def);

		if (bMayRecursive) {
			if (pSel->selFlags & SF_Recursive) {
				pCte->zCteErr =
				    "multiple recursive references: %s";
			} else {
				pCte->zCteErr =
				    "recursive reference in a subquery: %s";
			}
			sqlWalkSelect(pWalker, pSel);
		}
		pCte->zCteErr = 0;
		pParse->pWith = pSavedWith;
	}

	return 0;
}

/*
 * If the SELECT passed as the second argument has an associated WITH
 * clause, pop it from the stack stored as part of the Parse object.
 *
 * This function is used as the xSelectCallback2() callback by
 * sqlSelectExpand() when walking a SELECT tree to resolve table
 * names and other FROM clause elements.
 */
static void
selectPopWith(Walker * pWalker, Select * p)
{
	Parse *pParse = pWalker->pParse;
	With *pWith = findRightmost(p)->pWith;
	if (pWith != 0) {
		assert(pParse->pWith == pWith);
		pParse->pWith = pWith->pOuter;
	}
}

/**
 * Determine whether to generate a name for @a expr or not.
 *
 * Auto generated names is needed for every item in  a <SELECT>
 * expression list except asterisks, dots and column names (also
 * if this item hasn't alias).
 *
 * @param expr Expression from expression list to analyze.
 *
 * @retval true If item has to be named with auto-generated name.
 */
static bool
expr_autoname_is_required(struct Expr *expr)
{
	return (expr->op != TK_ASTERISK && expr->op != TK_DOT &&
		expr->op != TK_ID);
}

/*
 * This routine is a Walker callback for "expanding" a SELECT statement.
 * "Expanding" means to do the following:
 *
 *    (1)  Make sure VDBE cursor numbers have been assigned to every
 *         element of the FROM clause.
 *
 *    (2)  Fill in the pTabList->a[].pTab fields in the SrcList that
 *         defines FROM clause.  When views appear in the FROM clause,
 *         fill pTabList->a[].pSelect with a copy of the SELECT statement
 *         that implements the view.  A copy is made of the view's SELECT
 *         statement so that we can freely modify or delete that statement
 *         without worrying about messing up the persistent representation
 *         of the view.
 *
 *    (3)  Add terms to the WHERE clause to accommodate the NATURAL keyword
 *         on joins and the ON and USING clause of joins.
 *
 *    (4)  Scan the list of columns in the result set (pEList) looking
 *         for instances of the "*" operator or the TABLE.* operator.
 *         If found, expand each "*" to be every column in every table
 *         and TABLE.* to be every column in TABLE.
 *
 */
static int
selectExpander(Walker * pWalker, Select * p)
{
	Parse *pParse = pWalker->pParse;
	int i, j, k;
	SrcList *pTabList;
	ExprList *pEList;
	struct SrcList_item *pFrom;
	sql *db = pParse->db;
	Expr *pE, *pRight, *pExpr;
	u16 selFlags = p->selFlags;

	p->selFlags |= SF_Expanded;
	if (db->mallocFailed) {
		return WRC_Abort;
	}
	if (NEVER(p->pSrc == 0) || (selFlags & SF_Expanded) != 0) {
		return WRC_Prune;
	}
	pTabList = p->pSrc;
	pEList = p->pEList;
	if (pWalker->xSelectCallback2 == selectPopWith) {
		sqlWithPush(pParse, findRightmost(p)->pWith, 0);
	}

	/* Make sure cursor numbers have been assigned to all entries in
	 * the FROM clause of the SELECT statement.
	 */
	sqlSrcListAssignCursors(pParse, pTabList);

	/* Look up every table named in the FROM clause of the select.  If
	 * an entry of the FROM clause is a subquery instead of a table or view,
	 * then create a transient space structure to describe the subquery.
	 */
	for (i = 0, pFrom = pTabList->a; i < pTabList->nSrc; i++, pFrom++) {
		assert(pFrom->fg.isRecursive == 0 || pFrom->space != NULL);
		if (pFrom->fg.isRecursive)
			continue;
		assert(pFrom->space == NULL);

		if (withExpand(pWalker, pFrom))
			return WRC_Abort;
		if (pFrom->space != NULL) {
		} else

		if (pFrom->zName == 0) {
			Select *pSel = pFrom->pSelect;
			/* A sub-query in the FROM clause of a SELECT */
			assert(pSel != 0);
			assert(pFrom->space == NULL);
			if (sqlWalkSelect(pWalker, pSel))
				return WRC_Abort;
			/*
			 * Will be overwritten with pointer as
			 * unique identifier.
			 */
			const char *name = "sql_sq_DEADBEAFDEADBEAF";
			struct space *space =
				sql_ephemeral_space_new(sqlParseToplevel(pParse),
							name);
			if (space == NULL)
				return WRC_Abort;
			pFrom->space = space;
			/*
			 * Rewrite old name with correct pointer.
			 */
			name = tt_sprintf("sql_sq_%llX", (void *)space);
			sprintf(space->def->name, "%s", name);
			while (pSel->pPrior) {
				pSel = pSel->pPrior;
			}
			sqlColumnsFromExprList(pParse, pSel->pEList,
					       space->def);
		} else {
			/*
			 * An ordinary table or view name in the
			 * FROM clause.
			 */
			struct space *space = sql_lookup_space(pParse, pFrom);
			if (space == NULL)
				return WRC_Abort;
			if (pFrom->fg.isTabFunc) {
				const char *err =
					tt_sprintf("'%s' is not a function",
						   pFrom->zName);
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 err);
				pParse->is_aborted = true;
				return WRC_Abort;
			}
			if (space->def->opts.is_view) {
				struct Select *select =
					sql_view_compile(db, space->def->opts.sql);
				if (select == NULL)
					return WRC_Abort;
				sqlSrcListAssignCursors(pParse,
							    select->pSrc);
				assert(pFrom->pSelect == 0);
				pFrom->pSelect = select;
				sqlSelectSetName(pFrom->pSelect,
						 space->def->name);
				sqlWalkSelect(pWalker, pFrom->pSelect);
			}
		}
		/* Locate the index named by the INDEXED BY clause, if any. */
		if (sqlIndexedByLookup(pParse, pFrom)) {
			return WRC_Abort;
		}
	}

	/* Process NATURAL keywords, and ON and USING clauses of joins.
	 */
	if (db->mallocFailed || sqlProcessJoin(pParse, p)) {
		return WRC_Abort;
	}

	/* For every "*" that occurs in the column list, insert the names of
	 * all columns in all tables.  And for every TABLE.* insert the names
	 * of all columns in TABLE.  The parser inserted a special expression
	 * with the TK_ASTERISK operator for each "*" that it found in the column
	 * list.  The following code just has to locate the TK_ASTERISK
	 * expressions and expand each one to the list of all columns in
	 * all tables.
	 *
	 * The first loop just checks to see if there are any "*" operators
	 * that need expanding and names items of the expression
	 * list if needed.
	 */
	bool has_asterisk = false;
	for (k = 0; k < pEList->nExpr; k++) {
		pE = pEList->a[k].pExpr;
		if (pE->op == TK_ASTERISK)
			has_asterisk = true;
		assert(pE->op != TK_DOT || pE->pRight != 0);
		assert(pE->op != TK_DOT
		       || (pE->pLeft != 0 && pE->pLeft->op == TK_ID));
		if (pE->op == TK_DOT && pE->pRight->op == TK_ASTERISK)
			has_asterisk = true;
		if (pEList->a[k].zName == NULL &&
		    expr_autoname_is_required(pE)) {
			uint32_t idx = ++pParse->autoname_i;
			pEList->a[k].zName =
				sqlDbStrDup(db, sql_generate_column_name(idx));
		}
	}
	if (has_asterisk) {
		/*
		 * If we get here it means the result set contains one or more "*"
		 * operators that need to be expanded.  Loop through each expression
		 * in the result set and expand them one by one.
		 */
		struct ExprList_item *a = pEList->a;
		ExprList *pNew = 0;
		uint32_t flags = pParse->sql_flags;
		int longNames = (flags & SQL_FullColNames) != 0;

		for (k = 0; k < pEList->nExpr; k++) {
			pE = a[k].pExpr;
			pRight = pE->pRight;
			assert(pE->op != TK_DOT || pRight != 0);
			if (pE->op != TK_ASTERISK
			    && (pE->op != TK_DOT || pRight->op != TK_ASTERISK)
			    ) {
				/* This particular expression does not need to be expanded.
				 */
				pNew = sql_expr_list_append(pParse->db, pNew,
							    a[k].pExpr);
				if (pNew != NULL) {
					pNew->a[pNew->nExpr - 1].zName =
					    a[k].zName;
					pNew->a[pNew->nExpr - 1].zSpan =
					    a[k].zSpan;
					a[k].zName = 0;
					a[k].zSpan = 0;
				}
				a[k].pExpr = 0;
			} else {
				/* This expression is a "*" or a "TABLE.*" and needs to be
				 * expanded.
				 */
				int tableSeen = 0;	/* Set to 1 when TABLE matches */
				char *zTName = 0;	/* text of name of TABLE */
				if (pE->op == TK_DOT) {
					assert(pE->pLeft != 0);
					assert(!ExprHasProperty
					       (pE->pLeft, EP_IntValue));
					zTName = pE->pLeft->u.zToken;
				}
				for (i = 0, pFrom = pTabList->a;
				     i < pTabList->nSrc; i++, pFrom++) {
					struct space *space = pFrom->space;
					Select *pSub = pFrom->pSelect;
					char *zTabName = pFrom->zAlias;
					if (zTabName == NULL)
						zTabName = space->def->name;
					if (db->mallocFailed)
						break;
					if (pSub == 0
					    || (pSub->
						selFlags & SF_NestedFrom) ==
					    0) {
						pSub = 0;
						if (zTName != NULL
						    && strcmp(zTName, zTabName)
						    != 0) {
							continue;
						}
					}
					for (j = 0; j < (int)space->def->field_count; j++) {
						char *zName = space->def->fields[j].name;
						char *zColname;	/* The computed column name */
						char *zToFree;	/* Malloced string that needs to be freed */
						Token sColname;	/* Computed column name as a token */

						assert(zName);
						if (zTName && pSub
						    && sqlMatchSpanName(pSub->pEList->a[j].zSpan,
									    0,
									    zTName) == 0) {
							continue;
						}
						tableSeen = 1;

						if (i > 0 && zTName == 0) {
							if ((pFrom->fg.jointype & JT_NATURAL) != 0
							    && tableAndColumnIndex(pTabList, i, zName, 0, 0)) {
								/* In a NATURAL join, omit the join columns from the
								 * table to the right of the join
								 */
								continue;
							}
							if (sqlIdListIndex(pFrom->pUsing, zName) >= 0) {
								/* In a join with a USING clause, omit columns in the
								 * using clause from the table on the right.
								 */
								continue;
							}
						}
						pRight = sql_expr_new_named(db,
								TK_ID, zName);
						if (pRight == NULL)
							pParse->is_aborted = true;
						zColname = zName;
						zToFree = 0;
						if (longNames
						    || pTabList->nSrc > 1) {
							Expr *pLeft;
							pLeft = sql_expr_new_named(
									db,
									TK_ID,
									zTabName);
							if (pLeft == NULL) {
								pParse->
								is_aborted = true;
							}
							pExpr =
							    sqlPExpr(pParse,
									 TK_DOT,
									 pLeft,
									 pRight);
							if (longNames) {
								zColname =
								    sqlMPrintf
								    (db,
								     "%s.%s",
								     zTabName,
								     zName);
								zToFree =
								    zColname;
							}
						} else {
							pExpr = pRight;
						}
						pNew = sql_expr_list_append(
							pParse->db, pNew, pExpr);
						sqlTokenInit(&sColname, zColname);
						sqlExprListSetName(pParse,
								       pNew,
								       &sColname,
								       0);
						if (pNew != NULL
						    && (p->
							selFlags &
							SF_NestedFrom) != 0) {
							struct ExprList_item *pX
							    =
							    &pNew->a[pNew->
								     nExpr - 1];
							if (pSub) {
								pX->zSpan = sqlDbStrDup(db,
											    pSub->pEList->a[j].zSpan);
								testcase(pX->zSpan == 0);
							} else {
								pX->zSpan = sqlMPrintf(db,
											   "%s.%s",
											   zTabName,
											   zColname);
								testcase(pX->zSpan == 0);
							}
							pX->bSpanIsTab = 1;
						}
						sqlDbFree(db, zToFree);
					}
				}
				if (!tableSeen) {
					if (zTName) {
						diag_set(ClientError,
							 ER_NO_SUCH_SPACE,
							 zTName);
					} else {
						diag_set(ClientError,
							 ER_SQL_SELECT_WILDCARD);
					}
					pParse->is_aborted = true;
				}
			}
		}
		sql_expr_list_delete(db, pEList);
		p->pEList = pNew;
	}
#if SQL_MAX_COLUMN
	if (p->pEList && p->pEList->nExpr > db->aLimit[SQL_LIMIT_COLUMN]) {
		diag_set(ClientError, ER_SQL_PARSER_LIMIT, "The number of "\
			 "columns in result set", p->pEList->nExpr,
			 db->aLimit[SQL_LIMIT_COLUMN]);
		pParse->is_aborted = true;
		return WRC_Abort;
	}
#endif
	return WRC_Continue;
}

/*
 * No-op routine for the parse-tree walker.
 *
 * When this routine is the Walker.xExprCallback then expression trees
 * are walked without any actions being taken at each node.  Presumably,
 * when this routine is used for Walker.xExprCallback then
 * Walker.xSelectCallback is set to do something useful for every
 * subquery in the parser tree.
 */
int
sqlExprWalkNoop(Walker * NotUsed, Expr * NotUsed2)
{
	UNUSED_PARAMETER2(NotUsed, NotUsed2);
	return WRC_Continue;
}

/*
 * This routine "expands" a SELECT statement and all of its subqueries.
 * For additional information on what it means to "expand" a SELECT
 * statement, see the comment on the selectExpand worker callback above.
 *
 * Expanding a SELECT statement is the first step in processing a
 * SELECT statement.  The SELECT statement must be expanded before
 * name resolution is performed.
 *
 * If anything goes wrong, an error message is written into pParse.
 * The calling function can detect the problem by looking at pParse->is_aborted
 * and/or pParse->db->mallocFailed.
 */
static void
sqlSelectExpand(Parse * pParse, Select * pSelect)
{
	Walker w;
	memset(&w, 0, sizeof(w));
	w.xExprCallback = sqlExprWalkNoop;
	w.pParse = pParse;
	if (pParse->hasCompound) {
		w.xSelectCallback = convertCompoundSelectToSubquery;
		sqlWalkSelect(&w, pSelect);
	}
	w.xSelectCallback = selectExpander;
	if ((pSelect->selFlags & SF_MultiValue) == 0) {
		w.xSelectCallback2 = selectPopWith;
	}
	sqlWalkSelect(&w, pSelect);
}

/*
 * This is a Walker.xSelectCallback callback for the sqlSelectTypeInfo()
 * interface.
 *
 * For each FROM-clause subquery, add Column.zType and Column.zColl
 * information to the Table structure that represents the result set
 * of that subquery.
 *
 * The Table structure that represents the result set was constructed
 * by selectExpander() but the type and collation information was omitted
 * at that point because identifiers had not yet been resolved.  This
 * routine is called after identifier resolution.
 */
static void
selectAddSubqueryTypeInfo(Walker * pWalker, Select * p)
{
	Parse *pParse;
	int i;
	SrcList *pTabList;
	struct SrcList_item *pFrom;

	assert(p->selFlags & SF_Resolved);
	assert((p->selFlags & SF_HasTypeInfo) == 0);
	p->selFlags |= SF_HasTypeInfo;
	pParse = pWalker->pParse;
	pTabList = p->pSrc;
	for (i = 0, pFrom = pTabList->a; i < pTabList->nSrc; i++, pFrom++) {
		struct space *space = pFrom->space;
		assert(space != NULL);
		if (space->def->id == 0) {
			/* A sub-query in the FROM clause of a SELECT */
			Select *pSel = pFrom->pSelect;
			if (pSel) {
				while (pSel->pPrior)
					pSel = pSel->pPrior;
				sqlSelectAddColumnTypeAndCollation(pParse,
								   space->def,
							 	   pSel);
			}
		}
	}
}

/*
 * This routine adds datatype and collating sequence information to
 * the Table structures of all FROM-clause subqueries in a
 * SELECT statement.
 *
 * Use this routine after name resolution.
 */
static void
sqlSelectAddTypeInfo(Parse * pParse, Select * pSelect)
{
	Walker w;
	memset(&w, 0, sizeof(w));
	w.xSelectCallback2 = selectAddSubqueryTypeInfo;
	w.xExprCallback = sqlExprWalkNoop;
	w.pParse = pParse;
	sqlWalkSelect(&w, pSelect);
}

/*
 * This routine sets up a SELECT statement for processing.  The
 * following is accomplished:
 *
 *     *  VDBE Cursor numbers are assigned to all FROM-clause terms.
 *     *  Ephemeral Table objects are created for all FROM-clause subqueries.
 *     *  ON and USING clauses are shifted into WHERE statements
 *     *  Wildcards "*" and "TABLE.*" in result sets are expanded.
 *     *  Identifiers in expression are matched to tables.
 *
 * This routine acts recursively on all subqueries within the SELECT.
 */
void
sqlSelectPrep(Parse * pParse,	/* The parser context */
		  Select * p,	/* The SELECT statement being coded. */
		  NameContext * pOuterNC	/* Name context for container */
    )
{
	sql *db;
	if (NEVER(p == 0))
		return;
	db = pParse->db;
	if (db->mallocFailed)
		return;
	if (p->selFlags & SF_HasTypeInfo)
		return;
	sqlSelectExpand(pParse, p);
	if (pParse->is_aborted || db->mallocFailed)
		return;
	sqlResolveSelectNames(pParse, p, pOuterNC);
	if (pParse->is_aborted || db->mallocFailed)
		return;
	sqlSelectAddTypeInfo(pParse, p);
}

/*
 * Reset the aggregate accumulator.
 *
 * The aggregate accumulator is a set of memory cells that hold
 * intermediate results while calculating an aggregate.  This
 * routine generates code that stores NULLs in all of those memory
 * cells.
 */
static void
resetAccumulator(Parse * pParse, AggInfo * pAggInfo)
{
	Vdbe *v = pParse->pVdbe;
	int i;
	struct AggInfo_func *pFunc;
	int nReg = pAggInfo->nFunc + pAggInfo->nColumn;
	if (nReg == 0)
		return;
#ifdef SQL_DEBUG
	/* Verify that all AggInfo registers are within the range specified by
	 * AggInfo.mnReg..AggInfo.mxReg
	 */
	assert(nReg <= pAggInfo->mxReg - pAggInfo->mnReg + 1);
	for (i = 0; i < pAggInfo->nColumn; i++) {
		assert(pAggInfo->aCol[i].iMem >= pAggInfo->mnReg
		       && pAggInfo->aCol[i].iMem <= pAggInfo->mxReg);
	}
	for (i = 0; i < pAggInfo->nFunc; i++) {
		assert(pAggInfo->aFunc[i].iMem >= pAggInfo->mnReg
		       && pAggInfo->aFunc[i].iMem <= pAggInfo->mxReg);
	}
#endif
	sqlVdbeAddOp3(v, OP_Null, 0, pAggInfo->mnReg, pAggInfo->mxReg);
	for (pFunc = pAggInfo->aFunc, i = 0; i < pAggInfo->nFunc; i++, pFunc++) {
		if (pFunc->iDistinct >= 0) {
			Expr *pE = pFunc->pExpr;
			assert(!ExprHasProperty(pE, EP_xIsSelect));
			if (pE->x.pList == 0 || pE->x.pList->nExpr != 1) {
				diag_set(ClientError, ER_SQL_PARSER_GENERIC,
					 "DISTINCT aggregates must have "\
					 "exactly one argument");
				pParse->is_aborted = true;
				pFunc->iDistinct = -1;
			} else {
				struct sql_key_info *key_info =
					sql_expr_list_to_key_info(pParse,
								  pE->x.pList,
								  0);
				sqlVdbeAddOp4(v, OP_OpenTEphemeral,
						  pFunc->reg_eph, 1, 0,
						  (char *)key_info, P4_KEYINFO);
				sqlVdbeAddOp3(v, OP_IteratorOpen,
						  pFunc->iDistinct, 0, pFunc->reg_eph);
			}
		}
	}
}

/*
 * Invoke the OP_AggFinalize opcode for every aggregate function
 * in the AggInfo structure.
 */
static void
finalizeAggFunctions(Parse * pParse, AggInfo * pAggInfo)
{
	Vdbe *v = pParse->pVdbe;
	int i;
	struct AggInfo_func *pF;
	for (i = 0, pF = pAggInfo->aFunc; i < pAggInfo->nFunc; i++, pF++) {
		ExprList *pList = pF->pExpr->x.pList;
		assert(!ExprHasProperty(pF->pExpr, EP_xIsSelect));
		sqlVdbeAddOp2(v, OP_AggFinal, pF->iMem,
				  pList ? pList->nExpr : 0);
		sqlVdbeAppendP4(v, pF->func, P4_FUNC);
	}
}

/*
 * Update the accumulator memory cells for an aggregate based on
 * the current cursor position.
 */
static void
updateAccumulator(Parse * pParse, AggInfo * pAggInfo)
{
	Vdbe *v = pParse->pVdbe;
	int i;
	int regHit = 0;
	int addrHitTest = 0;
	struct AggInfo_func *pF;
	struct AggInfo_col *pC;

	pAggInfo->directMode = 1;
	for (i = 0, pF = pAggInfo->aFunc; i < pAggInfo->nFunc; i++, pF++) {
		int nArg;
		int addrNext = 0;
		int regAgg;
		ExprList *pList = pF->pExpr->x.pList;
		assert(!ExprHasProperty(pF->pExpr, EP_xIsSelect));
		if (pList) {
			nArg = pList->nExpr;
			regAgg = sqlGetTempRange(pParse, nArg);
			sqlExprCodeExprList(pParse, pList, regAgg, 0,
						SQL_ECEL_DUP);
		} else {
			nArg = 0;
			regAgg = 0;
		}
		if (pF->iDistinct >= 0) {
			addrNext = sqlVdbeMakeLabel(v);
			testcase(nArg == 0);	/* Error condition */
			testcase(nArg > 1);	/* Also an error */
			vdbe_insert_distinct(pParse, pF->iDistinct, pF->reg_eph,
					     addrNext, 1, regAgg);
		}
		if (sql_func_flag_is_set(pF->func, SQL_FUNC_NEEDCOLL)) {
			struct coll *coll = NULL;
			struct ExprList_item *pItem;
			int j;
			assert(pList != 0);	/* pList!=0 if pF->pFunc has NEEDCOLL */
			bool unused;
			uint32_t id;
			for (j = 0, pItem = pList->a; coll == NULL && j < nArg;
			     j++, pItem++) {
				if (sql_expr_coll(pParse, pItem->pExpr,
						  &unused, &id, &coll) != 0)
					return;
			}
			if (regHit == 0 && pAggInfo->nAccumulator)
				regHit = ++pParse->nMem;
			sqlVdbeAddOp4(v, OP_CollSeq, regHit, 0, 0,
					  (char *)coll, P4_COLLSEQ);
		}
		sqlVdbeAddOp3(v, OP_AggStep0, 0, regAgg, pF->iMem);
		sqlVdbeAppendP4(v, pF->func, P4_FUNC);
		sqlVdbeChangeP5(v, (u8) nArg);
		sql_expr_type_cache_change(pParse, regAgg, nArg);
		sqlReleaseTempRange(pParse, regAgg, nArg);
		if (addrNext) {
			sqlVdbeResolveLabel(v, addrNext);
			sqlExprCacheClear(pParse);
		}
	}

	/* Before populating the accumulator registers, clear the column cache.
	 * Otherwise, if any of the required column values are already present
	 * in registers, sqlExprCode() may use OP_SCopy to copy the value
	 * to pC->iMem. But by the time the value is used, the original register
	 * may have been used, invalidating the underlying buffer holding the
	 * text or blob value. See ticket [883034dcb5].
	 *
	 * Another solution would be to change the OP_SCopy used to copy cached
	 * values to an OP_Copy.
	 */
	if (regHit) {
		addrHitTest = sqlVdbeAddOp1(v, OP_If, regHit);
		VdbeCoverage(v);
	}
	sqlExprCacheClear(pParse);
	for (i = 0, pC = pAggInfo->aCol; i < pAggInfo->nAccumulator; i++, pC++) {
		sqlExprCode(pParse, pC->pExpr, pC->iMem);
	}
	pAggInfo->directMode = 0;
	sqlExprCacheClear(pParse);
	if (addrHitTest) {
		sqlVdbeJumpHere(v, addrHitTest);
	}
}

/**
 * Add a single OP_Explain instruction to the VDBE to explain
 * a simple count(*) query ("SELECT count(*) FROM <tab>").
 * For memtx engine count is a simple operation,
 * which takes O(1) complexity.
 *
 * @param parse_context Current parsing context.
 * @param table_name Name of table being queried.
 */
static void
explain_simple_count(struct Parse *parse_context, const char *table_name)
{
	if (parse_context->explain == 2) {
		char *zEqp = sqlMPrintf(parse_context->db, "B+tree count %s",
					    table_name);
		sqlVdbeAddOp4(parse_context->pVdbe, OP_Explain,
				  parse_context->iSelectId, 0, 0, zEqp,
				  P4_DYNAMIC);
	}
}

/**
 * Generate VDBE code that HALT program when subselect returned
 * more than one row (determined as LIMIT 1 overflow).
 * @param parser Current parsing context.
 * @param limit_reg LIMIT register.
 * @param end_mark mark to jump if select returned distinct one
 *                 row as expected.
 */
static void
vdbe_code_raise_on_multiple_rows(struct Parse *parser, int limit_reg, int end_mark)
{
	assert(limit_reg != 0);
	struct Vdbe *v = sqlGetVdbe(parser);
	assert(v != NULL);

	int r1 = sqlGetTempReg(parser);
	sqlVdbeAddOp2(v, OP_Integer, 0, r1);
	sqlVdbeAddOp3(v, OP_Ne, r1, end_mark, limit_reg);
	const char *error = tt_sprintf(tnt_errcode_desc(ER_SQL_EXECUTE),
				       "Expression subquery returned more "\
				       "than 1 row");
	sqlVdbeAddOp4(v, OP_SetDiag, ER_SQL_EXECUTE, 0, 0, error, P4_STATIC);
	sqlVdbeAddOp1(v, OP_Halt, -1);
	sqlReleaseTempReg(parser, r1);
}

/*
 * Generate code for the SELECT statement given in the p argument.
 *
 * The results are returned according to the SelectDest structure.
 * See comments in sqlInt.h for further information.
 *
 * This routine does NOT free the Select structure passed in.  The
 * calling function needs to do that.
 *
 * @retval 0 on success.
 * @retval != 0 on error.
 */
int
sqlSelect(Parse * pParse,		/* The parser context */
	      Select * p,		/* The SELECT statement being coded. */
	      SelectDest * pDest)	/* What to do with the query results */
{
	int i, j;		/* Loop counters */
	WhereInfo *pWInfo;	/* Return from sqlWhereBegin() */
	Vdbe *v;		/* The virtual machine under construction */
	int isAgg;		/* True for select lists like "count(*)" */
	ExprList *pEList = 0;	/* List of columns to extract. */
	SrcList *pTabList;	/* List of tables to select from */
	Expr *pWhere;		/* The WHERE clause.  May be NULL */
	ExprList *pGroupBy;	/* The GROUP BY clause.  May be NULL */
	Expr *pHaving;		/* The HAVING clause.  May be NULL */
	int rc = 1;		/* Value to return from this function */
	DistinctCtx sDistinct;	/* Info on how to code the DISTINCT keyword */
	SortCtx sSort;		/* Info on how to code the ORDER BY clause */
	AggInfo sAggInfo;	/* Information used by aggregate queries */
	int iEnd;		/* Address of the end of the query */
	sql *db;		/* The database connection */
	int iRestoreSelectId = pParse->iSelectId;
	pParse->iSelectId = pParse->iNextSelectId++;

	db = pParse->db;
	if (p == 0 || db->mallocFailed || pParse->is_aborted) {
		return 1;
	}
	memset(&sAggInfo, 0, sizeof(sAggInfo));
#ifdef SQL_DEBUG
	pParse->nSelectIndent++;
	SELECTTRACE(1, pParse, p, ("begin processing:\n"));
	if (sqlSelectTrace & 0x100) {
		sqlTreeViewSelect(0, p, 0);
	}
#endif

	assert(p->pOrderBy == 0 || pDest->eDest != SRT_DistFifo);
	assert(p->pOrderBy == 0 || pDest->eDest != SRT_Fifo);
	assert(p->pOrderBy == 0 || pDest->eDest != SRT_DistQueue);
	assert(p->pOrderBy == 0 || pDest->eDest != SRT_Queue);
	if (IgnorableOrderby(pDest)) {
		assert(pDest->eDest == SRT_Exists || pDest->eDest == SRT_Union
		       || pDest->eDest == SRT_Except
		       || pDest->eDest == SRT_Discard
		       || pDest->eDest == SRT_Queue
		       || pDest->eDest == SRT_DistFifo
		       || pDest->eDest == SRT_DistQueue
		       || pDest->eDest == SRT_Fifo);
		/* If ORDER BY makes no difference in the output then neither does
		 * DISTINCT so it can be removed too.
		 */
		sql_expr_list_delete(db, p->pOrderBy);
		p->pOrderBy = 0;
		p->selFlags &= ~SF_Distinct;
	}
	sqlSelectPrep(pParse, p, 0);
	memset(&sSort, 0, sizeof(sSort));
	sSort.pOrderBy = p->pOrderBy;
	pTabList = p->pSrc;
	if (pParse->is_aborted || db->mallocFailed) {
		goto select_end;
	}
	assert(p->pEList != 0);
	isAgg = (p->selFlags & SF_Aggregate) != 0;
#ifdef SQL_DEBUG
	if (sqlSelectTrace & 0x100) {
		SELECTTRACE(0x100, pParse, p, ("after name resolution:\n"));
		sqlTreeViewSelect(0, p, 0);
	}
#endif

	/* Try to flatten subqueries in the FROM clause up into the main query
	 */
	for (i = 0; !p->pPrior && i < pTabList->nSrc; i++) {
		struct SrcList_item *pItem = &pTabList->a[i];
		Select *pSub = pItem->pSelect;
		int isAggSub;
		struct space *space = pItem->space;
		if (pSub == 0)
			continue;

		/* Catch mismatch in the declared columns of a view and the number of
		 * columns in the SELECT on the RHS
		 */
		if ((int)space->def->field_count != pSub->pEList->nExpr) {
			diag_set(ClientError, ER_CREATE_SPACE, space->def->name,
				 "number of aliases doesn't match provided "\
				 "columns");
			pParse->is_aborted = true;
			goto select_end;
		}

		isAggSub = (pSub->selFlags & SF_Aggregate) != 0;
		if (flattenSubquery(pParse, p, i, isAgg, isAggSub)) {
			/* This subquery can be absorbed into its parent. */
			if (isAggSub) {
				isAgg = 1;
				p->selFlags |= SF_Aggregate;
			}
			i = -1;
		}
		pTabList = p->pSrc;
		if (db->mallocFailed)
			goto select_end;
		if (!IgnorableOrderby(pDest)) {
			sSort.pOrderBy = p->pOrderBy;
		}
	}

	/* Get a pointer the VDBE under construction, allocating a new VDBE if one
	 * does not already exist
	 */
	v = sqlGetVdbe(pParse);
	if (v == 0)
		goto select_end;

	/* Handle compound SELECT statements using the separate multiSelect()
	 * procedure.
	 */
	if (p->pPrior) {
		rc = multiSelect(pParse, p, pDest);
		pParse->iSelectId = iRestoreSelectId;

		int end = sqlVdbeMakeLabel(v);
		if ((p->selFlags & SF_SingleRow) != 0 && p->iLimit != 0) {
			vdbe_code_raise_on_multiple_rows(pParse, p->iLimit,
							 end);
		}
		sqlVdbeResolveLabel(v, end);

#ifdef SQL_DEBUG
		SELECTTRACE(1, pParse, p, ("end compound-select processing\n"));
		pParse->nSelectIndent--;
#endif
		return rc;
	}

	/* Generate code for all sub-queries in the FROM clause
	 */
	for (i = 0; i < pTabList->nSrc; i++) {
		struct SrcList_item *pItem = &pTabList->a[i];
		SelectDest dest;
		Select *pSub = pItem->pSelect;
		if (pSub == 0)
			continue;

		/* Sometimes the code for a subquery will be generated more than
		 * once, if the subquery is part of the WHERE clause in a LEFT JOIN,
		 * for example.  In that case, do not regenerate the code to manifest
		 * a view or the co-routine to implement a view.  The first instance
		 * is sufficient, though the subroutine to manifest the view does need
		 * to be invoked again.
		 */
		if (pItem->addrFillSub) {
			if (pItem->fg.viaCoroutine == 0) {
				sqlVdbeAddOp2(v, OP_Gosub, pItem->regReturn,
						  pItem->addrFillSub);
			}
			continue;
		}

		/* Increment Parse.nHeight by the height of the largest expression
		 * tree referred to by this, the parent select. The child select
		 * may contain expression trees of at most
		 * (SQL_MAX_EXPR_DEPTH-Parse.nHeight) height. This is a bit
		 * more conservative than necessary, but much easier than enforcing
		 * an exact limit.
		 */
		pParse->nHeight += sqlSelectExprHeight(p);

		/* Make copies of constant WHERE-clause terms in the outer query down
		 * inside the subquery.  This can help the subquery to run more efficiently.
		 */
		if ((pItem->fg.jointype & JT_OUTER) == 0
		    && pushDownWhereTerms(pParse, pSub, p->pWhere,
					  pItem->iCursor)
		    ) {
#ifdef SQL_DEBUG
			if (sqlSelectTrace & 0x100) {
				SELECTTRACE(0x100, pParse, p,
					    ("After WHERE-clause push-down:\n"));
				sqlTreeViewSelect(0, p, 0);
			}
#endif
		}

		/* Generate code to implement the subquery
		 *
		 * The subquery is implemented as a co-routine if all of these are true:
		 *   (1)  The subquery is guaranteed to be the outer loop (so that it
		 *        does not need to be computed more than once)
		 *   (2)  The ALL keyword after SELECT is omitted.  (Applications are
		 *        allowed to say "SELECT ALL" instead of just "SELECT" to disable
		 *        the use of co-routines.)
		 *
		 * TODO: Are there other reasons beside (1) to use a co-routine
		 * implementation?
		 */
		if (i == 0 && (pTabList->nSrc == 1 || (pTabList->a[1].fg.jointype & (JT_LEFT | JT_CROSS)) != 0)	/* (1) */
		    &&(p->selFlags & SF_All) == 0	/* (2) */
		    && OptimizationEnabled(db, SQL_SubqCoroutine)	/* (3) */
		    ) {
			/* Implement a co-routine that will return a single row of the result
			 * set on each invocation.
			 */
			int addrTop = sqlVdbeCurrentAddr(v) + 1;
			pItem->regReturn = ++pParse->nMem;
			sqlVdbeAddOp3(v, OP_InitCoroutine, pItem->regReturn,
					  0, addrTop);
			VdbeComment((v, "%s", pItem->space->def->name));
			pItem->addrFillSub = addrTop;
			sqlSelectDestInit(&dest, SRT_Coroutine,
					      pItem->regReturn, -1);
			pItem->iSelectId = pParse->iNextSelectId;
			sqlSelect(pParse, pSub, &dest);
			pItem->fg.viaCoroutine = 1;
			pItem->regResult = dest.iSdst;
			sqlVdbeEndCoroutine(v, pItem->regReturn);
			sqlVdbeJumpHere(v, addrTop - 1);
			sqlClearTempRegCache(pParse);
		} else {
			/* Generate a subroutine that will fill
			 * an ephemeral space with the content
			 * of this subquery. pItem->addrFillSub
			 * will point to the address of the
			 * generated subroutine.
			 * pItem->regReturn is a register
			 * allocated to hold the subroutine
			 * return address
			 */
			int topAddr;
			int onceAddr = 0;
			int retAddr;
			assert(pItem->addrFillSub == 0);
			pItem->regReturn = ++pParse->nMem;
			topAddr =
			    sqlVdbeAddOp2(v, OP_Integer, 0,
					      pItem->regReturn);
			pItem->addrFillSub = topAddr + 1;
			if (pItem->fg.isCorrelated == 0) {
				/* If the subquery is not
				 * correlated and if we are not
				 * inside of a trigger, then
				 * we only need to compute the
				 * value of the subquery once.
				 */
				onceAddr = sqlVdbeAddOp0(v, OP_Once);
				VdbeCoverage(v);
				VdbeComment((v, "materialize \"%s\"",
					     pItem->space->def->name));
			} else {
				VdbeNoopComment((v, "materialize \"%s\"",
						 pItem->space->def->name));
			}
			sqlSelectDestInit(&dest, SRT_EphemTab,
					      pItem->iCursor, ++pParse->nMem);
			pItem->iSelectId = pParse->iNextSelectId;
			sqlSelect(pParse, pSub, &dest);
			if (onceAddr)
				sqlVdbeJumpHere(v, onceAddr);
			retAddr =
			    sqlVdbeAddOp1(v, OP_Return, pItem->regReturn);
			VdbeComment((v, "end %s", pItem->space->def->name));
			sqlVdbeChangeP1(v, topAddr, retAddr);
			sqlClearTempRegCache(pParse);
		}
		if (db->mallocFailed)
			goto select_end;
		pParse->nHeight -= sqlSelectExprHeight(p);
	}

	/* Various elements of the SELECT copied into local variables for
	 * convenience
	 */
	pEList = p->pEList;
	pWhere = p->pWhere;
	pGroupBy = p->pGroupBy;
	pHaving = p->pHaving;
	sDistinct.isTnct = (p->selFlags & SF_Distinct) != 0;

#ifdef SQL_DEBUG
	if (sqlSelectTrace & 0x400) {
		SELECTTRACE(0x400, pParse, p,
			    ("After all FROM-clause analysis:\n"));
		sqlTreeViewSelect(0, p, 0);
	}
#endif

	/* If the query is DISTINCT with an ORDER BY but is not an aggregate, and
	 * if the select-list is the same as the ORDER BY list, then this query
	 * can be rewritten as a GROUP BY. In other words, this:
	 *
	 *     SELECT DISTINCT xyz FROM ... ORDER BY xyz
	 *
	 * is transformed to:
	 *
	 *     SELECT xyz FROM ... GROUP BY xyz ORDER BY xyz
	 *
	 * The second form is preferred as a single index (or temp-table) may be
	 * used for both the ORDER BY and DISTINCT processing. As originally
	 * written the query must use a temp-table for at least one of the ORDER
	 * BY and DISTINCT, and an index or separate temp-table for the other.
	 */
	if ((p->selFlags & (SF_Distinct | SF_Aggregate)) == SF_Distinct
	    && sqlExprListCompare(sSort.pOrderBy, pEList, -1) == 0) {
		p->selFlags &= ~SF_Distinct;
		pGroupBy = p->pGroupBy = sql_expr_list_dup(db, pEList, 0);
		/* Notice that even thought SF_Distinct has been cleared from p->selFlags,
		 * the sDistinct.isTnct is still set.  Hence, isTnct represents the
		 * original setting of the SF_Distinct flag, not the current setting
		 */
		assert(sDistinct.isTnct);

#ifdef SQL_DEBUG
		if (sqlSelectTrace & 0x400) {
			SELECTTRACE(0x400, pParse, p,
				    ("Transform DISTINCT into GROUP BY:\n"));
			sqlTreeViewSelect(0, p, 0);
		}
#endif
	}

	/* If there is an ORDER BY clause, then create an ephemeral index to
	 * do the sorting.  But this sorting ephemeral index might end up
	 * being unused if the data can be extracted in pre-sorted order.
	 * If that is the case, then the OP_OpenEphemeral instruction will be
	 * changed to an OP_Noop once we figure out that the sorting index is
	 * not needed.  The sSort.addrSortIndex variable is used to facilitate
	 * that change.
	 */
	if (sSort.pOrderBy) {
		struct sql_key_info *key_info =
			sql_expr_list_to_key_info(pParse, sSort.pOrderBy, 0);
		sSort.reg_eph = ++pParse->nMem;
		sSort.iECursor = pParse->nTab++;
		/* Number of columns in transient table equals to number of columns in
		 * SELECT statement plus number of columns in ORDER BY statement
		 * and plus one column for ID.
		 */
		int nCols = pEList->nExpr + sSort.pOrderBy->nExpr + 1;
		if (key_info->parts[0].sort_order == SORT_ORDER_DESC) {
			sSort.sortFlags |= SORTFLAG_DESC;
		}
		sSort.addrSortIndex =
		    sqlVdbeAddOp4(v, OP_OpenTEphemeral,
				      sSort.reg_eph,
				      nCols,
				      0, (char *)key_info, P4_KEYINFO);
		sqlVdbeAddOp3(v, OP_IteratorOpen, sSort.iECursor, 0, sSort.reg_eph);
		VdbeComment((v, "Sort table"));
	} else {
		sSort.addrSortIndex = -1;
	}

	/* If the output is destined for a temporary table, open that table.
	 */
	if (pDest->eDest == SRT_EphemTab) {
		struct sql_key_info *key_info =
			sql_expr_list_to_key_info(pParse, pEList, 0);
		sqlVdbeAddOp4(v, OP_OpenTEphemeral, pDest->reg_eph,
				  pEList->nExpr + 1, 0, (char *)key_info,
				  P4_KEYINFO);
		sqlVdbeAddOp3(v, OP_IteratorOpen, pDest->iSDParm, 0,
				  pDest->reg_eph);

		VdbeComment((v, "Output table"));
	}

	/* Set the limiter.
	 */
	iEnd = sqlVdbeMakeLabel(v);
	if ((p->selFlags & SF_FixedLimit) == 0) {
		p->nSelectRow = 320;	/* 4 billion rows */
	}
	computeLimitRegisters(pParse, p, iEnd);
	if (p->iLimit == 0 && sSort.addrSortIndex >= 0) {
		sqlVdbeChangeOpcode(v, sSort.addrSortIndex, OP_SorterOpen);
		sqlVdbeChangeP1(v, sSort.addrSortIndex, sSort.iECursor);
		sqlVdbeChangeToNoop(v, sSort.addrSortIndex + 1);
		sSort.sortFlags |= SORTFLAG_UseSorter;
	}

	/* Open an ephemeral index to use for the distinct set.
	 */
	if (p->selFlags & SF_Distinct) {
		sDistinct.cur_eph = pParse->nTab++;
		sDistinct.reg_eph = ++pParse->nMem;
		struct sql_key_info *key_info =
			sql_expr_list_to_key_info(pParse, p->pEList, 0);
		sDistinct.addrTnct = sqlVdbeAddOp4(v, OP_OpenTEphemeral,
						       sDistinct.reg_eph,
						       key_info->part_count,
						       0, (char *)key_info,
						       P4_KEYINFO);
		sqlVdbeAddOp3(v, OP_IteratorOpen, sDistinct.cur_eph, 0,
				  sDistinct.reg_eph);
		VdbeComment((v, "Distinct table"));
		sDistinct.eTnctType = WHERE_DISTINCT_UNORDERED;
	} else {
		sDistinct.eTnctType = WHERE_DISTINCT_NOOP;
	}

	if (!isAgg && pGroupBy == 0) {
		/* No aggregate functions and no GROUP BY clause */
		u16 wctrlFlags = (sDistinct.isTnct ? WHERE_WANT_DISTINCT : 0);
		assert(WHERE_USE_LIMIT == SF_FixedLimit);
		wctrlFlags |= p->selFlags & SF_FixedLimit;

		/* Begin the database scan. */
		pWInfo =
		    sqlWhereBegin(pParse, pTabList, pWhere, sSort.pOrderBy,
				      p->pEList, wctrlFlags, p->nSelectRow);
		if (pWInfo == 0)
			goto select_end;
		if (sqlWhereOutputRowCount(pWInfo) < p->nSelectRow) {
			p->nSelectRow = sqlWhereOutputRowCount(pWInfo);
		}
		if (sDistinct.isTnct && sqlWhereIsDistinct(pWInfo)) {
			sDistinct.eTnctType = sqlWhereIsDistinct(pWInfo);
		}
		if (sSort.pOrderBy) {
			sSort.nOBSat = sqlWhereIsOrdered(pWInfo);
			sSort.bOrderedInnerLoop =
			    sqlWhereOrderedInnerLoop(pWInfo);
			if (sSort.nOBSat == sSort.pOrderBy->nExpr) {
				sSort.pOrderBy = 0;
			}
		}

		/* If sorting index that was created by a prior OP_OpenEphemeral
		 * instruction ended up not being needed, then change the OP_OpenEphemeral
		 * into an OP_Noop.
		 */
		if (sSort.addrSortIndex >= 0 && sSort.pOrderBy == 0) {
			/*
			 * To handle ordering two op-codes are
			 * emitted: OpenTEphemeral & IteratorOpen.
			 * sSort.addrSortIndex is address of
			 * first insn in a couple. To evict
			 * ephemral space, need to noop both
			 * op-codes.
			 */
			sqlVdbeChangeToNoop(v, sSort.addrSortIndex);
			sqlVdbeChangeToNoop(v, sSort.addrSortIndex + 1);
		}

		/* Use the standard inner loop. */
		selectInnerLoop(pParse, p, pEList, -1, &sSort, &sDistinct,
				pDest, sqlWhereContinueLabel(pWInfo),
				sqlWhereBreakLabel(pWInfo));

		/* End the database scan loop.
		 */
		sqlWhereEnd(pWInfo);
	} else {
		/* This case when there exist aggregate functions or a GROUP BY clause
		 * or both
		 */
		NameContext sNC;	/* Name context for processing aggregate information */
		int iAMem;	/* First Mem address for storing current GROUP BY */
		int iBMem;	/* First Mem address for previous GROUP BY */
		int iUseFlag;	/* Mem address holding flag indicating that at least
				 * one row of the input to the aggregator has been
				 * processed
				 */
		int iAbortFlag;	/* Mem address which causes query abort if positive */
		int groupBySort;	/* Rows come from source in GROUP BY order */
		int addrEnd;	/* End of processing for this SELECT */
		int sortPTab = 0;	/* Pseudotable used to decode sorting results */
		int sortOut = 0;	/* Output register from the sorter */
		int orderByGrp = 0;	/* True if the GROUP BY and ORDER BY are the same */

		/* Remove any and all aliases between the result set and the
		 * GROUP BY clause.
		 */
		if (pGroupBy) {
			int k;	/* Loop counter */
			struct ExprList_item *pItem;	/* For looping over expression in a list */

			for (k = p->pEList->nExpr, pItem = p->pEList->a; k > 0;
			     k--, pItem++) {
				pItem->u.x.iAlias = 0;
			}
			for (k = pGroupBy->nExpr, pItem = pGroupBy->a; k > 0;
			     k--, pItem++) {
				pItem->u.x.iAlias = 0;
			}
			assert(66 == sqlLogEst(100));
			if (p->nSelectRow > 66)
				p->nSelectRow = 66;
		} else {
			assert(0 == sqlLogEst(1));
			p->nSelectRow = 0;
		}

		/* If there is both a GROUP BY and an ORDER BY clause and they are
		 * identical, then it may be possible to disable the ORDER BY clause
		 * on the grounds that the GROUP BY will cause elements to come out
		 * in the correct order. It also may not - the GROUP BY might use a
		 * database index that causes rows to be grouped together as required
		 * but not actually sorted. Either way, record the fact that the
		 * ORDER BY and GROUP BY clauses are the same by setting the orderByGrp
		 * variable.
		 */
		if (sqlExprListCompare(pGroupBy, sSort.pOrderBy, -1) == 0) {
			orderByGrp = 1;
		}

		/* Create a label to jump to when we want to abort the query */
		addrEnd = sqlVdbeMakeLabel(v);

		/* Convert TK_COLUMN_REF nodes into TK_AGG_COLUMN and make entries in
		 * sAggInfo for all TK_AGG_FUNCTION nodes in expressions of the
		 * SELECT statement.
		 */
		memset(&sNC, 0, sizeof(sNC));
		sNC.pParse = pParse;
		sNC.pSrcList = pTabList;
		sNC.pAggInfo = &sAggInfo;
		sAggInfo.mnReg = pParse->nMem + 1;
		sAggInfo.nSortingColumn = pGroupBy ? pGroupBy->nExpr : 0;
		sAggInfo.pGroupBy = pGroupBy;
		sqlExprAnalyzeAggList(&sNC, pEList);
		sqlExprAnalyzeAggList(&sNC, sSort.pOrderBy);
		if (pHaving) {
			sqlExprAnalyzeAggregates(&sNC, pHaving);
		}
		sAggInfo.nAccumulator = sAggInfo.nColumn;
		for (i = 0; i < sAggInfo.nFunc; i++) {
			assert(!ExprHasProperty
			       (sAggInfo.aFunc[i].pExpr, EP_xIsSelect));
			sNC.ncFlags |= NC_InAggFunc;
			sqlExprAnalyzeAggList(&sNC,
						  sAggInfo.aFunc[i].pExpr->x.
						  pList);
			sNC.ncFlags &= ~NC_InAggFunc;
		}
		sAggInfo.mxReg = pParse->nMem;
		if (db->mallocFailed)
			goto select_end;

		/* Processing for aggregates with GROUP BY is very different and
		 * much more complex than aggregates without a GROUP BY.
		 */
		if (pGroupBy) {
			int addr1;	/* A-vs-B comparision jump */
			int addrOutputRow;	/* Start of subroutine that outputs a result row */
			int regOutputRow;	/* Return address register for output subroutine */
			int addrSetAbort;	/* Set the abort flag and return */
			int addrTopOfLoop;	/* Top of the input loop */
			int addrSortingIdx;	/* The OP_OpenEphemeral for the sorting index */
			int addrReset;	/* Subroutine for resetting the accumulator */
			int regReset;	/* Return address register for reset subroutine */

			/* If there is a GROUP BY clause we might need a sorting index to
			 * implement it.  Allocate that sorting index now.  If it turns out
			 * that we do not need it after all, the OP_SorterOpen instruction
			 * will be converted into a Noop.
			 */
			sAggInfo.sortingIdx = pParse->nTab++;
			struct sql_key_info *key_info =
				sql_expr_list_to_key_info(pParse, pGroupBy, 0);
			addrSortingIdx =
			    sqlVdbeAddOp4(v, OP_SorterOpen,
					      sAggInfo.sortingIdx,
					      sAggInfo.nSortingColumn, 0,
					      (char *)key_info, P4_KEYINFO);

			/* Initialize memory locations used by GROUP BY aggregate processing
			 */
			iUseFlag = ++pParse->nMem;
			iAbortFlag = ++pParse->nMem;
			regOutputRow = ++pParse->nMem;
			addrOutputRow = sqlVdbeMakeLabel(v);
			regReset = ++pParse->nMem;
			addrReset = sqlVdbeMakeLabel(v);
			iAMem = pParse->nMem + 1;
			pParse->nMem += pGroupBy->nExpr;
			iBMem = pParse->nMem + 1;
			pParse->nMem += pGroupBy->nExpr;
			sqlVdbeAddOp2(v, OP_Integer, 0, iAbortFlag);
			VdbeComment((v, "clear abort flag"));
			sqlVdbeAddOp2(v, OP_Integer, 0, iUseFlag);
			VdbeComment((v, "indicate accumulator empty"));
			sqlVdbeAddOp3(v, OP_Null, 0, iAMem,
					  iAMem + pGroupBy->nExpr - 1);

			/* Begin a loop that will extract all source rows in GROUP BY order.
			 * This might involve two separate loops with an OP_Sort in between, or
			 * it might be a single loop that uses an index to extract information
			 * in the right order to begin with.
			 */
			sqlVdbeAddOp2(v, OP_Gosub, regReset, addrReset);
			pWInfo =
			    sqlWhereBegin(pParse, pTabList, pWhere,
					      pGroupBy, 0,
					      WHERE_GROUPBY | (orderByGrp ?
							       WHERE_SORTBYGROUP
							       : 0), 0);
			if (pWInfo == 0)
				goto select_end;
			if (sqlWhereIsOrdered(pWInfo) == pGroupBy->nExpr) {
				/* The optimizer is able to deliver rows in group by order so
				 * we do not have to sort.  The OP_OpenEphemeral table will be
				 * cancelled later because we still need to use the key_info
				 */
				groupBySort = 0;
			} else {
				/* Rows are coming out in undetermined order.  We have to push
				 * each row into a sorting index, terminate the first loop,
				 * then loop over the sorting index in order to get the output
				 * in sorted order
				 */
				int regBase;
				int regRecord;
				int nCol;
				int nGroupBy;

				explainTempTable(pParse,
						 (sDistinct.isTnct
						  && (p->
						      selFlags & SF_Distinct) ==
						  0) ? "DISTINCT" : "GROUP BY");

				groupBySort = 1;
				nGroupBy = pGroupBy->nExpr;
				nCol = nGroupBy;
				j = nGroupBy;
				for (i = 0; i < sAggInfo.nColumn; i++) {
					if (sAggInfo.aCol[i].iSorterColumn >= j) {
						nCol++;
						j++;
					}
				}
				regBase = sqlGetTempRange(pParse, nCol);
				sqlExprCacheClear(pParse);
				sqlExprCodeExprList(pParse, pGroupBy,
							regBase, 0, 0);
				j = nGroupBy;
				for (i = 0; i < sAggInfo.nColumn; i++) {
					struct AggInfo_col *pCol =
					    &sAggInfo.aCol[i];
					if (pCol->iSorterColumn >= j) {
						int r1 = j + regBase;
						sqlExprCodeGetColumnToReg
						    (pParse, pCol->iColumn,
						     pCol->iTable, r1);
						j++;
					}
				}
				regRecord = sqlGetTempReg(pParse);
				sqlVdbeAddOp3(v, OP_MakeRecord, regBase,
						  nCol, regRecord);
				sqlVdbeAddOp2(v, OP_SorterInsert,
						  sAggInfo.sortingIdx,
						  regRecord);
				sqlReleaseTempReg(pParse, regRecord);
				sqlReleaseTempRange(pParse, regBase, nCol);
				sqlWhereEnd(pWInfo);
				sAggInfo.sortingIdxPTab = sortPTab =
				    pParse->nTab++;
				sortOut = sqlGetTempReg(pParse);
				sqlVdbeAddOp3(v, OP_OpenPseudo, sortPTab,
						  sortOut, nCol);
				sqlVdbeAddOp2(v, OP_SorterSort,
						  sAggInfo.sortingIdx, addrEnd);
				VdbeComment((v, "GROUP BY sort"));
				VdbeCoverage(v);
				sAggInfo.useSortingIdx = 1;
				sqlExprCacheClear(pParse);

			}

			/* If the index or temporary table used by the GROUP BY sort
			 * will naturally deliver rows in the order required by the ORDER BY
			 * clause, cancel the ephemeral table open coded earlier.
			 *
			 * This is an optimization - the correct answer should result regardless.
			 * Use the SQL_GroupByOrder flag with SQL_TESTCTRL_OPTIMIZER to
			 * disable this optimization for testing purposes.
			 */
			if (orderByGrp
			    && OptimizationEnabled(db, SQL_GroupByOrder)
			    && (groupBySort || sqlWhereIsSorted(pWInfo))
			    ) {
				sSort.pOrderBy = 0;
				sqlVdbeChangeToNoop(v, sSort.addrSortIndex);
				sqlVdbeChangeToNoop(v, sSort.addrSortIndex + 1);
			}

			/* Evaluate the current GROUP BY terms and store in b0, b1, b2...
			 * (b0 is memory location iBMem+0, b1 is iBMem+1, and so forth)
			 * Then compare the current GROUP BY terms against the GROUP BY terms
			 * from the previous row currently stored in a0, a1, a2...
			 */
			addrTopOfLoop = sqlVdbeCurrentAddr(v);
			sqlExprCacheClear(pParse);
			if (groupBySort) {
				sqlVdbeAddOp3(v, OP_SorterData,
						  sAggInfo.sortingIdx, sortOut,
						  sortPTab);
			}
			for (j = 0; j < pGroupBy->nExpr; j++) {
				if (groupBySort) {
					sqlVdbeAddOp3(v, OP_Column,
							  sortPTab, j,
							  iBMem + j);
				} else {
					sAggInfo.directMode = 1;
					sqlExprCode(pParse,
							pGroupBy->a[j].pExpr,
							iBMem + j);
				}
			}
			sqlVdbeAddOp4(v, OP_Compare, iAMem, iBMem,
					  pGroupBy->nExpr,
					  (char*)sql_key_info_ref(key_info),
					  P4_KEYINFO);
			addr1 = sqlVdbeCurrentAddr(v);
			sqlVdbeAddOp3(v, OP_Jump, addr1 + 1, 0, addr1 + 1);
			VdbeCoverage(v);

			/* Generate code that runs whenever the GROUP BY changes.
			 * Changes in the GROUP BY are detected by the previous code
			 * block.  If there were no changes, this block is skipped.
			 *
			 * This code copies current group by terms in b0,b1,b2,...
			 * over to a0,a1,a2.  It then calls the output subroutine
			 * and resets the aggregate accumulator registers in preparation
			 * for the next GROUP BY batch.
			 */
			sqlExprCodeMove(pParse, iBMem, iAMem,
					    pGroupBy->nExpr);
			sqlVdbeAddOp2(v, OP_Gosub, regOutputRow,
					  addrOutputRow);
			VdbeComment((v, "output one row"));
			sqlVdbeAddOp2(v, OP_IfPos, iAbortFlag, addrEnd);
			VdbeCoverage(v);
			VdbeComment((v, "check abort flag"));
			sqlVdbeAddOp2(v, OP_Gosub, regReset, addrReset);
			VdbeComment((v, "reset accumulator"));

			/* Update the aggregate accumulators based on the content of
			 * the current row
			 */
			sqlVdbeJumpHere(v, addr1);
			updateAccumulator(pParse, &sAggInfo);
			sqlVdbeAddOp2(v, OP_Integer, 1, iUseFlag);
			VdbeComment((v, "indicate data in accumulator"));

			/* End of the loop
			 */
			if (groupBySort) {
				sqlVdbeAddOp2(v, OP_SorterNext,
						  sAggInfo.sortingIdx,
						  addrTopOfLoop);
				VdbeCoverage(v);
			} else {
				sqlWhereEnd(pWInfo);
				sqlVdbeChangeToNoop(v, addrSortingIdx);
			}

			/* Output the final row of result
			 */
			sqlVdbeAddOp2(v, OP_Gosub, regOutputRow,
					  addrOutputRow);
			VdbeComment((v, "output final row"));

			/* Jump over the subroutines
			 */
			sqlVdbeGoto(v, addrEnd);

			/* Generate a subroutine that outputs a single row of the result
			 * set.  This subroutine first looks at the iUseFlag.  If iUseFlag
			 * is less than or equal to zero, the subroutine is a no-op.  If
			 * the processing calls for the query to abort, this subroutine
			 * increments the iAbortFlag memory location before returning in
			 * order to signal the caller to abort.
			 */
			addrSetAbort = sqlVdbeCurrentAddr(v);
			sqlVdbeAddOp2(v, OP_Integer, 1, iAbortFlag);
			VdbeComment((v, "set abort flag"));
			sqlVdbeAddOp1(v, OP_Return, regOutputRow);
			sqlVdbeResolveLabel(v, addrOutputRow);
			addrOutputRow = sqlVdbeCurrentAddr(v);
			sqlVdbeAddOp2(v, OP_IfPos, iUseFlag,
					  addrOutputRow + 2);
			VdbeCoverage(v);
			VdbeComment((v,
				     "Groupby result generator entry point"));
			sqlVdbeAddOp1(v, OP_Return, regOutputRow);
			finalizeAggFunctions(pParse, &sAggInfo);
			sqlExprIfFalse(pParse, pHaving, addrOutputRow + 1,
					   SQL_JUMPIFNULL);
			selectInnerLoop(pParse, p, p->pEList, -1, &sSort,
					&sDistinct, pDest, addrOutputRow + 1,
					addrSetAbort);
			sqlVdbeAddOp1(v, OP_Return, regOutputRow);
			VdbeComment((v, "end groupby result generator"));

			/* Generate a subroutine that will reset the group-by accumulator
			 */
			sqlVdbeResolveLabel(v, addrReset);
			resetAccumulator(pParse, &sAggInfo);
			sqlVdbeAddOp1(v, OP_Return, regReset);

		} /* endif pGroupBy.  Begin aggregate queries without GROUP BY: */
		else {
			struct space *space = is_simple_count(p, &sAggInfo);
			if (space != NULL) {
				/*
				 * If is_simple_count() returns a pointer to
				 * space, then the SQL statement is of the form:
				 *
				 *   SELECT count(*) FROM <tbl>
				 *
				 * This statement is so common that it is
				 * optimized specially. The OP_Count instruction
				 * is executed on the primary key index,
				 * since there is no difference which index
				 * to choose.
				 */
				const int cursor = pParse->nTab++;
				/*
				 * Open the cursor, execute the OP_Count,
				 * close the cursor.
				 */
				vdbe_emit_open_cursor(pParse, cursor, 0, space);
				sqlVdbeAddOp2(v, OP_Count, cursor,
						  sAggInfo.aFunc[0].iMem);
				sqlVdbeAddOp1(v, OP_Close, cursor);
				explain_simple_count(pParse, space->def->name);
			} else
			{
				/* Check if the query is of one of the following forms:
				 *
				 *   SELECT min(x) FROM ...
				 *   SELECT max(x) FROM ...
				 *
				 * If it is, then ask the code in where.c to attempt to sort results
				 * as if there was an "ORDER ON x" or "ORDER ON x DESC" clause.
				 * If where.c is able to produce results sorted in this order, then
				 * add vdbe code to break out of the processing loop after the
				 * first iteration (since the first iteration of the loop is
				 * guaranteed to operate on the row with the minimum or maximum
				 * value of x, the only row required).
				 *
				 * A special flag must be passed to sqlWhereBegin() to slightly
				 * modify behavior as follows:
				 *
				 *   + If the query is a "SELECT min(x)", then the loop coded by
				 *     where.c should not iterate over any values with a NULL value
				 *     for x.
				 *
				 *   + The optimizer code in where.c (the thing that decides which
				 *     index or indices to use) should place a different priority on
				 *     satisfying the 'ORDER BY' clause than it does in other cases.
				 *     Refer to code and comments in where.c for details.
				 */
				ExprList *pMinMax = 0;
				u8 flag = WHERE_ORDERBY_NORMAL;
				ExprList *pDel = 0;

				assert(p->pGroupBy == 0);
				assert(flag == 0);
				if (p->pHaving == 0) {
					flag = minMaxQuery(&sAggInfo, &pMinMax);
				}
				assert(flag == 0
				       || (pMinMax != 0
					   && pMinMax->nExpr == 1));

				if (flag) {
					pMinMax =
					    sql_expr_list_dup(db, pMinMax, 0);
					pDel = pMinMax;
					assert(db->mallocFailed
					       || pMinMax != 0);
					if (!db->mallocFailed) {
						pMinMax->a[0].sort_order =
						    flag !=
						    WHERE_ORDERBY_MIN ? 1 : 0;
						pMinMax->a[0].pExpr->op =
						    TK_COLUMN_REF;
					}
				}

				/* This case runs if the aggregate has no GROUP BY clause.  The
				 * processing is much simpler since there is only a single row
				 * of output.
				 */
				resetAccumulator(pParse, &sAggInfo);
				pWInfo =
				    sqlWhereBegin(pParse, pTabList, pWhere,
						      pMinMax, 0, flag, 0);
				if (pWInfo == 0) {
					sql_expr_list_delete(db, pDel);
					goto select_end;
				}
				updateAccumulator(pParse, &sAggInfo);
				assert(pMinMax == 0 || pMinMax->nExpr == 1);
				if (sqlWhereIsOrdered(pWInfo) > 0) {
					sqlVdbeGoto(v,
							sqlWhereBreakLabel
							(pWInfo));
					VdbeComment((v, "%s() by index",
						     (flag ==
						      WHERE_ORDERBY_MIN ? "min"
						      : "max")));
				}
				sqlWhereEnd(pWInfo);
				finalizeAggFunctions(pParse, &sAggInfo);
				sql_expr_list_delete(db, pDel);
			}

			sSort.pOrderBy = 0;
			sqlExprIfFalse(pParse, pHaving, addrEnd,
					   SQL_JUMPIFNULL);
			selectInnerLoop(pParse, p, p->pEList, -1, 0, 0, pDest,
					addrEnd, addrEnd);
		}
		sqlVdbeResolveLabel(v, addrEnd);

	}			/* endif aggregate query */

	if (sDistinct.eTnctType == WHERE_DISTINCT_UNORDERED) {
		explainTempTable(pParse, "DISTINCT");
	}

	/* If there is an ORDER BY clause, then we need to sort the results
	 * and send them to the callback one by one.
	 */
	if (sSort.pOrderBy) {
		explainTempTable(pParse,
				 sSort.nOBSat >
				 0 ? "RIGHT PART OF ORDER BY" : "ORDER BY");
		generateSortTail(pParse, p, &sSort, pEList->nExpr, pDest);
	}

	/* Generate code that prevent returning multiple rows. */
	if ((p->selFlags & SF_SingleRow) != 0 && p->iLimit != 0)
		vdbe_code_raise_on_multiple_rows(pParse, p->iLimit, iEnd);
	/* Jump here to skip this query. */
	sqlVdbeResolveLabel(v, iEnd);

	/* The SELECT has been coded. If there is an error in the Parse structure,
	 * set the return code to 1. Otherwise 0.
	 */
	rc = (pParse->is_aborted);

	/* Control jumps to here if an error is encountered above, or upon
	 * successful coding of the SELECT.
	 */
 select_end:
	pParse->iSelectId = iRestoreSelectId;

	/* Identify column names if results of the SELECT are to be output.
	 */
	if (rc == 0 && pDest->eDest == SRT_Output) {
		generate_column_metadata(pParse, pTabList, pEList);
	}

	sqlDbFree(db, sAggInfo.aCol);
	sqlDbFree(db, sAggInfo.aFunc);
#ifdef SQL_DEBUG
	SELECTTRACE(1, pParse, p, ("end processing\n"));
	pParse->nSelectIndent--;
#endif
	return rc;
}

void
sql_expr_extract_select(struct Parse *parser, struct Select *select)
{
	struct ExprList *expr_list = select->pEList;
	assert(expr_list->nExpr == 1);
	parser->parsed_ast_type = AST_TYPE_EXPR;
	/*
	 * Extract a copy of parsed expression.
	 * We cannot use EXPRDUP_REDUCE flag in sqlExprDup call
	 * because some compiled Expr (like Checks expressions)
	 * may require further resolve with sqlResolveExprNames.
	 */
	parser->parsed_ast.expr =
		sqlExprDup(parser->db, expr_list->a->pExpr, 0);
}
