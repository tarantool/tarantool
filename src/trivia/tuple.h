/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <tuple>

namespace util {

/** Helper for util::get. */
template<int Index, class Search, class First, class... Types>
struct get_internal {
	using type = typename get_internal<Index + 1, Search, Types...>::type;
	static constexpr int index = Index;
};

template<int Index, class Search, class... Types>
struct get_internal<Index, Search, Search, Types...> {
	using type = get_internal;
	static constexpr int index = Index;
};

/** std::get by type for C++11. */
template<class T, class... Types>
constexpr T &
get(std::tuple<Types...> &tuple)
{
	return std::get<get_internal<0, T, Types...>::type::index>(tuple);
}

} /* namespace util */
