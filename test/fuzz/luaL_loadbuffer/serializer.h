/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include "lua_grammar.pb.h"

using namespace lua_grammar;

#define PROTO_TOSTRING(TYPE, VAR_NAME) \
	std::string TYPE##ToString(const TYPE & (VAR_NAME))

/* PROTO_TOSTRING version for nested (depth=2) protobuf messages. */
#define NESTED_PROTO_TOSTRING(TYPE, VAR_NAME, PARENT_MESSAGE) \
	std::string TYPE##ToString \
	(const PARENT_MESSAGE::TYPE & (VAR_NAME))

/**
 * Fuzzing parameters:
 * kMaxNumber - upper bound for all generated numbers.
 * kMinNumber - lower bound for all generated numbers.
 * kMaxStrLength - upper bound for generating string literals and identifiers.
 * kMaxIdentifiers - max number of unique generated identifiers.
 * kDefaultIdent - default name for identifier.
 * Default values were chosen arbitrary but not too big for better readability
 * of generated code samples.
 */
constexpr double kMaxNumber = 1000.0;
constexpr double kMinNumber = -1000.0;
constexpr size_t kMaxStrLength = 20;
constexpr size_t kMaxIdentifiers = 10;
constexpr char kDefaultIdent[] = "Name";

PROTO_TOSTRING(Block, block);
PROTO_TOSTRING(Chunk, chunk);

PROTO_TOSTRING(Statement, stat);

/** LastStatement and nested types. */
PROTO_TOSTRING(LastStatement, laststat);
NESTED_PROTO_TOSTRING(ReturnOptionalExpressionList, explist, LastStatement);

/**
 * Statement options.
 */

/** AssignmentList and nested types. */
PROTO_TOSTRING(AssignmentList, assignmentlist);
NESTED_PROTO_TOSTRING(VariableList, varlist, AssignmentList);

/** FunctionCall and nested types. */
PROTO_TOSTRING(FunctionCall, call);
NESTED_PROTO_TOSTRING(Args, args, FunctionCall);
NESTED_PROTO_TOSTRING(PrefixArgs, prefixargs, FunctionCall);
NESTED_PROTO_TOSTRING(PrefixNamedArgs, prefixnamedargs, FunctionCall);

/** DoBlock, WhileCycle and RepeatCycle clauses. */
PROTO_TOSTRING(DoBlock, block);
PROTO_TOSTRING(WhileCycle, whilecycle);
PROTO_TOSTRING(RepeatCycle, repeatcycle);

/** IfStatement and nested types. */
PROTO_TOSTRING(IfStatement, statement);
NESTED_PROTO_TOSTRING(ElseIfBlock, elseifblock, IfStatement);

/** ForCycleName and ForCycleList clauses. */
PROTO_TOSTRING(ForCycleName, forcyclename);
PROTO_TOSTRING(ForCycleList, forcyclelist);

/** Function and nested types. */
PROTO_TOSTRING(Function, func);
NESTED_PROTO_TOSTRING(FuncName, funcname, Function);

PROTO_TOSTRING(NameList, namelist);
PROTO_TOSTRING(FuncBody, body);
NESTED_PROTO_TOSTRING(NameListWithEllipsis, namelist, FuncBody);
NESTED_PROTO_TOSTRING(ParList, parlist, FuncBody);

/** LocalFunc and LocalNames clauses. */
PROTO_TOSTRING(LocalFunc, localfunc);
PROTO_TOSTRING(LocalNames, localnames);

/**
 * Expressions and variables.
 */

/** Expressions clauses. */
PROTO_TOSTRING(ExpressionList, explist);
PROTO_TOSTRING(OptionalExpressionList, explist);
PROTO_TOSTRING(PrefixExpression, prefExpr);

/* Variable and nested types. */
PROTO_TOSTRING(Variable, var);
NESTED_PROTO_TOSTRING(IndexWithExpression, indexexpr, Variable);
NESTED_PROTO_TOSTRING(IndexWithName, indexname, Variable);

/** Expression and nested types. */
PROTO_TOSTRING(Expression, expr);
NESTED_PROTO_TOSTRING(AnonFunc, function, Expression);
NESTED_PROTO_TOSTRING(ExpBinaryOpExp, binary, Expression);
NESTED_PROTO_TOSTRING(UnaryOpExp, unary, Expression);

/**
 * Tables and fields.
 */
PROTO_TOSTRING(TableConstructor, table);
PROTO_TOSTRING(FieldList, fieldlist);
NESTED_PROTO_TOSTRING(FieldWithFieldSep, field, FieldList);

/** Field and nested types. */
PROTO_TOSTRING(Field, field);
NESTED_PROTO_TOSTRING(ExpressionAssignment, assignment, Field);
NESTED_PROTO_TOSTRING(NameAssignment, assignment, Field);
PROTO_TOSTRING(FieldSep, sep);

/** Operators. */
PROTO_TOSTRING(BinaryOperator, op);
PROTO_TOSTRING(UnaryOperator, op);

/** Identifier (Name). */
PROTO_TOSTRING(Name, name);
