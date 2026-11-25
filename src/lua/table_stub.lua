-- luacheck: ignore 121 212

---The `table` module has everything in the standard Lua table library, and
---some Tarantool extensions.
---
---Write `table` to see the list of functions:
---
---* `clear` (LuaJIT extension = erase all elements)
---* `concat` (concatenate)
---* `copy` (make a copy of an array)
---* `deepcopy` (see below)
---* `foreach`
---* `foreachi`
---* `getn` (get the number of elements in an array)
---* `insert` (insert an element into an array)
---* `maxn` (get the largest index)
---* `move` (move elements between tables)
---* `new` (LuaJIT extension = return a new table with pre-allocated
---  elements)
---* `remove` (remove an element from an array)
---* `sort` (sort the elements of an array)
---
---In this section we describe the additional functions added by Tarantool:
---`copy`, `deepcopy`, and `equals`.
---
---@class table
---The Tarantool extensions of the `table` module.
---
---@field copy fun(input_table: table): table @Copy a table at the top level.
---
---    Supports the `__copy` metamethod for copying custom tables with
---    metatables.
---@field deepcopy fun(input_table: table): table
---@field sort fun(input_table: table,
---    comparison_function?: fun(a: any, b: any): boolean): nil
---@field equals fun(a: table, b: table): boolean @Compare two Lua tables.
---
---    Supports the `__eq` metamethod for comparing custom tables with
---    metatables.

local table = {}

---Return a "deep" copy of the table -- a copy which follows nested
---structures to any depth and does not depend on pointers, it copies the
---contents.
---
---Supports the `__copy` metamethod for copying custom tables with
---metatables.
---
---**Example:**
---
--- ```lua
--- local input_table = {1, {'a', 'b'}}
--- local output_table = table.deepcopy(input_table)
--- ```
---
---@param orig table the table to copy.
---@return table result the copy of the table.
function table.deepcopy(orig) end

---Copy any table, only the top level.
---
---Supports the `__copy` metamethod for copying custom tables with
---metatables.
---
---@param orig table the table to copy.
---@return table result the copy of the table.
function table.copy(orig) end

---Compare two Lua tables.
---
---Supports the `__eq` metamethod for comparing custom tables with
---metatables.
---
---**Example:**
---
--- ```lua
--- table.equals({1, 2}, {1, 2}) -- true
--- ```
---
---@param a table a table to compare.
---@param b table a table to compare with.
---@return boolean result true when the two tables are equal, false
---    otherwise.
function table.equals(a, b) end

---Sort the input table in place.
---
---The standard `table.sort` uses `a < b` as the default comparison
---function. It is not suitable for tables with nils, mixed types, or
---language-aware string sorting. The `tarantool_sort()` helper below sorts
---a table using a temporary Tarantool space.
---
---**Example (`tarantool_sort`):**
---
---```lua
---function tarantool_sort(input_table, collation)
---    local c = collation or 'binary'
---    local tmp_name = 'Temporary_for_tarantool_sort'
---    pcall(function() box.space[tmp_name]:drop() end)
---    box.schema.space.create(tmp_name, {temporary = true})
---    box.space[tmp_name]:create_index('I')
---    box.space[tmp_name]:create_index('I2',
---                                     {unique = false,
---                                      type = 'tree',
---                                      parts = {{2, 'scalar',
---                                                collation = c,
---                                                is_nullable = true}}})
---    for i = 1, table.maxn(input_table) do
---        box.space[tmp_name]:insert{i, input_table[i]}
---    end
---    local t = box.space[tmp_name].index.I2:select()
---    for i = 1, table.maxn(input_table) do
---        input_table[i] = t[i][2]
---    end
---    box.space[tmp_name]:drop()
---end
---```
---@param input_table table
---@param comparison_function? fun(a: any, b: any): boolean
function table.sort(input_table, comparison_function) end

---@param list table
---@param sep? string
---@param i? integer
---@param j? integer
---@return string
function table.concat(list, sep, i, j) end

---@param nseq integer
---@param nrec integer
---@return table
function table.create(nseq, nrec) end

---@param list table
---@param pos integer
---@param value any
function table.insert(list, pos, value) end

---@param tbl table
---@return number
function table.maxn(tbl) end

---@param a1 table
---@param f integer
---@param e integer
---@param t integer
---@param a2? table
---@return table
function table.move(a1, f, e, t, a2) end

---@param ... any
---@return table
function table.pack(...) end

---@param list table
---@param pos? integer
---@return any
function table.remove(list, pos) end

---@param list table
---@param i? integer
---@param j? integer
---@return any ...
function table.unpack(list, i, j) end

return table
