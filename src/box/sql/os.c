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
#include "sqliteInt.h"

/*
 * The following routines are convenience wrappers around methods
 * of the sqlite3_file object.  This is mostly just syntactic sugar. All
 * of this would be completely automatic if SQLite were coded using
 * C++ instead of plain old C.
 */
void
sqlite3OsClose(sqlite3_file * pId)
{
	if (pId->pMethods) {
		pId->pMethods->xClose(pId);
		pId->pMethods = 0;
	}
}

int
sqlite3OsRead(sqlite3_file * id, void *pBuf, int amt, i64 offset)
{
	return id->pMethods->xRead(id, pBuf, amt, offset);
}

int
sqlite3OsWrite(sqlite3_file * id, const void *pBuf, int amt, i64 offset)
{
	return id->pMethods->xWrite(id, pBuf, amt, offset);
}

void
sqlite3OsFileControlHint(sqlite3_file * id, int op, void *pArg)
{
	(void)id->pMethods->xFileControl(id, op, pArg);
}

#if SQLITE_MAX_MMAP_SIZE>0
/* The real implementation of xFetch and xUnfetch */
int
sqlite3OsFetch(sqlite3_file * id, i64 iOff, int iAmt, void **pp)
{
	return id->pMethods->xFetch(id, iOff, iAmt, pp);
}

int
sqlite3OsUnfetch(sqlite3_file * id, i64 iOff, void *p)
{
	return id->pMethods->xUnfetch(id, iOff, p);
}
#else
/* No-op stubs to use when memory-mapped I/O is disabled */
int
sqlite3OsFetch(MAYBE_UNUSED sqlite3_file * id,
	       MAYBE_UNUSED i64 iOff,
	       MAYBE_UNUSED int iAmt, void **pp)
{
	*pp = 0;
	return SQLITE_OK;
}

int
sqlite3OsUnfetch(MAYBE_UNUSED sqlite3_file * id,
		 MAYBE_UNUSED i64 iOff,
		 MAYBE_UNUSED void *p)
{
	return SQLITE_OK;
}
#endif

/*
 * The next group of routines are convenience wrappers around the
 * VFS methods.
 */
int
sqlite3OsOpen(sqlite3_vfs * pVfs,
	      const char *zPath,
	      sqlite3_file * pFile, int flags, int *pFlagsOut)
{
	int rc;
	/* 0x87f7f is a mask of SQLITE_OPEN_ flags that are valid to be passed
	 * down into the VFS layer.  Some SQLITE_OPEN_ flags (for example,
	 * SQLITE_OPEN_SHAREDCACHE) are blocked before
	 * reaching the VFS.
	 */
	rc = pVfs->xOpen(pVfs, zPath, pFile, flags & 0x87f7f, pFlagsOut);
	assert(rc == SQLITE_OK || pFile->pMethods == 0);
	return rc;
}

int
sqlite3OsRandomness(sqlite3_vfs * pVfs, int nByte, char *zBufOut)
{
	return pVfs->xRandomness(pVfs, nByte, zBufOut);
}

int
sqlite3OsSleep(sqlite3_vfs * pVfs, int nMicro)
{
	return pVfs->xSleep(pVfs, nMicro);
}

int
sqlite3OsGetLastError(sqlite3_vfs * pVfs)
{
	return pVfs->xGetLastError ? pVfs->xGetLastError(pVfs, 0, 0) : 0;
}

int
sqlite3OsCurrentTimeInt64(sqlite3_vfs * pVfs, sqlite3_int64 * pTimeOut)
{
	int rc;
	/* IMPLEMENTATION-OF: R-49045-42493 SQLite will use the xCurrentTimeInt64()
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
		*pTimeOut = (sqlite3_int64) (r * 86400000.0);
	}
	return rc;
}

int
sqlite3OsOpenMalloc(sqlite3_vfs * pVfs,
		    const char *zFile,
		    sqlite3_file ** ppFile, int flags, int *pOutFlags)
{
	int rc;
	sqlite3_file *pFile;
	pFile = (sqlite3_file *) sqlite3MallocZero(pVfs->szOsFile);
	if (pFile) {
		rc = sqlite3OsOpen(pVfs, zFile, pFile, flags, pOutFlags);
		if (rc != SQLITE_OK) {
			sqlite3_free(pFile);
		} else {
			*ppFile = pFile;
		}
	} else {
		rc = SQLITE_NOMEM_BKPT;
	}
	return rc;
}

void
sqlite3OsCloseFree(sqlite3_file * pFile)
{
	assert(pFile);
	sqlite3OsClose(pFile);
	sqlite3_free(pFile);
}

/*
 * This function is a wrapper around the OS specific implementation of
 * sqlite3_os_init(). The purpose of the wrapper is to provide the
 * ability to simulate a malloc failure, so that the handling of an
 * error in sqlite3_os_init() by the upper layers can be tested.
 */
int
sqlite3OsInit(void)
{
	void *p = sqlite3_malloc(10);
	if (p == 0)
		return SQLITE_NOMEM_BKPT;
	sqlite3_free(p);
	return sqlite3_os_init();
}

/*
 * The list of all registered VFS implementations.
 */
static sqlite3_vfs *SQLITE_WSD vfsList = 0;
#define vfsList GLOBAL(sqlite3_vfs *, vfsList)

/*
 * Locate a VFS by name.  If no name is given, simply return the
 * first VFS on the list.
 */
sqlite3_vfs *
sqlite3_vfs_find(const char *zVfs)
{
	sqlite3_vfs *pVfs = 0;
#ifndef SQLITE_OMIT_AUTOINIT
	int rc = sqlite3_initialize();
	if (rc)
		return 0;
#endif
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
vfsUnlink(sqlite3_vfs * pVfs)
{
	if (pVfs == 0) {
		/* No-op */
	} else if (vfsList == pVfs) {
		vfsList = pVfs->pNext;
	} else if (vfsList) {
		sqlite3_vfs *p = vfsList;
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
sqlite3_vfs_register(sqlite3_vfs * pVfs, int makeDflt)
{
#ifndef SQLITE_OMIT_AUTOINIT
	int rc = sqlite3_initialize();
	if (rc)
		return rc;
#endif
#ifdef SQLITE_ENABLE_API_ARMOR
	if (pVfs == 0)
		return SQLITE_MISUSE_BKPT;
#endif

	vfsUnlink(pVfs);
	if (makeDflt || vfsList == 0) {
		pVfs->pNext = vfsList;
		vfsList = pVfs;
	} else {
		pVfs->pNext = vfsList->pNext;
		vfsList->pNext = pVfs;
	}
	assert(vfsList);
	return SQLITE_OK;
}
