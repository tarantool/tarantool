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
 * This file contains functions used to access the internal hash tables
 * of user defined functions and collation sequences.
 */

#include "sqliteInt.h"
#include "box/session.h"

/*
 * Invoke the 'collation needed' callback to request a collation sequence
 * in the encoding enc of name zName, length nName.
 */
static void
callCollNeeded(sqlite3 * db, const char *zName)
{
	if (db->xCollNeeded) {
		char *zExternal = sqlite3DbStrDup(db, zName);
		if (!zExternal)
			return;
		db->xCollNeeded(db->pCollNeededArg, db, zExternal);
		sqlite3DbFree(db, zExternal);
	}
}

/*
 * This routine is called if the collation factory fails to deliver a
 * collation function in the best encoding but there may be other versions
 * of this collation function (for other text encodings) available. Use one
 * of these instead if they exist. Avoid a UTF-8 <-> UTF-16 conversion if
 * possible.
 */
static int
synthCollSeq(sqlite3 * db, CollSeq * pColl)
{
	CollSeq *pColl2;
	char *z = pColl->zName;
	int i;
	for (i = 0; i < 3; i++) {
		pColl2 = sqlite3FindCollSeq(db, z, 0);
		if (pColl2->xCmp != 0) {
			memcpy(pColl, pColl2, sizeof(CollSeq));
			pColl->xDel = 0;	/* Do not copy the destructor */
			return SQLITE_OK;
		}
	}
	return SQLITE_ERROR;
}

/*
 * This function is responsible for invoking the collation factory callback
 * or substituting a collation sequence of a different encoding when the
 * requested collation sequence is not available in the desired encoding.
 *
 * If it is not NULL, then pColl must point to the database native encoding
 * collation sequence with name zName, length nName.
 *
 * The return value is either the collation sequence to be used in database
 * db for collation type name zName, length nName, or NULL, if no collation
 * sequence can be found.  If no collation is found, leave an error message.
 *
 * See also: sqlite3LocateCollSeq(), sqlite3FindCollSeq()
 */
CollSeq *
sqlite3GetCollSeq(Parse * pParse,	/* Parsing context */
		  sqlite3 * db,	/* if called during runtime - pointer to the DB */
		  CollSeq * pColl,	/* Collating sequence with native encoding, or NULL */
		  const char *zName	/* Collating sequence name */
    )
{
	CollSeq *p;

	p = pColl;
	if (!p) {
		p = sqlite3FindCollSeq(db, zName, 0);
	}
	if (!p || !p->xCmp) {
		/* No collation sequence of this type for this encoding is registered.
		 * Call the collation factory to see if it can supply us with one.
		 */
		callCollNeeded(db, zName);
		p = sqlite3FindCollSeq(db, zName, 0);
	}
	if (p && !p->xCmp && synthCollSeq(db, p)) {
		p = 0;
	}
	assert(!p || p->xCmp);
	if (p == 0) {
		if (pParse)
			sqlite3ErrorMsg(pParse,
					"no such collation sequence: %s",
					zName);
		else {
		}		/* Assert will be triggered.  */
	}
	return p;
}

/*
 * This routine is called on a collation sequence before it is used to
 * check that it is defined. An undefined collation sequence exists when
 * a database is loaded that contains references to collation sequences
 * that have not been defined by sqlite3_create_collation() etc.
 *
 * If required, this routine calls the 'collation needed' callback to
 * request a definition of the collating sequence. If this doesn't work,
 * an equivalent collating sequence that uses a text encoding different
 * from the main database is substituted, if one is available.
 */
int
sqlite3CheckCollSeq(Parse * pParse, CollSeq * pColl)
{
	if (pColl) {
		const char *zName = pColl->zName;
		CollSeq *p =
		    sqlite3GetCollSeq(pParse, pParse->db, pColl,
				      zName);
		if (!p) {
			return SQLITE_ERROR;
		}
		assert(p == pColl);
	}
	return SQLITE_OK;
}

/*
 * Locate and return an entry from the db.aCollSeq hash table. If the entry
 * specified by zName and nName is not found and parameter 'create' is
 * true, then create a new entry. Otherwise return NULL.
 *
 * Each pointer stored in the sqlite3.aCollSeq hash table contains an
 * array of three CollSeq structures. The first is the collation sequence
 * preferred for UTF-8, the second UTF-16le, and the third UTF-16be.
 *
 * Stored immediately after the three collation sequences is a copy of
 * the collation sequence name. A pointer to this string is stored in
 * each collation sequence structure.
 */
static CollSeq *
findCollSeqEntry(sqlite3 * db,	/* Database connection */
		 const char *zName,	/* Name of the collating sequence */
		 int create	/* Create a new entry if true */
    )
{
	CollSeq *pColl;
	pColl = sqlite3HashFind(&db->aCollSeq, zName);

	if (0 == pColl && create) {
		int nName = sqlite3Strlen30(zName);
		pColl = sqlite3DbMallocZero(db, 1 * sizeof(*pColl) + nName + 1);
		if (pColl) {
			CollSeq *pDel = 0;
			pColl[0].zName = (char *)&pColl[1];
			memcpy(pColl[0].zName, zName, nName);
			pColl[0].zName[nName] = 0;
			pDel =
			    sqlite3HashInsert(&db->aCollSeq, pColl[0].zName,
					      pColl);

			/* If a malloc() failure occurred in sqlite3HashInsert(), it will
			 * return the pColl pointer to be deleted (because it wasn't added
			 * to the hash table).
			 */
			assert(pDel == 0 || pDel == pColl);
			if (pDel != 0) {
				sqlite3OomFault(db);
				sqlite3DbFree(db, pDel);
				pColl = 0;
			}
		}
	}
	return pColl;
}

/*
 * Parameter zName points to a UTF-8 encoded string nName bytes long.
 * Return the CollSeq* pointer for the collation sequence named zName
 * for the encoding 'enc' from the database 'db'.
 *
 * If the entry specified is not found and 'create' is true, then create a
 * new entry.  Otherwise return NULL.
 *
 * A separate function sqlite3LocateCollSeq() is a wrapper around
 * this routine.  sqlite3LocateCollSeq() invokes the collation factory
 * if necessary and generates an error message if the collating sequence
 * cannot be found.
 *
 * See also: sqlite3LocateCollSeq(), sqlite3GetCollSeq()
 */
CollSeq *
sqlite3FindCollSeq(sqlite3 * db, const char *zName, int create)
{
	CollSeq *pColl;
	if (zName) {
		pColl = findCollSeqEntry(db, zName, create);
	} else {
		pColl = db->pDfltColl;
	}
	return pColl;
}

/* During the search for the best function definition, this procedure
 * is called to test how well the function passed as the first argument
 * matches the request for a function with nArg arguments in a system
 * that uses encoding enc. The value returned indicates how well the
 * request is matched. A higher value indicates a better match.
 *
 * If nArg is -1 that means to only return a match (non-zero) if p->nArg
 * is also -1.  In other words, we are searching for a function that
 * takes a variable number of arguments.
 *
 * If nArg is -2 that means that we are searching for any function
 * regardless of the number of arguments it uses, so return a positive
 * match score for any
 *
 * The returned value is always between 0 and 6, as follows:
 *
 * 0: Not a match.
 * 1: UTF8/16 conversion required and function takes any number of arguments.
 * 2: UTF16 byte order change required and function takes any number of args.
 * 3: encoding matches and function takes any number of arguments
 * 4: UTF8/16 conversion required - argument count matches exactly
 * 5: UTF16 byte order conversion required - argument count matches exactly
 * 6: Perfect match:  encoding and argument count match exactly.
 *
 * If nArg==(-2) then any function with a non-null xSFunc is
 * a perfect match and any function with xSFunc NULL is
 * a non-match.
 */
#define FUNC_PERFECT_MATCH 4	/* The score for a perfect match */
static int
matchQuality(FuncDef * p,	/* The function we are evaluating for match quality */
	     int nArg		/* Desired number of arguments.  (-1)==any */
    )
{
	int match;

	/* nArg of -2 is a special case */
	if (nArg == (-2))
		return (p->xSFunc == 0) ? 0 : FUNC_PERFECT_MATCH;

	/* Wrong number of arguments means "no match" */
	if (p->nArg != nArg && p->nArg >= 0)
		return 0;

	/* Give a better score to a function with a specific number of arguments
	 * than to function that accepts any number of arguments.
	 */
	if (p->nArg == nArg) {
		match = 4;
	} else {
		match = 1;
	}

	return match;
}

/*
 * Search a FuncDefHash for a function with the given name.  Return
 * a pointer to the matching FuncDef if found, or 0 if there is no match.
 */
static FuncDef *
functionSearch(int h,		/* Hash of the name */
	       const char *zFunc	/* Name of function */
    )
{
	FuncDef *p;
	for (p = sqlite3BuiltinFunctions.a[h]; p; p = p->u.pHash) {
		if (sqlite3StrICmp(p->zName, zFunc) == 0) {
			return p;
		}
	}
	return 0;
}

/*
 * Insert a new FuncDef into a FuncDefHash hash table.
 */
void
sqlite3InsertBuiltinFuncs(FuncDef * aDef,	/* List of global functions to be inserted */
			  int nDef	/* Length of the apDef[] list */
    )
{
	int i;
	for (i = 0; i < nDef; i++) {
		FuncDef *pOther;
		const char *zName = aDef[i].zName;
		int nName = sqlite3Strlen30(zName);
		int h =
		    (sqlite3UpperToLower[(u8) zName[0]] +
		     nName) % SQLITE_FUNC_HASH_SZ;
		pOther = functionSearch(h, zName);
		if (pOther) {
			assert(pOther != &aDef[i] && pOther->pNext != &aDef[i]);
			aDef[i].pNext = pOther->pNext;
			pOther->pNext = &aDef[i];
		} else {
			aDef[i].pNext = 0;
			aDef[i].u.pHash = sqlite3BuiltinFunctions.a[h];
			sqlite3BuiltinFunctions.a[h] = &aDef[i];
		}
	}
}

/*
 * Locate a user function given a name, a number of arguments and a flag
 * indicating whether the function prefers UTF-16 over UTF-8.  Return a
 * pointer to the FuncDef structure that defines that function, or return
 * NULL if the function does not exist.
 *
 * If the createFlag argument is true, then a new (blank) FuncDef
 * structure is created and liked into the "db" structure if a
 * no matching function previously existed.
 *
 * If nArg is -2, then the first valid function found is returned.  A
 * function is valid if xSFunc is non-zero.  The nArg==(-2)
 * case is used to see if zName is a valid function name for some number
 * of arguments.  If nArg is -2, then createFlag must be 0.
 *
 * If createFlag is false, then a function with the required name and
 * number of arguments may be returned even if the eTextRep flag does not
 * match that requested.
 */
FuncDef *
sqlite3FindFunction(sqlite3 * db,	/* An open database */
		    const char *zName,	/* Name of the function.  zero-terminated */
		    int nArg,	/* Number of arguments.  -1 means any number */
		    u8 createFlag	/* Create new entry if true and does not otherwise exist */
    )
{
	FuncDef *p;		/* Iterator variable */
	FuncDef *pBest = 0;	/* Best match found so far */
	int bestScore = 0;	/* Score of best match */
	int h;			/* Hash value */
	int nName;		/* Length of the name */
	struct session *user_session = current_session();

	assert(nArg >= (-2));
	assert(nArg >= (-1) || createFlag == 0);
	nName = sqlite3Strlen30(zName);

	/* First search for a match amongst the application-defined functions.
	 */
	p = (FuncDef *) sqlite3HashFind(&db->aFunc, zName);
	while (p) {
		int score = matchQuality(p, nArg);
		if (score > bestScore) {
			pBest = p;
			bestScore = score;
		}
		p = p->pNext;
	}

	/* If no match is found, search the built-in functions.
	 *
	 * If the SQLITE_PreferBuiltin flag is set, then search the built-in
	 * functions even if a prior app-defined function was found.  And give
	 * priority to built-in functions.
	 *
	 * Except, if createFlag is true, that means that we are trying to
	 * install a new function.  Whatever FuncDef structure is returned it will
	 * have fields overwritten with new information appropriate for the
	 * new function.  But the FuncDefs for built-in functions are read-only.
	 * So we must not search for built-ins when creating a new function.
	 */
	if (!createFlag &&
	    (pBest == 0
	     || (user_session->sql_flags & SQLITE_PreferBuiltin) != 0)) {
		bestScore = 0;
		h = (sqlite3UpperToLower[(u8) zName[0]] +
		     nName) % SQLITE_FUNC_HASH_SZ;
		p = functionSearch(h, zName);
		while (p) {
			int score = matchQuality(p, nArg);
			if (score > bestScore) {
				pBest = p;
				bestScore = score;
			}
			p = p->pNext;
		}
	}

	/* If the createFlag parameter is true and the search did not reveal an
	 * exact match for the name, number of arguments and encoding, then add a
	 * new entry to the hash table and return it.
	 */
	if (createFlag && bestScore < FUNC_PERFECT_MATCH &&
	    (pBest =
	     sqlite3DbMallocZero(db, sizeof(*pBest) + nName + 1)) != 0) {
		FuncDef *pOther;
		pBest->zName = (const char *)&pBest[1];
		pBest->nArg = (u16) nArg;
		pBest->funcFlags = 0;
		memcpy((char *)&pBest[1], zName, nName + 1);
		pOther =
		    (FuncDef *) sqlite3HashInsert(&db->aFunc, pBest->zName,
						  pBest);
		if (pOther == pBest) {
			sqlite3DbFree(db, pBest);
			sqlite3OomFault(db);
			return 0;
		} else {
			pBest->pNext = pOther;
		}
	}

	if (pBest && (pBest->xSFunc || createFlag)) {
		return pBest;
	}
	return 0;
}

/*
 * Free all resources held by the schema structure. The void* argument points
 * at a Schema struct. This function does not call sqlite3DbFree(db, ) on the
 * pointer itself, it just cleans up subsidiary resources (i.e. the contents
 * of the schema hash tables).
 *
 * The Schema.cache_size variable is not cleared.
 */
void
sqlite3SchemaClear(void *p)
{
	Hash temp1;
	Hash temp2;
	HashElem *pElem;
	Schema *pSchema = (Schema *) p;

	temp1 = pSchema->tblHash;
	temp2 = pSchema->trigHash;
	sqlite3HashInit(&pSchema->trigHash);
	for (pElem = sqliteHashFirst(&temp2); pElem;
	     pElem = sqliteHashNext(pElem)) {
		sqlite3DeleteTrigger(0, (Trigger *) sqliteHashData(pElem));
	}
	sqlite3HashClear(&temp2);
	sqlite3HashInit(&pSchema->tblHash);
	for (pElem = sqliteHashFirst(&temp1); pElem;
	     pElem = sqliteHashNext(pElem)) {
		Table *pTab = sqliteHashData(pElem);
		sqlite3DeleteTable(0, pTab);
	}
	sqlite3HashClear(&temp1);
	sqlite3HashClear(&pSchema->fkeyHash);
	pSchema->pSeqTab = 0;
	if (pSchema->schemaFlags & DB_SchemaLoaded) {
		pSchema->iGeneration++;
		pSchema->schemaFlags &= ~DB_SchemaLoaded;
	}
}

/*
 * Find and return the schema associated with a BTree.  Create
 * a new one if necessary.
 */
Schema *
sqlite3SchemaGet(sqlite3 * db, Btree * pBt)
{
	Schema *p;
	if (pBt) {
		p = (Schema *) sqlite3BtreeSchema(pBt, sizeof(Schema),
						  sqlite3SchemaClear);
	} else {
		p = (Schema *) sqlite3DbMallocZero(0, sizeof(Schema));
	}
	if (!p) {
		sqlite3OomFault(db);
	} else if (0 == p->file_format) {
		sqlite3HashInit(&p->tblHash);
		sqlite3HashInit(&p->trigHash);
		sqlite3HashInit(&p->fkeyHash);
	}
	return p;
}
