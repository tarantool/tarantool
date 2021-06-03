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
 * number of rows in the table.)  The second integer is the
 * average number of rows in the index that have the same value in
 * the first column of the index.  The third integer is the average
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

#include "box/box.h"
#include "box/execute.h"
#include "box/index.h"
#include "box/key_def.h"
#include "box/schema.h"
#include <qsort_arg.h>

#include "sqlInt.h"
#include "tarantoolInt.h"
#include "mem.h"
#include "vdbeInt.h"

#if 0

/**
 * This routine generates code that opens the sql_stat1/4 tables.
 * If the sql_statN tables do not previously exist, they are
 * created.
 *
 * @param parse Parsing context.
 * @param table_name Delete records of this table if specified.
 */
static void
vdbe_emit_stat_space_open(struct Parse *parse, const char *table_name)
{
	const char *stat_names[] = {"_sql_stat1", "_sql_stat4"};
	struct Vdbe *v = sqlGetVdbe(parse);
	assert(v != NULL);
	assert(sqlVdbeDb(v) == parse->db);
	for (uint i = 0; i < lengthof(stat_names); ++i) {
		const char *space_name = stat_names[i];
		if (table_name != NULL) {
			vdbe_emit_stat_space_clear(parse, space_name, NULL,
						   table_name);
		} else {
			struct space *stat_space = space_by_name(stat_names[i]);
			assert(stat_space != NULL);
			sqlVdbeAddOp1(v, OP_Clear, stat_space->def->id);
		}
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
	sql *db;		/* Database connection, for malloc() */
};

/* Reclaim memory used by a Stat4Sample
 */
static void
sampleClear(sql * db, Stat4Sample * p)
{
	assert(db != 0);
	if (p->nKey) {
		sqlDbFree(db, p->aKey);
		p->nKey = 0;
	}
}

/* Initialize the BLOB value of a sample key.
 */
static void
sampleSetKey(sql * db, Stat4Sample * p, int n, const u8 * pData)
{
	assert(db != 0);
	if (p->nKey)
		sqlDbFree(db, p->aKey);
	p->aKey = sqlDbMallocRawNN(db, n);
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
	sqlDbFree(p->db, p);
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
MAYBE_UNUSED static void
statInit(sql_context * context, int argc, sql_value ** argv)
{
	Stat4Accum *p;
	int nCol;		/* Number of columns in index being sampled */
	int nKeyCol;		/* Number of key columns */
	int nColUp;		/* nCol rounded up for alignment */
	int n;			/* Bytes of space to allocate */
	sql *db;		/* Database connection */
	int mxSample = SQL_STAT4_SAMPLES;

	/* Decode the three function arguments */
	UNUSED_PARAMETER(argc);
	nCol = sql_value_int(argv[0]);
	assert(nCol > 0);
	/* Tarantool: we use an additional artificial column for the reason
	 * that Tarantool's indexes don't contain PK columns after key columns.
	 * Hence, in order to correctly gather statistics when dealing with 
	 * identical rows, we have to use this artificial column.
	 */
	nColUp = sizeof(tRowcnt) < 8 ? (nCol + 2) & ~1 : nCol + 1;
	nKeyCol = sql_value_int(argv[1]);
	assert(nKeyCol <= nCol);
	assert(nKeyCol > 0);

	/* Allocate the space required for the Stat4Accum object */
	n = sizeof(*p)
	    + sizeof(tRowcnt) * nColUp	/* Stat4Accum.anEq */
	    + sizeof(tRowcnt) * nColUp	/* Stat4Accum.anDLt */
	    + sizeof(tRowcnt) * nColUp	/* Stat4Accum.anLt */
	    + sizeof(Stat4Sample) * (nCol + 1 + mxSample)	/* Stat4Accum.aBest[], a[] */
	    + sizeof(tRowcnt) * 3 * nColUp * (nCol + 1 + mxSample);
	db = sql_context_db_handle(context);
	p = sqlDbMallocZero(db, n);
	if (p == 0) {
		context->is_aborted = true;
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
		    (tRowcnt) (sql_value_int64(argv[2]) /
			       (mxSample / 3 + 1) + 1);
		p->current.anLt = &p->current.anEq[nColUp];
		p->iPrn =
		    0x689e962d * (u32) nCol ^ 0xd0944565 *
		    (u32) sql_value_int(argv[2]);

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
	sql_result_blob(context, p, sizeof(*p), stat4Destructor);
}

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
	 * into samples array at this point.
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
MAYBE_UNUSED static void
statPush(sql_context * context, int argc, sql_value ** argv)
{
	int i;
	/* The three function arguments */
	Stat4Accum *p = (Stat4Accum *) sql_value_blob(argv[0]);
	int iChng = sql_value_int(argv[1]);

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
	sampleSetKey(p->db, &p->current, sql_value_bytes(argv[2]),
		     sql_value_blob(argv[2]));
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
MAYBE_UNUSED static void
statGet(sql_context * context, int argc, sql_value ** argv)
{
	Stat4Accum *p = (Stat4Accum *) sql_value_blob(argv[0]);
	/* STAT4 have a parameter on this routine. */
	int eCall = sql_value_int(argv[1]);
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
		 * "100 10 2", then sql estimates that:
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

		char *zRet = sqlMallocZero((p->nKeyCol + 1) * 25);
		if (zRet == 0) {
			context->is_aborted = true;
			return;
		}

		sql_snprintf(24, zRet, "%llu", (u64) p->nRow);
		z = zRet + sqlStrlen30(zRet);
		for (i = 0; i < p->nKeyCol; i++) {
			u64 nDistinct = p->current.anDLt[i] + 1;
			u64 iVal = (p->nRow + nDistinct - 1) / nDistinct;
			sql_snprintf(24, z, " %llu", iVal);
			z += sqlStrlen30(z);
			assert(p->current.anEq[i]);
		}
		assert(z[0] == '\0' && z > zRet);

		sql_result_text(context, zRet, -1, sql_free);
	} else if (eCall == STAT_GET_KEY) {
		if (p->iGet < 0) {
			samplePushPrevious(p, 0);
			p->iGet = 0;
		}
		if (p->iGet < p->nSample) {
			Stat4Sample *pS = p->a + p->iGet;
			sql_result_blob(context, pS->aKey, pS->nKey,
					    SQL_TRANSIENT);
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

	char *zRet = sqlMallocZero(p->nCol * 25);
	if (zRet == 0) {
		context->is_aborted = true;
	} else {
		int i;
		char *z = zRet;
		for (i = 0; i < p->nCol; i++) {
			sql_snprintf(24, z, "%llu ", (u64) aCnt[i]);
			z += sqlStrlen30(z);
		}
		assert(z[0] == '\0' && z > zRet);
		z[-1] = '\0';
		sql_result_text(context, zRet, -1, sql_free);
	}

}
#ifndef SQL_DEBUG
UNUSED_PARAMETER(argc);
#endif
}

static void
callStatGet(Vdbe * v, int regStat4, int iParam, int regOut)
{
	assert(regOut != regStat4 && regOut != regStat4 + 1);
	sqlVdbeAddOp2(v, OP_Integer, iParam, regStat4 + 1);
	struct func *func = sql_func_by_signature("_sql_stat_get", 2);
	assert(func != NULL);
	sqlVdbeAddOp4(v, OP_BuiltinFunction0, 0, regStat4, regOut,
		      (char *)func, P4_FUNC);
	sqlVdbeChangeP5(v, 2);
}

/**
 * Generate code to do an analysis of all indices associated with
 * a single table.
 *
 * @param parse Current parsing context.
 * @param space Space to be analyzed.
 */
static void
vdbe_emit_analyze_space(struct Parse *parse, struct space *space)
{
	assert(space != NULL);
	struct space *stat1 = space_by_name("_sql_stat1");
	assert(stat1 != NULL);
	struct space *stat4 = space_by_name("_sql_stat4");
	assert(stat4 != NULL);

	/* Register to hold Stat4Accum object. */
	int stat4_reg = ++parse->nMem;
	/* Index of changed index field. */
	int chng_reg = ++parse->nMem;
	/* Key argument passed to stat_push(). */
	int key_reg = ++parse->nMem;
	/* Temporary use register. */
	int tmp_reg = ++parse->nMem;
	/* Register containing table name. */
	int tab_name_reg = ++parse->nMem;
	/* Register containing index name. */
	int idx_name_reg = ++parse->nMem;
	/* Value for the stat column of _sql_stat1. */
	int stat1_reg = ++parse->nMem;
	/* MUST BE LAST (see below). */
	int prev_reg = ++parse->nMem;
	/* Do not gather statistics on system tables. */
	if (space_is_system(space))
		return;
	/*
	 * Open a read-only cursor on the table. Also allocate
	 * a cursor number to use for scanning indexes.
	 */
	int tab_cursor = parse->nTab;
	parse->nTab += 2;
	assert(space->index_count != 0);
	struct Vdbe *v = sqlGetVdbe(parse);
	assert(v != NULL);
	const char *tab_name = space_name(space);
	sqlVdbeAddOp4(v, OP_IteratorOpen, tab_cursor, 0, 0, (void *) space,
			  P4_SPACEPTR);
	sqlVdbeLoadString(v, tab_name_reg, space->def->name);
	for (uint32_t j = 0; j < space->index_count; ++j) {
		struct index *idx = space->index[j];
		const char *idx_name;
		/*
		 * Primary indexes feature automatically generated
		 * names. Thus, for the sake of clarity, use
		 * instead more familiar table name.
		 */
		if (idx->def->iid == 0)
			idx_name = tab_name;
		else
			idx_name = idx->def->name;
		int part_count = idx->def->key_def->part_count;
		/* Populate the register containing the index name. */
		sqlVdbeLoadString(v, idx_name_reg, idx_name);
		VdbeComment((v, "Analysis for %s.%s", tab_name, idx_name));
		/*
		 * Pseudo-code for loop that calls stat_push():
		 *
		 *   Rewind csr
		 *   if eof(csr) goto end_of_scan;
		 *   chng_reg = 0
		 *   goto chng_addr_0;
		 *
		 *  next_row:
		 *   chng_reg = 0
		 *   if( idx(0) != prev_reg(0) ) goto chng_addr_0
		 *   chng_reg = 1
		 *   if( idx(1) != prev_reg(1) ) goto chng_addr_1
		 *   ...
		 *   chng_reg = N
		 *   goto chng_addr_N
		 *
		 *  chng_addr_0:
		 *   prev_reg(0) = idx(0)
		 *  chng_addr_1:
		 *   prev_reg(1) = idx(1)
		 *  ...
		 *
		 *  distinct_addr:
		 *   key_reg = idx(key)
		 *   stat_push(P, chng_reg, key_reg)
		 *   Next csr
		 *   if !eof(csr) goto next_row;
		 *
		 *  end_of_scan:
		 */

		/*
		 * Make sure there are enough memory cells
		 * allocated to accommodate the prev_reg array
		 * and a trailing key (the key slot is required
		 * when building a record to insert into
		 * the sample column of the _sql_stat4 table).
		 */
		parse->nMem = MAX(parse->nMem, prev_reg + part_count);
		/* Open a cursor on the index being analyzed. */
		int idx_cursor;
		if (j != 0) {
			idx_cursor = parse->nTab - 1;
			sqlVdbeAddOp4(v, OP_IteratorOpen, idx_cursor,
					  idx->def->iid, 0,
					  (void *) space, P4_SPACEPTR);
			VdbeComment((v, "%s", idx->def->name));
		} else {
			/* We have already opened cursor on PK. */
			idx_cursor = tab_cursor;
		}
		/*
		 * Invoke the stat_init() function.
		 * The arguments are:
		 *  (1) The number of columns in the index
		 *      (including the number of PK columns);
		 *  (2) The number of columns in the key
		 *       without the pk;
		 *  (3) the number of rows in the index;
		 * FIXME: for Tarantool first and second args
		 * are the same.
		 *
		 * The third argument is only used for STAT4
		 */
		sqlVdbeAddOp2(v, OP_Count, idx_cursor, stat4_reg + 3);
		sqlVdbeAddOp2(v, OP_Integer, part_count, stat4_reg + 1);
		sqlVdbeAddOp2(v, OP_Integer, part_count, stat4_reg + 2);
		struct func *init_func =
			sql_func_by_signature("_sql_stat_init", 3);
		assert(init_func != NULL);
		sqlVdbeAddOp4(v, OP_BuiltinFunction0, 0, stat4_reg + 1,
			      stat4_reg, (char *)init_func, P4_FUNC);
		sqlVdbeChangeP5(v, 3);
		/*
		 * Implementation of the following:
		 *
		 *   Rewind csr
		 *   if eof(csr) goto end_of_scan;
		 *   chng_reg = 0
		 *   goto next_push_0;
		 */
		int rewind_addr = sqlVdbeAddOp1(v, OP_Rewind, idx_cursor);
		sqlVdbeAddOp2(v, OP_Integer, 0, chng_reg);
		int distinct_addr = sqlVdbeMakeLabel(v);
		/* Array of jump instruction addresses. */
		int *jump_addrs = region_alloc(&parse->region,
					       sizeof(int) * part_count);
		if (jump_addrs == NULL) {
			diag_set(OutOfMemory, sizeof(int) * part_count,
				 "region", "jump_addrs");
			parse->is_aborted = true;
			return;
		}
		/*
		 *  next_row:
		 *   chng_reg = 0
		 *   if( idx(0) != prev_reg(0) ) goto chng_addr_0
		 *   chng_reg = 1
		 *   if( idx(1) != prev_reg(1) ) goto chng_addr_1
		 *   ...
		 *   chng_reg = N
		 *   goto distinct_addr
		 */
		sqlVdbeAddOp0(v, OP_Goto);
		int next_row_addr = sqlVdbeCurrentAddr(v);
		if (part_count == 1 && idx->def->opts.is_unique) {
			/*
			 * For a single-column UNIQUE index, once
			 * we have found a non-NULL row, we know
			 * that all the rest will be distinct, so
			 * skip subsequent distinctness tests.
			 */
			sqlVdbeAddOp2(v, OP_NotNull, prev_reg,
					  distinct_addr);
		}
		struct key_part *part = idx->def->key_def->parts;
		for (int i = 0; i < part_count; ++i, ++part) {
			struct coll *coll = part->coll;
			sqlVdbeAddOp2(v, OP_Integer, i, chng_reg);
			sqlVdbeAddOp3(v, OP_Column, idx_cursor,
					  part->fieldno, tmp_reg);
			jump_addrs[i] = sqlVdbeAddOp4(v, OP_Ne, tmp_reg, 0,
							 prev_reg + i,
							 (char *)coll,
							 P4_COLLSEQ);
			sqlVdbeChangeP5(v, SQL_NULLEQ);
		}
		sqlVdbeAddOp2(v, OP_Integer, part_count, chng_reg);
		sqlVdbeGoto(v, distinct_addr);
		/*
		 *  chng_addr_0:
		 *   prev_reg(0) = idx(0)
		 *  chng_addr_1:
		 *   prev_reg(1) = idx(1)
		 *  ...
		 */
		sqlVdbeJumpHere(v, next_row_addr - 1);
		part = idx->def->key_def->parts;
		for (int i = 0; i < part_count; ++i, ++part) {
			sqlVdbeJumpHere(v, jump_addrs[i]);
			sqlVdbeAddOp3(v, OP_Column, idx_cursor,
					  part->fieldno, prev_reg + i);
		}
		sqlVdbeResolveLabel(v, distinct_addr);
		/*
		 *  chng_addr_N:
		 *   key_reg = idx(key)
		 *   stat_push(P, chng_reg, key_reg)
		 *   Next csr
		 *   if !eof(csr) goto next_row;
		 */
		assert(key_reg == (stat4_reg + 2));
		struct index *pk = space_index(space, 0);
		int pk_part_count = pk->def->key_def->part_count;
		/* Allocate memory for array. */
		parse->nMem = MAX(parse->nMem,
				  prev_reg + part_count + pk_part_count);
		int stat_key_reg = prev_reg + part_count;
		for (int i = 0; i < pk_part_count; i++) {
			uint32_t k = pk->def->key_def->parts[i].fieldno;
			assert(k < space->def->field_count);
			sqlVdbeAddOp3(v, OP_Column, idx_cursor, k,
					  stat_key_reg + i);
			VdbeComment((v, "%s", space->def->fields[k].name));
		}
		sqlVdbeAddOp3(v, OP_MakeRecord, stat_key_reg,
				  pk_part_count, key_reg);
		assert(chng_reg == (stat4_reg + 1));
		struct func *push_func =
			sql_func_by_signature("_sql_stat_push", 3);
		assert(push_func != NULL);
		sqlVdbeAddOp4(v, OP_BuiltinFunction0, 1, stat4_reg, tmp_reg,
			      (char *)push_func, P4_FUNC);
		sqlVdbeChangeP5(v, 3);
		sqlVdbeAddOp2(v, OP_Next, idx_cursor, next_row_addr);
		/* Add the entry to the stat1 table. */
		callStatGet(v, stat4_reg, STAT_GET_STAT1, stat1_reg);
		enum field_type types[4] = { FIELD_TYPE_STRING,
					     FIELD_TYPE_STRING,
					     FIELD_TYPE_STRING,
					     field_type_MAX };
		sqlVdbeAddOp4(v, OP_MakeRecord, tab_name_reg, 4, tmp_reg,
				  (char *)types, sizeof(types));
		sqlVdbeAddOp4(v, OP_IdxInsert, tmp_reg, 0, 0,
				  (char *)stat1, P4_SPACEPTR);
		/* Add the entries to the stat4 table. */
		int eq_reg = stat1_reg;
		int lt_reg = stat1_reg + 1;
		int dlt_reg = stat1_reg + 2;
		int sample_reg = stat1_reg + 3;
		int col_reg = stat1_reg + 4;
		int sample_key_reg = col_reg + part_count;
		parse->nMem = MAX(parse->nMem, col_reg + part_count);
		int next_addr = sqlVdbeCurrentAddr(v);
		callStatGet(v, stat4_reg, STAT_GET_KEY, sample_key_reg);
		int is_null_addr = sqlVdbeAddOp1(v, OP_IsNull,
						     sample_key_reg);
		callStatGet(v, stat4_reg, STAT_GET_NEQ, eq_reg);
		callStatGet(v, stat4_reg, STAT_GET_NLT, lt_reg);
		callStatGet(v, stat4_reg, STAT_GET_NDLT, dlt_reg);
		sqlVdbeAddOp4Int(v, OP_NotFound, tab_cursor, next_addr,
				     sample_key_reg, 0);
		/*
		 * We know that the sample_key_reg row exists
		 * because it was read by the previous loop.
		 * Thus the not-found jump of seekOp will never
		 * be taken.
		 */
		for (int i = 0; i < part_count; i++) {
			uint32_t tabl_col = idx->def->key_def->parts[i].fieldno;
			sqlVdbeAddOp3(v, OP_Column, tab_cursor, tabl_col,
				      col_reg + i);
		}
		sqlVdbeAddOp3(v, OP_MakeRecord, col_reg, part_count,
				  sample_reg);
		sqlVdbeAddOp3(v, OP_MakeRecord, tab_name_reg, 6, tmp_reg);
		sqlVdbeAddOp4(v, OP_IdxReplace, tmp_reg, 0, 0,
				  (char *)stat4, P4_SPACEPTR);
		/* P1==1 for end-of-loop. */
		sqlVdbeAddOp2(v, OP_Goto, 1, next_addr);
		sqlVdbeJumpHere(v, is_null_addr);
		/* End of analysis. */
		sqlVdbeJumpHere(v, rewind_addr);
	}
}

/*
 * Generate code that will cause the most recent index analysis to
 * be loaded into internal hash tables where is can be used.
 */
static void
loadAnalysis(Parse * pParse)
{
	Vdbe *v = sqlGetVdbe(pParse);
	if (v) {
		sqlVdbeAddOp1(v, OP_LoadAnalysis, 0);
	}
}

static int
sql_space_foreach_analyze(struct space *space, void *data)
{
	if (space->def->opts.is_view)
		return 0;
	vdbe_emit_analyze_space((struct Parse*)data, space);
	return 0;
}

/**
 * Generate code that will do an analysis of all spaces created
 * via SQL facilities.
 */
static void
sql_analyze_database(struct Parse *parser)
{
	sql_set_multi_write(parser, false);
	vdbe_emit_stat_space_open(parser, NULL);
	space_foreach(sql_space_foreach_analyze, (void *)parser);
	loadAnalysis(parser);
}

/**
 * Generate code that will do an analysis of a single table in
 * a database.
 *
 * @param parse Parser context.
 * @param table Target table to analyze.
 */
static void
vdbe_emit_analyze_table(struct Parse *parse, struct space *space)
{
	assert(space != NULL);
	sql_set_multi_write(parse, false);
	/*
	 * There are two system spaces for statistics: _sql_stat1
	 * and _sql_stat4.
	 */
	vdbe_emit_stat_space_open(parse, space->def->name);
	vdbe_emit_analyze_space(parse, space);
	loadAnalysis(parse);
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
sqlAnalyze(Parse * pParse, Token * pName)
{
	struct sql *db = pParse->db;
	if (pName == NULL) {
		/* Form 1:  Analyze everything */
		sql_analyze_database(pParse);
	} else {
		/* Form 2:  Analyze table named */
		char *z = sql_name_from_token(db, pName);
		if (z == NULL) {
			pParse->is_aborted = true;
			return;
		}
		struct space *sp = space_by_name(z);
		if (sp != NULL) {
			if (sp->def->opts.is_view) {
				diag_set(ClientError, ER_SQL_ANALYZE_ARGUMENT,
					 sp->def->name);
				pParse->is_aborted = true;
			} else {
				vdbe_emit_analyze_table(pParse, sp);
			}
		} else {
			diag_set(ClientError, ER_NO_SUCH_SPACE, z);
			pParse->is_aborted = true;
		}
		sqlDbFree(db, z);
	}
	struct Vdbe *v = sqlGetVdbe(pParse);
	if (v != NULL)
		sqlVdbeAddOp0(v, OP_Expire);
}

/**
 * Used to pass information from the analyzer reader through
 * to the callback routine.
 */
struct analysis_index_info {
	/** Database handler. */
	sql *db;
	/*** Array of statistics for each index. */
	struct index_stat *stats;
	/** Ordinal number of index to be processed. */
	uint32_t index_count;
};

/**
 * The first argument points to a nul-terminated string
 * containing a list of space separated integers. Load
 * the first stat_size of these into the output arrays.
 *
 * @param stat_string String containing array of integers.
 * @param stat_size Size of output arrays.
 * @param[out] stat_exact Decoded array of statistics.
 * @param[out] stat_log Decoded array of stat logariphms.
 */
static void
decode_stat_string(const char *stat_string, int stat_size, tRowcnt *stat_exact,
		   LogEst *stat_log) {
	const char *z = stat_string;
	if (z == NULL)
		z = "";
	for (int i = 0; *z && i < stat_size; i++) {
		tRowcnt v = 0;
		int c;
		while ((c = z[0]) >= '0' && c <= '9') {
			v = v * 10 + c - '0';
			z++;
		}
		if (stat_exact != NULL)
			stat_exact[i] = v;
		if (stat_log != NULL)
			stat_log[i] = sqlLogEst(v);
		if (*z == ' ')
			z++;
	}
}

/**
 * This callback is invoked once for each index when reading the
 * _sql_stat1 table.
 *
 *     argv[0] = name of the table
 *     argv[1] = name of the index (might be NULL)
 *     argv[2] = results of analysis - array of integers
 *
 * Entries for which argv[1]==NULL simply record the number
 * of rows in the table. This routine also allocates memory
 * for stat struct itself and statistics which are not related
 * to stat4 samples.
 *
 * @param data Additional analysis info.
 * @param argc Number of arguments.
 * @param argv Callback arguments.
 * @retval 0 on success, -1 otherwise.
 */
static int
analysis_loader(void *data, int argc, char **argv, char **unused)
{
	assert(argc == 3);
	UNUSED_PARAMETER2(unused, argc);
	if (argv == 0 || argv[0] == 0 || argv[2] == 0)
		return 0;
	struct analysis_index_info *info = (struct analysis_index_info *) data;
	assert(info->stats != NULL);
	struct index_stat *stat = &info->stats[info->index_count++];
	struct space *space = space_by_name(argv[0]);
	if (space == NULL)
		return -1;
	struct index *index;
	uint32_t iid = box_index_id_by_name(space->def->id, argv[1],
					    strlen(argv[1]));
	/*
	 * Convention is if index's name matches with space's
	 * one, then it is primary index.
	 */
	if (iid != BOX_ID_NIL) {
		index = space_index(space, iid);
	} else {
		if (sql_stricmp(argv[0], argv[1]) != 0)
			return -1;
		index = space_index(space, 0);
	}
	assert(index != NULL);
	/*
	 * Additional field is used to describe total
	 * count of tuples in index. Although now all
	 * indexes feature the same number of tuples,
	 * partial indexes are going to be implemented
	 * someday.
	 */
	uint32_t column_count = index->def->key_def->part_count + 1;
	/*
	 * Stat arrays may already be set here if there
	 * are duplicate _sql_stat1 entries for this
	 * index. In that case just clobber the old data
	 * with the new instead of allocating a new array.
	 */
	uint32_t stat1_size = column_count * sizeof(uint32_t);
	stat->tuple_stat1 = region_alloc(&fiber()->gc, stat1_size);
	if (stat->tuple_stat1 == NULL) {
		diag_set(OutOfMemory, stat1_size, "region", "tuple_stat1");
		return -1;
	}
	stat->tuple_log_est = region_alloc(&fiber()->gc, stat1_size);
	if (stat->tuple_log_est == NULL) {
		diag_set(OutOfMemory, stat1_size, "region", "tuple_log_est");
		return -1;
	}
	decode_stat_string(argv[2], column_count, stat->tuple_stat1,
			   stat->tuple_log_est);
	stat->is_unordered = false;
	stat->skip_scan_enabled = true;
	char *z = argv[2];
	/* Position ptr at the end of stat string. */
	for (; *z == ' ' || (*z >= '0' && *z <= '9'); ++z);
	while (z[0]) {
		if (strncmp("unordered", z, strlen("unordered")) == 0)
			index->def->opts.stat->is_unordered = true;
		else if (strncmp("noskipscan", z, strlen("noskipscan")) == 0)
			index->def->opts.stat->skip_scan_enabled = false;
		while (z[0] != 0 && z[0] != ' ')
			z++;
		while (z[0] == ' ')
			z++;
	}
	return 0;
}

/**
 * Calculate avg_eq array based on the samples from index.
 * Some *magic* calculations happen here.
 */
static void
init_avg_eq(struct index *index, struct index_stat *stat)
{
	assert(stat != NULL);
	struct index_sample *samples = stat->samples;
	uint32_t sample_count = stat->sample_count;
	uint32_t field_count = stat->sample_field_count;
	struct index_sample *last_sample = &samples[sample_count - 1];
	if (field_count > 1)
		stat->avg_eq[--field_count] = 1;
	for (uint32_t i = 0; i < field_count; ++i) {
		uint32_t column_count = index->def->key_def->part_count;
		tRowcnt eq_sum = 0;
		tRowcnt eq_avg = 0;
		uint32_t tuple_count = index->vtab->size(index);
		uint64_t distinct_tuple_count;
		uint64_t terms_sum = 0;
		if (i >= column_count || stat->tuple_stat1[i + 1] == 0) {
			distinct_tuple_count = 100 * last_sample->dlt[i];
			sample_count--;
		} else {
			assert(stat->tuple_stat1 != NULL);
			distinct_tuple_count = (100 * tuple_count) /
				stat->tuple_stat1[i + 1];
		}
		for (uint32_t j = 0; j < sample_count; ++j) {
			if (j == (stat->sample_count - 1) ||
			    samples[j].dlt[i] != samples[j + 1].dlt[i]) {
				eq_sum += samples[j].eq[i];
				terms_sum += 100;
			}
		}
		if (distinct_tuple_count > terms_sum) {
			eq_avg = 100 * (tuple_count - eq_sum) /
				(distinct_tuple_count - terms_sum);
		}
		if (eq_avg == 0)
			eq_avg = 1;
		stat->avg_eq[i] = eq_avg;
	}
}

/**
 * Given two key arguments, compare there payloads.
 * This is a simple wrapper around key_compare() to support
 * qsort() interface.
 */
static int
sample_compare(const void *a, const void *b, void *arg)
{
	struct key_def *def = (struct key_def *)arg;
	return key_compare(((struct index_sample *) a)->sample_key, HINT_NONE,
			   ((struct index_sample *) b)->sample_key, HINT_NONE,
			   def);
}

/**
 * Load the content from the _sql_stat4 table
 * into the relevant index->stat->samples[] arrays.
 *
 * Arguments must point to SQL statements that return
 * data equivalent to the following:
 *
 *    prepare: SELECT tbl,idx,count(*) FROM _sql_stat4 GROUP BY tbl,idx;
 *    load: SELECT tbl,idx,neq,nlt,ndlt,sample FROM _sql_stat4;
 *
 * 'prepare' statement is used to allocate enough memory for
 * statistics (i.e. arrays lt, dt, dlt and avg_eq). 'load' query
 * is needed for
 *
 * @param sql_select_prepare SELECT statement, see above.
 * @param sql_select_load SELECT statement, see above.
 * @param[out] stats Statistics is saved here.
 * @retval 0 on success, -1 otherwise.
 */
static int
load_stat_from_space(const char *sql_select_prepare,
		     const char *sql_select_load, struct index_stat *stats)
{
	struct index **indexes = NULL;
	struct space *stat_space = space_by_name("_sql_stat4");
	assert(stat_space != NULL);
	uint32_t index_count = box_index_len(stat_space->def->id, 0);
	if (index_count > 0) {
		size_t alloc_size = sizeof(struct index *) * index_count;
		indexes = region_alloc(&fiber()->gc, alloc_size);
		if (indexes == NULL) {
			diag_set(OutOfMemory, alloc_size, "region", "indexes");
			return -1;
		}
	}
	sql_stmt *stmt = NULL;
	int rc = sql_stmt_compile(sql_select_prepare, -1, NULL, &stmt, 0);
	if (rc)
		goto finalize;
	uint32_t current_idx_count = 0;
	while (sql_step(stmt) == SQL_ROW) {
		const char *space_name = (char *)sql_column_text(stmt, 0);
		if (space_name == NULL)
			continue;
		const char *index_name = (char *)sql_column_text(stmt, 1);
		if (index_name == NULL)
			continue;
		uint32_t sample_count = sql_column_int(stmt, 2);
		struct space *space = space_by_name(space_name);
		assert(space != NULL);
		struct index *index;
		uint32_t iid = box_index_id_by_name(space->def->id, index_name,
						    strlen(index_name));
		if (sql_stricmp(space_name, index_name) == 0 &&
		    iid == BOX_ID_NIL)
			index = space_index(space, 0);
		else
			index = space_index(space, iid);
		assert(index != NULL);
		uint32_t column_count = index->def->key_def->part_count;
		struct index_stat *stat = &stats[current_idx_count];
		stat->sample_field_count = column_count;
		stat->sample_count = 0;
		/* Space for sample structs. */
		size_t alloc_size = sizeof(struct index_sample) * sample_count;
		/* Space for eq, lt and dlt stats. */
		alloc_size += sizeof(tRowcnt) * column_count * 3 * sample_count;
		/* Space for avg_eq statistics. */
		alloc_size += column_count * sizeof(tRowcnt);
		/*
		 * We are trying to fit into one chunk samples,
		 * eq_avg and arrays of eq, lt and dlt stats.
		 * Firstly comes memory for structs of samples,
		 * than array of eq_avg and finally arrays of
		 * eq, lt and dlt stats.
		 */
		stat->samples = region_alloc(&fiber()->gc, alloc_size);
		if (stat->samples == NULL) {
			diag_set(OutOfMemory, alloc_size, "region", "samples");
			rc = -1;
			goto finalize;
		}
		memset(stat->samples, 0, alloc_size);
		/* Marking memory manually. */
		tRowcnt *pSpace = (tRowcnt *) & stat->samples[sample_count];
		stat->avg_eq = pSpace;
		pSpace += column_count;
		for (uint32_t i = 0; i < sample_count; i++) {
			stat->samples[i].eq = pSpace;
			pSpace += column_count;
			stat->samples[i].lt = pSpace;
			pSpace += column_count;
			stat->samples[i].dlt = pSpace;
			pSpace += column_count;
		}
		assert(((u8 *) pSpace) - alloc_size == (u8 *) (stat->samples));
		indexes[current_idx_count] = index;
		assert(current_idx_count < index_count);
		current_idx_count++;

	}
	rc = sql_stmt_finalize(stmt);
	if (rc)
		goto finalize;
	rc = sql_stmt_compile(sql_select_load, -1, NULL, &stmt, 0);
	if (rc)
		goto finalize;
	struct index *prev_index = NULL;
	current_idx_count = 0;
	while (sql_step(stmt) == SQL_ROW) {
		const char *space_name = (char *)sql_column_text(stmt, 0);
		if (space_name == NULL)
			continue;
		const char *index_name = (char *)sql_column_text(stmt, 1);
		if (index_name == NULL)
			continue;
		struct space *space = space_by_name(space_name);
		assert(space != NULL);
		struct index *index;
		uint32_t iid = box_index_id_by_name(space->def->id, index_name,
						    strlen(index_name));
		if (iid != BOX_ID_NIL) {
			index = space_index(space, iid);
		} else {
			if (sql_stricmp(space_name, index_name) != 0)
				return -1;
			index = space_index(space, 0);
		}
		assert(index != NULL);
		uint32_t column_count = index->def->key_def->part_count;
		if (index != prev_index) {
			if (prev_index != NULL) {
				init_avg_eq(prev_index,
					    &stats[current_idx_count]);
				current_idx_count++;
			}
			prev_index = index;
		}
		struct index_stat *stat = &stats[current_idx_count];
		struct index_sample *sample =
			&stat->samples[stats[current_idx_count].sample_count];
		decode_stat_string((char *)sql_column_text(stmt, 2),
				   column_count, sample->eq, 0);
		decode_stat_string((char *)sql_column_text(stmt, 3),
				   column_count, sample->lt, 0);
		decode_stat_string((char *)sql_column_text(stmt, 4),
				   column_count, sample->dlt, 0);
		/* Take a copy of the sample. */
		sample->key_size = sql_column_bytes(stmt, 5);
		sample->sample_key = region_alloc(&fiber()->gc,
						  sample->key_size);
		if (sample->sample_key == NULL) {
			sql_stmt_finalize(stmt);
			rc = -1;
			diag_set(OutOfMemory, sample->key_size,
				 "region", "sample_key");
			goto finalize;
		}
		if (sample->key_size > 0) {
			memcpy(sample->sample_key,
			       sql_column_blob(stmt, 5),
			       sample->key_size);
		}
		stats[current_idx_count].sample_count++;
	}
	rc = sql_stmt_finalize(stmt);
	if (rc == 0 && prev_index != NULL)
		init_avg_eq(prev_index, &stats[current_idx_count]);
	assert(current_idx_count <= index_count);
	for (uint32_t i = 0; i < current_idx_count; ++i) {
		struct index *index = indexes[i];
		assert(index != NULL);
		qsort_arg(stats[i].samples,
			  stats[i].sample_count,
			  sizeof(struct index_sample),
			  sample_compare, index->def->key_def);
	}
 finalize:
	return rc;
}

static int
load_stat_to_index(const char *sql_select_load, struct index_stat **stats)
{
	assert(stats != NULL && *stats != NULL);
	struct sql_stmt *stmt = NULL;
	if (sql_stmt_compile(sql_select_load, -1, NULL, &stmt, 0) != 0)
		return -1;
	uint32_t current_idx_count = 0;
	while (sql_step(stmt) == SQL_ROW) {
		const char *space_name = (char *)sql_column_text(stmt, 0);
		if (space_name == NULL)
			continue;
		const char *index_name = (char *)sql_column_text(stmt, 1);
		if (index_name == NULL)
			continue;
		struct space *space = space_by_name(space_name);
		assert(space != NULL);
		struct index *index;
		uint32_t iid = box_index_id_by_name(space->def->id, index_name,
						    strlen(index_name));
		if (iid != BOX_ID_NIL) {
			index = space_index(space, iid);
		} else {
			if (sql_stricmp(space_name, index_name) != 0)
				return -1;
			index = space_index(space, 0);
		}
		assert(index != NULL);
		free(index->def->opts.stat);
		index->def->opts.stat = stats[current_idx_count++];
	}
	return 0;
}

/**
 * This function performs copy of statistics.
 * In contrast to index_stat_dup(), there is no assumption
 * that source statistics is allocated within chunk. But
 * destination place is still one big chunk of heap memory.
 * See also index_stat_sizeof() for understanding memory layout.
 *
 * @param dest One chunk of memory where statistics
 *             will be placed.
 * @param src Statistics to be copied.
 */
static void
stat_copy(struct index_stat *dest, const struct index_stat *src)
{
	assert(dest != NULL);
	assert(src != NULL);
	dest->sample_count = src->sample_count;
	dest->sample_field_count = src->sample_field_count;
	dest->skip_scan_enabled = src->skip_scan_enabled;
	dest->is_unordered = src->is_unordered;
	uint32_t array_size = src->sample_field_count * sizeof(uint32_t);
	uint32_t stat1_offset = sizeof(struct index_stat);
	char *pos = (char *) dest + stat1_offset;
	memcpy(pos, src->tuple_stat1, array_size + sizeof(uint32_t));
	dest->tuple_stat1 = (uint32_t *) pos;
	pos += array_size + sizeof(uint32_t);
	memcpy(pos, src->tuple_log_est, array_size + sizeof(uint32_t));
	dest->tuple_log_est = (log_est_t *) pos;
	pos += array_size + sizeof(uint32_t);
	memcpy(pos, src->avg_eq, array_size);
	dest->avg_eq = (uint32_t *) pos;
	pos += array_size;
	dest->samples = (struct index_sample *) pos;
	pos += dest->sample_count * sizeof(struct index_sample);
	for (uint32_t i = 0; i < dest->sample_count; ++i) {
		dest->samples[i].key_size = src->samples[i].key_size;
		memcpy(pos, src->samples[i].eq, array_size);
		dest->samples[i].eq = (uint32_t *) pos;
		pos += array_size;
		memcpy(pos, src->samples[i].lt, array_size);
		dest->samples[i].lt = (uint32_t *) pos;
		pos += array_size;
		memcpy(pos, src->samples[i].dlt, array_size);
		dest->samples[i].dlt = (uint32_t *) pos;
		pos += array_size;
		memcpy(pos, src->samples[i].sample_key,
		       src->samples[i].key_size);
		dest->samples[i].sample_key = pos;
		pos += dest->samples[i].key_size;
	}
}

int
sql_analysis_load(struct sql *db)
{
	struct space *stat_space = space_by_name("_sql_stat1");
	assert(stat_space != NULL);
	ssize_t index_count = box_index_len(stat_space->def->id, 0);
	if (index_count < 0)
		return -1;
	if (box_txn_begin() != 0)
		goto fail;
	size_t stats_size = index_count * sizeof(struct index_stat);
	struct index_stat *stats = region_alloc(&fiber()->gc, stats_size);
	if (stats == NULL) {
		diag_set(OutOfMemory, stats_size, "region", "stats");
		goto fail;
	}
	memset(stats, 0, stats_size);
	struct analysis_index_info info;
	info.db = db;
	info.stats = stats;
	info.index_count = 0;
	const char *load_stat1 =
		"SELECT \"tbl\",\"idx\",\"stat\" FROM \"_sql_stat1\"";
	/* Load new statistics out of the _sql_stat1 table. */
	if (sql_exec(db, load_stat1, analysis_loader, &info) != 0)
		goto fail;
	if (info.index_count == 0) {
		box_txn_commit();
		return 0;
	}
	/*
	 * This query is used to allocate enough memory for
	 * statistics. Result rows are given in a form:
	 * <table name>, <index name>, <count of samples>
	 */
	const char *init_query = "SELECT \"tbl\",\"idx\",count(*) FROM "
				 "\"_sql_stat4\" GROUP BY \"tbl\",\"idx\"";
	/* Query for loading statistics into in-memory structs. */
	const char *load_query = "SELECT \"tbl\",\"idx\",\"neq\",\"nlt\","
				 "\"ndlt\",\"sample\" FROM \"_sql_stat4\"";
	/* Load the statistics from the _sql_stat4 table. */
	if (load_stat_from_space(init_query, load_query, stats) != 0)
		goto fail;
	/*
	 * Now we have complete statistics for each index
	 * allocated on the region. Time to copy it on the heap.
	 */
	size_t heap_stats_size = info.index_count * sizeof(struct index_stat *);
	struct index_stat **heap_stats = region_alloc(&fiber()->gc,
						      heap_stats_size);
	if (heap_stats == NULL) {
		diag_set(OutOfMemory, heap_stats_size, "malloc", "heap_stats");
		goto fail;
	}
	/*
	 * We are using 'everything or nothing' policy:
	 * if there is no enough memory for statistics even for
	 * one index, then refresh it for no one.
	 */
	for (uint32_t i = 0; i < info.index_count; ++i) {
		size_t size = index_stat_sizeof(stats[i].samples,
						stats[i].sample_count,
						stats[i].sample_field_count);
		heap_stats[i] = malloc(size);
		if (heap_stats[i] == NULL) {
			diag_set(OutOfMemory, size, "malloc", "heap_stats");
			for (uint32_t j = 0; j < i; ++j)
				free(heap_stats[j]);
			goto fail;
		}
	}
	/*
	 * We can't use stat_dup function since statistics on
	 * region doesn't fit into one memory chunk. Lets
	 * manually copy memory chunks and mark memory.
	 */
	for (uint32_t i = 0; i < info.index_count; ++i)
		stat_copy(heap_stats[i], &stats[i]);
	/*
	 * Ordered query is needed to be sure that indexes come
	 * in the same order as in previous SELECTs.
	 */
	const char *order_query = "SELECT \"tbl\",\"idx\" FROM "
				  "\"_sql_stat4\" GROUP BY \"tbl\",\"idx\"";
	if (load_stat_to_index(order_query, heap_stats) == 0)
		return box_txn_commit();
fail:
	box_txn_rollback();
	return -1;
}

#endif
