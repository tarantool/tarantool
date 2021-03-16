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
 * This file contains code for the VdbeSorter object, used in concert with
 * a VdbeCursor to sort large numbers of keys for CREATE INDEX statements
 * or by SELECT statements with ORDER BY clauses that cannot be satisfied
 * using indexes and without LIMIT clauses.
 *
 * The VdbeSorter object implements a multi-threaded external merge sort
 * algorithm that is efficient even if the number of elements being sorted
 * exceeds the available memory.
 *
 * Here is the (internal, non-API) interface between this module and the
 * rest of the sql system:
 *
 *    sqlVdbeSorterInit()       Create a new VdbeSorter object.
 *
 *    sqlVdbeSorterWrite()      Add a single new row to the VdbeSorter
 *                                  object.  The row is a binary blob in the
 *                                  OP_MakeRecord format that contains both
 *                                  the ORDER BY key columns and result columns
 *                                  in the case of a SELECT w/ ORDER BY, or
 *                                  the complete record for an index entry
 *                                  in the case of a CREATE INDEX.
 *
 *    sqlVdbeSorterRewind()     Sort all content previously added.
 *                                  Position the read cursor on the
 *                                  first sorted element.
 *
 *    sqlVdbeSorterNext()       Advance the read cursor to the next sorted
 *                                  element.
 *
 *    sqlVdbeSorterRowkey()     Return the complete binary blob for the
 *                                  row currently under the read cursor.
 *
 *    sqlVdbeSorterCompare()    Compare the binary blob for the row
 *                                  currently under the read cursor against
 *                                  another binary blob X and report if
 *                                  X is strictly less than the read cursor.
 *                                  Used to enforce uniqueness in a
 *                                  CREATE UNIQUE INDEX statement.
 *
 *    sqlVdbeSorterClose()      Close the VdbeSorter object and reclaim
 *                                  all resources.
 *
 *    sqlVdbeSorterReset()      Refurbish the VdbeSorter for reuse.  This
 *                                  is like Close() followed by Init() only
 *                                  much faster.
 *
 * The interfaces above must be called in a particular order.  Write() can
 * only occur in between Init()/Reset() and Rewind().  Next(), Rowkey(), and
 * Compare() can only occur in between Rewind() and Close()/Reset(). i.e.
 *
 *   Init()
 *   for each record: Write()
 *   Rewind()
 *     Rowkey()/Compare()
 *   Next()
 *   Close()
 *
 * Algorithm:
 *
 * Records passed to the sorter via calls to Write() are initially held
 * unsorted in main memory. Assuming the amount of memory used never exceeds
 * a threshold, when Rewind() is called the set of records is sorted using
 * an in-memory merge sort. In this case, no temporary files are required
 * and subsequent calls to Rowkey(), Next() and Compare() read records
 * directly from main memory.
 *
 * If the amount of space used to store records in main memory exceeds the
 * threshold, then the set of records currently in memory are sorted and
 * written to a temporary file in "Packed Memory Array" (PMA) format.
 * A PMA created at this point is known as a "level-0 PMA". Higher levels
 * of PMAs may be created by merging existing PMAs together - for example
 * merging two or more level-0 PMAs together creates a level-1 PMA.
 *
 * The threshold for the amount of main memory to use before flushing
 * records to a PMA is roughly the same as the limit configured for the
 * page-cache of the main database. Specifically, the threshold is set to
 * the value returned by "PRAGMA main.page_size" multipled by
 * that returned by "PRAGMA main.cache_size", in bytes.
 *
 * If the sorter is running in single-threaded mode, then all PMAs generated
 * are appended to a single temporary file. Or, if the sorter is running in
 * multi-threaded mode then up to (N+1) temporary files may be opened, where
 * N is the configured number of worker threads. In this case, instead of
 * sorting the records and writing the PMA to a temporary file itself, the
 * calling thread usually launches a worker thread to do so. Except, if
 * there are already N worker threads running, the main thread does the work
 * itself.
 *
 * When Rewind() is called, any data remaining in memory is flushed to a
 * final PMA. So at this point the data is stored in some number of sorted
 * PMAs within temporary files on disk.
 *
 * If there are fewer than SORTER_MAX_MERGE_COUNT PMAs in total and the
 * sorter is running in single-threaded mode, then these PMAs are merged
 * incrementally as keys are retreived from the sorter by the VDBE.  The
 * MergeEngine object, described in further detail below, performs this
 * merge.
 *
 * Or, if running in multi-threaded mode, then a background thread is
 * launched to merge the existing PMAs. Once the background thread has
 * merged T bytes of data into a single sorted PMA, the main thread
 * begins reading keys from that PMA while the background thread proceeds
 * with merging the next T bytes of data. And so on.
 *
 * Parameter T is set to half the value of the memory threshold used
 * by Write() above to determine when to create a new PMA.
 *
 * If there are more than SORTER_MAX_MERGE_COUNT PMAs in total when
 * Rewind() is called, then a hierarchy of incremental-merges is used.
 * First, T bytes of data from the first SORTER_MAX_MERGE_COUNT PMAs on
 * disk are merged together. Then T bytes of data from the second set, and
 * so on, such that no operation ever merges more than SORTER_MAX_MERGE_COUNT
 * PMAs at a time. This done is to improve locality.
 *
 * If running in multi-threaded mode and there are more than
 * SORTER_MAX_MERGE_COUNT PMAs on disk when Rewind() is called, then more
 * than one background thread may be created. Specifically, there may be
 * one background thread for each temporary file on disk, and one background
 * thread to merge the output of each of the others to a single PMA for
 * the main thread to read from.
 */
#include "sqlInt.h"
#include "mem.h"
#include "vdbeInt.h"

/*
 * Hard-coded maximum amount of data to accumulate in memory before flushing
 * to a level 0 PMA. The purpose of this limit is to prevent various integer
 * overflows. 512MiB.
 */
#define SQL_MAX_PMASZ    (1<<29)

/*
 * Private objects used by the sorter
 */
typedef struct MergeEngine MergeEngine;	/* Merge PMAs together */
typedef struct PmaReader PmaReader;	/* Incrementally read one PMA */
typedef struct PmaWriter PmaWriter;	/* Incrementally write one PMA */
typedef struct SorterRecord SorterRecord;	/* A record being sorted */
typedef struct SortSubtask SortSubtask;	/* A sub-task in the sort process */
typedef struct SorterFile SorterFile;	/* Temporary file object wrapper */
typedef struct SorterList SorterList;	/* In-memory list of records */
typedef struct IncrMerger IncrMerger;	/* Read & merge multiple PMAs */

/*
 * A container for a temp file handle and the current amount of data
 * stored in the file.
 */
struct SorterFile {
	sql_file *pFd;	/* File handle */
	i64 iEof;		/* Bytes of data stored in pFd */
};

/*
 * An in-memory list of objects to be sorted.
 *
 * If aMemory==0 then each object is allocated separately and the objects
 * are connected using SorterRecord.u.pNext.  If aMemory!=0 then all objects
 * are stored in the aMemory[] bulk memory, one right after the other, and
 * are connected using SorterRecord.u.iNext.
 */
struct SorterList {
	SorterRecord *pList;	/* Linked list of records */
	u8 *aMemory;		/* If non-NULL, bulk memory to hold pList */
	int szPMA;		/* Size of pList as PMA in bytes */
};

/*
 * The MergeEngine object is used to combine two or more smaller PMAs into
 * one big PMA using a merge operation.  Separate PMAs all need to be
 * combined into one big PMA in order to be able to step through the sorted
 * records in order.
 *
 * The aReadr[] array contains a PmaReader object for each of the PMAs being
 * merged.  An aReadr[] object either points to a valid key or else is at EOF.
 * ("EOF" means "End Of File".  When aReadr[] is at EOF there is no more data.)
 * For the purposes of the paragraphs below, we assume that the array is
 * actually N elements in size, where N is the smallest power of 2 greater
 * to or equal to the number of PMAs being merged. The extra aReadr[] elements
 * are treated as if they are empty (always at EOF).
 *
 * The aTree[] array is also N elements in size. The value of N is stored in
 * the MergeEngine.nTree variable.
 *
 * The final (N/2) elements of aTree[] contain the results of comparing
 * pairs of PMA keys together. Element i contains the result of
 * comparing aReadr[2*i-N] and aReadr[2*i-N+1]. Whichever key is smaller, the
 * aTree element is set to the index of it.
 *
 * For the purposes of this comparison, EOF is considered greater than any
 * other key value. If the keys are equal (only possible with two EOF
 * values), it doesn't matter which index is stored.
 *
 * The (N/4) elements of aTree[] that precede the final (N/2) described
 * above contains the index of the smallest of each block of 4 PmaReaders
 * And so on. So that aTree[1] contains the index of the PmaReader that
 * currently points to the smallest key value. aTree[0] is unused.
 *
 * Example:
 *
 *     aReadr[0] -> Banana
 *     aReadr[1] -> Feijoa
 *     aReadr[2] -> Elderberry
 *     aReadr[3] -> Currant
 *     aReadr[4] -> Grapefruit
 *     aReadr[5] -> Apple
 *     aReadr[6] -> Durian
 *     aReadr[7] -> EOF
 *
 *     aTree[] = { X, 5   0, 5    0, 3, 5, 6 }
 *
 * The current element is "Apple" (the value of the key indicated by
 * PmaReader 5). When the Next() operation is invoked, PmaReader 5 will
 * be advanced to the next key in its segment. Say the next key is
 * "Eggplant":
 *
 *     aReadr[5] -> Eggplant
 *
 * The contents of aTree[] are updated first by comparing the new PmaReader
 * 5 key to the current key of PmaReader 4 (still "Grapefruit"). The PmaReader
 * 5 value is still smaller, so aTree[6] is set to 5. And so on up the tree.
 * The value of PmaReader 6 - "Durian" - is now smaller than that of PmaReader
 * 5, so aTree[3] is set to 6. Key 0 is smaller than key 6 (Banana<Durian),
 * so the value written into element 1 of the array is 0. As follows:
 *
 *     aTree[] = { X, 0   0, 6    0, 3, 5, 6 }
 *
 * In other words, each time we advance to the next sorter element, log2(N)
 * key comparison operations are required, where N is the number of segments
 * being merged (rounded up to the next power of 2).
 */
struct MergeEngine {
	int nTree;		/* Used size of aTree/aReadr (power of 2) */
	SortSubtask *pTask;	/* Used by this thread only */
	int *aTree;		/* Current state of incremental merge */
	PmaReader *aReadr;	/* Array of PmaReaders to merge data from */
};

/*
 * This object represents a single thread of control in a sort operation.
 * Exactly VdbeSorter.nTask instances of this object are allocated
 * as part of each VdbeSorter object. Instances are never allocated any
 * other way. VdbeSorter.nTask is set to the number of worker threads allowed
 * (see SQL_CONFIG_WORKER_THREADS) plus one (the main thread).  Thus for
 * single-threaded operation, there is exactly one instance of this object
 * and for multi-threaded operation there are two or more instances.
 *
 * Essentially, this structure contains all those fields of the VdbeSorter
 * structure for which each thread requires a separate instance. For example,
 * each thread requires its own UnpackedRecord object to unpack records in
 * as part of comparison operations.
 */
typedef int (*SorterCompare) (SortSubtask *, bool *, const void *,
			      const void *);
struct SortSubtask {
	VdbeSorter *pSorter;	/* Sorter that owns this sub-task */
	UnpackedRecord *pUnpacked;	/* Space to unpack a record */
	SorterList list;	/* List for thread to write to a PMA */
	int nPMA;		/* Number of PMAs currently in file */
	SorterCompare xCompare;	/* Compare function to use */
	SorterFile file;	/* Temp file for level-0 PMAs */
	SorterFile file2;	/* Space for other PMAs */
};

/*
 * Main sorter structure. A single instance of this is allocated for each
 * sorter cursor created by the VDBE.
 *
 * mxKeysize:
 *   As records are added to the sorter by calls to sqlVdbeSorterWrite(),
 *   this variable is updated so as to be set to the size on disk of the
 *   largest record in the sorter.
 */
struct VdbeSorter {
	int mnPmaSize;		/* Minimum PMA size, in bytes */
	int mxPmaSize;		/* Maximum PMA size, in bytes.  0==no limit */
	int mxKeysize;		/* Largest serialized key seen so far */
	int pgsz;		/* Main database page size */
	PmaReader *pReader;	/* Readr data from here after Rewind() */
	MergeEngine *pMerger;	/* Or here, if bUseThreads==0 */
	sql *db;		/* Database connection */
	struct key_def *key_def;
	UnpackedRecord *pUnpacked;	/* Used by VdbeSorterCompare() */
	SorterList list;	/* List of in-memory records */
	int iMemory;		/* Offset of free space in list.aMemory */
	int nMemory;		/* Size of list.aMemory allocation in bytes */
	u8 bUsePMA;		/* True if one or more PMAs created */
	u8 typeMask;
	SortSubtask aTask;	/* A single subtask */
};

#define SORTER_TYPE_INTEGER 0x01
#define SORTER_TYPE_TEXT    0x02

/*
 * An instance of the following object is used to read records out of a
 * PMA, in sorted order.  The next key to be read is cached in nKey/aKey.
 * aKey might point into aMap or into aBuffer.  If neither of those locations
 * contain a contiguous representation of the key, then aAlloc is allocated
 * and the key is copied into aAlloc and aKey is made to poitn to aAlloc.
 *
 * pFd==0 at EOF.
 */
struct PmaReader {
	i64 iReadOff;		/* Current read offset */
	i64 iEof;		/* 1 byte past EOF for this PmaReader */
	int nAlloc;		/* Bytes of space at aAlloc */
	int nKey;		/* Number of bytes in key */
	sql_file *pFd;	/* File handle we are reading from */
	u8 *aAlloc;		/* Space for aKey if aBuffer and pMap wont work */
	u8 *aKey;		/* Pointer to current key */
	u8 *aBuffer;		/* Current read buffer */
	int nBuffer;		/* Size of read buffer in bytes */
	u8 *aMap;		/* Pointer to mapping of entire file */
	IncrMerger *pIncr;	/* Incremental merger */
};

/*
 * Normally, a PmaReader object iterates through an existing PMA stored
 * within a temp file. However, if the PmaReader.pIncr variable points to
 * an object of the following type, it may be used to iterate/merge through
 * multiple PMAs simultaneously.
 *
 * There are two types of IncrMerger object - single (bUseThread==0) and
 * multi-threaded (bUseThread==1).
 *
 * A multi-threaded IncrMerger object uses two temporary files - aFile[0]
 * and aFile[1]. Neither file is allowed to grow to more than mxSz bytes in
 * size. When the IncrMerger is initialized, it reads enough data from
 * pMerger to populate aFile[0]. It then sets variables within the
 * corresponding PmaReader object to read from that file and kicks off
 * a background thread to populate aFile[1] with the next mxSz bytes of
 * sorted record data from pMerger.
 *
 * When the PmaReader reaches the end of aFile[0], it blocks until the
 * background thread has finished populating aFile[1]. It then exchanges
 * the contents of the aFile[0] and aFile[1] variables within this structure,
 * sets the PmaReader fields to read from the new aFile[0] and kicks off
 * another background thread to populate the new aFile[1]. And so on, until
 * the contents of pMerger are exhausted.
 *
 * A single-threaded IncrMerger does not open any temporary files of its
 * own. Instead, it has exclusive access to mxSz bytes of space beginning
 * at offset iStartOff of file pTask->file2. And instead of using a
 * background thread to prepare data for the PmaReader, with a single
 * threaded IncrMerger the allocate part of pTask->file2 is "refilled" with
 * keys from pMerger by the calling thread whenever the PmaReader runs out
 * of data.
 */
struct IncrMerger {
	SortSubtask *pTask;	/* Task that owns this merger */
	MergeEngine *pMerger;	/* Merge engine thread reads data from */
	i64 iStartOff;		/* Offset to start writing file at */
	int mxSz;		/* Maximum bytes of data to store */
	int bEof;		/* Set to true when merge is finished */
	int bUseThread;		/* True to use a bg thread for this object */
	SorterFile aFile[2];	/* aFile[0] for reading, [1] for writing */
};

/*
 * An instance of this object is used for writing a PMA.
 *
 * The PMA is written one record at a time.  Each record is of an arbitrary
 * size.  But I/O is more efficient if it occurs in page-sized blocks where
 * each block is aligned on a page boundary.  This object caches writes to
 * the PMA so that aligned, page-size blocks are written.
 */
struct PmaWriter {
	int eFWErr;		/* Non-zero if in an error state */
	u8 *aBuffer;		/* Pointer to write buffer */
	int nBuffer;		/* Size of write buffer in bytes */
	int iBufStart;		/* First byte of buffer to write */
	int iBufEnd;		/* Last byte of buffer to write */
	i64 iWriteOff;		/* Offset of start of buffer in file */
	sql_file *pFd;	/* File handle to write to */
};

/*
 * This object is the header on a single record while that record is being
 * held in memory and prior to being written out as part of a PMA.
 *
 * How the linked list is connected depends on how memory is being managed
 * by this module. If using a separate allocation for each in-memory record
 * (VdbeSorter.list.aMemory==0), then the list is always connected using the
 * SorterRecord.u.pNext pointers.
 *
 * Or, if using the single large allocation method (VdbeSorter.list.aMemory!=0),
 * then while records are being accumulated the list is linked using the
 * SorterRecord.u.iNext offset. This is because the aMemory[] array may
 * be sqlRealloc()ed while records are being accumulated. Once the VM
 * has finished passing records to the sorter, or when the in-memory buffer
 * is full, the list is sorted. As part of the sorting process, it is
 * converted to use the SorterRecord.u.pNext pointers. See function
 * vdbeSorterSort() for details.
 */
struct SorterRecord {
	int nVal;		/* Size of the record in bytes */
	union {
		SorterRecord *pNext;	/* Pointer to next record in list */
		int iNext;	/* Offset within aMemory of next record */
	} u;
	/* The data for the record immediately follows this header */
};

/* Return a pointer to the buffer containing the record data for SorterRecord
 * object p. Should be used as if:
 *
 *   void *SRVAL(SorterRecord *p) { return (void*)&p[1]; }
 */
#define SRVAL(p) ((void*)((SorterRecord*)(p) + 1))

/* Maximum number of PMAs that a single MergeEngine can merge */
#define SORTER_MAX_MERGE_COUNT 16

static int vdbeIncrSwap(IncrMerger *);
static void vdbeIncrFree(IncrMerger *);

/*
 * Free all memory belonging to the PmaReader object passed as the
 * argument. All structure fields are set to zero before returning.
 */
static void
vdbePmaReaderClear(PmaReader * pReadr)
{
	sql_free(pReadr->aAlloc);
	sql_free(pReadr->aBuffer);
	if (pReadr->aMap)
		sqlOsUnfetch(pReadr->pFd, 0, pReadr->aMap);
	vdbeIncrFree(pReadr->pIncr);
	memset(pReadr, 0, sizeof(PmaReader));
}

/*
 * Read the next nByte bytes of data from the PMA p.
 * If successful, set *ppOut to point to a buffer containing the data
 * and return 0. Otherwise, if an error occurs, return an sql
 * error code.
 *
 * The buffer returned in *ppOut is only valid until the
 * next call to this function.
 */
static int
vdbePmaReadBlob(PmaReader * p,	/* PmaReader from which to take the blob */
		int nByte,	/* Bytes of data to read */
		u8 ** ppOut	/* OUT: Pointer to buffer containing data */
    )
{
	int iBuf;		/* Offset within buffer to read from */
	int nAvail;		/* Bytes of data available in buffer */

	if (p->aMap) {
		*ppOut = &p->aMap[p->iReadOff];
		p->iReadOff += nByte;
		return 0;
	}

	assert(p->aBuffer);

	/* If there is no more data to be read from the buffer, read the next
	 * p->nBuffer bytes of data from the file into it. Or, if there are less
	 * than p->nBuffer bytes remaining in the PMA, read all remaining data.
	 */
	iBuf = p->iReadOff % p->nBuffer;
	if (iBuf == 0) {
		int nRead;	/* Bytes to read from disk */
		int rc;		/* sqlOsRead() return code */

		/* Determine how many bytes of data to read. */
		if ((p->iEof - p->iReadOff) > (i64) p->nBuffer) {
			nRead = p->nBuffer;
		} else {
			nRead = (int)(p->iEof - p->iReadOff);
		}
		assert(nRead > 0);

		/* Readr data from the file. Return early if an error occurs. */
		rc = sqlOsRead(p->pFd, p->aBuffer, nRead, p->iReadOff);
		if (rc != 0)
			return rc;
	}
	nAvail = p->nBuffer - iBuf;

	if (nByte <= nAvail) {
		/* The requested data is available in the in-memory buffer. In this
		 * case there is no need to make a copy of the data, just return a
		 * pointer into the buffer to the caller.
		 */
		*ppOut = &p->aBuffer[iBuf];
		p->iReadOff += nByte;
	} else {
		/* The requested data is not all available in the in-memory buffer.
		 * In this case, allocate space at p->aAlloc[] to copy the requested
		 * range into. Then return a copy of pointer p->aAlloc to the caller.
		 */
		int nRem;	/* Bytes remaining to copy */

		/* Extend the p->aAlloc[] allocation if required. */
		if (p->nAlloc < nByte) {
			u8 *aNew;
			int nNew = MAX(128, p->nAlloc * 2);
			while (nByte > nNew)
				nNew = nNew * 2;
			aNew = sqlRealloc(p->aAlloc, nNew);
			if (!aNew)
				return -1;
			p->nAlloc = nNew;
			p->aAlloc = aNew;
		}

		/* Copy as much data as is available in the buffer into the start of
		 * p->aAlloc[].
		 */
		memcpy(p->aAlloc, &p->aBuffer[iBuf], nAvail);
		p->iReadOff += nAvail;
		nRem = nByte - nAvail;

		/* The following loop copies up to p->nBuffer bytes per iteration into
		 * the p->aAlloc[] buffer.
		 */
		while (nRem > 0) {
			int rc;	/* vdbePmaReadBlob() return code */
			int nCopy;	/* Number of bytes to copy */
			u8 *aNext;	/* Pointer to buffer to copy data from */

			nCopy = nRem;
			if (nRem > p->nBuffer)
				nCopy = p->nBuffer;
			rc = vdbePmaReadBlob(p, nCopy, &aNext);
			if (rc != 0)
				return rc;
			assert(aNext != p->aAlloc);
			memcpy(&p->aAlloc[nByte - nRem], aNext, nCopy);
			nRem -= nCopy;
		}

		*ppOut = p->aAlloc;
	}

	return 0;
}

/*
 * Read a varint from the stream of data accessed by p. Set *pnOut to
 * the value read.
 */
static int
vdbePmaReadVarint(PmaReader * p, u64 * pnOut)
{
	int iBuf;

	if (p->aMap) {
		p->iReadOff += sqlGetVarint(&p->aMap[p->iReadOff], pnOut);
	} else {
		iBuf = p->iReadOff % p->nBuffer;
		if (iBuf && (p->nBuffer - iBuf) >= 9) {
			p->iReadOff +=
			    sqlGetVarint(&p->aBuffer[iBuf], pnOut);
		} else {
			u8 aVarint[16], *a;
			int i = 0, rc;
			do {
				rc = vdbePmaReadBlob(p, 1, &a);
				if (rc)
					return rc;
				aVarint[(i++) & 0xf] = a[0];
			} while ((a[0] & 0x80) != 0);
			sqlGetVarint(aVarint, pnOut);
		}
	}

	return 0;
}

/*
 * Attempt to memory map file pFile. If successful, set *pp to point to the
 * new mapping and return 0. If the mapping is not attempted
 * (because the file is too large or the VFS layer is configured not to use
 * mmap), return 0 and set *pp to NULL.
 *
 * Or, if an error occurs, return an sql error code. The final value of
 * *pp is undefined in this case.
 */
static int
vdbeSorterMapFile(SortSubtask * pTask, SorterFile * pFile, u8 ** pp)
{
	int rc = 0;
	if (pFile->iEof <= (i64) (pTask->pSorter->db->nMaxSorterMmap)) {
		sql_file *pFd = pFile->pFd;
		if (pFd->pMethods->iVersion >= 3) {
			rc = sqlOsFetch(pFd, 0, (int)pFile->iEof,
					    (void **)pp);
			testcase(rc != 0);
		}
	}
	return rc;
}

/*
 * Attach PmaReader pReadr to file pFile (if it is not already attached to
 * that file) and seek it to offset iOff within the file.  Return 0
 * if successful, or an sql error code if an error occurs.
 */
static int
vdbePmaReaderSeek(SortSubtask * pTask,	/* Task context */
		  PmaReader * pReadr,	/* Reader whose cursor is to be moved */
		  SorterFile * pFile,	/* Sorter file to read from */
		  i64 iOff	/* Offset in pFile */
    )
{
	int rc = 0;

	assert(pReadr->pIncr == 0 || pReadr->pIncr->bEof == 0);

	if (pReadr->aMap) {
		sqlOsUnfetch(pReadr->pFd, 0, pReadr->aMap);
		pReadr->aMap = 0;
	}
	pReadr->iReadOff = iOff;
	pReadr->iEof = pFile->iEof;
	pReadr->pFd = pFile->pFd;

	rc = vdbeSorterMapFile(pTask, pFile, &pReadr->aMap);
	if (rc == 0 && pReadr->aMap == NULL) {
		int pgsz = pTask->pSorter->pgsz;
		int iBuf = pReadr->iReadOff % pgsz;
		if (pReadr->aBuffer == 0) {
			pReadr->aBuffer = (u8 *) sqlMalloc(pgsz);
			if (pReadr->aBuffer == 0)
				rc = -1;
			pReadr->nBuffer = pgsz;
		}
		if (rc == 0 && iBuf != 0) {
			int nRead = pgsz - iBuf;
			if ((pReadr->iReadOff + nRead) > pReadr->iEof) {
				nRead = (int)(pReadr->iEof - pReadr->iReadOff);
			}
			rc = sqlOsRead(pReadr->pFd, &pReadr->aBuffer[iBuf],
					   nRead, pReadr->iReadOff);
			testcase(rc != 0);
		}
	}

	return rc;
}

/*
 * Advance PmaReader pReadr to the next key in its PMA. Return 0 if
 * no error occurs, or an sql error code if one does.
 */
static int
vdbePmaReaderNext(PmaReader * pReadr)
{
	int rc = 0;	/* Return Code */
	u64 nRec = 0;		/* Size of record in bytes */

	if (pReadr->iReadOff >= pReadr->iEof) {
		IncrMerger *pIncr = pReadr->pIncr;
		int bEof = 1;
		if (pIncr) {
			rc = vdbeIncrSwap(pIncr);
			if (rc == 0 && pIncr->bEof == 0) {
				rc = vdbePmaReaderSeek(pIncr->pTask, pReadr,
						       &pIncr->aFile[0],
						       pIncr->iStartOff);
				bEof = 0;
			}
		}

		if (bEof) {
			/* This is an EOF condition */
			vdbePmaReaderClear(pReadr);
			testcase(rc != 0);
			return rc;
		}
	}

	if (rc == 0)
		rc = vdbePmaReadVarint(pReadr, &nRec);
	if (rc == 0) {
		pReadr->nKey = (int)nRec;
		rc = vdbePmaReadBlob(pReadr, (int)nRec, &pReadr->aKey);
		testcase(rc != 0);
	}

	return rc;
}

/*
 * Initialize PmaReader pReadr to scan through the PMA stored in file pFile
 * starting at offset iStart and ending at offset iEof-1. This function
 * leaves the PmaReader pointing to the first key in the PMA (or EOF if the
 * PMA is empty).
 *
 * If the pnByte parameter is NULL, then it is assumed that the file
 * contains a single PMA, and that that PMA omits the initial length varint.
 */
static int
vdbePmaReaderInit(SortSubtask * pTask,	/* Task context */
		  SorterFile * pFile,	/* Sorter file to read from */
		  i64 iStart,	/* Start offset in pFile */
		  PmaReader * pReadr,	/* PmaReader to populate */
		  i64 * pnByte	/* IN/OUT: Increment this value by PMA size */
    )
{
	int rc;

	assert(pFile->iEof > iStart);
	assert(pReadr->aAlloc == 0 && pReadr->nAlloc == 0);
	assert(pReadr->aBuffer == 0);
	assert(pReadr->aMap == 0);

	rc = vdbePmaReaderSeek(pTask, pReadr, pFile, iStart);
	if (rc == 0) {
		u64 nByte = 0;	/* Size of PMA in bytes */
		rc = vdbePmaReadVarint(pReadr, &nByte);
		pReadr->iEof = pReadr->iReadOff + nByte;
		*pnByte += nByte;
	}

	if (rc == 0)
		rc = vdbePmaReaderNext(pReadr);
	return rc;
}

/**
 * Compare key1 with key2. Use (pTask->key_def) for the collation
 * sequences used by the comparison. Return the result of the
 * comparison.
 *
 * If IN/OUT parameter *pbKey2Cached is true when this function is called,
 * it is assumed that (pTask->pUnpacked) contains the unpacked version
 * of key2. If it is false, (pTask->pUnpacked) is populated with the unpacked
 * version of key2 and *pbKey2Cached set to true before returning.
 *
 * If an OOM error is encountered, (pTask->pUnpacked->error_rc) is set
 * to -1.
 *
 * @param task Subtask context (for key_def).
 * @param key2_cached True if pTask->pUnpacked is key2.
 * @param key1 Left side of comparison.
 * @param key2 Right side of comparison.
 *
 * @retval +1 if key1 > key2, -1 otherwise.
 */
static int
vdbeSorterCompare(struct SortSubtask *task, bool *key2_cached,
		  const void *key1, const void *key2)
{
	struct UnpackedRecord *r2 = task->pUnpacked;
	if (!*key2_cached) {
		sqlVdbeRecordUnpackMsgpack(task->pSorter->key_def,
					       key2, r2);
		*key2_cached = 1;
	}
	return sqlVdbeRecordCompareMsgpack(key1, r2);
}

/*
 * Initialize the temporary index cursor just opened as a sorter cursor.
 *
 * Usually, the sorter module uses the value of (pCsr->key_def->part_count)
 * to determine the number of fields that should be compared from the
 * records being sorted. However, if the value passed as argument nField
 * is non-zero and the sorter is able to guarantee a stable sort, nField
 * is used instead. This is used when sorting records for a CREATE INDEX
 * statement. In this case, keys are always delivered to the sorter in
 * order of the primary key, which happens to be make up the final part
 * of the records being sorted. So if the sort is stable, there is never
 * any reason to compare PK fields and they can be ignored for a small
 * performance boost.
 *
 * The sorter can guarantee a stable sort when running in single-threaded
 * mode, but not in multi-threaded mode.
 *
 * 0 is returned if successful, or an sql error code otherwise.
 */
int
sqlVdbeSorterInit(sql * db,	/* Database connection (for malloc()) */
		      VdbeCursor * pCsr	/* Cursor that holds the new sorter */
    )
{
	int pgsz;		/* Page size of main database */
	VdbeSorter *pSorter;	/* The new sorter */
	int rc = 0;

	assert(pCsr->key_def != NULL);
	assert(pCsr->eCurType == CURTYPE_SORTER);

	pSorter = (VdbeSorter *) sqlDbMallocZero(db, sizeof(VdbeSorter));
	pCsr->uc.pSorter = pSorter;
	if (pSorter == 0)
		return -1;

	pSorter->key_def = pCsr->key_def;
	pSorter->pgsz = pgsz = 1024;
	pSorter->db = db;
	pSorter->aTask.pSorter = pSorter;

	/* Cache size in bytes */
	i64 mxCache;
	u32 szPma = sqlGlobalConfig.szPma;
	pSorter->mnPmaSize = szPma * pgsz;

	mxCache = SQL_DEFAULT_CACHE_SIZE;
	mxCache = mxCache * -1024;
	mxCache = MIN(mxCache, SQL_MAX_PMASZ);
	pSorter->mxPmaSize = MAX(pSorter->mnPmaSize, (int)mxCache);
	assert(pSorter->iMemory == 0);
	pSorter->nMemory = pgsz;
	pSorter->list.aMemory = (u8 *) sqlMalloc(pgsz);
	if (!pSorter->list.aMemory)
		rc = -1;

	if (pCsr->key_def->part_count < 13 &&
	    pCsr->key_def->parts[0].coll == NULL)
		pSorter->typeMask = SORTER_TYPE_INTEGER | SORTER_TYPE_TEXT;

	return rc;
}

/*
 * Free the list of sorted records starting at pRecord.
 */
static void
vdbeSorterRecordFree(sql * db, SorterRecord * pRecord)
{
	SorterRecord *p;
	SorterRecord *pNext;
	for (p = pRecord; p; p = pNext) {
		pNext = p->u.pNext;
		sqlDbFree(db, p);
	}
}

/*
 * Free all resources owned by the object indicated by argument pTask. All
 * fields of *pTask are zeroed before returning.
 */
static void
vdbeSortSubtaskCleanup(sql * db, SortSubtask * pTask)
{
	sqlDbFree(db, pTask->pUnpacked);

	assert(pTask->list.aMemory == 0);
	vdbeSorterRecordFree(0, pTask->list.pList);

	if (pTask->file.pFd) {
		sqlOsCloseFree(pTask->file.pFd);
	}
	if (pTask->file2.pFd) {
		sqlOsCloseFree(pTask->file2.pFd);
	}
	memset(pTask, 0, sizeof(SortSubtask));
}

#define vdbeSorterJoinAll(x,rcin) (rcin)

/*
 * Allocate a new MergeEngine object capable of handling up to
 * nReader PmaReader inputs.
 *
 * nReader is automatically rounded up to the next power of two.
 * nReader may not exceed SORTER_MAX_MERGE_COUNT even after rounding up.
 */
static MergeEngine *
vdbeMergeEngineNew(int nReader)
{
	int N = 2;		/* Smallest power of two >= nReader */
	int nByte;		/* Total bytes of space to allocate */
	MergeEngine *pNew;	/* Pointer to allocated object to return */

	assert(nReader <= SORTER_MAX_MERGE_COUNT);

	while (N < nReader)
		N += N;
	nByte = sizeof(MergeEngine) + N * (sizeof(int) + sizeof(PmaReader));

	pNew = (MergeEngine *) sqlMallocZero(nByte);
	if (pNew) {
		pNew->nTree = N;
		pNew->pTask = 0;
		pNew->aReadr = (PmaReader *) & pNew[1];
		pNew->aTree = (int *)&pNew->aReadr[N];
	}
	return pNew;
}

/*
 * Free the MergeEngine object passed as the only argument.
 */
static void
vdbeMergeEngineFree(MergeEngine * pMerger)
{
	int i;
	if (pMerger) {
		for (i = 0; i < pMerger->nTree; i++) {
			vdbePmaReaderClear(&pMerger->aReadr[i]);
		}
	}
	sql_free(pMerger);
}

/*
 * Free all resources associated with the IncrMerger object indicated by
 * the first argument.
 */
static void
vdbeIncrFree(IncrMerger * pIncr)
{
	if (pIncr) {
		vdbeMergeEngineFree(pIncr->pMerger);
		sql_free(pIncr);
	}
}

/*
 * Reset a sorting cursor back to its original empty state.
 */
void
sqlVdbeSorterReset(sql * db, VdbeSorter * pSorter)
{
	(void)vdbeSorterJoinAll(pSorter, 0);
	assert(pSorter->pReader == 0);
	vdbeMergeEngineFree(pSorter->pMerger);
	pSorter->pMerger = 0;
	vdbeSortSubtaskCleanup(db, &pSorter->aTask);
	pSorter->aTask.pSorter = pSorter;
	if (pSorter->list.aMemory == 0) {
		vdbeSorterRecordFree(0, pSorter->list.pList);
	}
	pSorter->list.pList = 0;
	pSorter->list.szPMA = 0;
	pSorter->bUsePMA = 0;
	pSorter->iMemory = 0;
	pSorter->mxKeysize = 0;
	sqlDbFree(db, pSorter->pUnpacked);
	pSorter->pUnpacked = 0;
}

enum field_type
vdbe_sorter_get_field_type(struct VdbeSorter *sorter, uint32_t field_no)
{
	return sorter->key_def->parts[field_no].type;
}

/*
 * Free any cursor components allocated by sqlVdbeSorterXXX routines.
 */
void
sqlVdbeSorterClose(sql * db, VdbeCursor * pCsr)
{
	VdbeSorter *pSorter;
	assert(pCsr->eCurType == CURTYPE_SORTER);
	pSorter = pCsr->uc.pSorter;
	if (pSorter) {
		sqlVdbeSorterReset(db, pSorter);
		sql_free(pSorter->list.aMemory);
		sqlDbFree(db, pSorter);
		pCsr->uc.pSorter = 0;
	}
}

#if SQL_MAX_MMAP_SIZE>0
/*
 * The first argument is a file-handle open on a temporary file. The file
 * is guaranteed to be nByte bytes or smaller in size. This function
 * attempts to extend the file to nByte bytes in size and to ensure that
 * the VFS has memory mapped it.
 *
 * Whether or not the file does end up memory mapped of course depends on
 * the specific VFS implementation.
 */
static void
vdbeSorterExtendFile(sql * db, sql_file * pFd, i64 nByte)
{
	if (nByte <= (i64) (db->nMaxSorterMmap) && pFd->pMethods->iVersion >= 3) {
		void *p = 0;
		int chunksize = 4 * 1024;
		sqlOsFileControlHint(pFd, SQL_FCNTL_CHUNK_SIZE,
					 &chunksize);
		sqlOsFileControlHint(pFd, SQL_FCNTL_SIZE_HINT, &nByte);
		sqlOsFetch(pFd, 0, (int)nByte, &p);
		sqlOsUnfetch(pFd, 0, p);
	}
}
#else
#define vdbeSorterExtendFile(x,y,z)
#endif

/*
 * Allocate space for a file-handle and open a temporary file. If successful,
 * set *ppFd to point to the malloc'd file-handle and return 0.
 * Otherwise, set *ppFd to 0 and return an sql error code.
 */
static int
vdbeSorterOpenTempFile(sql * db,	/* Database handle doing sort */
		       i64 nExtend,	/* Attempt to extend file to this size */
		       sql_file ** ppFd)
{
	int rc;
	rc = sqlOsOpenMalloc(db->pVfs, 0, ppFd,
				 SQL_OPEN_READWRITE | SQL_OPEN_CREATE |
				 SQL_OPEN_EXCLUSIVE |
				 SQL_OPEN_DELETEONCLOSE, &rc);
	if (rc == 0) {
		i64 max = SQL_MAX_MMAP_SIZE;
		sqlOsFileControlHint(*ppFd, SQL_FCNTL_MMAP_SIZE,
					 (void *)&max);
		if (nExtend > 0) {
			vdbeSorterExtendFile(db, *ppFd, nExtend);
		}
	}
	return rc;
}

/*
 * If it has not already been allocated, allocate the UnpackedRecord
 * structure at pTask->pUnpacked. Return 0 if successful (or
 * if no allocation was required), or -1 otherwise.
 */
static int
vdbeSortAllocUnpacked(SortSubtask * pTask)
{
	if (pTask->pUnpacked == 0) {
		pTask->pUnpacked =
			sqlVdbeAllocUnpackedRecord(pTask->pSorter->db,
						       pTask->pSorter->key_def);
		if (pTask->pUnpacked == 0)
			return -1;
		pTask->pUnpacked->nField = pTask->pSorter->key_def->part_count;
	}
	return 0;
}

/*
 * Merge the two sorted lists p1 and p2 into a single list.
 */
static SorterRecord *
vdbeSorterMerge(SortSubtask * pTask,	/* Calling thread context */
		SorterRecord * p1,	/* First list to merge */
		SorterRecord * p2	/* Second list to merge */
    )
{
	SorterRecord *pFinal = 0;
	SorterRecord **pp = &pFinal;
	bool bCached = false;

	assert(p1 != 0 && p2 != 0);
	for (;;) {
		int res;
		res =
		    pTask->xCompare(pTask, &bCached, SRVAL(p1),
				    SRVAL(p2));

		if (res <= 0) {
			*pp = p1;
			pp = &p1->u.pNext;
			p1 = p1->u.pNext;
			if (p1 == 0) {
				*pp = p2;
				break;
			}
		} else {
			*pp = p2;
			pp = &p2->u.pNext;
			p2 = p2->u.pNext;
			bCached = 0;
			if (p2 == 0) {
				*pp = p1;
				break;
			}
		}
	}
	return pFinal;
}

/*
 * Return the SorterCompare function to compare values collected by the
 * sorter object passed as the only argument.
 */
static SorterCompare
vdbeSorterGetCompare(VdbeSorter * p)
{
	(void)p;
	return vdbeSorterCompare;
}

/*
 * Sort the linked list of records headed at pTask->pList. Return
 * 0 if successful, or an sql error code (i.e. -1) if
 * an error occurs.
 */
static int
vdbeSorterSort(SortSubtask * pTask, SorterList * pList)
{
	int i;
	SorterRecord **aSlot;
	SorterRecord *p;
	int rc;

	rc = vdbeSortAllocUnpacked(pTask);
	if (rc != 0)
		return rc;

	p = pList->pList;
	pTask->xCompare = vdbeSorterGetCompare(pTask->pSorter);

	aSlot =
	    (SorterRecord **) sqlMallocZero(64 * sizeof(SorterRecord *));
	if (!aSlot) {
		return -1;
	}

	while (p) {
		SorterRecord *pNext;
		if (pList->aMemory) {
			if ((u8 *) p == pList->aMemory) {
				pNext = 0;
			} else {
				assert(p->u.iNext <
				       sqlMallocSize(pList->aMemory));
				pNext =
				    (SorterRecord *) & pList->aMemory[p->u.
								      iNext];
			}
		} else {
			pNext = p->u.pNext;
		}

		p->u.pNext = 0;
		for (i = 0; aSlot[i]; i++) {
			p = vdbeSorterMerge(pTask, p, aSlot[i]);
			aSlot[i] = 0;
		}
		aSlot[i] = p;
		p = pNext;
	}

	p = 0;
	for (i = 0; i < 64; i++) {
		if (aSlot[i] == 0)
			continue;
		p = p ? vdbeSorterMerge(pTask, p, aSlot[i]) : aSlot[i];
	}
	pList->pList = p;

	sql_free(aSlot);
	return 0;
}

/*
 * Initialize a PMA-writer object.
 */
static void
vdbePmaWriterInit(sql_file * pFd,	/* File handle to write to */
		  PmaWriter * p,	/* Object to populate */
		  int nBuf,	/* Buffer size */
		  i64 iStart	/* Offset of pFd to begin writing at */
    )
{
	memset(p, 0, sizeof(PmaWriter));
	p->aBuffer = (u8 *) sqlMalloc(nBuf);
	if (!p->aBuffer) {
		p->eFWErr = -1;
	} else {
		p->iBufEnd = p->iBufStart = (iStart % nBuf);
		p->iWriteOff = iStart - p->iBufStart;
		p->nBuffer = nBuf;
		p->pFd = pFd;
	}
}

/*
 * Write nData bytes of data to the PMA. Return 0
 * if successful, or an sql error code if an error occurs.
 */
static void
vdbePmaWriteBlob(PmaWriter * p, u8 * pData, int nData)
{
	int nRem = nData;
	while (nRem > 0 && p->eFWErr == 0) {
		int nCopy = nRem;
		if (nCopy > (p->nBuffer - p->iBufEnd)) {
			nCopy = p->nBuffer - p->iBufEnd;
		}

		memcpy(&p->aBuffer[p->iBufEnd], &pData[nData - nRem], nCopy);
		p->iBufEnd += nCopy;
		if (p->iBufEnd == p->nBuffer) {
			p->eFWErr = sqlOsWrite(p->pFd,
						   &p->aBuffer[p->iBufStart],
						   p->iBufEnd - p->iBufStart,
						   p->iWriteOff + p->iBufStart);
			p->iBufStart = p->iBufEnd = 0;
			p->iWriteOff += p->nBuffer;
		}
		assert(p->iBufEnd < p->nBuffer);

		nRem -= nCopy;
	}
}

/*
 * Flush any buffered data to disk and clean up the PMA-writer object.
 * The results of using the PMA-writer after this call are undefined.
 * Return 0 if flushing the buffered data succeeds or is not
 * required. Otherwise, return an sql error code.
 *
 * Before returning, set *piEof to the offset immediately following the
 * last byte written to the file.
 */
static int
vdbePmaWriterFinish(PmaWriter * p, i64 * piEof)
{
	int rc;
	if (p->eFWErr == 0 && ALWAYS(p->aBuffer) && p->iBufEnd > p->iBufStart) {
		p->eFWErr = sqlOsWrite(p->pFd,
					   &p->aBuffer[p->iBufStart],
					   p->iBufEnd - p->iBufStart,
					   p->iWriteOff + p->iBufStart);
	}
	*piEof = (p->iWriteOff + p->iBufEnd);
	sql_free(p->aBuffer);
	rc = p->eFWErr;
	memset(p, 0, sizeof(PmaWriter));
	return rc;
}

/*
 * Write value iVal encoded as a varint to the PMA. Return
 * 0 if successful, or an sql error code if an error occurs.
 */
static void
vdbePmaWriteVarint(PmaWriter * p, u64 iVal)
{
	int nByte;
	u8 aByte[10];
	nByte = sqlPutVarint(aByte, iVal);
	vdbePmaWriteBlob(p, aByte, nByte);
}

/*
 * Write the current contents of in-memory linked-list pList to a level-0
 * PMA in the temp file belonging to sub-task pTask. Return 0 if
 * successful, or an sql error code otherwise.
 *
 * The format of a PMA is:
 *
 *     * A varint. This varint contains the total number of bytes of content
 *       in the PMA (not including the varint itself).
 *
 *     * One or more records packed end-to-end in order of ascending keys.
 *       Each record consists of a varint followed by a blob of data (the
 *       key). The varint is the number of bytes in the blob of data.
 */
static int
vdbeSorterListToPMA(SortSubtask * pTask, SorterList * pList)
{
	sql *db = pTask->pSorter->db;
	int rc = 0;	/* Return code */
	PmaWriter writer;	/* Object used to write to the file */

#ifdef SQL_DEBUG
	/* Set iSz to the expected size of file pTask->file after writing the PMA.
	 * This is used by an assert() statement at the end of this function.
	 */
	i64 iSz =
	    pList->szPMA + sqlVarintLen(pList->szPMA) + pTask->file.iEof;
#endif

	memset(&writer, 0, sizeof(PmaWriter));
	assert(pList->szPMA > 0);

	/* If the first temporary PMA file has not been opened, open it now. */
	if (pTask->file.pFd == 0) {
		rc = vdbeSorterOpenTempFile(db, 0, &pTask->file.pFd);
		assert(rc != 0 || pTask->file.pFd);
		assert(pTask->file.iEof == 0);
		assert(pTask->nPMA == 0);
	}

	/* Try to get the file to memory map */
	if (rc == 0) {
		vdbeSorterExtendFile(db, pTask->file.pFd,
				     pTask->file.iEof + pList->szPMA + 9);
	}

	/* Sort the list */
	if (rc == 0)
		rc = vdbeSorterSort(pTask, pList);

	if (rc == 0) {
		SorterRecord *p;
		SorterRecord *pNext = 0;

		vdbePmaWriterInit(pTask->file.pFd, &writer,
				  pTask->pSorter->pgsz, pTask->file.iEof);
		pTask->nPMA++;
		vdbePmaWriteVarint(&writer, pList->szPMA);
		for (p = pList->pList; p; p = pNext) {
			pNext = p->u.pNext;
			vdbePmaWriteVarint(&writer, p->nVal);
			vdbePmaWriteBlob(&writer, SRVAL(p), p->nVal);
			if (pList->aMemory == 0)
				sql_free(p);
		}
		pList->pList = p;
		rc = vdbePmaWriterFinish(&writer, &pTask->file.iEof);
	}

	assert(rc != 0 || pList->pList == NULL);
	assert(rc != 0 || pTask->file.iEof == iSz);
	return rc;
}

/*
 * Advance the MergeEngine to its next entry.
 * Set *pbEof to true there is no next entry because
 * the MergeEngine has reached the end of all its inputs.
 *
 * Return 0 if successful or an error code if an error occurs.
 */
static int
vdbeMergeEngineStep(MergeEngine * pMerger,	/* The merge engine to advance to the next row */
		    int *pbEof	/* Set TRUE at EOF.  Set false for more content */
    )
{
	int rc;
	int iPrev = pMerger->aTree[1];	/* Index of PmaReader to advance */
	SortSubtask *pTask = pMerger->pTask;

	/* Advance the current PmaReader */
	rc = vdbePmaReaderNext(&pMerger->aReadr[iPrev]);

	/* Update contents of aTree[] */
	if (rc == 0) {
		int i;		/* Index of aTree[] to recalculate */
		PmaReader *pReadr1;	/* First PmaReader to compare */
		PmaReader *pReadr2;	/* Second PmaReader to compare */
		bool bCached = false;

		/* Find the first two PmaReaders to compare. The one that was just
		 * advanced (iPrev) and the one next to it in the array.
		 */
		pReadr1 = &pMerger->aReadr[(iPrev & 0xFFFE)];
		pReadr2 = &pMerger->aReadr[(iPrev | 0x0001)];

		for (i = (pMerger->nTree + iPrev) / 2; i > 0; i = i / 2) {
			/* Compare pReadr1 and pReadr2. Store the result in variable iRes. */
			int iRes;
			if (pReadr1->pFd == 0) {
				iRes = +1;
			} else if (pReadr2->pFd == 0) {
				iRes = -1;
			} else {
				iRes = pTask->xCompare(pTask, &bCached,
						       pReadr1->aKey,
						       pReadr2->aKey);
			}

			/* If pReadr1 contained the smaller value, set aTree[i] to its index.
			 * Then set pReadr2 to the next PmaReader to compare to pReadr1. In this
			 * case there is no cache of pReadr2 in pTask->pUnpacked, so set
			 * pKey2 to point to the record belonging to pReadr2.
			 *
			 * Alternatively, if pReadr2 contains the smaller of the two values,
			 * set aTree[i] to its index and update pReadr1. If vdbeSorterCompare()
			 * was actually called above, then pTask->pUnpacked now contains
			 * a value equivalent to pReadr2. So set pKey2 to NULL to prevent
			 * vdbeSorterCompare() from decoding pReadr2 again.
			 *
			 * If the two values were equal, then the value from the oldest
			 * PMA should be considered smaller. The VdbeSorter.aReadr[] array
			 * is sorted from oldest to newest, so pReadr1 contains older values
			 * than pReadr2 iff (pReadr1<pReadr2).
			 */
			if (iRes < 0 || (iRes == 0 && pReadr1 < pReadr2)) {
				pMerger->aTree[i] =
				    (int)(pReadr1 - pMerger->aReadr);
				pReadr2 =
				    &pMerger->aReadr[pMerger->
						     aTree[i ^ 0x0001]];
				bCached = 0;
			} else {
				if (pReadr1->pFd)
					bCached = 0;
				pMerger->aTree[i] =
				    (int)(pReadr2 - pMerger->aReadr);
				pReadr1 =
				    &pMerger->aReadr[pMerger->
						     aTree[i ^ 0x0001]];
			}
		}
		*pbEof = (pMerger->aReadr[pMerger->aTree[1]].pFd == 0);
	}

	return rc;
}

/*
 * Flush the current contents of VdbeSorter.list to a new PMA, possibly
 * using a background thread.
 */
static int
vdbeSorterFlushPMA(VdbeSorter * pSorter)
{
	pSorter->bUsePMA = 1;
	return vdbeSorterListToPMA(&pSorter->aTask, &pSorter->list);
}

/*
 * Add a record to the sorter.
 */
int
sqlVdbeSorterWrite(const VdbeCursor * pCsr,	/* Sorter cursor */
		       Mem * pVal	/* Memory cell containing record */
    )
{
	VdbeSorter *pSorter;
	int rc = 0;	/* Return Code */
	SorterRecord *pNew;	/* New list element */
	int bFlush;		/* True to flush contents of memory to PMA */
	int nReq;		/* Bytes of memory required */
	int nPMA;		/* Bytes of PMA space required */
	int t;			/* serial type of first record field */

	assert(pCsr->eCurType == CURTYPE_SORTER);
	pSorter = pCsr->uc.pSorter;
	getVarint32((const u8 *)&pVal->z[1], t);
	if (t > 0 && t < 10 && t != 7) {
		pSorter->typeMask &= SORTER_TYPE_INTEGER;
	} else if (t > 10 && (t & 0x01)) {
		pSorter->typeMask &= SORTER_TYPE_TEXT;
	} else {
		pSorter->typeMask = 0;
	}

	assert(pSorter);

	/* Figure out whether or not the current contents of memory should be
	 * flushed to a PMA before continuing. If so, do so.
	 *
	 * If using the single large allocation mode (pSorter->aMemory!=0), then
	 * flush the contents of memory to a new PMA if (a) at least one value is
	 * already in memory and (b) the new value will not fit in memory.
	 *
	 * Or, if using separate allocations for each record, flush the contents
	 * of memory to a PMA if either of the following are true:
	 *
	 *   * The total memory allocated for the in-memory list is greater
	 *     than (page-size * cache-size), or
	 */
	nReq = pVal->n + sizeof(SorterRecord);
	nPMA = pVal->n + sqlVarintLen(pVal->n);
	if (pSorter->mxPmaSize) {
		if (pSorter->list.aMemory) {
			bFlush = pSorter->iMemory
			    && (pSorter->iMemory + nReq) > pSorter->mxPmaSize;
		} else {
			bFlush = ((pSorter->list.szPMA > pSorter->mxPmaSize));
		}
		if (bFlush) {
			rc = vdbeSorterFlushPMA(pSorter);
			pSorter->list.szPMA = 0;
			pSorter->iMemory = 0;
			assert(rc != 0 || pSorter->list.pList == NULL);
		}
	}

	pSorter->list.szPMA += nPMA;
	if (nPMA > pSorter->mxKeysize) {
		pSorter->mxKeysize = nPMA;
	}

	if (pSorter->list.aMemory) {
		int nMin = pSorter->iMemory + nReq;

		if (nMin > pSorter->nMemory) {
			u8 *aNew;
			int iListOff =
			    (u8 *) pSorter->list.pList - pSorter->list.aMemory;
			int nNew = pSorter->nMemory * 2;
			while (nNew < nMin)
				nNew = nNew * 2;
			if (nNew > pSorter->mxPmaSize)
				nNew = pSorter->mxPmaSize;
			if (nNew < nMin)
				nNew = nMin;

			aNew = sqlRealloc(pSorter->list.aMemory, nNew);
			if (!aNew)
				return -1;
			pSorter->list.pList = (SorterRecord *) & aNew[iListOff];
			pSorter->list.aMemory = aNew;
			pSorter->nMemory = nNew;
		}

		pNew =
		    (SorterRecord *) & pSorter->list.aMemory[pSorter->iMemory];
		pSorter->iMemory += ROUND8(nReq);
		if (pSorter->list.pList) {
			pNew->u.iNext =
			    (int)((u8 *) (pSorter->list.pList) -
				  pSorter->list.aMemory);
		}
	} else {
		pNew = (SorterRecord *) sqlMalloc(nReq);
		if (pNew == 0) {
			return -1;
		}
		pNew->u.pNext = pSorter->list.pList;
	}

	memcpy(SRVAL(pNew), pVal->z, pVal->n);
	pNew->nVal = pVal->n;
	pSorter->list.pList = pNew;

	return rc;
}

/*
 * Read keys from pIncr->pMerger and populate pIncr->aFile[1]. The format
 * of the data stored in aFile[1] is the same as that used by regular PMAs,
 * except that the number-of-bytes varint is omitted from the start.
 */
static int
vdbeIncrPopulate(IncrMerger * pIncr)
{
	int rc = 0;
	int rc2;
	i64 iStart = pIncr->iStartOff;
	SorterFile *pOut = &pIncr->aFile[1];
	SortSubtask *pTask = pIncr->pTask;
	MergeEngine *pMerger = pIncr->pMerger;
	PmaWriter writer;
	assert(pIncr->bEof == 0);

	vdbePmaWriterInit(pOut->pFd, &writer, pTask->pSorter->pgsz, iStart);
	while (rc == 0) {
		int dummy;
		PmaReader *pReader = &pMerger->aReadr[pMerger->aTree[1]];
		int nKey = pReader->nKey;
		i64 iEof = writer.iWriteOff + writer.iBufEnd;

		/* Check if the output file is full or if the input has been exhausted.
		 * In either case exit the loop.
		 */
		if (pReader->pFd == 0)
			break;
		if ((iEof + nKey + sqlVarintLen(nKey)) >
		    (iStart + pIncr->mxSz))
			break;

		/* Write the next key to the output. */
		vdbePmaWriteVarint(&writer, nKey);
		vdbePmaWriteBlob(&writer, pReader->aKey, nKey);
		assert(pIncr->pMerger->pTask == pTask);
		rc = vdbeMergeEngineStep(pIncr->pMerger, &dummy);
	}

	rc2 = vdbePmaWriterFinish(&writer, &pOut->iEof);
	if (rc == 0)
		rc = rc2;
	return rc;
}

/*
 * This function is called when the PmaReader corresponding to pIncr has
 * finished reading the contents of aFile[0]. Its purpose is to "refill"
 * aFile[0] such that the PmaReader should start rereading it from the
 * beginning.
 *
 * For single-threaded objects, this is accomplished by literally reading
 * keys from pIncr->pMerger and repopulating aFile[0].
 *
 * For multi-threaded objects, all that is required is to wait until the
 * background thread is finished (if it is not already) and then swap
 * aFile[0] and aFile[1] in place. If the contents of pMerger have not
 * been exhausted, this function also launches a new background thread
 * to populate the new aFile[1].
 *
 * 0 is returned on success, or an sql error code otherwise.
 */
static int
vdbeIncrSwap(IncrMerger * pIncr)
{
	int rc = vdbeIncrPopulate(pIncr);
	pIncr->aFile[0] = pIncr->aFile[1];
	if (pIncr->aFile[0].iEof == pIncr->iStartOff) {
		pIncr->bEof = 1;
	}

	return rc;
}

/*
 * Allocate and return a new IncrMerger object to read data from pMerger.
 *
 * If an OOM condition is encountered, return NULL. In this case free the
 * pMerger argument before returning.
 */
static int
vdbeIncrMergerNew(SortSubtask * pTask,	/* The thread that will be using the new IncrMerger */
		  MergeEngine * pMerger,	/* The MergeEngine that the IncrMerger will control */
		  IncrMerger ** ppOut	/* Write the new IncrMerger here */
    )
{
	int rc = 0;
	IncrMerger *pIncr = *ppOut =
		(IncrMerger *) sqlMallocZero(sizeof(*pIncr));
	if (pIncr) {
		pIncr->pMerger = pMerger;
		pIncr->pTask = pTask;
		pIncr->mxSz =
		    MAX(pTask->pSorter->mxKeysize + 9,
			pTask->pSorter->mxPmaSize / 2);
		pTask->file2.iEof += pIncr->mxSz;
	} else {
		vdbeMergeEngineFree(pMerger);
		rc = -1;
	}
	return rc;
}

/*
 * Recompute pMerger->aTree[iOut] by comparing the next keys on the
 * two PmaReaders that feed that entry.  Neither of the PmaReaders
 * are advanced.  This routine merely does the comparison.
 */
static void
vdbeMergeEngineCompare(MergeEngine * pMerger,	/* Merge engine containing PmaReaders to compare */
		       int iOut	/* Store the result in pMerger->aTree[iOut] */
    )
{
	int i1;
	int i2;
	int iRes;
	PmaReader *p1;
	PmaReader *p2;

	assert(iOut < pMerger->nTree && iOut > 0);

	if (iOut >= (pMerger->nTree / 2)) {
		i1 = (iOut - pMerger->nTree / 2) * 2;
		i2 = i1 + 1;
	} else {
		i1 = pMerger->aTree[iOut * 2];
		i2 = pMerger->aTree[iOut * 2 + 1];
	}

	p1 = &pMerger->aReadr[i1];
	p2 = &pMerger->aReadr[i2];

	if (p1->pFd == 0) {
		iRes = i2;
	} else if (p2->pFd == 0) {
		iRes = i1;
	} else {
		SortSubtask *pTask = pMerger->pTask;
		bool cached = false;
		int res;
		assert(pTask->pUnpacked != 0);	/* from vdbeSortSubtaskMain() */
		res =
		    pTask->xCompare(pTask, &cached, p1->aKey, p2->aKey);
		if (res <= 0) {
			iRes = i1;
		} else {
			iRes = i2;
		}
	}

	pMerger->aTree[iOut] = iRes;
}

/*
 * Forward reference required as the vdbeIncrMergeInit() and
 * vdbePmaReaderIncrInit() routines are called mutually recursively when
 * building a merge tree.
 */
static int vdbePmaReaderIncrInit(PmaReader * pReader);

/*
 * Initialize the MergeEngine object passed as the second argument. Once this
 * function returns, the first key of merged data may be read from the
 * MergeEngine object in the usual fashion.
 *
 *
 * Use
 * vdbePmaReaderIncrMergeInit() to initialize each PmaReader that feeds data
 * to pMerger.
 *
 * 0 is returned if successful, or an sql error code otherwise.
 */
static int
vdbeMergeEngineInit(SortSubtask * pTask,	/* Thread that will run pMerger */
		    MergeEngine * pMerger 	/* MergeEngine to initialize */
    )
{
	int rc = 0;	/* Return code */
	int i;			/* For looping over PmaReader objects */
	int nTree = pMerger->nTree;

	/* Verify that the MergeEngine is assigned to a single thread */
	assert(pMerger->pTask == 0);
	pMerger->pTask = pTask;

	for (i = 0; i < nTree; i++) {
		rc = vdbePmaReaderIncrInit(&pMerger->aReadr[i]);
		if (rc != 0)
			return rc;
	}

	for (i = pMerger->nTree - 1; i > 0; i--) {
		vdbeMergeEngineCompare(pMerger, i);
	}
	return 0;
}

/*
 * The PmaReader is guaranteed to be an
 * incremental-reader (pReadr->pIncr!=0). This function serves to open
 * and/or initialize the temp file related fields of the IncrMerge
 * object at (pReadr->pIncr).
 *
 * All PmaReaders
 * in the sub-tree headed by pReadr are also initialized. Data is
 * loaded into the buffers belonging to pReadr and it is set to point to
 * the first key in its range.
 *
 * 0 is returned if successful, or an sql error code otherwise.
 */
static int
vdbePmaReaderIncrMergeInit(PmaReader * pReadr)
{
	int rc = 0;
	IncrMerger *pIncr = pReadr->pIncr;
	SortSubtask *pTask = pIncr->pTask;
	sql *db = pTask->pSorter->db;

	rc = vdbeMergeEngineInit(pTask, pIncr->pMerger);

	/* Set up the required files for pIncr. A multi-theaded IncrMerge object
	 * requires two temp files to itself, whereas a single-threaded object
	 * only requires a region of pTask->file2.
	 */
	if (rc == 0) {
		int mxSz = pIncr->mxSz;
		if (pTask->file2.pFd == 0) {
			assert(pTask->file2.iEof > 0);
			rc = vdbeSorterOpenTempFile(db,
						    pTask->file2.iEof,
						    &pTask->file2.pFd);
			pTask->file2.iEof = 0;
		}
		if (rc == 0) {
			pIncr->aFile[1].pFd = pTask->file2.pFd;
			pIncr->iStartOff = pTask->file2.iEof;
			pTask->file2.iEof += mxSz;
		}
	}

	if (rc == 0) {
		rc = vdbePmaReaderNext(pReadr);
	}

	return rc;
}

/*
 * If the PmaReader passed as the first argument is not an incremental-reader
 * (if pReadr->pIncr==0), then this function is a no-op. Otherwise, it invokes
 * the vdbePmaReaderIncrMergeInit() function with the parameters passed to
 * this routine to initialize the incremental merge.
 *
 * If the IncrMerger object is multi-threaded (IncrMerger.bUseThread==1),
 * then a background thread is launched to call vdbePmaReaderIncrMergeInit().
 * Or, if the IncrMerger is single threaded, the same function is called
 * using the current thread.
 */
static int
vdbePmaReaderIncrInit(PmaReader * pReadr)
{
	IncrMerger *pIncr = pReadr->pIncr;	/* Incremental merger */
	int rc = 0;	/* Return code */
	if (pIncr) {
		rc = vdbePmaReaderIncrMergeInit(pReadr);
	}
	return rc;
}

/*
 * Allocate a new MergeEngine object to merge the contents of nPMA level-0
 * PMAs from pTask->file. If no error occurs, set *ppOut to point to
 * the new object and return 0. Or, if an error does occur, set *ppOut
 * to NULL and return an sql error code.
 *
 * When this function is called, *piOffset is set to the offset of the
 * first PMA to read from pTask->file. Assuming no error occurs, it is
 * set to the offset immediately following the last byte of the last
 * PMA before returning. If an error does occur, then the final value of
 * *piOffset is undefined.
 */
static int
vdbeMergeEngineLevel0(SortSubtask * pTask,	/* Sorter task to read from */
		      int nPMA,	/* Number of PMAs to read */
		      i64 * piOffset,	/* IN/OUT: Readr offset in pTask->file */
		      MergeEngine ** ppOut	/* OUT: New merge-engine */
    )
{
	MergeEngine *pNew;	/* Merge engine to return */
	i64 iOff = *piOffset;
	int i;
	int rc = 0;

	*ppOut = pNew = vdbeMergeEngineNew(nPMA);
	if (pNew == 0)
		rc = -1;

	for (i = 0; i < nPMA && rc == 0; i++) {
		i64 nDummy = 0;
		PmaReader *pReadr = &pNew->aReadr[i];
		rc = vdbePmaReaderInit(pTask, &pTask->file, iOff, pReadr,
				       &nDummy);
		iOff = pReadr->iEof;
	}

	if (rc != 0) {
		vdbeMergeEngineFree(pNew);
		*ppOut = 0;
	}
	*piOffset = iOff;
	return rc;
}

/*
 * Return the depth of a tree comprising nPMA PMAs, assuming a fanout of
 * SORTER_MAX_MERGE_COUNT. The returned value does not include leaf nodes.
 *
 * i.e.
 *
 *   nPMA<=16    -> TreeDepth() == 0
 *   nPMA<=256   -> TreeDepth() == 1
 *   nPMA<=65536 -> TreeDepth() == 2
 */
static int
vdbeSorterTreeDepth(int nPMA)
{
	int nDepth = 0;
	i64 nDiv = SORTER_MAX_MERGE_COUNT;
	while (nDiv < (i64) nPMA) {
		nDiv = nDiv * SORTER_MAX_MERGE_COUNT;
		nDepth++;
	}
	return nDepth;
}

/*
 * pRoot is the root of an incremental merge-tree with depth nDepth (according
 * to vdbeSorterTreeDepth()). pLeaf is the iSeq'th leaf to be added to the
 * tree, counting from zero. This function adds pLeaf to the tree.
 *
 * If successful, 0 is returned. If an error occurs, an sql error
 * code is returned and pLeaf is freed.
 */
static int
vdbeSorterAddToTree(SortSubtask * pTask,	/* Task context */
		    int nDepth,	/* Depth of tree according to TreeDepth() */
		    int iSeq,	/* Sequence number of leaf within tree */
		    MergeEngine * pRoot,	/* Root of tree */
		    MergeEngine * pLeaf	/* Leaf to add to tree */
    )
{
	int rc = 0;
	int nDiv = 1;
	int i;
	MergeEngine *p = pRoot;
	IncrMerger *pIncr;

	rc = vdbeIncrMergerNew(pTask, pLeaf, &pIncr);

	for (i = 1; i < nDepth; i++) {
		nDiv = nDiv * SORTER_MAX_MERGE_COUNT;
	}

	for (i = 1; i < nDepth && rc == 0; i++) {
		int iIter = (iSeq / nDiv) % SORTER_MAX_MERGE_COUNT;
		PmaReader *pReadr = &p->aReadr[iIter];

		if (pReadr->pIncr == 0) {
			MergeEngine *pNew =
			    vdbeMergeEngineNew(SORTER_MAX_MERGE_COUNT);
			if (pNew == 0) {
				rc = -1;
			} else {
				rc = vdbeIncrMergerNew(pTask, pNew,
						       &pReadr->pIncr);
			}
		}
		if (rc == 0) {
			p = pReadr->pIncr->pMerger;
			nDiv = nDiv / SORTER_MAX_MERGE_COUNT;
		}
	}

	if (rc == 0) {
		p->aReadr[iSeq % SORTER_MAX_MERGE_COUNT].pIncr = pIncr;
	} else {
		vdbeIncrFree(pIncr);
	}
	return rc;
}

/*
 * This function is called as part of a SorterRewind() operation on a sorter
 * that has already written two or more level-0 PMAs to one or more temp
 * files. It builds a tree of MergeEngine/IncrMerger/PmaReader objects that
 * can be used to incrementally merge all PMAs on disk.
 *
 * If successful, 0 is returned and *ppOut set to point to the
 * MergeEngine object at the root of the tree before returning. Or, if an
 * error occurs, an sql error code is returned and the final value
 * of *ppOut is undefined.
 */
static int
vdbeSorterMergeTreeBuild(VdbeSorter * pSorter,	/* The VDBE cursor that implements the sort */
			 MergeEngine ** ppOut	/* Write the MergeEngine here */
    )
{
	MergeEngine *pMain = 0;
	int rc = 0;

	SortSubtask *pTask = &pSorter->aTask;
	assert(pTask->nPMA > 0);
	if (pTask->nPMA) {
		MergeEngine *pRoot = 0;	/* Root node of tree for this task */
		int nDepth = vdbeSorterTreeDepth(pTask->nPMA);
		i64 iReadOff = 0;

		if (pTask->nPMA <= SORTER_MAX_MERGE_COUNT) {
			rc = vdbeMergeEngineLevel0(pTask, pTask->nPMA,
						   &iReadOff, &pRoot);
		} else {
			int i;
			int iSeq = 0;
			pRoot =
			    vdbeMergeEngineNew(SORTER_MAX_MERGE_COUNT);
			if (pRoot == 0)
				rc = -1;
			for (i = 0; i < pTask->nPMA && rc == 0;
			     i += SORTER_MAX_MERGE_COUNT) {
				MergeEngine *pMerger = 0;	/* New level-0 PMA merger */
				int nReader;	/* Number of level-0 PMAs to merge */

				nReader =
				    MIN(pTask->nPMA - i,
					SORTER_MAX_MERGE_COUNT);
				rc = vdbeMergeEngineLevel0(pTask,
							   nReader,
							   &iReadOff,
							   &pMerger);
				if (rc == 0) {
					rc = vdbeSorterAddToTree(pTask,
								 nDepth,
								 iSeq++,
								 pRoot,
								 pMerger);
				}
			}
		}

		if (rc == 0) {
			assert(pMain == 0);
			pMain = pRoot;
		} else {
			vdbeMergeEngineFree(pRoot);
		}
	}

	if (rc != 0) {
		vdbeMergeEngineFree(pMain);
		pMain = 0;
	}
	*ppOut = pMain;
	return rc;
}

/*
 * This function is called as part of an sqlVdbeSorterRewind() operation
 * on a sorter that has written two or more PMAs to temporary files. It sets
 * up either VdbeSorter.pMerger (for single threaded sorters) or pReader
 * (for multi-threaded sorters) so that it can be used to iterate through
 * all records stored in the sorter.
 *
 * 0 is returned if successful, or an sql error code otherwise.
 */
static int
vdbeSorterSetupMerge(VdbeSorter * pSorter)
{
	int rc;			/* Return code */
	MergeEngine *pMain = 0;

	rc = vdbeSorterMergeTreeBuild(pSorter, &pMain);
	if (rc == 0) {
		rc = vdbeMergeEngineInit(&pSorter->aTask, pMain);
		pSorter->pMerger = pMain;
		pMain = 0;
	}

	if (rc != 0)
		vdbeMergeEngineFree(pMain);
	return rc;
}

/*
 * Once the sorter has been populated by calls to sqlVdbeSorterWrite,
 * this function is called to prepare for iterating through the records
 * in sorted order.
 */
int
sqlVdbeSorterRewind(const VdbeCursor * pCsr, int *pbEof)
{
	VdbeSorter *pSorter;
	int rc = 0;	/* Return code */

	assert(pCsr->eCurType == CURTYPE_SORTER);
	pSorter = pCsr->uc.pSorter;
	assert(pSorter);

	/* If no data has been written to disk, then do not do so now. Instead,
	 * sort the VdbeSorter.pRecord list. The vdbe layer will read data directly
	 * from the in-memory list.
	 */
	if (pSorter->bUsePMA == 0) {
		if (pSorter->list.pList) {
			*pbEof = 0;
			rc = vdbeSorterSort(&pSorter->aTask, &pSorter->list);
		} else {
			*pbEof = 1;
		}
		return rc;
	}

	/* Write the current in-memory list to a PMA. When the VdbeSorterWrite()
	 * function flushes the contents of memory to disk, it immediately always
	 * creates a new list consisting of a single key immediately afterwards.
	 * So the list is never empty at this point.
	 */
	assert(pSorter->list.pList);
	rc = vdbeSorterFlushPMA(pSorter);

	/* Join all threads */
	rc = vdbeSorterJoinAll(pSorter, rc);

	/* Assuming no errors have occurred, set up a merger structure to
	 * incrementally read and merge all remaining PMAs.
	 */
	assert(pSorter->pReader == 0);
	if (rc == 0) {
		rc = vdbeSorterSetupMerge(pSorter);
		*pbEof = 0;
	}

	return rc;
}

/*
 * Advance to the next element in the sorter.
 */
int
sqlVdbeSorterNext(sql * db, const VdbeCursor * pCsr, int *pbEof)
{
	VdbeSorter *pSorter;
	int rc;			/* Return code */

	assert(pCsr->eCurType == CURTYPE_SORTER);
	pSorter = pCsr->uc.pSorter;
	assert(pSorter->bUsePMA
	       || (pSorter->pReader == 0 && pSorter->pMerger == 0));
	if (pSorter->bUsePMA) {
		assert(pSorter->pReader == 0 || pSorter->pMerger == 0);
		assert(pSorter->pMerger);
		assert(pSorter->pMerger->pTask == &pSorter->aTask);
		rc = vdbeMergeEngineStep(pSorter->pMerger, pbEof);
	} else {
		SorterRecord *pFree = pSorter->list.pList;
		pSorter->list.pList = pFree->u.pNext;
		pFree->u.pNext = 0;
		if (pSorter->list.aMemory == 0)
			vdbeSorterRecordFree(db, pFree);
		*pbEof = !pSorter->list.pList;
		rc = 0;
	}
	return rc;
}

/*
 * Return a pointer to a buffer owned by the sorter that contains the
 * current key.
 */
static void *
vdbeSorterRowkey(const VdbeSorter * pSorter,	/* Sorter object */
		 int *pnKey	/* OUT: Size of current key in bytes */
    )
{
	void *pKey;
	if (pSorter->bUsePMA) {
		PmaReader *pReader;
			/*if( !pSorter->bUseThreads ) */  {
			pReader =
			    &pSorter->pMerger->aReadr[pSorter->pMerger->
						      aTree[1]];
			}
		*pnKey = pReader->nKey;
		pKey = pReader->aKey;
	} else {
		*pnKey = pSorter->list.pList->nVal;
		pKey = SRVAL(pSorter->list.pList);
	}
	return pKey;
}

/*
 * Copy the current sorter key into the memory cell pOut.
 */
int
sqlVdbeSorterRowkey(const VdbeCursor * pCsr, Mem * pOut)
{
	VdbeSorter *pSorter;
	void *pKey;
	int nKey;		/* Sorter key to copy into pOut */

	assert(pCsr->eCurType == CURTYPE_SORTER);
	pSorter = pCsr->uc.pSorter;
	pKey = vdbeSorterRowkey(pSorter, &nKey);
	if (mem_copy_bin(pOut, pKey, nKey) != 0)
		return -1;

	return 0;
}

/*
 * Compare the key in memory cell pVal with the key that the sorter cursor
 * passed as the first argument currently points to. For the purposes of
 * the comparison, ignore the rowid field at the end of each record.
 *
 * If the sorter cursor key contains any NULL values, consider it to be
 * less than pVal. Even if pVal also contains NULL values.
 *
 * If an error occurs, return -1.
 * Otherwise, set *pRes to a negative, zero or positive value if the
 * key in pVal is smaller than, equal to or larger than the current sorter
 * key.
 *
 * This routine forms the core of the OP_SorterCompare opcode, which in
 * turn is used to verify uniqueness when constructing a UNIQUE INDEX.
 */
int
sqlVdbeSorterCompare(const VdbeCursor * pCsr,	/* Sorter cursor */
			 Mem * pVal,	/* Value to compare to current sorter key */
			 int nKeyCol,	/* Compare this many columns */
			 int *pRes	/* OUT: Result of comparison */
    )
{
	VdbeSorter *pSorter;
	UnpackedRecord *r2;
	int i;
	void *pKey;
	int nKey;		/* Sorter key to compare pVal with */

	assert(pCsr->eCurType == CURTYPE_SORTER);
	pSorter = pCsr->uc.pSorter;
	r2 = pSorter->pUnpacked;
	if (r2 == 0) {
		r2 = pSorter->pUnpacked =
			sqlVdbeAllocUnpackedRecord(pSorter->db,  pCsr->key_def);
		if (r2 == 0)
			return -1;
		r2->nField = nKeyCol;
	}
	assert(r2->nField == nKeyCol);

	pKey = vdbeSorterRowkey(pSorter, &nKey);
	sqlVdbeRecordUnpackMsgpack(pCsr->key_def, pKey, r2);
	for (i = 0; i < nKeyCol; i++) {
		if (mem_is_null(&r2->aMem[i])) {
			*pRes = -1;
			return 0;
		}
	}

	*pRes = sqlVdbeRecordCompareMsgpack(pVal->z, r2);
	return 0;
}
