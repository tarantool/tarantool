/** List of ID of pragmas. */
enum
{
	PRAGMA_COLLATION_LIST = 0,
	PRAGMA_FOREIGN_KEY_LIST,
	PRAGMA_INDEX_INFO,
	PRAGMA_INDEX_LIST,
	PRAGMA_STATS,
	PRAGMA_TABLE_INFO,
};

/**
 * Column names and types for pragmas. The type of the column is
 * the following value after its name.
 */
static const char *const pragCName[] = {
	/* Used by: table_info */
	/*   0 */ "cid",
	/*   1 */ "integer",
	/*   2 */ "name",
	/*   3 */ "text",
	/*   4 */ "type",
	/*   3 */ "text",
	/*   6 */ "notnull",
	/*   1 */ "integer",
	/*   8 */ "dflt_value",
	/*   9 */ "text",
	/*  10 */ "pk",
	/*  11 */ "integer",
	/* Used by: stats */
	/*  12 */ "table",
	/*  13 */ "text",
	/*  14 */ "index",
	/*  15 */ "text",
	/*  16 */ "width",
	/*  17 */ "integer",
	/*  18 */ "height",
	/*  19 */ "integer",
	/* Used by: index_info */
	/*  20 */ "seqno",
	/*  21 */ "integer",
	/*  22 */ "cid",
	/*  23 */ "integer",
	/*  24 */ "name",
	/*  25 */ "text",
	/*  26 */ "desc",
	/*  27 */ "integer",
	/*  28 */ "coll",
	/*  29 */ "text",
	/*  30 */ "type",
	/*  31 */ "text",
	/* Used by: index_list */
	/*  32 */ "seq",
	/*  33 */ "integer",
	/*  34 */ "name",
	/*  35 */ "text",
	/*  36 */ "unique",
	/*  37 */ "integer",
	/* Used by: collation_list */
	/*  38 */ "seq",
	/*  39 */ "integer",
	/*  40 */ "name",
	/*  41 */ "text",
	/* Used by: foreign_key_list */
	/*  42 */ "id",
	/*  43 */ "integer",
	/*  44 */ "seq",
	/*  45 */ "integer",
	/*  46 */ "table",
	/*  47 */ "text",
	/*  48 */ "from",
	/*  49 */ "text",
	/*  50 */ "to",
	/*  51 */ "text",
	/*  52 */ "on_update",
	/*  53 */ "text",
	/*  54 */ "on_delete",
	/*  55 */ "text",
	/*  56 */ "match",
	/*  57 */ "text",
};

/** Definitions of all built-in pragmas */
struct PragmaName {
	/** Name of pragma. */
	const char *const zName;
	/** Id of pragma. */
	u8 ePragTyp;
	/** Start of column names in pragCName[] */
	u8 iPragCName;
	/** Number of column names. */
	u8 nPragCName;
};

/**
 * The order of pragmas in this array is important: it has
 * to be sorted. For more info see pragma_locate function.
 */
static const struct PragmaName aPragmaName[] = {
	{"collation_list", PRAGMA_COLLATION_LIST, 38, 2},
	{"foreign_key_list", PRAGMA_FOREIGN_KEY_LIST, 42, 8},
	{"index_info", PRAGMA_INDEX_INFO, 20, 6},
	{"index_list", PRAGMA_INDEX_LIST, 32, 3},
	{"stats", PRAGMA_STATS, 12, 4},
	{"table_info", PRAGMA_TABLE_INFO, 0, 6},
};
