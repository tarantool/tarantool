#ifndef TARANTOOL_SCOPED_GUARD_H_INCLUDED
#define TARANTOOL_SCOPED_GUARD_H_INCLUDED

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
#include <stdlib.h>
#include <type_traits>
#include <utility>

template<typename Functor>
struct ScopedGuard {
	static_assert(noexcept(std::declval<Functor>()()),
		      "Functor has to be noexcept");

	explicit ScopedGuard(const Functor & fun) noexcept(
		std::is_nothrow_move_constructible<Functor>::value)
		: is_active(true), f(fun) { }

	explicit ScopedGuard(Functor && fun) noexcept(
		std::is_nothrow_move_constructible<Functor>::value)
		: is_active(true), f(std::move(fun)) { }

	ScopedGuard(ScopedGuard && guard) noexcept(
		std::is_nothrow_move_constructible<Functor>::value)
		: is_active(guard.is_active), f(std::move(guard.f)) {
		guard.reset();
	}

	ScopedGuard &operator=(ScopedGuard &&guard) noexcept(
		std::swap(std::declval<Functor>(), std::declval<Functor>())) {
		std::swap(f, guard.f);
		is_active = guard.is_active;
		guard.reset();

		return *this;
	}

	~ScopedGuard() noexcept {
		if (is_active)
			f();
	}

	void reset() noexcept { is_active = false; }

	/* True if destructor and reset were not called */
	bool is_active;

private:
	ScopedGuard(const ScopedGuard &) = delete;
	ScopedGuard &operator=(const ScopedGuard &) = delete;

	/* User-defined functor that will be called in the destructor */
	Functor f;
};

template<typename Functor>
inline ScopedGuard<Functor> make_scoped_guard(Functor &&guard) {
	return ScopedGuard<Functor>(std::forward<Functor>(guard));
}

#endif /* TARANTOOL_SCOPED_GUARD_H_INCLUDED */
