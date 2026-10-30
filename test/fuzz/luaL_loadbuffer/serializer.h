/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <string>

#include "lua_grammar.pb.h"

namespace luajit_fuzzer {

/**
 * Fuzzing parameters:
 * kMaxCounterValue - value used in the condition
 * `if counter > kMaxCounterValue then break end` or, in functions,
 * `if counter > kMaxCounterValue then return end`. It is used to
 * prevent serealized code from encountering infinite recursions
 * and cycles.
 * kMaxNumber - upper bound for all generated numbers.
 * kMinNumber - lower bound for all generated numbers.
 * kMaxStrLength - upper bound for generating string literals and identifiers.
 * kMaxIdentifiers - max number of unique generated identifiers.
 * kDefaultIdent - default name for identifier.
 * Default values were chosen arbitrary but not too big for better readability
 * of generated code samples.
 */
constexpr std::size_t kMaxCounterValue = 5;
constexpr double kMaxNumber = 1000.0;
constexpr double kMinNumber = -1000.0;
constexpr size_t kMaxStrLength = 20;
constexpr size_t kMaxIdentifiers = 10;
constexpr char kDefaultIdent[] = "Name";

/**
 * Entry point for the serializer. Generates a Lua program from a
 * protobuf message with all counter initializations placed above
 * the serialized message. The purpose of the counters is to
 * address the timeout problem caused by infinite cycles and
 * recursions.
 */
std::string
MainBlockToString(const lua_grammar::Block &block);

} /* namespace luajit_fuzzer */
