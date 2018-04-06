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
 * This file contains code associated with the ANALYZE command.
 *
 * The ANALYZE command gather statistics about the content of tables
 * and indices.  These statistics are made available to the query planner
 * to help it make better decisions about how to perform queries.
 *
 * The following system tables are or have been supported:
 *
 *    CREATE TABLE _sql_stat1(tbl, idx, stat);
 *    CREATE TABLE _sql_stat4(tbl, idx, nEq, nLt, nDLt, sample);
 *
 * For most applications, _sql_stat1 provides all the statistics required
 * for the query planner to make good choices.
 *
 * Format of _sql_stat1:
 *
 * There is normally one row per index, with the index identified by the
 * name in the idx column.  The tbl column is the name of the table to
 * which the index belongs.  In each such row, the stat column will be
 * a string consisting of a list of integers.  The first integer in this
 * list is the number of rows in the index.  (This is the same as the
 * number of rows in the table, except for partial indices.)  The second
 * integer is the average number of rows in the index that have the same
 * value in the first column of the index.  The third integer is the average
 * number of rows in the index that have the same value for the first two
 * columns.  The N-th integer (for N>1) is the average number of rows in
 * the index which have the same value for the first N-1 columns.  For
 * a K-column index, there will be K+1 integers in the stat column.  If
 * the index is unique, then the last integer will be 1.
 *
 * The list of integers in the stat column can optionally be followed
 * by the keyword "unordered".  The "unordered" keyword, if it is present,
 * must be separated from the last integer by a single space.  If the
 * "unordered" keyword is present, then the query planner assumes that
 * the index is unordered and will not use the index for a range query.
 *
 * If the _sql_stat1.idx column is NULL, then the _sql_stat1.stat
 * column contains a single integer which is the (estimated) number of
 * rows in the table identified by _sql_stat1.tbl.
 *
 * Format for _sql_stat4:
 *
 * The _sql_stat4 table contains histogram data
 * to aid the query planner in choosing good indices based on the values
 * that indexed columns are compared against in the WHERE clauses of
 * queries.
 *
 * The _sql_stat4 table contains multiple entries for each index.
 * The idx column names the index and the tbl column is the table of the
 * index.  If the idx and tbl columns are the same, then the sample is
 * of the INTEGER PRIMARY KEY.  The sample column is a blob which is the
 * binary encoding of a key from the index.  The nEq column is a
 * list of integers.  The first integer is the approximate number
 * of entries in the index whose left-most column exactly matches
 * the left-most column of the sample.  The second integer in nEq
 * is the approximate number of entries in the index where the
 * first two columns match the first two columns of the sample.
 * And so forth.  nLt is another list of integers that show the approximate
 * number of entries that are strictly less than the sample.  The first
 * integer in nLt contains the number of entries in the index where the
 * left-most column is less than the left-most column of the sample.
 * The K-th integer in the nLt entry is the number of index entries
 * where the first K columns are less than the first K columns of the
 * sample.  The nDLt column is like nLt except that it contains the
 * number of distinct entries in the index that are less than the
 * sample.
 *
 * There can be an arbitrary number of _sql_stat4 entries per index.
 * The ANALYZE command will typically generate _sql_stat4 tables
 * that contain between 10 and 40 samples which are distributed across
 * the key space, though not uniformly, and which include samples with
 * large nEq values.
 *
 */
#ifndef SQLITE_OMIT_ANALYZE

#include "box/index.h"
#include "box/key_def.h"
#include "box/tuple_compare.h"
#include "box/schema.h"
#include "third_party/qsort_arg.h"

#include "sqliteInt.h"
#include "tarantoolInt.h"
#include "vdbeInt.h"

/*
 * This routine generates code that opens the sql_statN tables.
 * The _sql_stat1 table is always relevant. _sql_stat4 is only opened when
 * appropriate compile-time options are provided.
 *
 * If the sql_statN tables do not previously exist, it is created.
 *
 * Argument zWhere may be a pointer to a buffer containing a table name,
 * or it may be a NULL pointer. If it is not NULL, then all entries in
 * the sql_statN tables associated with the named table are deleted.
 * If zWhere==0, then code is generated to delete all stat table entries.
 */
static void
openStatTable(Parse * pParse,	/* Parsing context */
	      int iStatCur,	/* Open the _sql_stat1 table on this cursor */
	      const char *zWhere,	/* Delete entries for this table or index */
	      const char *zWhereType	/* Either "tbl" or "idx" */
    )
{
	const char *aTable[] = {
		"_sql_stat1",
		"_sql_stat4",
		NULL};
	int i;
	sqlite3 *db = pParse->db;
	Vdbe *v = sqlite3GetVdbe(pParse);
	int aRoot[ArraySize(aTable)];
	u8 aCreateTbl[ArraySize(aTable)];

	if (v == 0)
		return;
	assert(sqlite3VdbeDb(v) == db);

	/* Create new statistic tables if they do not exist, or clear them
	 * if they do already exist.
	 */
	for (i = 0; aTable[i]; i++) {
		const char *zTab = aTable[i];
		Table *pStat;
		/* The table already exists, because it is a system space */
		pStat = sqlite3HashFind(&db->pSchema->tblHash, zTab);
		assert(pStat != NULL);
		aRoot[i] = pStat->tnum;
		aCreateTbl[i] = 0;
		if (zWhere) {
			sqlite3NestedParse(pParse,
					   "DELETE FROM \"%s\" WHERE \"%s\"=%Q",
					   zTab, zWhereType, zWhere);
		} else {
			/*
			 * The sql_stat[134] table already exists.
			 * Delete all rows.
			 */
			sqlite3VdbeAddOp2(v, OP_Clear, aRoot[i], 0);
		}
	}

	/* Open the sql_stat[134] tables for writing. */
	for (i = 0; aTable[i]; i++) {
		int addr = emit_open_cursor(pParse, iStatCur + i, aRoot[i]);
		v->aOp[addr].p4.pKeyInfo = 0;
		v->aOp[addr].p4type = P4_KEYINFO;
		sqlite3VdbeChangeP5(v, aCreateTbl[i]);
		VdbeComment((v, aTable[i]));
	}
}

/*
 * Recommended number of samples for _sql_stat4
 */
#ifndef SQL_STAT4_SAMPLES
#define SQL_STAT4_SAMPLES 24
#endif

/*
 * Three SQL functions - stat_init(), stat_push(), and stat_get() -
 * share an instance of the following structure to hold their state
 * information.
 */
typedef struct Stat4Accum Stat4Accum;
typedef struct Stat4Sample Stat4Sample;
struct Stat4Sample {
	tRowcnt *anEq;		/* _sql_stat4.nEq */
	tRowcnt *anDLt;		/* _sql_stat4.nDLt */
	tRowcnt *anLt;		/* _sql_stat4.nLt */
	u8 *aKey;		/* Table key */
	u32 nKey;		/* Sizeof aKey[] */
	u8 isPSample;		/* True if a periodic sample */
	int iCol;		/* If !isPSample, the reason for inclusion */
	u32 iHash;		/* Tiebreaker hash */
};
struct Stat4Accum {
	tRowcnt nRow;		/* Number of rows in the entire table */
	tRowcnt nPSample;	/* How often to do a periodic sample */
	int nCol;		/* Number of columns in index + pk */
	int nKeyCol;		/* Number of index columns w/o the pk */
	int mxSample;		/* Maximum number of samples to accumulate */
	Stat4Sample current;	/* Current row as a Stat4Sample */
	u32 iPrn;		/* Pseudo-random number used for sampling */
	Stat4Sample *aBest;	/* Array of nCol best samples */
	int iMin;		/* Index in a[] of entry with minimum score */
	int nSample;		/* Current number of samples */
	int iGet;		/* Index of current sample accessed by stat_get() */
	Stat4Sample *a;		/* Array of mxSample Stat4Sample objects */
	sqlite3 *db;		/* Database connection, for malloc() */
};

/* Reclaim memory used by a Stat4Sample
 */
static void
sampleClear(sqlite3 * db, Stat4Sample * p)
{
	assert(db != 0);
	if (p->nKey) {
		sqlite3DbFree(db, p->aKey);
		p->nKey = 0;
	}
}

/* Initialize the BLOB value of a sample key.
 */
static void
sampleSetKey(sqlite3 * db, Stat4Sample * p, int n, const u8 * pData)
{
	assert(db != 0);
	if (p->nKey)
		sqlite3DbFree(db, p->aKey);
	p->aKey = sqlite3DbMallocRawNN(db, n);
	if (p->aKey) {
		p->nKey = n;
		memcpy(p->aKey, pData, n);
	} else {
		p->nKey = 0;
	}
}

/*
 * Copy the contents of object (*pFrom) into (*pTo).
 */
static void
sampleCopy(Stat4Accum * p, Stat4Sample * pTo, Stat4Sample * pFrom)
{
	pTo->isPSample = pFrom->isPSample;
	pTo->iCol = pFrom->iCol;
	pTo->iHash = pFrom->iHash;
	memcpy(pTo->anEq, pFrom->anEq, sizeof(tRowcnt) * (p->nCol+1));
	memcpy(pTo->anLt, pFrom->anLt, sizeof(tRowcnt) * (p->nCol+1));
	memcpy(pTo->anDLt, pFrom->anDLt, sizeof(tRowcnt) * (p->nCol+1));
	sampleSetKey(p->db, pTo, pFrom->nKey, pFrom->aKey);
}

/*
 * Reclaim all memory of a Stat4Accum structure.
 */
static void
stat4Destructor(void *pOld)
{
	Stat4Accum *p = (Stat4Accum *) pOld;
	int i;
	for (i = 0; i < p->nCol; i++)
		sampleClear(p->db, p->aBest + i);
	for (i = 0; i < p->mxSample; i++)
		sampleClear(p->db, p->a + i);
	sampleClear(p->db, &p->current);
	sqlite3DbFree(p->db, p);
}

/*
 * Implementation of the stat_init(N,K,C) SQL function. The three parameters
 * are:
 *     N:    The number of columns in the index including the pk (note 1)
 *     K:    The number of columns in the index excluding the pk.
 *     C:    The number of rows in the index (note 2)
 *
 * Note 1:  In the special case of the covering index, N is the number of
 * PRIMARY KEY columns, not the total number of columns in the table.
 *
 * Note 2:  C is only used for STAT4.
 *
 * N=K+P where P is the number of columns in the
 * PRIMARY KEY of the table.  The covering index as N==K as a special case.
 *
 * This routine allocates the Stat4Accum object in heap memory. The return
 * value is a pointer to the Stat4Accum object.  The datatype of the
 * return value is BLOB, but it is really just a pointer to the Stat4Accum
 * object.
 */
static void
statInit(sqlite3_context * context, int argc, sqlite3_value ** argv)
{
	Stat4Accum *p;
	int nCol;		/* Number of columns in index being sampled */
	int nKeyCol;		/* Number of key columns */
	int nColUp;		/* nCol rounded up for alignment */
	int n;			/* Bytes of space to allocate */
	sqlite3 *db;		/* Database connection */
	int mxSample = SQL_STAT4_SAMPLES;

	/* Decode the three function arguments */
	UNUSED_PARAMETER(argc);
	nCol = sqlite3_value_int(argv[0]);
	assert(nCol > 0);
	/* Tarantool: we use an additional artificial column for the reason
	 * that Tarantool's indexes don't contain PK columns after key columns.
	 * Hence, in order to correctly gather statistics when dealing with 
	 * identical rows, we have to use this artificial column.
	 */
	nColUp = sizeof(tRowcnt) < 8 ? (nCol + 2) & ~1 : nCol + 1;
	nKeyCol = sqlite3_value_int(argv[1]);
	assert(nKeyCol <= nCol);
	assert(nKeyCol > 0);

	/* Allocate the space required for the Stat4Accum object */
	n = sizeof(*p)
	    + sizeof(tRowcnt) * nColUp	/* Stat4Accum.anEq */
	    + sizeof(tRowcnt) * nColUp	/* Stat4Accum.anDLt */
	    + sizeof(tRowcnt) * nColUp	/* Stat4Accum.anLt */
	    + sizeof(Stat4Sample) * (nCol + 1 + mxSample)	/* Stat4Accum.aBest[], a[] */
	    + sizeof(tRowcnt) * 3 * nColUp * (nCol + 1 + mxSample);
	db = sqlite3_context_db_handle(context);
	p = sqlite3DbMallocZero(db, n);
	if (p == 0) {
		sqlite3_result_error_nomem(context);
		return;
	}

	p->db = db;
	p->nRow = 0;
	p->nCol = nCol;
	p->nKeyCol = nKeyCol;
	p->current.anDLt = (tRowcnt *) & p[1];
	p->current.anEq = &p->current.anDLt[nColUp];

	{
		u8 *pSpace;	/* Allocated space not yet assigned */
		int i;		/* Used to iterate through p->aSample[] */

		p->iGet = -1;
		p->mxSample = mxSample;
		p->nPSample =
		    (tRowcnt) (sqlite3_value_int64(argv[2]) /
			       (mxSample / 3 + 1) + 1);
		p->current.anLt = &p->current.anEq[nColUp];
		p->iPrn =
		    0x689e962d * (u32) nCol ^ 0xd0944565 *
		    (u32) sqlite3_value_int(argv[2]);

		/* Set up the Stat4Accum.a[] and aBest[] arrays */
		p->a = (struct Stat4Sample *)&p->current.anLt[nColUp];
		p->aBest = &p->a[mxSample];
		pSpace = (u8 *) (&p->a[mxSample + nCol + 1]);
		for (i = 0; i < (mxSample + nCol + 1); i++) {
			p->a[i].anEq = (tRowcnt *) pSpace;
			pSpace += (sizeof(tRowcnt) * nColUp);
			p->a[i].anLt = (tRowcnt *) pSpace;
			pSpace += (sizeof(tRowcnt) * nColUp);
			p->a[i].anDLt = (tRowcnt *) pSpace;
			pSpace += (sizeof(tRowcnt) * nColUp);
		}
		assert((pSpace - (u8 *) p) == n);

		for (i = 0; i < nCol + 1; i++) {
			p->aBest[i].iCol = i;
		}
	}

	/* Return a pointer to the allocated object to the caller.  Note that
	 * only the pointer (the 2nd parameter) matters.  The size of the object
	 * (given by the 3rd parameter) is never used and can be any positive
	 * value.
	 */
	sqlite3_result_blob(context, p, sizeof(*p), stat4Destructor);
}

static const FuncDef statInitFuncdef = {
	3,			/* nArg */
	0,			/* funcFlags */
	0,			/* pUserData */
	0,			/* pNext */
	statInit,		/* xSFunc */
	0,			/* xFinalize */
	"stat_init",		/* zName */
	{0}
};

/*
 * pNew and pOld are both candidate non-periodic samples selected for
 * the same column (pNew->iCol==pOld->iCol). Ignoring this column and
 * considering only any trailing columns and the sample hash value, this
 * function returns true if sample pNew is to be preferred over pOld.
 * In other words, if we assume that the cardinalities of the selected
 * column for pNew and pOld are equal, is pNew to be preferred over pOld.
 *
 * This function assumes that for each argument sample, the contents of
 * the anEq[] array from pSample->anEq[pSample->iCol+1] onwards are valid.
 */
static int
sampleIsBetterPost(Stat4Accum * pAccum, Stat4Sample * pNew, Stat4Sample * pOld)
{
	int nCol = pAccum->nCol;
	int i;
	assert(pNew->iCol == pOld->iCol);
	for (i = pNew->iCol + 1; i < nCol + 1; i++) {
		if (pNew->anEq[i] > pOld->anEq[i])
			return 1;
		if (pNew->anEq[i] < pOld->anEq[i])
			return 0;
	}
	if (pNew->iHash > pOld->iHash)
		return 1;
	return 0;
}

/*
 * Return true if pNew is to be preferred over pOld.
 *
 * This function assumes that for each argument sample, the contents of
 * the anEq[] array from pSample->anEq[pSample->iCol] onwards are valid.
 */
static int
sampleIsBetter(Stat4Accum * pAccum, Stat4Sample * pNew, Stat4Sample * pOld)
{
	tRowcnt nEqNew = pNew->anEq[pNew->iCol];
	tRowcnt nEqOld = pOld->anEq[pOld->iCol];

	assert(pOld->isPSample == 0 && pNew->isPSample == 0);

	if ((nEqNew > nEqOld))
		return 1;
	if (nEqNew == nEqOld) {
		if (pNew->iCol < pOld->iCol)
			return 1;
		return (pNew->iCol == pOld->iCol
			&& sampleIsBetterPost(pAccum, pNew, pOld));
	}
	return 0;
}

/*
 * Copy the contents of sample *pNew into the p->a[] array. If necessary,
 * remove the least desirable sample from p->a[] to make room.
 */
static void
sampleInsert(Stat4Accum * p, Stat4Sample * pNew, int nEqZero)
{
	Stat4Sample *pSample = 0;
	int i;

	if (pNew->isPSample == 0) {
		Stat4Sample *pUpgrade = 0;
		assert(pNew->anEq[pNew->iCol] > 0);

		/* This sample is being added because the prefix that ends in column
		 * iCol occurs many times in the table. However, if we have already
		 * added a sample that shares this prefix, there is no need to add
		 * this one. Instead, upgrade the priority of the highest priority
		 * existing sample that shares this prefix.
		 */
		for (i = p->nSample - 1; i >= 0; i--) {
			Stat4Sample *pOld = &p->a[i];
			if (pOld->anEq[pNew->iCol] == 0) {
				if (pOld->isPSample)
					return;
				assert(pOld->iCol > pNew->iCol);
				assert(sampleIsBetter(p, pNew, pOld));
				if (pUpgrade == 0
				    || sampleIsBetter(p, pOld, pUpgrade)) {
					pUpgrade = pOld;
				}
			}
		}
		if (pUpgrade) {
			pUpgrade->iCol = pNew->iCol;
			pUpgrade->anEq[pUpgrade->iCol] =
			    pNew->anEq[pUpgrade->iCol];
			goto find_new_min;
		}
	}

	/* If necessary, remove sample iMin to make room for the new sample. */
	if (p->nSample >= p->mxSample) {
		Stat4Sample *pMin = &p->a[p->iMin];
		tRowcnt *anEq = pMin->anEq;
		tRowcnt *anLt = pMin->anLt;
		tRowcnt *anDLt = pMin->anDLt;
		sampleClear(p->db, pMin);
		memmove(pMin, &pMin[1],
			sizeof(p->a[0]) * (p->nSample - p->iMin - 1));
		pSample = &p->a[p->nSample - 1];
		pSample->nKey = 0;
		pSample->anEq = anEq;
		pSample->anDLt = anDLt;
		pSample->anLt = anLt;
		p->nSample = p->mxSample - 1;
	}

	assert(p->nSample==0 || pNew->anLt[p->nCol] > p->a[p->nSample-1].anLt[p->nCol]);

	/* Insert the new sample */
	pSample = &p->a[p->nSample];
	sampleCopy(p, pSample, pNew);
	p->nSample++;

	/* Zero the first nEqZero entries in the anEq[] array. */
	memset(pSample->anEq, 0, sizeof(tRowcnt) * nEqZero);

 find_new_min:
	if (p->nSample >= p->mxSample) {
		int iMin = -1;
		for (i = 0; i < p->mxSample; i++) {
			if (p->a[i].isPSample)
				continue;
			if (iMin < 0
			    || sampleIsBetter(p, &p->a[iMin], &p->a[i])) {
				iMin = i;
			}
		}
		assert(iMin >= 0);
		p->iMin = iMin;
	}
}

/*
 * Field iChng of the index being scanned has changed. So at this point
 * p->current contains a sample that reflects the previous row of the
 * index. The value of anEq[iChng] and subsequent anEq[] elements are
 * correct at this point.
 */

static void
samplePushPrevious(Stat4Accum * p, int iChng)
{
	int i;
	/* Check if any samples from the aBest[] array should be pushed
	 * into IndexSample.a[] at this point.
	 */
	for (i = (p->nCol - 1); i >= iChng; i--) {
		Stat4Sample *pBest = &p->aBest[i];
		pBest->anEq[i] = p->current.anEq[i];
		if (p->nSample < p->mxSample
		    || sampleIsBetter(p, pBest, &p->a[p->iMin])) {
			sampleInsert(p, pBest, i);
		}
	}

	/* Update the anEq[] fields of any samples already collected. */
	for (i = p->nSample - 1; i >= 0; i--) {
		int j;
		for (j = iChng; j < p->nCol + 1; j++) {
			if (p->a[i].anEq[j] == 0)
				p->a[i].anEq[j] = p->current.anEq[j];
		}
	}
}

/*
 * Implementation of the stat_push SQL function:  stat_push(P,C,R)
 * Arguments:
 *
 *    P     Pointer to the Stat4Accum object created by stat_init()
 *    C     Index of left-most column to differ from previous row
 *    R     Key record for the current row
 *
 * This SQL function always returns NULL.  It's purpose it to accumulate
 * statistical data and/or samples in the Stat4Accum object about the
 * index being analyzed.  The stat_get() SQL function will later be used to
 * extract relevant information for constructing the sql_statN tables.
 *
 * The R parameter is only used for STAT4
 */
static void
statPush(sqlite3_context * context, int argc, sqlite3_value ** argv)
{
	int i;
	/* The three function arguments */
	Stat4Accum *p = (Stat4Accum *) sqlite3_value_blob(argv[0]);
	int iChng = sqlite3_value_int(argv[1]);

	UNUSED_PARAMETER(argc);
	UNUSED_PARAMETER(context);
	assert(p->nCol > 0);
	/* iChng == p->nCol means that the current and previous rows are identical */
	assert(iChng <= p->nCol);
	if (p->nRow == 0) {
		/* This is the first call to this function. Do initialization. */
		for (i = 0; i < p->nCol + 1; i++)
			p->current.anEq[i] = 1;
	} else {
		/* Second and subsequent calls get processed here */
		samplePushPrevious(p, iChng);

		/* Update anDLt[], anLt[] and anEq[] to reflect the values that apply
		 * to the current row of the index.
		 */
		for (i = 0; i < iChng; i++) {
			p->current.anEq[i]++;
		}
		for (i = iChng; i < p->nCol + 1; i++) {
			p->current.anDLt[i]++;
			p->current.anLt[i] += p->current.anEq[i];
			p->current.anEq[i] = 1;
		}
	}
	p->nRow++;
	sampleSetKey(p->db, &p->current, sqlite3_value_bytes(argv[2]),
		     sqlite3_value_blob(argv[2]));
	p->current.iHash = p->iPrn = p->iPrn * 1103515245 + 12345;
	{
		tRowcnt nLt = p->current.anLt[p->nCol];

		/* Check if this is to be a periodic sample. If so, add it. */
		if ((nLt / p->nPSample) != (nLt + 1) / p->nPSample) {
			p->current.isPSample = 1;
			p->current.iCol = 0;
			sampleInsert(p, &p->current, p->nCol);
			p->current.isPSample = 0;
		}
		/* Update the aBest[] array. */
		for (i = 0; i < p->nCol; i++) {
			p->current.iCol = i;
			if (i >= iChng
			    || sampleIsBetterPost(p, &p->current,
						  &p->aBest[i])) {
				sampleCopy(p, &p->aBest[i], &p->current);
			}
		}
	}
}

static const FuncDef statPushFuncdef = {
	3,			/* nArg */
	0,			/* funcFlags */
	0,			/* pUserData */
	0,			/* pNext */
	statPush,		/* xSFunc */
	0,			/* xFinalize */
	"stat_push",		/* zName */
	{0}
};

#define STAT_GET_STAT1 0	/* "stat" column of stat1 table */
#define STAT_GET_KEY   1	/* "key" column of stat4 entry */
#define STAT_GET_NEQ   2	/* "neq" column of stat4 entry */
#define STAT_GET_NLT   3	/* "nlt" column of stat4 entry */
#define STAT_GET_NDLT  4	/* "ndlt" column of stat4 entry */

/*
 * Implementation of the stat_get(P,J) SQL function.  This routine is
 * used to query statistical information that has been gathered into
 * the Stat4Accum object by prior calls to stat_push().  The P parameter
 * has type BLOB but it is really just a pointer to the Stat4Accum object.
 * The content to returned is determined by the parameter J
 * which is one of the STAT_GET_xxxx values defined above.
 */
static void
statGet(sqlite3_context * context, int argc, sqlite3_value ** argv)
{
	Stat4Accum *p = (Stat4Accum *) sqlite3_value_blob(argv[0]);
	/* STAT4 have a parameter on this routine. */
	int eCall = sqlite3_value_int(argv[1]);
	assert(argc == 2);
	assert(eCall == STAT_GET_STAT1 || eCall == STAT_GET_NEQ
	       || eCall == STAT_GET_KEY || eCall == STAT_GET_NLT
	       || eCall == STAT_GET_NDLT);
	if (eCall == STAT_GET_STAT1) {
		/* Return the value to store in the "stat" column of the _sql_stat1
		 * table for this index.
		 *
		 * The value is a string composed of a list of integers describing
		 * the index. The first integer in the list is the total number of
		 * entries in the index. There is one additional integer in the list
		 * for each indexed column. This additional integer is an estimate of
		 * the number of rows matched by a stabbing query on the index using
		 * a key with the corresponding number of fields. In other words,
		 * if the index is on columns (a,b) and the _sql_stat1 value is
		 * "100 10 2", then SQLite estimates that:
		 *
		 *   * the index contains 100 rows,
		 *   * "WHERE a=?" matches 10 rows, and
		 *   * "WHERE a=? AND b=?" matches 2 rows.
		 *
		 * If D is the count of distinct values and K is the total number of
		 * rows, then each estimate is computed as:
		 *
		 *        I = (K+D-1)/D
		 */
		char *z;
		int i;

		char *zRet = sqlite3MallocZero((p->nKeyCol + 1) * 25);
		if (zRet == 0) {
			sqlite3_result_error_nomem(context);
			return;
		}

		sqlite3_snprintf(24, zRet, "%llu", (u64) p->nRow);
		z = zRet + sqlite3Strlen30(zRet);
		for (i = 0; i < p->nKeyCol; i++) {
			u64 nDistinct = p->current.anDLt[i] + 1;
			u64 iVal = (p->nRow + nDistinct - 1) / nDistinct;
			sqlite3_snprintf(24, z, " %llu", iVal);
			z += sqlite3Strlen30(z);
			assert(p->current.anEq[i]);
		}
		assert(z[0] == '\0' && z > zRet);

		sqlite3_result_text(context, zRet, -1, sqlite3_free);
	} else if (eCall == STAT_GET_KEY) {
		if (p->iGet < 0) {
			samplePushPrevious(p, 0);
			p->iGet = 0;
		}
		if (p->iGet < p->nSample) {
			Stat4Sample *pS = p->a + p->iGet;
			sqlite3_result_blob(context, pS->aKey, pS->nKey,
					    SQLITE_TRANSIENT);
		}
	} else {
		tRowcnt *aCnt = 0;

		assert(p->iGet < p->nSample);
		switch (eCall) {
		case STAT_GET_NEQ:
			aCnt = p->a[p->iGet].anEq;
			break;
		case STAT_GET_NLT:
			aCnt = p->a[p->iGet].anLt;
			break;
		default:{
			aCnt = p->a[p->iGet].anDLt;
			p->iGet++;
			break;
		}
	}

	char *zRet = sqlite3MallocZero(p->nCol * 25);
	if (zRet == 0) {
		sqlite3_result_error_nomem(context);
	} else {
		int i;
		char *z = zRet;
		for (i = 0; i < p->nCol; i++) {
			sqlite3_snprintf(24, z, "%llu ", (u64) aCnt[i]);
			z += sqlite3Strlen30(z);
		}
		assert(z[0] == '\0' && z > zRet);
		z[-1] = '\0';
		sqlite3_result_text(context, zRet, -1, sqlite3_free);
	}

}
#ifndef SQLITE_DEBUG
UNUSED_PARAMETER(argc);
#endif
}

static const FuncDef statGetFuncdef = {
	2,			/* nArg */
	0,			/* funcFlags */
	0,			/* pUserData */
	0,			/* pNext */
	statGet,		/* xSFunc */
	0,			/* xFinalize */
	"stat_get",		/* zName */
	{0}
};

static void
callStatGet(Vdbe * v, int regStat4, int iParam, int regOut)
{
	assert(regOut != regStat4 && regOut != regStat4 + 1);
	sqlite3VdbeAddOp2(v, OP_Integer, iParam, regStat4 + 1);
	sqlite3VdbeAddOp4(v, OP_Function0, 0, regStat4, regOut,
			  (char *)&statGetFuncdef, P4_FUNCDEF);
	sqlite3VdbeChangeP5(v, 2);
}

/*
 * Generate code to do an analysis of all indices associated with
 * a single table.
 */
static void
analyzeOneTable(Parse * pParse,	/* Parser context */
		Table * pTab,	/* Table whose indices are to be analyzed */
		Index * pOnlyIdx,	/* If not NULL, only analyze this one index */
		int iStatCur,	/* Index of VdbeCursor that writes the _sql_stat1 table */
		int iMem,	/* Available memory locations begin here */
		int iTab	/* Next available cursor */
    )
{
	sqlite3 *db = pParse->db;	/* Database handle */
	Index *pIdx;		/* An index to being analyzed */
	int iIdxCur;		/* Cursor open on index being analyzed */
	int iTabCur;		/* Table cursor */
	Vdbe *v;		/* The virtual machine being built up */
	int i;			/* Loop counter */
	int space_ptr_reg = iMem++;
	int regStat4 = iMem++;	/* Register to hold Stat4Accum object */
	int regChng = iMem++;	/* Index of changed index field */
	int regKey = iMem++;	/* Key argument passed to stat_push() */
	int regTemp = iMem++;	/* Temporary use register */
	int regTabname = iMem++;	/* Register containing table name */
	int regIdxname = iMem++;	/* Register containing index name */
	int regStat1 = iMem++;	/* Value for the stat column of _sql_stat1 */
	int regPrev = iMem;	/* MUST BE LAST (see below) */

	pParse->nMem = MAX(pParse->nMem, iMem);
	v = sqlite3GetVdbe(pParse);
	if (v == 0 || NEVER(pTab == 0)) {
		return;
	}
	assert(pTab->tnum != 0);
	if (sqlite3_strlike("\\_%", pTab->zName, '\\') == 0) {
		/* Do not gather statistics on system tables */
		return;
	}

	/* Establish a read-lock on the table at the shared-cache level.
	 * Open a read-only cursor on the table. Also allocate a cursor number
	 * to use for scanning indexes (iIdxCur). No index cursor is opened at
	 * this time though.
	 */
	iTabCur = iTab++;
	iIdxCur = iTab++;
	pParse->nTab = MAX(pParse->nTab, iTab);
	sqlite3OpenTable(pParse, iTabCur, pTab, OP_OpenRead);
	sqlite3VdbeLoadString(v, regTabname, pTab->zName);

	for (pIdx = pTab->pIndex; pIdx; pIdx = pIdx->pNext) {
		int addrRewind;	/* Address of "OP_Rewind iIdxCur" */
		int addrNextRow;	/* Address of "next_row:" */
		const char *zIdxName;	/* Name of the index */
		int nColTest;	/* Number of columns to test for changes */

		if (pOnlyIdx && pOnlyIdx != pIdx)
			continue;
		/* Primary indexes feature automatically generated
		 * names. Thus, for the sake of clarity, use
		 * instead more familiar table name.
		 */
		if (IsPrimaryKeyIndex(pIdx)) {
			zIdxName = pTab->zName;
		} else {
			zIdxName = pIdx->zName;
		}
		nColTest = index_column_count(pIdx);

		/* Populate the register containing the index name. */
		sqlite3VdbeLoadString(v, regIdxname, zIdxName);
		VdbeComment((v, "Analysis for %s.%s", pTab->zName, zIdxName));

		/*
		 * Pseudo-code for loop that calls stat_push():
		 *
		 *   Rewind csr
		 *   if eof(csr) goto end_of_scan;
		 *   regChng = 0
		 *   goto chng_addr_0;
		 *
		 *  next_row:
		 *   regChng = 0
		 *   if( idx(0) != regPrev(0) ) goto chng_addr_0
		 *   regChng = 1
		 *   if( idx(1) != regPrev(1) ) goto chng_addr_1
		 *   ...
		 *   regChng = N
		 *   goto chng_addr_N
		 *
		 *  chng_addr_0:
		 *   regPrev(0) = idx(0)
		 *  chng_addr_1:
		 *   regPrev(1) = idx(1)
		 *  ...
		 *
		 *  endDistinctTest:
		 *   regKey = idx(key)
		 *   stat_push(P, regChng, regKey)
		 *   Next csr
		 *   if !eof(csr) goto next_row;
		 *
		 *  end_of_scan:
		 */

		/* Make sure there are enough memory cells allocated to accommodate
		 * the regPrev array and a trailing key (the key slot is required
		 * when building a record to insert into the sample column of
		 * the _sql_stat4 table).
		 */
		pParse->nMem = MAX(pParse->nMem, regPrev + nColTest);

		/* Open a read-only cursor on the index being analyzed. */
		struct space *space =
			space_by_id(SQLITE_PAGENO_TO_SPACEID(pIdx->tnum));
		assert(space != NULL);
		sqlite3VdbeAddOp4Ptr(v, OP_LoadPtr, 0, space_ptr_reg, 0,
				     (void *) space);
		sqlite3VdbeAddOp3(v, OP_OpenRead, iIdxCur, pIdx->tnum,
				  space_ptr_reg);
		sqlite3VdbeSetP4KeyInfo(pParse, pIdx);
		VdbeComment((v, "%s", pIdx->zName));

		/* Invoke the stat_init() function. The arguments are:
		 *
		 *    (1) the number of columns in the index
		 *        (including the number of PK columns)
		 *    (2) the number of columns in the key without the pk
		 *    (3) the number of rows in the index
		 *
		 *
		 * The third argument is only used for STAT4
		 */
		sqlite3VdbeAddOp2(v, OP_Count, iIdxCur, regStat4 + 3);
		sqlite3VdbeAddOp2(v, OP_Integer, nColTest, regStat4 + 1);
		sqlite3VdbeAddOp2(v, OP_Integer, nColTest, regStat4 + 2);
		sqlite3VdbeAddOp4(v, OP_Function0, 0, regStat4 + 1, regStat4,
				  (char *)&statInitFuncdef, P4_FUNCDEF);
		sqlite3VdbeChangeP5(v, 3);

		/* Implementation of the following:
		 *
		 *   Rewind csr
		 *   if eof(csr) goto end_of_scan;
		 *   regChng = 0
		 *   goto next_push_0;
		 *
		 */
		addrRewind = sqlite3VdbeAddOp1(v, OP_Rewind, iIdxCur);
		VdbeCoverage(v);
		sqlite3VdbeAddOp2(v, OP_Integer, 0, regChng);
		addrNextRow = sqlite3VdbeCurrentAddr(v);

		if (nColTest > 0) {
			int endDistinctTest = sqlite3VdbeMakeLabel(v);
			int *aGotoChng;	/* Array of jump instruction addresses */
			aGotoChng =
			    sqlite3DbMallocRawNN(db, sizeof(int) * nColTest);
			if (aGotoChng == 0)
				continue;

			/*
			 *  next_row:
			 *   regChng = 0
			 *   if( idx(0) != regPrev(0) ) goto chng_addr_0
			 *   regChng = 1
			 *   if( idx(1) != regPrev(1) ) goto chng_addr_1
			 *   ...
			 *   regChng = N
			 *   goto endDistinctTest
			 */
			sqlite3VdbeAddOp0(v, OP_Goto);
			addrNextRow = sqlite3VdbeCurrentAddr(v);
			if (nColTest == 1 && index_is_unique(pIdx)) {
				/* For a single-column UNIQUE index, once we have found a non-NULL
				 * row, we know that all the rest will be distinct, so skip
				 * subsequent distinctness tests.
				 */
				sqlite3VdbeAddOp2(v, OP_NotNull, regPrev,
						  endDistinctTest);
				VdbeCoverage(v);
			}
			for (i = 0; i < nColTest; i++) {
				struct coll *coll = sql_index_collation(pIdx, i);
				sqlite3VdbeAddOp2(v, OP_Integer, i, regChng);
				sqlite3VdbeAddOp3(v, OP_Column, iIdxCur,
						  pIdx->aiColumn[i], regTemp);
				aGotoChng[i] =
				    sqlite3VdbeAddOp4(v, OP_Ne, regTemp, 0,
						      regPrev + i, (char *)coll,
						      P4_COLLSEQ);
				sqlite3VdbeChangeP5(v, SQLITE_NULLEQ);
				VdbeCoverage(v);
			}
			sqlite3VdbeAddOp2(v, OP_Integer, nColTest, regChng);
			sqlite3VdbeGoto(v, endDistinctTest);

			/*
			 *  chng_addr_0:
			 *   regPrev(0) = idx(0)
			 *  chng_addr_1:
			 *   regPrev(1) = idx(1)
			 *  ...
			 */
			sqlite3VdbeJumpHere(v, addrNextRow - 1);
			for (i = 0; i < nColTest; i++) {
				sqlite3VdbeJumpHere(v, aGotoChng[i]);
				sqlite3VdbeAddOp3(v, OP_Column, iIdxCur,
						  pIdx->aiColumn[i],
						  regPrev + i);
			}
			sqlite3VdbeResolveLabel(v, endDistinctTest);
			sqlite3DbFree(db, aGotoChng);
		}

		/*
		 *  chng_addr_N:
		 *   regKey = idx(key)              // STAT4 only
		 *   stat_push(P, regChng, regKey)  // 3rd parameter STAT4 only
		 *   Next csr
		 *   if !eof(csr) goto next_row;
		 */
		assert(regKey == (regStat4 + 2));
		Index *pPk = sqlite3PrimaryKeyIndex(pIdx->pTable);
		int j, k, regKeyStat;
		int nPkColumn = (int)index_column_count(pPk);
		regKeyStat = sqlite3GetTempRange(pParse, nPkColumn);
		for (j = 0; j < nPkColumn; j++) {
			k = pPk->aiColumn[j];
			assert(k >= 0 && k < pTab->nCol);
			sqlite3VdbeAddOp3(v, OP_Column, iIdxCur, k, regKeyStat + j);
			VdbeComment((v, "%s", pTab->aCol[pPk->aiColumn[j]].zName));
		}
		sqlite3VdbeAddOp3(v, OP_MakeRecord, regKeyStat,
				  nPkColumn, regKey);
		sqlite3ReleaseTempRange(pParse, regKeyStat, nPkColumn);

		assert(regChng == (regStat4 + 1));
		sqlite3VdbeAddOp4(v, OP_Function0, 1, regStat4, regTemp,
				  (char *)&statPushFuncdef, P4_FUNCDEF);
		sqlite3VdbeChangeP5(v, 3);
		sqlite3VdbeAddOp2(v, OP_Next, iIdxCur, addrNextRow);
		VdbeCoverage(v);

		/* Add the entry to the stat1 table. */
		callStatGet(v, regStat4, STAT_GET_STAT1, regStat1);
		assert("BBB"[0] == SQLITE_AFF_TEXT);
		sqlite3VdbeAddOp4(v, OP_MakeRecord, regTabname, 3, regTemp,
				  "BBB", 0);
		sqlite3VdbeAddOp2(v, OP_IdxInsert, iStatCur, regTemp);

		/* Add the entries to the stat4 table. */

		int regEq = regStat1;
		int regLt = regStat1 + 1;
		int regDLt = regStat1 + 2;
		int regSample = regStat1 + 3;
		int regCol = regStat1 + 4;
		int regSampleKey = regCol + nColTest;
		int addrNext;
		int addrIsNull;

		pParse->nMem = MAX(pParse->nMem, regCol + nColTest);

		addrNext = sqlite3VdbeCurrentAddr(v);
		callStatGet(v, regStat4, STAT_GET_KEY, regSampleKey);
		addrIsNull = sqlite3VdbeAddOp1(v, OP_IsNull, regSampleKey);
		VdbeCoverage(v);
		callStatGet(v, regStat4, STAT_GET_NEQ, regEq);
		callStatGet(v, regStat4, STAT_GET_NLT, regLt);
		callStatGet(v, regStat4, STAT_GET_NDLT, regDLt);
		sqlite3VdbeAddOp4Int(v, OP_NotFound, iTabCur, addrNext,
				     regSampleKey, 0);
		/* We know that the regSampleKey row exists because it was read by
		 * the previous loop.  Thus the not-found jump of seekOp will never
		 * be taken
		 */
		VdbeCoverageNeverTaken(v);
		for (i = 0; i < nColTest; i++) {
			sqlite3ExprCodeLoadIndexColumn(pParse, pIdx,
									 iTabCur, i,
									 regCol + i);
		}
		sqlite3VdbeAddOp3(v, OP_MakeRecord, regCol, nColTest,
				  regSample);
		sqlite3VdbeAddOp3(v, OP_MakeRecord, regTabname, 6, regTemp);
		sqlite3VdbeAddOp2(v, OP_IdxReplace, iStatCur + 1, regTemp);
		sqlite3VdbeAddOp2(v, OP_Goto, 1, addrNext);	/* P1==1 for end-of-loop */
		sqlite3VdbeJumpHere(v, addrIsNull);

		/* End of analysis */
		sqlite3VdbeJumpHere(v, addrRewind);
	}
}

/*
 * Generate code that will cause the most recent index analysis to
 * be loaded into internal hash tables where is can be used.
 */
static void
loadAnalysis(Parse * pParse)
{
	Vdbe *v = sqlite3GetVdbe(pParse);
	if (v) {
		sqlite3VdbeAddOp1(v, OP_LoadAnalysis, 0);
	}
}

/*
 * Generate code that will do an analysis of an entire database
 */
static void
analyzeDatabase(Parse * pParse)
{
	sqlite3 *db = pParse->db;
	Schema *pSchema = db->pSchema;	/* Schema of database */
	HashElem *k;
	int iStatCur;
	int iMem;
	int iTab;

	sql_set_multi_write(pParse, false);
	iStatCur = pParse->nTab;
	pParse->nTab += 3;
	openStatTable(pParse, iStatCur, 0, 0);
	iMem = pParse->nMem + 1;
	iTab = pParse->nTab;
	for (k = sqliteHashFirst(&pSchema->tblHash); k; k = sqliteHashNext(k)) {
		Table *pTab = (Table *) sqliteHashData(k);
		analyzeOneTable(pParse, pTab, 0, iStatCur, iMem, iTab);
	}
	loadAnalysis(pParse);
}

/*
 * Generate code that will do an analysis of a single table in
 * a database.  If pOnlyIdx is not NULL then it is a single index
 * in pTab that should be analyzed.
 */
static void
analyzeTable(Parse * pParse, Table * pTab, Index * pOnlyIdx)
{
	int iStatCur;

	assert(pTab != 0);
	sql_set_multi_write(pParse, false);
	iStatCur = pParse->nTab;
	pParse->nTab += 3;
	if (pOnlyIdx) {
		openStatTable(pParse, iStatCur, pOnlyIdx->zName, "idx");
	} else {
		openStatTable(pParse, iStatCur, pTab->zName, "tbl");
	}
	analyzeOneTable(pParse, pTab, pOnlyIdx, iStatCur, pParse->nMem + 1,
			pParse->nTab);
	loadAnalysis(pParse);
}

/*
 * Generate code for the ANALYZE command.  The parser calls this routine
 * when it recognizes an ANALYZE command.
 *
 *        ANALYZE                            -- 1
 *        ANALYZE  <tablename>               -- 2
 *
 * Form 1 analyzes all indices the single database named.
 * Form 2 analyzes all indices associated with the named table.
 */
void
sqlite3Analyze(Parse * pParse, Token * pName)
{
	sqlite3 *db = pParse->db;
	char *z;
	Table *pTab;
	Vdbe *v;

	assert(db->pSchema != NULL);

	if (pName == 0) {
		/* Form 1:  Analyze everything */
		analyzeDatabase(pParse);
	} else {
		/* Form 2:  Analyze table named */
		z = sqlite3NameFromToken(db, pName);
		if (z) {
			if ((pTab = sqlite3LocateTable(pParse, 0, z)) != 0) {
				analyzeTable(pParse, pTab, 0);
			}
		}
		sqlite3DbFree(db, z);
	}

	v = sqlite3GetVdbe(pParse);
	if (v)
		sqlite3VdbeAddOp0(v, OP_Expire);
}

/*
 * Used to pass information from the analyzer reader through to the
 * callback routine.
 */
typedef struct analysisInfo analysisInfo;
struct analysisInfo {
	sqlite3 *db;
};

/*
 * The first argument points to a nul-terminated string containing a
 * list of space separated integers. Read the first nOut of these into
 * the array aOut[].
 */
static void
decodeIntArray(char *zIntArray,	/* String containing int array to decode */
	       int nOut,	/* Number of slots in aOut[] */
	       tRowcnt * aOut,	/* Store integers here */
	       LogEst * aLog,	/* Or, if aOut==0, here */
	       Index * pIndex	/* Handle extra flags for this index, if not NULL */
    )
{
	char *z = zIntArray;
	int c;
	int i;
	tRowcnt v;

	if (z == 0)
		z = "";

	for (i = 0; *z && i < nOut; i++) {
		v = 0;
		while ((c = z[0]) >= '0' && c <= '9') {
			v = v * 10 + c - '0';
			z++;
		}
		if (aOut)
			aOut[i] = v;
		if (aLog)
			aLog[i] = sqlite3LogEst(v);
		if (*z == ' ')
			z++;
	}

	if (pIndex) {
		pIndex->bUnordered = 0;
		pIndex->noSkipScan = 0;
		while (z[0]) {
			if (sqlite3_strglob("unordered*", z) == 0) {
				pIndex->bUnordered = 1;
			} else if (sqlite3_strglob("sz=[0-9]*", z) == 0) {
				pIndex->szIdxRow =
				    sqlite3LogEst(sqlite3Atoi(z + 3));
			} else if (sqlite3_strglob("noskipscan*", z) == 0) {
				pIndex->noSkipScan = 1;
			}
#ifdef SQLITE_ENABLE_COSTMULT
			else if (sqlite3_strglob("costmult=[0-9]*", z) == 0) {
				pIndex->pTable->costMult =
				    sqlite3LogEst(sqlite3Atoi(z + 9));
			}
#endif
			while (z[0] != 0 && z[0] != ' ')
				z++;
			while (z[0] == ' ')
				z++;
		}
	}
}

/*
 * This callback is invoked once for each index when reading the
 * _sql_stat1 table.
 *
 *     argv[0] = name of the table
 *     argv[1] = name of the index (might be NULL)
 *     argv[2] = results of analysis - on integer for each column
 *
 * Entries for which argv[1]==NULL simply record the number of rows in
 * the table.
 */
static int
analysisLoader(void *pData, int argc, char **argv, char **NotUsed)
{
	analysisInfo *pInfo = (analysisInfo *) pData;
	Index *pIndex;
	Table *pTable;
	const char *z;

	assert(argc == 3);
	UNUSED_PARAMETER2(NotUsed, argc);

	if (argv == 0 || argv[0] == 0 || argv[2] == 0) {
		return 0;
	}
	pTable = sqlite3HashFind(&pInfo->db->pSchema->tblHash, argv[0]);
	if (pTable == NULL)
		return 0;
	if (argv[1] == 0) {
		pIndex = 0;
	} else if (sqlite3_stricmp(argv[0], argv[1]) == 0) {
		pIndex = sqlite3PrimaryKeyIndex(pTable);
	} else {
		pIndex = sqlite3HashFind(&pTable->idxHash, argv[1]);
	}
	z = argv[2];

	if (pIndex) {
		tRowcnt *aiRowEst = 0;
		int nCol = index_column_count(pIndex) + 1;
		/* Index.aiRowEst may already be set here if there are duplicate
		 * _sql_stat1 entries for this index. In that case just clobber
		 * the old data with the new instead of allocating a new array.
		 */
		if (pIndex->aiRowEst == 0) {
			pIndex->aiRowEst =
			    (tRowcnt *) sqlite3MallocZero(sizeof(tRowcnt) *
							  nCol);
			if (pIndex->aiRowEst == 0)
				sqlite3OomFault(pInfo->db);
		}
		aiRowEst = pIndex->aiRowEst;
		pIndex->bUnordered = 0;
		decodeIntArray((char *)z, nCol, aiRowEst, pIndex->aiRowLogEst,
			       pIndex);
		if (pIndex->pPartIdxWhere == 0)
			pTable->nRowLogEst = pIndex->aiRowLogEst[0];
	} else {
		Index fakeIdx;
		fakeIdx.szIdxRow = pTable->szTabRow;
#ifdef SQLITE_ENABLE_COSTMULT
		fakeIdx.pTable = pTable;
#endif
		decodeIntArray((char *)z, 1, 0, &pTable->nRowLogEst, &fakeIdx);
		pTable->szTabRow = fakeIdx.szIdxRow;
	}

	return 0;
}

/*
 * If the Index.aSample variable is not NULL, delete the aSample[] array
 * and its contents.
 */
void
sqlite3DeleteIndexSamples(sqlite3 * db, Index * pIdx)
{
	if (pIdx->aSample) {
		int j;
		for (j = 0; j < pIdx->nSample; j++) {
			IndexSample *p = &pIdx->aSample[j];
			sqlite3DbFree(db, p->p);
		}
		sqlite3DbFree(db, pIdx->aSample);
	}
	if (db && db->pnBytesFreed == 0) {
		pIdx->nSample = 0;
		pIdx->aSample = 0;
	}
}

/*
 * Populate the pIdx->aAvgEq[] array based on the samples currently
 * stored in pIdx->aSample[].
 */
static void
initAvgEq(Index * pIdx)
{
	if (pIdx) {
		IndexSample *aSample = pIdx->aSample;
		IndexSample *pFinal = &aSample[pIdx->nSample - 1];
		int iCol;
		int nCol = 1;
		if (pIdx->nSampleCol > 1) {
			/* If this is stat4 data, then calculate aAvgEq[] values for all
			 * sample columns except the last. The last is always set to 1, as
			 * once the trailing PK fields are considered all index keys are
			 * unique.
			 */
			nCol = pIdx->nSampleCol - 1;
			pIdx->aAvgEq[nCol] = 1;
		}
		for (iCol = 0; iCol < nCol; iCol++) {
			int nSample = pIdx->nSample;
			int i;	/* Used to iterate through samples */
			tRowcnt sumEq = 0;	/* Sum of the nEq values */
			tRowcnt avgEq = 0;
			tRowcnt nRow;	/* Number of rows in index */
			i64 nSum100 = 0;	/* Number of terms contributing to sumEq */
			i64 nDist100;	/* Number of distinct values in index */
			int nColumn = index_column_count(pIdx);

			if (!pIdx->aiRowEst || iCol >= nColumn
			    || pIdx->aiRowEst[iCol + 1] == 0) {
				nRow = pFinal->anLt[iCol];
				nDist100 = (i64) 100 *pFinal->anDLt[iCol];
				nSample--;
			} else {
				nRow = pIdx->aiRowEst[0];
				nDist100 =
				    ((i64) 100 * pIdx->aiRowEst[0]) /
				    pIdx->aiRowEst[iCol + 1];
			}
			pIdx->nRowEst0 = nRow;

			/* Set nSum to the number of distinct (iCol+1) field prefixes that
			 * occur in the stat4 table for this index. Set sumEq to the sum of
			 * the nEq values for column iCol for the same set (adding the value
			 * only once where there exist duplicate prefixes).
			 */
			for (i = 0; i < nSample; i++) {
				if (i == (pIdx->nSample - 1)
				    || aSample[i].anDLt[iCol] !=
				    aSample[i + 1].anDLt[iCol]
				    ) {
					sumEq += aSample[i].anEq[iCol];
					nSum100 += 100;
				}
			}

			if (nDist100 > nSum100) {
				avgEq =
				    ((i64) 100 * (nRow - sumEq)) / (nDist100 -
								    nSum100);
			}
			if (avgEq == 0)
				avgEq = 1;
			pIdx->aAvgEq[iCol] = avgEq;
		}
	}
}

/*
 * Given two IndexSample arguments, compare there payloads
 */
static int
sampleCompareMsgPack(const void *a, const void *b, void *arg)
{
	struct key_def *def = (struct key_def *)arg;
	return key_compare(((IndexSample *) a)->p, ((IndexSample *) b)->p, def);
}

/*
 * Load the content from the _sql_stat4 table
 * into the relevant Index.aSample[] arrays.
 *
 * Arguments zSql1 and zSql2 must point to SQL statements that return
 * data equivalent to the following (statements are different for stat4,
 * see the caller of this function for details):
 *
 *    zSql1: SELECT tbl,idx,count(*) FROM _sql_stat4 GROUP BY tbl,idx
 *    zSql2: SELECT tbl,idx,neq,nlt,ndlt,sample FROM _sql_stat4
 *
 */
static int
loadStatTbl(sqlite3 * db,	/* Database handle */
	    Table * pStatTab,	/* Stat table to load to */
	    const char *zSql1,	/* SQL statement 1 (see above) */
	    const char *zSql2	/* SQL statement 2 (see above) */
    )
{
	int rc;			/* Result codes from subroutines */
	sqlite3_stmt *pStmt = 0;	/* An SQL statement being run */
	Index *pPrevIdx = 0;	/* Previous index in the loop */
	IndexSample *pSample;	/* A slot in pIdx->aSample[] */
	int nIdxCnt = 0;	/* Number of different indexes visited */
	int nIdx = 0;		/* Index loop iterator */
	Index **aIndex = 0;	/* Array holding all visited indexes */

	assert(db->lookaside.bDisable);

	assert(pStatTab);
	nIdxCnt = box_index_len(SQLITE_PAGENO_TO_SPACEID(pStatTab->tnum), 0);

	if (nIdxCnt > 0) {
		aIndex = sqlite3DbMallocZero(db, sizeof(Index *) * nIdxCnt);
		if (aIndex == 0) {
			return SQLITE_NOMEM_BKPT;
		}
	}

	rc = sqlite3_prepare(db, zSql1, -1, &pStmt, 0);
	if (rc)
		goto finalize;

	while (sqlite3_step(pStmt) == SQLITE_ROW) {
		int nIdxCol = 1;	/* Number of columns in stat4 records */

		char *zTab;	/* Table name */
		char *zIndex;	/* Index name */
		Index *pIdx;	/* Pointer to the index object */
		int nSample;	/* Number of samples */
		int nByte;	/* Bytes of space required */
		int i;		/* Bytes of space required */
		tRowcnt *pSpace;

		zTab = (char *)sqlite3_column_text(pStmt, 0);
		if (zTab == 0)
			continue;
		zIndex = (char *)sqlite3_column_text(pStmt, 1);
		if (zIndex == 0)
			continue;
		nSample = sqlite3_column_int(pStmt, 2);
		pIdx = sqlite3LocateIndex(db, zIndex, zTab);
		assert(pIdx == 0 || pIdx->nSample == 0);
		/* Index.nSample is non-zero at this point if data has already been
		 * loaded from the stat4 table.
		 */
		if (pIdx == 0 || pIdx->nSample)
			continue;

		nIdxCol = index_column_count(pIdx);
		pIdx->nSampleCol = nIdxCol;
		nByte = sizeof(IndexSample) * nSample;
		nByte += sizeof(tRowcnt) * nIdxCol * 3 * nSample;
		nByte += nIdxCol * sizeof(tRowcnt);	/* Space for Index.aAvgEq[] */

		pIdx->aSample = sqlite3DbMallocZero(db, nByte);
		if (pIdx->aSample == 0) {
			sqlite3_finalize(pStmt);
			rc = SQLITE_NOMEM_BKPT;
			goto finalize;
		}
		pSpace = (tRowcnt *) & pIdx->aSample[nSample];
		pIdx->aAvgEq = pSpace;
		pSpace += nIdxCol;
		for (i = 0; i < nSample; i++) {
			pIdx->aSample[i].anEq = pSpace;
			pSpace += nIdxCol;
			pIdx->aSample[i].anLt = pSpace;
			pSpace += nIdxCol;
			pIdx->aSample[i].anDLt = pSpace;
			pSpace += nIdxCol;
		}
		assert(((u8 *) pSpace) - nByte == (u8 *) (pIdx->aSample));

		aIndex[nIdx] = pIdx;
		assert(nIdx < nIdxCnt);
		++nIdx;
	}
	rc = sqlite3_finalize(pStmt);
	if (rc)
		goto finalize;

	rc = sqlite3_prepare(db, zSql2, -1, &pStmt, 0);
	if (rc)
		goto finalize;

	while (sqlite3_step(pStmt) == SQLITE_ROW) {
		char *zTab;	/* Table name */
		char *zIndex;	/* Index name */
		Index *pIdx;	/* Pointer to the index object */
		int nCol = 1;	/* Number of columns in index */

		zTab = (char *)sqlite3_column_text(pStmt, 0);
		if (zTab == 0)
			continue;
		zIndex = (char *)sqlite3_column_text(pStmt, 1);
		if (zIndex == 0)
			continue;
		pIdx = sqlite3LocateIndex(db, zIndex, zTab);
		if (pIdx == 0)
			continue;

		nCol = pIdx->nSampleCol;
		if (pIdx != pPrevIdx) {
			initAvgEq(pPrevIdx);
			pPrevIdx = pIdx;
		}
		pSample = &pIdx->aSample[pIdx->nSample];
		decodeIntArray((char *)sqlite3_column_text(pStmt, 2), nCol,
			       pSample->anEq, 0, 0);
		decodeIntArray((char *)sqlite3_column_text(pStmt, 3), nCol,
			       pSample->anLt, 0, 0);
		decodeIntArray((char *)sqlite3_column_text(pStmt, 4), nCol,
			       pSample->anDLt, 0, 0);

		/* Take a copy of the sample. Add two 0x00 bytes the end of the buffer.
		 * This is in case the sample record is corrupted. In that case, the
		 * sqlite3VdbeRecordCompare() may read up to two varints past the
		 * end of the allocated buffer before it realizes it is dealing with
		 * a corrupt record. Adding the two 0x00 bytes prevents this from causing
		 * a buffer overread.
		 */
		pSample->n = sqlite3_column_bytes(pStmt, 5);
		pSample->p = sqlite3DbMallocZero(db, pSample->n + 2);
		if (pSample->p == 0) {
			sqlite3_finalize(pStmt);
			rc = SQLITE_NOMEM_BKPT;
			goto finalize;
		}
		if (pSample->n) {
			memcpy(pSample->p, sqlite3_column_blob(pStmt, 5),
			       pSample->n);
		}
		pIdx->nSample++;
	}
	rc = sqlite3_finalize(pStmt);
	if (rc == SQLITE_OK)
		initAvgEq(pPrevIdx);

	assert(nIdx <= nIdxCnt);
	for (int i = 0; i < nIdx; ++i) {
		Index *pIdx = aIndex[i];
		assert(pIdx);
		/*  - sid, iid
		 *  - space_index_key_def
		 *  - key_compare
		 */
		int sid = SQLITE_PAGENO_TO_SPACEID(pIdx->tnum);
		int iid = SQLITE_PAGENO_TO_INDEXID(pIdx->tnum);
		struct space *s = space_by_id(sid);
		assert(s);
		struct key_def *def = space_index_key_def(s, iid);
		assert(def);
		qsort_arg(pIdx->aSample,
			  pIdx->nSample,
			  sizeof(pIdx->aSample[0]), sampleCompareMsgPack, def);
	}

 finalize:
	sqlite3DbFree(db, aIndex);
	return rc;
}

/*
 * Load content from the _sql_stat4 table into
 * the Index.aSample[] arrays of all indices.
 */
static int
loadStat4(sqlite3 * db)
{
	Table *pTab = 0;	/* Pointer to stat table */

	assert(db->lookaside.bDisable);
	pTab = sqlite3HashFind(&db->pSchema->tblHash, "_sql_stat4");
	/* _slq_stat4 is a system space, so it always exists. */
	assert(pTab != NULL);
	return loadStatTbl(db, pTab,
			   "SELECT \"tbl\",\"idx\",count(*) FROM \"_sql_stat4\""
			   " GROUP BY \"tbl\",\"idx\"",
			   "SELECT \"tbl\",\"idx\",\"neq\",\"nlt\",\"ndlt\","
			   "\"sample\" FROM \"_sql_stat4\"");
}

/*
 * Load the content of the _sql_stat1 and sql_stat4 tables. The
 * contents of _sql_stat1 are used to populate the Index.aiRowEst[]
*\* arrays. The contents of sql_stat4 are used to populate the
 * Index.aSample[] arrays.
 *
 * If the _sql_stat1 table is not present in the database, SQLITE_ERROR
 * is returned. In this case, even if the sql_stat4 table is present,
 * no data is read from it.
 *
 * If the _sql_stat4 table is not present in the database, SQLITE_ERROR
 * is returned. However, in this case, data is read from the _sql_stat1
 * table (if it is present) before returning.
 *
 * If an OOM error occurs, this function always sets db->mallocFailed.
 * This means if the caller does not care about other errors, the return
 * code may be ignored.
 */
int
sqlite3AnalysisLoad(sqlite3 * db)
{
	analysisInfo sInfo;
	HashElem *i, *j;
	char *zSql;
	int rc = SQLITE_OK;

	/* Clear any prior statistics */
	for (j = sqliteHashFirst(&db->pSchema->tblHash); j;
	     j = sqliteHashNext(j)) {
		Table *pTab = sqliteHashData(j);
		for (i = sqliteHashFirst(&pTab->idxHash); i;
		     i = sqliteHashNext(i)) {
			Index *pIdx = sqliteHashData(i);
			pIdx->aiRowLogEst[0] = 0;
			sqlite3DeleteIndexSamples(db, pIdx);
			pIdx->aSample = 0;
		}
	}

	/* Load new statistics out of the _sql_stat1 table */
	sInfo.db = db;
	zSql = "SELECT \"tbl\",\"idx\",\"stat\" FROM \"_sql_stat1\"";
	rc = sqlite3_exec(db, zSql, analysisLoader, &sInfo, 0);

	/* Set appropriate defaults on all indexes not in the _sql_stat1 table */
	for (j = sqliteHashFirst(&db->pSchema->tblHash); j;
	     j = sqliteHashNext(j)) {
		Table *pTab = sqliteHashData(j);
		for (i = sqliteHashFirst(&pTab->idxHash); i;
		     i = sqliteHashNext(i)) {
			Index *pIdx = sqliteHashData(i);
			if (pIdx->aiRowLogEst[0] == 0)
				sqlite3DefaultRowEst(pIdx);
		}
	}

	/* Load the statistics from the _sql_stat4 table. */
	if (rc == SQLITE_OK && OptimizationEnabled(db, SQLITE_Stat4)) {
		db->lookaside.bDisable++;
		rc = loadStat4(db);
		db->lookaside.bDisable--;
	}

	for (j = sqliteHashFirst(&db->pSchema->tblHash); j;
	     j = sqliteHashNext(j)) {
		Table *pTab = sqliteHashData(j);
		for (i = sqliteHashFirst(&pTab->idxHash); i;
		     i = sqliteHashNext(i)) {
			Index *pIdx = sqliteHashData(i);
			sqlite3_free(pIdx->aiRowEst);
			pIdx->aiRowEst = 0;
		}
	}

	if (rc == SQLITE_NOMEM) {
		sqlite3OomFault(db);
	}
	return rc;
}

#endif				/* SQLITE_OMIT_ANALYZE */
