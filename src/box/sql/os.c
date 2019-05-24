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
 *
 * This file contains OS interface code that is common to all
 * architectures.
 */
#include "sqlInt.h"

/*
 * The following routines are convenience wrappers around methods
 * of the sql_file object.  This is mostly just syntactic sugar. All
 * of this would be completely automatic if sql were coded using
 * C++ instead of plain old C.
 */
void
sqlOsClose(sql_file * pId)
{
	if (pId->pMethods) {
		pId->pMethods->xClose(pId);
		pId->pMethods = 0;
	}
}

int
sqlOsRead(sql_file * id, void *pBuf, int amt, i64 offset)
{
	return id->pMethods->xRead(id, pBuf, amt, offset);
}

int
sqlOsWrite(sql_file * id, const void *pBuf, int amt, i64 offset)
{
	return id->pMethods->xWrite(id, pBuf, amt, offset);
}

void
sqlOsFileControlHint(sql_file * id, int op, void *pArg)
{
	(void)id->pMethods->xFileControl(id, op, pArg);
}

#if SQL_MAX_MMAP_SIZE>0
/* The real implementation of xFetch and xUnfetch */
int
sqlOsFetch(sql_file * id, i64 iOff, int iAmt, void **pp)
{
	return id->pMethods->xFetch(id, iOff, iAmt, pp);
}

int
sqlOsUnfetch(sql_file * id, i64 iOff, void *p)
{
	return id->pMethods->xUnfetch(id, iOff, p);
}
#else
/* No-op stubs to use when memory-mapped I/O is disabled */
int
sqlOsFetch(MAYBE_UNUSED sql_file * id,
	       MAYBE_UNUSED i64 iOff,
	       MAYBE_UNUSED int iAmt, void **pp)
{
	*pp = 0;
	return 0;
}

int
sqlOsUnfetch(MAYBE_UNUSED sql_file * id,
		 MAYBE_UNUSED i64 iOff,
		 MAYBE_UNUSED void *p)
{
	return 0;
}
#endif

/*
 * The next group of routines are convenience wrappers around the
 * VFS methods.
 */
int
sqlOsOpen(sql_vfs * pVfs,
	      const char *zPath,
	      sql_file * pFile, int flags, int *pFlagsOut)
{
	int rc;
	/* 0x87f7f is a mask of SQL_OPEN_ flags that are valid to be passed
	 * down into the VFS layer.  Some SQL_OPEN_ flags (for example,
	 * SQL_OPEN_SHAREDCACHE) are blocked before
	 * reaching the VFS.
	 */
	rc = pVfs->xOpen(pVfs, zPath, pFile, flags & 0x87f7f, pFlagsOut);
	assert(rc == 0 || pFile->pMethods == 0);
	return rc;
}

int
sqlOsRandomness(sql_vfs * pVfs, int nByte, char *zBufOut)
{
	return pVfs->xRandomness(pVfs, nByte, zBufOut);
}

int
sqlOsCurrentTimeInt64(sql_vfs * pVfs, sql_int64 * pTimeOut)
{
	int rc;
	/* IMPLEMENTATION-OF: R-49045-42493 sql will use the xCurrentTimeInt64()
	 * method to get the current date and time if that method is available
	 * (if iVersion is 2 or greater and the function pointer is not NULL) and
	 * will fall back to xCurrentTime() if xCurrentTimeInt64() is
	 * unavailable.
	 */
	if (pVfs->iVersion >= 2 && pVfs->xCurrentTimeInt64) {
		rc = pVfs->xCurrentTimeInt64(pVfs, pTimeOut);
	} else {
		double r;
		rc = pVfs->xCurrentTime(pVfs, &r);
		*pTimeOut = (sql_int64) (r * 86400000.0);
	}
	return rc;
}

int
sqlOsOpenMalloc(sql_vfs * pVfs,
		    const char *zFile,
		    sql_file ** ppFile, int flags, int *pOutFlags)
{
	int rc;
	sql_file *pFile;
	pFile = (sql_file *) sqlMallocZero(pVfs->szOsFile);
	if (pFile) {
		rc = sqlOsOpen(pVfs, zFile, pFile, flags, pOutFlags);
		if (rc != 0) {
			sql_free(pFile);
		} else {
			*ppFile = pFile;
		}
	} else {
		rc = -1;
	}
	return rc;
}

void
sqlOsCloseFree(sql_file * pFile)
{
	assert(pFile);
	sqlOsClose(pFile);
	sql_free(pFile);
}

/*
 * The list of all registered VFS implementations.
 */
static sql_vfs *SQL_WSD vfsList = 0;
#define vfsList GLOBAL(sql_vfs *, vfsList)

/*
 * Locate a VFS by name.  If no name is given, simply return the
 * first VFS on the list.
 */
sql_vfs *
sql_vfs_find(const char *zVfs)
{
	sql_vfs *pVfs = 0;
	for (pVfs = vfsList; pVfs; pVfs = pVfs->pNext) {
		if (zVfs == 0)
			break;
		if (strcmp(zVfs, pVfs->zName) == 0)
			break;
	}
	return pVfs;
}

/*
 * Unlink a VFS from the linked list
 */
static void
vfsUnlink(sql_vfs * pVfs)
{
	if (pVfs == 0) {
		/* No-op */
	} else if (vfsList == pVfs) {
		vfsList = pVfs->pNext;
	} else if (vfsList) {
		sql_vfs *p = vfsList;
		while (p->pNext && p->pNext != pVfs) {
			p = p->pNext;
		}
		if (p->pNext == pVfs) {
			p->pNext = pVfs->pNext;
		}
	}
}

/*
 * Register a VFS with the system.  It is harmless to register the same
 * VFS multiple times.  The new VFS becomes the default if makeDflt is
 * true.
 */
int
sql_vfs_register(sql_vfs * pVfs, int makeDflt)
{

	vfsUnlink(pVfs);
	if (makeDflt || vfsList == 0) {
		pVfs->pNext = vfsList;
		vfsList = pVfs;
	} else {
		pVfs->pNext = vfsList->pNext;
		vfsList->pNext = pVfs;
	}
	assert(vfsList);
	return 0;
}
