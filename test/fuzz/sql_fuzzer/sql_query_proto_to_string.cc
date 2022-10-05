/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "sql_query_proto_to_string.h"

#include <algorithm>
#include <cstring>

using namespace sql_query;

/**
 * Every conversation function from SQL Query type to std::string must end with
 * a space symbol. Thus, no reserved or user words will accidentally
 * concatenate.
 */
#define PROTO_TOSTRING(TYPE, VAR_NAME) \
	std::string TYPE##ToString(const TYPE & (VAR_NAME))

namespace sql_fuzzer {

namespace {
constexpr uint32_t kMaxColumnNumber = 20;
constexpr uint32_t kMaxTableNumber = 8;
constexpr uint32_t kMaxColumnConstraintNumber = 10;
[[maybe_unused]]
constexpr uint32_t kMaxTableConstraintNumber = 10;
[[maybe_unused]]
constexpr uint32_t kMaxIndexNumber = 10;
} /* namespace */

PROTO_TOSTRING(Select, select);
PROTO_TOSTRING(FunctionExpression, expression);
PROTO_TOSTRING(Term, term);
PROTO_TOSTRING(PredicateExpression, expression);
PROTO_TOSTRING(JoinSource, source);

PROTO_TOSTRING(TableName, table_name) {
	std::string ret = "table";
	ret += std::to_string(table_name.code() % kMaxTableNumber);
	ret += " ";
	return ret;
}

PROTO_TOSTRING(ColumnName, column_name) {
	std::string ret = "column";
	ret += std::to_string(column_name.code() % kMaxColumnNumber);
	ret += " ";
	return ret;
}

PROTO_TOSTRING(VarChar, varchar) {
	std::string ret = "VARCHAR (";
	ret += std::to_string(varchar.integer());
	ret += ") ";
	return ret;
}

PROTO_TOSTRING(CollationClause, collation_clause) {
	std::string ret = "COLLATE \"";
	ret += CollationClause_CollationClauseEnum_Name(
		collation_clause.collation_clause_enum()
		);
	ret += "\" ";
	return ret;
}

PROTO_TOSTRING(CollatableDataType, type) {
	std::string ret;
	switch (type.collatable_data_type_oneof_case()) {
	case CollatableDataType::kTypeEnum:
		ret += CollatableDataType_CollatableDataTypeEnum_Name(
			type.type_enum()
			);
		ret += " ";
		break;
	case CollatableDataType::kVarchar:
		ret += VarCharToString(type.varchar());
		break;
	case CollatableDataType::COLLATABLE_DATA_TYPE_ONEOF_NOT_SET:
		ret += CollatableDataType_CollatableDataTypeEnum_Name(
			type.type_enum_fallback()
			);
		ret += " ";
		break;
	}
	if (type.has_collation_clause()) {
		ret += CollationClauseToString(type.collation_clause());
	}
	return ret;
}

PROTO_TOSTRING(DataType, type) {
	std::string ret;
	switch (type.data_type_oneof_case()) {
	case DataType::kSpecialTypeEnum:
		ret = DataType_SpecialDataTypeEnum_Name(
			type.special_type_enum()
			);
		ret += " ";
		return ret;
	case DataType::kTypeEnum:
		ret = DataType_DataTypeEnum_Name(type.type_enum());
		ret += " ";
		return ret;
	case DataType::kCollatableType:
		return CollatableDataTypeToString(type.collatable_type());
	case DataType::DATA_TYPE_ONEOF_NOT_SET:
		ret = DataType_DataTypeEnum_Name(type.type_enum_fallback());
		ret += " ";
		return ret;
	}
}

PROTO_TOSTRING(ColumnConstraintNullable, nullable) {
	switch (nullable.nullable_enum()) {
	case ColumnConstraintNullable::NOT_NULL:
		return "NOT NULL ";
	case ColumnConstraintNullable::NULLABLE:
		return "NULL ";
	}
}

PROTO_TOSTRING(ColumnConstraintName, constraint_name) {
	/* colcon is a constraction for column constraint */
	std::string ret = "colcon";
	ret += std::to_string(constraint_name.code() %
			      kMaxColumnConstraintNumber
			      );
	ret += " ";
	return ret;
}

static std::string
BooleanConstantToString(uint64_t value)
{
	return value ? "TRUE " : "FALSE ";
}

static std::string
DecimalConstantToString(uint64_t value)
{
	std::string ret = std::to_string(value);
	ret += " ";
	return ret;
}

static std::string
UUIDConstantToString(uint64_t value)
{
	std::string ret = std::to_string(value);
	ret += " ";
	return ret;
}

static std::string
VarbinaryToString(uint64_t value)
{
	std::string ret = "X\'";
	ret += std::to_string(value);
	ret += "\' ";
	return ret;
}

static std::string
StringConstantToString(uint64_t value)
{
	std::string ret = "\'";
	ret += std::to_string(value);
	ret += "\' ";
	return ret;
}

static std::string
DoubleConstantToString(uint64_t value)
{
	double d_value = 0.0;
	size_t number_of_bytes_to_copy =
		std::min(sizeof(double), sizeof(uint64_t));
	std::memcpy(&d_value, &value, number_of_bytes_to_copy);
	std::string ret = std::to_string(d_value);
	ret += " ";
	return ret;
}

static std::string
IntegerConstantToString(uint64_t value)
{
	int64_t signed_value = 0;
	std::memcpy(&signed_value, &value, sizeof(uint64_t));
	std::string ret = std::to_string(signed_value);
	ret += " ";
	return ret;
}

static std::string
NumberConstantToString(uint64_t value)
{
	return DoubleConstantToString(value);
}

static std::string
UnsignedConstantToString(uint64_t value)
{
	std::string ret = std::to_string(value);
	ret += " ";
	return ret;
}

static std::string
ScalarConstantToString(uint64_t value)
{
	if (value % 2 == 1) {
		return IntegerConstantToString(value);
	} else {
		return StringConstantToString(value);
	}
}

static std::string
DataTypeEnumConstantToString(DataType_DataTypeEnum type, uint64_t value)
{
	switch (type) {
	case DataType::BOOLEAN:
		return BooleanConstantToString(value);
	case DataType::DECIMAL:
		return DecimalConstantToString(value);
	case DataType::DOUBLE:
		return DoubleConstantToString(value);
	case DataType::INTEGER:
		return IntegerConstantToString(value);
	case DataType::NUMBER:
		return NumberConstantToString(value);
	case DataType::UNSIGNED:
		return UnsignedConstantToString(value);
	case DataType::UUID:
		return UUIDConstantToString(value);
	case DataType::VARBINARY:
		return VarbinaryToString(value);
	}
}

static std::string
CollatableDataTypeEnumConstantToString(
	CollatableDataType_CollatableDataTypeEnum type,
	uint64_t value)
{
	switch (type) {
	case CollatableDataType::SCALAR:
		return ScalarConstantToString(value);
	case CollatableDataType::STRING:
		return StringConstantToString(value);
	case CollatableDataType::TEXT:
		return StringConstantToString(value);
	}
}

static std::string
CollatableDataTypeConstantToString(CollatableDataType type, uint64_t value)
{
	switch (type.collatable_data_type_oneof_case()) {
	case CollatableDataType::kTypeEnum:
		return CollatableDataTypeEnumConstantToString(
			type.type_enum(), value
			);
	case CollatableDataType::kVarchar:
		return StringConstantToString(value);
	case CollatableDataType::COLLATABLE_DATA_TYPE_ONEOF_NOT_SET:
		return CollatableDataTypeEnumConstantToString(
			type.type_enum_fallback(), value
			);
	}
}

PROTO_TOSTRING(ConstantValue, constant) {
	switch (constant.type().data_type_oneof_case()) {
	case DataType::kTypeEnum:
		return DataTypeEnumConstantToString(
			constant.type().type_enum(),
			constant.value()
			);
	case DataType::kCollatableType:
		return CollatableDataTypeConstantToString(
			constant.type().collatable_type(),
			constant.value()
			);
	case DataType::kSpecialTypeEnum:
	case DataType::DATA_TYPE_ONEOF_NOT_SET:
		return DataTypeEnumConstantToString(
			constant.type().type_enum_fallback(),
			constant.value()
			);
	}
}

PROTO_TOSTRING(BinaryOperatorExpression, expression) {
	std::string ret = "(";
	ret += TermToString(expression.left_operand());

	switch (expression.binary_operator()) {
	case BinaryOperatorExpression::PLUS:
		ret += "+ ";
		break;
	case BinaryOperatorExpression::MINUS:
		ret += "- ";
		break;
	case BinaryOperatorExpression::AND:
		ret += "AND ";
		break;
	case BinaryOperatorExpression::OR:
		ret += "OR ";
		break;
	case BinaryOperatorExpression::BINARY_AND:
		ret += "& ";
		break;
	case BinaryOperatorExpression::BINARY_OR:
		ret += "| ";
		break;
	case BinaryOperatorExpression::MULTIPLY:
		ret += "* ";
		break;
	case BinaryOperatorExpression::DIVISION:
		ret += "/ ";
		break;
	case BinaryOperatorExpression::REMAINDER:
		ret += "% ";
		break;
	}

	ret += TermToString(expression.right_operand());
	ret += ") ";

	return ret;
}

PROTO_TOSTRING(UnaryOperatorExpression, expression) {
	std::string ret = "(";
	switch (expression.unary_operator()) {
	case UnaryOperatorExpression::NOT:
		ret += "!";
		ret += TermToString(expression.term());
		break;
	case UnaryOperatorExpression::PLUS:
		ret += "+";
		ret += TermToString(expression.term());
		break;
	case UnaryOperatorExpression::MINUS:
		ret += "-";
		ret += TermToString(expression.term());
		break;
	case UnaryOperatorExpression::IS_NULL:
		ret += TermToString(expression.term());
		ret += "IS NULL ";
		break;
	case UnaryOperatorExpression::IS_NOT_NULL:
		ret += TermToString(expression.term());
		ret += "IS NOT NULL ";
		break;
	case UnaryOperatorExpression::NO_OPERATOR:
		ret += TermToString(expression.term());
		break;
	}
	ret += ") ";

	return ret;
}

PROTO_TOSTRING(FunctionExpression, expression) {
	switch (expression.function_expression_oneof_case()) {
	case FunctionExpression::kBinaryOperatorExpression:
		return BinaryOperatorExpressionToString(
			expression.binary_operator_expression()
			);
	case FunctionExpression::FUNCTION_EXPRESSION_ONEOF_NOT_SET:
		return UnaryOperatorExpressionToString(
			expression.unary_operator_expression_fallback()
			);
	}
}

PROTO_TOSTRING(Term, term) {
	switch (term.term_oneof_case()) {
	case Term::kFunc:
		return FunctionExpressionToString(term.func());
	case Term::kPredicate:
		return PredicateExpressionToString(term.predicate());
	case Term::kColumnName:
		return ColumnNameToString(term.column_name());
	case Term::TERM_ONEOF_NOT_SET:
		return ConstantValueToString(term.constant_fallback());
	}
}

PROTO_TOSTRING(CompareExpression, compare) {
	std::string ret = "(";
	ret += TermToString(compare.left_operand());
	switch (compare.comparator()) {
	case CompareExpression::LESS:
		ret += "< ";
		break;
	case CompareExpression::LESS_EQUAL:
		ret += "<= ";
		break;
	case CompareExpression::EQUAL:
		ret += "= ";
		break;
	case CompareExpression::NOT_EQUAL:
		ret += "!= ";
		break;
	case CompareExpression::GREATER_EQUAL:
		ret += ">= ";
		break;
	case CompareExpression::GREATER:
		ret += "> ";
		break;
	case CompareExpression::EQUAL_EQUAL:
		ret += "== ";
		break;
	case CompareExpression::DIFFERENT:
		ret += "<> ";
		break;
	}

	ret += TermToString(compare.right_operand());
	ret += ") ";

	return ret;
}

PROTO_TOSTRING(PredicateExpression, expression) {
	switch (expression.predicate_oneof_case()) {
	case PredicateExpression::kCompare:
		return CompareExpressionToString(expression.compare());
	case PredicateExpression::PREDICATE_ONEOF_NOT_SET:
		return expression.bool_constant_fallback()
			? "TRUE " : "FALSE ";
	}
}

PROTO_TOSTRING(ReferenceForeignKeyClause, foreign_key_clause) {
	std::string ret = "REFERENCES ";

	ret += TableNameToString(foreign_key_clause.table_name());
	ret += "(";
	ret += ColumnNameToString(foreign_key_clause.column_name());
	for (int i = 0; i < foreign_key_clause.extra_column_names_size(); ++i) {
		ret += ", ";
		ret += ColumnNameToString(
			foreign_key_clause.extra_column_names(i)
			);
	}
	ret += ") ";

	if (foreign_key_clause.match_full()) {
		ret += "MATCH FULL ";
	}

	return ret;
}

PROTO_TOSTRING(NamedColumnConstraintCheck, check) {
	std::string ret = "CHECK (";
	ret += PredicateExpressionToString(check.check_expression());
	ret += ") ";
	return ret;
}

PROTO_TOSTRING(NamedColumnConstraint, named_constraint) {
	std::string ret;
	if (named_constraint.has_constraint_name()) {
		ret += "CONSTRAINT ";
		ret += ColumnConstraintNameToString(
			named_constraint.constraint_name()
			);
	}

	switch (named_constraint.constraint_oneof_case()) {
	case NamedColumnConstraint::kForeignKeyClause:
		ret += ReferenceForeignKeyClauseToString(
			named_constraint.foreign_key_clause()
			);
		break;
	case NamedColumnConstraint::kCheckExpression:
		ret += NamedColumnConstraintCheckToString(
			named_constraint.check_expression()
			);
		break;
	case NamedColumnConstraint::CONSTRAINT_ONEOF_NOT_SET:
		switch (named_constraint.enum_fallback()) {
		case NamedColumnConstraint::UNIQUE:
			ret += "UNIQUE ";
			break;
		case NamedColumnConstraint::PRIMARY_KEY:
			ret += "PRIMARY KEY ";
		}
		break;
	}
	return ret;
}

PROTO_TOSTRING(DefaultExpression, default_expression) {
	std::string ret = "DEFAULT ";
	ret += FunctionExpressionToString(default_expression.expression());
	return ret;
}

PROTO_TOSTRING(ColumnConstraint, constraint) {
	switch (constraint.column_constraint_oneof_case()) {
	case ColumnConstraint::kNullable:
		return ColumnConstraintNullableToString(
			constraint.nullable()
			);
	case ColumnConstraint::kNamedConstraint:
		return NamedColumnConstraintToString(
			constraint.named_constraint()
			);
	case ColumnConstraint::COLUMN_CONSTRAINT_ONEOF_NOT_SET:
		return DefaultExpressionToString(
			constraint.default_expression()
			);
	}
}

PROTO_TOSTRING(ColumnDefinition, def) {
	std::string ret = ColumnNameToString(def.column_name());
	ret += " ";
	ret += DataTypeToString(def.type());
	for (int i = 0; i < def.constraints_size(); ++i) {
		if (i > 0) {
			ret += ",";
		}
		ret += " ";
		ret += ColumnConstraintToString(def.constraints(i));
	}
	return ret;
}

PROTO_TOSTRING(TableConstraintPrimaryKey, primary_key) {
	std::string ret = "PRIMARY KEY ("
		+ ColumnNameToString(primary_key.column_name());
	for (int i = 0; i < primary_key.extra_column_names_size(); ++i) {
		ret += ", ";
		ret += ColumnNameToString(primary_key.extra_column_names(i));
	}
	ret += ") ";
	return ret;
}

PROTO_TOSTRING(TableConstraintUnique, unique) {
	std::string ret = "UNIQUE (" + ColumnNameToString(unique.column_name());
	for (int i = 0; i < unique.extra_column_names_size(); ++i) {
		ret += ", ";
		ret += ColumnNameToString(unique.extra_column_names(i));
	}
	ret += ") ";
	return ret;
}

PROTO_TOSTRING(TableConstraintCheck, check_expression) {
	std::string ret = "CHECK (";
	ret += PredicateExpressionToString(check_expression.predicate());
	ret += ") ";
	return ret;
}

PROTO_TOSTRING(TableConstraintForeignKeyClause, foreign_key) {
	std::string ret = "FOREIGN KEY (";
	ret += ColumnNameToString(foreign_key.column_name());
	for (int i = 0; i < foreign_key.extra_column_names_size(); ++i) {
		ret += ", ";
		ret += ColumnNameToString(foreign_key.extra_column_names(i));
	}
	ret += ") ";
	ret += ReferenceForeignKeyClauseToString(foreign_key.reference());
	return ret;
}

PROTO_TOSTRING(TableConstraint, table_constraint) {
	switch (table_constraint.table_constraint_oneof_case()) {
	case TableConstraint::kPrimaryKey:
		return TableConstraintPrimaryKeyToString(
			table_constraint.primary_key()
			);
	case TableConstraint::kCheckExpression:
		return TableConstraintCheckToString(
			table_constraint.check_expression()
			);
	case TableConstraint::kForeignKey:
		return TableConstraintForeignKeyClauseToString(
			table_constraint.foreign_key()
			);
	case TableConstraint::TABLE_CONSTRAINT_ONEOF_NOT_SET:
		return TableConstraintUniqueToString(
			table_constraint.unique_fallback()
			);
	}
}

PROTO_TOSTRING(DummyColumnDefinition, definition) {
	std::string ret = "column0 ";
	if (definition.type().data_type_oneof_case()
	    == DataType::kSpecialTypeEnum) {
		ret += DataType_DataTypeEnum_Name(
			definition.type().type_enum_fallback());
		ret += " ";
	} else {
		ret += DataTypeToString(definition.type());
	}

	ret += "PRIMARY KEY ";

	return ret;
}

PROTO_TOSTRING(Engine, engine) {
	std::string ret = "WITH ENGINE = \'";
	ret += Engine_EngineEnum_Name(engine.engine_enum());
	ret += "\' ";
	return ret;
}

static ColumnName
CreateColumnName(uint32_t code)
{
	ColumnName name;
	name.set_code(code);
	return name;
}

static std::string
ColumnDefinitionToStringWithName(const ColumnDefinition &column_def,
				 const ColumnName &name)
{
	ColumnDefinition mutable_column_def(column_def);
	*mutable_column_def.mutable_column_name() = name;
	return ColumnDefinitionToString(mutable_column_def);
}

PROTO_TOSTRING(CreateTable, create_table) {
	std::string ret("CREATE TABLE ");
	if (create_table.if_not_exists()) {
		ret += "IF NOT EXISTS ";
	}
	ret += TableNameToString(create_table.table_name());
	ret += "(";
	ret += DummyColumnDefinitionToString(create_table.dummy_definition());

	uint32_t column_count = 1;
	for (int i = 0; i < create_table.options_size(); ++i) {
		/* Inlined CreateTableOptionToString() function so columns
		 * would be named better than just randomly.
		 */
		switch (create_table.options(i).option_oneof_case()) {
		case CreateTableOption::kColumnDefinition:
			if (column_count >= kMaxColumnNumber) {
				break;
			}
			ret += ", ";
			ret += ColumnDefinitionToStringWithName(
				create_table.options(i).column_definition(),
				CreateColumnName(column_count++)
				);
			break;
		case CreateTableOption::kTableConstraint:
			ret += TableConstraintToString(
				create_table.options(i).table_constraint()
				);
		case CreateTableOption::OPTION_ONEOF_NOT_SET:
			if (column_count >= kMaxColumnNumber) {
				break;
			}
			ret += ", ";
			ret += ColumnDefinitionToStringWithName(
				create_table.options(i)
					.column_definition_fallback(),
				CreateColumnName(column_count++)
				);
			break;
		}
	}
	ret += ") ";
	if (create_table.has_engine()) {
		ret += EngineToString(create_table.engine());
	}

	return ret;
}

PROTO_TOSTRING(IndexName, name) {
	std::string ret = "index";
	ret += std::to_string(name.code() % kMaxIndexNumber);
	ret += " ";
	return ret;
}

PROTO_TOSTRING(SelectFromClauseOption1, from_clause) {
	std::string ret = TableNameToString(from_clause.table_name());

	if (from_clause.has_as_table_name()) {
		ret += "AS ";
		ret += TableNameToString(from_clause.as_table_name());
	}
	switch (from_clause.indexed_oneof_case()) {
	case SelectFromClauseOption1::kIndexedEnum:
		switch (from_clause.indexed_enum()) {
		case SelectFromClauseOption1::NOT_INDEXED:
			ret += "NOT INDEXED ";
			break;
		}
		break;
	case SelectFromClauseOption1::kIndexName:
		ret += "INDEXED BY ";
		ret += IndexNameToString(from_clause.index_name());
		break;
	case SelectFromClauseOption1::INDEXED_ONEOF_NOT_SET:
		break;
	}
	return ret;
}

PROTO_TOSTRING(SelectFromClauseOption2, option) {
	std::string ret = "(";
	ret += SelectToString(option.select_statement());
	ret += ") ";

	if (option.has_table_name()) {
		if (option.as_construction_present_flag()) {
			ret += "AS ";
		}
		ret += TableNameToString(option.table_name());
	}

	return ret;
}

PROTO_TOSTRING(LeftJoin, left_join) {
	std::string ret;
	if (left_join.natural()) {
		ret += "NATURAL ";
	}
	ret += "LEFT ";
	if (left_join.outer()) {
		ret += "OUTER ";
	}
	ret += "JOIN ";
	return ret;
}

PROTO_TOSTRING(InnerJoin, inner_join) {
	std::string ret;
	if (inner_join.natural()) {
		ret += "NATURAL ";
	}
	ret += "INNER JOIN ";
	return ret;
}

PROTO_TOSTRING(JoinOperator, join_operator) {
	switch (join_operator.join_operator_oneof_case()) {
	case JoinOperator::kLeftJoin:
		return LeftJoinToString(join_operator.left_join());
	case JoinOperator::kInnerJoin:
		return InnerJoinToString(join_operator.inner_join());
	case JoinOperator::JOIN_OPERATOR_ONEOF_NOT_SET:
		return "CROSS JOIN ";
	}
}

PROTO_TOSTRING(JoinSpecificationUsing, using_specification) {
	std::string ret = "USING ( ";
	ret += ColumnNameToString(using_specification.column_name());
	for (int i = 0;
		 i < using_specification.extra_column_names_size();
		 ++i) {
		ret += ", ";
		ret += ColumnNameToString(
			using_specification.extra_column_names(i)
			);
	}
	ret += ") ";
	return ret;
}

PROTO_TOSTRING(JoinSpecificationOnExpression, on_expr) {
	std::string ret = "ON ";
	ret += PredicateExpressionToString(on_expr.expr());
	return ret;
}

PROTO_TOSTRING(JoinSpecification, specification) {
	switch (specification.join_specification_oneof_case()) {
	case JoinSpecification::kUsingSpecification:
		return JoinSpecificationUsingToString(
			specification.using_specification()
			);
	case JoinSpecification::JOIN_SPECIFICATION_ONEOF_NOT_SET:
		return JoinSpecificationOnExpressionToString(
			specification.on_expr()
			);
	}
}

PROTO_TOSTRING(JoinedTable, joined_table) {
	std::string ret = "( ";
	ret += JoinSourceToString(joined_table.left_join_source());
	ret += JoinOperatorToString(joined_table.join_operator());
	ret += JoinSourceToString(joined_table.right_join_source()) + ") ";
	if (joined_table.has_specification()) {
		ret += JoinSpecificationToString(joined_table.specification());
	}
	return ret;
}

PROTO_TOSTRING(JoinSource, source) {
	switch (source.join_source_oneof_case()) {
	case JoinSource::kJoinedTable:
		return JoinedTableToString(source.joined_table());
	case JoinSource::JOIN_SOURCE_ONEOF_NOT_SET:
		return TableNameToString(source.table_name_fallback());
	}
}

PROTO_TOSTRING(SelectFromClause, option) {
	std::string ret = "FROM ";
	switch (option.select_from_clause_oneof_case()) {
	case SelectFromClause::kOption1:
		ret += SelectFromClauseOption1ToString(
			option.option1()
			);
		break;
	case SelectFromClause::kOption2:
		ret += SelectFromClauseOption2ToString(
			option.option2()
			);
		break;
	case SelectFromClause::SELECT_FROM_CLAUSE_ONEOF_NOT_SET:
		ret += JoinSourceToString(
			option.join_source_fallback()
			);
		break;
	}
	return ret;
}

PROTO_TOSTRING(ColumnAsExpression, as_expr) {
	std::string ret = FunctionExpressionToString(as_expr.expression());
	if (as_expr.has_column_name()) {
		ret += "AS ";
		ret += ColumnNameToString(as_expr.column_name());
	}
	return ret;
}

PROTO_TOSTRING(SelectColumn, column) {
	std::string ret;

	switch (column.column_oneof_case()) {
	case SelectColumn::kAnyColumnFromTable:
		ret = TableNameToString(column.any_column_from_table());
		ret += ".* ";
		return ret;
	case SelectColumn::kSelectColumnExpression:
		return ColumnAsExpressionToString(
			column.select_column_expression()
			);
	case SelectColumn::COLUMN_ONEOF_NOT_SET:
		switch (column.enum_fallback()) {
		case SelectColumn::COLUMN_ANY:
			return "* ";
		}
	}
}

PROTO_TOSTRING(OrderByExpression, order_by_expr) {
	std::string ret = ColumnNameToString(order_by_expr.column_name());
	switch (order_by_expr.order()) {
	case OrderByExpression::ASCENDING:
		ret += "ASC ";
		break;
	case OrderByExpression::DESCENDING:
		ret += "DESC ";
		break;
	case OrderByExpression::NONE:
		break;
	}
	return ret;
}

PROTO_TOSTRING(OrderBy, order_by) {
	std::string ret = "ORDER BY ";
	ret += OrderByExpressionToString(order_by.expr());
	for (int i = 0; i < order_by.extra_exprs_size(); ++i) {
		ret += ", ";
		ret += OrderByExpressionToString(order_by.extra_exprs(i));
	}
	return ret;
}

PROTO_TOSTRING(Limit, limit) {
	std::string ret = "LIMIT ";
	if (limit.has_offset()) {
		switch (limit.offset_symbol_enum()) {
		case Limit::OFFSET:
			ret += std::to_string(limit.limit());
			ret += " OFFSET ";
			ret += std::to_string(limit.offset());
			ret += " ";
			break;
		case Limit::COMMA:
			ret += std::to_string(limit.offset());
			ret += " , ";
			ret += std::to_string(limit.limit());
			ret += " ";
			break;
		}
	} else {
		ret += std::to_string(limit.limit());
		ret += " ";
	}
	return ret;
}

static bool
SelectColumnIsColumnAny(const SelectColumn &column)
{
	switch (column.column_oneof_case()) {
	case SelectColumn::kSelectColumnExpression:
		return false;
	case SelectColumn::kAnyColumnFromTable:
	case SelectColumn::COLUMN_ONEOF_NOT_SET:
		return true;
	}
}

static bool
SelectHasColumnAny(const Select &select)
{
	if (SelectColumnIsColumnAny(select.column())) {
		return true;
	}
	for (int i = 0; i < select.extra_columns_size(); ++i) {
		if (SelectColumnIsColumnAny(select.extra_columns(i))) {
			return true;
		}
	}
	return false;
}

PROTO_TOSTRING(SelectWhereExpression, where_expr) {
	std::string ret = "WHERE ";
	ret += PredicateExpressionToString(where_expr.expr());
	return ret;
}

PROTO_TOSTRING(SelectGroupByExpression, goup_by_expr) {
	std::string ret = "GROUP BY ";
	ret += FunctionExpressionToString(goup_by_expr.expr());
	return ret;
}

PROTO_TOSTRING(SelectHavingExpression, having_expr) {
	std::string ret = "HAVING ";
	ret += PredicateExpressionToString(having_expr.expr());
	return ret;
}

PROTO_TOSTRING(Select, select) {
	std::string ret("SELECT ");

	switch (select.option()) {
	case Select::DISTINCT:
	case Select::ALL:
		ret += Select_SelectOptionEnum_Name(select.option());
		ret += " ";
		break;
	case Select::NONE:
		break;
	}

	ret += SelectColumnToString(select.column());
	for (int i = 0; i < select.extra_columns_size(); ++i) {
		ret += ", ";
		ret += SelectColumnToString(select.extra_columns(i));
	}

	if (select.from_clause_present_flag() || SelectHasColumnAny(select)) {
		ret += SelectFromClauseToString(select.from_clause());
	}

	if (select.has_where_expr()) {
		ret += SelectWhereExpressionToString(select.where_expr());
	}

	if (select.has_group_by_exr()) {
		ret += SelectGroupByExpressionToString(select.group_by_exr());
	}

	if (select.has_having_expr()) {
		ret += SelectHavingExpressionToString(select.having_expr());
	}

	if (select.has_order_by()) {
		ret += OrderByToString(select.order_by());
	}

	if (select.has_limit()) {
		ret += LimitToString(select.limit());
	}

	return ret;
}

PROTO_TOSTRING(SQLQuery, query) {
	switch (query.query_oneof_case()) {
	case SQLQuery::kCreateTable:
		return CreateTableToString(query.create_table());
	case SQLQuery::kSelect:
		return SelectToString(query.select());
	case SQLQuery::QUERY_ONEOF_NOT_SET:
		return "";
	}
}

} /* namespace sql_fuzzer */
