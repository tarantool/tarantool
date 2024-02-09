#ifndef INCLUDES_TARANTOOL_BOX_PORT_H
#define INCLUDES_TARANTOOL_BOX_PORT_H
/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "trivia/util.h"
#include "small/obuf.h"
#include <port.h>
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct tuple;

extern const struct port_vtab port_c_vtab;

/** Port implementation used for storing raw data. */
struct port_msgpack {
	const struct port_vtab *vtab;
	const char *data;
	uint32_t data_sz;
	/**
	 * Buffer for dump_plain() function. It is created during
	 * dump on demand and is deleted together with the port.
	 */
	char *plain;
	/**
	 * Context for decoding MsgPack data. Owned by port, ownership can be
	 * acquired by calling `port_get_msgpack`.
	 */
	struct mp_ctx *ctx;
};

static_assert(sizeof(struct port_msgpack) <= sizeof(struct port),
	      "sizeof(struct port_msgpack) must be <= sizeof(struct port)");

/** Initialize a port to dump raw data. */
void
port_msgpack_create_with_ctx(struct port *port, const char *data,
			     uint32_t data_sz, struct mp_ctx *ctx);

static inline void
port_msgpack_create(struct port *port, const char *data, uint32_t data_sz)
{
	port_msgpack_create_with_ctx(port, data, data_sz, NULL);
}

/** Destroy a MessagePack port. */
void
port_msgpack_destroy(struct port *base);

/**
 * Set plain text version of data in the given port. It is copied.
 */
int
port_msgpack_set_plain(struct port *base, const char *plain, uint32_t len);

/** Port for storing the result of a Lua CALL/EVAL. */
struct port_lua {
	const struct port_vtab *vtab;
	/** Lua state that stores the result. */
	struct lua_State *L;
	/** Reference to L in tarantool_L. */
	int ref;
	/** Number of entries dumped to the port. */
	int size;
	/** Bottom index of values to be dumped. */
	int bottom;
};

static_assert(sizeof(struct port_lua) <= sizeof(struct port),
	      "sizeof(struct port_lua) must be <= sizeof(struct port)");

/**
 * Initialize a port to dump from Lua stack starting from bottom index.
 * For example, if Lua stack contains values A, B, C, D, E (from bottom to top),
 * then calling this function with bottom set to 3 would result in creating
 * a port containing C, D, E.
 */
void
port_lua_create_at(struct port *port, struct lua_State *L, int bottom);

/** Initialize a port to dump all values from Lua stack. */
static inline void
port_lua_create(struct port *port, struct lua_State *L)
{
	port_lua_create_at(port, L, 1);
}

/** Port implementation used with vdbe memory variables. */
struct port_vdbemem {
    const struct port_vtab *vtab;
    struct Mem *mem;
    uint32_t mem_count;
};

static_assert(sizeof(struct port_vdbemem) <= sizeof(struct port),
	      "sizeof(struct port_vdbemem) must be <= sizeof(struct port)");

/** Initialize a port to dump data in sql vdbe memory. */
void
port_vdbemem_create(struct port *base, struct Mem *mem,
		    uint32_t mem_count);

/**
 * Type of value in port_c_entry.
 */
enum port_c_entry_type {
	/** Type for unsupported values. Is used in get_c_entries methods. */
	PORT_C_ENTRY_UNKNOWN,
	PORT_C_ENTRY_NULL,
	PORT_C_ENTRY_NUMBER,
	PORT_C_ENTRY_TUPLE,
	PORT_C_ENTRY_STR,
	PORT_C_ENTRY_BOOL,
	PORT_C_ENTRY_MP,
	PORT_C_ENTRY_MP_OBJECT,
	PORT_C_ENTRY_ITERABLE,
};

struct port_c_iterator;

/**
 * Type of function that can be used as an iterator_create method in port_c.
 * It is called only when the port is still alive.
 */
typedef void
(*port_c_iterator_create_f)(void *data, struct port_c_iterator *it);

/**
 * Type of function that can be used as an iterator_next method in port_c.
 * The iterator is passed as the first argument.
 * The port to yield values is passed as the second argument.
 * The third argument is a pointer to a flag which is set when the iterator
 * is exhausted.
 * Function must return 0 in the case of success. If eof is not reached,
 * port `out` must be initialized, otherwise it mustn't.
 * Also, the function can be called even after eof was reached - in this
 * case it must work correctly and consistently return eof.
 * In the case of error, port out must not be initialized, diag must be set
 * and -1 must be returned.
 */
typedef int
(*port_c_iterator_next_f)(struct port_c_iterator *it, struct port *out,
			  bool *is_eof);

/**
 * An iterable object stored in port_c.
 * When it is dumped, an iterator is created and is dumped instead of
 * this object. Is dumped only when it is possible, for example, we cannot
 * pack iterator to MsgPack, so it is dumped as MP_NIL there.
 *
 * When the object is added to port_c, it must stay alive as long as port can
 * be dumped. It's quite possible than the created iterator will be still
 * alive after the port and the object will be destroyed, so this situation
 * must be handled by the author of the iterable object.
 */
struct port_c_iterable {
	/**
	 * Creates an iterator.
	 * Mustn't be NULL.
	 */
	port_c_iterator_create_f iterator_create;
	/** The iterable object itself. */
	void *data;
};

/**
 * An iterator created by port_c_iterable.
 */
struct port_c_iterator {
	/**
	 * Advances iterator and yields values.
	 * Mustn't be NULL.
	 */
	port_c_iterator_next_f next;
	/**
	 * Implementation dependent content.
	 * Needed to allocate an abstract iterator.
	 */
	char padding[32];
};

/**
 * List of C entries.
 */
struct port_c_entry {
	/**
	 * The next element in the list.
	 * NULL if it is the last one.
	 */
	struct port_c_entry *next;
	/** Type of underlying value. */
	enum port_c_entry_type type;
	/** Value of entry - actually, it is a variant of pre-defined types. */
	union {
		/** Floating point number. */
		double number;
		/** Tuple. Is referenced. */
		struct tuple *tuple;
		/** Boolean value. */
		bool boolean;
		/**
		 * String, is not zero-teminated.
		 */
		struct {
			/** String itself. */
			const char *data;
			/** Size of string in bytes. */
			uint32_t size;
		} str;
		struct {
			/** MsgPack itself. */
			const char *data;
			/** Size of MsgPack in bytes. */
			uint32_t size;
			/** Metadata, if any. */
			union {
				/**
				 * Format for an MP_ARRAY.
				 * Is used for PORT_C_ENTRY_MP.
				 */
				struct tuple_format *format;
				/**
				 * MsgPack context.
				 * Is used for PORT_C_ENTRY_MP_OBJECT.
				 */
				struct mp_ctx *ctx;
			};
		} mp;
		/** Iterable object. */
		struct port_c_iterable iterable;
	};
};

/**
 * C port is used by C functions from the public API. They can
 * return tuples and arbitrary MessagePack.
 * Warning: this structure is exposed in FFI, so any change in it must be
 * replicated if FFI cdef, see schema.lua.
 */
struct port_c {
	const struct port_vtab *vtab;
	struct port_c_entry *first;
	struct port_c_entry *last;
	struct port_c_entry first_entry;
	int size;
};

static_assert(sizeof(struct port_c) <= sizeof(struct port),
	      "sizeof(struct port_c) must be <= sizeof(struct port)");

/** Create a C port object. */
void
port_c_create(struct port *base);

/** Append a tuple to the port. Tuple is referenced. */
void
port_c_add_tuple(struct port *port, struct tuple *tuple);

/** Append raw MessagePack to the port. It is copied. */
void
port_c_add_mp(struct port *port, const char *mp, const char *mp_end);

/**
 * Append MessagePack object to the port.
 * Argument ctx is copied if it is not NULL.
 */
void
port_c_add_mp_object(struct port *port, const char *mp, const char *mp_end,
		     struct mp_ctx *ctx);

struct tuple_format;

/**
 * Append raw msgpack array to the port with given format.
 * Msgpack is copied, the format is referenced for port's lifetime.
 */
void
port_c_add_formatted_mp(struct port *port, const char *mp, const char *mp_end,
			struct tuple_format *format);

/** Append a string to the port. It is copied. */
void
port_c_add_str(struct port *port, const char *str, size_t len);

/** Append a zero-terminated string to the port. It is copied. */
static inline void
port_c_add_str0(struct port *port, const char *str)
{
	port_c_add_str(port, str, strlen(str));
}

/** Append a NULL value to the port. */
void
port_c_add_null(struct port *base);

/** Append a boolean value to the port. */
void
port_c_add_bool(struct port *base, bool val);

/** Append a numeric value to the port. */
void
port_c_add_number(struct port *base, double val);

/**
 * Append an iterable object to the port.
 * See description of `struct port_c_iterable` for details.
 */
void
port_c_add_iterable(struct port *base, void *data,
		    port_c_iterator_create_f create);

/** Method get_msgpack for struct port_c. */
const char *
port_c_get_msgpack(struct port *base, uint32_t *size);

void
port_init(void);

void
port_free(void);

/**
 * Encodes the port's content into the msgpack array.
 */
void
port_c_dump_msgpack_wrapped(struct port *port, struct obuf *out,
			    struct mp_ctx *ctx);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined __cplusplus */

#endif /* INCLUDES_TARANTOOL_BOX_PORT_H */
