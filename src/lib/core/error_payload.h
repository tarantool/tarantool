/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "trivia/util.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tt_uuid;

/** Single field of error payload. */
struct error_field {
	/** MessagePack field value. */
	char *data;
	/** Data size. */
	uint32_t size;
	/** Zero terminated field name. */
	char *name;
};

/**
 * Key-value pairs to store dynamic fields of an error object. The ones which
 * are defined not for all error types and user-defined ones.
 */
struct error_payload {
	/** Number of fields. */
	int count;
	/**
	 * Array of field pointers. Not very optimized, but
	 * - The errors are supposed to be rare;
	 * - Number of fields is around 3 tops - linear search can be even
	 *   faster than a generic hash table then;
	 * - Not storing them by values simplifies addition of new fields and
	 *   their removal.
	 */
	struct error_field **fields;
};

/** Create error payload. */
void
error_payload_create(struct error_payload *p);

/** Destroy error payload. */
void
error_payload_destroy(struct error_payload *p);

/**
 * Get value of a payload field as a string. If it is not string or is not
 * found - return NULL.
 */
const char *
error_payload_get_str(const struct error_payload *e, const char *name);

/**
 * Set value of a payload field to a string. If the field existed before, it is
 * overwritten.
 */
void
error_payload_set_str(struct error_payload *e, const char *name,
		      const char *value);

/**
 * Get value of a payload field as a uint. If it is not uint or is not found -
 * return false and the out parameter is set to 0.
 */
bool
error_payload_get_uint(const struct error_payload *e, const char *name,
		       uint64_t *value);

/**
 * Set value of a payload field to a uint. If the field existed before, it is
 * overwritten.
 */
void
error_payload_set_uint(struct error_payload *e, const char *name,
		       uint64_t value);

/**
 * Get value of a payload field as an int. If it is not int, or is not found, or
 * does not fit int64_t - return false and the out parameter is set to 0.
 */
bool
error_payload_get_int(const struct error_payload *e, const char *name,
		      int64_t *value);

/**
 * Set value of a payload field to an int. If the field existed before, it is
 * overwritten.
 */
void
error_payload_set_int(struct error_payload *e, const char *name, int64_t value);

/**
 * Get value of a payload field as a double. If it is not double or is not
 * found - return false and the out parameter is set to 0.
 */
bool
error_payload_get_double(const struct error_payload *e, const char *name,
			 double *value);

/**
 * Set value of a payload field to a double. If the field existed before, it is
 * overwritten.
 */
void
error_payload_set_double(struct error_payload *e, const char *name,
			 double value);

/**
 * Get value of a payload field as a bool. If it is not bool or is not found -
 * return false and the out parameter is set to false.
 */
bool
error_payload_get_bool(const struct error_payload *e, const char *name,
		       bool *value);

/**
 * Set value of a payload field to a bool. If the field existed before, it is
 * overwritten.
 */
void
error_payload_set_bool(struct error_payload *e, const char *name, bool value);

/**
 * Get value of a payload field as a UUID. If it is not UUID or is not found -
 * return false and the out parameter is set to UUID with zeros.
 */
bool
error_payload_get_uuid(const struct error_payload *e, const char *name,
		       struct tt_uuid *uuid);

/**
 * Set value of a payload field to a UUID. If the field existed before, it is
 * overwritten.
 */
void
error_payload_set_uuid(struct error_payload *e, const char *name,
		       const struct tt_uuid *uuid);

/**
 * Get MessagePack value of a payload field. If it is not found - return NULL
 * and the out parameter is set to 0.
 */
const char *
error_payload_get_mp(const struct error_payload *e, const char *name,
		     uint32_t *size);

/**
 * Set value of a payload field to a MessagePack buffer. If the field existed
 * before, it is overwritten.
 */
void
error_payload_set_mp(struct error_payload *e, const char *name,
		     const char *src, uint32_t size);

/** Remove the given field from the payload. */
void
error_payload_clear(struct error_payload *e, const char *name);

/**
 * Move all fields of one payload into another. Old fields of the destination
 * are all deleted. The source stays valid but empty.
 */
void
error_payload_move(struct error_payload *dst, struct error_payload *src);

/** Find a payload field by name. */
const struct error_field *
error_payload_find(const struct error_payload *e, const char *name);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
