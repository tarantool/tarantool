local json = require('json')
local fio = require('fio')

-- Try to load the runtime part. When it is absent provide a stub that
-- raises a clear error message on attempt to create a component.
local ok, wasm = pcall(require, "wasm")
if not ok then
    return {
        new = function()
            error("WASM module not found. Please " ..
                  "ensure 'wasm.so' is available.")
        end,
        load_components = function()
            return {}
        end,
    }
end

local M = {}

-- {{{ Helpers

-- Replace dashes with underscores to obtain valid Lua identifiers.
local function sanitize(name)
    return name:gsub("-", "_")
end

local function read_file(path)
    local f = fio.open(path, {'O_RDONLY'})
    local content = f:read()
    f:close()
    return content
end

 local function get(t, ...)
    local v = t
    for i = 1, select('#', ...) do
        if type(v) ~= 'table' then
            return nil
        end
        v = v[select(i, ...)]
    end
    return v
end

local function as_bool(v, default)
    if v == nil then
        return default
    end
    return not not v
end

local function copy_array(arr)
    local out = {}
    for i = 1, #arr do
        out[#out+1] = arr[i]
    end
    return out
end

local function copy_map(map)
    local out = {}
    for k, v in pairs(map) do
        out[k] = v
    end
    return out
end

local function valid_str(x)
    return type(x) == 'string' and x ~= ''
end

-- }}} Helpers

-- Transform user configuration into a form accepted by the wasm module.
local function prepare_config(config)
    config = config or {}

    local cfg = {}

    -- Inherit flags
    cfg.inherit_env = as_bool(get(config, 'env', 'inherit_env'), true)
    cfg.inherit_args = as_bool(get(config, 'args', 'inherit_args'), false)
    cfg.inherit_stdin = as_bool(get(config, 'stdio', 'inherit_stdin'), true)
    cfg.inherit_stdout = as_bool(get(config, 'stdio', 'inherit_stdout'), true)
    cfg.inherit_stderr = as_bool(get(config, 'stdio', 'inherit_stderr'), true)
    cfg.inherit_network =
        as_bool(get(config, 'network','inherit_network'), false)

    -- Env/args
    cfg.env  = copy_map(get(config, 'env', 'vars') or {})
    cfg.args = copy_array(get(config, 'args', 'value') or {})

    -- Stdio paths
    local stdin_path = get(config, 'stdio', 'stdin_path')
    local stdout_path = get(config, 'stdio', 'stdout_path')
    local stderr_path = get(config, 'stdio', 'stderr_path')
    cfg.stdin = valid_str(stdin_path) and stdin_path or nil
    cfg.stdout = valid_str(stdout_path) and stdout_path or nil
    cfg.stderr = valid_str(stderr_path) and stderr_path or nil

    -- Network options
    cfg.allow_ip_name_lookup =
        as_bool(get(config, 'network', 'allow_ip_name_lookup'), false)
    cfg.allow_tcp = as_bool(get(config, 'network', 'allow_tcp'), false)
    cfg.allow_udp = as_bool(get(config, 'network', 'allow_udp'), false)
    cfg.allowed_ips = copy_array(get(config, 'network', 'allowed_ips') or {})
    cfg.allowed_ports =
        copy_array(get(config, 'network', 'allowed_ports') or {})

    -- Limits
    local mem = get(config, 'limits', 'memory_limit')
    local fuel = get(config, 'limits', 'max_instructions')
    cfg.memory_limit = type(mem) == 'number' and mem or nil
    cfg.max_instructions = type(fuel) == 'number' and fuel or nil

    -- Preopened dirs: { host_path, guest_path, perms = ... }
    cfg.preopened_dirs = {}
    local list = get(config, 'fs', 'preopened_dirs') or {}
    if type(list) == 'table' then
        for _, d in ipairs(list) do
            if type(d) == 'table' and valid_str(d.host_path)
                and valid_str(d.guest_path) then
                cfg.preopened_dirs[#cfg.preopened_dirs+1] = {
                    d.host_path,
                    d.guest_path,
                    perms = d.perms,
                }
            end
        end
    end

    return cfg
end

-- Build a callable wrapper for an exported function.
local function make_func(uid, full_name, short_name,
                         info, iface_full, iface_short)
    local wrapper = {}

    wrapper._args = info.params
    wrapper._result = info.result
    wrapper._doc = info.doc
    wrapper._interface = iface_full
    wrapper._interface_short = iface_short
    wrapper._short_name = short_name
    wrapper._full_name = full_name

    function wrapper:help()
        local args = {}
        for _, pair in ipairs(self._args or {}) do
            table.insert(args, pair[1] .. ": " .. pair[2])
        end

        local sig = string.format("%s(%s) -> %s",
            sanitize(self._short_name),
            table.concat(args, ", "),
            table.concat(self._result or {}, ", "))

        if self._doc then
            print(self._doc)
        end

        if self._interface and self._interface_short then
            print(string.format("%s\tfrom iface.%s (\"%s\")", sig,
                                self._interface_short, self._interface))
        else
            print(sig)
        end
    end

    return setmetatable(wrapper, {
        __call = function(_, _, ...)
            return wasm.call(uid, full_name, ...)
        end
    })
end

-- Create a new component instance from options.
function M:new(opts)
    assert(opts.wasm or opts.dir, "must provide either `wasm` or `dir`")

    local meta = {
        wasm_path = opts.wasm,
        world = nil,
        lang = nil,
        wit_path = nil,
        source = nil,
        component_name = opts.name,
    }

    if opts.dir then
        local tarawasm_path = fio.pathjoin(opts.dir, "tarawasm.json")
        local content = read_file(tarawasm_path)
        local parsed = assert(json.decode(content))

        meta.world = parsed.world
        meta.lang = parsed.lang
        meta.wit_path = fio.pathjoin(opts.dir, parsed.wit_path)
        meta.source = fio.pathjoin(opts.dir, parsed.src_file)
        meta.wasm_wit_path = fio.pathjoin(opts.dir, parsed.wasm_file)

        if opts.wasm then
            -- Use provided path as-is if it exists; otherwise, treat it as
            -- relative to the component directory.
            if fio.path.exists(opts.wasm) then
                meta.wasm_path = opts.wasm
            else
                meta.wasm_path = fio.pathjoin(opts.dir, opts.wasm)
            end
        else
            meta.wasm_path = fio.pathjoin(opts.dir, parsed.world .. ".wasm")
        end
    elseif not meta.wasm_path then
        error("WASM path is not resolved")
    end

    local config = prepare_config(opts.config or {})

    local uid = wasm.load(meta.wasm_path, config)
    local exports = wasm.exports(uid)

    local iface = {}
    local lookup = {}
    local name_counter = {}

    local public = {
        iface = iface
    }

    local internal = {
        __uid = uid,
        __wasm_exports = exports,
        __lookup = lookup,
        __meta = meta,
        __config = config,
        __handle = nil,
    }

    for key, value in pairs(exports) do
        if value.params then
            local sanitized = sanitize(key)
            public[sanitized] = make_func(uid, key, sanitized, value, nil, nil)
            lookup[sanitized] = public[sanitized]
        else
            local short = key:match(".*/([%w%-_]+)@") or key
            short = sanitize(short)
            if public[short] then
                name_counter[short] = (name_counter[short] or 0) + 1
                short = short .. "_" .. tostring(name_counter[short])
            end

            iface[short] = {}

            for fname, finfo in pairs(value) do
                local full = key .. "::" .. fname
                local sanitized_fname = sanitize(fname)
                local f =
                    make_func(uid, full, sanitized_fname, finfo, key, short)
                iface[short][sanitized_fname] = f
                lookup["iface." .. short .. "." .. sanitized_fname] = f
            end
        end
    end

    -- Print information about all exported functions.
    function public:help()
        print("Exported functions:\n")
        for _, f in pairs(internal.__lookup) do
            if type(f) == "table" and f.help and getmetatable(f)
                and getmetatable(f).__call then
                f:help()
            end
        end
    end

    function public:meta()
        return internal.__meta
    end

    -- Start component execution.
    function public:run()
        internal.__handle = wasm.run(internal.__uid)
    end

    -- Wait for the component started with `run` to finish.
    function public:join()
        local h = internal.__handle
        if not h then
            error("No handle to join. Did you run the component?")
        end
        return wasm.join(h)
    end

    -- Invoke multiple functions in one call.
    function public:batch(call_list)
        local batch_calls = {}

        for i, entry in ipairs(call_list) do
            local func = entry[1]
            if not getmetatable(func) or not getmetatable(func).__call
                or not func._full_name then
                error(string.format("entry[%d][1] must be a " ..
                                    "valid wasm function", i))
            end

            local found_name = func._full_name
            local args = {}
            for j = 2, #entry do
                table.insert(args, entry[j])
            end

            table.insert(batch_calls, {found_name, unpack(args)})
        end

        return wasm.batch(internal.__uid, batch_calls)
    end

    -- Remove component from registry and unload it.
    function public:drop()
        local name = internal.__meta.component_name

        if box.wasm.get(name) == nil then
            error("No such component: " .. name, 2)
        end

        wasm.drop(internal.__uid)
        box.wasm.components[name] = nil
        return true
    end

    return setmetatable(public, {
        __index = function(_, k)
            return internal[k]
        end
    })
end

-- Helper that loads all components described in configuration.
function M.load_components(components)
    local registry = {}
    if type(components) ~= 'table' then
        return registry
    end

    for name, opts in pairs(components) do
        local comp_opts = {
            name = name,
            config = opts,
        }

        if fio.path.is_dir(opts.path) then
            comp_opts.dir = opts.path
        else
            comp_opts.wasm = opts.path
        end

        local comp = M:new(comp_opts)
        registry[name] = comp
        if opts.autorun then
            comp:run()
        end
    end

    return registry
end

return M
