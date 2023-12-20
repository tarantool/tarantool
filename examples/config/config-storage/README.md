# Role description

To store the cluster configuration in Tarantool, the built-in
`config.storage` role is provided.

## Table of contents

* [Practical usage](#practical-usage)
* [Data model](#data-model)
* [Operations](#operations)
  * [Put](#put)
  * [Get](#get)
  * [Delete](#delete)
  * [Transactions](#transactions)
* [Watchers](#watchers)
  * [Path updates](#path-updates)
  * [Server info](#server-info)

### Practical usage

The role operates with sync spaces and assumes the configured
synchronous replication.

Each server monitors it's status in the cluster. It can be one of:
* `connected`, when someone from quorum is available to the server.
* `disconnected`, if the server lost the connection with quorum.

The status is stored in `config.storage.info` as `status`.
Each change broadcasts the same name event, associated with this table.
This is a functional table, so it can be obtained using a call.

You can configure the interval of the status checks using the role
configuration:
```yml
roles_cfg:
  config.storage:
    status_check_interval: 3
```

By default, this interval is 5 seconds.

Despite replication, the provided operations are linearizable, achieved
through use the MVCC transaction manager. This means that if you successfully
performed a write operation on the leader, you can immediately read this data
from any server in the cluster that is in the `connected` state.

Each synchronous operation has a timeout equals to
`box.cfg.replication_synchro_timeout` and can be configured in the cluster
config.

There is an [example](./config.yaml) of a cluster config for
setting up a cluster using the role.

Notes:
* The role is activated by the line `roles: [config.storage]`.
* The role if configured with `roles_cfg`.
* The RAFT based leader instance election is used. There is one
leader instance.
* The MVCC transaction engine is used. It's essential for providing
linearizability.

### Data model

The role provides key-value storage and operates with values
associated with filesystem style paths. There is global counter
named `revision`, which increases whenever an write operation is
performed, regardless of whether it modifies the data.

The data corresponding to the path contains the following fields:
* `path`: well-formed path string, has the form `/s_1/s_2/.../s_n`, where each of `s_i` is non empty and does not contain `/`.
* `value`: any string.
* `mod_revision`: unsigned number, last `revision` at which this
value modification occurred.

### Operations

Each operation returns a response that contains:
* `revision`: the revision after performing the operation.
* `data`: an operation specific payload.

If an error occurs, it is explicitly raised using `error`.

Some operations may accept a path prefix instead of the path
as `path` argument. The prefix is distinguished by ending with
`/`. For example: `/foo/bar` is a path, `/foo/bar/` is a prefix. In this case, the operation will be performed for
values whose path has the corresponding prefix. The `/` prefix
means the operation will be performed for all values.

#### Put

To put the value by the path use
`config.storage.put(path, value)`, where:

* `path`: string, path.
* `value`: string.

```lua
config.storage.put('/foo/bar', 'v1')
---
- revision: 1
...

config.storage.put('/foo/bar', 'v2')
---
- revision: 2
...

config.storage.put('/foo/bar', 'v2')
---
- revision: 3
...
```

#### Get

To get the value data by the path use `config.storage.get(path)`,
where
* `path`: string, path or prefix.

Returns the list of the values data in the `data` field.

```lua
-- Put data.
config.storage.put('/a', 'v1')
config.storage.put('/a/b', 'v2')
config.storage.put('/a/b/c','v3')
config.storage.put('/ab', 'v4')

-- Get by path.
config.storage.get('/a')
---
- data:
  - path: /a
    mod_revision: 4
    value: v1
  revision: 7
...

-- Get by prefix.
config.storage.get('/a/')
---
- data:
  - path: /a/b
    mod_revision: 5
    value: v2
  - path: /a/b/c
    mod_revision: 6
    value: v3
  revision: 7
...

-- Get all values.
config.storage.get('/')
---
- data:
  - path: /a
    mod_revision: 4
    value: v1
  - path: /a/b
    mod_revision: 5
    value: v2
  - path: /a/b/c
    mod_revision: 6
    value: v3
  - path: /ab
    mod_revision: 7
    value: v4
  revision: 7
...
```

#### Delete

To delete the value by the path use
`config.storage.delete(path)`, where:
* `path`: string, path or prefix.

Returns the list of the deleted values data as the `data` field.

```lua
config.storage.put('/a', 'v1')
config.storage.put('/b', 'v2')

-- Delete value.
config.storage.delete('/a')
---
- data:
  - path: /a
    mod_revision: 4
    value: v1
  revision: 6
...

config.storage.delete('/a')
---
- data: []
  revision: 7
...

-- Delete all values.
config.storage.delete('/')
---
- data:
  - path: /b
    mod_revision: 5
    value: v2
  revision: 8
...
```

#### Transactions

* `config.storage.txn(request)` is an atomic `if/then/else`
construct over the storage.

Request is a table that contains:
* `predicates`: optional list of the predicates to check
  - if all predicates in the list evaluates to `true` then
  `on_success` operations are executed.
  - otherwise `on_failure` operations are executed.
* `on_success`: optional list with operations to execute.
* `on_failure`: optional list with operations to execute.

Each operation is a list that contains `cmd` string as the
first element and arguments. For example:
```lua
{'put', '/foo/bar', 'v1'}
{'delete', '/a/'}
```

Each predicate is compare predicate. It must be a list that
contains:
```lua
{target, operator, value[, path]}
```

* `target`: string, `revision` / `mod_revision` / `value` / `count`.
* `operator`: string, `eq` / `ne` / `gt` / `lt` / `ge` / `le` or
symbolic equivalents: `==`, `!=`, `>`, etc.
* `value`: unsigned or string, value to compare.
* `path`: optional string, can be path with `mod_revision` and `value`
target, or path/prefix with `count` target.

Response contains the following as `data`:
* `is_success`: boolean, whether the predicate evaluated to `true`.
* `responses`: the list of responses for all operations. It contains
the `data` field of the each operation response.

Notes:
* transcation is executed atomically and in isolation with all
other operations.
* operations inside transcation are executed sequentially.
* `revision` changes one time at the end of the transaction.
* if an error occurs the transcation is rolled back.
* attempting to compare `mod_revision` or `value` of a non-existing
path results in an error.
* nested `txn` are not supported.

```lua
local revision = config.storage.get('/').revision

-- Revision based compare.
config.storage.txn({
    predicates = {{'revision', '==', revision}},
    on_success = {{'put', '/a', 'v1'}}
})
---
- data:
    responses:
    - []
    is_success: true
  revision: 8
...

config.storage.get('/a')
---
- data:
    path: /a
    mod_revision: 8
    value: v1
  revision: 8
...

-- On failure operation. `count` can be
-- used to check for existence.
config.storage.txn({
    predicates = {{'count', '==', 0, '/a'}},
    on_failure = {{'delete', '/a'}},
})
---
- data:
    responses:
    - - path: /a
        mod_revision: 8
        value: v1
    is_success: false
  revision: 9
...

-- Responses data.
config.storage.put('/a', 'v')
config.storage.txn({
    predicates = {{'value', '==', 'v0', '/a'}},
    on_success = {{'put', '/a', 'v1'}},
    on_failure = {{'get', '/a'}}
})
---
- data:
    responses:
    - - path: /a
        mod_revision: 10
        value: v
    is_success: false
  revision: 10
...
```

### Watchers

#### Path updates

You can subsribe to all path or prefix changes. To do it call
`watch(config.storage:<path/prefix>)` on the Tarantool connection. For example:

* `conn:watch('config.storage:/foo/bar')` to subscribe to all
changes by the `/foo/bar` path.
* `conn:watch('config.storage:/foo/bar/')` to subscribe to all
changes by the `/foo/bar/` prefix. For example, `/foo/bar/file`, but no `/foo/bar` and `/foo`.

In response to watch, with each change you will receive the current
`revision` at the moment the path is modified.

#### Server info

You can subscribe to `config.storage.info` changes. In response to watch
with each change you will receive a new table.
