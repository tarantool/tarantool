# sql: prepared statements

* **Status**: In progress
* **Start date**: 08-06-2019
* **Authors**: Nikita Pettik @korablev77 korablev@tarantool.org
* **Issues**: #2592, #3292

# Summary

Currently, there's only one unified way to execute SQL queries: local
(box.execute()) and remote (nb:connect():execute() where nb is net-box module)
methods. Both functions have the same calling interface: they accept string
of SQL statement to be executed and optionally list of parameters to be bound.
For instance:

`box.execute("SELECT * FROM t WHERE a = ?", {12})`

Local version of :execute() calls SQL query execution mechanism right
after invokation. Meanwhile remote version makes up IProto request, sends it
to the server side (with IPROTO_SQL_TEXT and IPROTO_SQL_BIND keys) and waits
for the response-result which is sent back to the client until execution of
statement is finished.

Prepared statement is a feature allowing to execute the same (parameterized)
statement repeatedly without recompilation overhead. The typical workflow
with prepared statement is as follows:

1. Preparation stage: at first statement is prepared (i.e. compiled into
   VDBE byte-code). At this moment parameters markers are left unspecified.
   Compiled statement is saved into cache and re-compiled or invalidated only
   on demand (e.g. after schema change).
   :prepare() method returns handle (id, object with :execute() method
   or whatever) which allows to execute query later. Alongside with
   handle, :prepare() may return meta-information including types of
   columns in resulting set, number of parameters and so forth.
2. Execution stage: using mentioned handle, query can be located in cache.
   In case there's any variables to be bound, they are substituted firstly.
   Then byte-code implementing query is executed in virtual machine.

Such two-stage schema has several advantages:

 - It allows to avoid query compilation overhead: it may turn out to be
   significant for queries with short run-time lifespan
   (e.g.`INSERT INTO t VALUES (?);`);
 - Prepared statements are resilient against SQL injections;
 - It is required to implement functionality of SQL drivers (e.g. support
   so called dry-run execution - prepare allows returning meta-information
   without execution overhead).

# Other vendors specifications

## MySQL

### SQL syntax [1]

```
PREPARE stmt FROM "SELECT SQRT(POW(?,2);" 
SET @a = 2 
EXECUTE stmt USING @a; 
```

As one can see, PREPARE statement creates named handle for prepared statement
object which is further passed to EXECUTE statement alongside with values
to be bound. Prepared statement can be deallocated (i.e. erased from cache)
manually with DEALLOCATE PREPARE method.

### Protocol support [2]

COM_STMT_PREPARE is a command which creates a prepared statement from
the passed query string via MySQL's binary protocol. The only argument
is a string containing SQL query. If it is succeeded, it sends response
COM_STMT_PREPARE_OK which consists of next fields (a few the least important
are omitted):
```
 - statement_id 
 - num_columns 
 - num_params 
 [optional] if num_params > 0 
    [for each parameter] 
        - parameter definition 
 [optional] if num_columns > 0 
    [for each column] 
        - column definition 
```
Parameter definition may include next properties [3]: table name,
column name, type, character set, length of fixed-length fields,
default value etc. Column definition consists of the same fields,
so in this case their bodies are unified.

To execute prepared statement protocol declares COM_STMT_EXECUTE [4]
command. It takes id of statement to be executed and list of parameters
to be bound. In case of success it returns OK_Packet [5].

Note there's no COM_STMT_PREPARE_AND_EXECUTE, i.e. protocol always
requires preparation step.

### Caching of Prepared Statements [6]

Since prepared statements are supposed to be executed several times,
the server converts the statement to an internal structure and caches that
structure to be used during execution. In MySQL cache is session local:
statements cached for one session are not accessible to other sessions.
When session is closed, all statements are discarded. Moreover, statements
are kept up to date (automatically re-compiled) in case of internal metadata
changes caused by DDL operations. To limit number of prepared statements
MySQL features `max_prepared_stmt_count` variable; setting it to 0 disables
prepared statements at all.

[1] https://dev.mysql.com/doc/refman/8.0/en/sql-syntax-prepared-statements.html  
[2] https://dev.mysql.com/doc/internals/en/com-stmt-prepare-response.html  
[3] https://dev.mysql.com/doc/internals/en/com-query-response.html#packet-Protocol::ColumnDefinition  
[4] https://dev.mysql.com/doc/internals/en/com-stmt-execute.html  
[5] https://dev.mysql.com/doc/internals/en/packet-OK_Packet.html  
[6] https://dev.mysql.com/doc/refman/5.6/en/statement-caching.html  

## PostgreSQL

### SQL Syntax [1]

```
PREPARE fooplan (int, text, bool, numeric) AS INSERT INTO foo VALUES($1, $2, $3, $4);
EXECUTE fooplan(1, 'Hunter Valley', 't', 200.00);
```

Usage is quite similar to one in MySQL: PREPARE creates named handle,
which later can be executed with EXECUTE method.

### Protocol support [2]

Each SQL command can be executed via one of two sub-protocols. First
(simple) one [3] accepts string containing SQL statement on the client
side. On the server side, this request is parsed and executed "in one
step", i.e. without preparation, binding parameters etc. This is quite
similar to our current `:execute()` behaviour. Extended version of
protocol [4] allows processing query in a series of steps: prepare,
bind and execute. Firstly, client sends a `Parse` message, which contains
SQL string and optionally name of statement to be prepared, number of
parameters and their types (format of the messages can be seen here [5]).
It is worth noting that specifying types for bindings can be quite
meaningful in scope of improving static type system. If prepared
statement is created unnamed (i.e. without specified name) it lasts only until
the next `Parse` statement creating unnamed statement. Once a prepared
statement exists, it can be readied for execution using a `Bind` message.
`Bind` request accepts the name of prepared statement, the name of the
destination portal (portal is an entry of the next stage of preparation),
and the list of values to be bound. At this stage query planning takes
place, and query plan can be cached if query is executed repeatedly. Once
portal is created, it can be executed using `Execute` message. This request
accepts name of portal and maximum result-row count (which allows suspending
execution until the next call of `Execute` and sendind produced rows
by batches - that's why it is called portal). In addition, there are several
optional request types. For instance `Describe`, which returns meta-information
of resulting set.

### Caching of Prepared Statements [6]

Prepared statements in PosgreSQL are local to session which means that they
last for the duration of the current session and a single prepared statement
cannot be used by multiple simultaneous database clients. Prepared statement
not necessarily gets to the cache:

"If a prepared statement is executed enough times, the server may
eventually decide to save and re-use a generic plan rather than
re-planning each time."

[1] https://www.postgresql.org/docs/9.3/sql-prepare.html  
[2] https://www.postgresql.org/docs/10/protocol-overview.html  
[3] https://www.postgresql.org/docs/9.3/protocol-flow.html#AEN99807  
[4] https://www.postgresql.org/docs/9.5/protocol-flow.html#PROTOCOL-FLOW-EXT-QUERY  
[5] https://www.postgresql.org/docs/9.3/protocol-message-formats.html  
[6] https://jdbc.postgresql.org/documentation/head/server-prepare.html  

## MS SQL Server

It seems that manual prepare/execute interface is obsolete since
MS Server provides automatic caching of queries based on their
text representation. For details see: 

https://dba.stackexchange.com/questions/146092/microsoft-sql-server-prepared-statements 

Still, one can use unnecessary sp_prepare/sp_execute interface:
https://docs.microsoft.com/en-us/sql/relational-databases/system-stored-procedures/sp-prepare-transact-sql?view=sql-server-2017 

# Implementation details

## Interface

Firstly, let's introduce separate method :prepare() which takes string
containing SQL statement and optionally list containing types of parameters
to be bound. Local interface:
`box.prepare("SQL statement", {array_of_bindings})`.
`box.prepare()` (and netbox analogue `nb:prepare()`) returns object comprising:
id of prepared statement (calculated as value of hash function applied to the
original text of SQL query), count of parameters to be bound, map containing
types and names of parameters, names and types of fields forming resulting set.
Handle created by local `box.prepare` function features also `:execute()` and
`:unprepare()` methods. For example:
```
params_def = {}
params_def[1] = "integer"
params_def[2] = {}
params_def[2]['@v'] = "number"
local stmt = box.prepare("SELECT a, b, c FROM t WHERE a > ? AND a < @v", params_def)
```
`:prepare()` compiles statement and saves it to the prepared statement
cache on server side and returns handle to the object representing prepared
statement on the client side.
```
tarantool> stmt
---
- stmt_id: 1307020572
  params_count: 2
  params:
  - name: '?'
    type: integer
  - name: '@v'
    type: number
  metadata:
  - name: A
  - type: integer
  - name: B
  - type: integer
  - name: C
  - type: integer
  execute: 'function: 0x010e720450'
  unprepare: 'function 0x030e430240'
...
```
To avoid breaking current interface, let's assume that all unspecified
variables have most general type ANY:
```
cn:prepare("SELECT ?;")
---
- stmt_id: 1307020572
  params_count: 1
  params:
  - name: '?'
    type: ANY
  metadata:
  - name: '?'
  - type: ANY
...
```
When bindings are about to be substituted (via `:execute()` call), they
are firstly checked to be of specified types. In case of type mismatch an
error is raised.

Remote `:prepare()` and `:unprepare()` make up IProto request with new
IPROTO_PREPARE command. In case body contains IPROTO_SQL_TEXT key, this
command is considered to be prepare request; if body comprises IPROTO_STMT_ID
key, command is supposed to mean unprepare request. What is more,
IPROTO_EXECUTE command now is overloaded in the same way: it can accept both
IPROTO_SQL_TEXT and IPROTO_STMT_ID keys. Depending on request key, execute
command may result either in casual compile-and-execute procedure or execution
of prepared statement.

## Prepared statement handle

Different vendors use different handles to identify prepared statements.
For instance, MySQL relies on sequential numeric ids; PosgreSQL gives
unique character names to each prepared statement; Cassandra uses MD5
hash of original query as a prepared statement id. In current approach it is
suggested to use numeric ids (values of hash function applied to the original
string of SQL query) as prepared statement identifiers. To not bother with
remembering ids on client side, users should operate on opaque
`:execute()` method of prepared statement's handle. For example:
```
local stmt = box.prepare("SELECT ?;") 
stmt:execute({1}) 
--- 
- rows: 
  - [1] 
```
Now call of `:execute(args)` under the hood unfolds to
`box.execute(stmt.query_id, args)` that is automatically substitutes
appropriate id of prepared statement. The same concerns `:unprepare()` method.

## Cache

There's two main approaches concerning cache implementation. First one
assumes that the prepared statement cache is session local; second one -
that there's one cache which is global to all sessions. Session local
cache allows specific queries to be kept in different places so that they
wouldn't replace each other. For instance, DML requests can be executed
and cached through the one session (`INSERT INTO t VALUES (?);`),
meanwhile data selection occurs through another one. However, in this case
sessions can't share one prepared statement object, which leads to possible
performance issues. Thus, in Tarantool SQL it is suggested to use global
holder for prepared statements. Also, it is worth mentioning that cache in
fact is is not 'cache' in terms of invalidation policy: entries are erased
from cache only on explicit unprepare requests or session's disconnect.
Moreover, after any DDL operation all prepared statements are considered to be
expired. Expired entry can't be executed without re-preparation. Size of
cache is configured by `box.cfg.sql_cache_size` handle. There's also statistics
available in box.info:sql().cache:
 - `size` is a total amount of memory consumed by prepared statements;
 - `stmt_count` is a number of prepared statements statement.

When `:execute()` method is called and entry is found in prepared
statement cache, it should be copied before execution. Cloning is
required since byte-code can be self-modified during execution.
Moreover, it allows dealing with statement duplicates, like:
```
stmt1 = box.prepare("SELECT 1;") 
stmt2 = box.prepare("SELECT 1;") 

stmt1:unprepare() 
stmt2:execute() 
```
Instead of adding query duplicates to the prepared statement cache,
reference counter of corresponding prepared statement is incremented.
Note that duplicates imply that several sessions can share one prepared
statement. When reference counter reaches zero, prepared statement is to
be deleted. What is more, copying solves another problem. During execution of
huge SELECT query which may contain yields (for instance, UDF with sleep()),
and if at the same time another session attempts at executing the same prepared
statement, it will fail (or simply result in compile-and-execute procedure)
since instance of prepared statement contains run-time atrributes (program
counter, memory cells state and so on). Finally, another session firstly can
invalidate prepared statement by executing DDL operation, and then re-compile
statement currently being executed. The last scenarion may result in
unpredictable consequences.

It is supposed that there's no auto-caching (at least now). It means
that query can get to the prepared statement cache only if explicit
`:prepare()` invokation has taken place and is invalidated only by
user request (or at the end of session).

It is worth mentioning that each prepared statement also is assigned
with schema version at the moment of its creation. If current schema version
is different from prepared statement's one, error is raised saying that
prepared statement is expired and it requires re-compilation.
