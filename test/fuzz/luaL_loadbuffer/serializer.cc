/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "serializer.h"

static inline std::string
RemoveLeadingNumbers(const std::string &s)
{
	for (size_t i = 0; i < s.length(); ++i)
		if (!std::isdigit(s[i]))
			return s.substr(i);
	return "";
}

static inline std::string
ClearNonIdentifierSymbols(const std::string &s)
{
	std::string cleared;

	if (std::isalpha(s[0]) || s[0] == '_')
		cleared += s[0];

	for (size_t i = 1; i < s.length(); ++i)
		if (std::iswalnum(s[i]) || s[i] == '_')
			cleared += s[i];

	return cleared;
}

static inline std::string
clamp(std::string s, size_t maxSize = kMaxStrLength)
{
	if (s.size() > maxSize)
		s.resize(maxSize);
	return s;
}

static inline double
clamp(double number, double upper, double lower)
{
	return number <= lower ? lower :
	       number >= upper ? upper : number;
}

static inline std::string
ConvertToStringDefault(const std::string &s)
{
	std::string ident = RemoveLeadingNumbers(s);
	ident = clamp(ClearNonIdentifierSymbols(ident));
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
		laststat_str = "break";
		break;
	default:
		/* Chosen as default in order to decrease number of 'break's. */
		laststat_str = ReturnOptionalExpressionListToString(
			laststat.explist());
		break;
	}

	if (laststat.has_semicolon())
		laststat_str += "; ";

	return laststat_str;
}

NESTED_PROTO_TOSTRING(ReturnOptionalExpressionList, explist, LastStatement)
{
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
	/**
	 * TODO:
	 * Commented due to possible generation of infinite loops.
	 * In that case, fuzzer will drop only by timeout.
	 * Example: 'while true do end'.
	 */
	/*
	 * case StatType::kWhilecycle:
	 *      stat_str = WhileCycleToString(stat.whilecycle());
	 * case StatType::kRepeatcycle:
	 *	stat_str = RepeatCycleToString(stat.repeatcycle());
	 */
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

	if (stat.has_semicolon())
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
	std::string whilecycle_str = "while " + ExpressionToString(
		whilecycle.condition());
	whilecycle_str += " " + DoBlockToString(whilecycle.doblock());
	return whilecycle_str;
}

/**
 * RepeatCycle clause.
 */
PROTO_TOSTRING(RepeatCycle, repeatcycle)
{
	std::string repeatcycle_str = "repeat " + BlockToString(
		repeatcycle.block());
	repeatcycle_str += "until " + ExpressionToString(
		repeatcycle.condition());
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
	std::string elseifblock_str = "else if ";
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
	std::string forcyclename_str = "for " + NameToString(
		forcyclename.name());
	forcyclename_str += " = " + ExpressionToString(forcyclename.startexp());
	forcyclename_str += ", " + ExpressionToString(forcyclename.stopexp());

	if (forcyclename.has_stepexp())
		forcyclename_str += ", " + ExpressionToString(
			forcyclename.stepexp());

	forcyclename_str += " " + DoBlockToString(forcyclename.doblock());
	return forcyclename_str;
}

/**
 * ForCycleList clause.
 */
PROTO_TOSTRING(ForCycleList, forcyclelist)
{
	std::string forcyclelist_str = "for " + NameListToString(
		forcyclelist.names());
	forcyclelist_str += " in " + ExpressionListToString(
		forcyclelist.expressions());
	forcyclelist_str += " " + DoBlockToString(forcyclelist.doblock());
	return forcyclelist_str;
}

/**
 * Function and nested types.
 */
PROTO_TOSTRING(Function, func)
{
	std::string func_str = "function " + FuncNameToString(func.name());
	func_str += FuncBodyToString(func.body());
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

PROTO_TOSTRING(FuncBody, body)
{
	std::string body_str = "( ";
	if (body.has_parlist())
		body_str += ParListToString(body.parlist());
	body_str += " )\n\t";
	body_str += BlockToString(body.block());
	body_str += "end\n";
	return body_str;
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
	std::string localfunc_str = "local function " + NameToString(
		localfunc.name());
	localfunc_str += " " + FuncBodyToString(localfunc.funcbody());
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
		return " ... ";
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
	return "function " + FuncBodyToString(func.body());
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
	unary_str += ExpressionToString(unary.exp());
	return unary_str;
}

/**
 * Tables and fields.
 */
PROTO_TOSTRING(TableConstructor, table)
{
	std::string table_str = "{ ";
	if (table.has_fieldlist())
		table_str += FieldListToString(table.fieldlist());
	table_str += " }";
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
