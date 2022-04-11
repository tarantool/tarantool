/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

namespace util {
/**
 * Make a copy of the template object by using its copy constructor.
 */
template <class T>
T *
copy(const T *to_copy) { return new T(*to_copy); }

} /* namespace util */
