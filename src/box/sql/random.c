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
 * This file contains code to implement a pseudo-random number
 * generator (PRNG) for SQLite.
 *
 * Random numbers are used by some of the database backends in order
 * to generate random integer keys for tables or random filenames.
 */
#include "sqliteInt.h"

/* All threads share a single random number generator.
 * This structure is the current state of the generator.
 */
static SQLITE_WSD struct sqlite3PrngType {
	unsigned char isInit;	/* True if initialized */
	unsigned char i, j;	/* State variables */
	unsigned char s[256];	/* State variables */
} sqlite3Prng;

/*
 * Return N random bytes.
 */
void
sqlite3_randomness(int N, void *pBuf)
{
	unsigned char t;
	unsigned char *zBuf = pBuf;

	/* The "wsdPrng" macro will resolve to the pseudo-random number generator
	 * state vector.  If writable static data is unsupported on the target,
	 * we have to locate the state vector at run-time.  In the more common
	 * case where writable static data is supported, wsdPrng can refer directly
	 * to the "sqlite3Prng" state vector declared above.
	 */
#ifdef SQLITE_OMIT_WSD
	struct sqlite3PrngType *p =
	    &GLOBAL(struct sqlite3PrngType, sqlite3Prng);
#define wsdPrng p[0]
#else
#define wsdPrng sqlite3Prng
#endif

#ifndef SQLITE_OMIT_AUTOINIT
	if (sqlite3_initialize())
		return;
#endif

	if (N <= 0 || pBuf == 0) {
		wsdPrng.isInit = 0;
		return;
	}

	/* Initialize the state of the random number generator once,
	 * the first time this routine is called.  The seed value does
	 * not need to contain a lot of randomness since we are not
	 * trying to do secure encryption or anything like that...
	 *
	 * Nothing in this file or anywhere else in SQLite does any kind of
	 * encryption.  The RC4 algorithm is being used as a PRNG (pseudo-random
	 * number generator) not as an encryption device.
	 */
	if (!wsdPrng.isInit) {
		int i;
		char k[256];
		wsdPrng.j = 0;
		wsdPrng.i = 0;
		sqlite3OsRandomness(sqlite3_vfs_find(0), 256, k);
		for (i = 0; i < 256; i++) {
			wsdPrng.s[i] = (u8) i;
		}
		for (i = 0; i < 256; i++) {
			wsdPrng.j += wsdPrng.s[i] + k[i];
			t = wsdPrng.s[wsdPrng.j];
			wsdPrng.s[wsdPrng.j] = wsdPrng.s[i];
			wsdPrng.s[i] = t;
		}
		wsdPrng.isInit = 1;
	}

	assert(N > 0);
	do {
		wsdPrng.i++;
		t = wsdPrng.s[wsdPrng.i];
		wsdPrng.j += t;
		wsdPrng.s[wsdPrng.i] = wsdPrng.s[wsdPrng.j];
		wsdPrng.s[wsdPrng.j] = t;
		t += wsdPrng.s[wsdPrng.i];
		*(zBuf++) = wsdPrng.s[t];
	} while (--N);
}

#ifndef SQLITE_UNTESTABLE
/*
 * For testing purposes, we sometimes want to preserve the state of
 * PRNG and restore the PRNG to its saved state at a later time, or
 * to reset the PRNG to its initial state.  These routines accomplish
 * those tasks.
 *
 * The sqlite3_test_control() interface calls these routines to
 * control the PRNG.
 */
static SQLITE_WSD struct sqlite3PrngType sqlite3SavedPrng;
void
sqlite3PrngSaveState(void)
{
	memcpy(&GLOBAL(struct sqlite3PrngType, sqlite3SavedPrng),
	       &GLOBAL(struct sqlite3PrngType, sqlite3Prng), sizeof(sqlite3Prng)
	    );
}

void
sqlite3PrngRestoreState(void)
{
	memcpy(&GLOBAL(struct sqlite3PrngType, sqlite3Prng),
	       &GLOBAL(struct sqlite3PrngType, sqlite3SavedPrng),
	       sizeof(sqlite3Prng)
	    );
}
#endif				/* SQLITE_UNTESTABLE */
