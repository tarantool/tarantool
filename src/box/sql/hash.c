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
 * This is the implementation of generic hash-tables
 * used in sql.
 */
#include "sqlInt.h"
#include <assert.h>

/* Turn bulk memory into a hash table object by initializing the
 * fields of the Hash structure.
 *
 * "pNew" is a pointer to the hash table that is to be initialized.
 */
void
sqlHashInit(Hash * pNew)
{
	assert(pNew != 0);
	pNew->first = 0;
	pNew->count = 0;
	pNew->htsize = 0;
	pNew->ht = 0;
}

/* Remove all entries from a hash table.  Reclaim all memory.
 * Call this routine to delete a hash table or to reset a hash table
 * to the empty state.
 */
void
sqlHashClear(Hash * pH)
{
	HashElem *elem;		/* For looping over all elements of the table */

	assert(pH != 0);
	elem = pH->first;
	pH->first = 0;
	free(pH->ht);
	pH->ht = 0;
	pH->htsize = 0;
	while (elem) {
		HashElem *next_elem = elem->next;
		free(elem->pKey);
		free(elem);
		elem = next_elem;
	}
	pH->count = 0;
}

/*
 * The hashing function.
 */
static unsigned int
strHash(const char *z)
{
	unsigned int h = 0;
	unsigned char c;
	while ((c = (unsigned char)*z++) != 0) {	/*OPTIMIZATION-IF-TRUE */
		/* Knuth multiplicative hashing.  (Sorting & Searching, p. 510).
		 * 0x9e3779b1 is 2654435761 which is the closest prime number to
		 * (2**32)*golden_ratio, where golden_ratio = (sqrt(5) - 1)/2.
		 */
		h += c;
		h *= 0x9e3779b1;
	}
	return h;
}

/* Link pNew element into the hash table pH.  If pEntry!=0 then also
 * insert pNew into the pEntry hash bucket.
 */
static void
insertElement(Hash * pH,	/* The complete hash table */
	      struct _ht *pEntry,	/* The entry into which pNew is inserted */
	      HashElem * pNew	/* The element to be inserted */
    )
{
	HashElem *pHead;	/* First element already in pEntry */
	if (pEntry) {
		pHead = pEntry->count ? pEntry->chain : 0;
		pEntry->count++;
		pEntry->chain = pNew;
	} else {
		pHead = 0;
	}
	if (pHead) {
		pNew->next = pHead;
		pNew->prev = pHead->prev;
		if (pHead->prev) {
			pHead->prev->next = pNew;
		} else {
			pH->first = pNew;
		}
		pHead->prev = pNew;
	} else {
		pNew->next = pH->first;
		if (pH->first) {
			pH->first->prev = pNew;
		}
		pNew->prev = 0;
		pH->first = pNew;
	}
}

/** Resize the hash table to contain "new_size" buckets. */
static int
rehash(Hash * pH, unsigned int new_size)
{
	struct _ht *new_ht;	/* The new hash table */
	HashElem *elem, *next_elem;	/* For looping over existing elements */

#if SQL_MALLOC_SOFT_LIMIT>0
	if (new_size * sizeof(struct _ht) > SQL_MALLOC_SOFT_LIMIT) {
		new_size = SQL_MALLOC_SOFT_LIMIT / sizeof(struct _ht);
	}
	if (new_size == pH->htsize)
		return 0;
#endif
	new_ht = xmalloc(new_size * sizeof(struct _ht));
	free(pH->ht);
	pH->ht = new_ht;
	pH->htsize = new_size;
	memset(new_ht, 0, new_size * sizeof(struct _ht));
	for (elem = pH->first, pH->first = 0; elem; elem = next_elem) {
		unsigned int h = strHash(elem->pKey) % new_size;
		next_elem = elem->next;
		insertElement(pH, &new_ht[h], elem);
	}
	return 1;
}

/* This function (for internal use only) locates an element in an
 * hash table that matches the given key.  The hash for this key is
 * also computed and returned in the *pH parameter.
 */
static HashElem *
findElementWithHash(const Hash * pH,	/* The pH to be searched */
		    const char *pKey,	/* The key we are searching for */
		    unsigned int *pHash	/* Write the hash value here */
    )
{
	HashElem *elem;		/* Used to loop thru the element list */
	int count;		/* Number of elements left to test */
	unsigned int h;		/* The computed hash */

	if (pH->ht) {		/*OPTIMIZATION-IF-TRUE */
		struct _ht *pEntry;
		h = strHash(pKey) % pH->htsize;
		pEntry = &pH->ht[h];
		elem = pEntry->chain;
		count = pEntry->count;
	} else {
		h = 0;
		elem = pH->first;
		count = pH->count;
	}
	*pHash = h;
	while (count--) {
		assert(elem != 0);
		if (strcmp(elem->pKey, pKey) == 0) {
			return elem;
		}
		elem = elem->next;
	}
	return 0;
}

/* Remove a single entry from the hash table given a pointer to that
 * element and a hash on the element's key.
 */
static void
removeElementGivenHash(Hash * pH,	/* The pH containing "elem" */
		       HashElem * elem,	/* The element to be removed from the pH */
		       unsigned int h	/* Hash value for the element */
    )
{
	struct _ht *pEntry;
	if (elem->prev) {
		elem->prev->next = elem->next;
	} else {
		pH->first = elem->next;
	}
	if (elem->next) {
		elem->next->prev = elem->prev;
	}
	if (pH->ht) {
		pEntry = &pH->ht[h];
		if (pEntry->chain == elem) {
			pEntry->chain = elem->next;
		}
		pEntry->count--;
		assert(pEntry->count >= 0);
	}
	free(elem->pKey);
	free(elem);
	pH->count--;
	if (pH->count == 0) {
		assert(pH->first == 0);
		assert(pH->count == 0);
		sqlHashClear(pH);
	}
}

/* Attempt to locate an element of the hash table pH with a key
 * that matches pKey.  Return the data for this element if it is
 * found, or NULL if there is no match.
 */
void *
sqlHashFind(const Hash * pH, const char *pKey)
{
	HashElem *elem;		/* The element that matches key */
	unsigned int h;		/* A hash on key */

	assert(pH != 0);
	assert(pKey != 0);
	elem = findElementWithHash(pH, pKey, &h);
	return elem ? elem->data : 0;
}

/* Insert an element into the hash table pH.  The key is pKey
 * and the data is "data".
 *
 * If no element exists with a matching key, then a new
 * element is created and NULL is returned.
 *
 * If another element already exists with the same key, then the
 * new data replaces the old data and the old data is returned.
 * The key is not copied in this instance.
 *
 * If the "data" parameter to this function is NULL, then the
 * element corresponding to "key" is removed from the hash table.
 */
void *
sqlHashInsert(Hash * pH, const char *pKey, void *data)
{
	unsigned int h;		/* the hash of the key modulo hash table size */
	HashElem *elem;		/* Used to loop thru the element list */
	HashElem *new_elem;	/* New element added to the pH */

	assert(pH != 0);
	assert(pKey != 0);
	elem = findElementWithHash(pH, pKey, &h);
	if (elem) {
		void *old_data = elem->data;
		if (data == 0) {
			removeElementGivenHash(pH, elem, h);
		} else {
			elem->data = data;
			assert(elem->pKey != NULL);
			assert(strcmp(elem->pKey, pKey) == 0);
		}
		return old_data;
	}
	if (data == 0)
		return 0;
	new_elem = xmalloc(sizeof(HashElem));
	new_elem->pKey = xstrdup(pKey);
	new_elem->data = data;
	pH->count++;
	if (pH->count >= 10 && pH->count > 2 * pH->htsize) {
		if (rehash(pH, pH->count * 2)) {
			assert(pH->htsize > 0);
			h = strHash(pKey) % pH->htsize;
		}
	}
	insertElement(pH, pH->ht ? &pH->ht[h] : 0, new_elem);
	return 0;
}
