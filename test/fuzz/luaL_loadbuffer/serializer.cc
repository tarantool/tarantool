/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "serializer.h"

#include <stack>
#include <string>

#include <trivia/util.h>

using namespace lua_grammar;

extern char preamble_lua[];

#define PROTO_TOSTRING(TYPE, VAR_NAME) \
	std::string TYPE##ToString(const TYPE & (VAR_NAME))

/* PROTO_TOSTRING version for nested (depth=2) protobuf messages. */
#define NESTED_PROTO_TOSTRING(TYPE, VAR_NAME, PARENT_MESSAGE) \
	std::string TYPE##ToString \
	(const PARENT_MESSAGE::TYPE & (VAR_NAME))

namespace luajit_fuzzer {
namespace {

const std::string kCounterNamePrefix = "counter_";
const std::string kNumberWrapperName = "always_number";

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

std::string
NumberWrappedExpressionToString(const Expression &expr)
{
	std::string retval;
	retval += kNumberWrapperName;
	retval += "(";
	retval += ExpressionToString(expr);
	retval += ")";

	return retval;
}

/**
 * Class that controls id creation for counters. Basically, a
 * variable wrapper that guarantees variable to be incremented.
 */
class CounterIdProvider {
public:
	/** Returns number of id provided. */
	std::size_t count()
	{
		return id_;
	}

	/** Returns a new id that was not used after last clean(). */
	std::size_t next()
	{
		return id_++;
	}

	/**
	 * Cleans history. Should be used to make fuzzer starts
	 * independent.
	 */
	void clean()
	{
		id_ = 0;
	}

private:
	std::size_t id_ = 0;
};

/** A singleton for counter id provider. */
CounterIdProvider&
GetCounterIdProvider()
{
	static CounterIdProvider provider;
	return provider;
}

std::string
GetCounterName(std::size_t id)
{
	return kCounterNamePrefix + std::to_string(id);
}

/** Returns `<counter_name> = <counter_name> + 1`. */
std::string
GetCounterIncrement(const std::string &counter_name)
{
	std::string retval = counter_name;
	retval += " = ";
	retval += counter_name;
	retval += " + 1\n";
	return retval;
}

/**
 * Returns `if <counter_name> > kMaxCounterValue then
 * <then_block> end`.
 */
std::string
GetCondition(const std::string &counter_name, const std::string &then_block)
{
	std::string retval = "if ";
	retval += counter_name;
	retval += " > ";
	retval += std::to_string(kMaxCounterValue);
	retval += " then ";
	retval += then_block;
	retval += " end\n";
	return retval;
}

/**
 * Class that registers and provides context during code
 * generation.
 * Used to generate correct Lua code.
 */
class Context {
public:
	enum class BlockType {
		kReturnable,
		kBreakable,
		kReturnableWithVararg,
	};

	void step_in(BlockType type)
	{
		block_stack_.push(type);
		if (block_type_is_returnable_(type)) {
			returnable_stack_.push(type);
		}
	}

	void step_out()
	{
		assert(!block_stack_.empty());
		if (block_type_is_returnable_(block_stack_.top())) {
			assert(!returnable_stack_.empty());
			returnable_stack_.pop();
		}
		block_stack_.pop();
	}

	std::string get_next_block_setup()
	{
		std::size_t id = GetCounterIdProvider().next();
		std::string counter_name = GetCounterName(id);

		return GetCondition(counter_name, get_exit_statement_()) +
		       GetCounterIncrement(counter_name);
	}

	bool break_is_possible()
	{
		return !block_stack_.empty() &&
		       block_stack_.top() == BlockType::kBreakable;
	}

	bool return_is_possible()
	{
		return !returnable_stack_.empty();
	}

	bool vararg_is_possible()
	{
		return (returnable_stack_.empty() ||
			(!returnable_stack_.empty() &&
			 returnable_stack_.top() ==
				BlockType::kReturnableWithVararg));
	}

private:

	bool block_type_is_returnable_(BlockType type)
	{
		switch (type) {
		case BlockType::kBreakable:
			return false;
		case BlockType::kReturnable:
		case BlockType::kReturnableWithVararg:
			return true;
		}
		unreachable();
	}

	std::string get_exit_statement_()
	{
		assert(!block_stack_.empty());
		switch (block_stack_.top()) {
		case BlockType::kBreakable:
			return "break";
		case BlockType::kReturnable:
		case BlockType::kReturnableWithVararg:
			return "return";
		}
		unreachable();
	}

	std::stack<BlockType> block_stack_;
	/*
	 * The returnable block can be exited with return from
	 * the breakable block within it, but the breakable block
	 * cannot be exited with break from the returnable block within
	 * it.
	 * Valid code:
	 * `function foo() while true do return end end`
	 * Erroneous code:
	 * `while true do function foo() break end end`
	 * This stack is used to check if `return` is possible.
	 */
	std::stack<BlockType> returnable_stack_;
};

Context&
GetContext()
{
	static Context context;
	return context;
}

/**
 * Block may be placed not only in a cycle, so specially for cycles
 * there is a function that will add a break condition and a
 * counter increment.
 */
std::string
BlockToStringCycleProtected(const Block &block)
{
	std::string retval = GetContext().get_next_block_setup();
	retval += ChunkToString(block.chunk());
	return retval;
}

/**
 * DoBlock may be placed not only in a cycle, so specially for
 * cycles there is a function that will call
 * BlockToStringCycleProtected().
 */
std::string
DoBlockToStringCycleProtected(const DoBlock &block)
{
	std::string retval = "do\n";
	retval += BlockToStringCycleProtected(block.block());
	retval += "end\n";
	return retval;
}

/**
 * FuncBody may contain recursive calls, so for all function bodies,
 * there is a function that adds a return condition and a counter
 * increment.
 */
std::string
FuncBodyToStringReqProtected(const FuncBody &body)
{
	std::string body_str = "( ";
	if (body.has_parlist()) {
		body_str += ParListToString(body.parlist());
	}
	body_str += " )\n\t";

	body_str += GetContext().get_next_block_setup();

	body_str += BlockToString(body.block());
	body_str += "end\n";
	return body_str;
}

bool
FuncBodyHasVararg(const FuncBody &body)
{
	if (!body.has_parlist()) {
		return false;
	}
	const FuncBody::ParList &parlist = body.parlist();
	switch (parlist.parlist_oneof_case()) {
	case FuncBody::ParList::ParlistOneofCase::kNamelist:
		return parlist.namelist().has_ellipsis();
	case FuncBody::ParList::ParlistOneofCase::kEllipsis:
		return true;
	default:
		return parlist.namelist().has_ellipsis();
	}
}

Context::BlockType
GetFuncBodyType(const FuncBody &body)
{
	return FuncBodyHasVararg(body) ?
		Context::BlockType::kReturnableWithVararg :
		Context::BlockType::kReturnable;
}

std::string
ClearIdentifier(const std::string &identifier)
{
	std::string cleared;

	bool has_first_not_digit = false;
	for (char c : identifier) {
		if (has_first_not_digit && (std::iswalnum(c) || c == '_')) {
			cleared += c;
		} else if (std::isalpha(c) || c == '_') {
			has_first_not_digit = true;
			cleared += c;
		}
	}
	return cleared;
}

inline std::string
clamp(std::string s, size_t maxSize = kMaxStrLength)
{
	if (s.size() > maxSize)
		s.resize(maxSize);
	return s;
}

inline double
clamp(double number, double upper, double lower)
{
	return number <= lower ? lower :
	       number >= upper ? upper : number;
}

inline std::string
ConvertToStringDefault(const std::string &s)
{
	std::string ident = ClearIdentifier(s);
	ident = clamp(ident);
	if (ident.empty())
		return std::string(kDefaultIdent);
	return ident;
}

PROTO_TOSTRING(Block, block)
{
	return ChunkToString(block.chunk());
}

PROTO_TOSTRING(Chunk, chunk)
{
	std::string chunk_str;
	for (int i = 0; i < chunk.stat_size(); ++i)
		chunk_str += StatementToString(chunk.stat(i)) + "\n";

	if (chunk.has_laststat())
		chunk_str += LastStatementToString(chunk.laststat()) + "\n";

	return chunk_str;
}

/**
 * LastStatement and nested types.
 */
PROTO_TOSTRING(LastStatement, laststat)
{
	std::string laststat_str;
	using LastStatType = LastStatement::LastOneofCase;
	switch (laststat.last_oneof_case()) {
	case LastStatType::kExplist:
		laststat_str = ReturnOptionalExpressionListToString(
			laststat.explist());
		break;
	case LastStatType::kBreak:
		if (GetContext().break_is_possible()) {
			laststat_str = "break";
		}
		break;
	default:
		/* Chosen as default in order to decrease number of 'break's. */
		laststat_str = ReturnOptionalExpressionListToString(
			laststat.explist());
		break;
	}

	/*
	 * Add a semicolon when last statement is not empty
	 * to avoid errors like:
	 *
	 * <preamble.lua>
	 * (nil):Name0()
	 * (nil)() -- ambiguous syntax (function call x new statement) near '('
	 */
	if (!laststat_str.empty())
		laststat_str += "; ";

	return laststat_str;
}

NESTED_PROTO_TOSTRING(ReturnOptionalExpressionList, explist, LastStatement)
{
	if (!GetContext().return_is_possible()) {
		return "";
	}

	std::string explist_str = "return";
	if (explist.has_explist()) {
		explist_str += " " + ExpressionListToString(explist.explist());
		explist_str += " ";
	}
	return explist_str;
}

/**
 * Statement and statement options.
 */
PROTO_TOSTRING(Statement, stat)
{
	std::string stat_str;
	using StatType = Statement::StatOneofCase;
	switch (stat.stat_oneof_case()) {
	case StatType::kList:
		stat_str = AssignmentListToString(stat.list());
		break;
	case StatType::kCall:
		stat_str = FunctionCallToString(stat.call());
		break;
	case StatType::kBlock:
		stat_str = DoBlockToString(stat.block());
		break;
	case StatType::kWhilecycle:
		stat_str = WhileCycleToString(stat.whilecycle());
		break;
	case StatType::kRepeatcycle:
		stat_str = RepeatCycleToString(stat.repeatcycle());
		break;
	case StatType::kIfstat:
		stat_str = IfStatementToString(stat.ifstat());
		break;
	case StatType::kForcyclename:
		stat_str = ForCycleNameToString(stat.forcyclename());
		break;
	case StatType::kForcyclelist:
		stat_str = ForCycleListToString(stat.forcyclelist());
		break;
	case StatType::kFunc:
		stat_str = FunctionToString(stat.func());
		break;
	case StatType::kLocalfunc:
		stat_str = LocalFuncToString(stat.localfunc());
		break;
	case StatType::kLocalnames:
		stat_str = LocalNamesToString(stat.localnames());
		break;
	default:
		/**
		 * Chosen arbitrarily more for simplicity.
		 * TODO: Choose "more interesting" defaults.
		 */
		stat_str = AssignmentListToString(stat.list());
		break;
	}

	/*
	 * Always add a semicolon regardless of grammar
	 * to avoid errors like:
	 *
	 * <preamble.lua>
	 * (nil):Name0()
	 * (nil)() -- ambiguous syntax (function call x new statement) near '('
	 */
	stat_str += "; ";

	return stat_str;
}

/**
 * AssignmentList and nested types.
 */
PROTO_TOSTRING(AssignmentList, assignmentlist)
{
	std::string list_str = VariableListToString(assignmentlist.varlist());
	list_str += " = " + ExpressionListToString(assignmentlist.explist());
	return list_str;
}

NESTED_PROTO_TOSTRING(VariableList, varlist, AssignmentList)
{
	std::string varlist_str = VariableToString(varlist.var());
	for (int i = 0; i < varlist.vars_size(); ++i) {
		varlist_str += ", " + VariableToString(varlist.vars(i));
		varlist_str += " ";
	}
	return varlist_str;
}

/**
 * FunctionCall and nested types.
 */
PROTO_TOSTRING(FunctionCall, call)
{
	using FuncCallType = FunctionCall::CallOneofCase;
	switch (call.call_oneof_case()) {
	case FuncCallType::kPrefArgs:
		return PrefixArgsToString(call.prefargs());
	case FuncCallType::kNamedArgs:
		return PrefixNamedArgsToString(call.namedargs());
	default:
		/* Chosen for more variability of generated programs. */
		return PrefixNamedArgsToString(call.namedargs());
	}
}

NESTED_PROTO_TOSTRING(Args, args, FunctionCall)
{
	using ArgsType = FunctionCall::Args::ArgsOneofCase;
	switch (args.args_oneof_case()) {
	case ArgsType::kExplist:
		return "(" + OptionalExpressionListToString(args.explist()) +
		       ")";
	case ArgsType::kTableconstructor:
		return TableConstructorToString(args.tableconstructor());
	case ArgsType::kStr:
		return "'" + ConvertToStringDefault(args.str()) + "'";
	default:
		/* For more variability. */
		return TableConstructorToString(args.tableconstructor());
	}
}

NESTED_PROTO_TOSTRING(PrefixArgs, prefixargs, FunctionCall)
{
	std::string prefixargs_str = PrefixExpressionToString(
		prefixargs.prefixexp());
	prefixargs_str += " " + ArgsToString(prefixargs.args());
	return prefixargs_str;
}

NESTED_PROTO_TOSTRING(PrefixNamedArgs, prefixnamedargs, FunctionCall)
{
	std::string predixnamedargs_str = PrefixExpressionToString(
		prefixnamedargs.prefixexp());
	predixnamedargs_str += ":" + NameToString(prefixnamedargs.name());
	predixnamedargs_str += " " + ArgsToString(prefixnamedargs.args());
	return predixnamedargs_str;
}

/**
 * DoBlock clause.
 */
PROTO_TOSTRING(DoBlock, block)
{
	return "do\n" + BlockToString(block.block()) + "end\n";
}

/**
 * WhileCycle clause.
 */
PROTO_TOSTRING(WhileCycle, whilecycle)
{
	GetContext().step_in(Context::BlockType::kBreakable);

	std::string whilecycle_str = "while ";
	whilecycle_str += ExpressionToString(whilecycle.condition());
	whilecycle_str += " ";
	whilecycle_str += DoBlockToStringCycleProtected(whilecycle.doblock());

	GetContext().step_out();
	return whilecycle_str;
}

/**
 * RepeatCycle clause.
 */
PROTO_TOSTRING(RepeatCycle, repeatcycle)
{
	GetContext().step_in(Context::BlockType::kBreakable);

	std::string repeatcycle_str = "repeat\n";
	repeatcycle_str += BlockToStringCycleProtected(repeatcycle.block());
	repeatcycle_str += "until ";
	repeatcycle_str += ExpressionToString(repeatcycle.condition());

	GetContext().step_out();
	return repeatcycle_str;
}

/**
 * IfStatement and nested types.
 */
PROTO_TOSTRING(IfStatement, statement)
{
	std::string statement_str = "if " +
		ExpressionToString(statement.condition());
	statement_str += " then\n\t" + BlockToString(statement.first());

	for (int i = 0; i < statement.clauses_size(); ++i)
		statement_str += ElseIfBlockToString(statement.clauses(i));

	if (statement.has_last())
		statement_str += "else\n\t" + BlockToString(statement.last());

	statement_str += "end\n";
	return statement_str;
}

NESTED_PROTO_TOSTRING(ElseIfBlock, elseifblock, IfStatement)
{
	std::string elseifblock_str = "elseif ";
	elseifblock_str += ExpressionToString(elseifblock.condition());
	elseifblock_str += " then\n\t";
	elseifblock_str += BlockToString(elseifblock.block());
	return elseifblock_str;
}

/**
 * ForCycleName clause.
 * TODO: In 'for i = start, stop, step' construction start, stop, step
 * should be numbers. So results of the corresponding expressions
 * should be number.
 */
PROTO_TOSTRING(ForCycleName, forcyclename)
{
	GetContext().step_in(Context::BlockType::kBreakable);

	std::string forcyclename_str = "for ";
	forcyclename_str += NameToString(forcyclename.name());
	forcyclename_str += " = ";
	forcyclename_str += NumberWrappedExpressionToString(
		forcyclename.startexp());
	forcyclename_str += ", ";
	forcyclename_str += NumberWrappedExpressionToString(
		forcyclename.stopexp());

	if (forcyclename.has_stepexp())
		forcyclename_str += ", " + NumberWrappedExpressionToString(
			forcyclename.stepexp());

	forcyclename_str += " ";
	forcyclename_str += DoBlockToStringCycleProtected(
		forcyclename.doblock());

	GetContext().step_out();
	return forcyclename_str;
}

/**
 * ForCycleList clause.
 */
PROTO_TOSTRING(ForCycleList, forcyclelist)
{
	GetContext().step_in(Context::BlockType::kBreakable);

	std::string forcyclelist_str = "for ";
	forcyclelist_str += NameListToString(forcyclelist.names());
	forcyclelist_str += " in ";
	forcyclelist_str += ExpressionListToString(forcyclelist.expressions());
	forcyclelist_str += " ";
	forcyclelist_str += DoBlockToStringCycleProtected(
		forcyclelist.doblock());

	GetContext().step_out();
	return forcyclelist_str;
}

/**
 * Function and nested types.
 */
PROTO_TOSTRING(Function, func)
{
	GetContext().step_in(GetFuncBodyType(func.body()));

	std::string func_str = "function ";
	func_str += FuncNameToString(func.name());
	func_str += FuncBodyToStringReqProtected(func.body());

	GetContext().step_out();
	return func_str;
}

NESTED_PROTO_TOSTRING(FuncName, funcname, Function)
{
	std::string funcname_str = NameToString(funcname.firstname());

	for (int i = 0; i < funcname.names_size(); ++i)
		funcname_str += "." + NameToString(funcname.names(i));

	if (funcname.has_lastname())
		funcname_str += ":" + NameToString(funcname.lastname());

	return funcname_str;
}

PROTO_TOSTRING(NameList, namelist)
{
	std::string namelist_str = NameToString(namelist.firstname());
	for (int i = 0; i < namelist.names_size(); ++i)
		namelist_str += ", " + NameToString(namelist.names(i));
	return namelist_str;
}

NESTED_PROTO_TOSTRING(NameListWithEllipsis, namelist, FuncBody)
{
	std::string namelist_str = NameListToString(namelist.namelist());
	if (namelist.has_ellipsis())
		namelist_str += ", ...";
	return namelist_str;
}

NESTED_PROTO_TOSTRING(ParList, parlist, FuncBody)
{
	using ParListType = FuncBody::ParList::ParlistOneofCase;
	switch (parlist.parlist_oneof_case()) {
	case ParListType::kNamelist:
		return NameListWithEllipsisToString(parlist.namelist());
	case ParListType::kEllipsis:
		return "...";
	default:
		/* Chosen as default in order to decrease number of ellipses. */
		return NameListWithEllipsisToString(parlist.namelist());
	}
}

/**
 * LocalFunc clause.
 */
PROTO_TOSTRING(LocalFunc, localfunc)
{
	GetContext().step_in(GetFuncBodyType(localfunc.funcbody()));

	std::string localfunc_str = "local function ";
	localfunc_str += NameToString(localfunc.name());
	localfunc_str += " ";
	localfunc_str += FuncBodyToStringReqProtected(localfunc.funcbody());

	GetContext().step_out();
	return localfunc_str;
}

/**
 * LocalNames clause.
 */
PROTO_TOSTRING(LocalNames, localnames)
{
	std::string localnames_str = "local ";
	localnames_str += NameListToString(localnames.namelist());

	if (localnames.has_explist())
		localnames_str += " = " + ExpressionListToString(
			localnames.explist());
	return localnames_str;
}

/**
 * Expressions and variables.
 */

/**
 * Expressions clauses.
 */
PROTO_TOSTRING(ExpressionList, explist)
{
	std::string explist_str;
	for (int i = 0; i < explist.expressions_size(); ++i)
		explist_str += ExpressionToString(explist.expressions(i)) +
				", ";
	explist_str += ExpressionToString(explist.explast()) + " ";
	return explist_str;
}

PROTO_TOSTRING(OptionalExpressionList, explist)
{
	if (explist.has_explist())
		return ExpressionListToString(explist.explist());
	return "";
}

PROTO_TOSTRING(PrefixExpression, prefixexp)
{
	using PrefExprType = PrefixExpression::PrefixOneofCase;
	switch (prefixexp.prefix_oneof_case()) {
	case PrefExprType::kVar:
		return VariableToString(prefixexp.var());
	case PrefExprType::kFunctioncall:
		return FunctionCallToString(prefixexp.functioncall());
	case PrefExprType::kExp:
		return "(" + ExpressionToString(prefixexp.exp()) + ")";
	default:
		/*
		 * Can be generated too nested expressions with other options,
		 * though they can be enabled for more variable fuzzing.
		 */
		return VariableToString(prefixexp.var());
	}
}

/**
 * Variable and nested types.
 */
PROTO_TOSTRING(Variable, var)
{
	using VarType = Variable::VarOneofCase;
	switch (var.var_oneof_case()) {
	case VarType::kName:
		return NameToString(var.name());
	case VarType::kIndexexpr:
		return IndexWithExpressionToString(var.indexexpr());
	case VarType::kIndexname:
		return IndexWithNameToString(var.indexname());
	default:
		/*
		 * Can be generated too nested expressions with other options,
		 * though they can be enabled for more variable fuzzing.
		 */
		return NameToString(var.name());
	}
}

NESTED_PROTO_TOSTRING(IndexWithExpression, indexexpr, Variable)
{
	std::string indexexpr_str = PrefixExpressionToString(
		indexexpr.prefixexp());
	indexexpr_str += "[" + ExpressionToString(indexexpr.exp()) + "]";
	return indexexpr_str;
}

NESTED_PROTO_TOSTRING(IndexWithName, indexname, Variable)
{
	std::string indexname_str = PrefixExpressionToString(
		indexname.prefixexp());
	indexname_str += "." + ConvertToStringDefault(indexname.name());
	return indexname_str;
}

/**
 * Expression and nested types.
 */
PROTO_TOSTRING(Expression, expr)
{
	using ExprType = Expression::ExprOneofCase;
	switch (expr.expr_oneof_case()) {
	case ExprType::kNil:
		return "nil";
	case ExprType::kFalse:
		return "false";
	case ExprType::kTrue:
		return "true";
	case ExprType::kNumber: {
		/* Clamp number between given boundaries. */
		double number = clamp(expr.number(), kMaxNumber, kMinNumber);
		return std::to_string(number);
	}
	case ExprType::kStr:
		return "'" + ConvertToStringDefault(expr.str()) + "'";
	case ExprType::kEllipsis:
		if (GetContext().vararg_is_possible()) {
			return " ... ";
		} else {
			return " nil";
		}
	case ExprType::kFunction:
		return AnonFuncToString(expr.function());
	case ExprType::kPrefixexp:
		return PrefixExpressionToString(expr.prefixexp());
	case ExprType::kTableconstructor:
		return TableConstructorToString(expr.tableconstructor());
	case ExprType::kBinary:
		return ExpBinaryOpExpToString(expr.binary());
	case ExprType::kUnary:
		return UnaryOpExpToString(expr.unary());
	default:
		/**
		 * Arbitrary choice.
		 * TODO: Choose "more interesting" defaults.
		 */
		return "'" + ConvertToStringDefault(expr.str()) + "'";
	}
}

NESTED_PROTO_TOSTRING(AnonFunc, func, Expression)
{
	GetContext().step_in(GetFuncBodyType(func.body()));

	std::string retval = "function ";
	retval += FuncBodyToStringReqProtected(func.body());

	GetContext().step_out();
	return retval;
}

NESTED_PROTO_TOSTRING(ExpBinaryOpExp, binary, Expression)
{
	std::string binary_str = ExpressionToString(binary.leftexp());
	binary_str += " " + BinaryOperatorToString(binary.binop()) + " ";
	binary_str += ExpressionToString(binary.rightexp());
	return binary_str;
}

NESTED_PROTO_TOSTRING(UnaryOpExp, unary, Expression)
{
	std::string unary_str = UnaryOperatorToString(unary.unop());
	/*
	 * Add a whitespace before an expression with unary minus,
	 * otherwise double hyphen comments the following code
	 * and it breaks generated programs syntactically.
	 */
	unary_str += " " + ExpressionToString(unary.exp());
	return unary_str;
}

/**
 * Tables and fields.
 */
PROTO_TOSTRING(TableConstructor, table)
{
	std::string table_str = " (setmetatable({ ";
	if (table.has_fieldlist())
		table_str += FieldListToString(table.fieldlist());
	table_str += " }, table_mt))()";
	return table_str;
}

PROTO_TOSTRING(FieldList, fieldlist)
{
	std::string fieldlist_str = FieldToString(fieldlist.firstfield());
	for (int i = 0; i < fieldlist.fields_size(); ++i)
		fieldlist_str += FieldWithFieldSepToString(fieldlist.fields(i));
	if (fieldlist.has_lastsep())
		fieldlist_str += FieldSepToString(fieldlist.lastsep());
	return fieldlist_str;
}

NESTED_PROTO_TOSTRING(FieldWithFieldSep, field, FieldList)
{
	std::string field_str = FieldSepToString(field.sep());
	field_str += " " + FieldToString(field.field());
	return field_str;
}

/**
 * Field and nested types.
 */
PROTO_TOSTRING(Field, field)
{
	using FieldType = Field::FieldOneofCase;
	switch (field.field_oneof_case()) {
	case FieldType::kExprassign:
		return ExpressionAssignmentToString(field.exprassign());
	case FieldType::kNamedassign:
		return NameAssignmentToString(field.namedassign());
	case FieldType::kExpression:
		return ExpressionToString(field.expression());
	default:
		/* More common case of using fields. */
		return NameAssignmentToString(field.namedassign());
	}
}

NESTED_PROTO_TOSTRING(ExpressionAssignment, assignment, Field)
{
	std::string assignment_str = "[ " +
		ExpressionToString(assignment.key()) + " ]";
	assignment_str += " = " + ExpressionToString(assignment.value());
	return assignment_str;
}

NESTED_PROTO_TOSTRING(NameAssignment, assignment, Field)
{
	std::string assignment_str = NameToString(assignment.name());
	assignment_str += " = " + ExpressionToString(assignment.value());
	return assignment_str;
}

PROTO_TOSTRING(FieldSep, sep)
{
	using FieldSepType = FieldSep::SepOneofCase;
	switch (sep.sep_oneof_case()) {
	case FieldSepType::kComma:
		return ",";
	case FieldSepType::kSemicolon:
		return ";";
	default:
		return ",";
	}
}

/**
 * Operators.
 */
PROTO_TOSTRING(BinaryOperator, op)
{
	using BinopType = BinaryOperator::BinaryOneofCase;
	switch (op.binary_oneof_case()) {
	case BinopType::kAdd:
		return "+";
	case BinopType::kSub:
		return "-";
	case BinopType::kMult:
		return "*";
	case BinopType::kDiv:
		return "/";
	case BinopType::kExp:
		return "^";
	case BinopType::kMod:
		return "%";

	case BinopType::kConcat:
		return "..";

	case BinopType::kLess:
		return "<";
	case BinopType::kLessEqual:
		return "<=";
	case BinopType::kGreater:
		return ">";
	case BinopType::kGreaterEqual:
		return ">=";
	case BinopType::kEqual:
		return "==";
	case BinopType::kNotEqual:
		return "~=";
	case BinopType::kAnd:
		return "and";
	case BinopType::kOr:
		return "or";
	default:
		/* Works in most cases. */
		return "==";
	}
}

PROTO_TOSTRING(UnaryOperator, op)
{
	using UnaryopType = UnaryOperator::UnaryOneofCase;
	switch (op.unary_oneof_case()) {
	case UnaryopType::kNegate:
		return "-";
	case UnaryopType::kNot:
		return "not ";
	case UnaryopType::kLength:
		return "#";
	default:
		/* Works in most cases. */
		return "not ";
	}
}

/**
 * Identifier (Name).
 */
PROTO_TOSTRING(Name, name)
{
	std::string ident = ConvertToStringDefault(name.name());
	return ident + std::to_string(name.num() % kMaxIdentifiers);
}

} /* namespace */

std::string
MainBlockToString(const Block &block)
{
	GetCounterIdProvider().clean();

	std::string block_str = BlockToString(block);
	std::string retval = preamble_lua;

	for (size_t i = 0; i < GetCounterIdProvider().count(); ++i) {
		retval += GetCounterName(i);
		retval += " = 0\n";
	}
	retval += block_str;

	return retval;
}

} /* namespace luajit_fuzzer */
