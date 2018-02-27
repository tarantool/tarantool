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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "coll.h"
#include "third_party/PMurHash.h"
#include "error.h"
#include "diag.h"
#include <unicode/ucol.h>
#include <trivia/config.h>

enum {
	MAX_HASH_BUFFER = 1024,
	MAX_LOCALE = 1024,
};

/**
 * Compare two string using ICU collation.
 */
static int
coll_icu_cmp(const char *s, size_t slen, const char *t, size_t tlen,
	     const struct coll *coll)
{
	assert(coll->icu.collator != NULL);

	UErrorCode status = U_ZERO_ERROR;

#ifdef HAVE_ICU_STRCOLLUTF8
	UCollationResult result = ucol_strcollUTF8(coll->icu.collator,
						   s, slen, t, tlen, &status);
#else
	UCharIterator s_iter, t_iter;
	uiter_setUTF8(&s_iter, s, slen);
	uiter_setUTF8(&t_iter, t, tlen);
	UCollationResult result = ucol_strcollIter(coll->icu.collator,
						   &s_iter, &t_iter, &status);
#endif
	assert(!U_FAILURE(status));
	return (int)result;
}

/**
 * Get a hash of a string using ICU collation.
 */
static uint32_t
coll_icu_hash(const char *s, size_t s_len, uint32_t *ph, uint32_t *pcarry,
	      struct coll *coll)
{
	uint32_t total_size = 0;
	UCharIterator itr;
	uiter_setUTF8(&itr, s, s_len);
	uint8_t buf[MAX_HASH_BUFFER];
	uint32_t state[2] = {0, 0};
	UErrorCode status = U_ZERO_ERROR;
	while (true) {
		int32_t got = ucol_nextSortKeyPart(coll->icu.collator,
						   &itr, state, buf,
						   MAX_HASH_BUFFER, &status);
		PMurHash32_Process(ph, pcarry, buf, got);
		total_size += got;
		if (got < MAX_HASH_BUFFER)
			break;
	}
	return total_size;
}

/**
 * Set up ICU collator and init cmp and hash members of collation.
 * @param coll - collation to set up.
 * @param def - collation definition.
 * @return 0 on success, -1 on error.
 */
static int
coll_icu_init_cmp(struct coll *coll, const struct coll_def *def)
{
	if (coll->icu.collator != NULL) {
		ucol_close(coll->icu.collator);
		coll->icu.collator = NULL;
	}

	if (def->locale_len >= MAX_LOCALE) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			 "too long locale");
		return -1;
	}
	char locale[MAX_LOCALE];
	memcpy(locale, def->locale, def->locale_len);
	locale[def->locale_len] = '\0';
	UErrorCode status = U_ZERO_ERROR;
	struct UCollator *collator = ucol_open(locale, &status);
	if (U_FAILURE(status)) {
		diag_set(ClientError, ER_CANT_CREATE_COLLATION,
			 u_errorName(status));
		return -1;
	}
	coll->icu.collator = collator;

	if (def->icu.french_collation != COLL_ICU_DEFAULT) {
		enum coll_icu_on_off w = def->icu.french_collation;
		UColAttributeValue v =
			w == COLL_ICU_ON ? UCOL_ON :
			w == COLL_ICU_OFF ? UCOL_OFF :
			UCOL_DEFAULT;
		ucol_setAttribute(collator, UCOL_FRENCH_COLLATION, v, &status);
		if (U_FAILURE(status)) {
			diag_set(ClientError, ER_CANT_CREATE_COLLATION,
				 "failed to set french_collation");
			return -1;
		}
	}
	if (def->icu.alternate_handling != COLL_ICU_AH_DEFAULT) {
		enum coll_icu_alternate_handling w = def->icu.alternate_handling;
		UColAttributeValue v =
			w == COLL_ICU_AH_NON_IGNORABLE ? UCOL_NON_IGNORABLE :
			w == COLL_ICU_AH_SHIFTED ? UCOL_SHIFTED :
			UCOL_DEFAULT;
		ucol_setAttribute(collator, UCOL_ALTERNATE_HANDLING, v, &status);
		if (U_FAILURE(status)) {
			diag_set(ClientError, ER_CANT_CREATE_COLLATION,
				 "failed to set alternate_handling");
			return -1;
		}
	}
	if (def->icu.case_first != COLL_ICU_CF_DEFAULT) {
		enum coll_icu_case_first w = def->icu.case_first;
		UColAttributeValue v =
			w == COLL_ICU_CF_OFF ? UCOL_OFF :
			w == COLL_ICU_CF_UPPER_FIRST ? UCOL_UPPER_FIRST :
			w == COLL_ICU_CF_LOWER_FIRST ? UCOL_LOWER_FIRST :
			UCOL_DEFAULT;
		ucol_setAttribute(collator, UCOL_CASE_FIRST, v, &status);
		if (U_FAILURE(status)) {
			diag_set(ClientError, ER_CANT_CREATE_COLLATION,
				 "failed to set case_first");
			return -1;
		}
	}
	if (def->icu.case_level != COLL_ICU_DEFAULT) {
		enum coll_icu_on_off w = def->icu.case_level;
		UColAttributeValue v =
			w == COLL_ICU_ON ? UCOL_ON :
			w == COLL_ICU_OFF ? UCOL_OFF :
			UCOL_DEFAULT;
		ucol_setAttribute(collator, UCOL_CASE_LEVEL , v, &status);
		if (U_FAILURE(status)) {
			diag_set(ClientError, ER_CANT_CREATE_COLLATION,
				 "failed to set case_level");
			return -1;
		}
	}
	if (def->icu.normalization_mode != COLL_ICU_DEFAULT) {
		enum coll_icu_on_off w = def->icu.normalization_mode;
		UColAttributeValue v =
			w == COLL_ICU_ON ? UCOL_ON :
			w == COLL_ICU_OFF ? UCOL_OFF :
			UCOL_DEFAULT;
		ucol_setAttribute(collator, UCOL_NORMALIZATION_MODE, v, &status);
		if (U_FAILURE(status)) {
			diag_set(ClientError, ER_CANT_CREATE_COLLATION,
				 "failed to set normalization_mode");
			return -1;
		}
	}
	if (def->icu.strength != COLL_ICU_STRENGTH_DEFAULT) {
		enum coll_icu_strength w = def->icu.strength;
		UColAttributeValue v =
			w == COLL_ICU_STRENGTH_PRIMARY ? UCOL_PRIMARY :
			w == COLL_ICU_STRENGTH_SECONDARY ? UCOL_SECONDARY :
			w == COLL_ICU_STRENGTH_TERTIARY ? UCOL_TERTIARY :
			w == COLL_ICU_STRENGTH_QUATERNARY ? UCOL_QUATERNARY :
			w == COLL_ICU_STRENGTH_IDENTICAL ? UCOL_IDENTICAL :
			UCOL_DEFAULT;
		ucol_setAttribute(collator, UCOL_STRENGTH, v, &status);
		if (U_FAILURE(status)) {
			diag_set(ClientError, ER_CANT_CREATE_COLLATION,
				 "failed to set strength");
			return -1;
		}
	}
	if (def->icu.numeric_collation != COLL_ICU_DEFAULT) {
		enum coll_icu_on_off w = def->icu.numeric_collation;
		UColAttributeValue v =
			w == COLL_ICU_ON ? UCOL_ON :
			w == COLL_ICU_OFF ? UCOL_OFF :
			UCOL_DEFAULT;
		ucol_setAttribute(collator, UCOL_NUMERIC_COLLATION, v, &status);
		if (U_FAILURE(status)) {
			diag_set(ClientError, ER_CANT_CREATE_COLLATION,
				 "failed to set numeric_collation");
			return -1;
		}
	}

	coll->cmp = coll_icu_cmp;
	coll->hash = coll_icu_hash;
	return 0;
}

/**
 * Destroy ICU collation.
 */
static void
coll_icu_destroy(struct coll *coll)
{
	if (coll->icu.collator != NULL)
		ucol_close(coll->icu.collator);
}

bool
coll_is_case_sensitive(const struct coll *coll)
{
	UCollationStrength strength = ucol_getStrength(coll->icu.collator);
	return strength != UCOL_SECONDARY && strength != UCOL_PRIMARY;
}

/**
 * Create a collation by definition.
 * @param def - collation definition.
 * @return - the collation OR NULL on memory error (diag is set).
 */
struct coll *
coll_new(const struct coll_def *def)
{
	assert(def->type == COLL_TYPE_ICU); /* no more types are implemented yet */

	size_t total_len = sizeof(struct coll) + def->name_len + 1;
	struct coll *coll = (struct coll *)calloc(1, total_len);
	if (coll == NULL) {
		diag_set(OutOfMemory, total_len, "malloc", "struct coll");
		return NULL;
	}

	coll->id = def->id;
	coll->owner_id = def->owner_id;
	coll->type = def->type;
	coll->name_len = def->name_len;
	memcpy(coll->name, def->name, def->name_len);
	coll->name[coll->name_len] = 0;

	if (coll_icu_init_cmp(coll, def) != 0) {
		free(coll);
		return NULL;
	}

	return coll;
}

/**
 * Delete a collation.
 * @param cool - collation to delete.
 */
void
coll_delete(struct coll *coll)
{
	assert(coll->type == COLL_TYPE_ICU); /* no more types are implemented yet */
	coll_icu_destroy(coll);
	free(coll);
}
