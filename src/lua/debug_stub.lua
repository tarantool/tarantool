-- luacheck: ignore 121 212

---The `debug` library provides an interface for debugging Lua programs.
---Tarantool adds `debug.sourcedir()`, `debug.sourcefile()` and the
---`debug.__dir__` / `debug.__file__` shortcuts.
---
---**Example:**
---
---```lua
---print(debug.sourcedir())
---print(debug.sourcefile())
---print(debug.traceback())
---```
---
---@class debug
---@field sourcefile fun(level?: integer): string? Return the source file
---    name.
---@field sourcedir fun(level?: integer): string? Return the source
---    directory name.
---@field __file__ string? The source file name.
---@field __dir__ string? The source directory name.

---@field debug fun()
---@field getfenv fun(o: any): table
---@field gethook fun(thread?: thread): any
---@field getinfo fun(thread: thread, f: integer|function, what?: any): any
---@field getlocal fun(thread: thread, f: integer, index: integer): string?
---@field getmetatable fun(object: any): table?
---@field getregistry fun(...: any): table
---@field getupvalue fun(f: function, up: integer): string?, any?
---@field getuservalue fun(u: userdata, n?: integer): any
---@field setcstacklimit fun(limit: integer): integer|boolean
---@field setfenv fun(object: any, env: table): any
---@field sethook fun(thread: thread, hook: any, mask: any, count?: integer)
---@field setlocal fun(thread: thread, level: integer, index: integer,
---    value: any): string?
---@field setmetatable fun(value: any, meta?: table): any
---@field setupvalue fun(up: integer, value: any): string?
---@field setuservalue fun(udata: userdata, value: any, n?: integer): userdata?
---@field traceback fun(thread?: thread, message?: string,
---    level?: integer): string
---@field upvalueid fun(f: function, n: integer): lightuserdata
---@field upvaluejoin fun(n1: integer, n2: integer)
local debug = {}

---Return the source file name.
---@param level? integer
---@return string?
function debug.sourcefile(level) end

---Return the source directory name.
---@param level? integer
---@return string?
function debug.sourcedir(level) end

---@type string?
debug.__file__ = nil

---@type string?
debug.__dir__ = nil

---
--- Enters an interactive mode with the user, running each string that the user
--- enters. Using simple commands and other debug facilities, the user can
--- inspect global and local variables, change their values, evaluate
--- expressions, and so on. A line containing only the word `cont` finishes this
--- function, so that the caller continues its execution.
---
--- Note that commands for `debug.debug` are not lexically nested within any
--- function, and so have no direct access to local variables.
function debug.debug() end

--- @version 5.1
---
--- Returns the environment of object `o` .
---
--- @param o any
--- @return table
--- @nodiscard
function debug.getfenv(o) end

---
--- Returns the current hook settings of the thread, as three values: the
--- current hook function, the current hook mask, and the current hook count
--- (as set by the `debug.sethook` function).
--- @param thread? thread
--- @return debuglib.Hook? hook
--- @return string mask
--- @return integer count
function debug.gethook(thread) end

---
--- Returns a table with information about a function. You can give the
--- function directly, or you can give a number as the value of `f`,
--- which means the function running at level `f` of the call stack
--- of the given thread: level 0 is the current function (`getinfo` itself);
--- level 1 is the function that called `getinfo` (except for tail calls, which
--- do not count on the stack); and so on. If `f` is a number larger than
--- the number of active functions, then `getinfo` returns **nil**.
---
--- The returned table can contain all the fields returned by `lua_getinfo`,
--- with the string `what` describing which fields to fill in. The default for
--- `what` is to get all information available, except the table of valid
--- lines. If present, the option '`f`' adds a field named `func` with the
--- function itself. If present, the option '`L`' adds a field named
--- `activelines` with the table of valid lines.
---
--- For instance, the expression `debug.getinfo(1,"n").name` returns a table
--- with a name for the current function, if a reasonable name can be found,
--- and the expression `debug.getinfo(print)` returns a table with all available
--- information about the `print` function.
--- @overload fun(f: int|function, what?: debuglib.InfoWhat):
---    debuglib.DebugInfo?
--- @param thread thread
--- @param f integer|function
--- @param what? debuglib.InfoWhat
--- @return debuglib.DebugInfo?
--- @nodiscard
function debug.getinfo(thread, f, what) end

--- @version >5.2, JIT
---
--- This function returns the name and the value of the local variable with
--- index `local` of the function at level `level f` of the stack. This function
--- accesses not only explicit local variables, but also parameters,
--- temporaries, etc.
---
--- The first parameter or local variable has index 1, and so on, following the
--- order that they are declared in the code, counting only the variables that
--- are active in the current scope of the function. Negative indices refer to
--- vararg parameters; -1 is the first vararg parameter. The function returns
--- **nil** if there is no variable with the given index, and raises an error
--- when called with a level out of range. (You can call `debug.getinfo` to
--- check whether the level is valid.)
---
--- Variable names starting with '(' (open parenthesis) represent variables with
--- no known names (internal variables such as loop control variables, and
--- variables from chunks saved without debug information).
---
--- The parameter `f` may also be a function. In that case, `getlocal` returns
--- only the name of function parameters.
--- @overload fun(f: integer, integer): string?, any?
--- @overload fun(f: function, integer): string?
--- @overload fun(thread: thread, f: function, integer): string?
--- @param thread thread
--- @param f integer
--- @param index integer
--- @return string? name
--- @return any? value
--- @nodiscard
function debug.getlocal(thread, f, index) end

---
--- Returns the metatable of the given `value` or **nil** if it does not have
--- a metatable.
--- @param object any
--- @return table?
--- @nodiscard
function debug.getmetatable(object) end

---
--- Returns the registry table.
--- @return table
--- @nodiscard
function debug.getregistry() end

---
--- This function returns the name and the value of the upvalue with index
--- `up` of the function `f`. The function returns **nil** if there is no
--- upvalue with the given index.
---
--- Variable names starting with '(' (open parenthesis) represent variables with
--- no known names (variables from chunks saved without debug information).
--- @param f function
--- @param up integer
--- @return string? name
--- @return any? value
--- @nodiscard
function debug.getupvalue(f, up) end

---
--- Returns the `n`-th user value associated to the userdata `u` plus a boolean,
--- **false** if the userdata does not have that value.
--- @version >5.4
--- @param u userdata
--- @param n? integer
--- @return any
--- @return boolean?
function debug.getuservalue(u, n) end

---
--- ### **Deprecated in `Lua 5.4.2`**
---
--- Sets a new limit for the C stack. This limit controls how deeply nested
---    calls can go in Lua, with the intent of avoiding a stack overflow.
---
--- In case of success, this function returns the old limit. In case of error,
---    it returns `false`.
---
---
--- @deprecated
--- @param limit integer
--- @return integer|boolean
function debug.setcstacklimit(limit) end

---
--- Sets the environment of the given `object` to the given `table` .
---
--- @version 5.1, JIT
--- @generic T
--- @param object T
--- @param env table
--- @return T object
function debug.setfenv(object, env) end

---
--- Sets the given function as a hook. The string `mask` and the number `count`
--- describe when the hook will be called. The string mask may have any
--- combination of the following characters, with the given meaning:
---
--- * `"c"`: the hook is called every time Lua calls a function;
--- * `"r"`: the hook is called every time Lua returns from a function;
--- * `"l"`: the hook is called every time Lua enters a new line of code.
---
--- Moreover, with a `count` different from zero, the hook is called after every
--- `count` instructions.
---
--- When called without arguments, `debug.sethook` turns off the hook.
---
--- When the hook is called, its first parameter is a string describing
--- the event that has triggered its call: `"call"`, (or `"tail
--- call"`), `"return"`, `"line"`, and `"count"`. For line events, the hook also
--- gets the new line number as its second parameter. Inside a hook, you can
--- call `getinfo` with level 2 to get more information about the running
--- function (level 0 is the `getinfo` function, and level 1 is the hook
--- function)
--- @overload fun()
--- @overload fun(thread: thread?)
--- @overload fun(thread: thread?, hook: nil)
--- @overload fun(hook: debuglib.Hook, mask: debuglib.Hookmask|string, count:
---    integer?)
--- @param thread thread
--- @param hook debuglib.Hook
--- @param mask debuglib.Hookmask | string
--- @param count? integer
function debug.sethook(thread, hook, mask, count) end

---
--- This function assigns the value `value` to the local variable with
--- index `local` of the function at level `level` of the stack. The function
--- returns **nil** if there is no local variable with the given index, and
--- raises an error when called with a `level` out of range. (You can call
--- `getinfo` to check whether the level is valid.) Otherwise, it returns the
--- name of the local variable.
--- @overload fun(level: integer, index: integer, value: any): string?
--- @param thread thread
--- @param level integer
--- @param index integer
--- @param value any
--- @return string?
function debug.setlocal(thread, level, index, value) end

---
--- Sets the metatable for the given `object` to the given `table` (which
--- can be **nil**). Returns value.
--- @generic T
--- @param value T
--- @param meta? table
--- @return T value
--- @overload fun(value: table, meta: T): T
function debug.setmetatable(value, meta) end

---
--- This function assigns the value `value` to the upvalue with index `up`
--- of the function `f`. The function returns **nil** if there is no upvalue
--- with the given index. Otherwise, it returns the name of the upvalue.
--- @param f fun(): any
--- @param up integer
--- @param value any
--- @return string?
function debug.setupvalue(f, up, value) end

--- Sets the given `value` as the `n`-th associated to the given `udata`.
--- `udata` must be a full userdata.
---
--- Returns `udata`, or **nil** if the userdata does not have that value.
--- @version >5.4
--- @param udata userdata
--- @param value any
--- @param n? integer
--- @return userdata?
function debug.setuservalue(udata, value, n) end

--- @version 5.1, JIT
---
--- Returns a string with a traceback of the call stack. An optional message
--- string is appended at the beginning of the traceback. An optional level
--- number tells at which level to start the traceback (default is 1, the
--- function calling traceback).
--- @overload fun(): string
--- @overload fun(message?: string, level?: integer): string
--- @param thread? thread
--- @param message? string
--- @param level? integer
--- @return string
function debug.traceback(thread, message, level) end

--- Returns a unique identifier (as a light userdata) for the upvalue numbered
--- `n` from the given function.
---
--- These unique identifiers allow a program to check whether different
--- closures share upvalues. Lua closures that share an upvalue (that is, that
--- access a same external local variable) will return identical ids for those
--- upvalue indices.
--- @version >5.2, JIT
--- @param f function
--- @param n integer
--- @return lightuserdata id
--- @nodiscard
function debug.upvalueid(f, n) end

---
--- Make the `n1`-th upvalue of the Lua closure f1 refer to the `n2`-th upvalue
--- of the Lua closure f2.
--- @version >5.2, JIT
--- @param f1 fun(): any
--- @param n1 integer
--- @param f2 fun(): any
--- @param n2 integer
function debug.upvaluejoin(f1, n1, f2, n2) end

return debug
