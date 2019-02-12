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
 * This is the header file for the generic hash-table implementation
 * used in sql.
 */
#ifndef SQL_HASH_H
#define SQL_HASH_H

/* Forward declarations of structures. */
typedef struct Hash Hash;
typedef struct HashElem HashElem;

/* A complete hash table is an instance of the following structure.
 * The internals of this structure are intended to be opaque -- client
 * code should not attempt to access or modify the fields of this structure
 * directly.  Change this structure only by using the routines below.
 * However, some of the "procedures" and "functions" for modifying and
 * accessing this structure are really macros, so we can't really make
 * this structure opaque.
 *
 * All elements of the hash table are on a single doubly-linked list.
 * Hash.first points to the head of this list.
 *
 * There are Hash.htsize buckets.  Each bucket points to a spot in
 * the global doubly-linked list.  The contents of the bucket are the
 * element pointed to plus the next _ht.count-1 elements in the list.
 *
 * Hash.htsize and Hash.ht may be zero.  In that case lookup is done
 * by a linear search of the global list.  For small tables, the
 * Hash.ht table is never allocated because if there are few elements
 * in the table, it is faster to do a linear search than to manage
 * the hash table.
 */
struct Hash {
	unsigned int htsize;	/* Number of buckets in the hash table */
	unsigned int count;	/* Number of entries in this table */
	HashElem *first;	/* The first element of the array */
	struct _ht {		/* the hash table */
		int count;	/* Number of entries with this hash */
		HashElem *chain;	/* Pointer to first entry with this hash */
	} *ht;
};

/* Each element in the hash table is an instance of the following
 * structure.  All elements are stored on a single doubly-linked list.
 *
 * Again, this structure is intended to be opaque, but it can't really
 * be opaque because it is used by macros.
 */
struct HashElem {
	HashElem *next, *prev;	/* Next and previous elements in the table */
	void *data;		/* Data associated with this element */
	/** Key associated with this element */
	char *pKey;
};

/*
 * Access routines.  To delete, insert a NULL pointer.
 */
void sqlHashInit(Hash *);
void *sqlHashInsert(Hash *, const char *pKey, void *pData);
void *sqlHashFind(const Hash *, const char *pKey);
void sqlHashClear(Hash *);

/*
 * Macros for looping over all elements of a hash table.  The idiom is
 * like this:
 *
 *   Hash h;
 *   HashElem *p;
 *   ...
 *   for(p=sqlHashFirst(&h); p; p=sqlHashNext(p)){
 *     SomeStructure *pData = sqlHashData(p);
 *     // do something with pData
 *   }
 */
#define sqlHashFirst(H)  ((H)->first)
#define sqlHashNext(E)   ((E)->next)
#define sqlHashData(E)   ((E)->data)
/* #define sqlHashKey(E)    ((E)->pKey) // NOT USED */
/* #define sqlHashKeysize(E) ((E)->nKey)  // NOT USED */

/*
 * Number of entries in a hash table
 */
/* #define sqlHashCount(H)  ((H)->count) // NOT USED */

#endif				/* SQL_HASH_H */
