---@meta

---# Builtin `tarantool` module
---
---The `tarantool` module answers questions about how the Tarantool server
---was built: flags, compiler, build target, version, pid and uptime.
---
---The `tarantool.build` table contains:
---
---* `build.target` - the build target.
---* `build.options` - the CMake options.
---* `build.compiler` - the compiler used for the build.
---* `build.mod_format` - the module file extension.
---* `build.flags` - the compiler flags.
---* `build.linking` - `'static'` or `'dynamic'`.
---* `build.tzdata_version` - the bundled tzdata version.
---* `build.asan` - whether the build has AddressSanitizer enabled.
---* `build.test_build` - whether this is a test build.
---* `build.mimalloc` - the mimalloc scope: `'cpp'` or `'none'`.

---@class tarantool.build
---@field target string The build target.
---@field options string The CMake options.
---@field compiler string The compiler used for the build.
---@field mod_format string The module file extension.
---@field flags string The compiler flags.
---@field linking string 'static' or 'dynamic'.
---@field tzdata_version string The bundled tzdata version.
---@field asan boolean Whether the build has AddressSanitizer enabled.
---@field test_build boolean Whether this is a test build.
---@field mimalloc string The mimalloc scope: 'cpp' or 'none'.

---@class tarantool.debug
---@field getsources fun(modname: string): string? Return the source
---    code of a built-in module.

---@class tarantool
---@field version string The Tarantool version.
---@field ["package"] string The package name.
---@field build tarantool.build Build information.
---@field debug tarantool.debug Debug information.
---@field pid fun(): integer Return the process ID.
---@field uptime fun(): number Return the server uptime in seconds.

local tarantool = {}

---Return the process ID.
---
---@return integer
function tarantool.pid() end

---Return the server uptime in seconds.
---
---@return number
function tarantool.uptime() end

---The Tarantool version.
---@type string
tarantool.version = ''

---The package name.
---@type string
tarantool.package = ''

---Build information.
---@type tarantool.build
tarantool.build = {}

---Debug information.
---@type tarantool.debug
tarantool.debug = {}

return tarantool
