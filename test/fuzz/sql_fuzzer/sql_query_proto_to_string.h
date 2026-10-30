/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#include <string>
#include "sql_query.pb.h"

namespace sql_fuzzer {
std::string
SQLQueryToString(const sql_query::SQLQuery &query);
}
