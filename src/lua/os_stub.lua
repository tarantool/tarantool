---@meta
-- luacheck: ignore 121

---The `os` module contains the functions `execute()`, `rename()`,
---`getenv()`, `remove()`, `date()`, `exit()`, `time()`, `clock()`,
---`tmpname()`, `environ()`, `setenv()`, `setlocale()`, and `difftime()`.
---Most of these functions are described in the Lua manual, Chapter 22,
---"The Operating System Library".
---
---Tarantool overrides `os.getenv()` with a safer variant and `os.exit()`
---with a graceful shutdown variant. It also adds `os.environ()` and
---`os.setenv()`.
local os = {}

---Execute a command by passing it to the shell.
---
---**Example:**
---
---```lua
---os.execute('ls -l /usr')
---```
---
---@param command string
---@return integer
function os.execute(command) end

---Rename a file or directory.
---
---**Example:**
---
---```lua
---os.rename('local', 'foreign')
---```
---
---@param old_name string
---@param new_name string
---@return boolean success
---@return string? error
---@return integer? errno
function os.rename(old_name, new_name) end

---Get an environment variable.
---
---Tarantool uses a safer variant of `os.getenv()`: it returns a value
---only if the value has a sane length.
---
---**Example:**
---
---```lua
---os.getenv('PATH')
---```
---
---@param variable_name string
---@return string?
function os.getenv(variable_name) end

---Remove a file or directory.
---
---**Example:**
---
---```lua
---os.remove('file')
---```
---
---@param name string
---@return boolean success
---@return string? error
---@return integer? errno
function os.remove(name) end

---Return a formatted date.
---
---`format` is a format string. `time` is the number of seconds since
---1970-01-01. If `time` is omitted, the current time is used.
---
---**Example:**
---
---```lua
---os.date("%A %B %d")
---```
---
---@overload fun(format: '*t', time?: integer): os.date_table
---
---@param format? string
---@param time? integer
---@return string
function os.date(format, time) end

---Exit the program. On a server instance, this stops the instance.
---
---Tarantool makes `os.exit()` exit gracefully: it runs `on_shutdown`
---triggers. Only the main thread may call `os.exit()`.
---
---@param code? integer
function os.exit(code) end

---Return the number of seconds since the epoch.
---
---**Example:**
---
---```lua
---os.time()
---```
---
---@param time? os.date_table
---@return integer
function os.time(time) end

---Return the number of CPU seconds since the program start.
---
---**Example:**
---
---```lua
---os.clock()
---```
---
---@return number
function os.clock() end

---Return a name for a temporary file.
---
---**Example:**
---
---```lua
---os.tmpname()
---```
---
---@return string
function os.tmpname() end

---Return a table containing all environment variables.
---
---**Example:**
---
---```lua
---os.environ()['TERM']
---```
---
---@return table<string, string>?
function os.environ() end

---Set an environment variable.
---
---When `variable_value` is `nil`, the variable is removed.
---
---**Example:**
---
---```lua
---os.setenv('VERSION', '99')
---```
---
---@param variable_name string
---@param variable_value? string
function os.setenv(variable_name, variable_value) end

---Change the locale.
---
---If `new_locale_string` is not specified, return the current locale.
---
---**Example:**
---
---```lua
---string.sub(os.setlocale(), 1, 20)
---```
---
---@param new_locale_string? string
---@param category? string
---@return string?
function os.setlocale(new_locale_string, category) end

---Return the number of seconds between two times.
---
---**Example:**
---
---```lua
---os.difftime(os.time(), 0)
---```
---
---@param time1 integer
---@param time2 integer
---@return number
function os.difftime(time1, time2) end

---@class os.date_table
---A table returned by `os.date('*t')`.
---@field year integer The year.
---@field month integer The month, 1-12.
---@field day integer The day of the month, 1-31.
---@field hour integer The hour, 0-23.
---@field min integer The minute, 0-59.
---@field sec integer The second, 0-59.
---@field wday integer The day of the week, 1-7, where 1 is Sunday.
---@field yday integer The day of the year, 1-366.
---@field isdst boolean Whether daylight saving time is in effect.

return os
