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
 * Memory allocation functions used throughout sql.
 */
#include "sqlInt.h"
#include <stdarg.h>

void
sql_xfree(void *buf)
{
	if (buf == NULL)
		return;
	struct sql *db = sql_get();
	assert(db != NULL);
	if (buf >= db->lookaside.pStart && buf < db->lookaside.pEnd) {
		LookasideSlot *pBuf = (LookasideSlot *)buf;
		pBuf->pNext = db->lookaside.pFree;
		db->lookaside.pFree = pBuf;
		db->lookaside.nOut--;
		return;
	}
	free(buf);
}

void *
sql_xmalloc0(size_t n)
{
	void *p = sql_xmalloc(n);
	memset(p, 0, n);
	return p;
}

void *
sql_xmalloc(size_t n)
{
	struct sql *db = sql_get();
	assert(db != NULL);
	LookasideSlot *pBuf;
	if (db->lookaside.bDisable == 0) {
		if (n > db->lookaside.sz) {
			db->lookaside.anStat[1]++;
		} else if ((pBuf = db->lookaside.pFree) == 0) {
			db->lookaside.anStat[2]++;
		} else {
			db->lookaside.pFree = pBuf->pNext;
			db->lookaside.nOut++;
			db->lookaside.anStat[0]++;
			if (db->lookaside.nOut > db->lookaside.mxOut) {
				db->lookaside.mxOut = db->lookaside.nOut;
			}
			return (void *)pBuf;
		}
	}
	return xmalloc(n);
}

void *
sql_xrealloc(void *buf, size_t n)
{
	struct sql *db = sql_get();
	assert(db != NULL);
	if (buf == NULL)
		return sql_xmalloc(n);
	if (buf >= db->lookaside.pStart && buf < db->lookaside.pEnd) {
		if (n <= (size_t)db->lookaside.sz)
			return buf;
		void *new_buf = sql_xmalloc(n);
		memcpy(new_buf, buf, db->lookaside.sz);
		sql_xfree(buf);
		return new_buf;
	}
	return xrealloc(buf, n);
}

char *
sql_xstrdup(const char *str)
{
	if (str == NULL)
		return NULL;
	size_t size = strlen(str) + 1;
	char *new_str = sql_xmalloc(size);
	memcpy(new_str, str, size);
	return new_str;
}

char *
sql_xstrndup(const char *str, size_t len)
{
	if (str == NULL)
		return NULL;
	char *new_str = sql_xmalloc(len + 1);
	memcpy(new_str, str, len);
	new_str[len] = '\0';
	return new_str;
}
