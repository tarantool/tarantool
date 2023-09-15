/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
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
/**
 * When this macros is defined, it means the file included self in
 * order to customize some templates. Skip everything except them
 * then.
 */
#ifndef MP_ERROR_PRINT_DEFINITION

#include "box/mp_error.h"
#include "box/error.h"
#include "mpstream/mpstream.h"
#include "msgpuck.h"
#include "mp_extension_types.h"
#include "fiber.h"
#include "ssl_error.h"

/**
 * MP_ERROR format:
 *
 * MP_ERROR: <MP_MAP> {
 *     MP_ERROR_STACK: <MP_ARRAY> [
 *         <MP_MAP> {
 *             MP_ERROR_TYPE:  <MP_STR>,
 *             MP_ERROR_FILE: <MP_STR>,
 *             MP_ERROR_LINE: <MP_UINT>,
 *             MP_ERROR_MESSAGE: <MP_STR>,
 *             MP_ERROR_ERRNO: <MP_UINT>,
 *             MP_ERROR_CODE: <MP_UINT>,
 *             MP_ERROR_FIELDS: <MP_MAP> {
 *                 <MP_STR>: ...,
 *                 <MP_STR>: ...,
 *                 ...
 *             },
 *             ...
 *         },
 *         ...
 *     ]
 * }
 */

/**
 * MP_ERROR keys
 */
enum {
	MP_ERROR_STACK = 0x00
};

/**
 * Keys of individual error in the stack.
 */
enum {
	/** Error type. */
	MP_ERROR_TYPE = 0x00,
	/** File name from trace. */
	MP_ERROR_FILE = 0x01,
	/** Line from trace. */
	MP_ERROR_LINE = 0x02,
	/** Error message. */
	MP_ERROR_MESSAGE = 0x03,
	/** Errno at the moment of error creation. */
	MP_ERROR_ERRNO = 0x04,
	/** Error code. */
	MP_ERROR_CODE = 0x05,
	/*
	 * Type-specific fields stored as a map
	 * {string key = value}.
	 */
	MP_ERROR_FIELDS = 0x06,
	MP_ERROR_MAX,
};

static const char *const mp_error_field_to_json_key[MP_ERROR_MAX] = {
	"\"type\": ",
	"\"file\": ",
	"\"line\": ",
	"\"message\": ",
	"\"errno\": ",
	"\"code\": ",
	"\"fields\": ",
};

/**
 * The structure is used for storing parameters
 * during decoding MP_ERROR.
 */
struct mp_error {
	uint32_t code;
	uint32_t line;
	uint32_t saved_errno;
	const char *type;
	const char *file;
	const char *message;
	struct error_payload payload;
};

static void
mp_error_create(struct mp_error *mp_error)
{
	memset(mp_error, 0, sizeof(*mp_error));
	error_payload_create(&mp_error->payload);
}

static void
mp_error_destroy(struct mp_error *mp_error)
{
	error_payload_destroy(&mp_error->payload);
}

static uint32_t
mp_sizeof_error_one(const struct error *error)
{
	uint32_t errcode = box_error_code(error);
	int field_count = error->payload.count;
	uint32_t map_size = 6 + (field_count > 0);
	uint32_t data_size = 0;

	data_size += mp_sizeof_map(map_size);
	data_size += mp_sizeof_uint(MP_ERROR_TYPE);
	data_size += mp_sizeof_str(strlen(error->type->name));
	data_size += mp_sizeof_uint(MP_ERROR_LINE);
	data_size += mp_sizeof_uint(error->line);
	data_size += mp_sizeof_uint(MP_ERROR_FILE);
	data_size += mp_sizeof_str(strlen(error->file));
	data_size += mp_sizeof_uint(MP_ERROR_MESSAGE);
	data_size += mp_sizeof_str(strlen(error->errmsg));
	data_size += mp_sizeof_uint(MP_ERROR_ERRNO);
	data_size += mp_sizeof_uint(error->saved_errno);
	data_size += mp_sizeof_uint(MP_ERROR_CODE);
	data_size += mp_sizeof_uint(errcode);

	if (field_count > 0) {
		data_size += mp_sizeof_uint(MP_ERROR_FIELDS);
		data_size += mp_sizeof_map(field_count);
		for (int i = 0; i < field_count; ++i) {
			const struct error_field *f = error->payload.fields[i];
			data_size += mp_sizeof_str(strlen(f->name));
			data_size += f->size;
		}
	}
	return data_size;
}

static char *
mp_encode_error_one(char *data, const struct error *error)
{
	uint32_t errcode = box_error_code(error);
	int field_count = error->payload.count;
	uint32_t map_size = 6 + (field_count > 0);

	data = mp_encode_map(data, map_size);
	data = mp_encode_uint(data, MP_ERROR_TYPE);
	data = mp_encode_str0(data, error->type->name);
	data = mp_encode_uint(data, MP_ERROR_LINE);
	data = mp_encode_uint(data, error->line);
	data = mp_encode_uint(data, MP_ERROR_FILE);
	data = mp_encode_str0(data, error->file);
	data = mp_encode_uint(data, MP_ERROR_MESSAGE);
	data = mp_encode_str0(data, error->errmsg);
	data = mp_encode_uint(data, MP_ERROR_ERRNO);
	data = mp_encode_uint(data, error->saved_errno);
	data = mp_encode_uint(data, MP_ERROR_CODE);
	data = mp_encode_uint(data, errcode);

	if (field_count > 0) {
		data = mp_encode_uint(data, MP_ERROR_FIELDS);
		data = mp_encode_map(data, field_count);
		for (int i = 0; i < field_count; ++i) {
			const struct error_field *f = error->payload.fields[i];
			data = mp_encode_str0(data, f->name);
			memcpy(data, f->data, f->size);
			data += f->size;
		}
	}
	return data;
}

static struct error *
error_build_xc(struct mp_error *mp_error)
{
	/*
	 * To create an error the "raw" constructor is used
	 * because OOM error must be thrown in OOM case.
	 * Builders returns a pointer to the static OOM error
	 * in OOM case.
	 */
	struct error *err = NULL;
	if (mp_error->type == NULL || mp_error->message == NULL ||
	    mp_error->file == NULL) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "Missing mandatory error fields");
		return NULL;
	}

	if (strcmp(mp_error->type, "ClientError") == 0) {
		err = new ClientError();
	} else if (strcmp(mp_error->type, "CustomError") == 0) {
		err = new CustomError();
	} else if (strcmp(mp_error->type, "AccessDeniedError") == 0) {
		err = new AccessDeniedError();
	} else if (strcmp(mp_error->type, "XlogError") == 0) {
		err = new XlogError();
	} else if (strcmp(mp_error->type, "XlogGapError") == 0) {
		err = new XlogGapError();
	} else if (strcmp(mp_error->type, "SystemError") == 0) {
		err = new SystemError();
	} else if (strcmp(mp_error->type, "SocketError") == 0) {
		err = new SocketError();
	} else if (strcmp(mp_error->type, "OutOfMemory") == 0) {
		err = new OutOfMemory();
	} else if (strcmp(mp_error->type, "TimedOut") == 0) {
		err = new TimedOut();
	} else if (strcmp(mp_error->type, "ChannelIsClosed") == 0) {
		err = new ChannelIsClosed();
	} else if (strcmp(mp_error->type, "FiberIsCancelled") == 0) {
		err = new FiberIsCancelled();
	} else if (strcmp(mp_error->type, "LuajitError") == 0) {
		err = new LuajitError();
	} else if (strcmp(mp_error->type, "IllegalParams") == 0) {
		err = new IllegalParams();
	} else if (strcmp(mp_error->type, "CollationError") == 0) {
		err = new CollationError();
	} else if (strcmp(mp_error->type, "SwimError") == 0) {
		err = new SwimError();
	} else if (strcmp(mp_error->type, "CryptoError") == 0) {
		err = new CryptoError();
	} else if (strcmp(mp_error->type, "SSLError") == 0) {
		err = new SSLError();
	} else {
		err = new ClientError();
	}
	err->code = mp_error->code;
	err->saved_errno = mp_error->saved_errno;
	error_set_location(err, mp_error->file, mp_error->line);
	error_move_payload(err, &mp_error->payload);
	error_format_msg(err, "%s", mp_error->message);
	return err;
}

static inline const char *
region_strdup(struct region *region, const char *str, uint32_t len)
{
	char *res = (char *)region_alloc(region, len + 1);
	if (res == NULL) {
		diag_set(OutOfMemory, len + 1, "region_alloc", "res");
		return NULL;
	}
	memcpy(res, str, len);
	res[len] = 0;
	return res;
}

static inline const char *
mp_decode_and_copy_str(const char **data, struct region *region)
{
	if (mp_typeof(**data) != MP_STR) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "Invalid MP_ERROR MsgPack format");
		return NULL;
	}
	uint32_t str_len;
	const char *str = mp_decode_str(data, &str_len);
	return region_strdup(region, str, str_len);;
}

static int
mp_decode_error_fields(const char **data, struct mp_error *mp_err,
		       struct region *region)
{
	if (mp_typeof(**data) != MP_MAP)
		return -1;
	uint32_t map_sz = mp_decode_map(data);
	for (uint32_t i = 0; i < map_sz; ++i) {
		uint32_t svp = region_used(region);
		const char *key = mp_decode_and_copy_str(data, region);
		if (key == NULL)
			return -1;
		const char *value = *data;
		mp_next(data);
		uint32_t value_len = *data - value;
		error_payload_set_mp(&mp_err->payload, key, value, value_len);
		region_truncate(region, svp);
	}
	return 0;
}

static struct error *
mp_decode_error_one(const char **data)
{
	struct mp_error mp_err;
	mp_error_create(&mp_err);
	struct region *region = &fiber()->gc;
	uint32_t region_svp = region_used(region);
	struct error *err = NULL;
	uint32_t map_size;

	if (mp_typeof(**data) != MP_MAP)
		goto error;

	map_size = mp_decode_map(data);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (mp_typeof(**data) != MP_UINT)
			goto error;

		uint64_t key = mp_decode_uint(data);
		switch(key) {
		case MP_ERROR_TYPE:
			mp_err.type = mp_decode_and_copy_str(data, region);
			if (mp_err.type == NULL)
				goto finish;
			break;
		case MP_ERROR_FILE:
			mp_err.file = mp_decode_and_copy_str(data, region);
			if (mp_err.file == NULL)
				goto finish;
			break;
		case MP_ERROR_LINE:
			if (mp_typeof(**data) != MP_UINT)
				goto error;
			mp_err.line = mp_decode_uint(data);
			break;
		case MP_ERROR_MESSAGE:
			mp_err.message = mp_decode_and_copy_str(data, region);
			if (mp_err.message == NULL)
				goto finish;
			break;
		case MP_ERROR_ERRNO:
			if (mp_typeof(**data) != MP_UINT)
				goto error;
			mp_err.saved_errno = mp_decode_uint(data);
			break;
		case MP_ERROR_CODE:
			if (mp_typeof(**data) != MP_UINT)
				goto error;
			mp_err.code = mp_decode_uint(data);
			break;
		case MP_ERROR_FIELDS:
			if (mp_decode_error_fields(data, &mp_err, region) != 0)
				goto finish;
			break;
		default:
			mp_next(data);
		}
	}

	try {
		err = error_build_xc(&mp_err);
	} catch (OutOfMemory *e) {
		assert(err == NULL && !diag_is_empty(diag_get()));
	}
finish:
	region_truncate(region, region_svp);
	mp_error_destroy(&mp_err);
	return err;

error:
	diag_set(ClientError, ER_INVALID_MSGPACK,
		 "Invalid MP_ERROR MsgPack format");
	goto finish;
}

/**
 * Returns the exact buffer size needed to encode an error in MsgPack without
 * the MP_EXT header.
 */
static uint32_t
mp_sizeof_error_noext(const struct error *error)
{
	uint32_t err_cnt = 0;
	uint32_t data_size = mp_sizeof_map(1);
	data_size += mp_sizeof_uint(MP_ERROR_STACK);
	for (const struct error *it = error; it != NULL; it = it->cause) {
		err_cnt++;
		data_size += mp_sizeof_error_one(it);
	}
	data_size += mp_sizeof_array(err_cnt);
	return data_size;
}

uint32_t
mp_sizeof_error(const struct error *error)
{
	return mp_sizeof_ext(mp_sizeof_error_noext(error));
}

/**
 * Encodes an error in MsgPack without the MP_EXT header.
 */
static char *
mp_encode_error_noext(char *data, const struct error *error)
{
	uint32_t err_cnt = 0;
	for (const struct error *it = error; it != NULL; it = it->cause)
		err_cnt++;
	data = mp_encode_map(data, 1);
	data = mp_encode_uint(data, MP_ERROR_STACK);
	data = mp_encode_array(data, err_cnt);
	for (const struct error *it = error; it != NULL; it = it->cause)
		data = mp_encode_error_one(data, it);
	return data;
}

char *
mp_encode_error(char *data, const struct error *error)
{
	uint32_t data_size = mp_sizeof_error_noext(error);
	char *ptr = data;
	data = mp_encode_extl(data, MP_ERROR, data_size);
	data = mp_encode_error_noext(data, error);
	assert(data == ptr + mp_sizeof_ext(data_size));
	(void)ptr;
	return data;
}

void
error_to_mpstream_noext(const struct error *error, struct mpstream *stream)
{
	uint32_t data_size = mp_sizeof_error_noext(error);
	char *ptr = mpstream_reserve(stream, data_size);
	char *data = mp_encode_error_noext(ptr, error);
	assert(data == ptr + data_size);
	(void)data;
	mpstream_advance(stream, data_size);
}

void
error_to_mpstream(const struct error *error, struct mpstream *stream)
{
	uint32_t data_size = mp_sizeof_error_noext(error);
	uint32_t data_size_ext = mp_sizeof_ext(data_size);
	char *ptr = mpstream_reserve(stream, data_size_ext);
	char *data = ptr;
	data = mp_encode_extl(data, MP_ERROR, data_size);
	data = mp_encode_error_noext(data, error);
	assert(data == ptr + data_size_ext);
	(void)data;
	mpstream_advance(stream, data_size_ext);
}

struct error *
error_unpack_unsafe(const char **data)
{
	struct error *err = NULL;

	if (mp_typeof(**data) != MP_MAP) {
		diag_set(ClientError, ER_INVALID_MSGPACK,
			 "Invalid MP_ERROR MsgPack format");
		return NULL;
	}
	uint32_t map_size = mp_decode_map(data);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (mp_typeof(**data) != MP_UINT) {
			diag_set(ClientError, ER_INVALID_MSGPACK,
				 "Invalid MP_ERROR MsgPack format");
			goto error;
		}
		uint64_t key = mp_decode_uint(data);
		switch(key) {
		case MP_ERROR_STACK: {
			if (err != NULL) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "Invalid MP_ERROR MsgPack format");
				goto error;
			}
			if (mp_typeof(**data) != MP_ARRAY) {
				diag_set(ClientError, ER_INVALID_MSGPACK,
					 "Invalid MP_ERROR MsgPack format");
				goto error;
			}
			uint32_t stack_sz = mp_decode_array(data);
			struct error *effect = NULL;
			for (uint32_t i = 0; i < stack_sz; i++) {
				struct error *cur = mp_decode_error_one(data);
				if (cur == NULL)
					goto error;
				if (err == NULL) {
					err = cur;
					effect = cur;
					continue;
				}

				error_set_prev(effect, cur);
				effect = cur;
			}
			break;
		}
		default:
			mp_next(data);
		}
	}
	return err;

error:
	if (err != NULL) {
		/* A hack to delete the error. */
		error_ref(err);
		error_unref(err);
	}
	return NULL;
}

struct error *
error_unpack(const char **data, uint32_t len)
{
	const char *end = *data + len;
	const char *check = *data;
	if (mp_check(&check, end) != 0 || check != end)
		return NULL;
	return error_unpack_unsafe(data);
}

int
mp_validate_error(const char *data, uint32_t len)
{
	struct error *err = error_unpack(&data, len);
	if (err != NULL) {
		/* A hack to delete the error. */
		error_ref(err);
		error_unref(err);
		return 0;
	} else {
		return 1;
	}
}

/**
 * Include this file into self with a few template parameters
 * to create mp_snprint_error() and mp_fprint_error() functions
 * and their helpers from a printer template.
 */
#define MP_ERROR_PRINT_DEFINITION
#define MP_PRINT_FUNC snprintf
#define MP_PRINT_SUFFIX snprint
#define MP_PRINT_2(total, func, ...)						\
	SNPRINT(total, func, buf, size, __VA_ARGS__)
#define MP_PRINT_ARGS_DECL char *buf, int size
#include "box/mp_error.cc"

#define MP_ERROR_PRINT_DEFINITION
#define MP_PRINT_FUNC fprintf
#define MP_PRINT_SUFFIX fprint
#define MP_PRINT_2(total, func, ...) do {							\
	int bytes = func(file, __VA_ARGS__);					\
	if (bytes < 0)								\
		return -1;							\
	total += bytes;								\
} while (0)
#define MP_PRINT_ARGS_DECL FILE *file
#include "box/mp_error.cc"

/* !defined(MP_ERROR_PRINT_DEFINITION) */
#else
/* defined(MP_ERROR_PRINT_DEFINITION) */

/**
 * MP_ERROR extension string serializer.
 * There are two applications for string serialization - into a
 * buffer, and into a file. Structure of both is exactly the same
 * except for the copying/writing itself. To avoid code
 * duplication the code is templated and expects some macros to do
 * the actual output.
 *
 * Templates are defined similarly to the usual C trick, when all
 * the code is in one file and relies on some external macros to
 * define customizable types, functions, parameters. Then other
 * code can include this file with the macros defined. Even
 * multiple times, in case structure and function names also are
 * customizable.
 *
 * The tricky thing here is that the templates are defined in the
 * same file, which needs them. So the file needs to include
 * *self* with a few macros defined beforehand. That allows to
 * hide this template from any external access, use mp_error
 * internal functions, and keep all the code in one place without
 * necessity to split it into different files just to be able to
 * turn it into a template.
 */

#define MP_CONCAT4_R(a, b, c, d)	a##b##c##d
#define MP_CONCAT4(a, b, c, d)		MP_CONCAT4_R(a, b, c, d)
#define MP_PRINT(total, ...)		MP_PRINT_2(total, MP_PRINT_FUNC,	\
						   __VA_ARGS__)

#define mp_func_name(name)	MP_CONCAT4(mp_, MP_PRINT_SUFFIX, _, name)
#define mp_print_error_one	mp_func_name(error_one)
#define mp_print_error_stack	mp_func_name(error_stack)
#define mp_print_error		mp_func_name(error)
#define mp_print_common		mp_func_name(recursion)

static int
mp_print_error_one(MP_PRINT_ARGS_DECL, const char **data, int depth)
{
	int total = 0;
	MP_PRINT(total, "{");
	if (depth <= 0) {
		MP_PRINT(total, "...}");
		return total;
	}
	--depth;
	if (mp_typeof(**data) != MP_MAP)
		return -1;
	uint32_t map_size = mp_decode_map(data);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (i != 0)
			MP_PRINT(total, ", ");
		if (mp_typeof(**data) != MP_UINT)
			return -1;
		uint64_t key = mp_decode_uint(data);
		if (key < MP_ERROR_MAX)
			MP_PRINT(total, "%s", mp_error_field_to_json_key[key]);
		else
			MP_PRINT(total, "%llu: ", (unsigned long long)key);
		MP_PRINT_2(total, mp_print_common, data, depth);
	}
	MP_PRINT(total, "}");
	return total;
}

static int
mp_print_error_stack(MP_PRINT_ARGS_DECL, const char **data, int depth)
{
	int total = 0;
	MP_PRINT(total, "[");
	if (depth <= 0) {
		MP_PRINT(total, "...]");
		return total;
	}
	--depth;
	if (mp_typeof(**data) != MP_ARRAY)
		return -1;
	uint32_t arr_size = mp_decode_array(data);
	for (uint32_t i = 0; i < arr_size; ++i) {
		if (i != 0)
			MP_PRINT(total, ", ");
		MP_PRINT_2(total, mp_print_error_one, data, depth);
	}
	MP_PRINT(total, "]");
	return total;
}

/**
 * The main printer template. Depending on template parameters it
 * is turned into mp_snprint_error() with snprintf() semantics or
 * into mp_fprint_error() with fprintf() semantics.
 */
int
mp_print_error(MP_PRINT_ARGS_DECL, const char **data, int depth)
{
	int total = 0;
	MP_PRINT(total, "{");
	if (depth <= 0) {
		MP_PRINT(total, "...}");
		return total;
	}
	--depth;
	if (mp_typeof(**data) != MP_MAP)
		return -1;
	uint32_t map_size = mp_decode_map(data);
	for (uint32_t i = 0; i < map_size; ++i) {
		if (i != 0)
			MP_PRINT(total, ", ");
		if (mp_typeof(**data) != MP_UINT)
			return -1;
		uint64_t key = mp_decode_uint(data);
		switch(key) {
		case MP_ERROR_STACK: {
			MP_PRINT(total, "\"stack\": ");
			MP_PRINT_2(total, mp_print_error_stack, data, depth);
			break;
		}
		default:
			MP_PRINT(total, "%llu: ", (unsigned long long)key);
			MP_PRINT_2(total, mp_print_common, data, depth);
			break;
		}
	}
	MP_PRINT(total, "}");
	return total;
}

#undef MP_PRINT
#undef MP_CONCAT4_R
#undef MP_CONCAT4

#undef mp_func_name
#undef mp_print_error_one
#undef mp_print_error_stack
#undef mp_print_error
#undef mp_print_common

#undef MP_ERROR_PRINT_DEFINITION
#undef MP_PRINT_FUNC
#undef MP_PRINT_SUFFIX
#undef MP_PRINT_2
#undef MP_PRINT_ARGS_DECL

#endif /* defined(MP_ERROR_PRINT_DEFINITION) */
