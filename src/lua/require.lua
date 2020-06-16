local M = {
    registry = {},
    context = {},
}

local _require = require

local function require(name)
    local registry = M.registry
    local ctx = M.context
    local prev = ctx.current
    registry[name] = registry[name] or { parents = {}, value = 0 }

    table.insert(registry[name].parents, prev)
    registry[name].value = registry[name].value + 1
    ctx.current = name

    local mod = M._require(name)

    ctx.current = prev
    return mod
end

local function usage_count(registry, module_name, cache)
    cache = cache or {}
    local module = registry[module_name]
    local counter = module.value - #module.parents
    for _, parent_module in pairs(module.parents) do
        if not cache[parent_module] then
            cache[parent_module] = usage_count(registry, parent_module, cache)
        end
        counter = counter + cache[parent_module]
    end

    cache[module_name] = cache[module_name] or counter

    return counter
end

M.usage_count = usage_count

function M.patch_require()
    M._require = M._require or _require
    _G.require = require
end

function M.reset_registry()
    M.registry = {}
end

function M.get_registry()
    return M.registry
end

return M
