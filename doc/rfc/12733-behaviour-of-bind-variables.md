# Tarantool SQL: Behaviour of bind variables

* **Status**: In progress
* **Start date**: 03-11-2026
* **Authors**: Nina Rodicova @NinaRodicova nrodicova@gmail.com
* **Issues**: [#12733](https://github.com/tarantool/tarantool/issues/12733)

## Summary

New rules introduced
in the [patch](https://github.com/tarantool/tarantool/commit/bb0dd494862c20880f3b52b61f218b936d906635)
created a scenario where an SQL table could be unintentionally overwritten
with `NULL` values. This necessitated the addition of new rules that trigger
errors in such situations.
There are also plans to change the behavior of anonymous variables.
Now the first anonymous variable corresponds to the first value from the array,
the second to the second value, the n-th to the n-th value, and so on.
The change is that a `?` appearing in an SQL query immediately after `:a` will
now take on the value that follows the last preceding `?` in the array of
values, rather than the value that follows `:a` in that array.
It will also now be prohibited to mix anonymous variables with other variables.

## Background and motivation

### Bind variable types

- Named variables. Always begin with `:`, `@`, or `#`
  (Example: `:a`, `@a`, `#a` - The prefix is part of the key,
   so these are 3 different variables.)
- Numbered variables. Begin with `$`, followed by a natural number
  (Example: `$1`, `$2`, `$10`)
- Anonymous variables. Consist of a single `?`
  (Example: `?`)

### Old behaviour of bind variables

- Each named variable was mapped to a value from an array with a corresponding
  name; if the name was not found, the value is taken from an array of values
  at the index following the highest position in the SQL-query.
```
tarantool> box.execute([[SELECT $3, :a;]], {1, 2, 3, 4})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  rows:
  - [3, 4]
...

tarantool> box.execute([[SELECT :a, :a, :b, :a, :b;]], {{[':a'] = 1}, {[':b'] = 2}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: boolean
  - name: COLUMN_4
    type: boolean
  - name: COLUMN_5
    type: boolean
  rows:
  - [1, 1, 2, 1, 2]
...

tarantool> box.execute([[SELECT :a, :a, :b;]], {{[':b'] = 1}, {[':a'] = 2}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: boolean
  rows:
  - [2, 2, 1]
...

tarantool> box.execute([[SELECT :a, :a, :b;]], {1, 2})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: boolean
  rows:
  - [1, 1, 2]
...
```

- Each numbered variable ($N) was mapped to a value from an array,
  corresponding to the variable from the request with the number N.
```
tarantool> box.execute([[SELECT :a, $1;]], {123, {[':a'] = 1}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  rows:
  - [1, 1]
...

tarantool> box.execute([[select $1, :a;]], {{[':a'] = 123}})
---
- metadata:
  - name: COLUMN_1
    type: boolean
  - name: COLUMN_2
    type: integer
  rows:
  - [null, 123]
...

tarantool> box.execute([[select $2, :a;]], {321, {[':a'] = 123}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  rows:
  - [null, 123]
...

tarantool> box.execute([[select :a, $2;]], {321, {[':a'] = 123}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  rows:
  - [123, null]
...
```

- For each anonymous variable, the variable preceding it in the SQL query that
  had the highest index number was identified; the corresponding value for that
  biggest variable was then located within an array of values, and the value
  immediately following it in that array was mapped to the anonymous variable.
  If an anonymous variable was the first one in the query, it was mapped
  to the first value in the array.
```

tarantool> box.execute([[SELECT :a, ?;]], {{[':a'] = 1}, 43})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  rows:
  - [1, 43]
...

tarantool> box.execute([[SELECT ?, ?;]], {1, 43})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  rows:
  - [1, 43]
...

tarantool> box.execute([[SELECT :a, :b, :a, ?]], {{[':a'] = 1}, {[':b'] = 2}, 3})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: integer
  - name: COLUMN_4
    type: boolean
  rows:
  - [1, 2, 1, 3]
...
```

The previous rules were not intuitive. There were also bugs in the code:

For example:
```
tarantool> box.execute([[select #a, ?, :a;]], {111, {['#a'] = 222}, {[':a'] = 333}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  - name: COLUMN_3
    type: integer
  rows:
  - [222, null, 333]
...
```

### Current behaviour of bind variables

After the [patch] (https://github.com/tarantool/tarantool/commit/bb0dd494862c20880f3b52b61f218b936d906635)
the following rules apply:

- Each named variable is mapped to a corresponding value from the array.
  The value for a named variable within the array must be a map consisting
  of a name and a value.
```
tarantool> box.execute([[SELECT :a, :a, :b;]], {{[':a'] = 1}, {[':b'] = 2}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: boolean
  rows:
  - [1, 1, 2]
...
```

- If a variable from the request is not found in the array of values,
  it takes the value `NULL`.
```
tarantool> box.execute([[SELECT :a;]], {1})
---
- metadata:
  - name: COLUMN_1
    type: integer
  rows:
  - [null]
...
```

- Each numbered variable ($N) corresponds to a value from the array at index N.
```
tarantool> box.execute([[SELECT $1, $3, $2]], {4, 5, 6})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: integer
  rows:
  - [4, 6, 5]
...
```
The value of a named variable can also be accessed using the numbered variable
with the corresponding number.
For example:
```
tarantool> box.execute([[SELECT :a, $1]], {{[':a'] = 1}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  rows:
  - [1, 1]
...
```
- For each anonymous variable, the variable preceding it in the SQL query is
  identified; the corresponding value for that preceding variable is then
  located within an array of values, and the value immediately following it in
  that array is mapped to the anonymous variable. If an anonymous variable is
  the first one in the query, it is mapped to the first value in the array.
```
tarantool> box.execute([[SELECT ?, ?, ?, ?]], {1, 2, 3, 4})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: integer
  - name: COLUMN_4
    type: integer
  rows:
  - [1, 2, 3, 4]
...

tarantool> box.execute([[SELECT :a, ?;]], {{[':a'] = 1}, 43})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  rows:
  - [1, 43]
...

tarantool> box.execute([[SELECT ?, :a, ?;]], {1, {[':a'] = 5}, 2})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: integer
  rows:
  - [1, 5, 2]
...

tarantool> box.execute([[SELECT :a, :b, :a, ?, ?]], {{[':a'] = 1}, {[':b'] = 2}, 3})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: integer
  - name: COLUMN_4
    type: boolean
  - name: COLUMN_5
    type: boolean
  rows:
  - [1, 2, 1, 2, 3]
...
```

Another example:
```
tarantool> box.execute([[SELECT :a, ?, $3, ?, :b, ?, ?]], {2, {[':a'] = 1}, 3, {[':b'] = 4}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  - name: COLUMN_3
    type: integer
  - name: COLUMN_4
    type: boolean
  - name: COLUMN_5
    type: integer
  - name: COLUMN_6
    type: boolean
  - name: COLUMN_7
    type: boolean
  rows:
  - [1, 3, 3, 4, 4, null, null]
...

tarantool> box.execute([[select #a, ?, :a;]], {111, {['#a'] = 222}, {[':a'] = 333}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  - name: COLUMN_3
    type: integer
  rows:
  - [222, 333, 333]
...
```

## Detailed design

### Error when a named variable is not found in an array of values

- If a named variable from a query is not found in the array of values, an error
  will be thrown. This error is necessary to prevent the unintentional
  overwriting of tables with `NULL` values when upgrading to a newer version
  of Tarantool. Such an issue could arise because a query like
  `([[select :a]], {1})` previously resulted in the value `1` being recorded,
  rather than `NULL`.
  This error does not apply to numbered or anonymous variables (they take on
  a `NULL` value without an error if no corresponding value is found for them
  in the array).
```
tarantool> box.execute([[SELECT :a;]], {1})
---
- null
- No value for parameter ':a'
...

tarantool> box.execute([[SELECT $1;]], {})
---
- metadata:
  - name: COLUMN_1
    type: boolean
  rows:
  - [null]
...
tarantool> box.execute([[SELECT ?;]], {})
---
- metadata:
  - name: COLUMN_1
    type: boolean
  rows:
  - [null]
...
```

- An option `sql_named_var_default_null` will be introduced that
  allows this error to be disabled. In that case
  (`sql_named_var_default_null` == true) the value of `:a` becomes `NULL`.
```
tarantool> box.execute([[SELECT :a;]], {1})
---
- metadata:
  - name: COLUMN_1
    type: boolean
  rows:
  - [null]
...
```

### Error when mixing anonymous variables and variables of other types (named or numbered) in a single query

- By default, using a mix of anonymous and other types of variables in a single
  query will be prohibited (mixing numbered and named variables is permitted).
  An error will be thrown if such a mix is used. Checking for a mixture
  of variables of different types will take place at runtime.
  If an expression is checked during the prepare phase, the following situation
  may arise: a prepared statement is created, and subsequently, the ability
  to intermix variables is disabled; but the prepared statement still executes
  without any errors.
```
tarantool> box.execute([[SELECT :a, ?, $3, ?, :b, ?, ?]], {2, {[':a'] = 1}, 3, {[':b'] = 4}})
---
- null
- Mixing anonymous variables with other types of variables is not permitted
...
```

- An option (`sql_mixed_anon_vars`) will be introduced that disables this
  error. When using this option (`sql_mixed_anon_vars` == true),
  the query will work like this.
```

tarantool> box.execute([[SELECT :a, ?, $3, ?, :b, ?, ?]], {2, {[':a'] = 1}, 3, {[':b'] = 4}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  - name: COLUMN_3
    type: integer
  - name: COLUMN_4
    type: boolean
  - name: COLUMN_5
    type: integer
  - name: COLUMN_6
    type: boolean
  - name: COLUMN_7
    type: boolean
  rows:
  - [1, 2, 3, 1, 4, 3, 4]
...

tarantool> box.execute([[SELECT ?, :a, ?;]], {1, {[':a'] = 5}, 2})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: boolean
  rows:
  - [1, 5, 5]
...
```

### New behavior for anonymous variables

**Breaking change**

- New rules for anonymous variables: the first anonymous variable corresponds
  to the first value from the array, the second to the second value, the n-th
  to the n-th value, and so on.
  The change is that a `?` appearing in an SQL query immediately after `:a` will
  now take on the value that follows the last preceding `?` in the array of
  values, rather than the value that follows `:a` in that array.
  It is breaking when the mixed variables are allowed.

An example showing what the change consists of:

Suggested version:
```
tarantool> box.execute([[SELECT :a, ?, $3, ?, :b, ?, ?]], {2, {[':a'] = 1}, 3, {[':b'] = 4}})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: boolean
  - name: COLUMN_3
    type: integer
  - name: COLUMN_4
    type: boolean
  - name: COLUMN_5
    type: integer
  - name: COLUMN_6
    type: boolean
  - name: COLUMN_7
    type: boolean
  rows:
  - [1, 2, 3, 1, 4, 3, 4]
...
```
The first anonymous variable corresponds to the first element of the array: 2.
The second anonymous variable corresponds to the second element of the array: 1.
The third anonymous variable corresponds to the third element of the array: 3.
The fourth anonymous variable corresponds to the fourth element of the array: 4.

Current version:
```
  rows:
  - [1, 3, 3, 4, 4, null, null]
...
```
The first anonymous variable corresponds to the value after `:a` in the array: 3.
The second anonymous variable corresponds to the value after `$3`
in the array: 4. The third anonymous variable corresponds to the value
after `:b` in the array: `NULL`. The fourth anonymous variable corresponds
to the value after third `?` in the array: `NULL`.

Old version:
```
  rows:
  - [1, null, 3, null, 4, null, null]
...
```
The values of the first and second anonymous variables are bugs.
The third anonymous variable is matched with the value at index 6 in the array,
which is `NULL`; and the fourth anonymous variable is matched with the value
at index 7 in the array, which is also `NULL`.

Suggested version:
```
tarantool> box.execute([[SELECT ?, ?, ?, ?]], {1, 2, 3, 4})
---
- metadata:
  - name: COLUMN_1
    type: integer
  - name: COLUMN_2
    type: integer
  - name: COLUMN_3
    type: integer
  - name: COLUMN_4
    type: integer
  rows:
  - [1, 2, 3, 4]
...
```

### Fix metadata

- Fixing metadata is planned for later, as part of task #6651.

## Rationale and alternatives

### Alternative to introducing option for named variable

Alternative to introducing option for named variable is introduce the option
for all variables.

If this scenario is implemented, the following situation could arise:
disabling the error for numbered or anonymous variables also results
in the error being disabled for named variables, which could lead to
unintentional overwriting with `NULL` values.

 - If a variable of any type from a query is not found in the array of values,
   an error will be thrown.
```
tarantool> box.execute([[SELECT :a;]], {1})
---
- null
- No value for parameter ':a'
...

tarantool> box.execute([[SELECT $1;]], {})
---
- null
- No value for parameter '$1'
...

tarantool> box.execute([[SELECT ?;]], {})
---
- null
- No value for parameter '?'
...
```

- An option `sql_var_default_null` will be introduced that
  allows this error to be disabled. In that case
  (`sql_var_default_null` == true) the values of variables become `NULL`.
```
tarantool> box.execute([[SELECT :a;]], {1})
---
- metadata:
  - name: COLUMN_1
    type: boolean
  rows:
  - [null]
...

tarantool> box.execute([[SELECT $1;]], {})
---
- metadata:
  - name: COLUMN_1
    type: boolean
  rows:
  - [null]
...

tarantool> box.execute([[SELECT ?;]], {})
---
- metadata:
  - name: COLUMN_1
    type: boolean
  rows:
  - [null]
...
```

### Alternatives to changing behaviour of anonymous variables

- Retain the old behavior for anonymous variables: For each anonymous variable,
  the variable preceding it in the SQL query was identified; the corresponding
  value for that preceding variable was then located within an array of values,
  and the value immediately following it in that array was mapped
  to the anonymous variable. If an anonymous variable was the first one
  in the query, it was mapped to the first value in the array.

- This behavior is an alternative, as it is not trivial to calculate
  the position of `?`, especially if there is a named variable before the `?`.

--------------------------------------------------------------------------------

[gh-12733]: https://github.com/tarantool/tarantool/issues/12733
