struct Expr;
struct Select;
struct sql_trigger;

enum ast_type {
	AST_TYPE_UNDEFINED = 0,
	AST_TYPE_SELECT,
	AST_TYPE_EXPR,
	AST_TYPE_TRIGGER,
	ast_type_MAX
};

struct sql_parsed_ast {
	const char* sql_query; 	/**< original query */
	enum ast_type ast_type;	/**< Type of parsed_ast member. */
	bool keep_ast;		/**< Keep AST after .parse */
	union {
		struct Expr *expr;
		struct Select *select;
		struct sql_trigger *trigger;
	};
};
