/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "error_payload.h"
#include "mp_uuid.h"
#include "msgpuck.h"
#include "salad/grp_alloc.h"

void
error_payload_create(struct error_payload *p)
{
	p->count = 0;
	p->fields = NULL;
}

void
error_payload_destroy(struct error_payload *p)
{
	for (int i = 0; i < p->count; ++i) {
		TRASH(p->fields[i]);
		free(p->fields[i]);
	}
	free(p->fields);
	TRASH(p);
}

/**
 * Prepare a payload field to get a new value. If the field didn't exist, it is
 * added. If it existed then it is reallocated.
 * Extra parameter allows to allocate extra memory for arbitrary usage after the
 * error field and its value.
 */
static struct error_field *
error_payload_prepare(struct error_payload *e, const char *name,
		      uint32_t value_size, uint32_t extra)
{
	struct error_field *f;
	struct error_field **fields = e->fields;
	struct grp_alloc all = grp_alloc_initializer();
	int count = e->count;
	int i;
	for (i = 0; i < count; ++i) {
		f = fields[i];
		if (strcmp(name, f->name) != 0)
			continue;
		TRASH(f);
		free(f);
		goto alloc_field;
	}
	e->count = ++count;
	fields = xrealloc(fields, sizeof(fields[0]) * count);
	e->fields = fields;
alloc_field:
	grp_alloc_reserve_data(&all, sizeof(*f));
	grp_alloc_reserve_str0(&all, name);
	grp_alloc_reserve_data(&all, value_size + extra);
	grp_alloc_use(&all, xmalloc(grp_alloc_size(&all)));
	f = grp_alloc_create_data(&all, sizeof(*f));
	f->name = grp_alloc_create_str0(&all, name);
	f->data = grp_alloc_create_data(&all, value_size + extra);
	assert(grp_alloc_size(&all) == 0);
	f->size = value_size;
	fields[i] = f;
	return f;
}

const char *
error_payload_get_str(const struct error_payload *e, const char *name)
{
	const struct error_field *f = error_payload_find(e, name);
	if (f == NULL)
		return NULL;
	const char *data = f->data;
	if (mp_typeof(*data) != MP_STR)
		return NULL;
	uint32_t len;
	data = mp_decode_str(&data, &len);
	assert(data[len] == 0);
	return data;
}

void
error_payload_set_str(struct error_payload *e, const char *name,
		      const char *value)
{
	uint32_t len = strlen(value);
	char *data = error_payload_prepare(
		e, name, mp_sizeof_str(len), 1)->data;
	/*
	 * 1 extra byte in the end is reserved to append 0 after the encoded
	 * string. To be able to return it from get() without copying.
	 */
	*mp_encode_str(data, value, len) = 0;
}

bool
error_payload_get_uint(const struct error_payload *e, const char *name,
		       uint64_t *value)
{
	const struct error_field *f = error_payload_find(e, name);
	if (f == NULL)
		goto not_found;
	const char *data = f->data;
	if (mp_typeof(*data) != MP_UINT)
		goto not_found;
	*value = mp_decode_uint(&data);
	return true;

not_found:
	*value = 0;
	return false;
}

void
error_payload_set_uint(struct error_payload *e, const char *name,
		       uint64_t value)
{
	char *data = error_payload_prepare(
		e, name, mp_sizeof_uint(value), 0)->data;
	mp_encode_uint(data, value);
}

bool
error_payload_get_int(const struct error_payload *e, const char *name,
		      int64_t *value)
{
	const struct error_field *f = error_payload_find(e, name);
	if (f == NULL)
		goto not_found;
	const char *data = f->data;
	if (mp_typeof(*data) == MP_UINT) {
		uint64_t u = mp_decode_uint(&data);
		if (u > INT64_MAX)
			goto not_found;
		*value = u;
		return true;
	} else if (mp_typeof(*data) == MP_INT) {
		*value = mp_decode_int(&data);
		return true;
	}
not_found:
	*value = 0;
	return false;
}

void
error_payload_set_int(struct error_payload *e, const char *name, int64_t value)
{
	if (value >= 0)
		return error_payload_set_uint(e, name, value);
	char *data = error_payload_prepare(
		e, name, mp_sizeof_int(value), 0)->data;
	mp_encode_int(data, value);
}

bool
error_payload_get_double(const struct error_payload *e, const char *name,
			 double *value)
{
	const struct error_field *f = error_payload_find(e, name);
	if (f == NULL)
		goto not_found;
	const char *data = f->data;
	if (mp_typeof(*data) == MP_DOUBLE) {
		*value = mp_decode_double(&data);
		return true;
	} else if (mp_typeof(*data) == MP_FLOAT) {
		*value = mp_decode_float(&data);
		return true;
	}
not_found:
	*value = 0;
	return false;
}

void
error_payload_set_double(struct error_payload *e, const char *name,
			 double value)
{
	char *data = error_payload_prepare(
		e, name, mp_sizeof_double(value), 0)->data;
	mp_encode_double(data, value);
}

bool
error_payload_get_bool(const struct error_payload *e, const char *name,
		       bool *value)
{
	const struct error_field *f = error_payload_find(e, name);
	if (f == NULL)
		goto not_found;
	const char *data = f->data;
	if (mp_typeof(*data) != MP_BOOL)
		goto not_found;
	*value = mp_decode_bool(&data);
	return true;

not_found:
	*value = false;
	return false;
}

void
error_payload_set_bool(struct error_payload *e, const char *name, bool value)
{
	char *data = error_payload_prepare(
		e, name, mp_sizeof_bool(value), 0)->data;
	mp_encode_bool(data, value);
}

bool
error_payload_get_uuid(const struct error_payload *e, const char *name,
		       struct tt_uuid *uuid)
{
	const struct error_field *f = error_payload_find(e, name);
	if (f == NULL)
		goto not_found;
	const char *data = f->data;
	if (mp_decode_uuid(&data, uuid) == NULL)
		goto not_found;
	return true;

not_found:
	*uuid = uuid_nil;
	return false;
}

void
error_payload_set_uuid(struct error_payload *e, const char *name,
		       const struct tt_uuid *uuid)
{
	char *data = error_payload_prepare(e, name, mp_sizeof_uuid(), 0)->data;
	mp_encode_uuid(data, uuid);
}

const char *
error_payload_get_mp(const struct error_payload *e, const char *name,
		     uint32_t *size)
{
	const struct error_field *f = error_payload_find(e, name);
	if (f == NULL) {
		*size = 0;
		return NULL;
	}
	*size = f->size;
	return f->data;
}

void
error_payload_set_mp(struct error_payload *e, const char *name,
		     const char *src, uint32_t size)
{
	char *data;
	if (mp_typeof(*src) == MP_STR) {
		data = error_payload_prepare(e, name, size, 1)->data;
		/* @sa error_payload_set_str(). */
		data[size] = 0;
	} else {
		data = error_payload_prepare(e, name, size, 0)->data;
	}
	memcpy(data, src, size);
}

void
error_payload_clear(struct error_payload *e, const char *name)
{
	struct error_field **fields = e->fields;
	for (int i = 0; i < e->count; ++i) {
		struct error_field *f = fields[i];
		if (strcmp(name, f->name) != 0)
			continue;
		TRASH(f);
		free(f);
		int count = --e->count;
		if (count == 0) {
			free(fields);
			e->fields = NULL;
			return;
		}
		/* Cyclic deletion. Order does not matter in a dictionary. */
		fields[i] = fields[count];
		return;
	}
}

void
error_payload_move(struct error_payload *dst, struct error_payload *src)
{
	for (int i = 0; i < dst->count; ++i) {
		TRASH(dst->fields[i]);
		free(dst->fields[i]);
	}
	free(dst->fields);
	dst->fields = src->fields;
	dst->count = src->count;
	src->fields = NULL;
	src->count = 0;
}

const struct error_field *
error_payload_find(const struct error_payload *e, const char *name)
{
	for (int i = 0; i < e->count; ++i) {
		const struct error_field *f = e->fields[i];
		if (strcmp(name, f->name) == 0)
			return f;
	}
	return NULL;
}
