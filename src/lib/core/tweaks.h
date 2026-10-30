/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2023, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "diag.h"
#include "trivia/util.h"

/*
 * A tweak is an object that provides a convenient setter/getter API for
 * an arbitrary C variable. To register a tweak, use a TWEAK_XXX macro
 * at the global level in a C source file, for example:
 *
 *   static int64_t my_var;
 *   TWEAK_INT(my_var);
 *
 * This will create a tweak with name "my_var" that can be accessed with
 * the tweak_get and tweak_set functions:
 *
 *   struct tweak_val val;
 *   tweak_get("my_var", &val);
 *   val.ival = 42;
 *   tweak_set("my_var", &val);
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum tweak_value_type {
	TWEAK_VALUE_BOOL,
	TWEAK_VALUE_INT,
	TWEAK_VALUE_UINT,
	TWEAK_VALUE_DOUBLE,
	TWEAK_VALUE_STR,
};

/** Exported tweak value. */
struct tweak_value {
	/** Type of the tweak value. */
	enum tweak_value_type type;
	union {
		/** TWEAK_VALUE_BOOL */
		bool bval;
		/** TWEAK_VALUE_INT */
		int64_t ival;
		/** TWEAK_VALUE_UINT */
		uint64_t uval;
		/** TWEAK_VALUE_DOUBLE */
		double dval;
		/** TWEAK_VALUE_STR */
		const char *sval;
	};
};

struct tweak;

typedef void
(*tweak_get_f)(struct tweak *tweak, struct tweak_value *val);

typedef int
(*tweak_set_f)(struct tweak *tweak, const struct tweak_value *val);

/** Registered tweak. */
struct tweak {
	/** Pointer to the tweak data. */
	void *data;
	/** Returns the tweak value. */
	tweak_get_f get;
	/**
	 * Sets the tweak value.
	 *
	 * Note, the function may fail if the given value is incompatible
	 * with the tweak.
	 *
	 * Returns 0 on success. On error, sets diag and returns -1.
	 */
	tweak_set_f set;
};

static inline void
tweak_get(struct tweak *tweak, struct tweak_value *val)
{
	return tweak->get(tweak, val);
}

static inline int
tweak_set(struct tweak *tweak, const struct tweak_value *val)
{
	return tweak->set(tweak, val);
}

/**
 * Looks up a tweak by name. Returns NULL if not found.
 */
struct tweak *
tweak_find(const char *name);

typedef bool
tweak_foreach_f(const char *name, struct tweak *tweak, void *arg);

/**
 * Invokes a callback for each registered tweak with no particular order.
 *
 * The callback is passed a tweak name, a tweak object, and the given argument.
 * If it returns true, iteration continues. Otherwise, iteration breaks, and
 * the function returns false.
 */
bool
tweak_foreach(tweak_foreach_f cb, void *arg);

/**
 * Internal function that creates a new tweak object and adds it to
 * the tweak registry.
 *
 * WARNING: This is an *internal* function. DO NOT USE it directly,
 * use the TWEAK macro instead.
 *
 * WARNING: The name string isn't copied so it must never be freed
 * after calling this function. The TWEAK macro, which is the only
 * user of this function passes a string literal for the name, which
 * guarantees that this requirement is fulfilled.
 */
void
tweak_register_internal(const char *name, void *data,
			tweak_get_f get, tweak_set_f set);

/**
 * Defines a constructor function that will be called automatically at startup
 * to register a tweak for the given variable.
 *
 * The tweak will have the same name as the given variable. For getting and
 * setting the tweak value, the provided getter and setter callbacks will be
 * used.
 */
#define TWEAK(var, get, set)						\
__attribute__((constructor))						\
static void								\
var##_tweak_init(void)							\
{									\
	tweak_register_internal(#var, &(var), get, set);		\
}

/** Boolean tweak value getter. */
void
tweak_get_bool(struct tweak *tweak, struct tweak_value *val);

/** Boolean tweak value setter. */
int
tweak_set_bool(struct tweak *tweak, const struct tweak_value *val);

/** Registers a tweak for a boolean variable. */
#define TWEAK_BOOL(var)							\
STATIC_ASSERT_VAR_TYPE(var, bool)					\
TWEAK(var, tweak_get_bool, tweak_set_bool)

/** Integer tweak value getter. */
void
tweak_get_int(struct tweak *tweak, struct tweak_value *val);

/** Integer tweak value setter. */
int
tweak_set_int(struct tweak *tweak, const struct tweak_value *val);

/** Registers a tweak for an integer variable. */
#define TWEAK_INT(var)							\
STATIC_ASSERT_VAR_TYPE(var, int64_t)					\
TWEAK(var, tweak_get_int, tweak_set_int)

/** Unsigned integer tweak value getter. */
void
tweak_get_uint(struct tweak *tweak, struct tweak_value *val);

/** Unsigned integer tweak value setter. */
int
tweak_set_uint(struct tweak *tweak, const struct tweak_value *val);

/** Registers a tweak for an unsigned integer variable. */
#define TWEAK_UINT(var)							\
STATIC_ASSERT_VAR_TYPE(var, uint64_t)					\
TWEAK(var, tweak_get_uint, tweak_set_uint)

/** Double tweak value getter. */
void
tweak_get_double(struct tweak *tweak, struct tweak_value *val);

/** Double tweak value setter. */
int
tweak_set_double(struct tweak *tweak, const struct tweak_value *val);

/** Registers a tweak for a double variable. */
#define TWEAK_DOUBLE(var)						\
STATIC_ASSERT_VAR_TYPE(var, double)					\
TWEAK(var, tweak_get_double, tweak_set_double)

/**
 * Internal function that converts a tweak value to a enumeration.
 * On success, returns the enum value corresponding to the tweak value.
 * On error, sets diag and returns -1.
 */
int
tweak_value_to_enum_internal(const struct tweak_value *val,
			     const char *const *enum_strs, int enum_max);

/**
 * Registers a tweak for a enum variable.
 *
 * This macro only works for enums without gaps starting from 0.
 * The value of a enum tweak is exported as a string defined in
 * the enum_name##_strs array, where enum_name is the name of
 * the enum type. The enum must also have enum_name##_MAX value
 * defined. See also STR2ENUM.
 */
#define TWEAK_ENUM(enum_name, var)					\
STATIC_ASSERT_VAR_TYPE(var, enum enum_name)				\
static void								\
var##_tweak_get(struct tweak *tweak, struct tweak_value *val)		\
{									\
	assert(tweak->data == &(var));					\
	assert(tweak->get == var##_tweak_get);				\
	(void)tweak;							\
	(val)->type = TWEAK_VALUE_STR;					\
	(val)->sval = enum_name##_strs[(var)];				\
}									\
static int								\
var##_tweak_set(struct tweak *tweak, const struct tweak_value *val)	\
{									\
	assert(tweak->data == &(var));					\
	assert(tweak->set == var##_tweak_set);				\
	(void)tweak;							\
	int e = tweak_value_to_enum_internal(val, enum_name##_strs,	\
					     enum_name##_MAX);		\
	if (e < 0)							\
		return -1;						\
	(var) = e;							\
	return 0;							\
}									\
TWEAK(var, var##_tweak_get, var##_tweak_set)

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
