#ifndef TARANTOOL_LIB_CORE_REFLECTION_H_INCLUDED
#define TARANTOOL_LIB_CORE_REFLECTION_H_INCLUDED
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

#include <stddef.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h> /* strcmp */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct type_info;
struct method_info;

/**
 * Primitive C types
 */
enum ctype {
	CTYPE_VOID = 0,
	CTYPE_INT,
	CTYPE_CONST_CHAR_PTR
};

struct type_info {
	const char *name;
	const struct type_info *parent;
	const struct method_info *methods;
};

inline bool
type_assignable(const struct type_info *type, const struct type_info *object)
{
	assert(object != NULL);
	do {
		if (object == type)
			return true;
		assert(object->parent != object);
		object = object->parent;
	} while (object != NULL);
	return false;
}

/**
 * Determine if the specified object is assignment-compatible with
 * the object represented by type.
 */
#define type_cast(T, obj) ({						\
		T *r = NULL;						\
		if (type_assignable(&type_ ## T, (obj->type)))		\
			r = (T *) obj;					\
		(r);							\
	})

#if defined(__cplusplus)
/* Pointer to arbitrary C++ member function */
typedef void (type_info::*method_thiscall_f)(void);
#endif

enum { METHOD_ARG_MAX = 8 };

struct method_info {
	const struct type_info *owner;
	const char *name;
	enum ctype rtype;
	enum ctype atype[METHOD_ARG_MAX];
	int nargs;
	bool isconst;

	union {
		/* Add extra space to get proper struct size in C */
		void *_spacer[2];
#if defined(__cplusplus)
		method_thiscall_f thiscall;
#endif /* defined(__cplusplus) */
	};
};

#define type_foreach_method(m, method)						\
	for(const struct type_info *_m = (m); _m != NULL; _m = _m->parent)	\
		for (const struct method_info *method = _m->methods;		\
		     method->name != NULL; method++)

inline const struct method_info *
type_method_by_name(const struct type_info *type, const char *name)
{
	type_foreach_method(type, method) {
		if (strcmp(method->name, name) == 0)
			return method;
	}
	return NULL;
}

extern const struct method_info METHODS_SENTINEL;

#if defined(__cplusplus)
} /* extern "C" */

static_assert(sizeof(((struct method_info *) 0)->thiscall) <=
	      sizeof(((struct method_info *) 0)->_spacer), "sizeof(thiscall)");

/*
 * Begin of C++ syntax sugar
 */

/*
 * Initializer for struct type_info without methods
 */
inline struct type_info
make_type(const char *name, const struct type_info *parent)
{
	/* TODO: sorry, unimplemented: non-trivial designated initializers */
	struct type_info t;
	t.name = name;
	t.parent = parent;
	t.methods = &METHODS_SENTINEL;
	return t;
}

/*
 * Initializer for struct type_info with methods
 */
inline struct type_info
make_type(const char *name, const struct type_info *parent,
	  const struct method_info *methods)
{
	/* TODO: sorry, unimplemented: non-trivial designated initializers */
	struct type_info t;
	t.name = name;
	t.parent = parent;
	t.methods = methods;
	return t;
}

template<typename T> inline enum ctype ctypeof();
template<> inline enum ctype ctypeof<void>() { return CTYPE_VOID; }
template<> inline enum ctype ctypeof<int>() { return CTYPE_INT; }
template<> inline enum ctype ctypeof<const char *>() { return CTYPE_CONST_CHAR_PTR; }

/**
 * \cond false
 */

template <int N, typename... Args>
struct method_helper;

/** A helper for recursive templates */
template <int N, typename A, typename... Args>
struct method_helper<N, A, Args... >  {
	static bool
	invokable(const struct method_info *method)
	{
		if (method->atype[N] != ctypeof<A>())
			return false;
		return method_helper<N + 1, Args... >::invokable(method);
	}

	static void
	init(struct method_info *method)
	{
		method->atype[N] = ctypeof<A>();
		return method_helper<N + 1, Args... >::init(method);
	}
};

template <int N>
struct method_helper<N> {
	static bool
	invokable(const struct method_info *)
	{
		return true;
	}

	static void
	init(struct method_info *method)
	{
		method->nargs = N;
	}
};

/**
 * \endcond false
 */

/**
 * Initializer for R (T::*)(void) C++ member methods
 */
template<typename R, typename... Args, typename T> inline struct method_info
make_method(const struct type_info *owner, const char *name,
	R (T::*method_arg)(Args...))
{
	struct method_info m;
	m.owner = owner;
	m.name = name;
	m.thiscall = (method_thiscall_f) method_arg;
	m.isconst = false;
	m.rtype = ctypeof<R>();
	memset(m.atype, 0, sizeof(m.atype));
	method_helper<0, Args...>::init(&m);
	return m;
}

template<typename R, typename... Args, typename T> inline struct method_info
make_method(const struct type_info *owner, const char *name,
	R (T::*method_arg)(Args...) const)
{
	struct method_info m = make_method(owner, name, (R (T::*)(Args...)) method_arg);
	m.isconst = true;
	return m;
}

/**
 * Check if method is invokable with provided argument types
 */
template<typename R, typename... Args, typename T> inline bool
method_invokable(const struct method_info *method, T *object)
{
	static_assert(sizeof...(Args) <= METHOD_ARG_MAX, "too many arguments");
	if (!type_assignable(method->owner, object->type))
		return false;
	if (method->rtype != ctypeof<R>())
		return false;
	if (method->nargs != sizeof...(Args))
		return false;
	return method_helper<0, Args...>::invokable(method);
}

template<typename R, typename... Args, typename T> inline bool
method_invokable(const struct method_info *method, const T *object)
{
	if (!method->isconst)
		return false;
	return method_invokable<R, Args...>(method, const_cast<T*>(object));
}

/**
 * Invoke method with object and provided arguments.
 */
template<typename R, typename... Args, typename T > inline R
method_invoke(const struct method_info *method, T *object, Args... args)
{
	assert((method_invokable<R, Args...>(method, object)));
	typedef R (T::*MemberFunction)(Args...);
	return (object->*(MemberFunction) method->thiscall)(args...);
}

#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_LIB_CORE_REFLECTION_H_INCLUDED */
