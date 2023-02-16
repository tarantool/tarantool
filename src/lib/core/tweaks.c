/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "tweaks.h"

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "assoc.h"
#include "diag.h"
#include "trivia/util.h"
#include "tt_static.h"

/** Registry of all tweaks: name -> tweak. */
static struct mh_strnptr_t *tweaks;

__attribute__((destructor))
static void
tweaks_free(void)
{
	struct mh_strnptr_t *h = tweaks;
	if (h == NULL)
		return;
	mh_int_t i;
	mh_foreach(h, i) {
		struct tweak *tweak = mh_strnptr_node(h, i)->val;
		free(tweak);
	}
	mh_strnptr_delete(h);
	tweaks = NULL;
}

struct tweak *
tweak_find(const char *name)
{
	struct mh_strnptr_t *h = tweaks;
	uint32_t name_len = strlen(name);
	mh_int_t i = mh_strnptr_find_str(h, name, name_len);
	return i != mh_end(h) ? mh_strnptr_node(h, i)->val : NULL;
}

bool
tweak_foreach(tweak_foreach_f cb, void *arg)
{
	struct mh_strnptr_t *h = tweaks;
	mh_int_t i;
	mh_foreach(h, i) {
		struct mh_strnptr_node_t *node = mh_strnptr_node(h, i);
		const char *name = node->str;
		struct tweak *tweak = node->val;
		if (!cb(name, tweak, arg))
			return false;
	}
	return true;
}

void
tweak_register_internal(const char *name, void *data,
			tweak_get_f get, tweak_set_f set)
{
	/* Lazy initialization. */
	if (tweaks == NULL)
		tweaks = mh_strnptr_new();
	/*
	 * Sic: we don't copy the name, because it's a string literal,
	 * see the TWEAK macro implementation.
	 */
	struct mh_strnptr_t *h = tweaks;
	uint32_t name_len = strlen(name);
	uint32_t name_hash = mh_strn_hash(name, name_len);
	struct tweak *tweak = xmalloc(sizeof(*tweak));
	tweak->data = data;
	tweak->get = get;
	tweak->set = set;
	struct mh_strnptr_node_t node = {name, name_len, name_hash, tweak};
	struct mh_strnptr_node_t prev;
	struct mh_strnptr_node_t *prev_ptr = &prev;
	mh_strnptr_put(h, &node, &prev_ptr, NULL);
	assert(prev_ptr == NULL);
}

void
tweak_get_bool(struct tweak *tweak, struct tweak_value *val)
{
	assert(tweak->get == tweak_get_bool);
	val->type = TWEAK_VALUE_BOOL;
	val->bval = *(bool *)tweak->data;
}

int
tweak_set_bool(struct tweak *tweak, const struct tweak_value *val)
{
	assert(tweak->set == tweak_set_bool);
	if (val->type != TWEAK_VALUE_BOOL) {
		diag_set(IllegalParams, "Invalid value, expected boolean");
		return -1;
	}
	*(bool *)tweak->data = val->bval;
	return 0;
}

void
tweak_get_int(struct tweak *tweak, struct tweak_value *val)
{
	assert(tweak->get == tweak_get_int);
	val->type = TWEAK_VALUE_INT;
	val->ival = *(int *)tweak->data;
}

int
tweak_set_int(struct tweak *tweak, const struct tweak_value *val)
{
	assert(tweak->set == tweak_set_int);
	if (val->type != TWEAK_VALUE_INT) {
		diag_set(IllegalParams, "Invalid value, expected integer");
		return -1;
	}
	*(int *)tweak->data = val->ival;
	return 0;
}

void
tweak_get_double(struct tweak *tweak, struct tweak_value *val)
{
	assert(tweak->get == tweak_get_double);
	val->type = TWEAK_VALUE_DOUBLE;
	val->dval = *(double *)tweak->data;
}

int
tweak_set_double(struct tweak *tweak, const struct tweak_value *val)
{
	assert(tweak->set == tweak_set_double);
	if (val->type == TWEAK_VALUE_INT) {
		*(double *)tweak->data = val->ival;
	} else if (val->type == TWEAK_VALUE_DOUBLE) {
		*(double *)tweak->data = val->dval;
	} else {
		diag_set(IllegalParams, "Invalid value, expected number");
		return -1;
	}
	return 0;
}

/**
 * snprintf(buf, size, "Invalid value, expected one of: '%s', '%s', ...",
 *          enum_strs[0], enum_strs[1], ...);
 */
static int
invalid_enum_errmsg_snprint(char *buf, int size,
			    const char *const *enum_strs, int enum_max)
{
	int total = 0;
	SNPRINT(total, snprintf, buf, size, "Invalid value, expected one of: ");
	for (int i = 0; i < enum_max; ++i) {
		if (i > 0)
			SNPRINT(total, snprintf, buf, size, ", ");
		SNPRINT(total, snprintf, buf, size, "'%s'", enum_strs[i]);
	}
	return total;
}

/**
 * Calls invalid_enum_errmsg_snprint on tt_static_buf.
 */
static const char *
invalid_enum_errmsg(const char *const *enum_strs, int enum_max)
{
	char *buf = tt_static_buf();
	invalid_enum_errmsg_snprint(buf, TT_STATIC_BUF_LEN,
				    enum_strs, enum_max);
	return buf;
}

int
tweak_value_to_enum_internal(const struct tweak_value *val,
			     const char *const *enum_strs, int enum_max)
{
	if (val->type != TWEAK_VALUE_STR)
		goto error;
	int e = strindex(enum_strs, val->sval, enum_max);
	if (e == enum_max)
		goto error;
	return e;
error:
	diag_set(IllegalParams, invalid_enum_errmsg(enum_strs, enum_max));
	return -1;
}
