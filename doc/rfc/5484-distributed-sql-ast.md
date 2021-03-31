Request for SQL Abstract Syntax Tree data (AST)
===============================================

-   **Status**: In progress
-   **Start date**: 11-2020
-   **Authors**: Timur Safin <tsafin@tarantool.org>
-   **Issues**: #5484, #5485, #5486

Changes
-------

-   V2 – this version, updated with the post-implementation details;

-   v1 version with changed plans and more concise data structure
    definitions. Now we do not serialize to SQL for remote nodes, but
    really operate with AST and nested objects.

-   v0 initial version

SYNOPSIS
--------

``` sql
local sql = require `sqlparser`
  
-- parse, return ast, pass it back unmodified for execution
  
local ast = sql.parse [[ select * from "table" where id > 10 limit
10 ]]
assert(type(ast) == 'cdata')
local ok = sql.execute(ast)
  
-- raw access to cdata structures
assert(ffi.string(ast.sql_query) == "select * from "table" where
id > 10 limit 10 ")  
assert(ast.ast_type == ffi.C.AST_TYPE_SELECT)  
assert(type(ast.select) == 'cdata')  
assert(ast.select.op == 101)  
assert(ffi.string(ast.select.pSrc.a[0].zName) == "table") -- ¯\_(ツ)_/¯
  
-- Lua access to structurs as Lua tables via serialization  
local msgpack = require 'msgpack';
local mp_tuple= sql.serialize(ast)
  
local table = msgpack.decode(mp)
assert(table.select[1].from[1].zName == "table")
assert(table.select[1].limit["u.iValue"] == 10)
  
-- massaging tree data in the table
  
-- serialization  
local mp_to_send = msgpack.encode(tree)
  
-- networking magics ...
  
-- ... deserialization
local ast =sql.deserialize(mp_received)
sql.execute(ast)
```

Background and motivation
-------------------------

We need to approach distributed SQL as next big thing, which come
eventually. This is a long-term project, which to be approached
gradually. Here we will try to observe all necessary steps to be done
both in longer term and shorter term periods of time.

Longer terms goals will be described briefly, but shorter term goal
(extracting of AST from SQL parser) will be given in more details, we
could do it because of past experience with already developer PoC shows
us the bare minimum of Tarantool capabilities we need for this goal.

### Vocabulary:

We use standard MapReduce vocabulary when we talks about roles of
cluster nodes involved to the SQL queries processing.

|  |   |
|--|---|
| Router(s)     | The node which processes queries and send to the corresponding storage nodes for local processing. It combines/reduces resultant data and sends it back to client |
| Combiner(s)   | Depending on the aggregation complexity needs there may be several intermediate nodes which combine (aggregate) intermedate data and send it back                 |
| Storage nodes |   |

Distributed SQL scenario
------------------------

Once we move from single node case to multiple node case for SQL
execution all kinds of intra-node data exchange arise. Once we get
original SQL to the router node, it's expected that router would
preparse SQL query, (massage them appropriately) and then send some
command data to storage nodes for their local execution.   **The
question is** - what format of data should we send to storage node?

We might try to send to the storage node the compiled binary VDBE
byte-code, but it looks to be a bad idea for several reasons:

1.  Vdbe is not yet frozen and due to the technology used (lemon parser
    with on the fly generation of constants) it might vary greatly among
    various versions even for builds of same branch. \[Though, if we
    have to, we could take some extra measures to stabilize values of
    tokens generated\];
2.  But bigger problem is - different data distribution on different
    shard nodes in the cluster. Which may require follow different query
    plans used for the same SQL query. If we would generate blindly the
    single byte code for received SQL then we may degrade performance
    comparing to the case when bytecode generated locally, individually
    on each node involved, taking local statistics and applying local
    heuristics.

So at the moment simpler approach would be more preferable:

-   We simple transfer (modified) SQL query string to each of shard node
    involved;
-   Or, otherwise, we could transfer portable AST, serialized to some
    kind of binary form;

After further discussions we took 2<sup>nd</sup> approach – we use
MessagePack serialization format for transferring of AST binary data.

PoC - Mike Siomkin' distributed SQL
-----------------------------------

There is working and deployed proof-of-concept project which has been
implemented by Mike Siomkin, and which implements distributed SQL query
concept using currently available Tarantool facilities.

Note, that there are some obvious limitations though, but they further
prove the point that with relatively small efforts, some restricted SQL
subset might be processed in distributed cluster using current Tarantool
functions, and within relatively short time frame.

For parsing of SQL queries Mike's code is using SQLParser LuaRocks
(<https://github.com/tarantool/sqlparser>) module which is wrapping
HyRise SQL parser implemented in C++ 
(<https://github.com/hyrise/sql-parser>) for parsing given SQL queries,
and building abstract-syntax trees (AST).

The intermediate part between cluster controller at the Tarantool side
and SQL parser is gridql.lua module. This is gridql responsibility to
parse SQL, analyze resultant AST, and enrich it appropriately for
aggregate functions, and pagination support. *I.e. queries sent to
storage node will be different to the original SQL query*, and will be
different to the query executed by combiner/reducer node.

The used sql-parser module exports only 2 methods: parse(query), and
tostring(ast).

-   sqlparser.parse(q) uses ffi function parseSql, which wraps hyrise
    SQL parser mechanics and returns AST tree as ffi structure
    LuaSQLParseResult, which in turns, composed of series of
    LuaSQLStatement-based objects, which might be of various types (e.g.
    kStmtSelect,  kStmtImport,  kStmtInsert,  kStmtUpdate, kStmtDelete,
    etc.), each of them could be attributed different set of data,
    including LuaExpr lists of various kinds;
-   sqlparser.tostring(ast) stringifies the passed AST object;

Despite the fact that Hyrise SQL parser has *no knowledge about builtin
SQL functions* supported by Tarantool SQL, it's parsing facilities are
enough for AST tree traversal, because any known name is marked as
identifier of function, which is a good enough to start of SQL
processing in gridql module.

Local SQL execution is being done using builtin Tarantool SQL engine,
thus such lack of functions knowledge is not a problem, iff we pass
transparently SQL query down to the node processor.

Hyrise knowns all kinds of SQL queries, but at the moment gridql modules
*supports only distributed \`SELECT\`s*, and not handles any other kinds
of requests (i.e. distributed UPDATE, or DDL).

Unfortunately, gridql, at their current form, could not be published due
to heavy usage of customer specific virtual tables, but there are
promises that it's possible to generalize and simplify code, so it may
be eventually used elsewhere, far beyond its current context.  

Long-term goals
---------------

If you would look into industry precedents (to be shown in Appendixes
below) you could see that in theory, for supporting of distributed SQL
we have to have:

-   Some kind of router/proxy accepting SQL queries, which would
    prepares them to some kind of intermediate representation (or
    abstract-syntax tree);
-   Topology aware query planner will analyse parsed query AST and,
    having knowledge of data distribution, send those parsed ASTs
    (sub)queries to nodes, which keep relevant data. If there is no data
    distribution known at the time of scheduling then all cluster nodes
    might get involved (via standard Map-Combine-Reduce operation);
-   Query might get divided into nested subqueries, for which separate
    execution stages should be planned and invoked;
-   If transactions are not read only then cluster wide transaction
    manager / conflict manager might get involved for “2 phase commit”
    or alike mechanisms;
-   It would be ideal, for debugging purposes, if “distributed SQL
    module” may work even at the single-node configuration;

Possible timings for tasks of this long-term plan is unknown at the
moment, but, regardless, we believe that the nearest goal should be:

1.  SQL parser refactored to save Abstract-Syntax Tree aka AST data
    (with serialization and deserialization facilities involved);
2.  Then transaction/conflict manager should be extended with cluster
    wide transaction support, to make possible any possible next steps
    beyond simple read-only \`SELECT\`s;

2<sup>nd</sup> item is beyond current scope, and may be handled
elsewhere, here on we continue to talk about SQL specific plans;

Short-term distributed SQL plan
-------------------------------

At the moment parser, byte-code generation, and query execution are
tightly coupled in SQL engine in Tarantool code. This is a side-effect
of current architecture which is largely inherited by Tarantool from
SQLite parser. And such close coupling might become a road-blocker for
us in the longer term, when we would have to go wider, to different
nodes of cluster.

If we properly split query parser and query execution logics we may
simplify configuration, making it easier to approach distributed SQL.

-   So, for the 1st, obvious step - we would need to create a built-in
    module sqlparser which will wrap SQL parsing in parse(query) method,
    in a fashion similar to Mike Siomkin' sqlparser module above;
-   The sql.parse(query) method would need to return AST data
    structures, exported via ffi.
    -   At the moment only SELECT and VIEW queries build AST during
        parsing, although this looks as a major limitation, but it's ok
        for the 1st stage for PoC adaptation;
    -   For the 2nd stage we would need to extend AST with more SQL
        statement types, e.g. DMLs (INSERT / UPDATE / DELETE), and DDL
        (CREATE / DROP/ etc.).
    -   Worth to mention, that current AST structures as defined in the
        sqlint.h are quite similar to that used in Mike' sqlparser
        module and migration to our implementation should not require
        major rework in PoC;
-   As we build AST we may enrich returned AST nodes with information
    about built-in functions kinds and corresponding expression data
    types, which will be specific to our SQL implementation and which
    will help in router implementation of aggregation support;

### API modifications

-   In addition to currently available ways to run SQL queries via:
    1.  direct box.execute,
    2.  Or 2 step box.prepare + box.execute
    3.  We add sql.parse method, slightly similar to box.prepare, but
        which is stopping earlier in parse cycle, and which should allow
        later to execute query (similar to box.prepare + box.execute);
    4.  We add sql.execute method which generated VDBE for the AST
        passed and invoke query execution;
    5.  We add sql.serialize and sql.deserialize to serialize AST data
        into/from MessagePack format.

This refactoring with separation of parse/serialize/deserialize/execute
steps, should still maintain fully working SQL execution cycle, i.e.
with minimum code modifications all relevant SQL queries should pass
whole Tarantool SQL test suite. In particular sql-tap/e\_select.test.lua
test, which is testing various kinds of SELECT queries, should run
almost unmodified with 2 extra modes introduced:

-   Direct box.execute;
-   Sql.parse + sql.execute;
-   Sql.parse + sql.serialize + sql.deserialize + sql.execute;


```
+-----------------------------+ 
| box.execute [[ SELECT 1 ]]  | 
+-----------------------------+ 

+--------------------------------------------------------+ 
| box.prepare [[ SELECT 1 ]];    box.execute( handle );  | 
+------------------------------+-------------------------+ 

+--------------------------------------------------------------+ 
| sqlparser.parse [[ SELECT 1 ]]; sqlparser.serialize( ast );  | 
+----------------------------------------------^---------------+ 
                                               | 
                                               | 
                                               | 
                                               | 
      +----------------------------------------v------------------------+
      |  sqlparser.deserialize( messagepack ); sqlparser.execute( ast ) |
      +-----------------------------------------------------------------+
```

AST interface for external modules
----------------------------------

At the moment there is no separate SQL AST headers, which may be
included to external code and used for ffi calls, like Mike used HyRise
sqlparser headers in his LuaJIT code. We needed to extract from sqlInt.h
such publicly available interface, either manually or automatically.

Suggested approach is very similar to module api, but with few
modification in a way relevant code is being attributed and build
procedure used.

The block of code which should be retained for AST ffi interface is
marked with `/** \\cond ffi */` / `/** \\endcond ffi */` comment
lines.

Everything in between those statements go to extracted using this simple
massaging steps

-   Preprocessed to with comment retaintion (i.e. `cc –E –CC`);

-   cond ffi/endcond ffi compound blocks left extracted via sed;

-   Preprocessed again, but now with removal of white-spaces;

-   Final file with extracted blocks is converted to C/C++ array using
    txt2c;

-   This array then later used at the Tarantool initialization time for
    definition of cdef structures known for ffi (`ffi.cdef ""`). Since now
    on LuaJIT could access AST data structures.

Serialization/deserialization of AST
------------------------------------

Appendix A shows the current structures and enumerations used by SQL
parser for AST data structures.

### Serialization of AST to MessagePack

Using simple recursive approach we walk over all structures involved,
proceeding these steps:

-   call handlers for nested pointers to structures;

-   use `OUT_*` macros for writing messagepack data.

Sounds simple, isn’t it? Algorithm does not rollback in the output
buffer, thus all calculations if necessary (i.e. the number of elements
in array, with all conditions taken into account) should be done before
outputting array or map;

So for the following structure Select:

```c
struct Select {
        struct ExprList *pEList;        /* The fields of the result */
        u8 op;                  /* One of: TK_UNION TK_ALL TK_INTERSECT TK_EXCEPT */
        LogEst nSelectRow;      /* Estimated number of result rows */
        u32 selFlags;           /* Various SF_* values */
        int iLimit, iOffset;    /* Memory registers holding LIMIT & OFFSET counters */
        char zSelName[12];      /* Symbolic name of this SELECT use for debugging */
        int addrOpenEphm[2];    /* OP_OpenEphem opcodes related to this select */
        struct SrcList *pSrc;   /* The FROM clause */
        struct Expr *pWhere;    /* The WHERE clause */
        struct ExprList *pGroupBy;/* The GROUP BY clause */
        struct Expr *pHaving;   /* The HAVING clause */
        struct ExprList *pOrderBy;/* The ORDER BY clause */
        struct Select *pPrior;  /* Prior select in a compound select statement */
        struct Select *pNext;   /* Next select to the left in a compound */
        struct Expr *pLimit;    /* LIMIT expression. NULL means not used. */
        struct Expr *pOffset;   /* OFFSET expression. NULL means not used. */
        struct With *pWith;     /* WITH clause attached to this select. Or NULL. */
};

```

For its’ recursion level:

-   we open map with correct number of keys we expect to write;

-   output all data fields (i.e. op, nSelectRow, selFlags, etc.);

-   and call corresponding recursive handlers for all substructures
    (e.g. pEList, pSrc, pWhere, pGroupBy, pHaving, etc.)

So it would look like approximately:

```c
int
sql_walk_select(struct Walker *base, struct Select * p,
                const char *title)
{
…
        OUT_TUPLE_TITLE(walker->ibuf, title);

        // count number of selects in chain
        size_t n_selects = 0;
…
       OUT_ARRAY_N(ibuf, n_selects);
        while (p) {
                // estimate extra elements in map
                size_t extra = /* calculate number of not-NULL children */;
                
                OUT_MAP_N(ibuf, 8 + extra);

                // output data fields
                OUT_V(ibuf, p, op, uint);
                OUT_V(ibuf, p, nSelectRow, Xint);
                OUT_V(ibuf, p, selFlags, uint);
                …
                OUT_VA(ibuf, p, zSelName);
                OUT_V(ibuf, p, addrOpenEphm[0], Xint);
                …
                if ((rc = sql_walk_select_expr(base, p, false, "expr")))
                        goto return_error;
                if (sql_walk_expr_list(walker, p->pEList, "results"))
                        return WRC_Abort;
                if (sql_walk_expr(walker, p->pWhere, "where"))
                        return WRC_Abort;
                if (sql_walk_expr_list(walker, p->pGroupBy, "groupby"))
                        return WRC_Abort;
                if (sql_walk_expr(walker, p->pHaving, "having"))
                        return WRC_Abort;
                …
                p = p->pPrior;
        }
return_error:
        return rc & WRC_Abort;
}
```
i.e. each sql\_walk\_select would generate key “select” with value as
map (“select”:{}), inside of which we would generate array of 1 or more
items (\[{}\]), depending on number of subselects we have in SQL
statements (joins generate more than single select).

As a result of this call we will output to the ibuf buffer data in
MessagePack format, which later could be decoded as JSON like:

```json
{
   "select":[
      {
         "selFlags":0,
         "zSelName":"#1\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000",
         "iLimit":0,
         "op":101,
         "nSelectRow":0,
         "results":[
            {
…
            }
         ]
      }
   ]
}
```

> NB! In the ideal world we may use some DSL definition like Protobuff and
> its’ auto generation facilities for creation of serializer/deserializer
> code using original DSL definition. And eventually we plan to rewrite
> this code using ffi retrospection facilities, but we had some hard stack
> limit problems when we tried this approach. So we ended up using macro
> preprocessor in C as sort of DSL for our purposes. Trying to keep it
> terse and compact, and as close to original C structure definition as
> possible. We will revisit this code again the next day.

### Deserialization from MessagePack into AST

In deserializer we use recursive approach, similar to serializer as
described above. We similarly proceed MessagePack data structures from
left to right, without rollbacks, and allocate necessary data structures
wherever we find it in map.

So for the example we described above we would have simplified
deserializer as such:

```c
struct Select *
mp_decode_select(const char **data, bool subselect)
{
        // {"select": ... }

        struct span_view key = SPAN_INIT();
        IN_S(data, key);

        UNLESS_(key, "select")
                return NULL;

        Parse sParse;
…
        struct Select *pSelect = NULL;
        struct Select *p = NULL, *pPrior = NULL;

        int n_selects = EXPECT_ARRAY(data);
        for (int j = 0; j < n_selects; j++) {
                p = sqlSelectNew(…);

                if (pSelect == NULL)
                        pSelect = p;

                int n = EXPECT_MAP(data);
                for (int k = 0; k < n; k++) {
                        struct span_view key = SPAN_INIT();
                        EXPECT_KEY(data, key);
                        IN_V(data, *p, op, uint);
                        IN_V(data, *p, nSelectRow, Xint);
                        IN_V(data, *p, selFlags, uint);
                        IN_V(data, *p, iLimit, Xint);
                        IN_V(data, *p, iOffset, Xint);
                        IN_VA(data, *p, zSelName);
                        IN_V(data, *p, addrOpenEphm[0], Xint);
                        ON_(key, "results") {
                                p->pEList = mp_decode_expr_list(data);
                        }
                        else ON_(key, "where") {
                                p->pWhere = mp_decode_expr(data);
                        }
                        else ON_(key, "groupby") {
                                p->pGroupBy = mp_decode_expr_list(data);
                        }
                        else ON_(key, "having") {
                                p->pHaving = mp_decode_expr(data);
                        }
                        …
                }
                if (pPrior != NULL) {
                        pPrior->pPrior = p;
                        p->pNext = pPrior;
                }
                pPrior = p;
        }
        }
        return pSelect;
}
```
You could easily recognize code which is reading data fields via IN\_V()
macros.

Wherever we expect particular key (and then it’s value) we put guard
condition `UNLESS_` / `ON_`.

`EXPECT_MAP` is expecting to see `MP_MAP` right now, and `EXPECT_ARRAY` is
expecting to find `MP_ARRAY`. They would assert, if their expectations
would fail.

Appendix A - AST data structures
--------------------------------

Following data structures which have been extracted via
`\\cond ffi` / `\\endcond ffi`
methodology described above, and which is being embedded for ffi usage
purposes.

```c
enum field_type
{
    FIELD_TYPE_ANY = 0,
    FIELD_TYPE_UNSIGNED,
    FIELD_TYPE_STRING,
    FIELD_TYPE_NUMBER,
    FIELD_TYPE_DOUBLE,
    FIELD_TYPE_INTEGER,
    FIELD_TYPE_BOOLEAN,
    FIELD_TYPE_VARBINARY,
    FIELD_TYPE_SCALAR,
    FIELD_TYPE_DECIMAL,
    FIELD_TYPE_UUID,
    FIELD_TYPE_ARRAY,
    FIELD_TYPE_MAP,
    field_type_MAX
};
enum on_conflict_action
{
    ON_CONFLICT_ACTION_NONE = 0,
    ON_CONFLICT_ACTION_ROLLBACK,
    ON_CONFLICT_ACTION_ABORT,
    ON_CONFLICT_ACTION_FAIL,
    ON_CONFLICT_ACTION_IGNORE,
    ON_CONFLICT_ACTION_REPLACE,
    ON_CONFLICT_ACTION_DEFAULT,
    on_conflict_action_MAX
};
enum sort_order
{
    SORT_ORDER_ASC = 0,
    SORT_ORDER_DESC,
    SORT_ORDER_UNDEF,
    sort_order_MAX
};
typedef long long int sql_int64;
typedef unsigned long long int sql_uint64;
typedef sql_int64 sql_int64;
typedef sql_uint64 sql_uint64;
typedef struct sql_stmt sql_stmt;
typedef struct sql_context sql_context;
typedef struct sql sql;
typedef struct Mem sql_value;
typedef struct sql_file sql_file;
struct sql_file
{
    const struct sql_io_methods *pMethods;
};
typedef int (*sql_callback) (void *, int, char **, char **);
typedef sql_int64 i64;
typedef sql_uint64 u64;
typedef unsigned int u32;
typedef unsigned short int u16;
typedef short int i16;
typedef unsigned char u8;
typedef signed char i8;
typedef u32 tRowcnt;
typedef short int LogEst;
typedef u64 uptr;
struct AggInfo
{
    u8 directMode;
    u8 useSortingIdx;
    int sortingIdx;
    int sortingIdxPTab;
    int nSortingColumn;
    int mnReg, mxReg;
    struct ExprList *pGroupBy;
    struct AggInfo_col
    {
        struct space_def *space_def;
        int iTable;
        int iColumn;
        int iSorterColumn;
        int iMem;
        struct Expr *pExpr;
    } *aCol;
    int nColumn;
    int nAccumulator;
    struct AggInfo_func
    {
        struct Expr *pExpr;
        struct func *func;
        int iMem;
        int iDistinct;
        int reg_eph;
    } *aFunc;
    int nFunc;
};
typedef int ynVar;
struct Expr
{
    u8 op;
    union
    {
        enum field_type type;
        enum on_conflict_action on_conflict_action;
    };
    u32 flags;
    union
    {
        char *zToken;
        int iValue;
    } u;
    struct Expr *pLeft;
    struct Expr *pRight;
    union
    {
        struct ExprList *pList;
        struct Select *pSelect;
    } x;
    int nHeight;
    int iTable;
    ynVar iColumn;
    i16 iAgg;
    i16 iRightJoinTable;
    u8 op2;
    struct AggInfo *pAggInfo;
    struct space_def *space_def;
};
struct ExprList
{
    int nExpr;
    struct ExprList_item
    {
        struct Expr *pExpr;
        char *zName;
        char *zSpan;
        enum sort_order sort_order;
        union
        {
            unsigned bits;
            struct
            {
                unsigned done:1;
                unsigned bSpanIsTab:1;
                unsigned reusable:1;
            };
        };
        union
        {
            struct
            {
                u16 iOrderByCol;
                u16 iAlias;
            } x;
            int iConstExprReg;
        } u;
    } *a;
};
struct ExprSpan
{
    struct Expr *pExpr;
    const char *zStart;
    const char *zEnd;
};
struct IdList
{
    struct IdList_item
    {
        char *zName;
        int idx;
    } *a;
    int nId;
};
typedef u64 Bitmask;
struct SrcList
{
    int nSrc;
    u32 nAlloc;
    struct SrcList_item
    {
        char *zName;
        char *zAlias;
        struct space *space;
        struct Select *pSelect;
        int addrFillSub;
        int regReturn;
        int regResult;
        union
        {
            struct
            {
                u8 jointype;
                unsigned notIndexed:1;
                unsigned isIndexedBy:1;
                unsigned isTabFunc:1;
                unsigned isCorrelated:1;
                unsigned viaCoroutine:1;
                unsigned isRecursive:1;
            } fg;
            unsigned fgBits;
        };
        u8 iSelectId;
        int iCursor;
        struct Expr *pOn;
        struct IdList *pUsing;
        Bitmask colUsed;
        union
        {
            char *zIndexedBy;
            struct ExprList *pFuncArg;
        } u1;
        struct index_def *pIBIndex;
    } a[1];
};
struct Select
{
    struct ExprList *pEList;
    u8 op;
    LogEst nSelectRow;
    u32 selFlags;
    int iLimit, iOffset;
    char zSelName[12];
    int addrOpenEphm[2];
    struct SrcList *pSrc;
    struct Expr *pWhere;
    struct ExprList *pGroupBy;
    struct Expr *pHaving;
    struct ExprList *pOrderBy;
    struct Select *pPrior;
    struct Select *pNext;
    struct Expr *pLimit;
    struct Expr *pOffset;
    struct With *pWith;
};
struct SelectDest
{
    u8 eDest;
    enum field_type *dest_type;
    int iSDParm;
    int reg_eph;
    int iSdst;
    int nSdst;
    struct ExprList *pOrderBy;
};
struct sql_trigger;
enum ast_type
{
    AST_TYPE_UNDEFINED = 0,
    AST_TYPE_SELECT,
    AST_TYPE_EXPR,
    AST_TYPE_TRIGGER,
    ast_type_MAX
};
struct sql_parsed_ast
{
    const char *sql_query;
    enum ast_type ast_type;
    bool keep_ast;
    union
    {
        struct Expr *expr;
        struct Select *select;
        struct sql_trigger *trigger;
    };
};
struct With
{
    int nCte;
    struct With *pOuter;
    struct Cte
    {
        char *zName;
        struct ExprList *pCols;
        struct Select *pSelect;
        const char *zCteErr;
    } a[1];
};

```
Appendix A - Mike Siomkin' PoC for distributed SQL
--------------------------------------------------

There is working and deployed proof-of-concept project which has been
implemented by Mike Siomkin, and which implements distributed SQL query
concept using currently available Tarantool facilities.

Note, that there are some obvious limitations at the moment, but PoC
implementation proves the point that with relatively small efforts,
restricted distributed SQL processing might be implemented in current
Tarantool within relatively short time frame.

For preliminary parsing of SQL queries Mike' code is using [SQLParser
LuaRocks](https://github.com/tarantool/sqlparser) module which is
wrapping [HyRise SQL parser](https://github.com/hyrise/sql-parser)
implemented in C++ for parsing given SQL queries, and building
abstract-syntax trees (AST).

The intermediate part between cluster controller at the Tarantool side
and SQL parser is gridql.lua module. This is gridql responsibility to
parse SQL, analyze resultant AST, and enrich it appropriately for
aggregate functions, and for pagination support. *I.e. queries sent to
storage node will be different from the original SQL query*, and will be
different from the query executed by combiner/reducer node.

The used sql-parser module exports only 2 methods: parse(query), and
tostring(ast).

-   sqlparser.parse(q) uses ffi function parseSql, which wraps hyrise
    SQL parser mechanics and returns AST tree as ffi structure
    LuaSQLParseResult, which in turns, composed of series of
    LuaSQLStatement-based objects, which might be of various types (e.g.
    kStmtSelect, kStmtImport, kStmtInsert, kStmtUpdate, kStmtDelete,
    etc.), each of them could be attributed different set of data,
    including LuaExpr lists of various kinds;

-   sqlparser.tostring(ast) stringifies the passed AST object;

Despite the fact that Hyrise SQL parser has *no knowledge about built-in
SQL functions* supported by Tarantool SQL, its’ parsing capabilities are
well enough for AST tree traversal, because any known name is marked as
identifier of function, which is a good start of SQL processing in
gridql module.

Local SQL execution is being done using built-in Tarantool SQL engine,
thus such lack of functions knowledge is not a problem, iff we pass
transparently SQL query down to the node processor.

Hyrise knowns all kinds of SQL queries, but at the moment gridql modules
*supports only `SELECT`s*, and not handles any other kinds of requests
(i.e. UPDATE).

Unfortunately, gridql, at their current form, could not be published due
to heavy usage of customer specific virtual tables, but there are claims
that it is possible to generalize and simplify code, so it might be used
elsewhere beyond current context.  


