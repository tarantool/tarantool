# Tarantool Wire protocol

* **Status**: In progress
* **Start date**: 04-04-2018
* **Authors**: Vladislav Shpilevoy @Gerold103 v.shpilevoy@tarantool.org, Konstantin Osipov @kostja kostja@tarantool.org, Alexey Gadzhiev @alg1973 alg1973@gmail.com
* **Issues**: [#2677](https://github.com/tarantool/tarantool/issues/2677), [#2620](https://github.com/tarantool/tarantool/issues/2620), [#2618](https://github.com/tarantool/tarantool/issues/2618)

## Summary

Tarantool wire protocol is a convention for encoding and sending execution results of SQL, Lua and C stored functions, DML (Data Manipulation Language), DDL (Data Definition Language), DQL (Data Query Language). The protocol is unified for all request types. For a single request multiple responses of different types can be sent.

## Background and motivation

Tarantool wire protocol is called **IProto**, and is used by database connectors written in different languages and accessing the database via network. The protocol describes how to distinguish different message types and what data can be stored in each message. Tarantool response messages can be of the following kind:
* A response, which represents a single reply to a request or completes a chain of replies. There are two response types of this kind: OK and ERROR. Error response is trivial, and contains an error code and message. OK response may contain useful payload, such as result set rows or metadata.
* A response which is a part of a chain of replies - a so called PUSH-message. Multiple push messages can be sent in response to a single request, but they never indicate an end of reply stream: the end is always flagged by a response of the previous kind.

In support of this response set 2 main challenges appear:
1. How to unify responses;
2. How to support multiple messages inside a single request.

A response which contains a payload can contain either data or metadata, or both. If it is necessary to share the same response metadata among multiple PUSH messages, the metadata can be assigned a numeric identifier (PUSH ID) and referenced in the stream by this identifier.

The metadata itself can contain:
* affected row count, last autoincrement column value, flags (such metadata
  is sent in response to DML statements such as INSERT/UPDATE/DELETE);
* column count, names and types (used to describe result set rows that
  follow).

To understand how a single request can produce multiple responses, consider the stored procedure (do not pay attention to the syntax - it does not matter here):
```SQL
FUNCTION my_sql_func(a1, a2, a3, a4) BEGIN
    SELECT my_lua_func(a1);
    SELECT * FROM table1;
    SELECT my_c_func(a2);
    INSERT INTO table1 VALUES (1, 2, 3);
    RETURN a4;
END;
```
, where `my_lua_func()` is the function, written in Lua and sending its own push-messages:
```Lua
function my_lua_func(arg)
    box.session.push(arg)
    return arg
end
```
and `my_c_func()` is the function, written in C and returning some raw data:
```C
int
my_c_func(box_function_ctx_t *ctx) {
    box_tuple_t *tuple;
    /* Fill the tuple with any data. */
    return box_return_tuple(ctx, tuple);
}
```
Consider each statement:
* `SELECT FROM` can split a big result set into multiple messages;
* `SELECT my_lua_func()` produces 2 messages: one is the push-message generated in `my_lua_func` and another is the result of `SELECT` itself;
* `INSERT` creates 1 message with metadata;
* `RETURN` creates a final response message.

Of course, some of messages, or even all of them can be batched and sent as a single TCP packet.

In the next section we specify code names and messages used by the protocol.

For the protocol details - code values, all header and body keys - see Tarantool [website](https://tarantool.io/).

## Detailed design

Tarantool response consists of a body and a header. Header is used to store response code and some internal metainfo such as schema version, request id (called **sync** in Tarantool). Body is used to store result data and request-dependent metainfo.

### Header

There are 3 response codes in header:
* `IPROTO_OK` - the terminal response to a successful request;
* `IPROTO_ERROR | error code` - the terminal response to a request, which ended with an error.
* `IPROTO_PUSH` - non-final response. One request can generate multuple PUSH messages;

`IPROTO_ERROR` response is trivial, and consists just of code and message.

`IPROTO_OK` and `IPROTO_PUSH` have the same body format. But
1. `IPROTO_OK` finalizes a request;
2. `IPROTO_PUSH` can have `IPROTO_PUSH_ID` field in the header, that allows to build a chain of pushes with the same `ID`. Absense of this field means, that the push is not a part of a chain. All pushes which are part of the same chain (are identified by the same id) should share chain metadata.

### Body

The common body structure:
```
+----------------------------------------------+
| IPROTO_BODY: {                               |
|     IPROTO_METADATA: [                       |
|         {                                    |
|             IPROTO_FIELD_NAME: string,       |
|             IPROTO_FIELD_TYPE: number,       |
|             IPROTO_FIELD_FLAGS: number,      |
|         },                                   |
|         ...                                  |
|     ],                                       |
|                                              |
|     IPROTO_SQL_INFO: {                       |
|         SQL_INFO_ROW_COUNT: number,          |
|         SQL_INFO_LAST_ID: number,            |
|         ...                                  |
|     },                                       |
|                                              |
|     IPROTO_DATA: [                           |
|         tuple/scalar,                        |
|         ...                                  |
|     ]                                        |
| }                                            |
+----------------------------------------------+
```

Consider, how different responses use the body, and how they can be distinguished.

_A non formatted response_ has only `IPROTO_DATA` key in a body. It is the result of Lua and C DML, DDL, DQL, stored procedures calls, push messages. Such response is never linked with the next or previous messages of the same request.

_A non formatted response with metadata_ has only `IPROTO_SQL_INFO` and it is always a result of DDL/DML executed via SQL. As well as the previous type, this response is independent from other messages in the stream.

_A formatted response_ always has `IPROTO_DATA`, and can have both `IPROTO_SQL_INFO` and `IPROTO_METADATA`. It is a result of SQL DQL (`SELECT`) or SQL DML (`INSERT`). The response can be part of a stream. The first message of the stream always contains `IPROTO_METADATA` in the body and sets `IPROTO_PUSH_ID` in the header, should there be multiple messages sharing the same metadata. All other messasges in the stream contain `IPROTO_PUSH_ID` with the same value.

On the picture the state machine of the protocol is showed:
![alt text](https://raw.githubusercontent.com/tarantool/tarantool/6b171db229cd6f6acf74599039879e6f5acf35db/doc/rfc/3328-wire_protocol_img1.svg?sanitize=true)

For a call to `FUNCTION my_sql_func` the following responses are sent:
```
/* Push from my_lua_func(a1). */
+----------------------------------------------+
| HEADER: IPROTO_PUSH                          |
+- - - - - - - - - - - - - - - - - - - - - - - +
| BODY: {                                      |
|     IPROTO_DATA: [ a1 ]                      |
| }                                            |
+----------------------------------------------+

/* Result of SELECT my_lua_func(a1). */
+----------------------------------------------+
| HEADER: IPROTO_PUSH                          |
+- - - - - - - - - - - - - - - - - - - - - - - +
| BODY: {                                      |
|     IPROTO_DATA: [ [ a1 ] ],                 |
|     IPROTO_METADATA: [                       |
|         { /* field name, type ... */ }       |
|     ]                                        |
| }                                            |
+----------------------------------------------+

/* First push of SELECT * FROM table1. */
+----------------------------------------------+
| HEADER: IPROTO_PUSH, IPROTO_PUSH_ID = <id1>  |
+- - - - - - - - - - - - - - - - - - - - - - - +
| BODY: {                                      |
|    IPROTO_DATA: [ tuple1, tuple2, ... ]      |
|    IPROTO_METADATA: [                        |
|        { /* field1 name, type ... */ },      |
|        { /* field2 name, type ... */ },      |
|        ...                                   |
|    ]                                         |
| }                                            |
+----------------------------------------------+

    /* From second to last push. */
    +----------------------------------------------+
    | HEADER: IPROTO_PUSH, IPROTO_PUSH_ID = <id1>  |
    +- - - - - - - - - - - - - - - - - - - - - - - +
    | BODY: {                                      |
    |    IPROTO_DATA: [ tuple1, tuple2, ... ]      |
    | }                                            |
    +----------------------------------------------+

/* Result of SELECT my_c_func(a2). */
+----------------------------------------------+
| HEADER: IPROTO_PUSH                          |
+- - - - - - - - - - - - - - - - - - - - - - - +
| BODY: {                                      |
|     IPROTO_DATA: [ [ tuple ] ],              |
|     IPROTO_METADATA: [                       |
|         { /* field name, type ... */ }       |
|     ]                                        |
| }                                            |
+----------------------------------------------+

/* Result of INSERT INTO table1 VALUES (1, 2, 3). */
+----------------------------------------------+
| HEADER: IPROTO_PUSH                          |
+- - - - - - - - - - - - - - - - - - - - - - - +
| BODY: {                                      |
|     IPROTO_SQL_INFO: {                       |
|         SQL_INFO_ROW_COUNT: number,          |
|         SQL_INFO_LAST_ID: number,            |
|     }                                        |
| }                                            |
+----------------------------------------------+

/* Result of RETURN a4 */
+----------------------------------------------+
| HEADER: IPROTO_OK                            |
+- - - - - - - - - - - - - - - - - - - - - - - +
| BODY: {                                      |
|     IPROTO_DATA: [ a4 ]                      |
| }                                            |
+----------------------------------------------+
```

## Rationale and alternatives

Another way to link pushes together exists, replacing `IPROTO_PUSH_ID`.
Pushes can be linked via flag in a header: `IPROTO_FLAG_IS_CHAIN`, that would be stored in `IPROTO_FLAGS` header value. When a multiple messages form a chain, all of them except last one contain this flag. For example:
```
IPROTO_PUSH
    |
IPROTO_PUSH, IS_CHAIN
    |
    +--IPROTO_PUSH, IS_CHAIN
    |
    +--IPROTO_PUSH, IS_CHAIN
    |
    +--IPROTO_PUSH
    |
IPROTO_PUSH
    |
...
    |
IPROTO_OK/ERROR
```

It is slightly simpler than `PUSH_ID`, but
1. Does not enable to mix parts of different chains, if it will be needed sometimes;
2. The last response does not contain `IS_CHAIN`, but it is actually a part of chain. `IS_CHAIN` can not be stored in the last response, because else it will not be distinguishable from the next chain. This can be solved by renaming `IS_CHAIN` to `HAS_NEXT_CHAIN` or something, but `PUSH_ID` seems better - it has no these problems, and is more scalable.
