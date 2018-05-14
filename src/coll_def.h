#ifndef TARANTOOL_COLL_DEF_H_INCLUDED
#define TARANTOOL_COLL_DEF_H_INCLUDED
/*
 * Copyright 2010-2018, Tarantool AUTHORS, please see AUTHORS file.
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
#include <stddef.h>
#include <stdint.h>

/** The supported collation types */
enum coll_type {
	COLL_TYPE_ICU = 0,
	coll_type_MAX,
};

extern const char *coll_type_strs[];

/*
 * ICU collation options. See
 * http://icu-project.org/apiref/icu4c/ucol_8h.html#a583fbe7fc4a850e2fcc692e766d2826c
 */

/** Settings for simple ICU on/off options */
enum coll_icu_on_off {
	COLL_ICU_DEFAULT = 0,
	COLL_ICU_ON,
	COLL_ICU_OFF,
	coll_icu_on_off_MAX
};

extern const char *coll_icu_on_off_strs[];

/** Alternate handling ICU settings */
enum coll_icu_alternate_handling {
	COLL_ICU_AH_DEFAULT = 0,
	COLL_ICU_AH_NON_IGNORABLE,
	COLL_ICU_AH_SHIFTED,
	coll_icu_alternate_handling_MAX
};

extern const char *coll_icu_alternate_handling_strs[];

/** Case first ICU settings */
enum coll_icu_case_first {
	COLL_ICU_CF_DEFAULT = 0,
	COLL_ICU_CF_OFF,
	COLL_ICU_CF_UPPER_FIRST,
	COLL_ICU_CF_LOWER_FIRST,
	coll_icu_case_first_MAX
};

extern const char *coll_icu_case_first_strs[];

/** Strength ICU settings */
enum coll_icu_strength {
	COLL_ICU_STRENGTH_DEFAULT = 0,
	COLL_ICU_STRENGTH_PRIMARY,
	COLL_ICU_STRENGTH_SECONDARY,
	COLL_ICU_STRENGTH_TERTIARY,
	COLL_ICU_STRENGTH_QUATERNARY,
	COLL_ICU_STRENGTH_IDENTICAL,
	coll_icu_strength_MAX
};

extern const char *coll_icu_strength_strs[];

/** Collection of ICU settings */
struct coll_icu_def {
	enum coll_icu_on_off french_collation;
	enum coll_icu_alternate_handling alternate_handling;
	enum coll_icu_case_first case_first;
	enum coll_icu_on_off case_level;
	enum coll_icu_on_off normalization_mode;
	enum coll_icu_strength strength;
	enum coll_icu_on_off numeric_collation;
};

/** Collation definition. */
struct coll_def {
	/** Locale. */
	size_t locale_len;
	const char *locale;
	/** Collation type. */
	enum coll_type type;
	/** Type specific options. */
	struct coll_icu_def icu;
};

#endif /* TARANTOOL_COLL_DEF_H_INCLUDED */
