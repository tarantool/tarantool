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
 * This file contains code to support the concept of "benign"
 * malloc failures (when the xMalloc() or xRealloc() method of the
 * sql_mem_methods structure fails to allocate a block of memory
 * and returns 0).
 *
 * Most malloc failures are non-benign. After they occur, sql
 * abandons the current operation and returns an error code (usually
 * SQL_NOMEM) to the user. However, sometimes a fault is not necessarily
 * fatal. For example, if a malloc fails while resizing a hash table, this
 * is completely recoverable simply by not carrying out the resize. The
 * hash table will continue to function normally.  So a malloc failure
 * during a hash table resize is a benign fault.
 */

#include "sqlInt.h"

/*
 * Global variables.
 */
typedef struct BenignMallocHooks BenignMallocHooks;
static SQL_WSD struct BenignMallocHooks {
	void (*xBenignBegin) (void);
	void (*xBenignEnd) (void);
} sqlHooks = {
0, 0};

/* The "wsdHooks" macro will resolve to the appropriate BenignMallocHooks
 * structure.  If writable static data is unsupported on the target,
 * we have to locate the state vector at run-time.  In the more common
 * case where writable static data is supported, wsdHooks can refer directly
 * to the "sqlHooks" state vector declared above.
 */
#define wsdHooksInit
#define wsdHooks sqlHooks

/*
 * Register hooks to call when sqlBeginBenignMalloc() and
 * sqlEndBenignMalloc() are called, respectively.
 */
void
sqlBenignMallocHooks(void (*xBenignBegin) (void), void (*xBenignEnd) (void)
    )
{
	wsdHooksInit;
	wsdHooks.xBenignBegin = xBenignBegin;
	wsdHooks.xBenignEnd = xBenignEnd;
}

/*
 * This (sqlEndBenignMalloc()) is called by sql code to indicate that
 * subsequent malloc failures are benign. A call to sqlEndBenignMalloc()
 * indicates that subsequent malloc failures are non-benign.
 */
void
sqlBeginBenignMalloc(void)
{
	wsdHooksInit;
	if (wsdHooks.xBenignBegin) {
		wsdHooks.xBenignBegin();
	}
}

void
sqlEndBenignMalloc(void)
{
	wsdHooksInit;
	if (wsdHooks.xBenignEnd) {
		wsdHooks.xBenignEnd();
	}
}

